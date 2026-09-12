/*
 * Klingon Language Lab for the Commodore CDTV.
 *
 * Opens the 320x256 32-colour screen, the vertical blank server and
 * audio.device, then hands over to the interface (src/app.c). The left mouse
 * button is button A on the CDTV remote, the right one is button B.
 */
#include <exec/types.h>
#include <exec/interrupts.h>
#include <hardware/intbits.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <string.h>

#include "kll.h"
#include "audio.h"
#include "app.h"
#include "layout.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

UWORD vbl_hz = 50;
UWORD palette[32];

extern void VblServer(void);

static struct Interrupt vbl_int;
static UWORD black[32];

void msg(const char *s)
{
	BPTR out = Output();

	if (out)
		Write(out, (APTR)s, strlen(s));
}

void set_palette(struct Screen *scr)
{
	LoadRGB4(&scr->ViewPort, palette, 32);
}

#ifndef KLL_NO_REBOOT
/*
 * Leaving the program: there is no CLI to return to on a CDTV, so the machine
 * is rebooted, the way the CD titles do it. ColdReboot() arrived with 2.0, so
 * here it is done by hand, the way the ROM does: in supervisor mode, with
 * interrupts off, take the boot vector from the ROM (its length lives at
 * $00FFFFEC), pull the RESET line and jump in. The jump has to follow RESET
 * immediately: it is already in the prefetch queue, so it gets there even
 * while the machine is resetting.
 *
 * "make KLL_NO_REBOOT=1" returns to the CLI instead: handy under an emulator,
 * where a reboot would start the CD all over again.
 */
static void cold_reboot(void)
{
	register struct ExecBase *sys __asm("a6") = SysBase;

	__asm volatile (
		"	lea	1f(pc),a5\n"
		"	jsr	-30(a6)\n"		/* Supervisor() */
		"	bra.s	2f\n"
		"1:	move.w	#0x2700,sr\n"
		"	lea	0x01000000,a0\n"
		"	sub.l	-0x14(a0),a0\n"	/* start of the ROM */
		"	move.l	4(a0),a0\n"		/* boot vector */
		"	subq.l	#2,a0\n"
		"	reset\n"
		"	jmp	(a0)\n"
		"2:\n"
		:
		: "r" (sys)
		: "d0", "d1", "a0", "a1", "a5", "cc", "memory");
}
#endif

int main(void)
{
	static struct NewScreen ns = {
		0, 0, SCREEN_W, SCREEN_H, 5, 0, 1, 0,
		CUSTOMSCREEN | SCREENQUIET, NULL, NULL, NULL, NULL
	};
	static struct NewWindow nw = {
		0, 0, SCREEN_W, SCREEN_H, 0, 1,
		IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY,
		WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_ACTIVATE | WFLG_RMBTRAP | WFLG_NOCAREREFRESH,
		NULL, NULL, NULL, NULL, NULL,
		SCREEN_W, SCREEN_H, SCREEN_W, SCREEN_H, CUSTOMSCREEN
	};
	static const struct { UWORD index, color; } pointer[NPOINTER_COLORS] = POINTER_COLORS;
	struct Screen *scr = NULL;
	struct Window *win = NULL;
	BOOL vbl_added = FALSE;
	int rc = 20, i;

	IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 33);
	GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 33);
	if (!IntuitionBase || !GfxBase)
		goto cleanup;
	vbl_hz = (GfxBase->DisplayFlags & PAL) ? 50 : 60;

	/*
	 * Mouse pointer sprite colours (registers 17-19): Intuition rewrites
	 * them whenever it likes, so neither the clips nor the pictures use
	 * them (the same values are in tools/layout.py).
	 */
	for (i = 0; i < NPOINTER_COLORS; i++)
		palette[pointer[i].index] = pointer[i].color;

	scr = OpenScreen(&ns);
	if (!scr) {
		msg("KLL: cannot open the screen\n");
		goto cleanup;
	}
	LoadRGB4(&scr->ViewPort, black, 32);
	nw.Screen = scr;
	win = OpenWindow(&nw);
	if (!win)
		goto cleanup;

	vbl_int.is_Node.ln_Type = NT_INTERRUPT;
	vbl_int.is_Node.ln_Name = (char *)"KLL vblank";
	vbl_int.is_Code = (VOID (*)())VblServer;
	AddIntServer(INTB_VERTB, &vbl_int);
	vbl_added = TRUE;

	if (!audio_open())
		msg("KLL: audio.device not available\n");

	app_run(scr, win);
	rc = 0;

cleanup:
	audio_close();
	if (vbl_added)
		RemIntServer(INTB_VERTB, &vbl_int);
	if (win)
		CloseWindow(win);
	if (scr)
		CloseScreen(scr);
	if (GfxBase)
		CloseLibrary((struct Library *)GfxBase);
	if (IntuitionBase)
		CloseLibrary((struct Library *)IntuitionBase);
#ifndef KLL_NO_REBOOT
	cold_reboot();		/* does not return */
#endif
	return rc;
}
