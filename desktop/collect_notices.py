"""Collect installed runtime dependency notices into the distributable app."""
from importlib import metadata
from pathlib import Path
import sys

PACKAGES = ("pywebview", "pythonnet", "clr_loader", "hidapi", "cffi", "pycparser",
            "bottle", "proxy_tools", "typing_extensions", "pyinstaller")
root = Path(__file__).resolve().parent
target = root / "licenses"
target.mkdir(exist_ok=True)
for name in PACKAGES:
    dist = metadata.distribution(name)
    folder = target / name
    folder.mkdir(exist_ok=True)
    (folder / "PACKAGE-METADATA.txt").write_text(dist.read_text("METADATA") or
        f"{dist.metadata['Name']} {dist.version}\n", encoding="utf-8")
    for item in dist.files or ():
        if any(word in str(item).lower() for word in ("license", "copying", "copyright")):
            file = Path(dist.locate_file(item))
            if file.is_file():
                filename = str(item).replace("/", "_").replace("\\", "_")
                (folder / filename).write_bytes(file.read_bytes())
for file in (Path(sys.base_prefix) / "LICENSE.txt", Path(sys.base_prefix) / "LICENSE"):
    if file.exists():
        (target / "Python-LICENSE.txt").write_bytes(file.read_bytes())
        break
for directory in ("pico-sdk", "tinyusb", "Pico-PIO-USB"):
    repo = root.parent / directory
    folder = target / directory
    folder.mkdir(exist_ok=True)
    found = False
    for name in ("LICENSE", "LICENSE.TXT", "LICENSE.txt"):
        file = repo / name
        if file.is_file():
            (folder / name).write_bytes(file.read_bytes())
            found = True
    if not found:
        raise RuntimeError(f"Missing {directory} notices. Initialize the repository submodules before packaging.")
print(f"Collected dependency notices in {target}")
