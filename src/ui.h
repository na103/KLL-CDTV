#ifndef KLL_UI_H
#define KLL_UI_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>

/* A screen rectangle, in 320x256 coordinates (see tools/layout.py). */
struct rect {
	WORD x, y, w, h;
};

/* A copy from an atlas: position in the image, size, destination. */
struct blit {
	WORD sx, sy, w, h, dx, dy;
};

void ui_init(struct Screen *scr, struct Window *win);
void ui_cleanup(void);

/*
 * Shows or hides the mouse pointer: it stays hidden during transitions,
 * sounds and clips, and comes back whenever the program waits for a command.
 */
void ui_pointer(BOOL show);
void ui_fill(const struct rect *r, UWORD pen);

/*
 * Draws text in the rectangle, wrapping between words (and inside a word that
 * is wider than the rectangle), lines centred and the block centred
 * vertically. Text that does not fit is left out.
 */
void ui_text(const struct rect *r, CONST_STRPTR s, UWORD pen);

/*
 * Like ui_text, but first paints the band the text will occupy in bg: needed
 * where the artwork underneath would make the letters hard to read.
 */
void ui_text_bg(const struct rect *r, CONST_STRPTR s, UWORD pen, UWORD bg);

BOOL ui_hit(const struct rect *r, WORD x, WORD y);

#endif
