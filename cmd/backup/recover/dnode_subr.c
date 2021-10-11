#ident	"@(#)dnode_subr.c 1.7 92/03/11"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include "recover.h"
#include "cache.h"

#define	BLOCKSIZE	DNODE_READBLKSIZE

int readaheadhits, dnodemisses;

#define	DNODE_CACHESIZE		1000
#define	NUM_DNODE_CACHES	10
#define	HOSTSIZE		(2*BCHOSTNAMELEN)	/* name + internet id */
static struct {
	struct dnode	d[BLOCKSIZE];
} dcache[NUM_DNODE_CACHES][DNODE_CACHESIZE];
static int oldcache;
static struct cacheid {
	char	host[HOSTSIZE];
	u_long	fileid;
	int	emptycache;
	struct cache_header *h;
} cache_id[NUM_DNODE_CACHES];

#define	NULL_CACHEID	((struct cacheid *)0)

#ifdef __STDC__
static struct cacheid *getcache(char *, u_long);
static void makeunknown(u_long);
#else
static struct cacheid *getcache();
static void makeunknown();
#endif

struct dnode *
dnode_get(dbserv, hostname, fileid, num)
	char *dbserv;
	char *hostname;
	u_long fileid;
	u_long num;
{
	struct cacheid *cid;
	struct cache_header *h;
	struct cache_block *cbp;
	register int i, bound;

	if (unknown_dump(fileid))
		return (NULL_DNODE);

	if ((cid = getcache(hostname, fileid)) == NULL_CACHEID)
		return (NULL_DNODE);

	h = cid->h;

	if ((cbp = cache_getblock(h, num)) != NULL_CACHE_BLOCK)
		/*LINTED [alignment ok]*/
		return ((struct dnode *)cbp->data);

	bound = (int)num - BLOCKSIZE;
	if (bound < 0)
		bound = -1;

	for (i = num-1; i > bound; i--) {
		if ((cbp = cache_getblock(h, (u_long)i)) != NULL_CACHE_BLOCK) {
			struct dnode *d;

			/*LINTED [alignment ok]*/
			d = (struct dnode *)cbp->data;
			d += (num - i);
			readaheadhits++;
			return (d);
		}
	}

	dnodemisses++;
	if ((cbp = cache_alloc_block(h, num)) == NULL_CACHE_BLOCK)
		return (NULL_DNODE);
	if (dnode_blockread(dbserv, hostname, fileid, num, BLOCKSIZE,
			/*LINTED [alignment ok]*/
			(struct dnode *)cbp->data) != 0) {
		cache_release(h, cbp);
		return (NULL_DNODE);
	}
	cid->emptycache = 0;
	cbp->flags |= CACHE_ENT_VALID;
	/*LINTED [alignment ok]*/
	return ((struct dnode *)cbp->data);
}

static struct cacheid *
getcache(host, file)
	char *host;
	u_long	file;
{
	int i, myidx;
	struct cache_header *h;

	for (i = 0; i < NUM_DNODE_CACHES; i++) {
		if (strcmp(cache_id[i].host, host) == 0 &&
				cache_id[i].fileid == file)
			return (&cache_id[i]);
	}

	myidx = oldcache;
	h = cache_init((caddr_t)dcache[myidx], DNODE_CACHESIZE,
			BLOCKSIZE*sizeof (struct dnode), cache_id[myidx].h, 0);
	if (h == NULL_CACHE_HEADER)
		return (NULL_CACHEID);
	if (++oldcache >= NUM_DNODE_CACHES)
		oldcache = 0;
	(void) strcpy(cache_id[myidx].host, host);
	cache_id[myidx].fileid = file;
	cache_id[myidx].h = h;
	cache_id[myidx].emptycache = 1;
	return (&cache_id[myidx]);
}

void
#ifdef __STDC__
dnode_initcache(void)
#else
dnode_initcache()
#endif
{
	register int i;

	for (i = 0; i < NUM_DNODE_CACHES; i++) {
		if (cache_id[i].h) {
			cache_id[i].h = cache_init((caddr_t)dcache[i],
				DNODE_CACHESIZE,
				BLOCKSIZE*sizeof (struct dnode),
				cache_id[i].h, 0);
			cache_id[i].emptycache = 1;
		}
	}
	oldcache = 0;
}

dnode_flushcache(host, dumpid)
	char *host;
	u_long dumpid;
{
	register int i;

	makeunknown(dumpid);
	for (i = 0; i < NUM_DNODE_CACHES; i++) {
		if (strcmp(cache_id[i].host, host) == 0 &&
				cache_id[i].fileid == dumpid) {
			cache_id[i].host[0] = '\0';
			cache_id[i].fileid = 0;
			if (cache_id[i].emptycache)
				return (0);
			cache_id[i].emptycache = 1;
			(void) cache_init((caddr_t)dcache[i], DNODE_CACHESIZE,
					BLOCKSIZE*sizeof (struct dnode),
					cache_id[i].h, 0);
			return (1);
		}
	}
	return (0);
}

static struct und {
	u_long dumpid;
	struct und *nxt;
} *unds;

unknown_dump(dumpid)
	u_long dumpid;
{
	register struct und *p;

	for (p = unds; p; p = p->nxt)
		if (p->dumpid == dumpid)
			return (1);

	return (0);
}

static void
makeunknown(dumpid)
	u_long dumpid;
{
	struct und *p;

	if (unknown_dump(dumpid))
		return;
	p = (struct und *)malloc(sizeof (struct und));
	if (p) {
		p->dumpid = dumpid;
		p->nxt = unds;
		unds = p;
	}
}
