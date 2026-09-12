#!/usr/bin/env python3
"""Convert the content of the entries from the original CD to the CDTV one.

  mkmedia.py SRC CD --palette build/gui_palette.json [--no-video] [--only N]

For every database entry it produces the picture (IMG/<base>.PIC), the audio
(SND/<base>.RAW) and the clip (VIDEO/<base>.KXL), plus the interface sound
effects in SFX. Files already up to date are skipped, so the conversion can
be interrupted and resumed: the clips are the slow part.

The audio becomes signed 8-bit PCM at 11025 Hz mono, the format the program
hands to audio.device.
"""
import argparse
import os
import subprocess
import sys

import layout
import mkdb

RATE = 11025
HERE = os.path.dirname(os.path.abspath(__file__))


def newer(src, dst, *also):
    """True if dst has to be rebuilt: missing, or older than src (or also).

    The shared palette counts as a source too: the clips use its colours, so
    if it changes they all have to be converted again, otherwise they keep
    the old colours and show up with the wrong ones.
    """
    if not os.path.exists(dst):
        return True
    t = os.path.getmtime(dst)
    return any(t < os.path.getmtime(f) for f in (src,) + also if os.path.exists(f))


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f'{" ".join(cmd[:3])}...: {r.stderr.strip().splitlines()[-1:] or r.stdout}')
    return r


def wav2raw(src, dst, volume=1.0):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    run(['ffmpeg', '-y', '-v', 'error', '-i', src,
         '-filter:a', f'volume={volume}', '-ac', '1', '-ar', str(RATE),
         '-f', 's8', dst + '.tmp'])
    os.replace(dst + '.tmp', dst)


def bmp2pic(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    run([sys.executable, f'{HERE}/convimg.py', 'item', src, dst]
        + ' '.join(layout.reserve_args()).split())


def avi2kxl(src, dst, palette):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    run([sys.executable, f'{HERE}/avi2kxl.py', src, dst,
         '--colors', '32', '--fixed-palette', palette, '--block', '32768']
        + ' '.join(layout.reserve_args()).split())


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', help='directory with the files extracted from the ISO')
    ap.add_argument('cd', help='directory of the CDTV CD')
    ap.add_argument('--palette', required=True)
    ap.add_argument('--no-video', action='store_true', help='skip the clips')
    ap.add_argument('--only', type=int, default=0,
                    help='convert only the first N entries of each category')
    args = ap.parse_args()

    todo = []
    for dirname, label, recs in mkdb.all_categories(f'{args.src}/DATAFILE'):
        if args.only:
            recs = recs[:args.only]
        for r in recs:
            todo.append((dirname, r))

    done = 0
    for name in layout.SOUNDS:
        src = f'{args.src}/WAV/{name}.WAV'
        dst = f'{args.cd}/SFX/{name}.RAW'
        if newer(src, dst):
            wav2raw(src, dst)
            done += 1

    for n, (dirname, r) in enumerate(todo, 1):
        img = f'{args.src}/FIN_IMS2/{dirname}/{r[5]}'
        dst = f'{args.cd}/IMG/{mkdb.stem(r[5])}.PIC'
        if os.path.isfile(img) and newer(img, dst):
            bmp2pic(img, dst)
            done += 1

        for f in (r[8], r[9], r[10], r[11]):
            if not f or mkdb.stem(f) == mkdb.NO_MORE:
                continue
            snd = f'{args.src}/WAV/{f}'
            dst = f'{args.cd}/SND/{mkdb.stem(f)}.RAW'
            if os.path.isfile(snd) and newer(snd, dst):
                wav2raw(snd, dst)
                done += 1

        avi = f'{args.src}/AVI/{r[4]}'
        dst = f'{args.cd}/VIDEO/{mkdb.stem(r[4])}.KXL'
        if not args.no_video and os.path.isfile(avi) and newer(avi, dst, args.palette):
            print(f'  [{n}/{len(todo)}] {mkdb.stem(r[4])}', flush=True)
            avi2kxl(avi, dst, args.palette)
            done += 1

    print(f'{args.cd}: {done} files converted, {len(todo)} entries')


if __name__ == '__main__':
    main()
