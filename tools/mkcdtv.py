#!/usr/bin/env python3
"""Build a CDTV-bootable ISO from a directory.

The procedure follows cdtv-qdtitle (CDTV Land): ISO9660 level 3 with system
id CDTV, the CDTV.TM trademark file in the System Area starting at sector 2,
and the CDFS boot options in the Application Use field of the Primary Volume
Descriptor. CDTV.TM is not included: extract it from an original CDTV title.
"""
import argparse
import os
import shutil
import subprocess
import sys

SECTOR = 2048
PVD_OFFSET = 16 * SECTOR
TM_SECTOR = 2
TM_MAX_SECTORS = 11
APP_USE_OFFSET = PVD_OFFSET + 883
CDFS_BOOT_OPTIONS = bytes([0x00, ord('T'), ord('M'), 0x00, 0x14, 0x00, 0x00, 0x56, 0x88,
                           0x00, 0x00, 0x00, 0x02]) + bytes(14)


def find_mkisofs():
    # pycdlib-genisoimage will not do: it ignores -relaxed-filenames and
    # turns Startup-Sequence into STARTUP_SEQUENCE
    for name in ('mkisofs', 'genisoimage'):
        path = shutil.which(name)
        if path:
            return path
    sys.exit('mkisofs not found: install genisoimage')


def check_names(srcdir):
    """CDFS is case-insensitive: mkisofs would rename duplicates (S and s
    become S and S000, say) and the boot would not find
    S/Startup-Sequence."""
    for root, dirs, files in os.walk(srcdir):
        seen = {}
        for name in dirs + files:
            key = name.upper()
            if key in seen:
                sys.exit(f'{os.path.join(root, seen[key])} and {os.path.join(root, name)}: '
                         'same name on the CD (case-insensitive)')
            seen[key] = name


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('srcdir', help='directory holding the CD content')
    ap.add_argument('iso', help='ISO to create')
    ap.add_argument('--tm', required=True, help='the CDTV.TM file')
    ap.add_argument('--volume', default='KLL', help='volume name')
    args = ap.parse_args()

    if not os.path.isfile(args.tm):
        sys.exit(f'{args.tm} not found: the CDTV.TM trademark file is required')
    with open(args.tm, 'rb') as f:
        tm = f.read()
    if not tm or len(tm) > TM_MAX_SECTORS * SECTOR:
        sys.exit(f'{args.tm}: invalid size ({len(tm)} bytes)')

    check_names(args.srcdir)
    subprocess.run([find_mkisofs(), '-quiet',
                    '-relaxed-filenames', '-d', '-allow-leading-dots', '-allow-multidot',
                    '-input-charset', 'iso8859-1', '-output-charset', 'iso8859-1',
                    '-iso-level', '3', '-V', args.volume, '-sysid', 'CDTV',
                    '-o', args.iso, args.srcdir], check=True)

    with open(args.iso, 'r+b') as f:
        f.seek(PVD_OFFSET)
        if f.read(6) != b'\x01CD001':
            sys.exit(f'{args.iso}: no Primary Volume Descriptor at sector 16')
        f.seek(TM_SECTOR * SECTOR)
        if any(f.read(TM_MAX_SECTORS * SECTOR)):
            sys.exit(f'{args.iso}: the System Area is not empty')
        f.seek(TM_SECTOR * SECTOR)
        f.write(tm)
        f.seek(APP_USE_OFFSET)
        f.write(CDFS_BOOT_OPTIONS)

    print(f'{args.iso}: {os.path.getsize(args.iso) / 1048576:.1f} MB')


if __name__ == '__main__':
    main()
