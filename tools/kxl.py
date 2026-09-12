"""The KXL3 video format for the CDTV.

File header (2048 bytes, big endian):
   0 'KXL3'
   4 UWORD  width
   6 UWORD  height
   8 UWORD  bitplanes
  10 UWORD  frames per second
  12 UWORD  audio bytes per frame (signed 8-bit)
  14 UWORD  palette colours
  16 ULONG  block size
  20 ULONG  number of blocks
  24 ULONG  number of frames
  28 ULONG  offset of the first block
  32 palette (UWORDs, 0x0RGB)

Then the blocks, all the same size, read by the player one at a time:
   0 UWORD  frames in the block
   2 UWORD  audio bytes (frames x bytes per frame)
   4 UWORD  bytes used in the block, header included (even)
   6 UWORD  checksum: sum of the UWORDs from offset 8 to the bytes used
   8 the audio of the block frames, contiguous
   for each frame: UWORD video bytes (even), UWORD sum of the frame buffer
   bytes after decoding, then the video data
   zeros up to the end of the block

Video: the difference from the previous frame (the first one against an empty
frame). For every bitplane, seen as a linear sequence of bytes (rows aligned
to 16 bits), pairs of bytes (skip, copy): advance by "skip" bytes and copy the
"copy" bytes that follow. The pair (0, 0) ends the bitplane. Every field of
the video data is a byte, so the decoder never reads a word at an odd
address. The checksums and sums let the player verify reading and decoding.
"""
import struct

import numpy as np

MAGIC = b'KXL3'
FILE_HEADER = struct.Struct('>4sHHHHHHIIII')
BLOCK_HEADER = struct.Struct('>HHHH')
FRAME_HEADER = struct.Struct('>HH')
HEADER_SIZE = 2048
BLOCK_SIZE = 16384
MERGE_GAP = 2       # unchanged bytes kept inside a copy rather than starting a new one


def checksum(data):
    """Sum of the big-endian UWORDs (even length), modulo 65536."""
    return int(np.frombuffer(data, '>u2').sum(dtype=np.uint64)) & 0xFFFF


def to_planes(idx, planes):
    """Indices (H,W) -> array (bitplane, H, bytes per row)."""
    h, w = idx.shape
    rowbytes = (w + 15) // 16 * 2
    out = np.zeros((planes, h, rowbytes), np.uint8)
    for p in range(planes):
        bits = np.zeros((h, rowbytes * 8), np.uint8)
        bits[:, :w] = (idx >> p) & 1
        out[p] = np.packbits(bits, axis=1)
    return out


def planes_to_index(buf, w):
    planes, h, _ = buf.shape
    idx = np.zeros((h, w), np.uint8)
    for p in range(planes):
        idx |= np.unpackbits(buf[p], axis=1)[:, :w] << p
    return idx


def delta_encode(cur, prev):
    """cur, prev: (bitplane, bytes of the plane)."""
    out = bytearray()
    for p in range(cur.shape[0]):
        changed = np.flatnonzero(cur[p] != prev[p])
        runs = []
        for c in changed:
            if runs and c - runs[-1][1] <= MERGE_GAP + 1:
                runs[-1][1] = c
            else:
                runs.append([c, c])
        pos = 0
        for start, end in runs:
            skip = int(start) - pos
            while skip > 255:
                out += bytes((255, 0))
                skip -= 255
            data = cur[p, start:end + 1].tobytes()
            for k in range(0, len(data), 255):
                part = data[k:k + 255]
                out += bytes((skip if k == 0 else 0, len(part))) + part
            pos = int(end) + 1
        out += b'\0\0'
    return bytes(out)


def delta_decode(data, buf):
    """Apply the video data to buf (bitplane, bytes of the plane); returns bytes read."""
    o = 0
    size = buf.shape[1]
    for p in range(buf.shape[0]):
        pos = 0
        while True:
            skip, n = data[o], data[o + 1]
            o += 2
            if skip == 0 and n == 0:
                break
            pos += skip
            if pos + n > size:
                raise ValueError('video data past the end of the bitplane')
            buf[p, pos:pos + n] = np.frombuffer(data, np.uint8, n, o)
            o += n
            pos += n
    return o


def encode(indices, palette, planes, audio, abytes, fps, block_size=BLOCK_SIZE):
    """Indexed frames, palette (UWORDs 0x0RGB) and audio -> contents of a KXL3 file."""
    h, w = indices[0].shape
    plane_size = (w + 15) // 16 * 2 * h
    prev = np.zeros((planes, plane_size), np.uint8)

    blocks = []
    records = []
    first = 0
    used = BLOCK_HEADER.size

    def close_block():
        snd = audio[first * abytes:(first + len(records)) * abytes]
        body = snd + b''.join(records)
        header = BLOCK_HEADER.pack(len(records), len(snd), BLOCK_HEADER.size + len(body), checksum(body))
        blocks.append((header + body).ljust(block_size, b'\0'))

    for n, idx in enumerate(indices):
        cur = to_planes(idx, planes).reshape(planes, plane_size)
        video = delta_encode(cur, prev)
        prev = cur
        video += bytes(len(video) & 1)
        record = FRAME_HEADER.pack(len(video), int(cur.sum()) & 0xFFFF) + video
        need = abytes + len(record)
        if BLOCK_HEADER.size + need > block_size:
            raise ValueError(f'frame {n}: does not fit in a {block_size}-byte block')
        if used + need > block_size:
            close_block()
            records = []
            first = n
            used = BLOCK_HEADER.size
        records.append(record)
        used += need
    close_block()

    header = FILE_HEADER.pack(MAGIC, w, h, planes, fps, abytes, len(palette), block_size,
                              len(blocks), len(indices), HEADER_SIZE)
    header += struct.pack(f'>{len(palette)}H', *palette)
    return header.ljust(HEADER_SIZE, b'\0') + b''.join(blocks)


def read_header(data):
    f = FILE_HEADER.unpack_from(data, 0)
    if f[0] != MAGIC:
        raise ValueError('not a KXL3 file')
    hdr = dict(width=f[1], height=f[2], planes=f[3], fps=f[4], abytes=f[5], colors=f[6],
               block_size=f[7], blocks=f[8], frames=f[9], first_block=f[10])
    hdr['palette'] = list(struct.unpack_from(f'>{f[6]}H', data, FILE_HEADER.size))
    if len(data) != hdr['first_block'] + hdr['blocks'] * hdr['block_size']:
        raise ValueError('file size inconsistent with the header')
    return hdr


def iter_blocks(data, hdr):
    """For each block: (audio, list of (video data, frame buffer sum))."""
    bs = hdr['block_size']
    for b in range(hdr['blocks']):
        base = hdr['first_block'] + b * bs
        n, asize, used, cks = BLOCK_HEADER.unpack_from(data, base)
        if asize != n * hdr['abytes'] or used > bs or used & 1:
            raise ValueError(f'block {b}: inconsistent header')
        if checksum(data[base + BLOCK_HEADER.size:base + used]) != cks:
            raise ValueError(f'block {b}: wrong checksum')
        o = BLOCK_HEADER.size + asize
        frames = []
        for _ in range(n):
            vsize, fbsum = FRAME_HEADER.unpack_from(data, base + o)
            o += FRAME_HEADER.size
            if o + vsize > used:
                raise ValueError(f'block {b}: frame past the end of the data')
            frames.append((data[base + o:base + o + vsize], fbsum))
            o += vsize
        yield data[base + BLOCK_HEADER.size:base + BLOCK_HEADER.size + asize], frames


def decode(data):
    """Decode and check a KXL3 file: (header, list of indexed frames, audio)."""
    hdr = read_header(data)
    w, h, planes = hdr['width'], hdr['height'], hdr['planes']
    rowbytes = (w + 15) // 16 * 2
    buf = np.zeros((planes, rowbytes * h), np.uint8)
    frames = []
    audio = bytearray()
    for snd, records in iter_blocks(data, hdr):
        audio += snd
        for video, fbsum in records:
            if len(video) - delta_decode(video, buf) not in (0, 1):
                raise ValueError(f'frame {len(frames)}: inconsistent video data')
            if int(buf.sum()) & 0xFFFF != fbsum:
                raise ValueError(f'frame {len(frames)}: wrong frame buffer sum')
            frames.append(planes_to_index(buf.reshape(planes, h, rowbytes), w))
    if len(frames) != hdr['frames']:
        raise ValueError('frame count inconsistent with the header')
    return hdr, frames, bytes(audio)


def stats(data):
    """(frames, average KB/s, KB/s demanded by the worst block, first and last aside)."""
    hdr = read_header(data)
    counts = [len(r) for _, r in iter_blocks(data, hdr)]
    seconds = hdr['frames'] / hdr['fps']
    inner = [n for n in counts[1:-1] if n] or [n for n in counts if n]
    worst = max(hdr['block_size'] / 1024 * hdr['fps'] / n for n in inner)
    return hdr['frames'], len(data) / 1024 / seconds, worst
