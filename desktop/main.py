"""Launch G27 Studio; all UI assets and firmware are bundled offline."""
from pathlib import Path
import sys
import json
import time
import logging
import traceback


def main():
    import webview
    from studio.api import StudioAPI
    root = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    firmware = root / "firmware" / "adapter.uf2"
    if not firmware.exists() and not getattr(sys, "frozen", False):
        firmware = root.parent / "build" / "adapter.uf2"
    api = StudioAPI(firmware)
    smoke = "--smoke-test" in sys.argv
    window = webview.create_window("G27 Studio", str(root / "ui" / "index.html"),
        js_api=api, width=1280, height=850, min_size=(1000, 700), background_color="#111315")
    api._window = window
    window.events.closed += api._shutdown
    def smoke_test():
        result = {"ok": False, "error": "The desktop webview did not finish loading."}
        try:
            if not window.events.loaded.wait(20):
                raise RuntimeError(result["error"])
            window.hide()
            window.evaluate_js("window.pywebview.api.status().then(r=>window.smokeResult=r)")
            until = time.monotonic() + 10
            while time.monotonic() < until:
                response = window.evaluate_js("JSON.stringify(window.smokeResult||null)")
                if response and response != "null":
                    result = json.loads(response)
                    result["ui_ready"] = bool(window.evaluate_js("!!window.g27Studio && document.querySelectorAll('.page').length===5"))
                    break
                time.sleep(0.1)
        except Exception as exc:
            result = {"ok": False, "error": str(exc)}
        finally:
            if "--smoke-output" in sys.argv:
                Path(sys.argv[sys.argv.index("--smoke-output") + 1]).write_text(json.dumps(result), encoding="utf-8")
            window.destroy()
    webview.start(smoke_test if smoke else None, gui="edgechromium", debug="--debug" in sys.argv)


if __name__ == "__main__":
    smoke_path = None
    if "--smoke-output" in sys.argv:
        smoke_path = Path(sys.argv[sys.argv.index("--smoke-output") + 1])
        logging.basicConfig(filename=str(smoke_path) + ".log", level=logging.DEBUG)
        logging.info("Starting desktop application")
    try:
        main()
    except Exception as exc:
        logging.exception("Desktop startup failed")
        if smoke_path:
            smoke_path.write_text(json.dumps({"ok": False, "error": str(exc),
                "traceback": traceback.format_exc()}), encoding="utf-8")
        else:
            import ctypes
            ctypes.windll.user32.MessageBoxW(None,
                "G27 Studio could not open. Check that Microsoft Edge WebView2 is installed.\n\n"
                + str(exc), "G27 Studio", 0x10)
