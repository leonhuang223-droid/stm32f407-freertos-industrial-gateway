"""Build and validate STM32F407 A/B OTA packages.

The wire format is exactly ``image_header_t + raw_app.bin``. Manifest
digests cover the complete package; header digests cover only the raw body.
"""

from __future__ import annotations

import binascii
import hashlib
import struct
from dataclasses import dataclass, replace
from typing import Any

IMAGE_MAGIC = 0x494F5441
IMAGE_FLAG_CANDIDATE = 0x00000001
IMAGE_FLAG_CONFIRMED = 0x00000002
IMAGE_HEADER_FORMAT = "<IHH32s16s16sIIIII32s16sII"
IMAGE_HEADER_SIZE = struct.calcsize(IMAGE_HEADER_FORMAT)
IMAGE_HEADER_CRC32_OFFSET = IMAGE_HEADER_SIZE - 4
IMAGE_SHA256_LEN = 32
PARTITION_APP_IMAGE_SIZE = 256 * 1024
SLOT_A = 0
SLOT_B = 1
SLOT_LINK_ADDRESSES = {
    SLOT_A: 0x08020000,
    SLOT_B: 0x08080000,
}


class FirmwareToolError(ValueError):
    """Raised when a package violates the firmware wire contract."""


@dataclass(frozen=True)
class ImageHeader:
    target_id: str
    app_version: str
    git_sha: str
    target_slot: int
    link_address: int
    image_offset: int
    image_size: int
    image_crc32: int
    image_sha256: bytes
    min_bootloader_version: str
    image_flags: int
    header_crc32: int
    magic: int = IMAGE_MAGIC
    header_version: int = 1
    header_size: int = IMAGE_HEADER_SIZE

    def to_bytes(self, *, header_crc32: int | None = None) -> bytes:
        return struct.pack(
            IMAGE_HEADER_FORMAT,
            self.magic,
            self.header_version,
            self.header_size,
            _fixed_ascii(self.target_id, 32, "target_id"),
            _fixed_ascii(self.app_version, 16, "app_version"),
            _fixed_ascii(self.git_sha, 16, "git_sha"),
            self.target_slot,
            self.link_address,
            self.image_offset,
            self.image_size,
            self.image_crc32,
            _fixed_bytes(self.image_sha256, IMAGE_SHA256_LEN, "image_sha256"),
            _fixed_ascii(
                self.min_bootloader_version, 16, "min_bootloader_version"
            ),
            self.image_flags,
            self.header_crc32 if header_crc32 is None else header_crc32,
        )


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def header_crc32(header: ImageHeader) -> int:
    return crc32(header.to_bytes(header_crc32=0))


def parse_slot(text: str) -> int:
    normalized = text.strip().upper()
    if normalized in {"A", "0", "SLOT_A"}:
        return SLOT_A
    if normalized in {"B", "1", "SLOT_B"}:
        return SLOT_B
    raise FirmwareToolError(f"invalid slot: {text}")


def default_link_address(slot: int) -> int:
    try:
        return SLOT_LINK_ADDRESSES[slot]
    except KeyError as exc:
        raise FirmwareToolError(f"invalid slot: {slot}") from exc


def pack_firmware(
    raw_image: bytes,
    *,
    target_id: str,
    app_version: str,
    git_sha: str,
    target_slot: int,
    link_address: int | None = None,
    min_bootloader_version: str = "0.0.0",
    image_flags: int = IMAGE_FLAG_CANDIDATE,
) -> bytes:
    if not raw_image:
        raise FirmwareToolError("raw image is empty")
    if len(raw_image) > PARTITION_APP_IMAGE_SIZE:
        raise FirmwareToolError("raw image exceeds F407 application partition")
    if len(raw_image) % 2 != 0:
        raise FirmwareToolError("raw image size must be even")
    if image_flags not in {IMAGE_FLAG_CANDIDATE, IMAGE_FLAG_CONFIRMED}:
        raise FirmwareToolError("invalid image flags")
    link = default_link_address(target_slot) if link_address is None else link_address
    if link != default_link_address(target_slot):
        raise FirmwareToolError("link address does not match target slot")
    header = ImageHeader(
        target_id=target_id,
        app_version=app_version,
        git_sha=git_sha,
        target_slot=target_slot,
        link_address=link,
        image_offset=IMAGE_HEADER_SIZE,
        image_size=len(raw_image),
        image_crc32=crc32(raw_image),
        image_sha256=hashlib.sha256(raw_image).digest(),
        min_bootloader_version=min_bootloader_version,
        image_flags=image_flags,
        header_crc32=0,
    )
    header = replace(header, header_crc32=header_crc32(header))
    return header.to_bytes() + raw_image


def parse_header(package: bytes) -> ImageHeader:
    if len(package) < IMAGE_HEADER_SIZE:
        raise FirmwareToolError("package shorter than image header")
    values = struct.unpack(IMAGE_HEADER_FORMAT, package[:IMAGE_HEADER_SIZE])
    header = ImageHeader(
        magic=values[0],
        header_version=values[1],
        header_size=values[2],
        target_id=_decode_fixed(values[3]),
        app_version=_decode_fixed(values[4]),
        git_sha=_decode_fixed(values[5]),
        target_slot=values[6],
        link_address=values[7],
        image_offset=values[8],
        image_size=values[9],
        image_crc32=values[10],
        image_sha256=values[11],
        min_bootloader_version=_decode_fixed(values[12]),
        image_flags=values[13],
        header_crc32=values[14],
    )
    if header.magic != IMAGE_MAGIC or header.header_version != 1:
        raise FirmwareToolError("invalid image header magic or version")
    if header.header_size != IMAGE_HEADER_SIZE or header.image_offset != IMAGE_HEADER_SIZE:
        raise FirmwareToolError("invalid package layout")
    if header.target_slot not in SLOT_LINK_ADDRESSES:
        raise FirmwareToolError("invalid target slot")
    if header.link_address != default_link_address(header.target_slot):
        raise FirmwareToolError("link address does not match target slot")
    if header.header_crc32 != header_crc32(replace(header, header_crc32=0)):
        raise FirmwareToolError("invalid image header crc32")
    body_end = header.image_offset + header.image_size
    if body_end != len(package) or header.image_size > PARTITION_APP_IMAGE_SIZE:
        raise FirmwareToolError("package size does not match header")
    body = package[header.image_offset:body_end]
    if crc32(body) != header.image_crc32:
        raise FirmwareToolError("invalid image body crc32")
    if hashlib.sha256(body).digest() != header.image_sha256:
        raise FirmwareToolError("invalid image body sha256")
    return header


def build_manifest(
    package: bytes,
    *,
    download_url: str,
    force_update: bool = False,
    release_note: str = "",
) -> dict[str, Any]:
    if not download_url.startswith("http://"):
        raise FirmwareToolError("download_url must use http://")
    header = parse_header(package)
    return {
        "manifest_version": 1,
        "target_id": header.target_id,
        "version": header.app_version,
        "target_slot": header.target_slot,
        "link_address": header.link_address,
        "image_size": len(package),
        "crc32": crc32(package),
        "sha256": hashlib.sha256(package).hexdigest(),
        "download_url": download_url,
        "min_bootloader_version": header.min_bootloader_version,
        "force_update": force_update,
        "release_note": release_note,
    }


def _fixed_ascii(text: str, capacity: int, name: str) -> bytes:
    try:
        encoded = text.encode("ascii")
    except UnicodeEncodeError as exc:
        raise FirmwareToolError(f"{name} must be ASCII") from exc
    if not encoded or len(encoded) >= capacity:
        raise FirmwareToolError(f"{name} must be 1..{capacity - 1} bytes")
    return encoded + b"\x00" * (capacity - len(encoded))


def _fixed_bytes(data: bytes, capacity: int, name: str) -> bytes:
    if len(data) != capacity:
        raise FirmwareToolError(f"{name} must be {capacity} bytes")
    return data


def _decode_fixed(data: bytes) -> str:
    nul = data.find(b"\x00")
    if nul < 0:
        raise FirmwareToolError("unterminated fixed string")
    return data[:nul].decode("ascii")
