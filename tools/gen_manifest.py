"""Generate manifest JSON from a validated F407 OTA package."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.fwtools.image import FirmwareToolError, build_manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate F407 OTA manifest")
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--download-url", required=True)
    parser.add_argument("--force-update", action="store_true")
    parser.add_argument("--release-note", default="")
    args = parser.parse_args()
    try:
        manifest = build_manifest(
            args.package.read_bytes(),
            download_url=args.download_url,
            force_update=args.force_update,
            release_note=args.release_note,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (OSError, FirmwareToolError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
