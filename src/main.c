/*
 * Klingon Language Lab for the Commodore CDTV.
 *
 * Opens the 320x256 32-colour screen, the vertical blank server and
 * audio.device, then hands over to the interface (src/app.c). The left mouse
 * button is button A on the CDTV remote, the right one is button B.
 */
#include <exec/types.h>
#include <exec/interrupts.h>
#include <exec/memory.h>
#include <devices/input.h>
#include <devices/inputevent.h>
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

static struct Interrupt input_int;
static struct MsgPort *input_port;
static struct IOStdReq *input_req;
static BOOL input_added;

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

/*
 * Intuition lets the user drag a screen down (left Amiga + mouse, or the
 * title bar, which stays active above backdrop windows even when SCREENQUIET
 * hides it) and flip screens with left Amiga + N/M: either way the boot CLI
 * shows up behind the program. Kickstart 1.3 has no way to lock a screen in
 * place, so this input handler, ahead of Intuition (priority 50), takes those
 * events away. It runs in the input.device task: no library calls here.
 */
static struct InputEvent *input_filter(register struct InputEvent *list __asm("a0"),
				       register struct Screen *scr __asm("a1"))
{
	struct InputEvent *ie;

	for (ie = list; ie; ie = ie->ie_NextEvent) {
		if (ie->ie_Class == IECLASS_RAWMOUSE) {
			ie->ie_Qualifier &= ~(IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND);
			/* no hotspot lives up there: the rows are the black border */
			if (ie->ie_Code == IECODE_LBUTTON && scr->MouseY <= scr->BarHeight)
				ie->ie_Class = IECLASS_NULL;
		} else if (ie->ie_Class == IECLASS_RAWKEY &&
			   (ie->ie_Qualifier & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) &&
			   ((ie->ie_Code & ~IECODE_UP_PREFIX) == 0x36 ||	/* N */
			    (ie->ie_Code & ~IECODE_UP_PREFIX) == 0x37)) {	/* M */
			ie->ie_Class = IECLASS_NULL;
		}
	}
	return list;
}

static BOOL input_open(struct Screen *scr)
{
	input_port = port_create();
	if (!input_port)
		return FALSE;
	input_req = AllocMem(sizeof(*input_req), MEMF_PUBLIC | MEMF_CLEAR);
	if (!input_req)
		return FALSE;
	input_req->io_Message.mn_Node.ln_Type = NT_MESSAGE;
	input_req->io_Message.mn_ReplyPort = input_port;
	input_req->io_Message.mn_Length = sizeof(*input_req);
	if (OpenDevice((CONST_STRPTR)"input.device", 0, (struct IORequest *)input_req, 0)) {
		FreeMem(input_req, sizeof(*input_req));
		input_req = NULL;
		return FALSE;
	}
	input_int.is_Node.ln_Type = NT_INTERRUPT;
	input_int.is_Node.ln_Pri = 51;
	input_int.is_Node.ln_Name = (char *)"KLL screen lock";
	input_int.is_Code = (VOID (*)())input_filter;
	input_int.is_Data = scr;
	input_req->io_Command = IND_ADDHANDLER;
	input_req->io_Data = &input_int;
	DoIO((struct IORequest *)input_req);
	input_added = TRUE;
	return TRUE;
}

static void input_close(void)
{
	if (input_req) {
		if (input_added) {
			input_req->io_Command = IND_REMHANDLER;
			input_req->io_Data = &input_int;
			DoIO((struct IORequest *)input_req);
			input_added = FALSE;
		}
		CloseDevice((struct IORequest *)input_req);
		FreeMem(input_req, sizeof(*input_req));
		input_req = NULL;
	}
	if (input_port) {
		port_delete(input_port);
		input_port = NULL;
	}
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

	if (!input_open(scr))
		msg("KLL: input.device not available, the screen can be dragged\n");
	if (!audio_open())
		msg("KLL: audio.device not available\n");

	app_run(scr, win);
	rc = 0;

cleanup:
	audio_close();
	input_close();
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
