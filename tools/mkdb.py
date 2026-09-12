#!/usr/bin/env python3
"""Build the entry database from the .TXT files of the original CD.

  mkdb.py DATAFILE OUT.DB [--list]

Every DATAFILE/<CATEGORY>.TXT file holds records of 12 lines:

  1 code (U003)            7 text file
  2 first letter           8 description audio
  3 English                9 audio of the entry
  4 Klingon               10 "more" audio (M005T.WAV when there is none)
  5 .AVI clip             11 comment on a wrong answer, in Klingon
  6 .BMP picture          12 the same comment, in English

KDB1 format (big endian): a header with one entry per category (number of
entries, offset of the table, name), then for each category the table of
record offsets and the records themselves:

  UBYTE flags (bit 0: has the "more" audio), UBYTE length of the Klingon,
        of the English, 0
  char base[6][10]  audio, "more" audio, clip, picture and the two comments
                    on a wrong answer (without extension)
  char klingon[], english[]  zero-terminated
"""
import argparse
import os
import struct

import layout

FIELDS = 12
NO_MORE = 'M005T'	# placeholder: the entry has no "more" audio
PHON_DIR = 'PHON'	# extra category: the 34 pronunciation phonemes
HINT_DIR = 'HINT'	# and the drill hints, one per kind
HELP_DIR = 'HELP'	# and the help texts, one per command


def stem(name):
    return name.strip().rsplit('.', 1)[0].upper()


def read_records(path):
    rows = open(path, 'rb').read().decode('latin1').split('\r\n')
    recs = []
    for i in range(0, len(rows) - FIELDS + 1, FIELDS):
        r = [c.strip() for c in rows[i:i + FIELDS]]
        if not r[0]:
            break
        recs.append(r)
    return recs


def phoneme_records(txtdir):
    """The 34 phonemes, in the same fields as the entry records.

    The example text and its translation live in TXTFILES/P<nn>.TXT; the clip
    and the two audio files come from layout.PHONEMES. Phonemes have no
    picture, so that field stays empty.
    """
    recs = []
    for i, (audio, video, more) in enumerate(layout.PHONEMES, 1):
        base = layout.PHON_TXT.get(i, audio)
        lines = open(f'{txtdir}/{base}.TXT', 'rb').read().decode('latin1').splitlines()
        r = [''] * FIELDS
        r[0] = audio
        r[2] = lines[1].strip() if len(lines) > 1 else ''
        r[3] = lines[0].strip() if lines else ''
        r[4] = video + '.AVI'
        r[8] = audio + '.WAV'
        r[9] = more + '.WAV'
        recs.append(r)
    return recs


def text_records(txtdir, names):
    """Records with text only, for the drill hints and the help."""
    recs = []
    for name in names:
        text = open(f'{txtdir}/{name}.TXT', 'rb').read().decode('latin1')
        r = [''] * FIELDS
        r[0] = name
        r[3] = ' '.join(text.split())	# a single line, whitespace collapsed
        recs.append(r)
    return recs


def txtdir_of(datadir):
    return os.path.join(os.path.dirname(os.path.abspath(datadir)), 'TXTFILES')


def all_categories(datadir):
    """The eight entry categories, plus phonemes, hints and help."""
    cats = []
    for dirname, label in layout.CATEGORIES:
        cats.append((dirname, label, read_records(f'{datadir}/{dirname}.TXT')))
    txtdir = txtdir_of(datadir)
    cats.append((PHON_DIR, 'Phonemes', phoneme_records(txtdir)))
    cats.append((HINT_DIR, 'Hints', text_records(txtdir, layout.DRILL_HINTS)))
    cats.append((HELP_DIR, 'Help',
                 text_records(txtdir, [f for _, f in layout.HELP_TEXTS])))
    return cats


# curly quotes and apostrophes in the CD files: in Klingon the apostrophe is
# a letter, so it has to stay as a plain apostrophe
SMART = {0x91: "'", 0x92: "'", 0x93: '"', 0x94: '"', 0x96: '-', 0x97: '-'}


def plain(s):
    return ''.join(SMART.get(ord(c), c) for c in s)


def ascii_bytes(s, where):
    s = plain(s)
    out = s.encode('ascii', 'replace')
    if b'?' in out and '?' not in s:
        print(f'warning: non-ASCII characters in {where}: {s!r}')
    return out


def field(s, n):
    b = plain(s).encode('ascii', 'replace')[:n - 1]
    return b + b'\0' * (n - len(b))


def build(datadir, out, show=False):
    cats = all_categories(datadir)

    # the records are written after the header and the tables
    head = 4 + 2 + len(cats) * (2 + 4 + 12)
    tables = head
    off = tables + sum(len(r) * 4 for _, _, r in cats)

    blobs, offsets = [], []
    for dirname, label, recs in cats:
        offs = []
        for r in recs:
            more = stem(r[9])
            kl = ascii_bytes(r[3], f'{dirname} {r[0]} klingon')
            en = ascii_bytes(r[2], f'{dirname} {r[0]} inglese')
            body = struct.pack('>BBBB', 1 if more != NO_MORE else 0, len(kl), len(en), 0)
            body += field(stem(r[8]), 10) + field('' if more == NO_MORE else more, 10)
            body += field(stem(r[4]), 10) + field(stem(r[5]), 10)
            body += field(stem(r[10]), 10) + field(stem(r[11]), 10)
            body += kl + b'\0' + en + b'\0'
            if len(body) & 1:
                body += b'\0'
            offs.append(off)
            off += len(body)
            blobs.append(body)
        offsets.append(offs)

    with open(out, 'wb') as f:
        f.write(b'KDB1' + struct.pack('>H', len(cats)))
        pos = tables
        for (dirname, label, recs), offs in zip(cats, offsets):
            f.write(struct.pack('>HI', len(recs), pos) + field(label, 12))
            pos += len(recs) * 4
        for offs in offsets:
            f.write(struct.pack(f'>{len(offs)}I', *offs))
        for b in blobs:
            f.write(b)

    total = sum(len(r) for _, _, r in cats)
    print(f'{out}: {len(cats)} categories, {total} entries, {off} bytes')
    if show:
        for (dirname, label, recs) in cats:
            print(f'  {label:10s} {len(recs):3d} entries  ({dirname})')


def assets(datadir):
    """List of files to convert: (category, kind, source name, stem)."""
    out = []
    for dirname, label, recs in all_categories(datadir):
        for r in recs:
            if r[5]:
                out.append((dirname, 'img', r[5], stem(r[5])))
            out.append((dirname, 'snd', r[8], stem(r[8])))
            for wrong in (r[10], r[11]):
                if wrong:
                    out.append((dirname, 'snd', wrong, stem(wrong)))
            if stem(r[9]) != NO_MORE:
                out.append((dirname, 'snd', r[9], stem(r[9])))
            out.append((dirname, 'vid', r[4], stem(r[4])))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('datadir')
    ap.add_argument('out')
    ap.add_argument('--list', action='store_true', help='list the categories')
    args = ap.parse_args()
    build(args.datadir, args.out, args.list)


if __name__ == '__main__':
    main()
