/*
 * KPIC images: 'KPIC', width, height, bitplanes, number of colours, first
 * colour (UWORD), the colours as 0x0RGB, then bitplanes with rows aligned to
 * 16 bits.
 *
 * Interface screens and cutouts use colours 0-15; the entry pictures use
 * colours 16-31, like the video clips.
 */
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>

#include "kll.h"
#include "pic.h"

#define KPIC_HDR 14
#define MAXPLANES 5

#define RASTER_TOP	44	/* first visible raster line (standard DIWSTRT) */

BOOL pic_load(CONST_STRPTR name, struct pic *p)
{
	UBYTE hdr[KPIC_HDR];
	UBYTE cbuf[64];
	UWORD rowbytes, i;
	ULONG psize;
	BPTR fh;

	p->data = NULL;
	p->size = 0;
	fh = Open(name, MODE_OLDFILE);
	if (!fh)
		return FALSE;
	if (Read(fh, hdr, KPIC_HDR) != KPIC_HDR ||
	    hdr[0] != 'K' || hdr[1] != 'P' || hdr[2] != 'I' || hdr[3] != 'C')
		goto fail;

	p->w = rd16(hdr + 4);
	p->h = rd16(hdr + 6);
	p->planes = rd16(hdr + 8);
	p->ncolors = rd16(hdr + 10);
	p->base = rd16(hdr + 12);
	if (p->planes < 1 || p->planes > MAXPLANES || p->ncolors > 32 ||
	    p->base + p->ncolors > 32 || !p->w || !p->h)
		goto fail;

	if (Read(fh, cbuf, p->ncolors * 2) != (LONG)p->ncolors * 2)
		goto fail;
	for (i = 0; i < p->ncolors; i++)
		p->colors[i] = rd16(cbuf + 2 * i) & 0xFFF;

	rowbytes = ((p->w + 15) >> 4) << 1;
	psize = (ULONG)rowbytes * p->h;
	/* one extra zeroed plane when the image has fewer than five */
	p->size = psize * (p->planes < MAXPLANES ? p->planes + 1 : p->planes);
	p->data = AllocMem(p->size, MEMF_CHIP | MEMF_CLEAR);
	if (!p->data)
		goto fail;
	if (Read(fh, p->data, psize * p->planes) != (LONG)(psize * p->planes))
		goto fail;

	InitBitMap(&p->bm, MAXPLANES, rowbytes * 8, p->h);
	for (i = 0; i < MAXPLANES; i++)
		p->bm.Planes[i] = p->data + psize * (i < p->planes ? i : p->planes);
	Close(fh);
	return TRUE;

fail:
	pic_free(p);
	Close(fh);
	return FALSE;
}

void pic_free(struct pic *p)
{
	if (p->data)
		FreeMem(p->data, p->size);
	p->data = NULL;
	p->size = 0;
}

void pic_palette(const struct pic *p, struct Screen *scr)
{
	UWORD i;

	for (i = 0; i < p->ncolors; i++)
		palette[p->base + i] = p->colors[i];
	set_palette(scr);
}

/* See kll.h: waits at most two frames, then writes anyway. */
void beam_below(WORD line)
{
	WORD target = RASTER_TOP + line;
	ULONG end = vbl_count + 2;

	if (target >= (vbl_hz == 50 ? 312 : 262))
		return;
	while ((WORD)VBeamPos() < target)
		if ((LONG)(vbl_count - end) >= 0)
			return;		/* never got there: copy anyway */
}

void pic_blit(const struct pic *p, struct Screen *scr, WORD sx, WORD sy,
	      WORD w, WORD h, WORD dx, WORD dy)
{
	WORD y;

	/* in stripes, each copied as soon as the beam is past it */
	for (y = 0; y < h; y += BEAM_STRIPE) {
		WORD sh = h - y < BEAM_STRIPE ? h - y : BEAM_STRIPE;

		beam_below(dy + y + sh);
		Forbid();
		BltBitMap((struct BitMap *)&p->bm, sx, sy + y, &scr->BitMap,
			  dx, dy + y, w, sh, 0xC0, 0x1F, NULL);
		blit_wait();
		Permit();
	}
}

BOOL pic_show(CONST_STRPTR name, struct Screen *scr, WORD x, WORD y)
{
	struct pic p;

	if (!pic_load(name, &p))
		return FALSE;
	pic_blit(&p, scr, 0, 0, p.w, p.h, x, y);
	pic_palette(&p, scr);
	pic_free(&p);
	return TRUE;
}
