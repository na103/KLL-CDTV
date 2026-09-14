#!/usr/bin/env python3
"""Printable CD cover (for personal use only, like the CD itself).

  mkcover.py SRC OUT        (make cover: SRC = build/src/FIN_IMS2, OUT = build/cover)

Writes to OUT:
  front.png      front booklet, 120x120 mm (+3 mm bleed)
  back.png       tray inlay with the spines, 151x118 mm (+3 mm bleed)
  cover_A4.pdf   both pieces on an A4 sheet, with crop and fold marks

The emblem, the Simon & Schuster logo and the screens come from the original
CD you extracted; the style follows the Star Trek: Klingon box (1996) and the
covers of the CDTV titles (dark band with the logo at the top, coloured band
with label and category at the bottom). The free (OFL) fonts are downloaded
from Google Fonts on the first run, into OUT/fonts. Print at 100%, no fitting.
"""
import argparse
import functools
import os
import random
import subprocess
import sys
import urllib.request

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

FONT_BASE = 'https://github.com/google/fonts/raw/main/ofl/'
FONT_URLS = {
    'Orbitron-VF.ttf': 'orbitron/Orbitron%5Bwght%5D.ttf',
    'Cinzel-VF.ttf': 'cinzel/Cinzel%5Bwght%5D.ttf',
    'Oswald-VF.ttf': 'oswald/Oswald%5Bwght%5D.ttf',
    'ArchivoNarrow-VF.ttf': 'archivonarrow/ArchivoNarrow%5Bwght%5D.ttf',
    'RussoOne-Regular.ttf': 'russoone/RussoOne-Regular.ttf',
}

IMS = FONTS = SANS = None	# set by main()

DPI = 300
BLEED = 3.0

ORANGE = (250, 150, 20)
RED = (220, 30, 40)
BAND = (18, 18, 20)


def mm(v):
    return int(round(v * DPI / 25.4))


def pt(v):
    return int(round(v * DPI / 72))


def X(v):
    """A position in mm from the trim edge, in pixels of the canvas with bleed."""
    return mm(v + BLEED)


def ensure_fonts():
    os.makedirs(FONTS, exist_ok=True)
    for name, url in FONT_URLS.items():
        path = os.path.join(FONTS, name)
        if not os.path.isfile(path):
            print(f'  downloading {name}', flush=True)
            urllib.request.urlretrieve(FONT_BASE + url, path + '.tmp')
            os.replace(path + '.tmp', path)


def system_font(family):
    """A bold system sans serif for the small print; without fontconfig it
    falls back to Archivo Narrow."""
    try:
        path = subprocess.run(['fc-match', '-f', '%{file}', family],
                              capture_output=True, text=True).stdout.strip()
    except OSError:
        path = ''
    return path if os.path.isfile(path) else os.path.join(FONTS, 'ArchivoNarrow-VF.ttf')


def font(name, px, weight=None):
    path = name if os.path.isabs(name) else os.path.join(FONTS, name)
    f = ImageFont.truetype(path, max(4, int(px)))
    if weight is not None:
        try:
            f.set_variation_by_axes([weight])
        except (OSError, AttributeError, ValueError):
            pass
    return f


# ------------------------------------------------------------------ text

def text_mask(text, fnt, tracking=0.0):
    asc, desc = fnt.getmetrics()
    if tracking:
        widths = [fnt.getlength(c) for c in text]
        w = int(sum(widths) + tracking * (len(text) - 1)) + 8
    else:
        w = int(fnt.getlength(text)) + 8
    m = Image.new('L', (w, asc + desc + 8), 0)
    d = ImageDraw.Draw(m)
    if tracking:
        x = 4
        for c, cw in zip(text, widths):
            d.text((x, 4), c, font=fnt, fill=255)
            x += cw + tracking
    else:
        d.text((4, 4), text, font=fnt, fill=255)
    return m.crop(m.getbbox())


def fitted(text, name, max_w, max_h, weight=None, tracking_em=0.0):
    """Mask of the text, the largest that fits in max_w x max_h pixels."""
    size = 200
    for _ in range(4):
        m = text_mask(text, font(name, size, weight), tracking_em * size)
        size = max(4, int(size * min(max_w / m.width, max_h / m.height)))
    while True:
        m = text_mask(text, font(name, size, weight), tracking_em * size)
        if (m.width <= max_w and m.height <= max_h) or size <= 4:
            return m
        size -= max(1, size // 50)


def vgrad(size, top, bottom):
    w, h = size
    t = np.linspace(0, 1, h)[:, None, None]
    row = np.array(top) * (1 - t) + np.array(bottom) * t
    return Image.fromarray(np.repeat(row, w, axis=1).astype('uint8'))


def paint(canvas, mask, x, y, fill):
    layer = Image.new('RGB', mask.size, fill) if isinstance(fill, tuple) else fill
    canvas.paste(layer, (int(x), int(y)), mask)


def paint_rgba(canvas, img, x, y):
    canvas.paste(img, (int(x), int(y)), img)


def wrap(text, fnt, max_w):
    lines, cur = [], ''
    for word in text.split():
        t = (cur + ' ' + word).strip()
        if fnt.getlength(t) <= max_w:
            cur = t
        else:
            lines.append(cur)
            cur = word
    lines.append(cur)
    return lines


# --------------------------------------------------------------- effects

def glow(canvas, cx, cy, rx, ry, color, alpha):
    w, h = canvas.size
    y, x = np.ogrid[:h, :w]
    a = np.clip(1 - ((x - cx) / rx) ** 2 - ((y - cy) / ry) ** 2, 0, 1) ** 2 * alpha
    arr = np.asarray(canvas).astype(np.float32)
    arr = arr * (1 - a[..., None]) + np.array(color, np.float32) * a[..., None]
    canvas.paste(Image.fromarray(arr.astype('uint8')))


def stars(canvas, box, count, seed):
    rnd = random.Random(seed)
    d = ImageDraw.Draw(canvas)
    x0, y0, x1, y1 = box
    for _ in range(count):
        x, y = rnd.uniform(x0, x1), rnd.uniform(y0, y1)
        v = int(rnd.uniform(60, 200) * rnd.random())
        r = rnd.choice((1, 1, 1, 2, 2, 3))
        d.ellipse((x - r, y - r, x + r, y + r), fill=(v, v, min(255, v + 20)))


# ------------------------------------------------------ from the original CD

@functools.lru_cache(None)
def emblem():
    """The Klingon emblem of the title screen, redrawn at 10x: a geometric ring
    on the circle that best fits the red pixels, and smoothed blades."""
    src = np.asarray(Image.open(os.path.join(IMS, 'SPLASH.BMP')).convert('RGB')).astype(int)
    crop = src[108:396, 128:512]
    h, w = crop.shape[:2]
    s = 10
    orange = np.abs(crop - np.array((253, 146, 4))).sum(-1) < 120
    red = np.abs(crop - np.array((239, 22, 41))).sum(-1) < 120

    blades = Image.fromarray((orange * 255).astype('uint8')).resize((w * s, h * s), Image.BICUBIC)
    blades = np.asarray(blades.filter(ImageFilter.GaussianBlur(s * 1.2))) > 127

    ys, xs = np.nonzero(red)
    a = np.c_[2 * xs, 2 * ys, np.ones(len(xs))]
    cx, cy, c = np.linalg.lstsq(a, xs ** 2 + ys ** 2, rcond=None)[0]
    r = np.sqrt(c + cx ** 2 + cy ** 2) * s
    t = (np.median(np.abs(np.hypot(xs - cx, ys - cy) - r / s)) * 2 + 2) * s
    cx, cy = cx * s, cy * s
    ring = Image.new('L', (w * s, h * s), 0)
    d = ImageDraw.Draw(ring)
    d.ellipse((cx - r - t / 2, cy - r - t / 2, cx + r + t / 2, cy + r + t / 2), fill=255)
    d.ellipse((cx - r + t / 2, cy - r + t / 2, cx + r - t / 2, cy + r - t / 2), fill=0)
    gap = Image.fromarray((blades * 255).astype('uint8')).filter(ImageFilter.MaxFilter(int(s * 2.6) | 1))
    ring = (np.asarray(ring) > 127) & ~(np.asarray(gap) > 127)

    rgba = np.zeros((h * s, w * s, 4), 'uint8')
    rgba[ring] = RED + (255,)
    rgba[blades] = ORANGE + (255,)
    img = Image.fromarray(rgba, 'RGBA')
    return img.crop(img.getbbox())


@functools.lru_cache(None)
def ssi_logo():
    g = np.asarray(Image.open(os.path.join(IMS, 'SPLASH02.BMP')).convert('L')).astype(int)
    ys, xs = np.nonzero(g > 128)
    c = g[ys.min() - 2:ys.max() + 3, xs.min() - 2:xs.max() + 3]
    s = 8
    m = Image.fromarray(c.astype('uint8')).resize((c.shape[1] * s, c.shape[0] * s), Image.BICUBIC)
    m = m.filter(ImageFilter.GaussianBlur(s * 0.5)).point(lambda v: 255 if v > 128 else 0)
    img = Image.new('RGBA', m.size, (255, 255, 255, 0))
    img.putalpha(m)
    return img


def screen(path):
    """A screen the way the CDTV shows it: 320 columns, 32 colours."""
    im = Image.open(path).convert('RGB').resize((320, 240), Image.BOX)
    im = im.quantize(32).convert('RGB')
    return im.resize((640, 480), Image.NEAREST)


def scaled(img, w=None, h=None):
    if w is None:
        w = img.width * h / img.height
    if h is None:
        h = img.height * w / img.width
    return img.resize((max(1, int(w)), max(1, int(h))), Image.LANCZOS)


# ------------------------------------------------------------- CDTV logo

def cdtv_logo(height, color, sub=True):
    """The CDTV logo: heavy square letters, a thin tilted ellipse around "CD",
    TM at the top and MULTIMEDIA underneath on the right."""
    letters = text_mask('CDTV', font('RussoOne-Regular.ttf', 160))
    letters = scaled(letters, h=height)
    tw, th = letters.size
    W, H = int(tw * 1.3), int(th * 2.1)
    ox, oy = int(tw * 0.14), int(th * 0.5)
    m = Image.new('L', (W, H), 0)
    m.paste(255, (ox, oy, ox + tw, oy + th), letters)

    ring = Image.new('L', (W, H), 0)
    ecx, ecy = ox + tw * 0.30, oy + th * 0.58
    ew, eh = tw * 0.80, th * 1.42
    ImageDraw.Draw(ring).ellipse((ecx - ew / 2, ecy - eh / 2, ecx + ew / 2, ecy + eh / 2),
                                 outline=255, width=max(1, int(th * 0.055)))
    m = ImageChops.lighter(m, ring.rotate(12, center=(ecx, ecy), resample=Image.BICUBIC))

    tm = fitted('TM', SANS, tw, th * 0.2)
    m.paste(255, (ox + tw + int(th * 0.04), oy - int(th * 0.08)), tm)
    if sub:
        mult = fitted('MULTIMEDIA', SANS, tw * 0.62, th * 0.19)
        mx = ox + tw - mult.width
        m.paste(255, (mx, oy + th + int(th * 0.14)), mult)
    m = m.crop(m.getbbox())
    img = Image.new('RGBA', m.size, color + (0,))
    img.putalpha(m)
    return img


# ----------------------------------------------------------------- front

def front():
    size = mm(120 + 2 * BLEED)
    c = Image.new('RGB', (size, size), (0, 0, 0))
    stars(c, (0, X(13), size, X(104)), 900, 1)
    glow(c, X(60), X(78), mm(55), mm(42), (120, 10, 12), 0.55)
    glow(c, X(60), X(36), mm(70), mm(16), (60, 8, 8), 0.5)

    # CDTV band at the top
    ImageDraw.Draw(c).rectangle((0, 0, size, X(13)), fill=BAND)
    ImageDraw.Draw(c).rectangle((0, X(13), size, X(13) + mm(0.5)), fill=(150, 150, 155))
    logo = cdtv_logo(mm(6.2), (255, 255, 255))
    paint_rgba(c, logo, X(9), X(6.7) - logo.height / 2)
    tag = fitted('INTERACTIVE LANGUAGE COURSE', SANS, mm(52), mm(2.6), tracking_em=0.12)
    paint(c, tag, X(111) - tag.width, X(6.7) - tag.height / 2, (170, 170, 175))

    # title, as on the box
    st = fitted('STAR TREK', 'Orbitron-VF.ttf', mm(98), mm(10.5), weight=900, tracking_em=0.04)
    paint(c, st, X(60) - st.width / 2, X(17), vgrad(st.size, (255, 255, 255), (165, 170, 182)))
    kl = fitted('KLINGON', 'Cinzel-VF.ttf', mm(108), mm(21), weight=900)
    kx, ky = X(60) - kl.width / 2, X(29)
    # outline and shadow on a padded mask, otherwise the blur stops at the box
    p = mm(3)
    edge = ImageOps.expand(kl, p).filter(ImageFilter.MaxFilter(9))
    shadow = edge.filter(ImageFilter.GaussianBlur(mm(1.2)))
    paint(c, shadow, kx - p, ky - p + mm(0.6), (90, 0, 0))
    paint(c, edge, kx - p, ky - p, (45, 0, 2))
    paint(c, kl, kx, ky, vgrad(kl.size, (255, 80, 60), (140, 8, 14)))
    ll = fitted('LANGUAGE LAB', 'Orbitron-VF.ttf', mm(74), mm(5), weight=700, tracking_em=0.35)
    paint(c, ll, X(60) - ll.width / 2, X(51.5), ORANGE)

    # emblem
    em = scaled(emblem(), h=mm(42))
    p = mm(6)
    halo = ImageOps.expand(em.getchannel('A'), p).filter(ImageFilter.GaussianBlur(mm(2.2)))
    halo = halo.point(lambda v: int(v * 0.55))
    ex, ey = X(60) - em.width / 2, X(57)
    paint(c, halo, ex - p, ey - p, (255, 110, 0))
    paint_rgba(c, em, ex, ey)

    # bottom band with label, category and publisher
    band = vgrad((size, size - X(104)), (196, 22, 30), (100, 8, 14))
    c.paste(band, (0, X(104)))
    ImageDraw.Draw(c).rectangle((0, X(104), size, X(104) + mm(0.4)), fill=(255, 190, 120))
    lod = fitted('LEARN OR DIE!', 'Cinzel-VF.ttf', mm(50), mm(5.2), weight=900, tracking_em=0.08)
    paint(c, lod, X(6), X(111.5) - lod.height / 2, (255, 225, 170))

    d = ImageDraw.Draw(c)
    inner = cdtv_logo(mm(3.8), (0, 0, 0), sub=False)
    mult = fitted('Multimedia', SANS, mm(18), mm(3.0))
    lx0, ly0, ly1 = X(60), X(100.5), X(108.5)
    lx1 = lx0 + mm(2) + inner.width + mm(2.5) + mult.width + mm(2.5)
    d.rounded_rectangle((lx0, ly0, lx1, ly1), radius=mm(0.8), fill=(250, 250, 250),
                        outline=(0, 0, 0), width=mm(0.35))
    paint_rgba(c, inner, lx0 + mm(2), (ly0 + ly1) / 2 - inner.height / 2)
    paint(c, mult, lx0 + mm(4.5) + inner.width, (ly0 + ly1) / 2 - mult.height / 2 + mm(0.3),
          (60, 60, 60))
    cat = fitted('Education', SANS, mm(30), mm(3.4))
    paint(c, cat, X(61), X(110.3), (255, 255, 255))
    age = fitted('For Teens & Adults', SANS, mm(34), mm(2.6))
    paint(c, age, X(61), X(114.6), (255, 235, 235))

    ss = scaled(ssi_logo(), h=mm(13))
    paint_rgba(c, ss, X(116) - ss.width, X(105.8))
    return c


# ------------------------------------------------------------------ back

SPINE = 6.5
BACK_W, BACK_H = 151.0, 118.0


def spine_strip(length, width, flip):
    """A spine, drawn horizontally and then rotated. The first rows (y below
    the bleed) end up on the outer edge and are trimmed away, so the content
    is centred on the visible part; the same goes for the bleed at both ends."""
    s = Image.new('RGB', (length, width), (0, 0, 0))
    bl = mm(BLEED)
    vis = width - bl
    mid = bl + vis / 2
    logo = scaled(cdtv_logo(int(vis * 0.5), (255, 255, 255), sub=False), h=int(vis * 0.5))
    title = fitted('STAR TREK: KLINGON', 'Orbitron-VF.ttf', length * 0.42, vis * 0.42,
                   weight=900, tracking_em=0.05)
    sub = fitted('LANGUAGE LAB', 'Orbitron-VF.ttf', length * 0.2, vis * 0.3,
                 weight=700, tracking_em=0.3)
    em = scaled(emblem(), h=int(vis * 0.72))
    x = bl + mm(5)
    paint_rgba(s, logo, x, mid - logo.height / 2)
    x += logo.width + mm(6)
    paint(s, title, x, mid - title.height / 2, (255, 255, 255))
    x += title.width + mm(4)
    paint(s, sub, x, mid - sub.height / 2, ORANGE)
    paint_rgba(s, em, length - bl - mm(5) - em.width, mid - em.height / 2)
    return s.rotate(-90 if flip else 90, expand=True)


def back():
    W, H = mm(BACK_W + 2 * BLEED), mm(BACK_H + 2 * BLEED)
    c = Image.new('RGB', (W, H), (0, 0, 0))
    left, right = X(SPINE), X(BACK_W - SPINE)
    stars(c, (left, X(11), right, X(100)), 700, 2)
    glow(c, X(BACK_W / 2), X(55), mm(75), mm(45), (90, 8, 10), 0.5)
    d = ImageDraw.Draw(c)

    # spines
    for x0, flip in ((0, False), (right, True)):
        strip = spine_strip(H, X(SPINE) if not flip else W - right, flip)
        c.paste(strip, (x0, 0))
    for x in (left, right):
        d.line((x, 0, x, H), fill=(60, 60, 60), width=2)

    # CDTV band at the top
    d.rectangle((left + 2, 0, right - 2, X(11)), fill=BAND)
    d.rectangle((left + 2, X(11), right - 2, X(11) + mm(0.5)), fill=(150, 150, 155))
    logo = cdtv_logo(mm(5.2), (255, 255, 255))
    paint_rgba(c, logo, X(SPINE + 5), X(5.8) - logo.height / 2)
    # Orbitron has no middle dot: a dash as the separator
    head = fitted('STAR TREK: KLINGON  -  LANGUAGE LAB', 'Orbitron-VF.ttf', mm(90), mm(2.8),
                  weight=700, tracking_em=0.08)
    paint(c, head, X(BACK_W - SPINE - 5) - head.width, X(5.8) - head.height / 2, (185, 185, 190))

    # headline and text
    x0, x1 = X(SPINE + 6), X(BACK_W - SPINE - 6)
    k = fitted("tlhIngan Hol Dajatlh'a'?", 'Oswald-VF.ttf', mm(100), mm(8), weight=700)
    paint(c, k, x0, X(15), vgrad(k.size, (255, 80, 60), (170, 12, 18)))
    q = fitted('Do you speak Klingon?', 'Oswald-VF.ttf', mm(60), mm(3.6), weight=400)
    paint(c, q, x0 + k.width + mm(4), X(15) + k.height - q.height, (230, 230, 230))

    body = font('ArchivoNarrow-VF.ttf', pt(9.2), weight=500)
    text = ("Learn the language of the Klingon Empire from the leader of the High "
            "Council himself. Gowron (Robert O'Reilly) speaks every word and phrase "
            "on video, with the voice of Marc Okrand, the linguist who created the "
            "Klingon language. Study the words, master the sounds, then prove your "
            "worth in the drills - a warrior who hesitates is a warrior who fails.")
    y = X(26)
    for line in wrap(text, body, x1 - x0):
        d.text((x0, y), line, font=body, fill=(225, 225, 225))
        y += int(pt(9.2) * 1.28)

    # screens
    shots = [
        (os.path.join(IMS, 'MAINB.BMP'), 'MENU', 'Words and phrases in eight categories'),
        (os.path.join(IMS, 'PRONUNB.BMP'), 'PHONEME', 'The 34 sounds of Klingon'),
        (os.path.join(IMS, 'FOOD', 'DRILL02.BMP'), 'DRILL', 'Eleven questions, then the verdict'),
    ]
    sw, sh = mm(40), mm(30)
    gap = ((x1 - x0) - 3 * sw) / 2
    sy = X(49)
    cap = font('ArchivoNarrow-VF.ttf', pt(7.4), weight=500)
    capb = font('Orbitron-VF.ttf', pt(6.6), weight=900)
    for i, (path, label, desc) in enumerate(shots):
        if not os.path.isfile(path):
            path = os.path.join(IMS, 'HOLB.BMP')
        sx = int(x0 + i * (sw + gap))
        d.rectangle((sx - mm(0.6), sy - mm(0.6), sx + sw + mm(0.6), sy + sh + mm(0.6)),
                    fill=(70, 70, 75))
        c.paste(screen(path).resize((sw, sh), Image.NEAREST), (sx, sy))
        d.text((sx, sy + sh + mm(1.8)), label, font=capb, fill=ORANGE)
        d.text((sx, sy + sh + mm(1.8) + pt(8.4)), desc, font=cap, fill=(210, 210, 210))

    cats = fitted('PHRASES · MYTHS & LEGENDS · CURSES · HOL · COMMANDS · FOOD & DRINK · WEAPONS · RITUALS',
                  SANS, x1 - x0, mm(2.6), tracking_em=0.06)
    paint(c, cats, (x0 + x1) / 2 - cats.width / 2, X(94.5), (255, 190, 120))

    # bottom band: requirements, notices, publisher
    band = vgrad((right - left - 4, H - X(100)), (150, 16, 22), (70, 6, 10))
    c.paste(band, (left + 2, X(100)))
    d.rectangle((left + 2, X(100), right - 2, X(100) + mm(0.4)), fill=(255, 190, 120))
    bx0, by0, bx1, by1 = x0, X(102.5), x0 + mm(47), X(115.5)
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=mm(0.8), outline=(255, 225, 170), width=mm(0.3))
    small = font(SANS, pt(6.2))
    req = font(SANS, pt(5.6))
    d.text((bx0 + mm(1.6), by0 + mm(1.1)), 'SYSTEM REQUIREMENTS', font=small, fill=(255, 225, 170))
    for n, line in enumerate(('Commodore CDTV · Kickstart 1.3 · 1 MB',
                              'CDTV remote: A selects, B goes back')):
        d.text((bx0 + mm(1.6), by0 + mm(4.6) + n * pt(7.4)), line, font=req, fill=(255, 255, 255))

    regular = SANS.replace('Bold', 'Regular')
    legal = font(regular if os.path.isfile(regular) else SANS, pt(5))
    lx = bx1 + mm(4)
    ss = scaled(ssi_logo(), h=mm(12.5))
    paint_rgba(c, ss, x1 - ss.width, X(102.8))
    lines = wrap("Klingon Language Lab © 1996 Simon & Schuster Interactive, a division of "
                 "Simon & Schuster. STAR TREK and related marks are trademarks of Paramount "
                 "Pictures. CDTV conversion 2026 by na103 - github.com/na103/KLL-CDTV. "
                 "Personal backup of an original disc: not for sale.",
                 legal, x1 - ss.width - mm(4) - lx)
    for n, line in enumerate(lines):
        d.text((lx, X(103) + n * int(pt(5) * 1.35)), line, font=legal, fill=(235, 220, 220))
    return c


# -------------------------------------------------------------- A4 sheet

def crop_marks(d, x, y, w, h, bleed):
    L, gap = mm(5), bleed + mm(1)
    for cx, sx in ((x, -1), (x + w, 1)):
        for cy, sy in ((y, -1), (y + h, 1)):
            d.line((cx + sx * gap, cy, cx + sx * (gap + L), cy), fill=0, width=2)
            d.line((cx, cy + sy * gap, cx, cy + sy * (gap + L)), fill=0, width=2)


def sheet(f, b):
    page = Image.new('RGB', (mm(210), mm(297)), (255, 255, 255))
    d = ImageDraw.Draw(page)
    bl = mm(BLEED)
    note = font(SANS, pt(8))
    d.text((mm(15), mm(7)), 'Klingon Language Lab CDTV - print at 100% (actual size), '
           'do not fit to page', font=note, fill=0)

    fx, fy = (page.width - f.width) // 2, mm(16)
    page.paste(f, (fx, fy))
    crop_marks(d, fx + bl, fy + bl, f.width - 2 * bl, f.height - 2 * bl, bl)
    d.text((fx, fy + f.height + mm(1.5)), 'FRONT - booklet 120 x 120 mm', font=note, fill=(90, 90, 90))

    bx, by = (page.width - b.width) // 2, fy + f.height + mm(12)
    page.paste(b, (bx, by))
    crop_marks(d, bx + bl, by + bl, b.width - 2 * bl, b.height - 2 * bl, bl)
    for s in (SPINE, BACK_W - SPINE):
        x = bx + bl + mm(s)
        for y0, y1 in ((by - mm(7), by - mm(1)), (by + b.height + mm(1), by + b.height + mm(7))):
            d.line((x, y0, x, y1), fill=0, width=1)
    d.text((bx, by + b.height + mm(8)), 'BACK - tray inlay 151 x 118 mm, fold the spines '
           'on the marks (6.5 mm)', font=note, fill=(90, 90, 90))
    return page


def main():
    global IMS, FONTS, SANS
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', help='FIN_IMS2 of the extracted original CD (build/src/FIN_IMS2)')
    ap.add_argument('out', help='directory to write the cover to (build/cover)')
    args = ap.parse_args()

    IMS = args.src
    if not os.path.isfile(os.path.join(IMS, 'SPLASH.BMP')):
        sys.exit(f'{IMS}/SPLASH.BMP not found: the extracted original CD is needed (make extract)')
    FONTS = os.path.join(args.out, 'fonts')
    ensure_fonts()
    SANS = system_font('Liberation Sans:bold')

    f, b = front(), back()
    f.save(os.path.join(args.out, 'front.png'), dpi=(DPI, DPI))
    b.save(os.path.join(args.out, 'back.png'), dpi=(DPI, DPI))
    sheet(f, b).save(os.path.join(args.out, 'cover_A4.pdf'), 'PDF', resolution=DPI)
    print(f'{args.out}: front.png, back.png, cover_A4.pdf')


if __name__ == '__main__':
    main()
