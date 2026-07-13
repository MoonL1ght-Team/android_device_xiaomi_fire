#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import sys
import xml.etree.ElementTree as ET
from argparse import ArgumentParser
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


def rewrite_xml_line(line, size):
    start = line.index("<partition_size>") + len("<partition_size>")
    end = line.index("</partition_size>")
    return f"{line[:start]}0x{size:x}{line[end:]}"


def rewrite_text_line(line, size):
    prefix, _old = line.split(":", 1)
    return f"{prefix}: 0x{size:x}\n"


def write_patched_scatter(src, dst):
    text = src.read_text(errors="ignore")
    xml_format = text[:256].lstrip().startswith("<")
    active = None
    patched = set()
    out = []

    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        if xml_format:
            for name in EXPECTED:
                if f"<partition_name>{name}</partition_name>" in stripped:
                    active = name
                    break
            if active and "<partition_size>" in stripped:
                line = rewrite_xml_line(line, EXPECTED[active])
                patched.add(active)
                active = None
        else:
            if stripped.startswith("partition_name:"):
                name = stripped.split(":", 1)[1].strip()
                active = name if name in EXPECTED else None
            elif active and stripped.startswith("partition_size:"):
                line = rewrite_text_line(line, EXPECTED[active])
                patched.add(active)
                active = None
        out.append(line)

    missing = sorted(set(EXPECTED) - patched)
    if missing:
        raise SystemExit(f"failed to patch partition(s): {', '.join(missing)}")

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("".join(out))


def main():
    parser = ArgumentParser()
    parser.add_argument("scatter", type=Path)
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="write a scatter with the Fire GKI 6.6 physical partition sizes",
    )
    args = parser.parse_args()

    path = args.scatter
    if args.output:
        write_patched_scatter(path, args.output)
        path = args.output

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
