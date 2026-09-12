#!/usr/bin/env python3
"""Check that everything the build needs is in place, and say what is missing.

Run by "make check", and by "make" before it starts: a conversion that dies
halfway because ffmpeg is not installed wastes several minutes, and the CD
image cannot be produced at all without the two Commodore files.
"""
import argparse
import shutil
import sys

MODULES = (('numpy', 'numpy'), ('PIL', 'Pillow'), ('pycdlib', 'pycdlib'))


def missing_tools(cc, vasm):
    tools = [
        (cc, 'Amiga cross compiler, from the bebbo/amiga-gcc toolchain'),
        (vasm, 'assembler vasm (m68k, Motorola syntax)'),
        ('ffmpeg', 'ffmpeg, for the audio and the video clips'),
    ]
    out = [f'{name} -- {what}' for name, what in tools if not shutil.which(name)]
    if not shutil.which('genisoimage') and not shutil.which('mkisofs'):
        out.append('genisoimage -- or mkisofs, to build the ISO filesystem')
    return out


def missing_modules():
    out = []
    for name, package in MODULES:
        try:
            __import__(name)
        except ImportError:
            out.append(f'{package} -- Python module, "pip install {package}"')
    return out


def missing_files(args):
    files = [
        (args.iso, 'the original Klingon Language Lab CD image: this repository '
                   'carries none of its content, so point at your own copy with '
                   '"make ISO=path/to/klingon.iso"'),
        (args.tm, 'the CDTV trademark file, which belongs to Commodore: see the '
                  'CDTV.TM section of the README for how to get it'),
        (args.rmtm, 'the Commodore RMTM utility: see the RMTM section of the README'),
        (args.font, 'a TrueType font for the interface; pass another one with '
                    '"make FONT_TTF=/path/to/font.ttf"'),
    ]
    from os.path import isfile
    return [f'{path} -- {what}' for path, what in files if not isfile(path)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default='m68k-amigaos-gcc')
    ap.add_argument('--vasm', default='vasmm68k_mot')
    ap.add_argument('--iso', required=True)
    ap.add_argument('--tm', required=True)
    ap.add_argument('--rmtm', required=True)
    ap.add_argument('--font', required=True)
    args = ap.parse_args()

    missing = missing_tools(args.cc, args.vasm) + missing_modules() + missing_files(args)
    if not missing:
        return 0
    print('The build cannot start yet. Missing:', file=sys.stderr)
    for line in missing:
        print(f'  {line}', file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
