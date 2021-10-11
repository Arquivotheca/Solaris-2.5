/*
 * ptree -- print family tree of processes
 */

#ident	"@(#)ptree.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/procfs.h>

typedef struct ps {
	int	done;
	uid_t	uid;
	uid_t	gid;
	pid_t	pid;
	pid_t	ppid;
	pid_t	pgrp;
	pid_t	sid;
	timestruc_t start;
	char	psargs[PRARGSZ];
	struct ps *pp;		/* parent */
	struct ps *sp;		/* sibling */
	struct ps *cp;		/* child */
} ps_t;

static	ps_t	**ps;		/* array of ps_t's */
static	unsigned psize;		/* size of array */
static	int	nps;		/* number of ps_t's */
static	ps_t	*p0;		/* process 0 */
static	ps_t	*p1;		/* process 1 */

static	int	aflag = 0;
static	int	columns = 80;

static	char	*procdir = "/proc";	/* standard /proc directory */

static int prprint(ps_t *p, int level, int recurse);
static void printone(ps_t *p, int level);
static void prsort(ps_t *p);

/* ARGSUSED */
main(argc, argv)
	int	argc;
	char	**argv;
{
	prpsinfo_t info;	/* process information structure from /proc */
	char	*command;
	int opt;
	int errflg = 0;
	struct winsize winsize;
	char *s;
	int n;

	DIR *dirp;
	struct dirent *dentp;
	char	pname[100];
	int	pdlen;

	register ps_t *p;

	if ((command = strrchr(argv[0], '/')) == NULL)
		command = argv[0];
	else
		command++;

	/* options */
	while ((opt = getopt(argc, argv, "a")) != EOF) {
		switch (opt) {
		case 'a':		/* include children of process 0 */
			aflag = 1;
			break;
		default:
			errflg = 1;
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (errflg) {
		(void) fprintf(stderr,
			"usage:\t%s [ -a ] [ {pid|user} ... ]\n", command);
		(void) fprintf(stderr,
			"  (show process trees)\n");
		(void) fprintf(stderr,
			"  list can include process-ids and user names\n");
		(void) fprintf(stderr,
			"  -a : include children of process 0\n");
		exit(2);
	}

	/*
	 * Kind of a hack to determine the width of the output...
	 */
	if ((s = getenv("COLUMNS")) != NULL && (n = atoi(s)) > 0)
		columns = n;
	else if (isatty(fileno(stdout)) &&
	    ioctl(fileno(stdout), TIOCGWINSZ, &winsize) == 0 &&
	    winsize.ws_col != 0)
		columns = winsize.ws_col;

	nps = 0;
	psize = 0;
	ps = NULL;

	/*
	 * Search the /proc directory for all processes.
	 */
	if ((dirp = opendir(procdir)) == NULL) {
		(void) fprintf(stderr, "%s: cannot open PROC directory %s\n",
		    command, procdir);
		exit(1);
	}

	(void) strcpy(pname, procdir);
	pdlen = strlen(pname);
	pname[pdlen++] = '/';

	/* for each active process --- */
	while (dentp = readdir(dirp)) {
		int	procfd;	/* filedescriptor for /proc/nnnnn */

		if (dentp->d_name[0] == '.')		/* skip . and .. */
			continue;
		(void) strcpy(pname + pdlen, dentp->d_name);
retry:
		if ((procfd = open(pname, O_RDONLY)) == -1)
			continue;

		/*
		 * Get the info structure for the process and close quickly.
		 */
		if (ioctl(procfd, PIOCPSINFO, (char *) &info) == -1) {
			int	saverr = errno;

			(void) close(procfd);
			if (saverr == EAGAIN)
				goto retry;
			if (saverr != ENOENT)
				perror(pname);
			continue;
		}
		(void) close(procfd);

		if (nps >= psize) {
			if ((psize *= 2) == 0)
				psize = 20;
			if ((ps = (ps_t **)realloc(ps, psize*sizeof(ps_t *)))
			    == NULL) {
				perror("realloc()");
				exit(2);
			}
		}
		if ((p = (ps_t *)malloc(sizeof(ps_t))) == NULL) {
			perror("malloc()");
			exit(2);
		}
		ps[nps++] = p;
		p->done = 0;
		p->uid = info.pr_uid;
		p->gid = info.pr_gid;
		p->pid = info.pr_pid;
		p->ppid = info.pr_ppid;
		p->pgrp = info.pr_pgrp;
		p->sid = info.pr_sid;
		p->start = info.pr_start;
		if (info.pr_zomb)
			(void) strcpy(p->psargs, "<defunct>");
		else if (info.pr_psargs[0] == '\0')
			(void) strncpy(p->psargs, info.pr_fname,
			    sizeof(p->psargs));
		else
			(void) strncpy(p->psargs, info.pr_psargs,
			    sizeof(p->psargs));
		p->psargs[sizeof(p->psargs)-1] = '\0';
		p->pp = NULL;
		p->sp = NULL;
		p->cp = NULL;
		switch (p->pid) {
		case 0:
			p0 = p;
			break;
		case 1:
			p1 = p;
			break;
		}
	}

	(void) closedir(dirp);

	if (p0 == NULL)
		p0 = ps[0];
	if (p1 == NULL)
		p1 = p0;
	prsort(p0);

	if (argc == 0) {
		for (p = aflag? p0->cp : p1->cp; p != NULL; p = p->sp)
			(void) prprint(p, 0, 1);
		return 0;
	}

	while (argc-- > 0) {
		char *arg;
		char *next;
		register pid_t pid;
		register uid_t uid;
		register int n;
		register int level;

		/* in case some silly person said 'ptree /proc/[0-9]*' */
		arg = strrchr(*argv, '/');
		if (arg++ == NULL)
			arg = *argv;
		argv++;
		uid = -1;
		pid = strtoul(arg, &next, 10);
		if (pid < 0 || *next != '\0') {
			struct passwd *pw = getpwnam(arg);
			if (pw == NULL) {
				(void) fprintf(stderr,
					"%s: cannot find %s passwd entry\n",
					command, arg);
				continue;
			}
			uid = pw->pw_uid;
			pid = -1;
		}

		if (pid == 0)
			continue;

		for (n = 0; n < nps; n++) {
			register ps_t *p = ps[n];

			if (p->ppid == 0 && !aflag)
				continue;

			if (p->pid == pid || p->uid == uid) {
				level = prprint(p, 0, -1);
				(void) prprint(p, level-1, 1);
				if (uid == -1)
					break;
			}
		}
	}

	return (0);
}

static void
printone(register ps_t *p, register int level)
{
	register int n;

	if (!p->done && p->pid != 0) {
		for (n = 0; n < level; n++)
			(void) fputs("  ", stdout);
		if ((n = columns - 1 - 6 - 2*level) < 0)
			n = 0;
		(void) printf("%-5ld %.*s\n", p->pid, n, p->psargs);
		p->done = 1;
	}
}

static int
prprint(register ps_t *p, register int level, int recurse)
{
	if (recurse > 0) {
		printone(p, level);
		for (p = p->cp; p != NULL; p = p->sp)
			(void) prprint(p, level+1, recurse);
		return level;
	} else if (recurse < 0) {
		if (p == (aflag? p0 : p1))
			return 0;
		if (p->pp != NULL)
			level = prprint(p->pp, 0, recurse);
		printone(p, level);
		return level+1;
	} else {
		printone(p, level);
		return level;
	}
}

static void
prsort(register ps_t *pp)
{
	register ps_t *sp;
	register int n;

	for (n = 0; n < nps; n++) {
		register ps_t *p = ps[n];

		if (p != NULL && p != pp && p->ppid == pp->pid) {
			/* found a child process */
			register ps_t **here;

			/* ps[n] = NULL; */
			p->pp = pp;

			/* sort by start time */
			for (here = &pp->cp, sp = pp->cp;
			    sp != NULL;
			    here = &sp->sp, sp = sp->sp) {
				if (p->start.tv_sec < sp->start.tv_sec)
					break;
				if (p->start.tv_sec == sp->start.tv_sec
				 && p->start.tv_nsec < sp->start.tv_nsec)
					break;
			}
			p->sp = sp;
			*here = p;

			prsort(p);
		}
	}
}
