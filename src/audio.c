/*
 * Streaming sample playback through audio.device (Kickstart 1.3).
 *
 * Successive writes on the same channel are queued by the device and played
 * back to back. Every block is written to one left and one right channel
 * from the same buffer.
 */
#include <exec/memory.h>
#include <exec/ports.h>
#include <devices/audio.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>

#include "kll.h"
#include "audio.h"

#define MAXREQ 64

static struct MsgPort *port;
static struct IOAudio *ctrl;		/* request used to allocate the channels */
static struct IOAudio *reqs[MAXREQ];
static UBYTE busy[MAXREQ];
static UBYTE req_tag[MAXREQ];
static UBYTE tag_pending[AUDIO_MAXTAGS];
static BOOL dev_open;
static ULONG left_mask, right_mask;

/* left+right channel pairs */
static UBYTE channel_map[] = { 3, 5, 10, 12 };

BOOL audio_open(void)
{
	ULONG mask;
	int i;

	port = port_create();
	if (!port)
		goto fail;
	ctrl = AllocMem(sizeof(struct IOAudio), MEMF_PUBLIC | MEMF_CLEAR);
	if (!ctrl)
		goto fail;
	ctrl->ioa_Request.io_Message.mn_ReplyPort = port;
	ctrl->ioa_Request.io_Message.mn_Node.ln_Pri = ADALLOC_MAXPREC;
	ctrl->ioa_Request.io_Message.mn_Length = sizeof(struct IOAudio);
	ctrl->ioa_Data = channel_map;
	ctrl->ioa_Length = sizeof(channel_map);
	if (OpenDevice((CONST_STRPTR)AUDIONAME, 0, (struct IORequest *)ctrl, 0))
		goto fail;
	dev_open = TRUE;

	mask = (ULONG)ctrl->ioa_Request.io_Unit;
	left_mask = mask & 9;
	right_mask = mask & 6;

	for (i = 0; i < MAXREQ; i++) {
		reqs[i] = AllocMem(sizeof(struct IOAudio), MEMF_PUBLIC | MEMF_CLEAR);
		if (!reqs[i])
			goto fail;
		*reqs[i] = *ctrl;
	}
	return TRUE;

fail:
	audio_close();
	return FALSE;
}

void audio_close(void)
{
	int i;

	if (dev_open) {
		audio_stop();
		CloseDevice((struct IORequest *)ctrl);
		dev_open = FALSE;
	}
	for (i = 0; i < MAXREQ; i++) {
		if (reqs[i]) {
			FreeMem(reqs[i], sizeof(struct IOAudio));
			reqs[i] = NULL;
		}
	}
	if (ctrl) {
		FreeMem(ctrl, sizeof(struct IOAudio));
		ctrl = NULL;
	}
	if (port) {
		port_delete(port);
		port = NULL;
	}
}

BOOL audio_ready(void)
{
	return dev_open;
}

void audio_poll(void)
{
	struct Message *m;
	int i;

	if (!dev_open)
		return;
	while ((m = GetMsg(port)) != NULL) {
		for (i = 0; i < MAXREQ; i++) {
			if ((struct Message *)reqs[i] == m) {
				busy[i] = 0;
				tag_pending[req_tag[i]]--;
				break;
			}
		}
	}
}

static int free_req(void)
{
	int i;

	for (i = 0; i < MAXREQ; i++)
		if (!busy[i])
			return i;
	return -1;
}

BOOL audio_queue(UBYTE *data, ULONG len, UWORD period, UWORD tag)
{
	ULONG masks[2];
	int c, i;

	if (!dev_open || tag >= AUDIO_MAXTAGS)
		return FALSE;
	masks[0] = left_mask;
	masks[1] = right_mask;
	for (c = 0; c < 2; c++) {
		struct IOAudio *r;

		if (!masks[c])
			continue;
		while ((i = free_req()) < 0) {
			WaitPort(port);
			audio_poll();
		}
		r = reqs[i];
		r->ioa_Request.io_Command = CMD_WRITE;
		r->ioa_Request.io_Flags = ADIOF_PERVOL;
		r->ioa_Request.io_Unit = (struct Unit *)masks[c];
		r->ioa_AllocKey = ctrl->ioa_AllocKey;
		r->ioa_Data = data;
		r->ioa_Length = len & ~1UL;
		r->ioa_Period = period;
		r->ioa_Volume = 64;
		r->ioa_Cycles = 1;
		busy[i] = 1;
		req_tag[i] = tag;
		tag_pending[tag]++;
		/* BeginIO, not SendIO, which would clear ADIOF_PERVOL */
		BeginIO((struct IORequest *)r);
	}
	return TRUE;
}

UWORD audio_pending(UWORD tag)
{
	return tag < AUDIO_MAXTAGS ? tag_pending[tag] : 0;
}

BOOL audio_idle(void)
{
	int i;

	for (i = 0; i < MAXREQ; i++)
		if (busy[i])
			return FALSE;
	return TRUE;
}

void audio_stop(void)
{
	int i;

	if (!dev_open)
		return;
	audio_poll();
	for (i = 0; i < MAXREQ; i++)
		if (busy[i])
			AbortIO((struct IORequest *)reqs[i]);
	for (i = 0; i < MAXREQ; i++) {
		if (busy[i]) {
			WaitIO((struct IORequest *)reqs[i]);
			busy[i] = 0;
			tag_pending[req_tag[i]]--;
		}
	}
}
