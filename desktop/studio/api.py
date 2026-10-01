"""Small typed surface exposed to the local desktop webview."""
from functools import wraps
import json
from pathlib import Path
import threading
from struct import error as struct_error

from . import __version__
from .settings import Settings, parse_lut
from .transport import Adapter, OP_REBOOT, OP_PROFILE, OP_FORCE
from .uf2 import pico_drives, personalized_image, flash_image


def exposed(function):
    @wraps(function)
    def call(self, *args, **kwargs):
        try:
            with self._lock:
                return {"ok": True, "data": function(self, *args, **kwargs)}
        except (ValueError, OSError, ConnectionError, RuntimeError, TypeError, KeyError, struct_error) as exc:
            return {"ok": False, "error": str(exc) or "The operation failed. Reconnect the Pico and try again."}
    return call


class StudioAPI:
    def __init__(self, firmware_path: Path, adapter=None):
        self._firmware_path = firmware_path
        self._adapter = adapter or Adapter()
        self._window = None
        self._lock = threading.RLock()

    @exposed
    def status(self):
        return {"version": __version__, "adapters": self._adapter.scan(), "pico_drives": pico_drives(),
                "connected": self._adapter.handle is not None, "firmware_available": self._firmware_path.is_file()}

    @exposed
    def connect(self, path):
        info = self._adapter.open(path)
        return {"info": info, "settings": self._adapter.read_settings().to_dict(), "devices": self._adapter.devices()}

    @exposed
    def enter_setup(self, path):
        return self._adapter.enter_setup(path)

    @exposed
    def disconnect(self):
        self._adapter.close()

    @exposed
    def measure(self, slot=None, axis=None):
        data = self._adapter.telemetry()
        if slot is not None and axis is not None:
            data["axis"] = self._adapter.axis(int(slot), int(axis))
        return data

    @exposed
    def devices(self):
        return self._adapter.devices()

    @exposed
    def save_settings(self, data, persistent=True):
        settings = Settings.from_dict(data)
        self._adapter.write_settings(settings, save=bool(persistent))
        readback = self._adapter.read_settings()
        if readback.to_dict() != settings.to_dict():
            raise OSError("The Pico did not confirm the saved settings.")
        return readback.to_dict()

    @exposed
    def preview_profile(self, profile):
        if type(profile) is not int or not 0 <= profile < 5:
            raise ValueError("Choose a force-feedback profile.")
        self._adapter.request(OP_PROFILE, bytes([profile]))

    @exposed
    def force_pulse(self, direction):
        if direction not in (-1, 0, 1):
            raise ValueError("Choose a left, stop, or right force pulse.")
        self._adapter.request(OP_FORCE, bytes([(direction * 12) & 255]))

    @exposed
    def reboot(self, target):
        if target not in ("console", "bootsel"):
            raise ValueError("Choose console mode or firmware installation.")
        self._adapter.request(OP_REBOOT, bytes([2 if target == "console" else 3]))
        self._adapter.close()

    def _dialog(self, save, name, filters):
        if self._window is None:
            raise RuntimeError("File dialogs are available in the desktop application.")
        import webview
        result = self._window.create_file_dialog(webview.FileDialog.SAVE if save else webview.FileDialog.OPEN,
            save_filename=name, file_types=filters, allow_multiple=False)
        if not result:
            return None
        return Path(result[0])

    @exposed
    def import_lut(self):
        path = self._dialog(False, "", ("Wheel LUT (*.lut)", "Text file (*.txt)"))
        if path is None:
            return None
        if path.stat().st_size > 256_000:
            raise ValueError("Choose a LUT smaller than 256 KB.")
        return {"name": path.name, "lut": parse_lut(path.read_text(encoding="utf-8-sig"))}

    @exposed
    def export_settings(self, data):
        settings = Settings.from_dict(data)
        path = self._dialog(True, "my-g27-settings.json", ("G27 settings (*.json)",))
        if path is None:
            return None
        path.write_text(json.dumps({"format": "g27-studio", "version": 1, "settings": settings.to_dict()}, indent=2), encoding="utf-8")
        return path.name

    @exposed
    def import_settings(self):
        path = self._dialog(False, "", ("G27 settings (*.json)",))
        if path is None:
            return None
        if path.stat().st_size > 256_000:
            raise ValueError("Choose a settings file smaller than 256 KB.")
        content = json.loads(path.read_text(encoding="utf-8-sig"))
        if not isinstance(content, dict) or content.get("format") != "g27-studio" or content.get("version") != 1:
            raise ValueError("Choose a settings backup exported by G27 Studio.")
        return Settings.from_dict(content["settings"]).to_dict()

    @exposed
    def export_uf2(self, data):
        image = personalized_image(self._firmware_path.read_bytes(), Settings.from_dict(data))
        path = self._dialog(True, "my-g27-adapter.uf2", ("Pico firmware (*.uf2)",))
        if path is None:
            return None
        path.write_bytes(image)
        return {"name": path.name, "bytes": len(image)}

    @exposed
    def flash(self, data, drive):
        image = personalized_image(self._firmware_path.read_bytes(), Settings.from_dict(data))
        self._adapter.close()
        flash_image(image, drive)
        return {"message": "Image sent. Wait for the Pico to restart, then reconnect it in G27 Studio to verify."}

    def _shutdown(self):
        with self._lock:
            try:
                if self._adapter.handle:
                    self._adapter.request(OP_FORCE, bytes([0]))
            except (OSError, ValueError, ConnectionError):
                pass
            self._adapter.close()
