#!/usr/bin/env python3
"""Convert an AVI clip to the KXL3 format for the CDTV (see kxl.py).

The video is reduced to a 12-bit palette, normally without dithering: it
first goes through a denoising filter and the mapping uses temporal
hysteresis, so still pixels keep their colour from frame to frame (which also
shrinks the delta-coded data). Reserved colours (--reserve) stay in the
palette but the video never uses them. The audio is signed 8-bit mono with a
whole, even number of samples per frame (736 at 15 fps = 11040 Hz), as
audio.device requires. The resulting file is decoded again and compared with
the frames and audio it came from.
"""
import argparse
import json
import subprocess
import sys

import numpy as np
from PIL import Image

import amigagfx as ag
import kxl


def ffmpeg(args):
    return subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', *args],
                          check=True, stdout=subprocess.PIPE).stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--size', default='118x88', help='video size (WxH)')
    ap.add_argument('--fps', type=int, default=15)
    ap.add_argument('--rate', type=int, default=11040, help='audio sample rate')
    ap.add_argument('--colors', type=int, default=16)
    ap.add_argument('--fixed-palette',
                    help='JSON with colours fixed at the start of the palette (the interface ones, say)')
    ap.add_argument('--reserve', action='append', default=[], metavar='INDICE=RGB',
                    help='a reserved colour the video must not use, e.g. 17=F80 (repeatable)')
    ap.add_argument('--power', type=float, default=0.5,
                    help='weight of the counts in the palette (1 = plain frequency)')
    ap.add_argument('--denoise', default='hqdn3d',
                    help='ffmpeg filter applied to the source ("" = none)')
    ap.add_argument('--postfilter', default='',
                    help='ffmpeg filter applied after scaling to the final size')
    ap.add_argument('--hysteresis', type=float, default=40.0,
                    help='temporal hysteresis threshold when not dithering (0 = none)')
    ap.add_argument('--dither-mode', choices=('none', 'ordered', 'fs'), default='none',
                    help='no dithering, ordered, or Floyd-Steinberg')
    ap.add_argument('--dither', type=float, default=10.0,
                    help='strength of the ordered dithering')
    ap.add_argument('--block', type=int, default=kxl.BLOCK_SIZE,
                    help='size of the read blocks')
    ap.add_argument('--preview', help='PNG with a few converted frames')
    a = ap.parse_args()

    w, h = map(int, a.size.split('x'))
    abytes = a.rate // a.fps
    if a.rate % a.fps or abytes % 2:
        sys.exit('--rate must give a whole, even number of samples per frame')
    planes = max(1, (a.colors - 1).bit_length())

    vf = f'fps={a.fps},scale={w}:{h}:flags=lanczos'
    if a.denoise:
        vf = f'{a.denoise},{vf}'
    if a.postfilter:
        vf = f'{vf},{a.postfilter}'
    raw = ffmpeg(['-i', a.src, '-an', '-vf', vf, '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'])
    frames = np.frombuffer(raw, np.uint8).reshape(-1, h, w, 3)
    if not len(frames):
        sys.exit(f'{a.src}: no video frames')
    try:
        audio = ffmpeg(['-i', a.src, '-vn', '-ac', '1', '-ar', str(a.rate), '-f', 's8', '-'])
    except subprocess.CalledProcessError:
        audio = b''
    audio = audio[:len(frames) * abytes].ljust(len(frames) * abytes, b'\0')

    fixed = ()
    if a.fixed_palette:
        with open(a.fixed_palette) as f:
            fixed = np.array(json.load(f), np.float32)
    reserved = {}
    for r in a.reserve:
        index, rgb = r.split('=')
        reserved[int(index)] = int(rgb, 16)
    if any(i < len(fixed) or i >= a.colors for i in reserved):
        sys.exit('reserved colours must come after the fixed ones and inside the palette')

    # palette without the reserved colours, then slotted into the free entries
    free = [i for i in range(a.colors) if i not in reserved]
    pal = ag.make_palette(frames.reshape(-1, 3), len(free), fixed=fixed, power=a.power)
    lut = np.array(free, np.uint8)
    words = [0] * a.colors
    for i, c in zip(free, ag.palette_words(pal)):
        words[i] = c
    for i, c in reserved.items():
        words[i] = c

    if a.dither_mode == 'none':
        indices = [lut[i] for i in ag.map_sequence(frames, pal, a.hysteresis)]
    elif a.dither_mode == 'fs':
        indices = [lut[ag.map_fs(frame, pal)] for frame in frames]
    else:
        indices = [lut[ag.map_ordered(frame, pal, a.dither)] for frame in frames]

    data = kxl.encode(indices, words, planes, audio, abytes, a.fps, a.block)

    # check: decoding the file must reproduce the source data
    hdr, dec_frames, dec_audio = kxl.decode(data)
    if (hdr['palette'] != words or dec_audio != audio or len(dec_frames) != len(indices) or
            any((d != i).any() for d, i in zip(dec_frames, indices))):
        sys.exit(f'{a.dst}: decoding does not match the source data')

    with open(a.dst, 'wb') as f:
        f.write(data)

    if a.preview:
        rgb = np.array([[(c >> 8) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17] for c in words], np.uint8)
        picks = np.linspace(0, len(indices) - 1, min(8, len(indices))).astype(int)
        tiles = [ag.preview(indices[i], rgb, 3) for i in picks]
        sheet = Image.new('RGB', (tiles[0].width * 4, tiles[0].height * ((len(tiles) + 3) // 4)))
        for n, t in enumerate(tiles):
            sheet.paste(t, ((n % 4) * t.width, (n // 4) * t.height))
        sheet.save(a.preview)

    count, avg, worst = kxl.stats(data)
    print(f'{a.dst}: {count} frames {w}x{h}, {len(free)} of {a.colors} colours used, '
          f'{len(data) // 1024} KB, average {avg:.1f} KB/s, worst block {worst:.1f} KB/s')


if __name__ == '__main__':
    main()
