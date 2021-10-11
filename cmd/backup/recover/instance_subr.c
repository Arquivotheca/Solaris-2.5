#ident	"@(#)instance_subr.c 1.8 91/12/20"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"
#include <sys/stat.h>
#include "cache.h"

static struct cache_header *icache;
static char ihost[256];
static char idbserv[256];

instance_ropen(dbserv, host)
	char *dbserv;
	char *host;
{
	struct instance_record dummy;

	(void) strcpy(ihost, host);
	(void) strcpy(idbserv, dbserv);
	/*
	 * determine the record size for this host's instance file
	 */
	if (instance_read(idbserv, ihost, INSTANCE_FREEREC,
			sizeof (struct instance_record), &dummy) == -1) {
		return (-1);
	}
	instance_recsize = dummy.i_entry[0].ie_dnode_index;
	entries_perrec = dummy.i_entry[0].ie_dumpid;
	if (entries_perrec <= 0 || entries_perrec > 100) {
		(void) fprintf(stderr, gettext(
			"instance_recsize is %d!\n"), instance_recsize);
		return (-1);
	}
	return (instance_initcache());
}

void
#ifdef __STDC__
instance_rclose(void)
#else
instance_rclose()
#endif
{
	*ihost = *idbserv = '\0';
	instance_recsize = 0;
	entries_perrec = 0;
}

struct instance_record *
instance_getrec(recnum)
	u_long recnum;
{
	struct cache_block *cbp;
	struct instance_record *irp;

	if (recnum == NONEXISTENT_BLOCK || instance_recsize == 0) {
		irp = NULL_IREC;
	} else if ((cbp = cache_getblock(icache,
				recnum)) != NULL_CACHE_BLOCK) {
		/*LINTED [alignment ok]*/
		irp = (struct instance_record *)cbp->data;
	} else if ((cbp = cache_alloc_block(icache,
				recnum)) == NULL_CACHE_BLOCK) {
		irp = NULL_IREC;
	} else if (instance_read(idbserv, ihost, recnum, instance_recsize,
			/*LINTED [alignment ok]*/
			(struct instance_record *)cbp->data) == -1) {
		irp = NULL_IREC;
		cache_release(icache, cbp);
	} else {
		cbp->flags |= CACHE_ENT_VALID;
		/*LINTED [alignment ok]*/
		irp = (struct instance_record *)cbp->data;
	}
	return (irp);
}

#define	ICACHE_SIZE	1000
static struct instance_record	*icrecs;

#ifdef __STDC__
instance_initcache(void)
#else
instance_initcache()
#endif
{
	static int cache_recsize;

	if (cache_recsize != instance_recsize) {
		if (icrecs)
			free((char *)icrecs);
		icrecs = (struct instance_record *)
			malloc((unsigned)(ICACHE_SIZE*instance_recsize));
		if (icrecs == NULL) {
			(void) fprintf(stderr, gettext(
				"Cannot allocate instance cache\n"));
			return (-1);
		}
		cache_recsize = instance_recsize;
		if (icache) {
			free_cache(icache);
			icache = NULL;
		}
	}
	if ((icache = cache_init((caddr_t)icrecs, ICACHE_SIZE,
			instance_recsize, icache, 0)) == NULL_CACHE_HEADER) {
		(void) fprintf(stderr,
			gettext("Cannot initialize instance cache!\n"));
		return (-1);
	}
	return (0);
}
