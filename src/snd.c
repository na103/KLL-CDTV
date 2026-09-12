/*
 * Sound effects and speech: whole samples loaded into chip RAM and handed to
 * audio.device. The .RAW files are signed 8-bit PCM at 11025 Hz mono
 * (tools/mkmedia.py), the same format as the audio inside the clips.
 *
 * The audio.device tags start at SND_TAG, so they never clash with the ones
 * the video player uses for its own blocks.
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "kll.h"
#include "audio.h"
#include "snd.h"

#define SND_TAG		8
/*
 * A single write cannot exceed 65535 words: the channel length counter is
 * 16-bit, and past that the count wraps around (a twelve-second commentary
 * played for less than one). Long samples are therefore queued in chunks,
 * which the device plays back to back.
 */
#define SND_CHUNK	0x10000UL
#define SND_RATE	11025UL
#define PAL_CLOCK	3546895UL
#define NTSC_CLOCK	3579545UL

static UBYTE *buf[SND_SLOTS];
static ULONG len[SND_SLOTS];

static UWORD snd_period(void)
{
	return (UWORD)((vbl_hz == 50 ? PAL_CLOCK : NTSC_CLOCK) / SND_RATE);
}

static void release(WORD i)
{
	if (buf[i])
		FreeMem(buf[i], len[i]);
	buf[i] = NULL;
	len[i] = 0;
}

void snd_poll(void)
{
	WORD i;

	audio_poll();
	for (i = 0; i < SND_SLOTS; i++)
		if (buf[i] && !audio_pending(SND_TAG + i))
			release(i);
}

BOOL snd_busy(WORD slot)
{
	if (slot < 0 || slot >= SND_SLOTS)
		return FALSE;
	return buf[slot] && audio_pending(SND_TAG + slot);
}

WORD snd_play(CONST_STRPTR name)
{
	WORD i;
	BPTR fh;
	ULONG size;

	snd_poll();
	for (i = 0; i < SND_SLOTS && buf[i]; i++)
		;
	if (i == SND_SLOTS)
		return -1;

	fh = Open(name, MODE_OLDFILE);
	if (!fh)
		return -1;
	Seek(fh, 0, OFFSET_END);
	size = Seek(fh, 0, OFFSET_BEGINNING);
	if (size < 2)
		goto fail;
	buf[i] = AllocMem(size, MEMF_CHIP);
	if (!buf[i])
		goto fail;
	len[i] = size;
	if (Read(fh, buf[i], size) != (LONG)size)
		goto fail;
	Close(fh);

	{
		UWORD period = snd_period();
		ULONG off, n;

		for (off = 0; off < size; off += n) {
			n = size - off;
			if (n > SND_CHUNK)
				n = SND_CHUNK;
			n &= ~1UL;
			if (!n || !audio_queue(buf[i] + off, n, period, SND_TAG + i))
				break;
		}
		if (!off) {		/* nothing queued: nothing to wait for */
			release(i);
			return -1;
		}
	}
	return i;

fail:
	release(i);
	Close(fh);
	return -1;
}

void snd_stop(void)
{
	WORD i;

	audio_stop();
	for (i = 0; i < SND_SLOTS; i++)
		release(i);
}
