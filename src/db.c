/*
 * Database of entries (KDB1 format, see tools/mkdb.py). The file is small (a
 * few thousand bytes) and is kept in memory as a whole: entries can then be
 * read while the CD drive is busy with the content.
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "kll.h"
#include "db.h"

#define HDR		6	/* 'KDB1', number of categories */
#define CATSIZE		18	/* count, table offset, name */
#define NAMELEN		12
#define BASELEN		10	/* file names inside a record */

static UBYTE *db;
static ULONG db_size;
static UWORD ncats;

BOOL db_open(CONST_STRPTR name)
{
	BOOL ok = FALSE;
	BPTR fh;

	db_close();
	fh = Open(name, MODE_OLDFILE);
	if (!fh)
		return FALSE;
	Seek(fh, 0, OFFSET_END);
	db_size = Seek(fh, 0, OFFSET_BEGINNING);	/* returns the previous position */
	if (db_size < HDR)
		goto done;

	db = AllocMem(db_size, MEMF_PUBLIC);
	if (!db)
		goto done;
	ok = Read(fh, db, db_size) == (LONG)db_size;
	if (ok)
		ok = db[0] == 'K' && db[1] == 'D' && db[2] == 'B' && db[3] == '1';
	if (ok) {
		ncats = rd16(db + 4);
		ok = (ULONG)HDR + ncats * CATSIZE <= db_size;
	}

done:
	Close(fh);
	if (!ok)
		db_close();
	return ok;
}

void db_close(void)
{
	if (db)
		FreeMem(db, db_size);
	db = NULL;
	db_size = 0;
	ncats = 0;
}

UWORD db_cats(void)
{
	return ncats;
}

static const UBYTE *cat_entry(UWORD cat)
{
	if (!db || cat >= ncats)
		return NULL;
	return db + HDR + (ULONG)cat * CATSIZE;
}

UWORD db_count(UWORD cat)
{
	const UBYTE *e = cat_entry(cat);

	return e ? rd16(e) : 0;
}

CONST_STRPTR db_name(UWORD cat)
{
	const UBYTE *e = cat_entry(cat);

	return e ? (CONST_STRPTR)(e + 6) : (CONST_STRPTR)"";
}

BOOL db_item(UWORD cat, UWORD idx, struct item *it)
{
	const UBYTE *e = cat_entry(cat);
	const UBYTE *r;
	ULONG off;

	if (!e || idx >= rd16(e))
		return FALSE;
	off = rd32(e + 2) + (ULONG)idx * 4;
	if (off + 4 > db_size)
		return FALSE;
	off = rd32(db + off);
	if (off + 4 + 6 * BASELEN > db_size)
		return FALSE;

	r = db + off;
	it->flags = r[0];
	it->snd = (CONST_STRPTR)(r + 4);
	it->more = (CONST_STRPTR)(r + 4 + BASELEN);
	it->vid = (CONST_STRPTR)(r + 4 + 2 * BASELEN);
	it->img = (CONST_STRPTR)(r + 4 + 3 * BASELEN);
	it->wk = (CONST_STRPTR)(r + 4 + 4 * BASELEN);
	it->we = (CONST_STRPTR)(r + 4 + 5 * BASELEN);
	it->klingon = (CONST_STRPTR)(r + 4 + 6 * BASELEN);
	it->english = it->klingon + r[1] + 1;
	return TRUE;
}
