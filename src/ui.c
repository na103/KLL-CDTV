/*
 * Drawing the interface: fills, text and pointer on the custom screen.
 *
 * The text does not use the ROM topaz 8, eight pixels wide per character
 * (nine characters across a panel of the HOL screen), but a proportional
 * font built in RAM by tools/mkfont.py: as tall as topaz, but each character
 * only as wide as it needs to be, which fits nearly twice as many in a
 * panel. Line breaks are measured in pixels with TextLength.
 */
#include <exec/memory.h>
#include <exec/nodes.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "kll.h"
#include "ui.h"
#include "font.h"

#define MAXLINES 16
#define MAXCOLS 48

extern struct GfxBase *GfxBase;

static struct RastPort rp;

/*
 * Pointer: it disappears during animations, sounds and clips, so the arrow
 * does not sit frozen over the scene. The empty sprite lives in chip RAM and
 * is two position words, one row of data and two words of terminator.
 */
#define PTR_WORDS	6

static struct Window *ui_win;
static UWORD *ui_ptr;
static BOOL ptr_shown = TRUE;

/* the font lives here: glyphs go in chip RAM because the blitter reads them */
static struct TextFont font;
static UBYTE *font_chip;

static void font_build(void)
{
	font_chip = AllocMem(FONT_BYTES, MEMF_CHIP);
	if (!font_chip)
		return;			/* stay with the system font */
	CopyMem((APTR)font_data, font_chip, FONT_BYTES);

	font.tf_Message.mn_Node.ln_Type = NT_FONT;
	font.tf_Message.mn_Node.ln_Name = (char *)"kll.font";
	font.tf_YSize = FONT_YSIZE;
	font.tf_Style = FS_NORMAL;
	font.tf_Flags = FPF_DESIGNED | FPF_PROPORTIONAL;
	font.tf_XSize = FONT_XSIZE;
	font.tf_Baseline = FONT_BASELINE;
	font.tf_BoldSmear = 1;
	font.tf_Accessors = 1;
	font.tf_LoChar = FONT_LOCHAR;
	font.tf_HiChar = FONT_HICHAR;
	font.tf_CharData = font_chip;
	font.tf_Modulo = FONT_MODULO;
	font.tf_CharLoc = (APTR)font_loc;
	font.tf_CharSpace = (APTR)font_space;
	font.tf_CharKern = (APTR)font_kern;
	SetFont(&rp, &font);
}

void ui_init(struct Screen *scr, struct Window *win)
{
	InitRastPort(&rp);
	rp.BitMap = &scr->BitMap;
	SetFont(&rp, GfxBase->DefaultFont);
	SetDrMd(&rp, JAM1);
	font_build();
	ui_win = win;
	ui_ptr = AllocMem(PTR_WORDS * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
}

void ui_cleanup(void)
{
	ui_pointer(TRUE);
	if (ui_ptr)
		FreeMem(ui_ptr, PTR_WORDS * sizeof(UWORD));
	ui_ptr = NULL;
	ui_win = NULL;
	SetFont(&rp, GfxBase->DefaultFont);
	if (font_chip)
		FreeMem(font_chip, FONT_BYTES);
	font_chip = NULL;
}

void ui_pointer(BOOL show)
{
	if (!ui_win || !ui_ptr || show == ptr_shown)
		return;
	if (show)
		ClearPointer(ui_win);
	else
		SetPointer(ui_win, ui_ptr, 1, 16, 0, 0);
	ptr_shown = show;
}

void ui_fill(const struct rect *r, UWORD pen)
{
	WORD y;

	if (r->w <= 0 || r->h <= 0)
		return;
	SetAPen(&rp, pen);
	for (y = 0; y < r->h; y += BEAM_STRIPE) {
		WORD h = r->h - y < BEAM_STRIPE ? r->h - y : BEAM_STRIPE;

		beam_below(r->y + y + h);
		RectFill(&rp, r->x, r->y + y, r->x + r->w - 1, r->y + y + h - 1);
		blit_wait();
	}
}

static WORD width_of(const char *s, WORD len)
{
	return (WORD)TextLength(&rp, (CONST_STRPTR)s, len);
}

static void draw_text(const struct rect *r, CONST_STRPTR s, UWORD pen, WORD bg)
{
	char line[MAXLINES][MAXCOLS + 2];
	UBYTE len[MAXLINES];
	WORD wid[MAXLINES];
	WORD ch = rp.TxHeight, rows, n = 0, i, j, y;
	CONST_STRPTR p = s;

	if (!s || !*s || ch <= 0)
		return;
	rows = r->h / ch;
	if (rows < 1)
		return;
	if (rows > MAXLINES)
		rows = MAXLINES;

	while (*p && n < rows) {
		WORD fit = 0, take, last = 0;
		BOOL cut = FALSE;

		while (*p == ' ')
			p++;
		if (!*p)
			break;
		/* how many characters fit on the line */
		while (p[fit] && fit < MAXCOLS &&
		       width_of((const char *)p, fit + 1) <= r->w)
			fit++;
		if (!p[fit] || fit >= MAXCOLS) {
			take = fit;
		} else {
			for (i = fit; i > 0; i--)
				if (p[i] == ' ') {
					last = i;
					break;
				}
			if (last) {
				take = last;	/* break at the last space */
			} else {
				WORD rest = 0;	/* word wider than the line */

				while (p[fit + rest] && p[fit + rest] != ' ')
					rest++;
				/* a lone letter at the end looks bad: pull it back */
				take = (rest == 1 && fit > 2) ? fit - 1 : fit;
				cut = TRUE;
			}
		}
		for (j = 0; j < take; j++)
			line[n][j] = (char)p[j];
		if (cut) {
			/* the hyphen has to fit: drop a letter if need be */
			line[n][j] = '-';
			while (j > 1 && width_of(line[n], j + 1) > r->w) {
				j--;
				line[n][j] = '-';
			}
			j++;
			take = j - 1;
		}
		len[n] = (UBYTE)j;
		wid[n] = width_of(line[n], j);
		n++;
		p += take;
	}

	y = r->y + (r->h - n * ch) / 2;
	if (bg >= 0) {
		struct rect fill;

		fill.x = r->x;
		fill.y = y;
		fill.w = r->w;
		fill.h = n * ch;
		ui_fill(&fill, (UWORD)bg);
	}
	SetAPen(&rp, pen);
	for (i = 0; i < n; i++) {
		Move(&rp, r->x + (r->w - wid[i]) / 2, y + i * ch + rp.TxBaseline);
		Text(&rp, (CONST_STRPTR)line[i], len[i]);
	}
}

void ui_text(const struct rect *r, CONST_STRPTR s, UWORD pen)
{
	draw_text(r, s, pen, -1);
}

void ui_text_bg(const struct rect *r, CONST_STRPTR s, UWORD pen, UWORD bg)
{
	draw_text(r, s, pen, (WORD)bg);
}

BOOL ui_hit(const struct rect *r, WORD x, WORD y)
{
	return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}
