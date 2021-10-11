
#ident	"@(#)getdnode.c 1.16 93/04/28"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include "cmds.h"

static int opaque_mode = 1;

#define	GROW	100
static int alloced_dumps;
static int ndumps;
static struct dumps {
	u_long 	dumpid;
	u_long	dnode_offset;
	time_t	dumptime;
};
static struct dumps *dumps;

#ifdef __STDC__
static int timecompare(struct dumps *, struct dumps *);
static int do_opaque(char *, struct dnode *, struct dir_entry *, int, time_t);
static int do_translucent(char *, struct dnode *, struct dir_entry *,
	int, time_t);
static int changed_perms(int, int, char *);
static int ismntpt(char *, char *, time_t);
#else
static int timecompare();
static int do_opaque();
static int do_translucent();
static int changed_perms();
static int ismntpt();
#endif

int
#ifdef __STDC__
getopaquemode(void)
#else
getopaquemode()
#endif
{
	return (opaque_mode);
}

void
set_lookupmode(arg)
	char *arg;
{
	static char *t = "translucent";
	static int tlen = 11;   /* strlen(t) */
	static char *o = "opaque";
	static int olen = 6;	/* strlen(o) */
	register int len;

	if (arg == NULL) {
		(void) printf(gettext("lookup mode is `%s'\n"), o);
		opaque_mode = 1;
		return;
	}

	len = strlen(arg);
	if (arg && len <= tlen && strncmp(arg, t, len) == 0) {
		(void) printf(gettext("lookup mode is `%s'\n"), t);
		opaque_mode = 0;
	} else if (arg && len <= olen && strncmp(arg, o, len) == 0) {
		(void) printf(gettext("lookup mode is `%s'\n"), o);
		opaque_mode = 1;
	} else {
		(void) printf(gettext(
		    "invalid lookup mode specification -- must be %s or %s\n"),
		    o, t);
	}
}

static int
timecompare(a, b)
	struct dumps *a, *b;
{
	return (a->dumptime - b->dumptime);
}

getdnode(host, dn, ep, mode, timestamp, type, path)
	char *host;
	struct dnode *dn;
	struct dir_entry *ep;
	int mode;
	time_t timestamp;
	int type;
	char *path;
{
	static struct dnode dummy;
	u_long irec, firstirec;
	struct instance_record *ir;
	struct dheader *h;
	register int i;
	int rc;

	ndumps = 0;
	irec = firstirec = ep->de_instances;
	do {
		if (irec == NONEXISTENT_BLOCK)
			break;
		if ((ir = instance_getrec(irec)) == NULL_IREC)
			break;
		for (i = 0; i < entries_perrec; i++) {
			if (ir->i_entry[i].ie_dumpid == 0)
				continue;
			if (header_get(dbserv, host,
					ir->i_entry[i].ie_dumpid, &h))
				continue;
			if (ndumps >= alloced_dumps) {
				alloced_dumps += GROW;
				if (dumps == (struct dumps *)0)
					dumps = (struct dumps *) malloc(
						sizeof (struct dumps) *
						alloced_dumps);
				else
					dumps = (struct dumps *) realloc(
						(void *)dumps,
						sizeof (struct dumps) *
						alloced_dumps);
				if (dumps == (struct dumps *)0) {
					fprintf(stderr, gettext(
					    "Too many dumps: %d\n"), ndumps);
					panic(gettext("Out of memory."));
				}
			}
			dumps[ndumps].dumpid = ir->i_entry[i].ie_dumpid;
			dumps[ndumps].dnode_offset =
				ir->i_entry[i].ie_dnode_index;
			dumps[ndumps].dumptime = h->dh_time;
			ndumps++;
		}
		irec = ir->ir_next;
	} while (irec != firstirec);

	if (ndumps == 0) {
		if (ep->de_directory != NONEXISTENT_BLOCK) {
			rc = 1;
		} else {
			rc = 0;
		}
	} else {
#ifdef __STDC__
		qsort((char *)dumps, ndumps, sizeof (struct dumps),
			(int (*)(const void *, const void *)) timecompare);
#else
		qsort((char *)dumps, ndumps, sizeof (struct dumps),
			(int (*)())timecompare);
#endif
		switch (type) {
		case LOOKUP_DEFAULT:
			if (opaque_mode)
				rc = do_opaque(host, dn, ep, mode, timestamp);
			else
				rc = do_translucent(host, dn, ep,
					mode, timestamp);
			break;
		case LOOKUP_TRANSLUCENT:
			rc = do_translucent(host, dn, ep, mode, timestamp);
			break;
		case LOOKUP_OPAQUE:
			rc = do_opaque(host, dn, ep, mode, timestamp);
			break;
		default:
			(void) fprintf(stderr,
				gettext("%s: bad type %d\n"), "getdnode", type);
			return (0);
		}
	}

	if (rc == 1) {
		char rpath[MAXPATHLEN];

		/*
		 * A directory that we couldn't find a dnode for.  See
		 * if we should return a fake dnode for it or not.
		 */
		if (path && *path) {
			(void) strcpy(rpath, path);
			smashpath(rpath);
			if (ismntpt(host, rpath, timestamp)) {
				dummy.dn_mode = S_IFDIR|S_IRWXU|S_IRWXG|S_IRWXO;
				bcopy((char *)&dummy, (char *)dn,
					sizeof (struct dnode));
			} else {
				rc = 0;
			}
		} else {
			rc = 0;
		}
	}
	return (rc);
}

/*
 * make the view of the filesystem `opaque' at the most recent
 * level 0 dump -- files which existed previously, but had been
 * deleted as of the previous level 0 are not visible without
 * doing `setdate'.
 */
static int
do_opaque(host, dn, ep, mode, timestamp)
	char *host;
	struct dnode *dn;
	struct dir_entry *ep;
	int mode;
	time_t	timestamp;
{
	int rc = 0;
	struct dnode *dp;
	register int i, j, dumpidx;
	struct dheader *h;
	char mntpt[MAXPATHLEN];
	int mntlen, newlen;
	struct dumplist *l, *lp;
	u_long mydump, offset;
	u_long *dlist = (u_long *) NULL;
	int listcnt;

	mntpt[0] = '\0';
	mntlen = 0;
	for (i = 0; i < ndumps; i++) {
		/*
		 * In opaque mode, we always favor a mount point over
		 * the underlying directory when choosing a dnode.
		 * Mount points are determined by checking the length
		 * of the `dh_mnt' field in the dump header (assuming
		 * that a mount point name must always be longer than
		 * the name for a dump of the filesystem that contains
		 * the mounted-on directory).
		 *
		 * We go ahead and get all the headers even though
		 * `getdnode()' already got them -- they're cached
		 * so it will be cheap.
		 */
		if (header_get(dbserv, host, dumps[i].dumpid, &h))
				continue;
		if ((newlen = strlen(h->dh_mnt)) > mntlen) {
			(void) strcpy(mntpt, h->dh_mnt);
			mntlen = newlen;
		}
	}

	if (mntlen == 0)
		goto done;

	/*
	 * now that we know the mount point in question, retrieve the
	 * list of dumps from the current date setting back to the first
	 * previous level 0 dump.
	 */
	if (listget(dbserv, host, mntpt, timestamp, &l))
		goto done;

	listcnt = 0;
	lp = l;
	while (lp) {
		listcnt++;
		lp = lp->nxt;
	}
	if (listcnt == 0)
		goto done;

	/* now allocate the proper amount of memory */
	dlist = (u_long *) malloc(sizeof (struct dumps) * listcnt);
	if (dlist == (u_long *)0) {
		(void) fprintf(stderr, gettext("Too many dumps: %d\n"),
			listcnt);
		panic(gettext("Out of memory."));
	}
	listcnt = 0;
	while (l) {
		dlist[listcnt++] = l->dumpid;
		l = l->nxt;
	}

	/*
	 * choose the dnode from the latest dump in the list
	 */
	mydump = 0;
	for (i = listcnt-1; i >= 0; i--) {
		for (j = 0; j < ndumps; j++) {
			if (dumps[j].dumpid == dlist[i]) {
				dumpidx = j;
				mydump = dumps[j].dumpid;
				offset = dumps[j].dnode_offset;
				break;
			}
		}
		if (mydump)
			break;
	}

	if (mydump == 0)
		goto done;

	rc = mydump;
	dp = dnode_get(dbserv, host, mydump, offset);
	if (dp == NULL_DNODE) {
		free(dlist);
		return (0);
	}

	*dn = *dp;

	if (permchk(dp, mode, host)) {
		free(dlist);
		return (0);
	}

	if (dumpidx != (ndumps-1)) {
		if (changed_perms(dumpidx, mode, host)) {
			free(dlist);
			return (0);
		}
	}

done:
#if 1
	/*
	 * XXX: we're trying to provide access to files whose
	 * containing directories have not been dumped.
	 * I think the only time this can happen is when there is
	 * a mount point somewhere below this non-dumped directory...
	 */
	if (rc == 0 && ep->de_directory != NONEXISTENT_BLOCK) {
		rc = 1;
	}
#endif
	if (dlist)
		free(dlist);
	return (rc);
}

/*
 * provide a `translucent' view of dumped files -- the
 * most recent instance prior to the current date setting will be
 * retrieved, regardless of what dump contains it.
 */
static int
do_translucent(host, dn, ep, mode, timestamp)
	char *host;
	struct dnode *dn;
	struct dir_entry *ep;
	int mode;
	time_t timestamp;
{
	struct dnode *dp;
	int rc;
	register int i;
	int dumpidx;

	rc = 0;
	dumpidx = -1;
	for (i = 0; i < ndumps; i++) {
		if (dumps[i].dumptime > timestamp)
			break;
		dumpidx = i;
	}
	if (dumpidx == -1)
		goto done;
	rc = dumps[dumpidx].dumpid;
	dp = dnode_get(dbserv, host, dumps[dumpidx].dumpid,
			dumps[dumpidx].dnode_offset);
	if (dp == NULL_DNODE)
		goto done;

	*dn = *dp;

	if (permchk(dp, mode, host))
		return (0);

	if (dumpidx != (ndumps-1)) {
		if (changed_perms(dumpidx, mode, host))
			return (0);
	}

done:
#if 1
	/*
	 * XXX: we're trying to provide access to files whose
	 * containing directories have not been dumped.
	 * I think the only time this can happen is when there is
	 * a mount point somewhere below this non-dumped directory...
	 */
	if (rc == 0 && ep->de_directory != NONEXISTENT_BLOCK) {
		rc = 1;
	}
#endif
	return (rc);

}

/*
 * we claim that restrictive permissions on a new version of a file
 * will apply to older versions as well, so here we check to make sure
 * that the caller can access versions newer than the one he has
 * chosen...
 */
static int
changed_perms(idx, mode, host)
	int idx;
	int mode;
	char *host;
{
	register int i;
	struct dnode *dp;

	for (i = idx; i < ndumps; i++) {
		dp = dnode_get(dbserv, host,
			dumps[i].dumpid, dumps[i].dnode_offset);
		if (dp) {
			if (permchk(dp, mode, host))
				return (1);
		}
	}
	return (0);
}


static struct mntpts *mntpts;
static int maxmntlen;

static int
ismntpt(host, path, timestamp)
	char *host;
	char *path;
	time_t timestamp;
{
	register struct mntpts *p;
	register int len = strlen(path);
	int rc, longest;

	if (*path == '/' && *(path+1) == '\0')
		/*
		 * root is always a mount point (?)
		 */
		return (1);

	if (mntpts == (struct mntpts *)-1) {
		return (0);
	} else if (mntpts == (struct mntpts *)0) {
		rc = db_getmntpts(dbserv, host, path, timestamp, &mntpts);
		if (rc == -1 || mntpts == (struct mntpts *)NULL) {
			mntpts = (struct mntpts *)-1;
			return (0);
		}
	}

	if (maxmntlen && len > maxmntlen)
		return (0);

	longest = 0;
	for (p = mntpts; p; p = p->nxt) {
		if (longest < p->mp_namelen)
			longest = p->mp_namelen;
		if (len <= p->mp_namelen &&
				p->mp_name[len] == '/' &&
				strncmp(path, p->mp_name, len) == 0)
			return (1);
	}
	maxmntlen = longest;
	return (0);
}

void
#ifdef __STDC__
flush_mntpts(void)
#else
flush_mntpts()
#endif
{
	if (mntpts != (struct mntpts *)-1)
		free_mntpts(mntpts);
	mntpts = (struct mntpts *)NULL;
	maxmntlen = 0;
}
