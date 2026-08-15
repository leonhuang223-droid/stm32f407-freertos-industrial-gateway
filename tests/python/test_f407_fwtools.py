from __future__ import annotations

import hashlib
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.fwtools.image import (  # noqa: E402
    IMAGE_HEADER_SIZE,
    PARTITION_APP_IMAGE_SIZE,
    SLOT_A,
    SLOT_B,
    FirmwareToolError,
    build_manifest,
    crc32,
    pack_firmware,
    parse_header,
)


class FirmwareToolsTest(unittest.TestCase):
    def test_slot_b_package_and_manifest_contract(self) -> None:
        raw = bytes(range(256)) * 2
        package = pack_firmware(
            raw,
            target_id="STM32F407ZGT6_GATEWAY_V1",
            app_version="2.1.0",
            git_sha="abcdef0",
            target_slot=SLOT_B,
            min_bootloader_version="1.0.0",
        )
        header = parse_header(package)
        manifest = build_manifest(
            package,
            download_url="http://127.0.0.1:8000/app_b.pkg",
            release_note="host test",
        )

        self.assertEqual(IMAGE_HEADER_SIZE + len(raw), len(package))
        self.assertEqual(0x08080000, header.link_address)
        self.assertEqual(crc32(raw), header.image_crc32)
        self.assertEqual(hashlib.sha256(raw).digest(), header.image_sha256)
        self.assertEqual(crc32(package), manifest["crc32"])
        self.assertEqual(hashlib.sha256(package).hexdigest(), manifest["sha256"])
        self.assertEqual(len(package), manifest["image_size"])

    def test_slot_a_address_and_invalid_link_are_rejected(self) -> None:
        package = pack_firmware(
            b"\x55\xaa" * 32,
            target_id="STM32F407ZGT6_GATEWAY_V1",
            app_version="1.1.0",
            git_sha="1234567",
            target_slot=SLOT_A,
        )
        self.assertEqual(0x08020000, parse_header(package).link_address)
        with self.assertRaisesRegex(FirmwareToolError, "link address"):
            pack_firmware(
                b"\x55\xaa" * 32,
                target_id="STM32F407ZGT6_GATEWAY_V1",
                app_version="1.1.0",
                git_sha="1234567",
                target_slot=SLOT_A,
                link_address=0x08080000,
            )

    def test_corruption_and_size_guards(self) -> None:
        valid_package = pack_firmware(
                b"\x10\x20" * 32,
                target_id="STM32F407ZGT6_GATEWAY_V1",
                app_version="1.2.0",
                git_sha="7654321",
                target_slot=SLOT_B,
            )
        package = bytearray(valid_package)
        package[-1] ^= 0x01
        with self.assertRaisesRegex(FirmwareToolError, "crc32"):
            parse_header(bytes(package))
        with self.assertRaisesRegex(FirmwareToolError, "exceeds"):
            pack_firmware(
                b"\x00\x00" * (PARTITION_APP_IMAGE_SIZE // 2 + 1),
                target_id="STM32F407ZGT6_GATEWAY_V1",
                app_version="1.2.0",
                git_sha="7654321",
                target_slot=SLOT_B,
            )
        with self.assertRaisesRegex(FirmwareToolError, "http"):
            build_manifest(
                valid_package,
                download_url="https://example.com/app.pkg",
            )


if __name__ == "__main__":
    unittest.main()
