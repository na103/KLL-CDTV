"""Shared helpers for converting graphics to the Amiga OCS format."""
import numpy as np
from PIL import Image

# 4x4 Bayer matrix, centred on zero
BAYER4 = np.array([[0, 8, 2, 10],
                   [12, 4, 14, 6],
                   [3, 11, 1, 9],
                   [15, 7, 13, 5]], np.float32) / 16.0 - 0.46875

# perceptual weights for colour distance
WEIGHTS = np.array([0.30, 0.59, 0.11], np.float32)


def snap12(rgb):
    """Round 0..255 colours to the 4 bits per channel of OCS (still 0..255)."""
    rgb = np.asarray(rgb, np.float32)
    return (np.clip(np.rint(rgb / 17.0), 0, 15) * 17).astype(np.uint8)


def palette_words(pal):
    """Palette (n,3) 0..255 -> list of UWORDs, 0x0RGB."""
    return [((int(r) // 17) << 8) | ((int(g) // 17) << 4) | (int(b) // 17)
            for r, g, b in pal]


def nearest(pixels, pal, chunk=65536):
    """Index of the nearest palette colour for every pixel (N,3)."""
    pix = np.asarray(pixels, np.float32).reshape(-1, 3)
    pal = np.asarray(pal, np.float32)
    out = np.empty(len(pix), np.uint8)
    for i in range(0, len(pix), chunk):
        d = pix[i:i + chunk, None, :] - pal[None, :, :]
        out[i:i + chunk] = np.argmin((d * d * WEIGHTS).sum(axis=2), axis=1)
    return out


def _min_dist(colors, cents):
    d = colors[:, None, :] - cents[None, :, :]
    return (d * d * WEIGHTS).sum(axis=2).min(axis=1)


def make_palette(pixels, n, fixed=(), iters=20, power=1.0):
    """A palette of n 12-bit colours: k-means weighted over the 12-bit histogram.

    The colours in `fixed` take the first indices and are never moved. Working
    on the 12-bit histogram keeps many near-identical shades (the blacks, say)
    from producing duplicate colours once rounded. Each colour is weighted by
    its pixel count raised to `power`: below 1, large flat areas (dark
    backgrounds) count for less and more colours are left for the detail.
    """
    colors, counts = np.unique(snap12(np.asarray(pixels).reshape(-1, 3)), axis=0, return_counts=True)
    colors = colors.astype(np.float32)
    weights = counts.astype(np.float32) ** power
    fixed = np.array(fixed, np.float32).reshape(-1, 3)
    nfix = len(fixed)

    if len(colors) + nfix <= n:
        extra = [c for c in colors if not any((c == f).all() for f in fixed)]
        pal = np.vstack([fixed, np.array(extra, np.float32).reshape(-1, 3)])
        return snap12(np.vstack([pal, np.repeat(pal[:1], n - len(pal), axis=0)]))

    # seeding: the most frequent colour, then the worst-rendered ones
    cent = np.vstack([fixed, np.zeros((n - nfix, 3), np.float32)])
    start = nfix
    if nfix == 0:
        cent[0] = colors[np.argmax(weights)]
        start = 1
    for j in range(start, n):
        cent[j] = colors[np.argmax(_min_dist(colors, cent[:j]) * np.sqrt(weights))]

    for _ in range(iters):
        idx = nearest(colors, cent)
        for j in range(nfix, n):
            m = idx == j
            if m.any():
                cent[j] = (colors[m] * weights[m, None]).sum(axis=0) / weights[m].sum()
        # a centroid that collides with another once rounded is moved to
        # the colour contributing most to the error
        seen = set()
        for j in range(n):
            key = tuple(snap12(cent[j]))
            if key in seen and j >= nfix:
                others = np.delete(cent, j, axis=0)
                cent[j] = colors[np.argmax(_min_dist(colors, others) * weights)]
                key = tuple(snap12(cent[j]))
            seen.add(key)
    return snap12(cent)


def map_sequence(frames, pal, hysteresis=0.0):
    """Map a sequence of frames (N,H,W,3) to the palette, without dithering.

    With `hysteresis` > 0 a pixel keeps the index it had in the previous frame
    when its error exceeds the best colour by less than the threshold
    (weighted squared distance): still pixels then stop flickering between two
    near colours, which on screen looks like an animated dither pattern.
    """
    pal = np.asarray(pal, np.float32)
    out = np.empty(frames.shape[:3], np.uint8)
    prev = None
    for n, frame in enumerate(frames):
        pix = frame.reshape(-1, 3).astype(np.float32)
        d = ((pix[:, None, :] - pal[None, :, :]) ** 2 * WEIGHTS).sum(axis=2)
        best = d.argmin(axis=1)
        if prev is not None and hysteresis > 0:
            rows = np.arange(len(pix))
            best = np.where(d[rows, prev] <= d[rows, best] + hysteresis, prev, best)
        out[n] = best.reshape(frame.shape[:2])
        prev = best
    return out


def map_ordered(rgb, pal, strength=20.0):
    """Map an image (H,W,3) to the palette with ordered dithering (stable across frames)."""
    h, w, _ = rgb.shape
    thr = np.tile(BAYER4, (h // 4 + 1, w // 4 + 1))[:h, :w, None] * strength
    return nearest(rgb.astype(np.float32) + thr, pal).reshape(h, w)


def map_fs(rgb, pal):
    """Map an image (H,W,3) to the palette with Floyd-Steinberg dithering."""
    n = len(pal)
    padded = np.vstack([pal, np.repeat(pal[:1], 256 - n, axis=0)]).astype(np.uint8)
    palimg = Image.new('P', (1, 1))
    palimg.putpalette(padded.flatten().tolist())
    q = Image.fromarray(np.asarray(rgb, np.uint8), 'RGB').quantize(
        palette=palimg, dither=Image.Dither.FLOYDSTEINBERG)
    idx = np.array(q, np.uint8)
    # the copies of colour 0 used as padding go back to index 0
    idx[idx >= n] = 0
    return idx


def planar(idx, planes):
    """Indices (H,W) -> Amiga bitplanes: plane 0 first, rows aligned to 16 bits."""
    h, w = idx.shape
    aligned = (w + 15) // 16 * 16
    out = bytearray()
    for p in range(planes):
        bits = np.zeros((h, aligned), np.uint8)
        bits[:, :w] = (idx >> p) & 1
        out += np.packbits(bits, axis=1).tobytes()
    return bytes(out)


def preview(idx, pal, scale=2):
    """A scaled-up PIL image of the indexed picture."""
    im = Image.fromarray(np.asarray(pal, np.uint8)[idx], 'RGB')
    return im.resize((im.width * scale, im.height * scale), Image.Resampling.NEAREST)
