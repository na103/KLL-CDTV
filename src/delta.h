#ifndef KLL_DELTA_H
#define KLL_DELTA_H

#include <exec/types.h>

/*
 * Applies the video data of a KXL3 frame (format described in tools/kxl.py)
 * to the frame buffer (bitplanes one after another, plane_size bytes each).
 * *sum is the sum of the frame buffer bytes, updated on every write.
 * Returns FALSE if a copy would run past the end of a bitplane: decoding
 * stops there.
 */
BOOL delta_decode(const UBYTE *p, UBYTE *fb, ULONG plane_size, UWORD planes, UWORD *sum);

#endif
