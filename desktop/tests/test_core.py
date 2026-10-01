import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from studio.settings import Settings, SIZE, CONFIG_OFFSET, parse_lut
from studio.uf2 import FLASH_BASE, encode_uf2, parse_uf2, personalized_image, pico_drives, flash_image
from studio.transport import Adapter
from studio.api import StudioAPI


class SettingsTests(unittest.TestCase):
    def test_roundtrip_signed_calibration_and_lut(self):
        s = Settings(external_brake=True, brake_released=20000, brake_pressed=-20000,
                     custom_lut=True, default_profile=4, lut=parse_lut("0|0\n0.01|0.12\n1|1"))
        self.assertEqual(Settings.decode(s.encode()).to_dict(), s.to_dict())

    def test_every_byte_is_protected(self):
        good = Settings().encode()
        for i in range(SIZE):
            broken = bytearray(good); broken[i] ^= 128
            with self.assertRaises(ValueError):
                Settings.decode(bytes(broken))

    def test_rejects_invalid_calibration_and_device_conflicts(self):
        for s in (Settings(default_profile=5), Settings(external_brake=True, brake_width=32),
                  Settings(external_brake=True, brake_released=0, brake_pressed=0),
                  Settings(external_brake=True, brake_released=65535),
                  Settings(external_brake=True, auth_vid=0x2341, auth_pid=0x8037),
                  Settings(external_brake=True, brake_vid=0x046D, brake_pid=0xC29B)):
            with self.assertRaises(ValueError):
                s.encode()

    def test_lut_import_rejects_unsafe_or_ambiguous_curves(self):
        for text in ("", "0|0\n0.5|0.6\n0.5|0.7\n1|1", "0|0\n0.5|0.8\n0.8|0.7\n1|1",
                     "0|0\n1|nan", "0|0.1\n1|1", "0|0\n1|2", "0|0\n1|0.9", "0|0\nbad\n1|1"):
            with self.assertRaises(ValueError):
                parse_lut(text)
        self.assertEqual(parse_lut("# wheel test\n0 | 0\n1 | 1"), list(range(128)))


class UF2Tests(unittest.TestCase):
    def setUp(self):
        self.firmware = encode_uf2({FLASH_BASE: bytes(range(256)), FLASH_BASE + 256: bytes(256)})

    def test_personalization_keeps_code_and_replaces_both_settings_banks(self):
        image = personalized_image(self.firmware, Settings(default_profile=3))
        pages = parse_uf2(image)
        self.assertEqual(pages[FLASH_BASE], bytes(range(256)))
        start = FLASH_BASE + CONFIG_OFFSET
        data = pages[start] + pages[start + 256]
        self.assertEqual(Settings.decode(data).default_profile, 3)
        self.assertTrue(all(pages[start + offset] == bytes([255]) * 256 for offset in range(512, 8192, 256)))
        self.assertNotIn(start + 8192, pages)  # profile journal is preserved

    def test_rejects_corrupt_wrong_board_overlapping_and_incomplete_images(self):
        for offset, value in ((0, 0), (28, 0), (20, 1), (12, 1)):
            bad = bytearray(self.firmware); bad[offset] = value
            with self.assertRaises(ValueError):
                parse_uf2(bytes(bad))
        with self.assertRaises(ValueError):
            parse_uf2(self.firmware[:-1])
        with self.assertRaises(ValueError):
            personalized_image(encode_uf2({FLASH_BASE: bytes(256), FLASH_BASE + CONFIG_OFFSET: bytes(256)}), Settings())

    def test_flash_only_verified_pico_drive(self):
        with tempfile.TemporaryDirectory() as folder:
            pico = Path(folder) / "pico"; other = Path(folder) / "other"
            pico.mkdir(); other.mkdir()
            (pico / "INFO_UF2.TXT").write_text("UF2 Bootloader\nModel: Raspberry Pi RP2350\nBoard-ID: RPI-RP2350\n")
            self.assertEqual(pico_drives([pico, other]), [])
            (pico / "INFO_UF2.TXT").write_text("UF2 Bootloader v3.0\nModel: Raspberry Pi RP2040\nBoard-ID: RPI-RP2\n")
            self.assertEqual(pico_drives([pico, other]), [str(pico.resolve())])
            with self.assertRaises(ValueError):
                flash_image(self.firmware, str(other), roots=[pico, other])
            flash_image(self.firmware, str(pico), roots=[pico])
            self.assertEqual((pico / "G27-STUDIO.UF2").read_bytes(), self.firmware)


class FirmwareIntegrationTests(unittest.TestCase):
    def test_desktop_reads_and_writes_actual_firmware_protocol(self):
        fixture = Path(os.environ.get("G27_PROTOCOL_FIXTURE", "../build-host-tests/protocol_fixture.exe"))
        if not fixture.exists():
            self.skipTest("Build the native protocol fixture to run C/Python integration.")
        process = subprocess.Popen([str(fixture.resolve())], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

        class FirmwareHID:
            def __init__(self): self.pending = None
            def send_feature_report(self, data):
                process.stdin.write(data[1:].hex() + "\n"); process.stdin.flush()
                self.pending = bytes([1]) + bytes.fromhex(process.stdout.readline().strip())
                return len(data)
            def get_feature_report(self, report, size): return self.pending
            def close(self): pass

        adapter = Adapter(hid_module=object()); adapter.handle = FirmwareHID()
        try:
            settings = adapter.read_settings()
            self.assertEqual(settings.to_dict(), Settings().to_dict())
            settings.external_brake = True; settings.brake_released = 20000; settings.brake_pressed = -20000
            settings.custom_lut = True; settings.default_profile = 4
            settings.lut = parse_lut("0|0\n0.1|0.25\n1|1")
            adapter.write_settings(settings)
            self.assertEqual(adapter.read_settings().to_dict(), settings.to_dict())
            self.assertEqual(adapter.devices(), [])
            self.assertEqual(adapter.telemetry()["gear"], "N")
        finally:
            process.stdin.close(); process.wait(timeout=5); process.stdout.close()


class TransportFailureTests(unittest.TestCase):
    def test_webview_exposes_only_api_methods(self):
        api = StudioAPI(Path("unused.uf2"), Adapter(hid_module=object()))
        for name in dir(api):
            if not name.startswith("_"):
                self.assertTrue(callable(getattr(api, name)), name)

    def test_unrelated_truncated_or_oversized_reply_closes_connection(self):
        for failure in ("sequence", "operation", "truncated", "oversized", "send"):
            class BadHID:
                closed = False
                def send_feature_report(self, data):
                    reply = bytearray([1, 87, 65, 1, data[4], data[5], 0, 0]) + bytearray(56)
                    if failure == "sequence": reply[5] ^= 1
                    if failure == "operation": reply[4] ^= 1
                    if failure == "oversized": reply[7] = 57
                    self.reply = reply[:-1] if failure == "truncated" else reply
                    return 63 if failure == "send" else 64
                def get_feature_report(self, report, size): return self.reply
                def close(self): self.closed = True
            with self.subTest(failure=failure):
                handle = BadHID()
                adapter = Adapter(hid_module=object()); adapter.handle = handle
                with self.assertRaises(OSError): adapter.request(1)
                self.assertIsNone(adapter.handle)
                self.assertTrue(handle.closed)

    def test_device_not_ready_preserves_connection_and_reports_useful_error(self):
        class NotReadyHID:
            def send_feature_report(self, data):
                self.reply = bytes([1, 87, 65, 1, data[4], data[5], 6, 0]) + bytes(56)
                return 64
            def get_feature_report(self, report, size): return self.reply
        adapter = Adapter(hid_module=object()); adapter.handle = NotReadyHID()
        api = StudioAPI(Path("unused.uf2"), adapter)
        result = api.force_pulse(1)
        self.assertFalse(result["ok"])
        self.assertIn("calibration", result["error"])
        self.assertIsNotNone(adapter.handle)

    def test_invalid_backups_return_user_errors(self):
        api = StudioAPI(Path("unused.uf2"), Adapter(hid_module=object()))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "settings.json"
            api._dialog = lambda *args: path
            for content in ('[]', '{}', '{"format":"g27-studio","version":1}',
                            '{"format":"g27-studio","version":1,"settings":{"lut":null}}'):
                path.write_text(content)
                self.assertFalse(api.import_settings()["ok"])


if __name__ == "__main__":
    unittest.main()
