"""Version 1 settings wire format, shared with src/settings.c."""
from dataclasses import asdict, dataclass, field
import math
import struct

SIZE = 512
VERSION = 1
MAGIC = 0x31534157
FLASH_BYTES = 2 * 1024 * 1024
CONFIG_OFFSET = FLASH_BYTES - 12288


def checksum(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


@dataclass
class Settings:
    external_brake: bool = False
    brake_vid: int = 0x2341
    brake_pid: int = 0x8037
    brake_instance: int = 255
    brake_report_id: int = 3
    brake_width: int = 16
    brake_signed: bool = True
    brake_bit_offset: int = 120
    brake_released: int = -32767
    brake_pressed: int = 32767
    brake_timeout_ms: int = 500
    auth_vid: int = 0
    auth_pid: int = 0
    auth_instance: int = 255
    default_profile: int = 1
    custom_lut: bool = False
    lut: list[int] = field(default_factory=lambda: list(range(128)))

    def validate(self):
        for name in ("external_brake", "brake_signed", "custom_lut"):
            if type(getattr(self, name)) is not bool:
                raise ValueError(f"{name} must be true or false.")
        for name in ("brake_vid", "brake_pid", "brake_bit_offset", "brake_timeout_ms", "auth_vid", "auth_pid"):
            if type(getattr(self, name)) is not int or not 0 <= getattr(self, name) <= 65535:
                raise ValueError(f"{name} is outside its supported range.")
        for name in ("brake_instance", "brake_report_id", "brake_width", "auth_instance", "default_profile"):
            if type(getattr(self, name)) is not int or not 0 <= getattr(self, name) <= 255:
                raise ValueError(f"{name} is outside its supported range.")
        if not 0 <= self.default_profile < 5:
            raise ValueError("Choose one of the five force-feedback profiles.")
        if bool(self.auth_vid) != bool(self.auth_pid):
            raise ValueError("The controller vendor and product must both be selected.")
        if any(type(v) is not int or not -(1 << 31) <= v < (1 << 31)
               for v in (self.brake_released, self.brake_pressed)):
            raise ValueError("The brake calibration must contain valid whole numbers.")
        if self.external_brake:
            if not self.brake_vid or not self.brake_pid or not 1 <= self.brake_width <= 31:
                raise ValueError("Choose a supported brake device and axis.")
            if self.brake_bit_offset + self.brake_width > (64 - bool(self.brake_report_id)) * 8:
                raise ValueError("The brake axis extends beyond the input report.")
            low = -(1 << (self.brake_width - 1)) if self.brake_signed else 0
            high = (1 << (self.brake_width - self.brake_signed)) - 1
            if any(type(v) is not int or not low <= v <= high for v in (self.brake_released, self.brake_pressed)):
                raise ValueError("The brake calibration exceeds the axis range.")
            if self.brake_released == self.brake_pressed:
                raise ValueError("Press the brake before capturing its pressed endpoint.")
            if self.brake_vid == 0x046D and self.brake_pid in (0xC294, 0xC29B):
                raise ValueError("Choose a separate USB device for the external brake.")
            if (self.brake_vid, self.brake_pid) == (self.auth_vid, self.auth_pid):
                raise ValueError("The pedal device cannot also authenticate the console.")
        if not isinstance(self.lut, list) or len(self.lut) != 128 or self.lut[0] != 0 or self.lut[-1] != 127:
            raise ValueError("A force curve must contain 128 samples from zero to full force.")
        if any(type(v) is not int or not 0 <= v <= 127 for v in self.lut):
            raise ValueError("The force curve contains an invalid sample.")
        if any(a > b for a, b in zip(self.lut, self.lut[1:])):
            raise ValueError("The force curve must never decrease.")
        return self

    def encode(self, sequence=0) -> bytes:
        self.validate()
        b = bytearray(SIZE)
        struct.pack_into("<IHHI", b, 0, MAGIC, VERSION, SIZE, sequence)
        struct.pack_into("<BBHHBBHB", b, 16, self.external_brake, self.brake_instance,
                         self.brake_vid, self.brake_pid, self.brake_report_id,
                         self.brake_width, self.brake_bit_offset, self.brake_signed)
        struct.pack_into("<iiHHHBBB", b, 28, self.brake_released, self.brake_pressed,
                         self.brake_timeout_ms, self.auth_vid, self.auth_pid,
                         self.auth_instance, self.default_profile, self.custom_lut)
        b[48:176] = bytes(self.lut)
        struct.pack_into("<I", b, 12, checksum(b[16:]) ^ checksum(b[:12]))
        return bytes(b)

    @classmethod
    def decode(cls, b: bytes):
        if len(b) != SIZE or struct.unpack_from("<IHH", b) != (MAGIC, VERSION, SIZE):
            raise ValueError("This configuration format is not supported by G27 Studio.")
        if struct.unpack_from("<I", b, 12)[0] != checksum(b[16:]) ^ checksum(b[:12]):
            raise ValueError("The configuration checksum is invalid.")
        values = struct.unpack_from("<BBHHBBHB", b, 16) + struct.unpack_from("<iiHHHBBB", b, 28)
        names = ("external_brake", "brake_instance", "brake_vid", "brake_pid", "brake_report_id",
                 "brake_width", "brake_bit_offset", "brake_signed", "brake_released", "brake_pressed",
                 "brake_timeout_ms", "auth_vid", "auth_pid", "auth_instance", "default_profile", "custom_lut")
        data = dict(zip(names, values))
        for name in ("external_brake", "brake_signed", "custom_lut"):
            if data[name] not in (0, 1):
                raise ValueError("The configuration has an invalid flag.")
            data[name] = bool(data[name])
        return cls(**data, lut=list(b[48:176])).validate()

    def to_dict(self):
        return asdict(self)

    @classmethod
    def from_dict(cls, data):
        if not isinstance(data, dict):
            raise ValueError("The settings file must contain an object.")
        if set(data) - set(cls.__dataclass_fields__):
            raise ValueError("The settings file contains unsupported fields.")
        return cls(**data).validate()


def parse_lut(text: str) -> list[int]:
    """Validate an Assetto Corsa x|y LUT and sample without changing neutral."""
    if len(text) > 256_000:
        raise ValueError("This LUT is too large. Choose a file smaller than 256 KB.")
    points = []
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith(("#", ";")):
            continue
        try:
            x, y = (float(v.strip()) for v in line.split("|"))
        except (ValueError, TypeError):
            raise ValueError(f"Line {number}: expected input|output, such as 0.5|0.6.") from None
        if not math.isfinite(x) or not math.isfinite(y) or not 0 <= x <= 1 or not 0 <= y <= 1:
            raise ValueError(f"Line {number}: values must be between 0 and 1.")
        if points and (x <= points[-1][0] or y < points[-1][1]):
            raise ValueError(f"Line {number}: inputs must increase and outputs must not decrease.")
        points.append((x, y))
    if len(points) < 2 or points[0] != (0.0, 0.0) or points[-1] != (1.0, 1.0):
        raise ValueError("The LUT must start with 0|0 and finish with 1|1.")
    result, interval = [], 0
    for i in range(128):
        x = i / 127
        while interval + 1 < len(points) - 1 and points[interval + 1][0] < x:
            interval += 1
        x0, y0 = points[interval]; x1, y1 = points[interval + 1]
        y = y0 + (y1 - y0) * (x - x0) / (x1 - x0)
        result.append(min(127, max(0, math.floor(y * 127 + 0.5))))
    return result
