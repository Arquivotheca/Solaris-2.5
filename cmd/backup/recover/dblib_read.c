#ident	"@(#)dblib_read.c 1.20 92/03/25"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include "recover.h"
#include <ctype.h>
#include <signal.h>
#include <rpc/rpc.h>
#ifdef USG
#include <rpc/clnt_soc.h>
#endif
#include <netinet/in.h>
#include <sys/socket.h>
/* #include <database/activetape.h> */

static struct timeval TIMEOUT = { 25, 0 };

static CLIENT *cl;
static char lastdbserv[BCHOSTNAMELEN+1];
static long cachetime;

extern jmp_buf	cmdstart;

#ifdef __STDC__
static bool_t xdr_dumplist(XDR *, struct readdata *);
static bool_t xdr_mntpts(XDR *, struct readdata *);
static int xdr_findresults(XDR *);
static int gethandle(char *);
static CLIENT *my_clnt_create(char *, u_long, u_long, char *);
static void readerror(char *, char *host, int);
#else
static bool_t xdr_dumplist();
static bool_t xdr_mntpts();
static int xdr_findresults();
static int gethandle();
static CLIENT *my_clnt_create();
static void readerror();
#endif

int
dir_read(dbserv, host, blknum, dbp)
	char *dbserv, *host;
	u_long	blknum;
	struct dir_block *dbp;
{
	struct blk_readargs d;
	struct readdata r;

	if (gethandle(dbserv))
		return (-1);
	d.host = host;
	d.recnum = blknum;
	d.blksize = DIR_BLKSIZE;
	d.cachetime = cachetime;

	r.retdata = (char *)dbp;

	if (clnt_call(cl, READ_DIR, xdr_blkread, (caddr_t)&d,
			xdr_dirread, (caddr_t)&r, TIMEOUT) != RPC_SUCCESS) {
		clnt_perror(cl, lastdbserv);
		return (-1);
	}
	if (r.readrc == DBREAD_NEWDATA) {
		(void) dir_initcache();
		(void) instance_initcache();
		purge_dumplists();
		flush_mntpts();
		cachetime = 0;
		longjmp(cmdstart, 1);
	} else if (r.readrc != DBREAD_SUCCESS) {
		readerror(dbserv, host, r.readrc);
		return (-1);
	}
	return (0);
}

int
instance_read(dbserv, host, recnum, blksize, irp)
	char *dbserv, *host;
	u_long	recnum;
	int blksize;
	struct instance_record *irp;
{
	struct blk_readargs i;
	struct readdata r;

	if (gethandle(dbserv))
		return (-1);
	i.host = host;
	i.recnum = recnum;
	i.blksize = blksize;
	i.cachetime = cachetime;

	r.retdata = (char *)irp;

	if (clnt_call(cl, READ_INST, xdr_blkread, (caddr_t)&i,
			xdr_instread, (caddr_t)&r, TIMEOUT) != RPC_SUCCESS) {
		clnt_perror(cl, lastdbserv);
		return (-1);
	}
	if (r.readrc == DBREAD_NEWDATA) {
		/*
		 * Database has been updated.  Flush the possibly
		 * outdated cache and start again.
		 */
		(void) dir_initcache();
		(void) instance_initcache();
		purge_dumplists();
		flush_mntpts();
		cachetime = 0;
		longjmp(cmdstart, 1);
	} else if (r.readrc != DBREAD_SUCCESS) {
		readerror(dbserv, host, r.readrc);
		return (-1);
	}
	return (0);
}

#ifdef notdef
int
dnode_read(dbserv, host, dumpid, recnum, dnp)
	char *dbserv, *host;
	u_long dumpid, recnum;
	struct dnode *dnp;
{
	struct dnode_readargs d;
	struct readdata r;

	if (gethandle(dbserv))
		return (-1);
	d.host = host;
	d.dumpid = dumpid;
	d.recnum = recnum;

	r.retdata = (char *)dnp;

	if (clnt_call(cl, READ_DNODE, xdr_dnodeargs, &d,
				xdr_dnoderead, &r, TIMEOUT) != RPC_SUCCESS) {
		return (-1);
	}

	if (r.readrc == DBREAD_NODUMP) {
		/*
		 * can't open dnode file for dumpid
		 */
		uncache_header(dumpid);
		remove_extractlist(dumpid);
		if (dnode_flushcache(host, dumpid)) {
			/*
			 * got rid of cached dnodes for this dump,
			 * now restart the current command
			 */
			longjmp(cmdstart, 1);
		} else {
			/*
			 * dump never existed
			 */
			return (-1);
		}
	} else if (r.readrc != DBREAD_SUCCESS) {
		readerror(dbserv, host, r.readrc);
		return (-1);
	}

	return (0);
}
#endif

int
dnode_blockread(dbserv, host, dumpid, recnum, blksize, dnp)
	char *dbserv, *host;
	u_long dumpid, recnum;
	int blksize;
	struct dnode *dnp;
{
	struct dnode_readargs d;
	struct readdata r;

	if (gethandle(dbserv))
		return (-1);

	d.host = host;
	d.dumpid = dumpid;
	d.recnum = recnum;
	if (blksize != DNODE_READBLKSIZE)
		return (-1);

	r.retdata = (char *)dnp;

	if (clnt_call(cl, READ_DNODEBLK, xdr_dnodeargs, (caddr_t)&d,
	    xdr_dnodeblkread, (caddr_t)&r, TIMEOUT) != RPC_SUCCESS) {
		return (-1);
	}

	if (r.readrc == DBREAD_NODUMP) {
		/*
		 * can't open dnode file for dumpid
		 */
		uncache_header(dumpid);
		remove_extractlist(dumpid);
		if (dnode_flushcache(host, dumpid)) {
			/*
			 * if there were any cached dnodes for this
			 * dump, restart the current command
			 */
			longjmp(cmdstart, 1);
		} else {
			/*
			 * dump never existed
			 */
			return (-1);
		}
	} else if (r.readrc != DBREAD_SUCCESS) {
		readerror(dbserv, host, r.readrc);
		return (-1);
	}

	return (0);
}

char *
db_readlink(dbserv, host, dumpid, offset)
	char *dbserv;
	char *host;
	u_long dumpid;
	long offset;
{
	struct readdata r;
	struct dnode_readargs l;
	static char linkval[MAXPATHLEN];

	if (gethandle(dbserv))
		return (NULL);

	l.host = host;
	l.dumpid = dumpid;
	l.recnum = offset;
	r.retdata = linkval;

	if (clnt_call(cl, READ_LINKVAL, xdr_dnodeargs, (caddr_t)&l,
			xdr_linkval, (caddr_t)&r, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	if (r.readrc == DBREAD_NODUMP) {
		/*
		 * can't open link file for dumpid
		 */
		uncache_header(dumpid);
		remove_extractlist(dumpid);
		if (dnode_flushcache(host, dumpid)) {
			/*
			 * if there were any cached dnodes for this
			 * dump, restart the current command
			 */
			longjmp(cmdstart, 1);
		} else {
			/*
			 * dump never existed
			 */
			return (NULL);
		}
		return (NULL);
	} else if (r.readrc != DBREAD_SUCCESS) {
		readerror(dbserv, host, r.readrc);
		return (NULL);
	}
	return (linkval);
}

int
header_read(dbserv, host, dumpid, hp)
	char *dbserv, *host;
	u_long dumpid;
	struct dheader **hp;
{
	struct header_readargs h;
	struct readdata r;

	if (gethandle(dbserv))
		return (-1);

	h.host = host;
	h.dumpid = dumpid;

	*hp = (struct dheader *)malloc(sizeof (struct dheader));
	if (*hp == (struct dheader *)0) {
		perror("header_read malloc");
		return (-1);
	}

	r.retdata = (char *)*hp;

	if (clnt_call(cl, READ_HEADER, xdr_headerargs, (caddr_t)&h,
			xdr_dheaderread, (caddr_t)&r, TIMEOUT) != RPC_SUCCESS) {
		free((char *)*hp);
		return (-1);
	}

	if (r.readrc == DBREAD_NODUMP) {
		/*
		 * can't locate header file for dumpid
		 */
		uncache_header(dumpid);
		remove_extractlist(dumpid);
		if (dnode_flushcache(host, dumpid)) {
			/*
			 * if there were any cached dnodes for this
			 * dump, restart the current command
			 */
			longjmp(cmdstart, 1);
		}
		return (r.readrc);
	} else if (r.readrc != DBREAD_SUCCESS) {
		readerror(dbserv, host, r.readrc);
		free((char *)*hp);
		return (r.readrc);
	}

	if ((*hp)->dh_ntapes > 1) {
		int size;

		size = sizeof (struct dheader)+(((*hp)->dh_ntapes-1)*LBLSIZE);
		free((char *)*hp);
		*hp = (struct dheader *)malloc((unsigned)size);
		if (*hp == (struct dheader *)0) {
			perror("header_read malloc");
			return (-1);
		}

		r.retdata = (char *)*hp;

		if (clnt_call(cl, READ_FULLHEADER, xdr_headerargs, (caddr_t)&h,
		    xdr_fullheaderread, (caddr_t)&r, TIMEOUT) != RPC_SUCCESS) {
			free((char *)*hp);
			return (-1);
		}

		if (r.readrc != DBREAD_SUCCESS) {
			readerror(dbserv, host, r.readrc);
			free((char *)*hp);
			return (r.readrc);
		}
	}
	return (0);
}

#ifdef notdef
int
read_fsheader(dbserv, host, name, time, hp)
	char *dbserv, *host, *name;
	time_t	time;
	struct dheader **hp;
{
	struct fsheader_readargs args;
	struct readdata r;

	if (gethandle(dbserv))
		return (-1);

	args.host = host;
	args.mntpt = name;
	args.time = time;

	*hp = (struct dheader *)malloc(sizeof (struct dheader));
	if (*hp == (struct dheader *)0) {
		perror("read_fsheader malloc");
		return (-1);
	}

	r.retdata = (char *)*hp;
	if (clnt_call(cl, READ_FSHEADER, xdr_fsheaderargs, &args,
			xdr_dheaderread, &r, TIMEOUT) != RPC_SUCCESS) {
		free((char *)*hp);
		return (-1);
	}

	if (r.readrc != DBREAD_SUCCESS) {
		readerror(dbserv, host, r.readrc);
		free((char *)*hp);
		return (-1);
	}

	if (*(*hp)->dh_host == '\0') {
		free((char *)*hp);
		return (-1);
	}

	if ((*hp)->dh_ntapes > 1) {
		int size;

		size = sizeof (struct dheader)+(((*hp)->dh_ntapes-1)*LBLSIZE);
		free((char *)*hp);
		*hp = (struct dheader *)malloc((unsigned)size);
		if (*hp == (struct dheader *)0) {
			perror("read_fsheader malloc");
			return (-1);
		}

		r.retdata = (char *)*hp;
		if (clnt_call(cl, READ_FULLFSHEADER, xdr_fsheaderargs, &args,
				xdr_fullheaderread,
				&r, TIMEOUT) != RPC_SUCCESS) {
			free((char *)*hp);
			return (-1);
		}

		if (r.readrc != DBREAD_SUCCESS) {
			free((char *)*hp);
			readerror(dbserv, host, r.readrc);
			return (-1);
		}
	}
	return (0);
}
#endif

static bool_t
xdr_dumplist(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	struct dumplist *t, *last, **retlist;
	struct dheader dummy;
	int size;
	int labelsize = LBLSIZE;
	register int i;
	char *p;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);

	if (objp->readrc != DBREAD_SUCCESS)
		return (TRUE);

	/*LINTED [alignment ok]*/
	retlist = (struct dumplist **)objp->retdata;
	last = (struct dumplist *)0;
	while (xdr_dheader(xdrs, &dummy) == TRUE) {
		t = (struct dumplist *)malloc(sizeof (struct dumplist));
		if (t == (struct dumplist *)0)
			return (FALSE);
		t->nxt = (struct dumplist *)0;
		size = sizeof (struct dheader) +
			((dummy.dh_ntapes - 1)*LBLSIZE);
		t->h = (struct dheader *)malloc((unsigned)size);
		if (t->h == (struct dheader *)0)
			return (FALSE);
		bcopy((char *)&dummy, (char *)t->h, sizeof (struct dheader));
		for (i = 1; i < dummy.dh_ntapes; i++) {
			p = t->h->dh_label[i];
			if (!xdr_bytes(xdrs, &p, (u_int *)&labelsize, LBLSIZE))
				return (FALSE);
			if (labelsize != LBLSIZE)
				return (FALSE);
		}

		if (!xdr_u_long(xdrs, &t->dumpid))
			return (FALSE);

		if (last) {
			last->nxt = t;
		} else {
			*retlist = t;
		}
		last = t;
	}
	return (TRUE);
}

void
free_dumplist(p)
	struct dumplist *p;
{
	register struct dumplist *t;

	while (p) {
		t = p;
		p = p->nxt;
		free((char *)t->h);
		free((char *)t);
	}
}

int
read_dumps(dbserv, host, name, time, dl)
	char *dbserv, *host, *name;
	time_t	time;
	struct dumplist **dl;
{
	struct fsheader_readargs args;
	struct readdata r;
	static struct timeval dt = { 180, 0 };

	if (gethandle(dbserv))
		return (-1);

	*dl = (struct dumplist *)0;
	args.host = host;
	args.mntpt = name;
	args.time = time;

	r.retdata = (char *)dl;
	if (clnt_call(cl, READ_DUMPS, xdr_fsheaderargs, (caddr_t)&args,
			xdr_dumplist, (caddr_t)&r, dt) != RPC_SUCCESS) {
		clnt_perror(cl, "read_dumps");
		return (-1);
	}

	if (r.readrc != DBREAD_SUCCESS) {
		if (r.readrc != DBREAD_NODUMP)
			readerror(dbserv, host, r.readrc);
		return (-1);
	}
	return (0);
}

static bool_t
xdr_mntpts(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	struct mntpts *mp, *last, **retlist;
	char *sp;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);

	if (objp->readrc != DBREAD_SUCCESS)
		return (TRUE);

	/*LINTED [alignment ok]*/
	retlist = (struct mntpts **)objp->retdata;
	last = (struct mntpts *)0;
	sp = NULL;
	while (xdr_string(xdrs, &sp, MAXPATHLEN)) {
		mp = (struct mntpts *)malloc(sizeof (struct mntpts));
		if (mp == (struct mntpts *)0)
			return (FALSE);
		mp->nxt = (struct mntpts *)0;
		mp->mp_name = sp;
		mp->mp_namelen = strlen(sp);
		if (last)
			last->nxt = mp;
		else
			*retlist = mp;
		last = mp;
		sp = NULL;
	}
	return (TRUE);
}

void
free_mntpts(p)
	struct mntpts *p;
{
	register struct mntpts *t;

	while (p) {
		t = p;
		p = p->nxt;
		xdr_free(xdr_string, (char *)&t->mp_name);
		free((char *)p);
	}
}

int
db_getmntpts(dbserv, host, name, time, mpp)
	char *dbserv, *host, *name;
	time_t	time;
	struct mntpts **mpp;
{
	struct fsheader_readargs args;
	static struct timeval dt = { 180, 0 };
	struct readdata r;

	if (gethandle(dbserv))
		return (-1);

	args.host = host;
	args.mntpt = name;
	args.time = time;
	r.retdata = (char *)mpp;

	if (clnt_call(cl, CHECK_MNTPT, xdr_fsheaderargs, (caddr_t)&args,
			xdr_mntpts, (caddr_t)&r, dt) != RPC_SUCCESS) {
		clnt_perror(cl, "db_getmntpts");
		return (-1);
	}

	if (r.readrc != DBREAD_SUCCESS) {
		if (r.readrc != DBREAD_NODUMP)
			readerror(dbserv, host, r.readrc);
		return (-1);
	}
	return (0);
}

#ifdef notdef
int
acttape_read(dbserv, label, tp)
	char *dbserv;
	char *label;
	struct active_tape *tp;
{
	struct tape_readargs t;
	struct readdata r;

	if (gethandle(dbserv))
		return (-1);
	t.label = label;

	r.retdata = (char *)tp;
	if (clnt_call(cl, READ_TAPE, xdr_tapeargs, &t,
			xdr_acttaperead, &r, TIMEOUT) != RPC_SUCCESS) {
		return (-1);
	}
	if (r.readrc != DBREAD_SUCCESS) {
		readerror(dbserv, "", r.readrc);
		return (-1);
	}
	/*
	 * XXX: client needs to know this `record number' so he
	 * knows how to interpret the `next'pointer...
	 * Also a function to read tape by block rather than label
	 * so we can get that next block??
	 */
	return (0);
}
#endif

static int
xdr_findresults(xdrs)
	XDR *xdrs;
{
	char line[MAXPATHLEN], *lp;
	int size;
	register char *p;

	lp = line;
	term_start_output();
	while (xdr_bytes(xdrs, &lp, (u_int *)&size, MAXPATHLEN) == TRUE) {
		for (p = line; *p; p++)
			if (isprint((u_char)*p) || *p == '\n')
				term_putc((u_char)*p);
			else
				term_putc('?');
	}
	term_finish_output();
	return (TRUE);
}

int
db_find(dbserv, host, arg, curdir, timestamp)
	char *dbserv;
	char *host;
	char *arg;
	char *curdir;
	time_t timestamp;
{
	struct db_findargs a;
	static struct timeval ft = { 300, 0 };

	if (gethandle(dbserv))
		return (-1);

	if (getperminfo(&a))
		return (-1);

	a.host = host;
	a.opaque_mode = getopaquemode();
	a.arg = arg;
	a.timestamp = timestamp;
	a.curdir = curdir;

	if (clnt_call(cl, DB_FIND, xdr_dbfindargs, (caddr_t)&a,
			xdr_findresults, (caddr_t)0, ft) != RPC_SUCCESS) {
		clnt_perror(cl, "db_find");
		return (-1);
	}
	return (0);
}

static int
gethandle(sname)
	char *sname;
{
	int newserv = 0;

	if (cachetime == 0)
		cachetime = time((time_t *)0);
	if (strcmp(sname, lastdbserv)) {
		(void) strcpy(lastdbserv, sname);
		newserv = 1;
	}
	if (newserv) {
#ifdef USG
		sigset_t myset;
#else
		int maskall = ~0;
		int oldmask;
#endif

		if (cl) {
			auth_destroy(cl->cl_auth);
			clnt_destroy(cl);
		}
		/* be root so we get a privileged port */
#ifdef USG
		(void) sigfillset(&myset);
		(void) sigprocmask(SIG_SETMASK, &myset, &myset);
#else
		oldmask = sigsetmask(maskall);
#endif
		(void) setreuid(-1, 0);
		if ((cl = my_clnt_create(lastdbserv,
					DBSERV, DBVERS, "tcp")) == NULL) {
			clnt_pcreateerror(lastdbserv);
			invalidate_handle();
			(void) setreuid(-1, getuid());
#ifdef USG
			(void) sigprocmask(SIG_SETMASK, &myset, (sigset_t *)0);
#else
			(void) sigsetmask(oldmask);
#endif
			return (-1);
		}
#if 0  /* DEBUG */
/*
 * check the port number that I got.  If I run as root, I get a
 * privileged port else I don't (for free from `clnt_create()').
 * By setting our effective uid to root before doing the clnt_create()
 * we should always have a privileged port (assuming that we are
 * setuid root).
 */
{
		int mysock;
		struct sockaddr_in myname;
		int size = sizeof (struct sockaddr_in);
		if (clnt_control(cl, CLGET_FD, &mysock) == TRUE) {
			if (getsockname(mysock, &myname, &size) == -1) {
				perror("getsockname");
			}
			(void) fprintf(stderr,
				gettext("user is %d\n"), getuid());
			if (myname.sin_port < IPPORT_RESERVED) {
				(void) fprintf(stderr,
					gettext("privileged port\n"));
			} else {
				(void) fprintf(stderr,
					gettext("not privileged port\n"));
			}
		}
}
#endif

#ifdef DES
		user2netname(servername, getuid(), NULL);
		cl->cl_auth = authdes_create(servername, 60, NULL, NULL);
#else
		cl->cl_auth = authunix_create_default();
#endif
		(void) setreuid(-1, getuid());
#ifdef USG
		(void) sigprocmask(SIG_SETMASK, &myset, (sigset_t *)0);
#else
		(void) sigsetmask(oldmask);
#endif
	}
	return (0);
}

/*
 * like `clnt_create()' except we don't set default timeouts (since
 * we want a proc like `find' to set a longer one for itself).
 */
static CLIENT *
my_clnt_create(hostname, prog, vers, proto)
	char *hostname;
	u_long prog;
	u_long vers;
	char *proto;
{
	struct hostent *h;
	struct protoent *p;
	struct sockaddr_in sin;
	int sock;

	(void) bzero(sin.sin_zero, sizeof (sin.sin_zero));
	h = gethostbyname(hostname);
	if (h == NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNHOST;
		return (NULL);
	}
	if (h->h_addrtype != AF_INET) {
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = EAFNOSUPPORT;
		return (NULL);
	}
	bcopy(h->h_addr, (char *) &sin.sin_addr, h->h_length);
	sin.sin_family = AF_INET;
	sin.sin_port = 0;

	p = getprotobyname(proto);
	if (p == NULL || p->p_proto != IPPROTO_TCP) {
		rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
		rpc_createerr.cf_error.re_errno = EPFNOSUPPORT;
		return (NULL);
	}
	sock = RPC_ANYSOCK;
	return (clnttcp_create(&sin, prog, vers, &sock, 0, 0));
}

void
#ifdef __STDC__
invalidate_handle(void)
#else
invalidate_handle()
#endif
{
	if (cl) {
		auth_destroy(cl->cl_auth);
		clnt_destroy(cl);
		cl = NULL;
	}
	*lastdbserv = '\0';
}

static void
readerror(dbserv, host, rc)
	char *dbserv, *host;
	int rc;
{
	switch (rc) {
	case DBREAD_NOHOST:
		(void) fprintf(stderr,
		    gettext("host `%s' is not in database at `%s'\n"),
			host, dbserv);
		break;
	case DBREAD_NODUMP:
		(void) fprintf(stderr,
			gettext("requested dump is not in database\n"));
		break;
	case DBREAD_SERVERDOWN:
		(void) fprintf(stderr,
			gettext("database server unavailable\n"));
		(void) kill(getpid(), SIGINT);
		break;
	default:
		(void) fprintf(stderr,
			gettext("unrecognized database read error %d\n"), rc);
		break;
	}
}
