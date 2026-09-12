/*
 * Playback of KXL3 clips streamed from the CD (format described in
 * tools/kxl.py).
 *
 * After the header the file is split into fixed-size blocks. Reading normally
 * uses ACTION_READ packets sent asynchronously to the file system, so the
 * loop keeps showing frames while the CD reads; with VIDEO_SYNC_READ it uses
 * Read() instead. Two requests are kept in flight, so the file system always
 * has the next one queued and the drive never idles while the CPU verifies a
 * block and decodes frames: packets to one handler are served in order, so
 * blocks arrive in file order. The number of blocks is in the header, so the
 * player never reads past the end.
 *
 * Each block holds the audio for its frames, queued to audio.device in a
 * single write, plus the video data of those frames. When it is time to show
 * a frame, every frame still owed is decoded into the frame buffer (each one
 * is the difference from the previous) and only the last is copied to the
 * screen, while the raster beam is outside the rectangle. A block is freed
 * once all its frames have been decoded and its audio has played.
 *
 * The file carries a checksum per block and the expected frame buffer sum
 * after each frame: the player verifies both and counts failures in the
 * statistics.
 *
 * The copy to the screen is split into horizontal stripes, each copied just
 * after the raster has passed it: a copy of the whole rectangle takes more
 * than two thirds of a frame and the beam would always catch up with it.
 *
 * Four-bitplane clips are copied into planes 0-3 of the rectangle with plane
 * 4 set, so they use colours 16-31. In five-bitplane clips colours 0-15 are
 * the interface ones. Registers 16-31 take the palette from the file, where
 * 17-19 are the pointer colours: Intuition uses them for the mouse sprite,
 * so the clips leave them alone.
 */
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "kll.h"
#include "audio.h"
#include "delta.h"
#include "video.h"

#define MAGIC		0x4B584C33UL	/* 'KXL3' */
#define FH_SIZE		32		/* file header without the palette */
#define BH_SIZE		8		/* block header */
#define NBLOCKS		5	/* blocks held in memory */
#define NREQ		2	/* reads in flight at once */
#define MAXQ		64	/* frames read but not yet decoded */
#define MAXAHEAD	18	/* with this many frames queued, stop reading */
#define LEAD		12	/* frames read before playback starts */
#define MIN_BLOCK	4096
#define MAX_BLOCK	65536

#define PAL_CLOCK	3546895UL
#define NTSC_CLOCK	3579545UL

#define RAWKEY_ESC	0x45

#define RASTER_TOP	44	/* first visible raster line (standard DIWSTRT) */
#define STRIPES		8	/* horizontal stripes the copy is split into */
#define BLIT_LINES	60	/* raster lines guessed per stripe, then measured */
#define BLIT_LATE	2	/* vertical blanks after which the copy happens anyway */

struct block {
	UBYTE *mem;
	UWORD frames;		/* frames of this block not yet decoded */
};

struct frame {
	UBYTE *video;
	UWORD block;
	UWORD fbsum;		/* expected frame buffer sum after the frame */
};

/* state of a read request */
#define RD_IDLE		0
#define RD_SENT		1	/* sent to the file system */
#define RD_DONE		2	/* finished, block not verified yet */

struct reader {
	struct StandardPacket pkt;
	UWORD block;
	UBYTE state;
	ULONG sent;		/* vertical blank when it was sent */
};

struct player {
	struct block blocks[NBLOCKS];
	struct frame q[MAXQ];
	struct reader rd[NREQ];
	struct MsgPort *port;
	struct FileHandle *fh;
	UWORD rdhead, rdtail, rdcount;	/* requests in flight, in send order */
	UWORD pal[32];
	UWORD waiting[NBLOCKS];	/* blocks read before the start, audio still to queue */
	UWORD nwaiting;
	UBYTE *fb;		/* frame buffer: bitplanes one after another */
	UWORD pending;		/* frames decoded but not copied yet */
	UWORD todo;		/* stripes still to copy */
	ULONG pend_at;		/* vertical blank when it was decoded */
	WORD btop;		/* first raster line of the rectangle */
	WORD dur, dmax;		/* stripe duration, measured and largest useful */
	ULONG block_size, nblocks, blocks_read, first_block;
	ULONG plane_size, fb_size, nread;
	UWORD qhead, qcount, fbsum;
	UWORD w, h, planes, rowbytes, fps, asize, ncolors, period;
	BOOL started;
};

static void read_send(struct player *pl, UWORD b)
{
	struct reader *rd = &pl->rd[pl->rdtail];

	rd->pkt.sp_Msg.mn_Node.ln_Name = (char *)&rd->pkt.sp_Pkt;
	rd->pkt.sp_Pkt.dp_Link = &rd->pkt.sp_Msg;
	rd->pkt.sp_Pkt.dp_Port = pl->port;
	rd->pkt.sp_Pkt.dp_Type = ACTION_READ;
	rd->pkt.sp_Pkt.dp_Arg1 = pl->fh->fh_Arg1;
	rd->pkt.sp_Pkt.dp_Arg2 = (LONG)pl->blocks[b].mem;
	rd->pkt.sp_Pkt.dp_Arg3 = pl->block_size;
	rd->block = b;
	rd->sent = vbl_count;
	rd->state = RD_SENT;
	pl->rdtail = (pl->rdtail + 1) % NREQ;
	pl->rdcount++;
	PutMsg(pl->fh->fh_Type, &rd->pkt.sp_Msg);
}

/* marks the finished reads, without waiting */
static void read_poll(struct player *pl)
{
	struct Message *m;
	UWORD i;

	while ((m = GetMsg(pl->port)) != NULL)
		for (i = 0; i < NREQ; i++)
			if (m == &pl->rd[i].pkt.sp_Msg) {
				pl->rd[i].state = RD_DONE;
				break;
			}
}

/* waits for and discards the reads still in flight */
static void read_flush(struct player *pl)
{
	while (pl->rdcount) {
		struct reader *rd = &pl->rd[pl->rdhead];

		while (rd->state == RD_SENT) {
			read_poll(pl);
			if (rd->state == RD_SENT)
				WaitPort(pl->port);
		}
		rd->state = RD_IDLE;
		pl->rdhead = (pl->rdhead + 1) % NREQ;
		pl->rdcount--;
	}
}

static WORD free_block(struct player *pl)
{
	WORD i;
	UWORD r;

	for (i = 0; i < NBLOCKS; i++) {
		if (pl->blocks[i].frames || audio_pending(i))
			continue;
		for (r = 0; r < NREQ; r++)
			if (pl->rd[r].state != RD_IDLE && pl->rd[r].block == i)
				break;
		if (r == NREQ)
			return i;
	}
	return -1;
}

static void queue_block_audio(struct player *pl, UWORD b)
{
	UBYTE *mem = pl->blocks[b].mem;
	UWORD len = rd16(mem + 2);

	if (len)
		audio_queue(mem + BH_SIZE, len, pl->period, b);
}

/*
 * Checks a freshly read block and queues its frames. FALSE if the read is
 * short or the block structure is invalid; a wrong checksum is only counted.
 */
static BOOL parse_block(struct player *pl, struct video_stats *st, UWORD b, LONG got,
			BOOL verify)
{
	UBYTE *mem = pl->blocks[b].mem;
	UWORD n, asize, used, cks = 0, i;
	ULONG off;

	if (got != (LONG)pl->block_size)
		return FALSE;
	n = rd16(mem);
	asize = rd16(mem + 2);
	used = rd16(mem + 4);
	if ((ULONG)n * pl->asize != asize || used > pl->block_size || (used & 1) ||
	    BH_SIZE + (ULONG)asize > used ||
	    pl->qcount + n > MAXQ || (!pl->started && pl->nwaiting == NBLOCKS))
		return FALSE;

	/*
	 * Verifying costs a few milliseconds per block, during which the raster
	 * runs on and waiting stripes lose their window, so it only happens on
	 * request (VIDEO_VERIFY). The block is aligned and used is even, so the
	 * sum is taken in words.
	 */
	if (verify) {
		const UWORD *w = (const UWORD *)(mem + BH_SIZE);
		UWORD nw = (used - BH_SIZE) >> 1;

		while (nw--)
			cks += *w++;
		if (cks != rd16(mem + 6))
			st->bad_blocks++;
	}

	off = BH_SIZE + (ULONG)asize;
	for (i = 0; i < n; i++) {
		struct frame *f;
		UWORD vsize;

		if (off + 4 > used)
			return FALSE;
		vsize = rd16(mem + off);
		f = &pl->q[(pl->qhead + pl->qcount + i) % MAXQ];
		f->fbsum = rd16(mem + off + 2);
		off += 4;
		if (off + vsize > used)
			return FALSE;
		f->video = mem + off;
		f->block = b;
		off += vsize;
	}
	pl->qcount += n;
	pl->nread += n;
	pl->blocks[b].frames = n;
	if (pl->started)
		queue_block_audio(pl, b);
	else
		pl->waiting[pl->nwaiting++] = b;
	return TRUE;
}

/*
 * Clip colours into registers 16-31: a 16-colour palette goes there as is,
 * and in a 32-colour one the first 16 belong to the interface and are
 * ignored.
 */
static void clip_palette(struct Screen *scr, const UWORD *pal, UWORD n)
{
	UWORD base = n > 16 ? 0 : 16, i;

	for (i = 0; i < n; i++) {
		UWORD reg = base + i, c = pal[i];

		if (reg >= 16)
			SetRGB4(&scr->ViewPort, reg, c >> 8, (c >> 4) & 15, c & 15);
	}
}

static BOOL user_abort(struct Window *win)
{
	struct IntuiMessage *im;
	BOOL abort = FALSE;

	while ((im = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
		if (im->Class == IDCMP_MOUSEBUTTONS &&
		    (im->Code == SELECTDOWN || im->Code == MENUDOWN))
			abort = TRUE;
		if (im->Class == IDCMP_RAWKEY && im->Code == RAWKEY_ESC)
			abort = TRUE;
		ReplyMsg((struct Message *)im);
	}
	return abort;
}

static WORD raster_lines(void)
{
	return vbl_hz == 50 ? 313 : 263;
}

/*
 * The copy to the screen is not synchronised with the raster: BltBitMap
 * copies one bitplane at a time, so if the beam crosses the stripe during
 * the copy the lines it has passed show some planes of the new frame and
 * some of the old one, that is, pixels in colours that do not exist. This
 * function returns how many lines are left before the beam comes back to the
 * top of the stripe, which is how long the copy has; -1 if the beam is
 * crossing it right now.
 */
static WORD beam_gap(WORD top, WORD bottom)
{
	WORD pos = (WORD)VBeamPos();
	WORD gap;

	if (pos >= top && pos < bottom)
		return -1;
	gap = top - pos;
	if (gap < 0)
		gap += raster_lines();
	return gap;
}

/*
 * How many lines must be left for a copy to be worth starting: the duration
 * measured on past copies plus half of it as margin, since a copy can take
 * longer because of interrupts or a blitter that is still busy.
 */
static WORD beam_need(WORD dur, WORD height)
{
	WORD need = dur + (dur >> 1) + 16;
	WORD lim = raster_lines() - height - 8;

	return need > lim ? lim : need;
}

/* duration of the last copy, in raster lines */
static WORD beam_elapsed(WORD from)
{
	WORD pos = (WORD)VBeamPos() - from;

	return pos < 0 ? pos + raster_lines() : pos;
}

/*
 * Copies the stripes the raster has already passed; once they are all on the
 * screen the frame is shown. A stripe that has been waiting too many vertical
 * blanks is copied anyway, so playback never stalls.
 *
 * The raster check and the copy sit inside Forbid(): the file system task has
 * a higher priority and, if it took the CPU between the two, the copy would
 * start with the raster somewhere else entirely. Interrupts stay enabled and
 * copying one stripe takes a few milliseconds.
 */
static void blit_stripes(struct player *pl, struct Screen *scr, struct BitMap *bm,
			 WORD x, WORD y, struct video_stats *st, UWORD flags)
{
	BOOL late = vbl_count - pl->pend_at > BLIT_LATE;
	UWORD s;

	for (s = 0; s < STRIPES; s++) {
		WORD sy, sh, beam, gap;

		if (!(pl->todo & (1 << s)))
			continue;
		sy = (WORD)(((ULONG)pl->h * s) / STRIPES);
		sh = (WORD)(((ULONG)pl->h * (s + 1)) / STRIPES) - sy;
		Forbid();
		gap = beam_gap(pl->btop + sy, pl->btop + sy + sh);
		if (gap < 0 || gap <= beam_need(pl->dur, sh)) {
			Permit();
			if (!late)
				continue;
			st->late_blits++;
			Forbid();
			gap = raster_lines();	/* copy anyway: already late */
		}
		beam = (WORD)VBeamPos();
		BltBitMap(bm, 0, sy, &scr->BitMap, x, y + sy, pl->w, sh, 0xC0,
			  pl->planes < 5 ? 0x0F : 0x1F, NULL);
		blit_wait();
		beam = beam_elapsed(beam);
		if (beam >= gap)
			st->late_blits++;	/* the raster caught up with us anyway */
		Permit();
		/*
		 * Reference duration: the maximum, but easing back down. If one copy
		 * is stretched by an interrupt, keeping that value forever would
		 * narrow the window so much that every stripe would end up copying
		 * late, and the fault would last for the whole clip.
		 */
		if (beam > pl->dur)
			pl->dur = beam > pl->dmax ? pl->dmax : beam;
		else
			pl->dur -= (pl->dur - beam) >> 3;
		if ((ULONG)beam > st->max_blit)
			st->max_blit = beam;
		pl->todo &= ~(1 << s);
	}
	if (pl->todo)
		return;
	if (flags & VIDEO_SLOW_BLIT)
		WaitTOF();	/* experiment: leaves the frame buffer still for a while */
	st->shown++;
	st->skipped += pl->pending - 1;
	pl->pending = 0;
	pl->todo = (1 << STRIPES) - 1;
}

LONG video_play(CONST_STRPTR name, struct Screen *scr, struct Window *win,
		WORD x, WORD y, UWORD flags, struct video_stats *st,
		void (*progress)(struct Screen *, const struct video_stats *))
{
	struct player *pl;
	UBYTE hdr[FH_SIZE + 64];
	struct BitMap bm;
	struct RastPort rp;
	BPTR fh = 0;
	ULONG t0 = 0, done_frames = 0, next_progress = 0;
	LONG result = VIDEO_ERROR;
	BOOL eof = FALSE, starved = FALSE;
	WORD i, b;

	st->frames = st->shown = st->skipped = st->underruns = st->max_read = 0;
	st->bad_blocks = st->bad_frames = st->max_blit = st->late_blits = 0;
	pl = AllocMem(sizeof(*pl), MEMF_CLEAR);
	if (!pl)
		return VIDEO_ERROR;

	fh = Open(name, MODE_OLDFILE);
	if (!fh)
		goto done;
	if (Read(fh, hdr, sizeof(hdr)) != sizeof(hdr) || rd32(hdr) != MAGIC)
		goto done;

	pl->w = rd16(hdr + 4);
	pl->h = rd16(hdr + 6);
	pl->planes = rd16(hdr + 8);
	pl->fps = rd16(hdr + 10);
	pl->asize = rd16(hdr + 12);
	pl->ncolors = rd16(hdr + 14);
	pl->block_size = rd32(hdr + 16);
	pl->nblocks = rd32(hdr + 20);
	pl->first_block = rd32(hdr + 28);
	pl->rowbytes = ((pl->w + 15) >> 4) << 1;
	pl->plane_size = (ULONG)pl->rowbytes * pl->h;
	if (pl->planes < 4 || pl->planes > 5 || pl->w == 0 || pl->w > 320 ||
	    pl->h == 0 || pl->h > 256 || pl->fps == 0 || pl->fps > 50 ||
	    pl->asize == 0 || (pl->asize & 1) || pl->ncolors > 32 ||
	    pl->block_size < MIN_BLOCK || pl->block_size > MAX_BLOCK ||
	    pl->nblocks == 0 || pl->first_block < sizeof(hdr))
		goto done;
	for (i = 0; i < pl->ncolors; i++)
		pl->pal[i] = rd16(hdr + FH_SIZE + 2 * i) & 0xFFF;
	if (Seek(fh, pl->first_block, OFFSET_BEGINNING) < 0)
		goto done;

	pl->fb_size = pl->plane_size * pl->planes;
	pl->fb = AllocMem(pl->fb_size, MEMF_CHIP | MEMF_CLEAR);
	if (!pl->fb)
		goto done;
	for (i = 0; i < NBLOCKS; i++) {
		pl->blocks[i].mem = AllocMem(pl->block_size, MEMF_CHIP);
		if (!pl->blocks[i].mem)
			goto done;
	}
	pl->port = port_create();
	if (!pl->port)
		goto done;
	pl->fh = (struct FileHandle *)BADDR(fh);

	pl->period = (UWORD)((vbl_hz == 50 ? PAL_CLOCK : NTSC_CLOCK) / ((ULONG)pl->asize * pl->fps));
	InitBitMap(&bm, pl->planes, pl->rowbytes * 8, pl->h);
	for (i = 0; i < pl->planes; i++)
		bm.Planes[i] = pl->fb + (ULONG)i * pl->plane_size;
	InitRastPort(&rp);
	rp.BitMap = &scr->BitMap;
	pl->todo = (1 << STRIPES) - 1;
	pl->btop = RASTER_TOP + scr->TopEdge + y;
	/* past this duration no moment would be safe: copy regardless */
	pl->dmax = raster_lines() - (pl->h / STRIPES) - 8;
	pl->dur = BLIT_LINES < pl->dmax ? BLIT_LINES : pl->dmax;
	result = VIDEO_DONE;

	for (;;) {
		BOOL worked = FALSE;

		audio_poll();
		if (user_abort(win)) {
			result = VIDEO_ABORTED;
			break;
		}

		/* free stripes are copied at once: the window is short */
		if (pl->started && pl->pending) {
			blit_stripes(pl, scr, &bm, x, y, st, flags);
			worked = TRUE;
		}

		/* finished reads, verified in file order */
		read_poll(pl);
		while (pl->rdcount && pl->rd[pl->rdhead].state == RD_DONE) {
			struct reader *rd = &pl->rd[pl->rdhead];

			if (vbl_count - rd->sent > st->max_read)
				st->max_read = vbl_count - rd->sent;
			rd->state = RD_IDLE;
			pl->rdhead = (pl->rdhead + 1) % NREQ;
			pl->rdcount--;
			if (!parse_block(pl, st, rd->block, rd->pkt.sp_Pkt.dp_Res1,
					 (flags & VIDEO_VERIFY) != 0)) {
				result = VIDEO_ERROR;
				goto stop;
			}
			if (++pl->blocks_read == pl->nblocks)
				eof = TRUE;
			worked = TRUE;
		}

		/* new reads: two in flight, so the drive never idles */
		while (pl->blocks_read + pl->rdcount < pl->nblocks && pl->rdcount < NREQ &&
		       pl->qcount < MAXAHEAD && (b = free_block(pl)) >= 0) {
			worked = TRUE;
			if (flags & VIDEO_SYNC_READ) {
				ULONG sent = vbl_count;
				LONG got = Read(fh, pl->blocks[b].mem, pl->block_size);

				if (vbl_count - sent > st->max_read)
					st->max_read = vbl_count - sent;
				if (!parse_block(pl, st, b, got, (flags & VIDEO_VERIFY) != 0)) {
					result = VIDEO_ERROR;
					goto stop;
				}
				if (++pl->blocks_read == pl->nblocks)
					eof = TRUE;
				break;
			}
			read_send(pl, b);
		}

		/* start: palette, rectangle and audio of the blocks already read */
		if (!pl->started && (pl->qcount >= LEAD || (eof && !pl->rdcount))) {
			if (pl->qcount == 0) {
				result = VIDEO_ERROR;
				break;
			}
			clip_palette(scr, pl->pal, pl->ncolors);
			if (pl->planes < 5) {
				SetAPen(&rp, 16);
				rp.Mask = 0x10;
				RectFill(&rp, x, y, x + pl->w - 1, y + pl->h - 1);
				rp.Mask = 0xFF;
			}
			for (i = 0; i < pl->nwaiting; i++)
				queue_block_audio(pl, pl->waiting[i]);
			pl->nwaiting = 0;
			t0 = vbl_count;
			next_progress = t0 + vbl_hz;
			pl->started = TRUE;
		}

		if (pl->started) {
			ULONG due = ((vbl_count - t0) * pl->fps) / vbl_hz;

			/* nothing is decoded until the previous frame is fully on
			   screen: the stripes still missing would take the new one */
			if (!pl->pending && due >= done_frames && pl->qcount > 0) {
				ULONG last = done_frames + pl->qcount - 1;
				ULONG target = due < last ? due : last;
				ULONG n = 0;

				/* decode every frame owed, copy only the last */
				while (done_frames <= target) {
					struct frame *f = &pl->q[pl->qhead];
					BOOL ok = delta_decode(f->video, pl->fb, pl->plane_size,
							       pl->planes, &pl->fbsum);

					if (!ok || pl->fbsum != f->fbsum) {
						st->bad_frames++;
						/* realign the sum: only new mismatches count */
						pl->fbsum = f->fbsum;
					}
					pl->blocks[f->block].frames--;
					pl->qhead = (pl->qhead + 1) % MAXQ;
					pl->qcount--;
					done_frames++;
					n++;
				}
				pl->pend_at = vbl_count;
				pl->pending = n;
				worked = TRUE;
			}

			/* audio ran out with blocks still unread: the CD is too slow */
			if (audio_ready() && audio_idle() && !eof) {
				if (!starved)
					st->underruns++;
				starved = TRUE;
			} else {
				starved = FALSE;
			}

			/* the panel uses the blitter: update it only on a complete
			   frame, or it steals the window from waiting stripes */
			if (progress && !pl->pending && vbl_count >= next_progress) {
				st->frames = pl->nread;
				progress(scr, st);
				next_progress = vbl_count + vbl_hz;
			}

			if (eof && !pl->rdcount && pl->qcount == 0 && !pl->pending && audio_idle())
				break;
		}

		if (!worked)
			WaitTOF();
	}
stop:
	read_flush(pl);
	audio_stop();
	/*
	 * As on the CD, the rectangle keeps the last frame: it is neither
	 * cleared nor given the shared palette back, which would repaint what
	 * is left there in the interface colours.
	 */
	st->frames = pl->nread;

done:
	if (pl->port)
		port_delete(pl->port);
	for (i = 0; i < NBLOCKS; i++)
		if (pl->blocks[i].mem)
			FreeMem(pl->blocks[i].mem, pl->block_size);
	if (pl->fb)
		FreeMem(pl->fb, pl->fb_size);
	if (fh)
		Close(fh);
	FreeMem(pl, sizeof(*pl));
	return result;
}
