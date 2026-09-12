#ifndef KLL_AUDIO_H
#define KLL_AUDIO_H

#include <exec/types.h>

#define AUDIO_MAXTAGS 32

BOOL audio_open(void);
void audio_close(void);
BOOL audio_ready(void);			/* device open */

/*
 * Queues a block of samples on the left and right channels. The buffer must
 * be in chip RAM and stay valid until audio_pending(tag) returns 0. A block
 * cannot exceed 65535 words (131070 bytes): the channel length counter is
 * 16-bit, so longer samples have to be split by the caller.
 */
BOOL audio_queue(UBYTE *data, ULONG len, UWORD period, UWORD tag);

void audio_poll(void);			/* collects the finished requests */
UWORD audio_pending(UWORD tag);
BOOL audio_idle(void);
void audio_stop(void);			/* aborts and drops everything */

#endif
