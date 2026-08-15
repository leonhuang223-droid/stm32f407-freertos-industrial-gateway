"""Package a slot-linked F407 raw application image."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.fwtools.image import FirmwareToolError, pack_firmware, parse_slot

TARGET_ID = "STM32F407ZGT6_GATEWAY_V1"


def main() -> int:
    parser = argparse.ArgumentParser(description="Build F407 OTA package")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--slot", required=True, choices=("A", "B", "a", "b"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--git-sha", default="unknown")
    parser.add_argument("--min-bootloader-version", default="1.0.0")
    args = parser.parse_args()
    try:
        package = pack_firmware(
            args.input.read_bytes(),
            target_id=TARGET_ID,
            app_version=args.version,
            git_sha=args.git_sha,
            target_slot=parse_slot(args.slot),
            min_bootloader_version=args.min_bootloader_version,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(package)
    except (OSError, FirmwareToolError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
