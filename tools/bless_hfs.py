#!/usr/bin/env python3
"""Bless an HFS volume so a Macintosh ROM will boot it.

An HFS volume boots when two things are true:

  1. Its boot blocks (the first 1024 bytes, logical blocks 0-1) hold valid
     680x0 boot code beginning with the signature 'LK' (0x4C4B). This code
     is volume-independent: it locates the blessed System Folder, then opens
     the System file and launches the shell (Finder) named in the boot block
     header. We copy these verbatim from a known-bootable System volume.

  2. The Master Directory Block (logical block 2) names the "blessed" System
     Folder in drFndrInfo[0] -- the directory ID the ROM searches for the
     System file. hformat does neither, so a freshly formatted volume is not
     bootable; this script supplies both.

Usage: bless_hfs.py <target.dsk> <bootblock-source.dsk> <blessed-dir-id>
"""
import sys
import struct

MDB_OFFSET = 1024          # logical block 2 (512-byte blocks)
DR_SIG_WORD = 0x4244       # 'BD' -- MDB signature
DR_FNDR_INFO = 92          # offset of drFndrInfo within the MDB
BOOT_SIG = 0x4C4B          # 'LK' -- boot block signature


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    target, source, dir_id = sys.argv[1], sys.argv[2], int(sys.argv[3])

    with open(source, 'rb') as f:
        boot = f.read(1024)
    if struct.unpack('>H', boot[0:2])[0] != BOOT_SIG:
        sys.exit("error: source has no 'LK' boot blocks -- not bootable")

    with open(target, 'r+b') as f:
        data = bytearray(f.read())
        if struct.unpack('>H', data[MDB_OFFSET:MDB_OFFSET + 2])[0] != DR_SIG_WORD:
            sys.exit("error: target has no 'BD' MDB -- not an HFS volume")

        # 1. Install the boot code.
        data[0:1024] = boot
        # 2. Bless the System Folder.
        off = MDB_OFFSET + DR_FNDR_INFO
        struct.pack_into('>I', data, off, dir_id)

        f.seek(0)
        f.write(data)

    print(f"blessed {target}: boot blocks installed, drFndrInfo[0] = {dir_id}")


if __name__ == '__main__':
    main()
