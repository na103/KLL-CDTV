#ifndef KLL_DB_H
#define KLL_DB_H

#include <exec/types.h>

#define ITEM_MORE	1	/* the entry has the "more" audio */

/* A database entry: the names are the CD file stems, without extension. */
struct item {
	UWORD flags;
	CONST_STRPTR snd;	/* SND/<snd>.RAW */
	CONST_STRPTR more;	/* SND/<more>.RAW */
	CONST_STRPTR vid;	/* VIDEO/<vid>.KXL */
	CONST_STRPTR img;	/* IMG/<img>.PIC */
	/* comment on a wrong drill answer, SND/<...>.RAW: the first is used
	   where the answers are written, the second where they are listened
	   to (as on the original CD) */
	CONST_STRPTR wk;
	CONST_STRPTR we;
	CONST_STRPTR klingon;
	CONST_STRPTR english;
};

BOOL db_open(CONST_STRPTR name);
void db_close(void);

UWORD db_cats(void);
UWORD db_count(UWORD cat);		/* entries in the category, cat from 0 */
CONST_STRPTR db_name(UWORD cat);	/* name of the category */
BOOL db_item(UWORD cat, UWORD idx, struct item *it);

#endif
