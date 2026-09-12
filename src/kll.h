#ifndef KLL_H
#define KLL_H

#include <exec/types.h>
#include <intuition/screens.h>
#include <proto/graphics.h>

extern volatile ULONG vbl_count;	/* bumped on every vertical blank (vbl.s) */
extern UWORD vbl_hz;			/* 50 PAL, 60 NTSC */
extern UWORD palette[32];		/* screen colours, 0x0RGB */

void msg(const char *s);
void set_palette(struct Screen *scr);

/*
 * Wait until the raster beam has passed the given screen line. The blitter
 * and RectFill work one bitplane at a time, so an area the beam crosses while
 * it is being written shows up for one frame with its planes mixed (stray
 * pixels in the wrong colours). Large areas are therefore written in stripes,
 * each one as soon as the beam is past it.
 */
#define BEAM_STRIPE	16	/* lines per stripe */

void beam_below(WORD line);

struct MsgPort *port_create(void);
void port_delete(struct MsgPort *mp);

/*
 * Wait for the blitter before touching the source data again. WaitBlit has to
 * be called twice: on a 68000 the blitter-busy bit of DMACONR can still read
 * as clear in the first cycles after a copy is started, so the first check may
 * return at once while the blitter is still reading.
 */
static inline void blit_wait(void)
{
	WaitBlit();
	WaitBlit();
}

static inline UWORD rd16(const UBYTE *p)
{
	return (UWORD)((p[0] << 8) | p[1]);
}

static inline ULONG rd32(const UBYTE *p)
{
	return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

#endif
