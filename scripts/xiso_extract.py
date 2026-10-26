#!/usr/bin/env python3
"""Minimal XDVDFS (Xbox / Xbox 360 ISO) extractor.

Usage: xiso_extract.py <image.iso> <out_dir> [--list]

Handles full XGD2/XGD3 dumps and trimmed game-partition images by probing
the known partition offsets for the volume descriptor magic.
"""
import os
import struct
import sys

SECTOR = 0x800
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# trimmed/XISO, XGD3, XGD2, XGD1
PARTITION_OFFSETS = (0x0, 0x2080000, 0xFDA0000, 0x18300000)
ATTR_DIRECTORY = 0x10


def find_partition(f):
    for off in PARTITION_OFFSETS:
        f.seek(off + 32 * SECTOR)
        if f.read(len(MAGIC)) == MAGIC:
            return off
    raise SystemExit("not an XDVDFS image (volume descriptor not found)")


def walk_dir(f, base, sector, size, prefix=""):
    """Yield (relative_path, is_dir, sector, size) for a directory table."""
    f.seek(base + sector * SECTOR)
    table = f.read(size)
    stack = [0]
    seen = set()
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(table):
            continue
        seen.add(off)
        left, right, ent_sector, ent_size, attr, name_len = struct.unpack_from("<HHIIBB", table, off)
        if left == 0xFFFF:  # padding / empty table
            continue
        name = table[off + 14:off + 14 + name_len].decode("latin-1")
        path = os.path.join(prefix, name)
        is_dir = bool(attr & ATTR_DIRECTORY)
        yield path, is_dir, ent_sector, ent_size
        if is_dir and ent_size:
            yield from walk_dir(f, base, ent_sector, ent_size, path)
        if left:
            stack.append(left * 4)
        if right:
            stack.append(right * 4)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    iso, out = sys.argv[1], sys.argv[2]
    list_only = "--list" in sys.argv[3:]
    with open(iso, "rb") as f:
        base = find_partition(f)
        f.seek(base + 32 * SECTOR + len(MAGIC))
        root_sector, root_size = struct.unpack("<II", f.read(8))
        for path, is_dir, sector, size in walk_dir(f, base, root_sector, root_size):
            print(("d " if is_dir else "f ") + f"{size:>12}  {path}")
            if list_only:
                continue
            dest = os.path.join(out, path)
            if is_dir:
                os.makedirs(dest, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            f.seek(base + sector * SECTOR)
            remaining = size
            with open(dest, "wb") as o:
                while remaining:
                    chunk = f.read(min(remaining, 1 << 20))
                    if not chunk:
                        raise SystemExit(f"unexpected EOF extracting {path}")
                    o.write(chunk)
                    remaining -= len(chunk)


if __name__ == "__main__":
    main()
