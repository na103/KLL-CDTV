#!/usr/bin/env python3
"""Check a KXL file: structure, full decode and the data rate it demands.

With --raw it compares the decode against the frame buffers produced by the
C decoder built for the host (make test).
"""
import argparse
import sys

import kxl


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('file')
    ap.add_argument('--raw', help='output of build/delta_test to compare against')
    a = ap.parse_args()

    with open(a.file, 'rb') as f:
        data = f.read()
    hdr, frames, audio = kxl.decode(data)
    count, avg, worst = kxl.stats(data)
    print(f'{a.file}: {count} frames {hdr["width"]}x{hdr["height"]} at {hdr["fps"]} fps, '
          f'{hdr["blocks"]} blocks, {len(audio)} bytes of audio, '
          f'average {avg:.1f} KB/s, worst block {worst:.1f} KB/s')

    if a.raw:
        expect = b''.join(kxl.to_planes(f, hdr['planes']).tobytes() for f in frames)
        with open(a.raw, 'rb') as f:
            got = f.read()
        if got != expect:
            sys.exit('C decoder: result differs from the Python one')
        print('C decoder: identical to the Python one')


if __name__ == '__main__':
    main()
