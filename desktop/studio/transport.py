"""PC setup HID protocol. No console authentication payload is exposed."""
from pathlib import Path
import struct
import threading
import time

from .settings import Settings, SIZE, VERSION, FLASH_BYTES

VID, PID = 0xCAFE, 0x4029
OP_INFO, OP_READ, OP_WRITE, OP_APPLY, OP_DEVICE, OP_AXIS, OP_TELEMETRY, OP_REBOOT, OP_PROFILE, OP_FORCE = range(1, 11)
ERRORS = {
    1: "The Pico rejected this request. Its firmware may need updating.",
    2: "The Pico rejected the settings. Check the pedal calibration and force curve.",
    3: "The settings transfer was incomplete. Reconnect the Pico and try again.",
    4: "The Pico could not save its settings. Reconnect it and try again.",
    5: "This device is no longer connected.",
    6: "The wheel is not ready. Wait for calibration to finish, then try again.",
}


class Adapter:
    def __init__(self, hid_module=None):
        if hid_module is None:
            import hid
            hid_module = hid
        self.hid = hid_module
        self.handle = None
        self.lock = threading.RLock()
        self.sequence = 0
        self.info = None

    def scan(self):
        found = []
        for d in self.hid.enumerate():
            mode = "setup" if (d["vendor_id"], d["product_id"]) == (VID, PID) else (
                "console" if (d["vendor_id"], d["product_id"], d.get("usage_page")) == (0x046D, 0xC260, 0xFFF0) else None)
            if mode:
                found.append({"path": d["path"].hex(), "mode": mode,
                              "name": d.get("product_string") or "G27 adapter"})
        return found

    def close(self):
        with self.lock:
            if self.handle:
                self.handle.close()
            self.handle = None; self.info = None

    def open(self, path: str):
        with self.lock:
            self.close()
            allowed = {d["path"]: d for d in self.scan()}
            if path not in allowed or allowed[path]["mode"] != "setup":
                raise ValueError("Choose a Pico in setup mode.")
            handle = self.hid.device()
            try:
                handle.open_path(bytes.fromhex(path)); self.handle = handle
                data = self.request(OP_INFO)
                version, size, flash, generation, major, minor, profile, wheel = struct.unpack("<HHIIBBBB", data)
                if (version, size, flash) != (VERSION, SIZE, FLASH_BYTES):
                    raise ValueError("This Pico's firmware is incompatible with this G27 Studio release.")
                self.info = {"firmware": f"{major}.{minor}", "generation": generation, "profile": profile, "wheel": bool(wheel)}
            except Exception:
                self.close(); raise
            return self.info

    def enter_setup(self, path: str):
        with self.lock:
            allowed = {d["path"]: d for d in self.scan()}
            if path not in allowed or allowed[path]["mode"] != "console":
                raise ValueError("Choose the adapter connected to this PC.")
            handle = self.hid.device()
            try:
                handle.open_path(bytes.fromhex(path))
                sent = handle.send_feature_report(bytes([0xF3]) + b"WACFG\x01\x00" + bytes(56))
                if sent != 64:
                    raise OSError("The adapter did not accept the setup request.")
            finally:
                handle.close()
            # Scan again in the UI; legacy firmware ignores the magic and needs
            # first-time BOOTSEL flashing. Never claim setup mode until enumerated.
            return {"message": "Waiting for setup mode. If it does not appear, install the bundled firmware using BOOTSEL."}

    def request(self, operation, payload=b"") -> bytes:
        with self.lock:
            if not self.handle:
                raise ConnectionError("Connect the Pico in setup mode first.")
            if len(payload) > 57:
                raise ValueError("The request exceeds the HID packet size.")
            self.sequence = (self.sequence + 1) & 255
            packet = bytes([1, 87, 65, 1, operation, self.sequence, len(payload)]) + payload
            packet += bytes(64 - len(packet))
            try:
                if self.handle.send_feature_report(packet) != 64:
                    raise OSError("The Pico did not accept a complete request.")
                reply = bytes(self.handle.get_feature_report(1, 64))
                if len(reply) != 64 or reply[:6] != bytes([1, 87, 65, 1, operation, self.sequence]):
                    raise OSError("The Pico returned an incomplete or unrelated response.")
                status, length = reply[6:8]
                if length > 56:
                    raise OSError("The Pico returned an invalid response length.")
                if status:
                    raise ValueError(ERRORS.get(status, "The Pico rejected this operation."))
                return reply[8:8 + length]
            except OSError:
                self.close(); raise

    def read_settings(self):
        with self.lock:
            data = bytearray()
            for offset in range(0, SIZE, 56):
                count = min(56, SIZE - offset)
                part = self.request(OP_READ, struct.pack("<HB", offset, count))
                if len(part) != count:
                    raise OSError("The Pico returned incomplete settings.")
                data.extend(part)
            return Settings.decode(bytes(data))

    def write_settings(self, settings: Settings, save=True):
        with self.lock:
            data = settings.encode()
            for offset in range(0, SIZE, 55):
                self.request(OP_WRITE, struct.pack("<H", offset) + data[offset:offset + 55])
            self.request(OP_APPLY, bytes([bool(save)]))

    def devices(self):
        result = []
        with self.lock:
            for slot in range(16):
                try:
                    data = self.request(OP_DEVICE, bytes([slot]))
                except ValueError as exc:
                    if str(exc) == ERRORS[5]:
                        continue
                    raise
                address, instance, vid, pid, axis_count, auth, valid, fingerprint, updated, has_report = struct.unpack("<BBHHBBBIIB", data)
                axes = [self.axis(slot, i) for i in range(axis_count)]
                name = "Logitech G27" if vid == 0x046D and pid == 0xC29B else (
                    "Logitech G27 · initializing" if vid == 0x046D and pid == 0xC294 else (
                        "Arduino Micro" if vid == 0x2341 and pid == 0x8037 else (
                            "Hori controller" if vid == 0x0F0D else "USB controller")))
                result.append({"slot": slot, "address": address, "instance": instance, "vid": vid, "pid": pid,
                               "name": name, "axes": axes, "auth_capable": bool(auth), "descriptor_valid": bool(valid),
                               "fingerprint": fingerprint, "updated": updated, "has_report": bool(has_report)})
        return result

    def axis(self, slot, index):
        data = self.request(OP_AXIS, bytes([slot, index]))
        page, usage, offset, report, width, signed, low, high, value, valid = struct.unpack("<HHHBBBiiiB", data)
        names = {0x30: "X", 0x31: "Y", 0x32: "Z", 0x33: "X rotation", 0x34: "Y rotation", 0x35: "Z rotation", 0x36: "Slider", 0x37: "Dial", 0x38: "Wheel", 0xC4: "Accelerator", 0xC5: "Brake"}
        return {"index": index, "name": names.get(usage, "Axis"), "usage_page": page, "usage": usage,
                "bit_offset": offset, "report_id": report, "width": width, "signed": bool(signed),
                "minimum": low, "maximum": high, "value": value, "valid": bool(valid)}

    def telemetry(self):
        data = self.request(OP_TELEMETRY)
        steering, throttle, brake, clutch, gear, profile = struct.unpack_from("<HHHHBB", data)
        received, sent, dropped, queued, connected = struct.unpack_from("<IIIHB", data, 13)
        return {"steering": (steering - 32767.5) / 32767.5, "throttle": 1 - throttle / 65535,
                "brake": 1 - brake / 65535, "clutch": 1 - clutch / 65535,
                "gear": "R" if gear & 128 else next((str(i + 1) for i in range(6) if gear & (1 << i)), "N"),
                "profile": profile, "buttons": list(data[10:13]), "received": received, "sent": sent,
                "dropped": dropped, "queued": queued, "wheel_connected": connected == 2,
                "wheel_state": ("absent", "initializing", "native", "compatibility")[min(connected, 3)],
                "extra_buttons": data[28] if len(data) > 28 else 0,
                "dpad": data[29] if len(data) > 29 else 8, "time": time.monotonic()}
