#ifndef KLL_VIDEO_H
#define KLL_VIDEO_H

#include <exec/types.h>
#include <intuition/intuition.h>

#define VIDEO_DONE	0
#define VIDEO_ABORTED	1
#define VIDEO_ERROR	-1

#define VIDEO_SYNC_READ	1	/* blocks read with Read() instead of packets */
#define VIDEO_SLOW_BLIT	2	/* waits a vertical blank after every copy */
#define VIDEO_VERIFY	4	/* checks every block checksum (costs CPU) */

struct video_stats {
	ULONG frames;		/* frames read */
	ULONG shown;		/* frames copied to the screen */
	ULONG skipped;		/* frames decoded but dropped because they were late */
	ULONG underruns;	/* times the audio ran dry */
	ULONG max_read;		/* slowest read, in vertical blanks */
	ULONG bad_blocks;	/* blocks read with a wrong checksum */
	ULONG bad_frames;	/* frames whose buffer did not match after decoding */
	ULONG max_blit;		/* longest copy, in raster lines */
	ULONG late_blits;	/* stripes copied without waiting for the beam */
};

/*
 * Plays a KXL3 clip at (x,y) on the screen. A mouse click or ESC stops it.
 * The VIDEO_* flags are there to compare reading and copying strategies. If
 * progress is not NULL it is called about once a second with the current
 * statistics.
 */
LONG video_play(CONST_STRPTR name, struct Screen *scr, struct Window *win,
		WORD x, WORD y, UWORD flags, struct video_stats *st,
		void (*progress)(struct Screen *, const struct video_stats *));

#endif
