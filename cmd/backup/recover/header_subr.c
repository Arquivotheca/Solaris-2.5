#ident	"@(#)header_subr.c 1.5 91/12/20"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"

static struct holdheader {
	struct dheader *dh;
	u_long dumpid;
	struct holdheader *nxt;
} *holdheaders;

static struct holdlist {
	struct dumplist *dl;
	struct holdlist *nxt;
} *holdlist;

static struct nullist {
	char *mntpt;
	struct nullist *nxt;
} *nullist;

#ifdef __STDC__
static struct dheader *lookup_header(u_long);
static void hold_header(struct dheader *, u_long);
static struct dumplist *lookup_list(char *);
static void hold_list(struct dumplist *);
static int null_list(char *);
static void make_null(char *);
#else
static struct dheader *lookup_header();
static void hold_header();
static struct dumplist *lookup_list();
static void hold_list();
static int null_list();
static void make_null();
#endif

header_get(dbserv, host, dumpid, header)
	char *dbserv;
	char *host;
	u_long dumpid;
	struct dheader **header;
{
	struct dheader *p;
	int rc;

	if (unknown_dump(dumpid)) {
		return (-1);
	}

	if (p = lookup_header(dumpid)) {
		*header = p;
		return (0);
	}
	if (rc = header_read(dbserv, host, dumpid, header))
		return (rc);

	hold_header(*header, dumpid);
	return (0);
}

listget(dbserv, host, mntpt, timestamp, l)
	char *dbserv;
	char *host;
	char *mntpt;
	time_t timestamp;
	struct dumplist **l;
{
	struct dumplist *dl;

	if (dl = lookup_list(mntpt)) {
		*l = dl;
		return (0);
	} else if (null_list(mntpt)) {
		return (1);
	}

	if (read_dumps(dbserv, host, mntpt, timestamp, &dl)) {
		make_null(mntpt);
		return (1);
	}

	hold_list(dl);
	*l = dl;
	return (0);
}

/*
 * lookup the header info for the given dumpid in our local cache
 */
static struct dheader *
lookup_header(dumpid)
	u_long dumpid;
{
	register struct holdheader *p;

	for (p = holdheaders; p; p = p->nxt) {
		if (p->dumpid == dumpid)
			return (p->dh);
	}
	return ((struct dheader *)0);
}

/*
 * un-cache a header for a dump which has been deleted.
 */
void
uncache_header(dumpid)
	u_long dumpid;
{
	register struct holdheader *p, *lastp;
	register struct holdlist *l, *lastl;

	lastp = (struct holdheader *)0;
	for (p = holdheaders; p; p = p->nxt) {
		if (p->dumpid == dumpid) {
			if (lastp) {
				lastp->nxt = p->nxt;
			} else {
				holdheaders = p->nxt;
			}
			free((char *)p->dh);
			free((char *)p);
			break;
		}
		lastp = p;
	}

	lastl = NULL;
	for (l = holdlist; l; l = l->nxt) {
		if (l->dl->dumpid == dumpid) {
			if (lastl)
				lastl->nxt = l->nxt;
			else
				holdlist = l->nxt;
			free_dumplist(l->dl);
			free((char *)l);
			break;
		}
		lastl = l;
	}
}

void
#ifdef __STDC__
purge_dumplists(void)
#else
purge_dumplists()
#endif
{
	register struct holdlist *l, *ll;
	register struct nullist *n, *nn;

	l = holdlist;
	while (l) {
		ll = l;
		l = l->nxt;
		free_dumplist(ll->dl);
		free((char *)ll);
	}
	holdlist = NULL;

	n = nullist;
	while (n) {
		nn = n;
		n = n->nxt;
		free(nn->mntpt);
		free((char *)nn);
	}
	nullist = NULL;
}

/*
 * save header info that we read from the database in a local cache
 */
static void
hold_header(p, dumpid)
	struct dheader *p;
	u_long dumpid;
{
	struct holdheader *h;

	if (lookup_header(dumpid))
		return;

	h = (struct holdheader *)malloc(sizeof (struct holdheader));
	if (h) {
		h->dumpid = dumpid;
		h->dh = p;
		h->nxt = holdheaders;
		holdheaders = h;
	}
}

static struct dumplist *
lookup_list(name)
	char *name;
{
	register struct holdlist *h;

	for (h = holdlist; h; h = h->nxt) {
		if (strcmp(h->dl->h->dh_mnt, name) == 0)
			return (h->dl);
	}
	return (NULL);
}

static void
hold_list(l)
	struct dumplist *l;
{
	struct holdlist *hl;

	if (lookup_list(l->h->dh_mnt))
		return;

	hl = (struct holdlist *)malloc(sizeof (struct holdlist));
	if (hl) {
		hl->dl = l;
		hl->nxt = holdlist;
		holdlist = hl;
	}
}

static int
null_list(mntpt)
	char *mntpt;
{
	register struct nullist *p;

	for (p = nullist; p; p = p->nxt) {
		if (strcmp(p->mntpt, mntpt) == 0)
			return (1);
	}
	return (0);
}

static void
make_null(mntpt)
	char *mntpt;
{
	register struct nullist *p;

	p = (struct nullist *)malloc(sizeof (struct nullist));
	if (p) {
		p->mntpt = (char *)malloc((unsigned)(strlen(mntpt)+1));
		if (p->mntpt) {
			(void) strcpy(p->mntpt, mntpt);
			p->nxt = nullist;
			nullist = p;
		} else {
			free((char *)p);
		}
	}
}
