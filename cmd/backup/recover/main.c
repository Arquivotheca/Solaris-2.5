#ident	"@(#)main.c 1.52 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pwd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <config.h>
#include "cmds.h"

char 	*dbserv;
char	*tapedev;
jmp_buf	cmdstart;
time_t sixmonthsago, onehourfromnow;
int	attempt_seek = 1;		/* try to seek by default */

static jmp_buf sigbuf;
static time_t now;

static char currenthost[BCHOSTNAMELEN+1];
static char dbserver[BCHOSTNAMELEN];
static char *progname;

#ifdef __STDC__
static void getargs(int, char **, char *, char *);
static int allow_sethost(char *);
static void getdirs(char *, char *, char *, char *, time_t);
static void bailout(void);
static int mkhost(char *, char *);
static int openhost(char *, char *);
static void prompt(char *, char *);
static void myhandler(int);
static void clear_caches(void);
#else
static void getargs();
static int allow_sethost();
static void getdirs();
static void bailout();
static int mkhost();
static int openhost();
static void prompt();
static void myhandler();
static void clear_caches();
#endif

/*ARGSUSED*/
main(argc, argv)
	int argc;
	char *argv[];
{
	char arg[MAXPATHLEN];
	char curdir[MAXPATHLEN];
	char in[2*MAXPATHLEN];
	char localdir[MAXPATHLEN];
	char homedir[MAXPATHLEN];
	int cnt, cmd;
	char host[2*BCHOSTNAMELEN];
	char disphost[BCHOSTNAMELEN];
	time_t timestamp;
	struct sigvec myvec;
	struct arglist ap;
	int notify_level;

	progname = strrchr(argv[0], '/');
	if (progname == (char *)0)
		progname = argv[0];
	else
		progname++;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	(void) setreuid(-1, getuid());
	term_init();
	getargs(argc, argv, host, disphost);
	timestamp = time((time_t *)0);
	now = timestamp;
	sixmonthsago = now - 6L*30L*24L*60L*60L;
	onehourfromnow = now + 60L*60L;

	(void) bzero((char *)&ap, sizeof (struct arglist));
	myvec.sv_handler = myhandler;
#ifdef USG
	(void) sigemptyset(&myvec.sa_mask);
	(void) sigaddset(&myvec.sa_mask, SIGINT);
	(void) sigaddset(&myvec.sa_mask, SIGQUIT);
	(void) sigaddset(&myvec.sa_mask, SIGHUP);
	(void) sigaddset(&myvec.sa_mask, SIGPIPE);
	myvec.sa_flags = SA_RESTART;
#else
	myvec.sv_mask = sigmask(SIGINT)|sigmask(SIGQUIT)|sigmask(SIGHUP)|
			sigmask(SIGPIPE);
	myvec.sv_flags = 0;
#endif
	(void) sigvec(SIGINT, &myvec, (struct sigvec *)NULL);
	(void) sigvec(SIGQUIT, &myvec, (struct sigvec *)NULL);
	(void) sigvec(SIGHUP, &myvec, (struct sigvec *)NULL);
	(void) sigvec(SIGPIPE, &myvec, (struct sigvec *)NULL);

	localdir[0] = '\0';
	homedir[0] = '\0';
	getdirs(localdir, homedir, curdir, host, timestamp);

	notify_level = 0;

	outfp = NULL;
	if (setjmp(sigbuf)) {
		freeargs(&ap);
		if (outfp)
			close_output();
	}
	prompt(disphost, curdir);
	while (fgets(in, sizeof (in), stdin) != NULL) {
		if (setjmp(cmdstart)) {
			if (outfp == NULL)
				outfp = stdout;
			(void) fprintf(outfp,
				gettext("restarting command: %s\n"), in);
			freeargs(&ap);
			close_output();
		}
		cnt = getcmd(in, &cmd, arg, curdir, localdir,
				&ap, host, timestamp);
		switch (cmd) {
		case CMD_ADD:
			if (cnt)
				addfiles(host, localdir, &ap,
						(char *)NULL, timestamp);
			break;
		case CMD_ADDNAME:
			addfiles(host, localdir, &ap, arg, timestamp);
			break;
		case CMD_DELETE:
			if (cnt)
				deletefiles(host, &ap, timestamp);
			break;
		case CMD_LS:
			if (cnt == 0) {
				/*EMPTY*/
			} else if (cnt > 1) {
				multifile_ls(host, &ap, timestamp, 0);
			} else {
				term_start_output();
				listfiles(host, ap.base, timestamp,
							0, (char *)0);
				term_finish_output();
			}
			break;
		case CMD_LL:
			if (cnt == 0) {
				/*EMPTY*/
			} else if (cnt > 1) {
				multifile_ls(host, &ap, timestamp, 1);
			} else {
				term_start_output();
				listfiles(host, ap.base, timestamp,
							1, (char *)0);
				term_finish_output();
			}
			break;
		case CMD_CD:
			if (cnt > 1) {
				(void) fprintf(outfp, gettext(
					"ambiguous directory specification\n"));
			} else if (cnt == 1) {
				change_directory(host, curdir,
						cnt, ap.base, timestamp);
			} else if (cnt == 0) {
				db_homedir(curdir, homedir, host, timestamp);
			}
			break;
		case CMD_PWD:
			(void) fprintf(outfp, "%s\n", curdir);
			break;
		case CMD_LCD:
		{
			char newdir[MAXPATHLEN];
			char resolved[MAXPATHLEN];

			if (cnt == 0) {
				(void) strcpy(arg, homedir);
			} else if (cnt != 1) {
				(void) fprintf(outfp, gettext(
					"bad local dir specification\n"));
				break;
			}
			if (*arg == '/') {
				(void) strcpy(newdir, arg);
			} else if (*arg == '~') {
				(void) strcpy(newdir, arg);
				if (mkuserdir(newdir))
					break;
			} else {
				(void) sprintf(newdir, "%s/%s", localdir, arg);
			}

			if (realpath(newdir, resolved) == (char *)NULL) {
				(void) fprintf(outfp,
					gettext("Cannot resolve path %s\n"),
						newdir);
			} else if (access(resolved,
					R_OK|X_OK|F_OK) == -1) {
				(void) fprintf(outfp,
					gettext("access denied for %s\n"),
						resolved);
			} else {
				(void) strcpy(localdir, resolved);
				(void) fprintf(outfp,
					gettext("files now directed to %s\n"),
					localdir);
			}
			break;
		}
		case CMD_LPWD:
			(void) fprintf(outfp, "%s\n", localdir);
			break;
		case CMD_FASTRECOVER:
			if (cnt) {
				delay_io(arg);
			}
			break;
		case CMD_FASTFIND:
			if (cnt) {
				find(host, arg, curdir, timestamp);
			}
			break;
		case CMD_VERSIONS:
			showversions(host, curdir, cnt, &ap);
			break;
		case CMD_SETHOST:
		{
			char newhost[2*BCHOSTNAMELEN];

			if (!allow_sethost(arg)) {
				break;
			}

			if (mkhost(newhost, arg))
				break;

			if (strcmp(host, "")) {
				dir_rclose();
				instance_rclose();
			}

			if (openhost(dbserv, newhost)) {
				if (openhost(dbserv, host))
					exit(1);
			} else {
				purge_dumplists();
				flush_mntpts();
				(void) strcpy(host, newhost);
				(void) strcpy(disphost, arg);
				db_homedir(curdir, homedir, host, timestamp);
			}
			break;
		}
#ifdef NOTNOW
		case CMD_SETDB:
		{
			static char newdbserv[BCHOSTNAMELEN];

			dir_rclose();
			instance_rclose();

			(void) strcpy(newdbserv, arg);
			if (instance_ropen(newdbserv, host)) {
				(void) fprintf(outfp, gettext(
				"Cannot open database (instance) for `%s'\n"),
						host);
				exit(1);
			} else if (dir_ropen(newdbserv, host)) {
				(void) fprintf(outfp, gettext(
				"Cannot open database (dir) for `%s'\n"),
						host);
				exit(1);
			}
			dbserv = newdbserv;
			break;
		}
#endif
		case CMD_SETDATE:
			if (cnt < 1) {
				timestamp = time((time_t *)0);
				clear_caches();
			} else {
				time_t t;

				if ((t = setdate(arg)) == (time_t)0) {
					(void) fprintf(outfp, gettext(
						"bad time specification\n"));
				} else {
					if (t > timestamp)
						clear_caches();
					timestamp = t;
				}
			}
			purge_dumplists();
			flush_mntpts();
			(void) fprintf(outfp,
				gettext("current date setting is %s"),
						lctime(&timestamp));
			break;
		case CMD_SHOWSET:
			/*
			 * show the values of everything for which
			 * there is a `set' command
			 */
			(void) fprintf(outfp, gettext("date setting is: %s"),
			    lctime(&timestamp));
			(void) fprintf(outfp, gettext("dumped host is: %s\n"),
			    disphost);
			(void) fprintf(outfp, gettext("lookup mode is: %s\n"),
			    getopaquemode() ? "opaque" : "translucent");
			break;
		case CMD_LIST:
			print_extractlist();
			break;
		case CMD_EXTRACT:
			extract(notify_level);
			break;
		case CMD_SHOWDUMP:
			showdump(host, &ap, timestamp);
			break;
		case CMD_XRESTORE:
			fullrestore(host, &ap, localdir,
				timestamp, 0, notify_level);
			break;
		case CMD_RRESTORE:
			fullrestore(host, &ap, localdir,
				timestamp, 1, notify_level);
			break;
		case CMD_NOTIFY:
			if (cnt == 1) {
				if (strcmp(arg, "none") == 0) {
					notify_level = 0;
				} else if (strcmp(arg, "all") == 0) {
					notify_level = -1;
				} else if (sscanf(arg,
						"%d", &notify_level) != 1) {
					(void) fprintf(outfp, gettext(
						"bad notify level: %s\n"), arg);
				}
			}
			break;
		case CMD_HELP:
			if (cnt)
				printhelp(arg);
			else
				printhelp((char *)0);
			break;
		case CMD_QUIT:
			if (check_extractlist())
				bailout();
			break;
		case CMD_INVAL:
			usage();
			break;
		case CMD_NULL:
			break;
		case CMD_AMBIGUOUS:
			(void) printf(gettext(
				"Ambiguous command abbreviation\n"));
			break;
		case CMD_SETMODE:
			if (cnt)
				set_lookupmode(arg);
			else
				set_lookupmode((char *)NULL);
			break;
		default:
			(void) fprintf(outfp, gettext("unknown cmd\n"));
			break;
		}
		freeargs(&ap);
		close_output();
		prompt(disphost, curdir);
	}
	bailout();
#ifdef lint
	return (0);
#endif
}

static void
getargs(argc, argv, host, disphost)
	int argc;
	char *argv[];
	char *host, *disphost;
{
	extern char *optarg;
	extern int optind;
	int c, badopt;

#ifdef __STDC__
	(void) readconfig((char *)0, (void (*)(const char *, ...))0);
#else
	(void) readconfig((char *)0, (void (*)())0);
#endif
	if (gethostname(currenthost, BCHOSTNAMELEN)) {
		perror("gethostname");
		exit(1);
	}
	*host = '\0';
	badopt = 0;
	while ((c = getopt(argc, argv, "f:s:h:r")) != -1) {
		switch (c) {
		case 'f':
			tapedev = optarg;
			break;
		case 's':
			dbserv = optarg;
			break;
		case 'h':
			if (mkhost(host, optarg))
				exit(1);
			(void) strcpy(disphost, optarg);
			break;
		case 'r':		/* "r"ead the tape mode; no seeking */
			attempt_seek = 0;
			break;
		case '?':
			badopt++;
			break;
		}
	}

	if (badopt || (optind < argc)) {
		(void) fprintf(stderr,
			gettext(
	"Usage: recover [ -f dumpfile ] [ -s dbserv ] [ -h host ] [-r]\n"));
		exit(1);
	}

	if (!dbserv) {
		(void) getdbserver(dbserver, sizeof (dbserver));
		if (strcmp(dbserver, "localhost") == 0) {
			dbserv = currenthost;
		} else
			dbserv = dbserver;
	}

	if (*host == '\0') {
		(void) strcpy(disphost, currenthost);
		if (mkhost(host, currenthost))
			exit(1);
	} else {
		if (!allow_sethost(disphost))
			exit(1);
	}

	if (tapedev) {
		if (*tapedev == '+') {
			if (setdevice(tapedev+1)) {
				(void) fprintf(stderr, gettext(
					"device sequence `%s' not found\n"),
						tapedev);
				tapedev = NULL;
			}
		} else {
			if (makedevice("recoverseq", tapedev, sequence) < 0) {
				(void) fprintf(stderr, gettext(
					"Cannot construct device sequence\n"));
				tapedev = NULL;
			}
			(void) setdevice("recoverseq");
		}
	}

	if (instance_ropen(dbserv, host)) {
#ifdef notdef
		fprintf(stderr,
			gettext("Cannot open instance for '%s'\n"), host);
#endif
		exit(1);
	} else if (dir_ropen(dbserv, host)) {
#ifdef notdef
		fprintf(stderr,
			gettext("Cannot open dir for '%s'\n"), host);
#endif
		exit(1);
	}
}

static int
allow_sethost(host)
	char *host;
{
	if (strcmp(currenthost, dbserv) == 0 && getuid() == 0)
		return (1);

	if (strcmp(currenthost, host) == 0)
		return (1);

	(void) fprintf(stderr,
		gettext("You must be root on database server machine "));
	(void) fprintf(stderr,
		gettext("to specify an alternate host database\n"));
	return (0);
}

static void
getdirs(localdir, homedir, curdir, host, timestamp)
	char *localdir, *homedir, *curdir, *host;
	time_t timestamp;
{
	struct passwd *pw;
	struct afile ap;
	struct dir_block *bp;
	struct dir_entry *ep;
	u_long blk;
	char fulldir[MAXPATHLEN];

	if (*localdir == 0) {
		if (getwd(localdir) == 0) {
			(void) fprintf(stderr,
				gettext("Cannot get current directory: %s\n"),
				localdir);
			exit(1);
		}
	}

	if (*homedir == 0) {
		(void) strcpy(homedir, "/");
		if (pw = getpwuid(getuid())) {
			(void) strcpy(homedir, pw->pw_dir);
		} else {
			(void) fprintf(stderr, gettext(
				"Cannot get home directory, using `/'\n"));
		}
	}

	ep = pathcheck(localdir, fulldir, host, timestamp, 0, 1, &bp, &blk);
	if (ep) {
		ap.dir_blknum = blk;
		ap.dbp = bp;
		ap.dep = ep;
		ap.name = fulldir;
		ap.expanded = 0;
		change_directory(host, curdir, 1, &ap, timestamp);
		(void) strcpy(curdir, fulldir);
	} else {
		db_homedir(curdir, homedir, host, timestamp);
	}
}

static void
#ifdef __STDC__
bailout(void)
#else
bailout()
#endif
{
	dir_rclose();
	instance_rclose();
#ifdef CACHE_STATS
	cache_stats();
#endif
	exit(0);
}

static int
mkhost(host, arg)
	char *host, *arg;
{
	struct hostent *hp;
	struct in_addr inaddr;
	char *dot;

	if ((hp = gethostbyname(arg)) == NULL) {
		(void) fprintf(stderr, gettext("unknown host `%s'\n"), arg);
		return (1);
	}
	/*LINTED [alignment ok]*/
	inaddr.s_addr = *((u_long *)hp->h_addr);
	(void) strcpy(host, arg);
	if ((dot = strchr(host, '.')) != NULL)
		*dot = '\0';
	(void) strcat(host, ".");
	(void) strcat(host, inet_ntoa(inaddr));
	return (0);
}

static int
openhost(dbserv, host)
	char *dbserv;
	char *host;
{
	if (instance_ropen(dbserv, host)) {
		(void) fprintf(stderr, gettext(
			"Cannot open database (instance) for '%s'\n"), host);
			return (1);
	} else if (dir_ropen(dbserv, host)) {
		(void) fprintf(stderr, gettext(
			"Cannot open database (dir) for '%s'\n"), host);
		return (1);
	}
	return (0);
}

static void
prompt(host, curdir)
	char *host, *curdir;
{
	char buf[2*MAXPATHLEN];
	int maxwidth;
	static char *arrow = " => ";

	if (!isatty(fileno(stdout)))
		return;

	maxwidth = get_termwidth();
	maxwidth = maxwidth >> 1;
	(void) sprintf(buf, "%s:%s", host, curdir);
	if ((int)strlen(buf) < maxwidth) {
		(void) fputs(buf, stdout);
		(void) fputs(arrow, stdout);
	} else {
		(void) puts(buf);
		(void) fputs(arrow, stdout);
	}
}

void
panic(s)
	char *s;
{
	(void) fprintf(stderr, "%s: %s\n", progname, s);
	bailout();
}

/*ARGSUSED*/
static void
myhandler(sig)
	int	sig;
{
	int rc;

	while (rc = waitpid(-1, (int *)0,  WNOHANG)) {
		if (rc == -1) {
			if (errno != ECHILD)
				perror("waitpid");
			break;
		}
	}
	clear_caches();
	invalidate_handle();
	longjmp(sigbuf, 1);
	/*NOTREACHED*/
}

static void
#ifdef __STDC__
clear_caches(void)
#else
clear_caches()
#endif
{
	(void) dir_initcache();
	(void) instance_initcache();
	dnode_initcache();
	purge_dumplists();
	flush_mntpts();
}

char *
lctime(timep)
	time_t	*timep;
{
	static char buf[256];

	(void) cftime(buf, "%c\n", timep);
	return (buf);
}
