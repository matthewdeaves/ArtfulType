#!/usr/bin/env python3
"""Wraps a raw Macintosh floppy sector dump in a DiskCopy 4.2 image
header, so classic Disk Copy can actually open/write it -- a plain
raw sector dump (what Mini vMac and hfsutils use directly) has no
header at all and Disk Copy won't recognize it.

Format verified against two independent references (ciderpress2.com
and discferret.com's DiskCopy 4.2 docs), not from memory alone:
84-byte header (64-byte Pascal-string disk name, dataSize, tagSize,
dataChecksum, tagChecksum, diskFormat, formatByte, a fixed 0x0100
signature word) followed by the raw data, no tag data for HFS floppies.

Checksum: 32-bit accumulator, add each big-endian 16-bit word, then
rotate the accumulator right 1 bit, repeat for every word in the data.

Currently only handles 800K Mac floppies (the only size this project
needs right now) -- add other sizes' diskFormat/formatByte values
(verified, not guessed) before reusing this for a different size.
"""
import struct
import sys


def diskcopy_checksum(data):
    acc = 0
    for i in range(0, len(data) - 1, 2):
        word = (data[i] << 8) | data[i + 1]
        acc = (acc + word) & 0xFFFFFFFF
        acc = ((acc >> 1) | ((acc & 1) << 31)) & 0xFFFFFFFF
    return acc


def make_diskcopy_image(raw_path, out_path, disk_name):
    with open(raw_path, "rb") as f:
        data = f.read()

    if len(data) != 819200:
        raise ValueError(f"Expected an 800K (819200-byte) floppy image, got {len(data)} bytes")
    disk_format, format_byte = 1, 0x22  # 800KB, 800KB Mac (GCR)

    name_bytes = disk_name.encode("mac_roman", errors="replace")[:63]
    name_field = bytes([len(name_bytes)]) + name_bytes
    name_field += bytes(64 - len(name_field))

    header = (
        name_field
        + struct.pack(">I", len(data))      # dataSize
        + struct.pack(">I", 0)              # tagSize (none for HFS)
        + struct.pack(">I", diskcopy_checksum(data))
        + struct.pack(">I", 0)              # tagChecksum (none)
        + bytes([disk_format, format_byte])
        + struct.pack(">H", 0x0100)         # signature
    )
    assert len(header) == 84

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(data)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: make_diskcopy_image.py <raw-image> <output> <disk-name>")
        sys.exit(1)
    make_diskcopy_image(sys.argv[1], sys.argv[2], sys.argv[3])
