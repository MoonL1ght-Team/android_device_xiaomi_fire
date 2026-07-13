#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import sys
import xml.etree.ElementTree as ET
from pathlib import Path


EXPECTED = {
    "boot_a": 0x8000000,
    "boot_b": 0x8000000,
    "vendor_boot_a": 0x800000,
    "vendor_boot_b": 0x800000,
}


def parse_int(value):
    return int(value.strip(), 0)


def parse_xml(path):
    root = ET.parse(path).getroot()
    parts = {}
    for elem in root.findall(".//partition_index"):
        fields = {child.tag: (child.text or "").strip() for child in elem}
        name = fields.get("partition_name")
        if not name:
            continue
        parts[name] = {
            "start": parse_int(fields.get("linear_start_addr", "0")),
            "size": parse_int(fields.get("partition_size", "0")),
        }
    return parts


def parse_scatter_text(path):
    parts = {}
    current = {}
    for raw in path.read_text(errors="ignore").splitlines():
        line = raw.strip()
        if line.startswith("- partition_index:"):
            if current.get("partition_name"):
                parts[current["partition_name"]] = {
                    "start": parse_int(current.get("linear_start_addr", "0")),
                    "size": parse_int(current.get("partition_size", "0")),
                }
            current = {}
            continue
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        current[key.strip()] = value.strip()
    if current.get("partition_name"):
        parts[current["partition_name"]] = {
            "start": parse_int(current.get("linear_start_addr", "0")),
            "size": parse_int(current.get("partition_size", "0")),
        }
    return parts


def load_parts(path):
    prefix = path.read_text(errors="ignore")[:256].lstrip()
    if prefix.startswith("<"):
        return parse_xml(path)
    return parse_scatter_text(path)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: check-fire-gki66-partitions.py MT6768_Android_scatter.xml|txt"
        )

    path = Path(sys.argv[1])
    parts = load_parts(path)
    errors = []

    for name, expected_size in EXPECTED.items():
        part = parts.get(name)
        if part is None:
            errors.append(f"{name}: missing from scatter")
            continue
        start = part["start"]
        size = part["size"]
        end = start + size
        print(f"{name}: start=0x{start:x} size=0x{size:x} end=0x{end:x}")
        if size != expected_size:
            errors.append(
                f"{name}: size 0x{size:x}, expected 0x{expected_size:x}"
            )

    for vendor_name, next_name in (
        ("vendor_boot_a", "dtbo_a"),
        ("vendor_boot_b", "dtbo_b"),
    ):
        vendor = parts.get(vendor_name)
        next_part = parts.get(next_name)
        if not vendor or not next_part:
            continue
        vendor_end = vendor["start"] + vendor["size"]
        if vendor_end > next_part["start"]:
            errors.append(
                f"{vendor_name}: end 0x{vendor_end:x} overlaps {next_name} "
                f"at 0x{next_part['start']:x}"
            )

    if errors:
        print("Fire GKI 6.6 partition contract failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        raise SystemExit(1)

    print("Fire GKI 6.6 partition contract looks coherent")


if __name__ == "__main__":
    main()
