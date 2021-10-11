#ident	"@(#)dir_subr.c 1.7 92/03/25"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"
#include <assert.h>
#include <sys/stat.h>
#include "cache.h"

static struct cache_header *dircache;
static char dirhost[256];
static char dirdbserv[256];

int
dir_ropen(dbserv, host)
	char *dbserv;
	char *host;
{
	(void) strcpy(dirdbserv, dbserv);
	(void) strcpy(dirhost, host);
	dir_initcache();
	return (0);
}

void
#ifdef __STDC__
dir_rclose(void)
#else
dir_rclose()
#endif
{
	return;
}

/*
 * locate the block which contains the entry for 'path' where
 * path is a fully qualified pathname.
 */
struct dir_entry *
dir_path_lookup(blknum, bp, path)
	u_long *blknum;
	struct dir_block **bp;
	char *path;
{
#define	MAXPATH	255 /* XXX */
	char *morepath = path;
	char comp[MAXPATH];
	int rc;
	struct dir_entry *ep;
	u_long thisblk;

	if (strcmp(path, "/") == 0)
		morepath = ".";
	thisblk = DIR_ROOTBLK;
	ep = NULL_DIRENTRY;
	while ((rc = getpathcomponent(&morepath, comp)) == 1) {
		if (thisblk == NONEXISTENT_BLOCK)
			break;
		/* see if component exists in the current block */
		if ((ep = dir_name_lookup(&thisblk, bp, comp)) == NULL_DIRENTRY)
			break;
		*blknum = thisblk;
		thisblk = ep->de_directory;
	}
	if (rc != 0)
		ep = NULL_DIRENTRY;

	return (ep);
}

int
getpathcomponent(pp, comp)
	register char **pp;
	register char *comp;
{
	if (**pp == 0)
		return (0);
	/* skip leading slashes */
	while (**pp == '/')
		(*pp)++;
	while (**pp && (**pp != '/')) {
		*comp++ = **pp;
		(*pp)++;
	}
	/* and skip trailing slashes */
	while (**pp == '/')
		(*pp)++;
	*comp = 0;
	return (1);
}

/*
 * look up the given name in the given directory block
 */
struct dir_entry *
dir_name_lookup(blknum, bp, name)
	u_long *blknum;
	struct dir_block **bp;
	char *name;
{
	struct dir_entry *ep;
	u_long startblock;

	if ((*bp = dir_getblock(*blknum)) == NULL_DIRBLK)
		return (NULL_DIRENTRY);

	if ((*bp)->db_spaceavail == DIRBLOCK_DATASIZE)
		/* empty block? */
		return (NULL_DIRENTRY);

	startblock = *blknum;
	do {
		/*LINTED [alignment ok]*/
		ep = (struct dir_entry *)(*bp)->db_data;
		/*LINTED [alignment ok]*/
		while (ep != DE_END(*bp)) {
			register char *s, *t;

			/*
			 * inline version of
			 * if (strcmp(ep->de_name, name) == 0)
			 *	return (ep);
			 */
			for (s = ep->de_name, t = name;
					*s == *t && *s && *t; s++, t++)
				;
			if (*s == '\0' && *t == '\0')
				return (ep);
			/* end inline */
			ep = DE_NEXT(ep);
		}
		*blknum = (*bp)->db_next;
		if (*blknum != startblock)
			*bp = dir_getblock(*blknum);
	} while (*bp && *blknum != startblock);

	return (NULL_DIRENTRY);
}

#define	DIR_CACHESIZE	1000
static struct dir_block	dir_cache_blocks[DIR_CACHESIZE];

void
#ifdef __STDC__
dir_initcache(void)
#else
dir_initcache()
#endif
{
	if ((dircache = cache_init((caddr_t)dir_cache_blocks,
			DIR_CACHESIZE, sizeof (struct dir_block),
			dircache, 0)) == NULL_CACHE_HEADER) {
		(void) fprintf(stderr,
			gettext("Cannot initialize dir cache!\n"));
		exit(1);
	}
}

/*
 * retrieve the specified block
 */
struct dir_block *
dir_getblock(blknum)
	u_long blknum;
{
	struct cache_block *cbp;
	struct dir_block *dbp;

	if ((cbp = cache_getblock(dircache, blknum)) != NULL_CACHE_BLOCK) {
		/*LINTED [alignment ok]*/
		dbp = (struct dir_block *)cbp->data;
	} else {
		if ((cbp = cache_alloc_block(dircache,
					blknum)) == NULL_CACHE_BLOCK) {
			dbp = NULL_DIRBLK;
		} else {
			if (dir_read(dirdbserv, dirhost, blknum,
					/*LINTED [alignment ok]*/
					(struct dir_block *)cbp->data) == -1) {
				cache_release(dircache, cbp);
				dbp = NULL_DIRBLK;
			} else {
				cbp->flags |= CACHE_ENT_VALID;
				/*LINTED [alignment ok]*/
				dbp = (struct dir_block *)cbp->data;
			}
		}
	}
	return (dbp);
}
