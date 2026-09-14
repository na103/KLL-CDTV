/*
 * The interface: the screens of the original CD (title, main menu, HOL,
 * pronunciation, drills, score, help) and the moves between them.
 *
 * The Mac port uses one thread per sequence; here there is a single task, so
 * the waits are loops that meanwhile collect finished audio and Intuition
 * events. Rectangles and hot spots come from build/gen/layout.h, which
 * tools/layout.py derives from measurements of the original screens; the lit
 * and unlit commands are cutouts of a single image (DATA/HOLBTN.PIC) held in
 * memory while the section is open.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "kll.h"
#include "ui.h"
#include "pic.h"
#include "db.h"
#include "snd.h"
#include "video.h"
#include "app.h"
#include "layout.h"
#include "palette.h"

#define RAWKEY_ESC	0x45
#define RAWKEY_0	0x0A
#define RAWKEY_KP0	0x0F

#define EV_NONE		0
#define EV_SELECT	1	/* left mouse button, A on the remote */
#define EV_BACK		2	/* right button (B) or ESC: back one level */
#define EV_KEY		3

#define DB_FILE		"DATA/KLL.DB"
#define ATLAS_FILE	"DATA/HOLBTN.PIC"
#define PRON_ATLAS	"DATA/PRONBTN.PIC"

/* in the database the phonemes are one more category after the eight of the
   menu, then the drill hints and the help texts */
#define PHON_CAT	NCATEGORIES
#define HINT_CAT	(NCATEGORIES + 1)
#define HELP_CAT	(NCATEGORIES + 2)

/* where to go when a section ends */
#define NEXT_MENU	0
#define NEXT_HOL	1
#define NEXT_PHON	2
#define NEXT_DRILL	3

struct ev {
	UWORD type;
	WORD x, y;
	UWORD code;
};

struct app {
	struct Screen *scr;
	struct Window *win;
	struct pic atlas;	/* lit and unlit cutouts of the current screen */
	struct pic bg;		/* drill screen, to repaint a piece of it */
	struct video_stats st;
	UWORD cat;		/* chosen category, from 0 */
	UWORD item;
	UWORD next;		/* section to open when this one ends */
	BOOL has_stats;
	BOOL help;		/* help mode: touch a command and read about it */
	BOOL back;
};

/* interface rectangles (screen coordinates, 320x256) */
static const struct rect r_img = R_HOL_IMG;
static const struct rect r_ktext = R_HOL_KTEXT;
static const struct rect r_etext = R_HOL_ETEXT;
static const struct rect r_cat = R_HOL_CATNAME;
static const struct rect r_pos = R_HOL_POS;
/* EXIT only quits from the menu: the closing animation takes that screen
   apart, so it would make no sense from the others */
static const struct rect r_exit = R_EXIT;
static const struct rect r_help = R_HELP;
static const struct rect r_credits = R_CREDITS;
static const struct rect r_category = R_HOL_CATEGORY;
static const struct rect r_illu = R_HOL_ILLU;
static const struct rect r_tutor = R_HOL_TUTOR;
static const struct rect r_more = R_HOL_MORE;
static const struct rect r_next = R_HOL_NEXT;
static const struct rect r_prev = R_HOL_PREV;
static const struct rect r_phone = R_HOL_PHONE;
static const struct rect r_drill = R_HOL_DRILL;

/* pronunciation screen */
static const struct rect r_pron_img = R_PRON_IMG;
static const struct rect r_pron_text = R_PRON_TEXT;
static const struct rect r_pron_phon = R_PRON_PHON;
static const struct rect r_pron_more = R_PRON_MORE;
static const struct rect r_pron_exam = R_PRON_EXAM;
static const struct rect r_pron_hol = R_PRON_HOL;
static const struct rect r_pron_drill = R_PRON_DRILL;
static const struct rect phon_hot[NPHONEMES] = PHON_HOT;

/* atlas cutouts: the lit version and the one from the background */
static const struct blit t_bar_help = B_BAR_HELP;
static const struct blit t_off_bar_help = B_OFF_BAR_HELP;
static const struct blit t_category = B_CATEGORY;
static const struct blit t_illu = B_ILLU;
static const struct blit t_tutor = B_TUTOR;
static const struct blit t_more = B_MORE;
static const struct blit t_cover = B_COVER;
static const struct blit t_prev = B_PREV;
static const struct blit t_next = B_NEXT;
static const struct blit t_off_illu = B_OFF_ILLU;
static const struct blit t_off_tutor = B_OFF_TUTOR;
static const struct blit t_off_more = B_OFF_MORE;
static const struct blit t_off_prev = B_OFF_PREV;
static const struct blit t_off_next = B_OFF_NEXT;
static const struct blit t_bar_phone = B_BAR_PHONE;
static const struct blit t_bar_drill = B_BAR_DRILL;

/* cutouts of the pronunciation screen (atlas DATA/PRONBTN.PIC) */
static const struct blit t_bar_pron_help = B_BAR_PRON_HELP;
static const struct blit t_off_bar_pron_help = B_OFF_BAR_PRON_HELP;
static const struct blit t_bar_pron_hol = B_BAR_PRON_HOL;
static const struct blit t_bar_pron_drill = B_BAR_PRON_DRILL;
static const struct blit t_phon = B_PHON;
static const struct blit t_off_phon = B_OFF_PHON;
static const struct blit t_pmore = B_PMORE;
static const struct blit t_off_pmore = B_OFF_PMORE;
static const struct blit t_exam = B_EXAM;
static const struct blit t_off_exam = B_OFF_EXAM;
static const struct blit phon_on[NPHONEMES] = PHON_ON;
static const struct blit phon_off[NPHONEMES] = PHON_OFF;

static char pathbuf[48];

/* "IMG/" + "U003" + ".PIC" */
static CONST_STRPTR mkpath(const char *dir, CONST_STRPTR base, const char *ext)
{
	char *p = pathbuf;

	while (*dir)
		*p++ = *dir++;
	while (*base)
		*p++ = (char)*base++;
	while (*ext)
		*p++ = *ext++;
	*p = '\0';
	return (CONST_STRPTR)pathbuf;
}

static WORD sfx(const char *name)
{
	return snd_play(mkpath("SFX/", (CONST_STRPTR)name, ".RAW"));
}

/* ------------------------------------------------------------------ events */

static void ev_poll(struct app *a, struct ev *e)
{
	struct IntuiMessage *im;

	e->type = EV_NONE;
	while ((im = (struct IntuiMessage *)GetMsg(a->win->UserPort)) != NULL) {
		ULONG cls = im->Class;
		UWORD code = im->Code;
		WORD mx = im->MouseX, my = im->MouseY;

		ReplyMsg((struct Message *)im);
		if (e->type != EV_NONE)
			continue;	/* one event at a time, the rest is dropped */
		if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
			e->type = EV_SELECT;
			e->x = mx;
			e->y = my;
		} else if ((cls == IDCMP_MOUSEBUTTONS && code == MENUDOWN) ||
			   (cls == IDCMP_RAWKEY && code == RAWKEY_ESC)) {
			e->type = EV_BACK;
		} else if (cls == IDCMP_RAWKEY && !(code & 0x80)) {
			e->type = EV_KEY;
			e->code = code;
		}
	}
}

/* Waits ms milliseconds; TRUE if an event arrived meanwhile. */
static BOOL app_wait(struct app *a, UWORD ms, struct ev *e)
{
	ULONG end = vbl_count + ((ULONG)ms * vbl_hz) / 1000;

	ui_pointer(FALSE);
	for (;;) {
		snd_poll();
		ev_poll(a, e);
		if (e->type != EV_NONE) {
			if (e->type == EV_BACK)
				a->back = TRUE;
			return TRUE;
		}
		if ((LONG)(vbl_count - end) >= 0)
			return FALSE;
		WaitTOF();
	}
}

/* Pause used by the animations: clicks arriving while drawing are dropped. */
static void app_delay(struct app *a, UWORD ms)
{
	ULONG end = vbl_count + ((ULONG)ms * vbl_hz) / 1000;
	struct ev e;

	ui_pointer(FALSE);
	while ((LONG)(vbl_count - end) < 0) {
		snd_poll();
		ev_poll(a, &e);
		WaitTOF();
	}
}

/* Waits for a sound to finish; only the "back" button cuts it short. */
static void wait_snd(struct app *a, WORD slot)
{
	struct ev e;

	ui_pointer(FALSE);
	while (snd_busy(slot)) {
		snd_poll();
		ev_poll(a, &e);
		if (e.type == EV_BACK) {
			a->back = TRUE;
			return;
		}
		WaitTOF();
	}
}

static void app_event(struct app *a, struct ev *e)
{
	ui_pointer(TRUE);	/* the pointer is only needed while waiting for a command */
	for (;;) {
		snd_poll();
		ev_poll(a, e);
		if (e->type != EV_NONE) {
			if (e->type == EV_BACK)
				a->back = TRUE;
			return;
		}
		WaitTOF();
	}
}

/* ----------------------------------------------------------------- screens */

/* A sequence of full screens, like the fade of the original CD. */
static void fade(struct app *a, const char *const *names, WORD n, UWORD ms)
{
	WORD i;

	ui_pointer(FALSE);
	for (i = 0; i < n; i++) {
		pic_show((CONST_STRPTR)names[i], a->scr, 0, 0);
		if (ms)
			app_delay(a, ms);
	}
}

static void tile(struct app *a, const struct blit *b)
{
	if (a->atlas.data)
		pic_blit(&a->atlas, a->scr, b->sx, b->sy, b->w, b->h, b->dx, b->dy);
}

/* Category under the pointer: the two menu columns are slanted. */
static WORD menu_cat(WORD x, WORD y)
{
	WORD bx = x * 2, by = (y - SCREEN_TOP) * 2, t, k;

	if (bx >= MENU_L_X0 && bx < MENU_L_X1) {
		t = by - (WORD)((MENU_L_SLOPE * (LONG)(bx - MENU_L_XREF)) / 1000);
		k = (t - MENU_L_Y0) / MENU_L_STEP;
		if (t >= MENU_L_Y0 && k < 4)
			return MENU_L_FIRST + k;
	}
	if (bx >= MENU_R_X0 && bx < MENU_R_X1) {
		t = by - (WORD)((MENU_R_SLOPE * (LONG)(bx - MENU_R_XREF)) / 1000);
		k = (t - MENU_R_Y0) / MENU_R_STEP;
		if (t >= MENU_R_Y0 && k < 4)
			return MENU_R_FIRST + k;
	}
	return 0;
}

/*
 * Removes the CDTV trademark screen, which the system keeps in front of
 * everything. The Startup-Sequence does not do it: if the trademark went
 * away earlier, the boot CLI would show for as long as it takes to load the
 * program and the first picture. This way the logo gives way to a screen
 * that is already painted.
 *
 * This is what the CDTV RMTM utility does: it opens playerprefs.library and
 * calls the function at -144 (its disassembly says so; that is the whole
 * command). On a machine without that library nothing happens.
 */
#define PLAYERPREFS_RMTM	(-144)

static void trademark_off(struct app *a)
{
	struct Library *pp = OpenLibrary((CONST_STRPTR)"playerprefs.library", 0);

	if (pp) {
		__asm volatile ("move.l a6,-(sp)\n\t"
				"move.l %0,a6\n\t"
				"jsr %c1(a6)\n\t"
				"move.l (sp)+,a6"
				: : "a"(pp), "i"(PLAYERPREFS_RMTM)
				: "d0", "d1", "a0", "a1", "cc", "memory");
		CloseLibrary(pp);
	}
	ScreenToFront(a->scr);
	ActivateWindow(a->win);
}

/*
 * Opening. The original CD starts with the production credits and the Simon
 * & Schuster logo: at 320x256 that text cannot be read, so it goes straight
 * to the title screen (without the music that came with the credits).
 */
static void splash(struct app *a)
{
	struct ev e;

	ui_pointer(FALSE);
	pic_show((CONST_STRPTR)"DATA/SPLASH.PIC", a->scr, 0, 0);
	trademark_off(a);
	app_wait(a, 2500, &e);		/* a click goes to the menu at once */
	a->back = FALSE;
	sfx("CHIRP");
	app_delay(a, 1000);
}

/*
 * Main menu. Returns the chosen category (from 1), 0 for the credits,
 * -1 to quit.
 */
static WORD main_menu(struct app *a)
{
	static const char *const frames[] = {
		"DATA/MAIN_04.PIC", "DATA/MAIN_03.PIC", "DATA/MAIN_02.PIC",
		"DATA/MAIN_01.PIC", "DATA/MAINB.PIC"
	};
	static const struct { WORD x, y; } ovl[NCATEGORIES] = MAIN_OVL_POS;
	char num[2];

	fade(a, frames, 5, 120);
	sfx("RECON02");
	a->back = FALSE;

	for (;;) {
		struct pic p;
		struct ev e;
		WORD cat;

		app_event(a, &e);
		if (a->back)
			return -1;
		if (e.type != EV_SELECT)
			continue;
		if (ui_hit(&r_exit, e.x, e.y))
			return -1;
		if (ui_hit(&r_credits, e.x, e.y))
			return 0;
		cat = menu_cat(e.x, e.y);
		if (!cat)
			continue;

		/* the chosen entry lights up, then the section opens */
		ui_pointer(FALSE);
		num[0] = (char)('0' + cat);
		num[1] = '\0';
		if (pic_load(mkpath("DATA/MH", (CONST_STRPTR)num, ".PIC"), &p)) {
			pic_blit(&p, a->scr, 0, 0, p.w, p.h,
				 ovl[cat - 1].x, ovl[cat - 1].y);
			pic_free(&p);
		}
		sfx("COMP1");
		app_delay(a, 1300);
		return cat;
	}
}

static void credits(struct app *a)
{
	struct ev e;

	pic_show((CONST_STRPTR)"DATA/CREDITS.PIC", a->scr, 0, 0);
	sfx("RECON02");
	a->back = FALSE;
	app_event(a, &e);
}

/* ------------------------------------------------------------- HOL section */

static char *put_num(char *p, ULONG n)
{
	char tmp[10];
	int i = 0;

	do {
		tmp[i++] = (char)('0' + n % 10);
		n /= 10;
	} while (n && i < 10);
	while (i)
		*p++ = tmp[--i];
	return p;
}

/* Diagnostics of the last clip, in the English panel (key 0). */
static void show_stats(struct app *a)
{
	static const char *const label[] = { "vis ", "salt ", "buchi ", "lett ",
					     "cop ", "tardi " };
	ULONG value[6];
	char buf[80], *p = buf;
	WORD i;

	value[0] = a->st.shown;
	value[1] = a->st.skipped;
	value[2] = a->st.underruns;
	value[3] = a->st.max_read;
	value[4] = a->st.max_blit;
	value[5] = a->st.late_blits;
	for (i = 0; i < 6; i++) {
		const char *s = label[i];

		while (*s)
			*p++ = *s++;
		p = put_num(p, value[i]);
		*p++ = ' ';
	}
	*p = '\0';
	ui_fill(&r_etext, PEN_BLACK);
	ui_text(&r_etext, (CONST_STRPTR)buf, PEN_TEXT);
}

/*
 * Help mode: pressing HELP lights that command in the bar too and the panel
 * explains how it works; the next touch, instead of running a command, reads
 * out its description. The texts are the ones from the CD, in TXTFILES.
 */
struct help_spot {
	const struct rect *r;
	UWORD txt;
};

#define HELP_SPOTS(t)	(t), (UWORD)(sizeof(t) / sizeof((t)[0]))

static const struct help_spot help_hol[] = {
	{ &r_drill, HLP_HOL_DRILL },
	{ &r_phone, HLP_HOL_PHONE }, { &r_category, HLP_HOL_CATEGORY },
	{ &r_illu, HLP_HOL_ILLU }, { &r_tutor, HLP_HOL_TUTOR },
	{ &r_more, HLP_HOL_MORE }, { &r_next, HLP_HOL_NEXT },
	{ &r_prev, HLP_HOL_PREV },
};

static const struct help_spot help_pron[] = {
	{ &r_pron_hol, HLP_PRON_HOL },
	{ &r_pron_drill, HLP_PRON_DRILL }, { &r_pron_phon, HLP_PRON_PHON },
	{ &r_pron_more, HLP_PRON_MORE }, { &r_pron_exam, HLP_PRON_EXAM },
};

/* in the drills the bar only leads to the other two sections */
static const struct help_spot help_drill[] = {
	{ &r_pron_hol, HLP_HOL }, { &r_phone, HLP_PHONEME },
};

/*
 * Writes a text in the rectangle. Where there is artwork underneath (the
 * drills) only the band of lines is painted, otherwise the whole rectangle
 * is cleared first.
 */
static void help_say(const struct rect *area, UWORD cat, UWORD idx, BOOL clear)
{
	struct item it;

	if (!db_item(cat, idx, &it))
		return;
	if (clear) {
		ui_fill(area, PEN_BLACK);
		ui_text(area, it.klingon, PEN_TEXT);
	} else {
		ui_text_bg(area, it.klingon, PEN_TEXT, PEN_BLACK);
	}
}

/* The command touched in help mode; TRUE if there was one. */
static BOOL help_click(const struct ev *e, const struct help_spot *spot, UWORD n,
		       const struct rect *area, BOOL clear)
{
	UWORD i;

	for (i = 0; i < n; i++)
		if (ui_hit(spot[i].r, e->x, e->y)) {
			help_say(area, HELP_CAT, spot[i].txt, clear);
			return TRUE;
		}
	return FALSE;
}

/* The picture of the entry, centred in the video rectangle. */
static void show_image(struct app *a, const struct item *it)
{
	struct pic p;

	ui_fill(&r_img, PEN_BLACK);
	if (!pic_load(mkpath("IMG/", it->img, ".PIC"), &p))
		return;
	pic_blit(&p, a->scr, 0, 0, p.w, p.h,
		 r_img.x + (r_img.w - p.w) / 2, r_img.y + (r_img.h - p.h) / 2);
	pic_palette(&p, a->scr);
	pic_free(&p);
}

static void show_item(struct app *a, BOOL play)
{
	struct item it;

	if (!db_item(a->cat, a->item, &it))
		return;
	ui_pointer(FALSE);

	ui_fill(&r_ktext, PEN_BLACK);
	ui_fill(&r_etext, PEN_BLACK);
	ui_fill(&r_pos, PEN_BLACK);
	ui_text(&r_ktext, it.klingon, PEN_TEXT);
	ui_text(&r_etext, it.english, PEN_TEXT);
	/* the category name is black on the panel, as on the CD: it is always
	   rewritten identically, so there is nothing to clear first */
	ui_text(&r_cat, db_name(a->cat), PEN_BLACK);
	{
		/* position in the list, in the black box on the right */
		char buf[12], *p = put_num(buf, a->item + 1);

		*p++ = '/';
		p = put_num(p, db_count(a->cat));
		*p = '\0';
		ui_text(&r_pos, (CONST_STRPTR)buf, PEN_TEXT);
	}
	a->has_stats = FALSE;

	tile(a, &t_off_tutor);
	tile(a, &t_illu);
	/* the MORE label disappears if the entry has no second commentary */
	tile(a, (it.flags & ITEM_MORE) ? &t_off_more : &t_cover);

	show_image(a, &it);
	if (play)
		wait_snd(a, snd_play(mkpath("SND/", it.snd, ".RAW")));
}

static void hol(struct app *a)
{
	static const char *const frames[] = {
		"DATA/HOL_04.PIC", "DATA/HOL_03.PIC", "DATA/HOL_02.PIC",
		"DATA/HOL_01.PIC", "DATA/HOLB.PIC"
	};
	UWORD count = db_count(a->cat);

	a->next = NEXT_MENU;
	fade(a, frames, 5, 120);
	if (!pic_load((CONST_STRPTR)ATLAS_FILE, &a->atlas))
		msg("KLL: " ATLAS_FILE " not found\n");
	sfx("RECON01");

	a->help = FALSE;
	a->item = 0;
	a->back = FALSE;
	show_item(a, TRUE);

	while (!a->back) {
		struct item it;
		struct ev e;

		app_event(a, &e);
		if (a->back)
			break;
		if (e.type == EV_KEY) {
			if ((e.code == RAWKEY_0 || e.code == RAWKEY_KP0) && a->has_stats)
				show_stats(a);
			continue;
		}
		if (e.type != EV_SELECT)
			continue;
		ui_pointer(FALSE);	/* no arrow on top of the animations */

		if (ui_hit(&r_help, e.x, e.y)) {
			a->help = !a->help;
			sfx("RECON05");
			tile(a, a->help ? &t_bar_help : &t_off_bar_help);
			if (a->help)
				help_say(&r_img, HELP_CAT, HLP_INTRO, TRUE);
			else
				show_item(a, FALSE);	/* the picture comes back */
			continue;
		}
		if (a->help) {
			help_click(&e, HELP_SPOTS(help_hol), &r_img, TRUE);
			continue;
		}

		if (ui_hit(&r_phone, e.x, e.y)) {
			tile(a, &t_bar_phone);
			sfx("COMP2");
			app_delay(a, 900);
			a->next = NEXT_PHON;
			break;
		} else if (ui_hit(&r_drill, e.x, e.y)) {
			tile(a, &t_bar_drill);
			sfx("COMP2");
			app_delay(a, 900);
			a->next = NEXT_DRILL;
			break;
		} else if (ui_hit(&r_category, e.x, e.y)) {
			tile(a, &t_category);
			sfx("COMP4");
			app_delay(a, 900);
			break;
		} else if (ui_hit(&r_next, e.x, e.y) && count) {
			tile(a, &t_next);
			a->item = (UWORD)((a->item + 1) % count);
			show_item(a, TRUE);
			tile(a, &t_off_next);
		} else if (ui_hit(&r_prev, e.x, e.y) && count) {
			tile(a, &t_prev);
			a->item = (UWORD)((a->item + count - 1) % count);
			show_item(a, TRUE);
			tile(a, &t_off_prev);
		} else if (ui_hit(&r_illu, e.x, e.y)) {
			tile(a, &t_illu);
			tile(a, &t_off_tutor);
			if (db_item(a->cat, a->item, &it))
				show_image(a, &it);
			wait_snd(a, sfx("RECON01"));
		} else if (ui_hit(&r_tutor, e.x, e.y)) {
			tile(a, &t_tutor);
			tile(a, &t_off_illu);
			if (db_item(a->cat, a->item, &it)) {
				ui_pointer(FALSE);
				ui_fill(&r_img, PEN_BLACK);
				video_play(mkpath("VIDEO/", it.vid, ".KXL"),
					   a->scr, a->win, r_img.x, r_img.y,
					   0, &a->st, NULL);
				a->has_stats = TRUE;
				sfx("RECON02");
				/* as on the CD the rectangle keeps the last
				   frame: ILLUSTRATION brings the picture
				   back */
				tile(a, &t_off_tutor);
			}
		} else if (ui_hit(&r_more, e.x, e.y)) {
			if (db_item(a->cat, a->item, &it) && (it.flags & ITEM_MORE)) {
				tile(a, &t_more);
				wait_snd(a, snd_play(mkpath("SND/", it.more, ".RAW")));
				tile(a, &t_off_more);
			}
		}
	}
	pic_free(&a->atlas);
}

/* --------------------------------------------------- pronunciation section */

/*
 * One phoneme: its button lights up, the sound is played, the example word
 * appears and the clip of a Klingon saying it starts. This is the sequence
 * of the original CD, where the PHONEME and EXAMPLE commands light up in
 * turn to say what is being heard.
 */
static void play_phoneme(struct app *a, WORD n)
{
	struct rect top = r_pron_text, bottom = r_pron_text;
	struct item it;

	if (!db_item(PHON_CAT, (UWORD)n, &it))
		return;
	ui_pointer(FALSE);
	tile(a, &t_off_pmore);
	tile(a, &t_off_exam);
	ui_fill(&r_pron_text, PEN_BLACK);
	ui_fill(&r_pron_img, PEN_BLACK);

	tile(a, &t_phon);
	wait_snd(a, snd_play(mkpath("SND/", it.snd, ".RAW")));
	tile(a, &t_off_phon);
	if (a->back)
		return;

	/* the example word, Klingon above and the translation below */
	top.h = r_pron_text.h / 2;
	bottom.y = r_pron_text.y + top.h;
	bottom.h = r_pron_text.h - top.h;
	ui_text(&top, it.klingon, PEN_TEXT);
	ui_text(&bottom, it.english, PEN_TEXT);
	app_delay(a, 700);

	tile(a, &t_exam);
	video_play(mkpath("VIDEO/", it.vid, ".KXL"), a->scr, a->win,
		   r_pron_img.x, r_pron_img.y, 0, &a->st, NULL);
	a->has_stats = TRUE;
	tile(a, &t_off_exam);
	sfx("RECON02");
}

/* Plays again what the chosen phoneme has already shown. */
static void repeat_phoneme(struct app *a, WORD sel, WORD what)
{
	struct item it;

	if (sel < 0 || !db_item(PHON_CAT, (UWORD)sel, &it))
		return;
	ui_pointer(FALSE);
	if (what == 0) {
		tile(a, &t_phon);
		wait_snd(a, snd_play(mkpath("SND/", it.snd, ".RAW")));
		tile(a, &t_off_phon);
	} else if (what == 1) {
		tile(a, &t_pmore);
		wait_snd(a, snd_play(mkpath("SND/", it.more, ".RAW")));
		tile(a, &t_off_pmore);
	} else {
		tile(a, &t_exam);
		ui_fill(&r_pron_img, PEN_BLACK);
		video_play(mkpath("VIDEO/", it.vid, ".KXL"), a->scr, a->win,
			   r_pron_img.x, r_pron_img.y, 0, &a->st, NULL);
		a->has_stats = TRUE;
		tile(a, &t_off_exam);
	}
	sfx("RECON02");
}

static WORD phon_hit(WORD x, WORD y)
{
	WORD i;

	for (i = 0; i < NPHONEMES; i++)
		if (ui_hit(&phon_hot[i], x, y))
			return i;
	return -1;
}

static void phoneme(struct app *a)
{
	static const char *const frames[] = {
		"DATA/PRONU_05.PIC", "DATA/PRONU_04.PIC", "DATA/PRONU_03.PIC",
		"DATA/PRONU_02.PIC", "DATA/PRONU_01.PIC", "DATA/PRONUNB.PIC"
	};
	WORD sel = -1;

	a->next = NEXT_MENU;
	fade(a, frames, 6, 120);
	if (!pic_load((CONST_STRPTR)PRON_ATLAS, &a->atlas))
		msg("KLL: " PRON_ATLAS " not found\n");
	sfx("RECON02");
	a->help = FALSE;
	a->back = FALSE;

	while (!a->back) {
		struct ev e;
		WORD n;

		app_event(a, &e);
		if (a->back)
			break;
		if (e.type == EV_KEY) {
			if ((e.code == RAWKEY_0 || e.code == RAWKEY_KP0) && a->has_stats)
				show_stats(a);
			continue;
		}
		if (e.type != EV_SELECT)
			continue;
		ui_pointer(FALSE);

		if (ui_hit(&r_help, e.x, e.y)) {
			a->help = !a->help;
			sfx("RECON05");
			tile(a, a->help ? &t_bar_pron_help : &t_off_bar_pron_help);
			if (a->help)
				help_say(&r_pron_img, HELP_CAT, HLP_INTRO, TRUE);
			else
				ui_fill(&r_pron_img, PEN_BLACK);
			continue;
		}
		if (a->help) {
			/* the 34 buttons share a single text, as on the CD */
			if (phon_hit(e.x, e.y) >= 0)
				help_say(&r_pron_img, HELP_CAT, HLP_PRON_BTN, TRUE);
			else
				help_click(&e, HELP_SPOTS(help_pron), &r_pron_img, TRUE);
			continue;
		}

		if (ui_hit(&r_pron_hol, e.x, e.y)) {
			tile(a, &t_bar_pron_hol);
			sfx("COMP2");
			app_delay(a, 900);
			a->next = NEXT_HOL;
			break;
		} else if (ui_hit(&r_pron_drill, e.x, e.y)) {
			tile(a, &t_bar_pron_drill);
			sfx("COMP2");
			app_delay(a, 900);
			a->next = NEXT_DRILL;
			break;
		} else if ((n = phon_hit(e.x, e.y)) >= 0) {
			if (sel >= 0 && sel != n)
				tile(a, &phon_off[sel]);
			sel = n;
			tile(a, &phon_on[n]);
			play_phoneme(a, n);
		} else if (ui_hit(&r_pron_phon, e.x, e.y)) {
			repeat_phoneme(a, sel, 0);
		} else if (ui_hit(&r_pron_more, e.x, e.y)) {
			repeat_phoneme(a, sel, 1);
		} else if (ui_hit(&r_pron_exam, e.x, e.y)) {
			repeat_phoneme(a, sel, 2);
		}
	}
	pic_free(&a->atlas);
}


/* ------------------------------------------------------------------ drills */

/*
 * Three kinds of drill, as on the original CD: read the Klingon and pick the
 * translation, listen to four entries and confirm with CHOOSE, or listen to
 * one entry and pick the translation. After eleven questions comes the
 * score. Every category has its own screens, in DRILL/.
 */
#define DRILL_TEXT_KIND	1	/* read the Klingon */
#define DRILL_SND_KIND	2	/* listen to the four answers */
#define DRILL_ASK_KIND	3	/* listen to the question */

struct quiz {
	UWORD kind;
	UWORD item[DRILL_ANSWERS];
	UWORD right;
};

static const struct rect d_box[DRILL_ANSWERS] = DRILL_BOX;
static const struct rect d_text[DRILL_ANSWERS] = DRILL_TEXT;
static const struct rect d3_box[DRILL_ANSWERS] = DRILL3_BOX;
static const struct rect d3_text[DRILL_ANSWERS] = DRILL3_TEXT;
static const struct rect r_drill_q = R_DRILL_Q;
static const struct rect r_repeat = R_DRILL3_REPEAT;
static const struct rect r_choose = R_DRILL_CHOOSE;
static const struct rect r_hint = R_DRILL_HINT;
static const struct rect r_bar = { 14, 22, 298, 37 };	/* the bar at the top */
static const struct rect r_wrong_n = R_DRILL_WRONG_N;
static const struct rect r_right_n = R_DRILL_RIGHT_N;
static const struct rect r_meter = R_DRILL_METER;
static const struct rect r_score_text = R_SCORE_TEXT;
static const struct rect r_score_wrong = R_SCORE_WRONG_N;
static const struct rect r_score_right = R_SCORE_RIGHT_N;
static const struct rect r_score_cat = R_SCORE_CAT;

static const struct blit d_on[DRILL_ANSWERS] = { B_DBOX1, B_DBOX2, B_DBOX3, B_DBOX4 };
static const struct blit d_off[DRILL_ANSWERS] =
	{ B_OFF_DBOX1, B_OFF_DBOX2, B_OFF_DBOX3, B_OFF_DBOX4 };
static const struct blit t_choose = B_DCHOOSE;
static const struct blit t_off_choose = B_OFF_DCHOOSE;
static const struct blit t_bar_dhelp = B_BAR_DHELP;
static const struct blit t_dwrong = B_DWRONG;
static const struct blit t_dright = B_DRIGHT;

static ULONG rnd_state;

/* A random number from 0 to n-1. */
static UWORD rnd(UWORD n)
{
	rnd_state = rnd_state * 1103515245UL + 12345;
	return n ? (UWORD)((rnd_state >> 16) % n) : 0;
}

/* "DRILL/D" + category + kind + ".PIC" */
static CONST_STRPTR drill_path(UWORD cat, char kind)
{
	char *p = pathbuf;
	const char *dir = "DRILL/D";

	while (*dir)
		*p++ = *dir++;
	*p++ = (char)('1' + cat);
	*p++ = kind;
	*p++ = '.';
	*p++ = 'P';
	*p++ = 'I';
	*p++ = 'C';
	*p = '\0';
	return (CONST_STRPTR)pathbuf;
}

/*
 * Four different entries of the category, one of which is the answer. The
 * answer is not one of the n questions already asked (asked), unless the
 * category runs out of entries.
 */
static void pick_quiz(struct app *a, struct quiz *q, const UWORD *asked, UWORD n)
{
	UWORD count = db_count(a->cat), answer;
	WORD i, j;

	q->kind = (UWORD)(1 + rnd(3));
	do {
		answer = rnd(count);
		for (j = 0; j < (WORD)n; j++)
			if (asked[j] == answer)
				break;
	} while (j < (WORD)n && count > n);

	q->right = rnd(DRILL_ANSWERS);
	for (i = 0; i < DRILL_ANSWERS; i++) {
		if (i == (WORD)q->right) {
			q->item[i] = answer;
			continue;
		}
		for (;;) {
			q->item[i] = rnd(count);
			if (q->item[i] == answer)
				continue;
			for (j = 0; j < i; j++)
				if (q->item[j] == q->item[i])
					break;
			if (j == i)
				break;
		}
	}
}

/* Repaints a piece of the drill screen, clearing whatever is on top of it. */
static void drill_restore(struct app *a, const struct rect *r)
{
	if (a->bg.data)
		pic_blit(&a->bg, a->scr, r->x, r->y, r->w, r->h, r->x, r->y);
}

/* Counters and meter: right answers in red from the right, wrong ones in green from the left. */
static void draw_score(struct app *a, UWORD right, UWORD wrong)
{
	struct rect bar = r_meter;
	char buf[6], *p;

	/* the text is transparent: without clearing, the new number lands on the old one */
	drill_restore(a, &r_wrong_n);
	p = put_num(buf, wrong);
	*p = '\0';
	ui_text(&r_wrong_n, (CONST_STRPTR)buf, PEN_TEXT);
	drill_restore(a, &r_right_n);
	p = put_num(buf, right);
	*p = '\0';
	ui_text(&r_right_n, (CONST_STRPTR)buf, PEN_TEXT);

	if (wrong) {
		bar.w = (WORD)(r_meter.w * wrong / DRILL_QUESTIONS);
		ui_fill(&bar, PEN_GREEN);
	}
	if (right) {
		bar.w = (WORD)(r_meter.w * right / DRILL_QUESTIONS);
		bar.x = r_meter.x + r_meter.w - bar.w;
		ui_fill(&bar, PEN_RED);
	}
}

/* Sets up the question screen; for the third kind it plays the entry. */
static void show_quiz(struct app *a, const struct quiz *q, UWORD right, UWORD wrong)
{
	const struct rect *box = q->kind == DRILL_ASK_KIND ? d3_text : d_text;
	struct item it;
	WORD i;

	ui_pointer(FALSE);
	a->help = FALSE;	/* the fresh screen has the plain bar */
	/* the screen stays in memory: needed to clear the hint away */
	pic_free(&a->bg);
	if (pic_load(drill_path(a->cat, (char)('0' + q->kind)), &a->bg)) {
		pic_blit(&a->bg, a->scr, 0, 0, a->bg.w, a->bg.h, 0, 0);
		pic_palette(&a->bg, a->scr);
	}
	draw_score(a, right, wrong);

	for (i = 0; i < DRILL_ANSWERS; i++) {
		if (!db_item(a->cat, q->item[i], &it))
			continue;
		if (q->kind == DRILL_SND_KIND)
			continue;	/* here the answers are listened to */
		ui_text(&box[i], it.english, PEN_TEXT);
	}

	/* as on the CD: first you read what to do, then the question arrives */
	if (db_item(HINT_CAT, (UWORD)(q->kind - 1), &it))
		ui_text_bg(&r_hint, it.klingon, PEN_TEXT, PEN_BLACK);
	app_delay(a, 2500);
	drill_restore(a, &r_hint);

	if (!db_item(a->cat, q->item[q->right], &it))
		return;
	if (q->kind == DRILL_TEXT_KIND)
		ui_text(&r_drill_q, it.klingon, PEN_TEXT);
	else if (q->kind == DRILL_SND_KIND)
		ui_text(&r_drill_q, it.english, PEN_TEXT);
	else
		wait_snd(a, snd_play(mkpath("SND/", it.snd, ".RAW")));
}

/*
 * HOL and PHONEME in the top bar. In the drills and on the score screen they
 * lead out of the section, as on the original CD; in the main menu the bar is
 * only drawn and does not respond (that too is like the original).
 */
static BOOL bar_exit(struct app *a, const struct ev *e)
{
	if (ui_hit(&r_pron_hol, e->x, e->y))
		a->next = NEXT_HOL;
	else if (ui_hit(&r_phone, e->x, e->y))
		a->next = NEXT_PHON;
	else
		return FALSE;
	sfx("COMP2");
	app_delay(a, 700);
	return TRUE;
}

/* Waits for the answer: returns the chosen index, -1 if the section is left. */
static WORD ask_quiz(struct app *a, const struct quiz *q)
{
	const struct rect *box = q->kind == DRILL_ASK_KIND ? d3_box : d_box;
	WORD heard = -1;
	struct item it;

	for (;;) {
		struct ev e;
		WORD i;

		app_event(a, &e);
		if (a->back)
			return -1;
		if (e.type != EV_SELECT)
			continue;
		ui_pointer(FALSE);
		if (ui_hit(&r_help, e.x, e.y)) {
			a->help = !a->help;
			sfx("RECON05");
			if (a->help) {
				tile(a, &t_bar_dhelp);
				help_say(&r_hint, HELP_CAT, HLP_INTRO, FALSE);
			} else {
				drill_restore(a, &r_bar);
				drill_restore(a, &r_hint);
			}
			continue;
		}
		if (a->help) {
			drill_restore(a, &r_hint);	/* away with the previous text */
			if (!help_click(&e, HELP_SPOTS(help_drill), &r_hint, FALSE))
				for (i = 0; i < DRILL_ANSWERS; i++)
					if (ui_hit(&box[i], e.x, e.y)) {
						/* the hint explains the boxes */
						help_say(&r_hint, HINT_CAT,
							 (UWORD)(q->kind - 1), FALSE);
						break;
					}
			continue;
		}
		if (bar_exit(a, &e))
			return -1;
		if (q->kind == DRILL_ASK_KIND && ui_hit(&r_repeat, e.x, e.y)) {
			if (db_item(a->cat, q->item[q->right], &it))
				wait_snd(a, snd_play(mkpath("SND/", it.snd, ".RAW")));
			continue;
		}
		if (q->kind == DRILL_SND_KIND && heard >= 0 &&
		    ui_hit(&r_choose, e.x, e.y)) {
			tile(a, &t_choose);
			app_delay(a, 300);
			tile(a, &t_off_choose);
			tile(a, &d_off[heard]);
			return heard;
		}
		for (i = 0; i < DRILL_ANSWERS; i++) {
			if (!ui_hit(&box[i], e.x, e.y))
				continue;
			if (q->kind != DRILL_SND_KIND)
				return i;
			/* the entry plays and the box stays lit */
			if (heard >= 0 && heard != i)
				tile(a, &d_off[heard]);
			heard = i;
			tile(a, &d_on[i]);
			if (db_item(a->cat, q->item[i], &it))
				wait_snd(a, snd_play(mkpath("SND/", it.snd, ".RAW")));
			break;
		}
	}
}

/* Right or wrong: a sound, the box that flashes, the score. */
static void quiz_answer(struct app *a, const struct quiz *q, WORD chosen,
			UWORD right, UWORD wrong)
{
	const struct blit *mark = chosen == (WORD)q->right ? &t_dright : &t_dwrong;
	struct item it;

	wait_snd(a, sfx(chosen == (WORD)q->right ? "RIGHT01" : "WRONG01"));
	tile(a, mark);
	app_delay(a, 700);
	draw_score(a, right, wrong);
	/*
	 * On a wrong answer the commander comments on the entry that was
	 * picked: in Klingon where the answers are written, in English where
	 * they are listened to.
	 */
	if (chosen != (WORD)q->right && db_item(a->cat, q->item[chosen], &it))
		wait_snd(a, snd_play(mkpath("SND/",
			q->kind == DRILL_SND_KIND ? it.we : it.wk, ".RAW")));
	app_delay(a, 300);
}

/*
 * The final score, with the commander's verdict: the clip depends on how many
 * answers were right (on the original CD it ranges from insult to praise).
 */
static void score(struct app *a, UWORD right, UWORD wrong)
{
	static const struct { UWORD upto; const char *clip; } clips[NSCORE_CLIPS]
		= SCORE_CLIPS;
	struct rect line = r_score_text;
	char buf[40], *p;
	WORD i;

	ui_pointer(FALSE);
	pic_show((CONST_STRPTR)"DATA/SCORE.PIC", a->scr, 0, 0);
	p = put_num(buf, wrong);
	*p = '\0';
	ui_text(&r_score_wrong, (CONST_STRPTR)buf, PEN_TEXT);
	p = put_num(buf, right);
	*p = '\0';
	ui_text(&r_score_right, (CONST_STRPTR)buf, PEN_TEXT);

	line.h = r_score_text.h / 2;
	ui_text(&line, db_name(a->cat), PEN_TEXT);
	line.y += line.h;
	p = put_num(buf, (ULONG)right * 100 / DRILL_QUESTIONS);
	*p++ = '%';
	*p = '\0';
	ui_text(&line, (CONST_STRPTR)buf, PEN_TEXT);
	app_delay(a, 1500);

	for (i = 0; i < NSCORE_CLIPS - 1 && right > clips[i].upto; i++)
		;
	video_play(mkpath("VIDEO/", (CONST_STRPTR)clips[i].clip, ".KXL"),
		   a->scr, a->win, r_img.x, r_img.y, 0, &a->st, NULL);
	a->has_stats = TRUE;

	a->next = NEXT_HOL;		/* normally it goes back to the words */
	for (;;) {
		struct ev e;

		app_event(a, &e);
		if (a->back)
			return;
		if (e.type != EV_SELECT)
			continue;
		ui_pointer(FALSE);
		if (ui_hit(&r_score_cat, e.x, e.y)) {
			a->next = NEXT_MENU;	/* CATEGORY: choose one again */
			sfx("COMP4");
			app_delay(a, 700);
			return;
		}
		if (bar_exit(a, &e))
			return;
	}
}

static void drill(struct app *a)
{
	UWORD right = 0, wrong = 0, n;
	UWORD asked[DRILL_QUESTIONS];

	a->next = NEXT_MENU;
	a->back = FALSE;
	rnd_state += vbl_count;
	ui_pointer(FALSE);
	pic_show(drill_path(a->cat, 'N'), a->scr, 0, 0);
	if (!pic_load(drill_path(a->cat, 'H'), &a->atlas))
		msg("KLL: drill screens not found\n");
	sfx("RECON02");

	for (n = 0; n < DRILL_QUESTIONS; n++) {
		struct quiz q;
		WORD chosen;

		pick_quiz(a, &q, asked, n);
		asked[n] = q.item[q.right];
		show_quiz(a, &q, right, wrong);
		chosen = ask_quiz(a, &q);
		if (chosen < 0)
			break;
		if (chosen == (WORD)q.right)
			right++;
		else
			wrong++;
		quiz_answer(a, &q, chosen, right, wrong);
		if (a->back)
			break;
	}
	pic_free(&a->atlas);
	pic_free(&a->bg);
	if (right + wrong == DRILL_QUESTIONS)
		score(a, right, wrong);
}

/* Leaving: the four screens in reverse, then main reboots the machine. */
static void goodbye(struct app *a)
{
	static const char *const frames[] = {
		"DATA/MAIN_01.PIC", "DATA/MAIN_02.PIC", "DATA/MAIN_03.PIC",
		"DATA/MAIN_04.PIC"
	};

	sfx("RECON05");
	fade(a, frames, 4, 100);
	app_delay(a, 400);
}

void app_run(struct Screen *scr, struct Window *win)
{
	static struct app a;

	a.scr = scr;
	a.win = win;
	ui_init(scr, win);

	if (!db_open((CONST_STRPTR)DB_FILE)) {
		msg("KLL: " DB_FILE " not found\n");
		return;
	}

	splash(&a);
	for (;;) {
		WORD cat;

		if (a.next == NEXT_HOL) {
			hol(&a);
			continue;
		}
		if (a.next == NEXT_PHON) {
			phoneme(&a);
			continue;
		}
		if (a.next == NEXT_DRILL) {
			drill(&a);
			continue;
		}
		cat = main_menu(&a);
		if (cat < 0)
			break;
		if (cat == 0) {
			credits(&a);
			continue;
		}
		a.cat = (UWORD)(cat - 1);
		hol(&a);
	}
	goodbye(&a);
	snd_stop();
	db_close();
	ui_cleanup();
}
