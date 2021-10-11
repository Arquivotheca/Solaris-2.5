
#ident	"@(#)dbfind.c 1.14 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <sys/stat.h>
#include <sys/vnode.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <setjmp.h>
#include "defs.h"
#include "rpcdefs.h"

/*
 * support for performing a `find' operation at the database server.
 * Note that the original philosophy of the server was to just return
 * records and let the client sort through them.  In the case of find,
 * this proved to be a particularly poor performer due to the number
 * of RPC's and XDR operations required to traverse the entire directory
 * hierarchy.  So, we do it here on the server and send back the
 * results.
 *
 * XXX: need a good way of sharing common code with recover.
 * All of the getdnode(), dump {header, list} stuff, and dir_{path,name}lookup
 * are mostly duplicates of what's in the recover program.
 */

static struct dumps {
	u_long 	dumpid;
	u_long	dnode_offset;
	time_t	dumptime;
};

#define	LOOKUP_DEFAULT		0
#define	LOOKUP_OPAQUE		1
#define	LOOKUP_TRANSLUCENT	2

static int opaque_mode;

#ifdef __STDC__
static void make_null(const char *);
static int null_list(const char *);
static void makeunknown(u_long);
static int unknown_dump(u_long);
static void hold_list(struct dumplist *);
static struct dumplist *lookup_list(const char *);
static void hold_header(struct dheader *p, u_long);
static struct dheader *lookup_header(u_long);
static int listget(const char *, const char *, time_t, struct dumplist **);
static int header_read(const char *, u_long, struct dheader **);
static int header_get(const char *, u_long, struct dheader **);
static struct dir_entry *dir_name_lookup(u_long, const char *);
static int getpathcomponent(char **, char *);
static struct dir_entry *dir_path_lookup(char *);
static int setcurdir(char *);
static FILE *getdnodefp(const char *, u_long);
static struct dnode *dnode_get(const char *, u_long, u_long);
static struct instance_record *instrec_get(u_long);
static int getdirblock(u_long, struct dir_block *);
static void closeall(void);
static int openall(const char *);
static int gmatch(char *, const char *);
static void setperms(struct db_findargs *);
static int permchk(struct dnode *, int m);
static void smashpath(char *);
static int ismntpt(const char *, const char *, time_t);
static int changed_perms(int, int, const char *);
static int do_translucent(const char *, struct dnode *, struct dir_entry *,
	int, time_t);
static int do_opaque(const char *, struct dnode *, struct dir_entry *,
	int, time_t);
static int getdnode(const char *, struct dnode *, struct dir_entry *,
	int, time_t, int, const char *);
static int timecompare(struct dumps *, struct dumps *);
static bool_t descend(XDR *, const char *, const char *,
	u_long, time_t, const char *);
#else
static void make_null();
static int null_list();
static void makeunknown();
static int unknown_dump();
static void hold_list();
static struct dumplist *lookup_list();
static void hold_header();
static struct dheader *lookup_header();
static int listget();
static int header_read();
static int header_get();
static struct dir_entry *dir_name_lookup();
static int getpathcomponent();
static struct dir_entry *dir_path_lookup();
static int setcurdir();
static FILE *getdnodefp();
static struct dnode *dnode_get();
static struct instance_record *instrec_get();
static int getdirblock();
static void closeall();
static int openall();
static int gmatch();
static void setperms();
static int permchk();
static void smashpath();
static int ismntpt();
static int changed_perms();
static int do_translucent();
static int do_opaque();
static int getdnode();
static int timecompare();
static bool_t descend();
#endif

/*
 * the `fast-find' command.  Like "find . -name arg -print".
 */
bool_t
xdr_fastfind(xdrs, p)
	XDR *xdrs;
	struct db_findargs *p;
{
	int rc;
	u_long blk;

	if (getreadlock() == 0) {
		return (xdr_unavailable(xdrs));		/* XXX */
	}
	setperms(p);

	rc = TRUE;
	opaque_mode = p->opaque_mode;
	if (openall(p->host) == 0) {
		blk = setcurdir(p->curdir);
		if (blk != NONEXISTENT_BLOCK) {
			rc = descend(xdrs, p->host, p->arg,
					blk, p->timestamp, p->curdir);
		}
		closeall();
	}
	releasereadlock();
	return (rc);
}

static bool_t
#ifdef __STDC__
descend(XDR *xdrs,
	const char *host,
	const char *arg,
	u_long blknum,
	time_t timestamp,
	const char *path)
#else
descend(xdrs, host, arg, blknum, timestamp, path)
	XDR *xdrs;
	char *host;
	char *arg;
	u_long blknum;
	time_t timestamp;
	char *path;
#endif
{
	u_long startblk, thisblk;
	struct dir_block holdblock;
	struct dir_entry *ep;
	struct dnode dn;
	char fullpath[MAXPATHLEN+1], retbuf[MAXPATHLEN+2], *rp;
	int inroot = 0;
	int size, rc;
	u_long dumpid;
	int pathlen, namelen;

	if (path[0] == '/' && path[1] == '\0')
		inroot = 1;

	pathlen = strlen(path);
	startblk = thisblk = blknum;
	do {
		if (getdirblock(thisblk, &holdblock))
			break;
		/*LINTED*/
		ep = (struct dir_entry *)holdblock.db_data;
		/*LINTED*/
		for (; ep != DE_END(&holdblock); ep = DE_NEXT(ep)) {
			if ((ep->de_name[0] == '.' && ep->de_name[1] == '\0') ||
					(ep->de_name[0] == '.' &&
					ep->de_name[1] == '.' &&
					ep->de_name[2] == '\0')) {
				continue;
			}
			if ((dumpid = getdnode(host, &dn, ep, VREAD, timestamp,
					LOOKUP_DEFAULT, path)) == 0) {
				continue;
			}

			namelen = pathlen + ep->de_name_len + 1;
			if (gmatch(ep->de_name, arg) && dumpid != 1) {
				if (inroot) {
					(void) sprintf(retbuf,
						"/%s\n", ep->de_name);
				} else {
					if (namelen > MAXPATHLEN) {
						(void) strcpy(retbuf, gettext(
							"name too long\n"));
					} else {
						(void) sprintf(retbuf,
							"%s/%s\n",
							path, ep->de_name);
					}
				}
				rp = retbuf;
				size = strlen(retbuf)+1;
				if (!xdr_bytes(xdrs, &rp, (u_int *)&size,
							MAXPATHLEN))
					return (FALSE);
			}

			if (namelen > MAXPATHLEN)
				continue;

			if (ep->de_directory != NONEXISTENT_BLOCK &&
					S_ISDIR(dn.dn_mode) &&
					permchk(&dn, VEXEC) == 0) {
				if (inroot)
					(void) sprintf(fullpath,
							"/%s", ep->de_name);
				else
					(void) sprintf(fullpath, "%s/%s",
							path, ep->de_name);
				rc = descend(xdrs, host, arg, ep->de_directory,
						timestamp, fullpath);
				if (rc == FALSE)
					return (rc);
			}
		}
		thisblk = holdblock.db_next;
	} while (thisblk != startblk);
	return (TRUE);
}

/*
 * XXX
 * all of this code needs to be shared with `getdnode.c' in the
 * recover program
 * XXX
 */
#define	GETDNODE
#ifdef GETDNODE
/*
 * We explode if a given file has more than MAXDUMPS instances,
 * or if there are more than MAXDUMPS dumps in a dumplist.
 */
#define	MAXDUMPS	500
static int ndumps;
static struct dumps dumps[MAXDUMPS];

static int
timecompare(a, b)
	struct dumps *a, *b;
{
	return (a->dumptime - b->dumptime);
}

static int
#ifdef __STDC__
getdnode(const char *host,
	struct dnode *dn,
	struct dir_entry *ep,
	int mode,
	time_t timestamp,
	int type,
	const char *path)
#else
getdnode(host, dn, ep, mode, timestamp, type, path)
	char *host;
	struct dnode *dn;
	struct dir_entry *ep;
	int mode;
	time_t timestamp;
	int type;
	char *path;
#endif
{
	u_long irec, firstirec;
	struct instance_record *ir;
	struct dheader *h;
	register int i;
	static struct dnode dummy;
	int rc;

	ndumps = 0;
	irec = firstirec = ep->de_instances;
	do {
		if (irec == NONEXISTENT_BLOCK)
			break;
		if ((ir = instrec_get(irec)) == NULL_IREC)
			break;
		for (i = 0; i < entries_perrec; i++) {
			if (ir->i_entry[i].ie_dumpid == 0)
				continue;
			if (header_get(host, ir->i_entry[i].ie_dumpid, &h))
				continue;
			dumps[ndumps].dumpid = ir->i_entry[i].ie_dumpid;
			dumps[ndumps].dnode_offset =
				ir->i_entry[i].ie_dnode_index;
			dumps[ndumps].dumptime = h->dh_time;
			if (++ndumps >= MAXDUMPS) {
				return (0);
			}
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
			(int (*)(const void *, const void *))timecompare);
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
			(void) fprintf(stderr, gettext("%s: bad type %d\n"),
				"getdnode", type);
			return (0);
		}
	}

	if (rc == 1) {
		char rpath[MAXPATHLEN];

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
#ifdef __STDC__
do_opaque(const char *host,
	struct dnode *dn,
	struct dir_entry *ep,
	int mode,
	time_t	timestamp)
#else
do_opaque(host, dn, ep, mode, timestamp)
	char *host;
	struct dnode *dn;
	struct dir_entry *ep;
	int mode;
	time_t	timestamp;
#endif
{
	int rc = 0;
	struct dnode *dp;
	register int i, j, dumpidx;
	struct dheader *h;
	char mntpt[MAXPATHLEN];
	int mntlen, newlen;
	struct dumplist *l;
	u_long mydump, offset;
	u_long dumplist[MAXDUMPS];
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
		if (header_get(host, dumps[i].dumpid, &h))
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
	if (listget(host, mntpt, timestamp, &l))
		goto done;

	listcnt = 0;
	while (l) {
		dumplist[listcnt] = l->dumpid;
		if (++listcnt >= MAXDUMPS)
			return (0);
		l = l->nxt;
	}

	/*
	 * choose the dnode from the latest dump in the list
	 */
	mydump = 0;
	for (i = listcnt-1; i >= 0; i--) {
		for (j = 0; j < ndumps; j++) {
			if (dumps[j].dumpid == dumplist[i]) {
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
	dp = dnode_get(host, mydump, offset);
	if (dp == NULL_DNODE)
		return (0);

	*dn = *dp;

	if (permchk(dp, mode))
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
 * provide a `translucent' view of dumped files -- the
 * most recent instance prior to the current date setting will be
 * retrieved, regardless of what dump contains it.
 */
static int
#ifdef __STDC__
do_translucent(const char *host,
	struct dnode *dn,
	struct dir_entry *ep,
	int mode,
	time_t timestamp)
#else
do_translucent(host, dn, ep, mode, timestamp)
	char *host;
	struct dnode *dn;
	struct dir_entry *ep;
	int mode;
	time_t timestamp;
#endif
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
	dp = dnode_get(host, dumps[dumpidx].dumpid,
			dumps[dumpidx].dnode_offset);
	if (dp == NULL_DNODE)
		goto done;

	*dn = *dp;

	if (permchk(dp, mode))
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
#ifdef __STDC__
changed_perms(int idx,
	int mode,
	const char *host)
#else
changed_perms(idx, mode, host)
	int idx;
	int mode;
	char *host;
#endif
{
	register int i;
	struct dnode *dp;

	for (i = idx; i < ndumps; i++) {
		dp = dnode_get(host, dumps[i].dumpid, dumps[i].dnode_offset);
		if (dp) {
			if (permchk(dp, mode))
				return (1);
		}
	}
	return (0);
}

static int
#ifdef __STDC__
ismntpt(const char *host,
	const char *path,
	time_t timestamp)
#else
ismntpt(host, path, timestamp)
	char *host;
	char *path;
	time_t timestamp;
#endif
{
	static struct mntpts *mntpts = (struct mntpts *)-1;
	static int maxmntlen;
	register struct mntpts *p;
	register int len;

	if (*path == '/' && *(path+1) == '\0')
		/*
		 * root is always a mount point (?)
		 */
		return (1);

	if (mntpts == (struct mntpts *)-1) {
		mntpts = (struct mntpts *)NULL;
		if (get_mntpts(host, timestamp, &mntpts))
			return (0);
	}

	len = strlen(path);
	if (maxmntlen && len > maxmntlen)
		return (0);

	for (p = mntpts; p; p = p->nxt) {
		if (maxmntlen < p->mp_namelen)
			maxmntlen = p->mp_namelen;
		if (len <= p->mp_namelen &&
				strncmp(path, p->mp_name, len) == 0)
			return (1);
	}
	return (0);
}

static void
smashpath(s)
	char *s;
{
	register char *p1, *p2, *t;
	char buf[256];

	p2 = s;
	if (*p2 != '/') {
		return;
	}
	p1 = buf;
	while (*p2 && *p2 == '/')
		p2++;
	while (*p2) {
		if (*p2 == '.' && *(p2+1) == '.') {
			p2 += 2;
			while (*p2 && (*p2 == ' ' || *p2 == '/'))
				p2++;
			*p1 = '\0';
			if (t = strrchr(buf, '/')) {
				*t = '\0';
				p1 = t;
			}
		} else if (*p2 == '.' && (*(p2+1) == '/' || *(p2+1) == 0)) {
			p2++;
			if (*p2)
				p2++;
		} else {
			while (*p2 && *p2 == '/')
				p2++;
			if (*p2)
				*p1++ = '/';
			while (*p2 && *p2 != '/')
				*p1++ = *p2++;
			if (*p2)
				p2++;
		}
	}
	*p1 = '\0';
	if (strcmp(buf, ""))
		(void) strcpy(s, buf);
	else
		(void) strcpy(s, "/");
}
#endif GETDNODE

static long thisuser, thisgroup;
static int ngroups;
static int *gidset;
static char *thishost, *dbhost;

static void
setperms(p)
	struct db_findargs *p;
{
	thisuser = p->uid;
	thisgroup = p->gid;
	ngroups = p->ngroups;
	gidset = p->gidlist;
	thishost = p->myhost;
	dbhost = p->host;
}

/*
 * check permissions
 */
static int
permchk(dp, m)
	struct dnode *dp;
	int m;
{
	register int i;

	if (thisuser == 0 && strncmp(thishost, dbhost, strlen(thishost)) == 0)
		/* super-user sees all, but only for dumps of his machine */
		return (0);

	if (thisuser != dp->dn_uid) {
		m >>= 3;	/* check `group' perms */
		if (thisgroup == dp->dn_gid)
			goto found;
		for (i = 0; i < ngroups; i++)
			if (dp->dn_gid == gidset[i])
				goto found;
		m >>= 3;	/* check `other' perms */
	}

found:
	if ((dp->dn_mode & m) == m)
		return (0);
	return (1);	/* EACCESS */
}

static int
#ifdef __STDC__
gmatch(register char *s,
	const char *p)
#else
gmatch(s, p)
	register char *s;
	char *p;
#endif
{
	register char *cp = (char *)p;
	register int scc;
	char c;
	char ok;
	int lc;

	scc = *s++;
	switch (c = *cp++) {
	case '[':
		ok = 0;
		lc = -1;
		while (c = *cp++) {
			if (c == ']') {
				return (ok ? gmatch(s, cp) : 0);
			} else if (c == '-') {
				if (lc <= scc && scc <= (*cp++))
					ok++;
			} else {
				lc = c;
				if (scc == lc)
					ok++;
			}
		}
		return (0);

	case '\\':
		c = *cp++;
		/*FALLTHROUGH*/

	default:
		if (c != scc)
			return (0);
		/*FALLTHROUGH*/

	case '?':
		return (scc ? gmatch(s, cp) : 0);

	case '*':
		if (*cp == 0)
			return (1);
		s--;
		while (*s) {
			if (gmatch(s++, cp))
				return (1);
		}
		return (0);

	case 0:
		return (scc == 0);
	}
}

static FILE *dir_fp;
static FILE *inst_fp;
static int lastfd;
#define	MAXDNODEFDS	50
static struct holdem {
	FILE *fp;
	u_long dumpid;
} hold_dnodes[MAXDNODEFDS];
static struct instance_record *ir;

static int
#ifdef __STDC__
openall(const char *host)
#else
openall(host)
	char *host;
#endif
{
	char file[256];
	struct instance_record first;

	(void) sprintf(file, "%s/%s", host, DIRFILE);
	if ((dir_fp = fopen(file, "r")) == NULL) {
		return (1);
	}
	(void) sprintf(file, "%s/%s", host, INSTANCEFILE);
	if ((inst_fp = fopen(file, "r")) == NULL) {
		(void) fclose(dir_fp);
		return (1);
	}
	if (fread((char *)&first, sizeof (struct instance_record),
				1, inst_fp) != 1) {
		perror("inst_getsize");
		(void) fclose(dir_fp);
		(void) fclose(inst_fp);
		return (1);
	}
	instance_recsize = first.i_entry[0].ie_dnode_index;
	entries_perrec = first.i_entry[0].ie_dumpid;
	ir = (struct instance_record *)malloc((unsigned)instance_recsize);
	if (ir == NULL_IREC) {
		(void) fprintf(stderr,
			gettext("cannot allocate instance record\n"));
		(void) fclose(dir_fp);
		(void) fclose(inst_fp);
		return (1);
	}
	lastfd = 0;
	return (0);
}

static void
#ifdef __STDC__
closeall(void)
#else
closeall()
#endif
{
	register int i;

	if (ir)
		free((char *)ir);
	if (dir_fp)
		(void) fclose(dir_fp);
	if (inst_fp)
		(void) fclose(inst_fp);
	dir_fp = inst_fp = NULL;

	for (i = 0; i < lastfd; i++) {
		if (hold_dnodes[i].dumpid) {
			(void) fclose(hold_dnodes[i].fp);
			hold_dnodes[i].dumpid = 0;
			hold_dnodes[i].fp = NULL;
		}
	}
	lastfd = 0;
}

static int
getdirblock(num, bp)
	u_long num;
	struct dir_block *bp;
{
	if (fseek(dir_fp, (long)(num*DIR_BLKSIZE), 0) == -1) {
		perror("dir_seek");
		return (-1);
	}
	if (fread((char *)bp, DIR_BLKSIZE, 1, dir_fp) != 1) {
		perror("dir_read");
		return (-1);
	}
	return (0);
}

static struct instance_record *
instrec_get(num)
	u_long num;
{
	if (num == NONEXISTENT_BLOCK)
		return (NULL_IREC);
	if (fseek(inst_fp, (long)(num*instance_recsize), 0) == -1) {
		perror("inst_seek");
		return (NULL_IREC);
	}
	if (fread((char *)ir, instance_recsize, 1, inst_fp) != 1) {
		perror("inst_read");
		return (NULL_IREC);
	}
	return (ir);
}

static struct dnode *
#ifdef __STDC__
dnode_get(const char *host,
	u_long dumpid,
	u_long index)
#else
dnode_get(host, dumpid, index)
	char *host;
	u_long dumpid;
	u_long index;
#endif
{
	FILE *fp;
	static struct dnode dn;

	if ((fp = getdnodefp(host, dumpid)) == NULL) {
		return (NULL_DNODE);
	}
	if (fseek(fp, (long)(index*sizeof (struct dnode)), 0) == -1) {
		perror("dnode lseek");
		return (NULL_DNODE);
	}
	if (fread((char *)&dn, sizeof (struct dnode), 1, fp) != 1) {
		perror("dnode_read");
		return (NULL_DNODE);
	}
	return (&dn);
}

static FILE *
#ifdef __STDC__
getdnodefp(const char *host,
	u_long dumpid)
#else
getdnodefp(host, dumpid)
	char *host;
	u_long dumpid;
#endif
{
	register int i;
	char filename[256];
	FILE *fp;

	for (i = 0; i < lastfd; i++) {
		if (hold_dnodes[i].dumpid == dumpid)
			return (hold_dnodes[i].fp);
	}

	if (lastfd >= MAXDNODEFDS) {
		for (i = 0; i < MAXDNODEFDS; i++) {
			if (hold_dnodes[i].dumpid) {
				(void) fclose(hold_dnodes[i].fp);
				hold_dnodes[i].dumpid = 0;
				hold_dnodes[i].fp = NULL;
			}
		}
		lastfd = 0;
	}
	(void) sprintf(filename, "%s/%s.%lu", host, DNODEFILE, dumpid);
	if ((fp = fopen(filename, "r")) == NULL) {
		perror("dnode open");
		return (NULL);
	}
	hold_dnodes[lastfd].fp = fp;
	hold_dnodes[lastfd].dumpid = dumpid;
	lastfd++;
	return (fp);
}

static int
setcurdir(dirname)
	char *dirname;
{
	struct dir_entry *ep;

	ep = (struct dir_entry *)dir_path_lookup(dirname);
	if (ep != NULL_DIRENTRY)
		return (ep->de_directory);
	return (NONEXISTENT_BLOCK);
}

/*
 * locate the block which contains the entry for 'path' where
 * path is a fully qualified pathname.
 */
static struct dir_entry *
dir_path_lookup(path)
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
		if ((ep = dir_name_lookup(thisblk, comp)) == NULL_DIRENTRY)
			break;
		thisblk = ep->de_directory;
	}
	if (rc != 0)
		ep = NULL_DIRENTRY;

	return (ep);
}

static int
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
static struct dir_entry *
#ifdef __STDC__
dir_name_lookup(u_long blknum,
	const char *name)
#else
dir_name_lookup(blknum, name)
	u_long blknum;
	char *name;
#endif
{
	struct dir_entry *ep;
	u_long startblock;
	struct dir_block myblock;

	if (getdirblock(blknum, &myblock))
		return (NULL_DIRENTRY);

	if (myblock.db_spaceavail == DIRBLOCK_DATASIZE)
		/* empty block? */
		return (NULL_DIRENTRY);

	startblock = blknum;
	do {
		/*LINTED*/
		ep = (struct dir_entry *)myblock.db_data;
		/*LINTED*/
		while (ep != DE_END(&myblock)) {
			register char *s;
			register char *t;

			/*
			 * inline version of
			 * if (strcmp(ep->de_name, name) == 0)
			 *	return (ep);
			 */
			for (s = ep->de_name, t = (char *)name;
					*s == *t && *s && *t; s++, t++)
				;
			if (*s == '\0' && *t == '\0')
				return (ep);
			/* end inline */
			ep = DE_NEXT(ep);
		}
		blknum = myblock.db_next;
		if (blknum != startblock)
			if (getdirblock(blknum, &myblock))
				return (NULL_DIRENTRY);
	} while (blknum != startblock);

	return (NULL_DIRENTRY);
}

static struct holdheader {
	struct dheader *dh;
	u_long dumpid;
	struct holdheader *nxt;
} *holdheaders;

static struct holdlist {
	struct dumplist *dl;
	struct holdlist *nxt;
} *holdlist;

static struct nulldump {
	u_long dumpid;
	struct nulldump *nxt;
} *nulldump;

static struct nullist {
	char *mntpt;
	struct nullist *nxt;
} *nullist;

static int
#ifdef __STDC__
header_get(const char *host,
	u_long dumpid,
	struct dheader **header)
#else
header_get(host, dumpid, header)
	char *host;
	u_long dumpid;
	struct dheader **header;
#endif
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
	if (rc = header_read(host, dumpid, header)) {
		makeunknown(dumpid);
		return (rc);
	}

	hold_header(*header, dumpid);
	return (0);
}

static int
#ifdef __STDC__
header_read(const char *host,
	u_long dumpid,
	struct dheader **h)
#else
header_read(host, dumpid, h)
	char *host;
	u_long dumpid;
	struct dheader **h;
#endif
{
	char namebuf[MAXPATHLEN];
	struct stat stbuf;
	struct dheader *p;
	int fd;

	*h = NULL;
	(void) sprintf(namebuf, "%s/%s.%lu", host, HEADERFILE, dumpid);
	if ((fd = open(namebuf, O_RDONLY)) == -1) {
		return (-1);
	}
	if (fstat(fd, &stbuf) == -1) {
		return (-1);
	}
	p = (struct dheader *)malloc((unsigned)stbuf.st_size);
	if (p == NULL) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "header_read");
		(void) close(fd);
		return (-1);
	}
	if (read(fd, (char *)p, (int)stbuf.st_size) != (int)stbuf.st_size) {
		(void) fprintf(stderr,
			gettext("%s: %s error\n"), "header_read", "read");
		(void) close(fd);
		free((char *)p);
		return (-1);
	}
	(void) close(fd);
	*h = p;
	return (0);
}

static int
#ifdef __STDC__
listget(const char *host,
	const char *mntpt,
	time_t timestamp,
	struct dumplist **l)
#else
listget(host, mntpt, timestamp, l)
	char *host;
	char *mntpt;
	time_t timestamp;
	struct dumplist **l;
#endif
{
	struct dumplist *dl;
	struct fsheader_readargs p;

	if (dl = lookup_list(mntpt)) {
		*l = dl;
		return (0);
	} else if (null_list(mntpt)) {
		return (1);
	}

	p.host = (char *)host;
	p.mntpt = (char *)mntpt;
	p.time = timestamp;
	if ((dl = fheader_search(&p)) == NULL) {
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
	if (h == (struct holdheader *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "hold_header");
		exit(1);
	}
	h->dumpid = dumpid;
	h->dh = p;
	h->nxt = holdheaders;
	holdheaders = h;
}

static struct dumplist *
#ifdef __STDC__
lookup_list(const char *name)
#else
lookup_list(name)
	char *name;
#endif
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
	if (hl == (struct holdlist *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "hold_list");
		exit(1);
	}
	hl->dl = l;
	hl->nxt = holdlist;
	holdlist = hl;
}

static int
unknown_dump(dumpid)
	u_long dumpid;
{
	register struct nulldump *p;

	for (p = nulldump; p; p = p->nxt) {
		if (p->dumpid == dumpid)
			return (1);
	}
	return (0);
}

static void
makeunknown(dumpid)
	u_long dumpid;
{
	register struct nulldump *p;

	p = (struct nulldump *)malloc(sizeof (struct nulldump));
	if (p == (struct nulldump *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "makeunknown");
		exit(1);
	}
	p->nxt = nulldump;
	nulldump = p;
	p->dumpid = dumpid;
}

static int
#ifdef __STDC__
null_list(const char *mntpt)
#else
null_list(mntpt)
	char *mntpt;
#endif
{
	register struct nullist *p;

	for (p = nullist; p; p = p->nxt) {
		if (strcmp(p->mntpt, mntpt) == 0)
			return (1);
	}
	return (0);
}

static void
#ifdef __STDC__
make_null(const char *mntpt)
#else
make_null(mntpt)
	char *mntpt;
#endif
{
	register struct nullist *p;

	p = (struct nullist *)malloc(sizeof (struct nullist));
	if (p == (struct nullist *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "make_null");
		exit(1);
	}
	p->mntpt = (char *)malloc((unsigned)(strlen(mntpt)+1));
	if (p->mntpt == (char *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "make_null");
		free((char *)p);
		exit(1);
	}
	(void) strcpy(p->mntpt, mntpt);
	p->nxt = nullist;
	nullist = p;
}
