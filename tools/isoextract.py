#!/usr/bin/env python3
"""Extract from the original Klingon Language Lab ISO the files the build uses."""
import argparse
import os

import pycdlib

DIRS = ('AVI', 'WAV', 'FIN_IMS2', 'DATAFILE', 'TXTFILES', 'HD')


def walk(iso, path):
    for child in iso.list_children(iso_path=path):
        name = child.file_identifier().decode('ascii')
        if name in ('.', '..'):
            continue
        full = path.rstrip('/') + '/' + name
        if child.is_dir():
            yield from walk(iso, full)
        else:
            yield full, child.data_length


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('iso', help='the original CD image')
    ap.add_argument('outdir', help='where to put the files')
    args = ap.parse_args()

    iso = pycdlib.PyCdlib()
    iso.open(args.iso)
    count = 0
    for d in DIRS:
        for isopath, size in walk(iso, '/' + d):
            out = os.path.join(args.outdir, isopath.split(';')[0].lstrip('/'))
            # skip files already extracted
            if os.path.exists(out) and os.path.getsize(out) == size:
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out + '.tmp', 'wb') as f:
                iso.get_file_from_iso_fp(f, iso_path=isopath)
            os.replace(out + '.tmp', out)
            count += 1
    iso.close()
    print(f'{count} files extracted into {args.outdir}')


if __name__ == '__main__':
    main()
