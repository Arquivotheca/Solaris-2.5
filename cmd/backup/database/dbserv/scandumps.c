
#ident	"@(#)scandumps.c 1.15 93/05/12"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#define	_POSIX_SOURCE	/* hack to avoid redef of MAXNAMLEN */
#define	_POSIX_C_SOURCE
#include <dirent.h>
#undef	_POSIX_C_SOURCE
#undef	_POSIX_SOURCE
#include "rpcdefs.h"

/*
 * support for the "full restore" functionality of recover.
 * For a given host, file system and date we look up the headers
 * of all dumps required to do a full restore (in the order that
 * they should be restored)
 */

static struct dumplist *listhead, *latest, *retlist;

#ifdef __STDC__
static int mntpt_search(const char *, time_t, struct mntpts **);
static void add_mntpt(struct mntpts **, const char *);
static int header_search(const struct fsheader_readargs *);
static void savedump(struct dheader *, int, int);
static void order_list(void);
static struct dumplist *locate_dump(time_t, u_long);
static struct dheader *new_header(struct dheader *, int);
#else
static int mntpt_search();
static void add_mntpt();
static int header_search();
static void savedump();
static void order_list();
static struct dumplist *locate_dump();
static struct dheader *new_header();
#endif

/*
 * return all dump headers used for a full restore
 * of the given filesystem as of the given date.
 */
struct readdata *
read_dumps_1(p)
	struct fsheader_readargs *p;
{
	static struct readdata r;

	r.retdata = NULL;
	listhead = latest = retlist = NULL;
	if (getreadlock() == 0) {
		r.readrc = DBREAD_SERVERDOWN;
		return (&r);
	}
	r.readrc = header_search(p);
	if (r.readrc == DBREAD_SUCCESS) {
		r.retdata = (char *)retlist;
	}
	releasereadlock();
	return (&r);
}

/*
 * this routine is called only from dbfind.c.
 * Thus we can assume that we have already forked a sub-process
 * and that we have already read-locked
 * the database...
 */
struct dumplist *
#ifdef __STDC__
fheader_search(const struct fsheader_readargs *p)
#else
fheader_search(p)
	struct fsheader_readargs *p;
#endif
{
	listhead = latest = retlist = NULL;
	if (header_search(p) == DBREAD_SUCCESS) {
		return (retlist);
	}
	return (NULL);
}

/*
 * "encode" XDR.
 * This function lives here rather than in `dbserv_xdr.c' because it
 * is so closely tied to the data structures here, and because its
 * encode specific  (The routines in dbserv_xdr are generally "two-way")
 */
bool_t
xdr_headerlist(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	register struct dumplist *list, *pp, *p;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);

	/*LINTED*/
	list = (struct dumplist *)objp->retdata;

	if (list && objp->readrc == DBREAD_SUCCESS) {
		p = list;
		while (p) {
			if (!xdr_fullheader(xdrs, p->h))
				return (FALSE);
			if (!xdr_u_long(xdrs, &p->dumpid))
				return (FALSE);
			pp = p;
			p = p->nxt;
			free((char *)pp->h);
			free((char *)pp);
		}
	}

	return (TRUE);
}

/*
 * return a list of all valid mount points for the given host
 * as of the given time.  This is called by the dbfind() functionality.
 * Thus, we assume that we are running in a sub-process of dbserv, and
 * that we already hold a read lock on the database.
 */
#ifdef __STDC__
get_mntpts(const char *host,
	time_t timestamp,
	struct mntpts **mpp)
#else
get_mntpts(host, timestamp, mpp)
	char *host;
	time_t timestamp;
	struct mntpts **mpp;
#endif
{
	struct mntpts *rlist;
	int rc;

	rc = mntpt_search(host, timestamp, &rlist);
	if (rc == DBREAD_SUCCESS) {
		*mpp = rlist;
		rc = 0;
	}
	return (rc);
}

/*
 * return a list of all valid mount points for a given host at
 * a given time.  This is user-callable via RPC.
 */
struct readdata *
check_mntpt_1(p)
	struct fsheader_readargs *p;
{
	static struct readdata r;
	static struct mntpts *mntlist;

	mntlist = (struct mntpts *)NULL;
	if (getreadlock() == 0) {
		r.readrc = DBREAD_SERVERDOWN;
		return (&r);
	}

	r.readrc = mntpt_search(p->host, p->time, &mntlist);
	if (r.readrc == DBREAD_SUCCESS)
		r.retdata = (char *)mntlist;
	releasereadlock();
	return (&r);
}

/*
 * see if there are any headers for mount points at or "below" the
 * given path, as of the given time stamp.
 */
static int
#ifdef __STDC__
mntpt_search(const char *host,
	time_t timestamp,
	struct mntpts **mpp)
#else
mntpt_search(host, timestamp, mpp)
	char *host;
	time_t timestamp;
	struct mntpts **mpp;
#endif
{
	DIR *dirp;
	struct dirent *ep;
	char fullname[MAXPATHLEN];
	int fd, hnamlen;
	struct dheader dummy;
	int rc;

	*mpp = (struct mntpts *)NULL;
	if ((dirp = opendir(host)) == NULL) {
		rc = DBREAD_NOHOST;
	} else {
		rc = DBREAD_NODUMP;
		hnamlen = strlen(HEADERFILE);
		while (ep = readdir(dirp)) {
			if (strncmp(ep->d_name, HEADERFILE, hnamlen))
				continue;
			(void) sprintf(fullname, "%s/%s", host, ep->d_name);
			if ((fd = open(fullname, O_RDONLY)) == -1) {
				perror("fullname");
				continue;
			}
			if (read(fd, (char *)&dummy,
					sizeof (struct dheader)) == -1) {
				perror("read");
				(void) close(fd);
				continue;
			}
			(void) close(fd);
			if (dummy.dh_time <= timestamp) {
				rc = DBREAD_SUCCESS;
				add_mntpt(mpp, dummy.dh_mnt);
			}
		}
		(void) closedir(dirp);
	}

	return (rc);
}

static void
#ifdef __STDC__
add_mntpt(struct mntpts **mntlist,
	const char *name)
#else
add_mntpt(mntlist, name)
	struct mntpts **mntlist;
	char *name;
#endif
{
	register struct mntpts *p;
	unsigned namelen = strlen(name);

	for (p = *mntlist; p; p = p->nxt) {
		if (p->mp_namelen == namelen && strcmp(name, p->mp_name) == 0)
			return;
	}
	p = (struct mntpts *)malloc(sizeof (struct mntpts));
	if (p == (struct mntpts *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "add_mntpt");
		exit(1);
	}
	p->mp_name = malloc(namelen);
	if (p->mp_name == (char *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "add_mntpt");
		free((char *)p);
		exit(1);
	}
	p->mp_namelen = namelen;
	(void) strcpy(p->mp_name, name);
	p->nxt = *mntlist;
	*mntlist = p;
}

bool_t
xdr_mntptlist(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	register struct mntpts *list, *t, *p;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);

	/*LINTED*/
	list = (struct mntpts *)objp->retdata;

	if (list && objp->readrc == DBREAD_SUCCESS) {
		p = list;
		while (p) {
			if (!xdr_string(xdrs, &p->mp_name, MAXPATHLEN))
				return (FALSE);
			t = p;
			p = p->nxt;
			free(t->mp_name);
			free((char *)t);
		}
	}
	return (TRUE);
}

static int
#ifdef __STDC__
header_search(const struct fsheader_readargs *p)
#else
header_search(p)
	struct fsheader_readargs *p;
#endif
{
	DIR *dirp;
	struct dirent *ep;
	char fullname[1024];
	int fd, rc;
	struct dheader dummy;

	rc = DBREAD_NODUMP;
	if ((dirp = opendir(p->host)) == NULL) {
		(void) fprintf(stderr, gettext(
			"cannot open host directory for %s\n"), p->host);
		return (DBREAD_NOHOST);
	}
	while (ep = readdir(dirp)) {
		if (strncmp(ep->d_name, HEADERFILE, strlen(HEADERFILE)))
			continue;
		(void) sprintf(fullname, "%s/%s", p->host, ep->d_name);
		if ((fd = open(fullname, O_RDONLY)) == -1) {
			perror(fullname);
			continue;
		}
		if (read(fd, (char *)&dummy, sizeof (struct dheader)) == -1) {
			perror("read");
			(void) close(fd);
			continue;
		}
		if (strcmp(p->mntpt, dummy.dh_mnt) == 0) {
			if ((dummy.dh_flags & DH_PARTIAL) == 0 &&
					dummy.dh_time <= p->time) {
				char *cp;
				int dumpid;

				if (cp = strchr(ep->d_name, '.')) {
					cp++;
					if (sscanf(cp, "%d", &dumpid) == 1) {
						savedump(&dummy, fd, dumpid);
					}
				}
			}
		}
		(void) close(fd);
	}
	(void) closedir(dirp);
	order_list();
	if (retlist)
		rc = DBREAD_SUCCESS;
	return (rc);
}

static void
savedump(hp, fd, dumpid)
	struct dheader *hp;
	int fd;
	int dumpid;
{
	struct dumplist *dlp;
	struct dheader *dhp;

	dlp = (struct dumplist *)malloc(sizeof (struct dumplist));
	if (dlp == NULL) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "savedump");
		exit(1);
	}
	dhp = new_header(hp, fd);
	if (dhp == NULL) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "savedump");
		free((char *)dlp);
		exit(1);
	}

	dlp->h = dhp;
	if (latest == NULL || (dhp->dh_time > latest->h->dh_time))
		latest = dlp;

	dlp->dumpid = dumpid;
	dlp->nxt = listhead;
	listhead = dlp;
}

static void
#ifdef __STDC__
order_list(void)
#else
order_list()
#endif
{
	register struct dumplist *l, *next;

	if (latest == NULL) {
		(void) fprintf(stderr, gettext("no dumps\n"));
		return;
	}
	l = locate_dump(latest->h->dh_time, latest->h->dh_level);
	if (l == NULL)
		return;
	retlist = NULL;
	if ((l->h->dh_flags & DH_EMPTY) == 0)
		retlist = l;
	l->nxt = NULL;
	while (l->h->dh_prvdumptime) {
		next = locate_dump(l->h->dh_prvdumptime, l->h->dh_level);
		if (next == NULL) {
			(void) fprintf(stderr, gettext("%s: %s error\n"),
				"locate_dump", "next");
			return;
		}
		next->nxt = NULL;
		if ((next->h->dh_flags & DH_EMPTY) == 0) {
			next->nxt = retlist;
			retlist = next;
		}
		l = next;
	}
}

static struct dumplist *
locate_dump(date, level)
	time_t	date;
	u_long	level;
{
	register struct dumplist *l, *pl;

	for (l = listhead, pl = NULL; l; l = l->nxt) {
		if (l->h->dh_time == date) {
			if (l->h->dh_level > level) {
				(void) fprintf(stderr, gettext(
				    "%s: dump level error\n"), "locate_dump");
				return (NULL);
			}
			if (pl)
				pl->nxt = l->nxt;
			else
				listhead = l->nxt;
			return (l);
		}
		pl = l;
	}
	(void) fprintf(stderr, gettext("%s error\n"), "locate_dump");
	return (NULL);
}

static struct dheader *
new_header(candidate, fd)
	struct dheader *candidate;
	int fd;
{
	struct dheader *c1;
	int size;

	size = sizeof (struct dheader) + ((candidate->dh_ntapes-1)*LBLSIZE);
	c1 = (struct dheader *)malloc((unsigned)size);
	if (c1 == (struct dheader *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "new_header");
		return ((struct dheader *)0);
	}
	if (lseek(fd, (off_t)0, SEEK_SET) == -1) {
		perror("new_header/lseek");
		free((char *)c1);
		return ((struct dheader *)0);
	}
	if (read(fd, (char *)c1, size) != size) {
		perror("new_header/read");
		free((char *)c1);
		return ((struct dheader *)0);
	}
	return (c1);
}
