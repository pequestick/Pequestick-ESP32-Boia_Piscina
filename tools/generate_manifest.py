#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path


def build_manifest(firmware_path, version, build_sha, build_date, firmware_url, notes):
    firmware = Path(firmware_path)
    digest = hashlib.sha256(firmware.read_bytes()).hexdigest()
    return {
        "version": version,
        "build_sha": build_sha,
        "build_short_sha": build_sha[:12],
        "build_date": build_date,
        "size": firmware.stat().st_size,
        "sha256": digest,
        "firmware_url": firmware_url,
        "notes": notes,
    }


def main():
    parser = argparse.ArgumentParser(description="Generate the signed-data OTA manifest")
    parser.add_argument("--firmware", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-sha", required=True)
    parser.add_argument("--build-date", required=True)
    parser.add_argument("--firmware-url", required=True)
    parser.add_argument("--notes", default="")
    args = parser.parse_args()

    manifest = build_manifest(
        args.firmware,
        args.version,
        args.build_sha,
        args.build_date,
        args.firmware_url,
        args.notes,
    )
    Path(args.output).write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
