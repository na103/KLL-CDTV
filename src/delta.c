/* KXL3 delta video decoding; no Amiga dependencies, so it can be tested on the host. */
#include "delta.h"

BOOL delta_decode(const UBYTE *p, UBYTE *fb, ULONG plane_size, UWORD planes, UWORD *sum)
{
	UBYTE *dst = fb;
	UWORD pln, s = *sum;
	BOOL ok = TRUE;

	for (pln = 0; pln < planes && ok; pln++) {
		UBYTE *end = dst + plane_size;
		UBYTE *next = end;

		for (;;) {
			UBYTE skip = *p++;
			UBYTE n = *p++;

			if (!(skip | n))
				break;
			dst += skip;
			if (dst + n > end) {
				ok = FALSE;
				break;
			}
			while (n--) {
				s += *p - *dst;
				*dst++ = *p++;
			}
		}
		dst = next;
	}
	*sum = s;
	return ok;
}
