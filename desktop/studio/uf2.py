"""Validate and personalize Pico UF2 images; only write verified RP2040 drives."""
import ctypes
import os
from pathlib import Path
import struct

from .settings import CONFIG_OFFSET, FLASH_BYTES, Settings

MAGIC0, MAGIC1, MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
RP2040_FAMILY = 0xE48BFF56
FLASH_BASE = 0x10000000
BLOCK_SIZE = 512


def parse_uf2(data: bytes) -> dict[int, bytes]:
    if not data or len(data) % BLOCK_SIZE:
        raise ValueError("The firmware is not a complete UF2 image.")
    pages, numbers = {}, set()
    block_count = len(data) // BLOCK_SIZE
    for offset in range(0, len(data), BLOCK_SIZE):
        block = data[offset:offset + BLOCK_SIZE]
        m0, m1, flags, address, size, number, count, family = struct.unpack_from("<8I", block)
        if (m0, m1, struct.unpack_from("<I", block, 508)[0]) != (MAGIC0, MAGIC1, MAGIC_END):
            raise ValueError("The UF2 image contains a damaged block.")
        if flags != 0x2000 or family != RP2040_FAMILY or size != 256:
            raise ValueError("Choose G27 Studio firmware built for a Raspberry Pi Pico (RP2040).")
        if count != block_count or number >= count or number in numbers:
            raise ValueError("The UF2 image has missing or duplicate blocks.")
        if not FLASH_BASE <= address < FLASH_BASE + FLASH_BYTES or address % 256:
            raise ValueError("The UF2 image writes outside the supported Pico flash.")
        if address in pages:
            raise ValueError("The UF2 image contains overlapping flash pages.")
        pages[address] = block[32:288]; numbers.add(number)
    if numbers != set(range(block_count)):
        raise ValueError("The UF2 image has incomplete block numbering.")
    return pages


def encode_uf2(pages: dict[int, bytes]) -> bytes:
    result = bytearray()
    for number, (address, payload) in enumerate(sorted(pages.items())):
        if len(payload) != 256:
            raise ValueError("UF2 pages must contain 256 bytes.")
        block = bytearray(512)
        struct.pack_into("<8I", block, 0, MAGIC0, MAGIC1, 0x2000, address, 256, number, len(pages), RP2040_FAMILY)
        block[32:288] = payload; struct.pack_into("<I", block, 508, MAGIC_END)
        result += block
    return bytes(result)


def personalized_image(firmware: bytes, settings: Settings) -> bytes:
    pages = parse_uf2(firmware)
    config_start = FLASH_BASE + CONFIG_OFFSET
    if FLASH_BASE not in pages:
        raise ValueError("The bundled firmware does not include Pico's boot sector.")
    if any(address >= config_start for address in pages):
        raise ValueError("The firmware overlaps the reserved settings storage.")
    # Replace both settings banks. A surviving bank with a larger sequence must
    # never override the personalized settings on the next boot.
    config = settings.encode(sequence=1) + bytes([255]) * (8192 - 512)
    for offset in range(0, len(config), 256):
        pages[config_start + offset] = config[offset:offset + 256]
    return encode_uf2(pages)


def pico_drives(roots=None) -> list[str]:
    if roots is None:
        if os.name != "nt":
            return []  # Desktop release currently targets Windows.
        mask = ctypes.windll.kernel32.GetLogicalDrives()
        roots = [Path(f"{chr(65 + i)}:/") for i in range(26) if mask & (1 << i)]
    found = []
    for root in roots:
        root = Path(root)
        try:
            info = (root / "INFO_UF2.TXT").read_text(encoding="utf-8", errors="replace")
            if "Board-ID: RPI-RP2" in {line.strip() for line in info.splitlines()}:
                found.append(str(root.resolve()))
        except OSError:
            continue
    return found


def flash_image(data: bytes, selected_drive: str, roots=None):
    parse_uf2(data)
    target = Path(selected_drive).resolve()
    verified = [Path(d).resolve() for d in pico_drives(roots)]
    if target not in verified:
        raise ValueError("The selected Pico is no longer in BOOTSEL mode. Hold BOOTSEL while reconnecting it.")
    # Pico resets as the final block arrives; no readback is possible on this drive.
    with (target / "G27-STUDIO.UF2").open("wb", buffering=0) as stream:
        for offset in range(0, len(data), 512):
            written = stream.write(data[offset:offset + 512])
            if written != 512:
                raise OSError("The USB transfer stopped before the full firmware image was sent.")
