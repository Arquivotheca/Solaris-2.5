
#ident	"@(#)mdbmaint.c 1.30 93/06/23"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#include <ctype.h>
#include <rpc/rpc.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef USG
#include <rpc/clnt_soc.h>
#endif
#include <config.h>

int nmapblocks = 100;

static char thishost[BCHOSTNAMELEN+1];
static char *myname;

#ifdef __STDC__
static void usage(void);
static bool_t xdr_strresults(XDR *, int);
static void dodbinfo(char *);
static void listem(const char *, int, const char *);
static void dodelete(char *, char *);
#else
static void usage();
static bool_t xdr_strresults();
static void dodbinfo();
static void listem();
static void dodelete();
#endif

main(argc, argv)
	int argc;
	char *argv[];
{
	extern char *optarg;
	extern int  optind;
	int c;
	int listverb;
	int rebuildall = 0;
	int tpbsize;
	int nblks;
	char *dbserv, *dumpdev, *dbroot, *host, *tapelabel, *tempdir;
	char cmd[BUFSIZ];
	FILE *in;
	struct labelstruct *labeltype = NULL;
	extern struct labelstruct labeltypes[];

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	myname = strrchr(argv[0], '/');
	if (myname == (char *)0)
		myname = argv[0];
	else
		myname++;

	if (argc < 2) {
		usage();
	}

	if (geteuid() != 0) {
		(void) fprintf(stderr,
			gettext("%s: must be run by root\n"), myname);
		exit(1);
	}

	if (gethostname(thishost, BCHOSTNAMELEN)) {
		perror("gethostname");
		exit(1);
	}

	dbserv = dumpdev = dbroot = host = tapelabel = tempdir = NULL;
	listverb = tpbsize = 0;
	while ((c = getopt(argc, argv, "ab:d:f:h:l:m:r:s:t:vV")) != -1) {
		switch (c) {
		case 'a':
			++rebuildall;
			break;
		case 'b':
			tpbsize = atoi(optarg);
			if (tpbsize <= 0 || tpbsize > 512 || (tpbsize & 1)) {
				tpbsize = 0;
				(void) fprintf(stderr, gettext(
				    "Bad tape block size, default used\n"));
			} else {
				tpbsize /= 2;
			}
			break;
		case 'd':
			dumpdev = optarg;
			break;
		case 'f':
			tempdir = optarg;
			break;
		case 'h':
			host = optarg;
			break;
		case 'l':
			for (labeltype = labeltypes;
			    labeltype->name; labeltype++)
				if (strcmp(labeltype->name, optarg) == 0)
				    break;
			if (labeltype->name == NULL)
			    usage();
			break;
		case 'm':
			nblks = atoi(optarg);
			if (nblks <= 0 || nblks > 500) {
				(void) fprintf(stderr, gettext(
					"bad map block specification\n"));
				(void) fprintf(stderr, gettext(
					"using %d megabyte blocks\n"),
					nmapblocks);
			} else {
				nmapblocks = nblks;
			}
			break;
		case 'r':
			dbroot = optarg;
			break;
		case 's':
			dbserv = optarg;
			break;
		case 't':
			tapelabel = optarg;
			break;
		case 'v':
			listverb = 1;
			break;
		case 'V':
			listverb = 2;
			break;
		}
	}

	if (optind >= argc)
		usage();

	switch (argv[optind][0]) {
	case 'd':
		if (strcmp(argv[optind], "dumpadd") == 0) {
			dumpadd(dbserv, dumpdev, tempdir, tpbsize);
		} else if (strcmp(argv[optind], "dir_rebuild") == 0) {
			rebuilddir(dbroot, host, rebuildall);
		} else if (strcmp(argv[optind], "delete") == 0) {
			dodelete(dbserv, tapelabel);
		} else if (strcmp(argv[optind], "dbinfo") == 0) {
			while (dbserv == NULL) {
				char buf[256];

				(void) fprintf(stderr,
					gettext(
	"What database server do you wish information about (hostname)? "));
				if (gets(buf) == NULL)
					exit(1);
				if (buf[0])
					dbserv = buf;
			}
			dodbinfo(dbserv);
		} else {
			usage();
		}
		break;
	case 'p':
		if (strcmp(argv[optind], "pslabel"))
		    usage();
		    while (dbserv == NULL) {
			    char buf[256];

			    (void) fprintf(stderr, gettext(
	"What database server do you wish to list tapes from (hostname)? "));
			    if (gets(buf) == NULL)
				    exit(1);
			    if (buf[0])
				    dbserv = buf;
		    }
		sprintf(cmd, "%s -s %s -V tapelist", argv[0], dbserv);
		while (++optind < argc) {
		    strcat(cmd, " ");
		    strcat(cmd, argv[optind]);
		}
		if ((in = popen(cmd, "r")) == NULL) {
		    fprintf(stderr, gettext("can't invoke '%s':"), cmd);
		    perror("");
		    exit(1);
		}
		pslabel(in, labeltype);
		break;
	case 'r':
		if (strcmp(argv[optind], "reclaim"))
			usage();
		reclaim(dbroot, host);
		break;
	case 's':
		if (strcmp(argv[optind], "stop") == 0) {
			pokeserver(QUIESCE_OPERATION, dbserv);
		} else if (strcmp(argv[optind], "start") == 0) {
			pokeserver(RESUME_OPERATION, dbserv);
		} else {
			usage();
		}
		break;
	case 't':
		if (strcmp(argv[optind], "tapefile_rebuild") == 0) {
			rebuildtape(dbroot);
		} else if (strcmp(argv[optind], "tapeadd") == 0) {
			tapeadd(dbserv, dumpdev, tempdir, tpbsize);
		} else if (strcmp(argv[optind], "tapelist") == 0) {
			if (dbserv == NULL)
				dbserv = thishost;
			if (++optind < argc) {
				for (; optind < argc; optind++)
					listem(argv[optind], listverb, dbserv);
			} else {
				listem((char *)NULL, listverb, dbserv);
			}
		} else {
			usage();
		}
		break;
	default:
		usage();
	}
	exit(0);
#ifdef lint
	return (0);
#endif
	/*NOTREACHED*/
}

static void
#ifdef __STDC__
usage(void)
#else
usage()
#endif
{
	extern struct labelstruct labeltypes[];
	struct labelstruct *lt;

	(void) fprintf(stderr, gettext("usage: %s arg\n\n"), myname);
	(void) fprintf(stderr, gettext("where `arg' is one of:\n\n"));
	(void) fprintf(stderr,
		gettext("\t[%s tapelabel] %s\n"), "-t", "delete");
	(void) fprintf(stderr,
	    gettext("\t[%s database_root] [%s | %s host] [%s mapsize] %s\n"),
		"-r", "-a", "-h", "-m", "dir_rebuild");
	(void) fprintf(stderr,
		gettext("\t[%s database_server] [%s dumpdevice] "), "-s", "-d");
	(void) fprintf(stderr, gettext("[%s tempdir] [%s blksize] %s\n"),
		"-f", "-b", "dumpadd");

	(void) fprintf(stderr,
	    gettext("\t[%s database_root] [%s host] [%s mapsize] %s\n"),
		"-r", "-h", "-m", "reclaim");
	(void) fprintf(stderr, "\tstart\n");
	(void) fprintf(stderr, "\tstop\n");
	(void) fprintf(stderr, gettext(
"\t[%s database_server] [%s dumpdevice] [%s tempdir] [%s blksize] %s\n"),
		"-s", "-d", "-f", "-b", "tapeadd");
	(void) fprintf(stderr,
	    gettext("\t[%s database_root] [%s mapsize] %s\n"),
		"-r", "-m", "tapefile_rebuild");
	(void) fprintf(stderr,
	    gettext("\t[%s database_server] [%s] [%s] %s [tapelabel...]\n"),
		"-s", "-v", "-V", "tapelist");
	(void) fprintf(stderr,
	    gettext("\t[%s database server] [%s %s"), "-s", "-l",
	    labeltypes[0].name);
	for (lt = labeltypes + 1; lt->name; lt++)
	    (void) fprintf(stderr, "|%s", lt->name);
	(void) fprintf(stderr,
	    gettext("] %s [tapelabel...]\n"), "pslabel");
	(void) fprintf(stderr,
		gettext("\t[%s database_server] %s\n"), "-s", "dbinfo");
	exit(1);
}

static struct timeval TIMEOUT = {120, 0};

/*ARGSUSED*/
static bool_t
xdr_strresults(xdrs, notused)
	XDR *xdrs;
	int notused;
{
	char line[256], *lp;
	int size;
	register char *p;

	lp = line;
	while (xdr_bytes(xdrs, &lp, (u_int *)&size, 256) == TRUE) {
		for (p = line; *p; p++)
			if (isprint((u_char)*p) || *p == '\n')
				(void) putchar(*p);
			else
				(void) putchar('?');
	}
	return (TRUE);
}

static void
dodbinfo(dbserv)
	char *dbserv;
{
	CLIENT *cl;
#ifdef USG
	struct hostent *h = gethostbyname(dbserv);
	struct sockaddr_in addr;
	int socket = RPC_ANYSOCK;

	if (h == (struct hostent *)0) {
		(void) fprintf(stderr, gettext("unknown host `%s'\n"), dbserv);
		exit(1);
	}

	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	/*LINTED [alignment ok]*/
	addr.sin_addr.s_addr = *(u_long *)(h->h_addr);

	cl = clnttcp_create(&addr, DBSERV, DBVERS, &socket, 0, 0);
#else
	cl = clnt_create(dbserv, DBSERV, DBVERS, "tcp");
#endif
	if (cl == NULL) {
		clnt_pcreateerror("clnttcp_create");
		(void) fprintf(stderr, gettext(
			"database server not running at host `%s'\n"), dbserv);
		exit(1);
	}
	cl->cl_auth = authunix_create_default();

	if (clnt_call(cl, DB_DBINFO, xdr_void, NULL,
			xdr_strresults, NULL, TIMEOUT) != RPC_SUCCESS) {
		clnt_perror(cl, dbserv);
		exit(1);
	}
}

static void
#ifdef __STDC__
listem(const char *name,
	int verbose,
	const char *dbserv)
#else
listem(name, verbose, dbserv)
	char *name;
	int verbose;
	char *dbserv;
#endif
{
	CLIENT *cl;
	struct tapelistargs a;
#ifdef USG
	struct hostent *h = gethostbyname(dbserv);
	struct sockaddr_in addr;
	int socket = RPC_ANYSOCK;

	if (h == (struct hostent *)0) {
		(void) fprintf(stderr, gettext("unknown host `%s'\n"), dbserv);
		exit(1);
	}

	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	/*LINTED [alignment ok]*/
	addr.sin_addr.s_addr = *(u_long *)(h->h_addr);

	cl = clnttcp_create(&addr, DBSERV, DBVERS, &socket, 0, 0);
#else
	cl = clnt_create(dbserv, DBSERV, DBVERS, "tcp");
#endif
	if (cl == NULL) {
		(void) fprintf(stderr, gettext(
			"database server not running at host `%s'\n"), dbserv);
		exit(1);
	}
	cl->cl_auth = authunix_create_default();

	a.label = name ? (char *)name : "";
	a.verbose = verbose;

	if (clnt_call(cl, DB_TAPELIST, xdr_tapelistargs, (caddr_t)&a,
			xdr_strresults, (caddr_t)0, TIMEOUT) != RPC_SUCCESS) {
		clnt_perror(cl, (char *)dbserv);
		exit(1);
	}
}

void
pokeserver(cmd, dbserv)
	u_long cmd;
	char *dbserv;
{
	CLIENT *cl;

	if (dbserv) {
		if (strcmp(dbserv, thishost)) {
			if (cmd == QUIESCE_OPERATION)
				(void) fprintf(stderr, gettext(
					"You cannot stop a remote server\n"));
			else
				(void) fprintf(stderr, gettext(
					"You cannot start a remote server\n"));
			exit(1);
		}
	} else {
		dbserv = thishost;
	}

	for (;;) {
#ifdef USG
		struct sockaddr_in addr;
		int socket = RPC_ANYSOCK;
		struct hostent *h = gethostbyname(dbserv);

		if (h == (struct hostent *)0) {
			(void) fprintf(stderr,
				gettext("unknown host `%s'\n"), dbserv);
			exit(1);
		}

		addr.sin_family = AF_INET;
		addr.sin_port = 0;
		/*LINTED [alignment ok]*/
		addr.sin_addr.s_addr = *(u_long *)(h->h_addr);

		cl = clnttcp_create(&addr, DBSERV, DBVERS, &socket, 0, 0);
#else
		cl = clnt_create(dbserv, DBSERV, DBVERS, "tcp");
#endif
		if (cl == NULL) {
			(void) fprintf(stderr, gettext(
				"database server not running at host `%s'\n"),
				dbserv);
			return;
		}
		cl->cl_auth = authunix_create_default();

		if (clnt_call(cl, cmd, xdr_void, NULL,
				xdr_void, NULL, TIMEOUT) == RPC_SUCCESS) {
			return;
		} else {
			clnt_perror(cl, dbserv);
		}
		clnt_destroy(cl);
		TIMEOUT.tv_sec += 60;
		if (cmd == QUIESCE_OPERATION)
			(void) fprintf(stderr, gettext(
		    "Re-trying database stop operation at server `%s'\n"),
				dbserv);
		else
			(void) fprintf(stderr, gettext(
		    "Re-trying database start operation at server `%s'\n"),
				dbserv);
	}
}

static void
dodelete(dbserv, tapelabel)
	char *dbserv;
	char *tapelabel;
{
#define	LABELSIZE 16
	char label[LABELSIZE];
	int rc;

	if (dbserv) {
		if (strcmp(dbserv, thishost)) {
			(void) fprintf(stderr, gettext(
				"You cannot delete from a remote server\n"));
			exit(1);
		}
	} else {
		dbserv = thishost;
	}
	(void) bzero(label, LABELSIZE);
	if (tapelabel == NULL) {
		(void) fprintf(stderr,
			gettext("Enter tape label to be deleted: "));
		if (gets(label) == NULL)
			exit(1);
	} else {
		(void) strcpy(label, tapelabel);
	}
	if ((rc = delete_bytape(dbserv, label)) != 0) {
		if (rc == -1)
			(void) fprintf(stderr,
				gettext("%s: %s: no such tape\n"),
				myname, label);
		exit(1);
	}
}

char *
getdbhost(name)
	char *name;
{
	static char hostdir[MAXPATHLEN];
	struct hostent *h;
	struct in_addr inaddr;
	char *dot, *newline;

	if (name == NULL) {
		(void) fprintf(stderr, gettext(
			"Enter name of host to be operated on: "));
		if (fgets(hostdir, sizeof (hostdir), stdin) == NULL)
			exit(1);
		if ((newline = strrchr(hostdir, '\n')) != NULL)
			*newline = '\0';
		name = hostdir;
	} else {
		(void) strcpy(hostdir, name);
		name = hostdir;
	}

	/* if it has a dot and it's followed by a digit, just return it */
	if ((dot = strchr(name, '.')) != NULL) {
		if (isdigit(*(dot + 1)))
			return (hostdir);
	}
	h = gethostbyname(name);
	if (h == NULL) {
		(void) fprintf(stderr, gettext(
			"Cannot get host entry for `%s'\n"), name);
		return (NULL);
	}
	/*LINTED [alignment ok]*/
	inaddr.s_addr = *((u_long *)h->h_addr);
	if (dot != NULL)
		*dot = '\0';
	(void) strcat(name, ".");
	(void) strcat(name, inet_ntoa(inaddr));
	return (hostdir);
}

static int lockfd;

#if defined(USG) && !defined(FLOCK)

#include <sys/file.h>
/*
 * Trump up a version of flock based on fcntl.
 * You don't need this if your implementation
 * has flock -- just compile with -DFLOCK.
 */
#define	LOCK_SH		1	/* shared lock */
#define	LOCK_EX		2	/* exclusive lock */
#define	LOCK_NB		4	/* don't block when locking */
#define	LOCK_UN		8	/* unlock */

static int
flock(fd, operation)
	int	fd;
	int	operation;
{
	struct flock fl;
	int block = 0;
	int status;

	if (operation & LOCK_SH)
		fl.l_type = F_RDLCK;
	else if (operation & LOCK_EX)
		fl.l_type = F_WRLCK;
	else if (operation & LOCK_UN)
		fl.l_type = F_UNLCK;
	if ((operation & LOCK_NB) == 0)
		block = 1;
	fl.l_whence = SEEK_SET; /* XXX ? */
	fl.l_start = (off_t)0;
	fl.l_len = (off_t)0;	/* till EOF */
	while ((status = fcntl(fd, F_SETLK, (char *)&fl)) < 0 &&
		(errno == EACCES || errno == EAGAIN) && block) {
		(void) sleep(1);
	}
	return (status);
}
#endif

void
#ifdef __STDC__
maint_lock(void)
#else
maint_lock()
#endif
{
	if ((lockfd = open(UTIL_LOCKFILE, O_RDWR|O_CREAT, 0600)) == -1) {
		perror("lockdb/open");
		exit(1);
	}
	if (flock(lockfd, LOCK_EX) == -1) {
		perror("lockdb/flock");
		exit(1);
	}
}

void
#ifdef __STDC__
maint_unlock(void)
#else
maint_unlock()
#endif
{
	if (flock(lockfd, LOCK_UN) == -1) {
		perror("unlockdb/flock");
		exit(1);
	}
	(void) close(lockfd);
}

char *
lctime(timep)
	time_t  *timep;
{
	static char buf[256];
	struct tm *tm;

	tm = localtime(timep);
	(void) strftime(buf, sizeof (buf), "%c\n", tm);
	return (buf);
}
