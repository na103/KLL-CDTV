#ifndef KLL_PIC_H
#define KLL_PIC_H

#include <exec/types.h>
#include <graphics/gfx.h>
#include <intuition/screens.h>

/*
 * A KPIC image loaded into chip RAM. The BitMap always has five planes: the
 * ones the image does not use point at a zeroed plane, so copying to the
 * screen with mask 0x1F also clears the high planes of what was there.
 */
struct pic {
	struct BitMap bm;
	UBYTE *data;
	ULONG size;
	UWORD w, h, planes, ncolors, base;
	UWORD colors[32];
};

BOOL pic_load(CONST_STRPTR name, struct pic *p);
void pic_free(struct pic *p);

/* Loads the image colours into the screen palette, starting at p->base. */
void pic_palette(const struct pic *p, struct Screen *scr);

void pic_blit(const struct pic *p, struct Screen *scr, WORD sx, WORD sy,
	      WORD w, WORD h, WORD dx, WORD dy);

/* Loads an image, copies it to (x,y), applies its palette and frees it. */
BOOL pic_show(CONST_STRPTR name, struct Screen *scr, WORD x, WORD y);

#endif
