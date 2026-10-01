"""Browser interaction and screenshot checks for the offline desktop UI.

Run with a development environment containing Playwright. Use --channel msedge
on Windows or install Chromium with `playwright install chromium`.
"""
import argparse
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
MOCK = """
window.pywebview = {api: {
 status: async()=>({ok:true,data:{version:'1.0.0',adapters:[{path:'1234',mode:'setup',name:'G27 Studio Setup'}],pico_drives:[],connected:true,firmware_available:true}}),
 connect: async()=>({ok:true,data:{info:{firmware:'1.0'},settings:window.g27Studio.state.settings,devices:[
 {slot:0,address:1,instance:0,vid:1133,pid:49707,name:'Logitech G27',axes:[],auth_capable:false},
 {slot:1,address:2,instance:0,vid:3853,pid:94,name:'Hori controller',axes:[],auth_capable:true},
 {slot:2,address:3,instance:0,vid:9025,pid:32823,name:'Arduino Micro',auth_capable:false,axes:[{index:0,name:'Z rotation',usage_page:1,usage:53,bit_offset:120,report_id:3,width:16,signed:true,minimum:-32767,maximum:32767,value:-32767,valid:true}]}]}}),
 measure:async(slot)=>({ok:true,data:{steering:.25,throttle:.5,brake:.75,clutch:.1,gear:'3',buttons:[8,0,0],wheel_connected:true,axis:slot===2?{index:0,name:'Z rotation',value:window.testAxisValue??1234,valid:true}:undefined}}),
 devices:async()=>({ok:true,data:window.g27Studio.state.devices}),
 save_settings:async(s)=>({ok:true,data:s}),preview_profile:async()=>({ok:true,data:null}),
 import_lut:async()=>({ok:true,data:{name:'my-wheel.lut',lut:Array.from({length:128},(_,i)=>i)}}),
 force_pulse:async()=>({ok:true,data:null}),reboot:async()=>({ok:true,data:null})
}};
"""


def run(channel=None):
    with sync_playwright() as pw:
        browser = pw.chromium.launch(channel=channel, headless=True)
        page = browser.new_page(viewport={"width": 1400, "height": 1020}, device_scale_factor=1)
        errors = []
        page.on("pageerror", lambda err: errors.append(str(err)))
        url = (ROOT / "desktop/ui/index.html").as_uri()
        page.goto(url + "?demo=1")
        page.wait_for_function("window.g27Studio.state.trace.length > 25")
        assert page.locator("#demo-banner").is_visible()
        first = page.locator("#steering-degrees").inner_text()
        page.wait_for_function("document.getElementById('steering-degrees').innerText !== " + repr(first))
        assert page.locator("#trace-empty").is_hidden()
        page.wait_for_timeout(8000)
        images = ROOT / "docs/images"; images.mkdir(exist_ok=True)
        page.screenshot(path=str(images / "studio-dashboard.png"), full_page=True)
        page.locator('[data-page="devices"]').click()
        assert page.locator("#external-settings").is_visible()
        page.screenshot(path=str(images / "studio-devices.png"), full_page=True)
        page.locator('[data-page="feedback"]').click()
        page.locator('[data-profile="4"]').click()
        assert page.locator("#curve-name").inner_text() == "Measured wheel LUT"
        page.screenshot(path=str(images / "studio-feedback.png"), full_page=True)
        page.locator("#pulse-right").click()
        assert "Exit demo" in page.locator("#notice").inner_text()
        for name in ("install", "guide", "overview"):
            page.locator(f'[data-page="{name}"]').click()
            assert page.locator(f"#{name}").is_visible()
        page.set_viewport_size({"width": 1000, "height": 700})
        assert page.evaluate("document.documentElement.scrollWidth <= window.innerWidth")
        assert not errors, errors
        page.close()

        # A separate fixture drives the live backend interface. These are tests,
        # not screenshots representing hardware that was never connected.
        page = browser.new_page(viewport={"width": 1280, "height": 900})
        page.on("pageerror", lambda err: errors.append(str(err)))
        page.add_init_script(MOCK)
        page.goto(url)
        page.locator("#connect").click()
        page.locator('[data-adapter="0"]').click()
        page.wait_for_function("window.g27Studio.state.connected")
        page.wait_for_function("document.getElementById('gear-value').textContent==='3'")
        assert page.locator("#demo-banner").is_hidden()
        assert page.locator("#brake-value").inner_text().startswith("75")
        page.locator('[data-page="devices"]').click()
        page.locator("#external-brake").click()
        page.locator("#brake-device").select_option("2")
        page.wait_for_function("window.g27Studio.state.axis?.value===1234")
        page.locator("#capture-released").click()
        assert page.locator("#released-endpoint").inner_text() == "1234"
        page.evaluate("window.testAxisValue=24000")
        page.wait_for_function("window.g27Studio.state.axis?.value===24000")
        page.locator("#capture-pressed").click()
        assert page.locator("#pressed-endpoint").inner_text() == "24000"
        page.locator("#invert-brake").click()
        assert page.locator("#released-endpoint").inner_text() == "24000"
        page.locator("#auth-device").select_option("1")
        assert page.evaluate("window.g27Studio.state.settings.auth_vid") == 3853
        page.locator("#preview-settings").click()
        page.wait_for_function("!window.g27Studio.state.busy")
        assert "setup session" in page.locator("#notice").inner_text()
        page.locator('[data-page="feedback"]').click()
        page.locator("#import-lut").click()
        page.wait_for_function("window.g27Studio.state.settings.custom_lut")
        assert page.locator("#lut-info").is_visible()
        page.locator("#feedback .save-settings").click()
        page.wait_for_function("!window.g27Studio.state.dirty")
        assert page.locator("#dirty").is_hidden()
        page.locator('[data-page="install"]').click()
        page.locator("#flash").click()
        assert "BOOTSEL" in page.locator("#notice").inner_text()
        assert not errors, errors
        browser.close()
    print("UI navigation, responsive layout, live charts, calibration, LUT import, save and failure checks passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("--channel", default=None)
    run(parser.parse_args().channel)
