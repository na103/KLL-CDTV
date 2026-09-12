#ifndef KLL_SND_H
#define KLL_SND_H

#include <exec/types.h>

#define SND_SLOTS	3	/* sounds playing at once */

/*
 * Plays a RAW sample (signed 8-bit PCM, 11025 Hz mono) read from the CD.
 * Returns the slot number, or -1 if the file is missing or memory ran out;
 * the buffer stays in chip RAM until the sound has finished.
 */
WORD snd_play(CONST_STRPTR name);

void snd_poll(void);		/* frees the slots of finished sounds */
BOOL snd_busy(WORD slot);
void snd_stop(void);		/* stops everything and frees the buffers */

#endif
