#ident	"@(#)pflags.c	1.2	95/03/15 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>

static	int	look( char * );
static	void	lwplook( prstatus_t * );
static	int	perr( const char * );
static	char *	prflags( long );
static	char *	prwhy( int );
static	char *	prwhat( int , int );

extern	char *	signame( int );
extern	char *	fltname( int );
extern	char *	sysname( int );

static	char	procname[64];

main(argc, argv)
int argc;
char **argv;
{
	int rc = 0;

	if (argc <= 1) {
		char * cmd = strrchr(argv[0], '/');

		if (cmd++ == NULL)
			cmd = argv[0];
		(void) fprintf(stderr, "usage:  %s pid ...\n", cmd);
		(void) fprintf(stderr, "  (report process status flags)\n");
		return 2;
	}

	while (--argc > 0)
		rc += look(*++argv);

	return rc;
}

static int
look(arg)
	char *arg;
{
	int rval = 0;
	int fd;
	int nlwp;
	int i;
	id_t * lwpid;
	char * pidp;
	prstatus_t prstatus;
	prpsinfo_t prpsinfo;
	sigset_t sigmask;
	fltset_t fltmask;
	sysset_t entrymask;
	sysset_t exitmask;
	long sigtrace, sigtrace2, fltbits;
	long sigpend, sigpend2;
	long *bits;

	if (strchr(arg, '/') != NULL)
		(void) strncpy(procname, arg, sizeof(procname));
	else {
		(void) strcpy(procname, "/proc/");
		(void) strncat(procname, arg, sizeof(procname)-6);
	}
	pidp = strrchr(procname, '/')+1;
	while (*pidp == '0' && *(pidp+1) != '\0')
		pidp++;

	if ((fd = open(procname, O_RDONLY)) < 0)
		return perr(NULL);
	else if (ioctl(fd, PIOCPSINFO, (int)&prpsinfo) != 0)
		rval = perr("PIOCPSINFO");
	else if (ioctl(fd, PIOCSTATUS, (int)&prstatus) != 0)
		rval = perr("PIOCSTATUS");
	else if (ioctl(fd, PIOCGTRACE, (int)&sigmask) != 0)
		rval = perr("PIOCGTRACE");
	else if (ioctl(fd, PIOCGFAULT, (int)&fltmask) != 0)
		rval = perr("PIOCGFAULT");
	else if (ioctl(fd, PIOCGENTRY, (int)&entrymask) != 0)
		rval = perr("PIOCGENTRY");
	else if (ioctl(fd, PIOCGEXIT, (int)&exitmask) != 0)
		rval = perr("PIOCGEXIT");

	if (rval) {
		(void) close(fd);
		if (errno == ENOENT) {
			(void) printf("%s:\t<defunct>\n", pidp);
			return 0;
		}
		return rval;
	}

	prpsinfo.pr_psargs[72] = '\0';
	(void) printf("%s:\t%s\n", pidp,
		prpsinfo.pr_psargs);

	sigtrace = *((long *)&sigmask);
	sigtrace2 = *((long *)&sigmask + 1);
	fltbits = *((long *)&fltmask);
	if (sigtrace || sigtrace2 || fltbits) {
		if (sigtrace || sigtrace2)
			(void) printf("  sigtrace = 0x%.8x,0x%.8x",
				sigtrace, sigtrace2);
		if (fltbits)
			(void) printf("  flttrace = 0x%.8x", fltbits);
		(void) printf("\n");
	}

	bits = ((long *)&entrymask);
	if (bits[0] || bits[1] || bits[2] || bits[3] || bits[4] || bits[5])
		(void) printf(
		"  entrymask = 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x\n",
			bits[0], bits[1], bits[2], bits[3], bits[4], bits[5]);

	bits = ((long *)&exitmask);
	if (bits[0] || bits[1] || bits[2] || bits[3] || bits[4] || bits[5])
		(void) printf(
		"  exitmask  = 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x\n",
			bits[0], bits[1], bits[2], bits[3], bits[4], bits[5]);

	sigpend  = *((long *)&prstatus.pr_sigpend);
	sigpend2 = *((long *)&prstatus.pr_sigpend + 1);
	if (sigpend || sigpend2)
		(void) printf("  sigpend = 0x%.8x,0x%.8x\n",
			sigpend, sigpend2);

	if ((nlwp = prstatus.pr_nlwp) <= 0)
		nlwp = 1;
	lwpid = (id_t *)malloc((nlwp+1)*sizeof(id_t));
	if (ioctl(fd, PIOCLWPIDS, (int)lwpid) != 0) {
		rval = perr("PIOCLWPIDS");
		(void) close(fd);
		free((char *)lwpid);
		return rval;
	}

	for (i = 0; i < nlwp; i++) {
		int lwpfd;

		if ((lwpfd = ioctl(fd, PIOCOPENLWP, (int)&lwpid[i])) < 0) {
			(void) perr("PIOCOPENLWP");
		} else if (ioctl(lwpfd, PIOCSTATUS, (int)&prstatus) != 0) {
			(void) close(lwpfd);
			(void) perr("PIOCSTATUS");
		} else {
			(void) close(lwpfd);
			lwplook(&prstatus);
		}
	}

	(void) close(fd);
	free((char *)lwpid);
	return 0;
}

static void
lwplook(psp)
	register prstatus_t * psp;
{
	long sighold, sighold2;
	long sigpend, sigpend2;
	int cursig;

	(void) printf("  /%d:\t", psp->pr_who);
	(void) printf("flags = %s", prflags(psp->pr_flags));
	if (psp->pr_flags & PR_ASLEEP) {
		if ((psp->pr_flags & ~PR_ASLEEP) != 0)
			(void) printf("|");
		(void) printf("PR_ASLEEP");
		if (psp->pr_syscall && !(psp->pr_flags & PR_ISSYS)) {
			int i;

			(void) printf(" [ %s(", sysname(psp->pr_syscall));
			for (i = 0; i < psp->pr_nsysarg; i++) {
				if (i != 0)
					printf(",");
				printf("0x%x", psp->pr_sysarg[i]);
			}
			(void) printf(") ]");
		}
	}
	(void) printf("\n");

	if (psp->pr_flags & PR_STOPPED) {
		(void) printf("  why = %s", prwhy(psp->pr_why));
		if (psp->pr_why != PR_REQUESTED
		 && psp->pr_why != PR_SUSPENDED)
			(void) printf("  what = %s",
				prwhat(psp->pr_why, psp->pr_what));
		(void) printf("\n");
	}

	sighold  = *((long *)&psp->pr_sighold);
	sighold2 = *((long *)&psp->pr_sighold + 1);
	sigpend  = *((long *)&psp->pr_lwppend);
	sigpend2 = *((long *)&psp->pr_lwppend + 1);
	cursig   = psp->pr_cursig;

	if (sighold || sighold2 || sigpend || sigpend2 || cursig) {
		if (sighold || sighold2)
			(void) printf("  sigmask = 0x%.8x,0x%.8x",
				sighold, sighold2);
		if (sigpend || sigpend2)
			(void) printf("  lwppend = 0x%.8x,0x%.8x",
				sigpend, sigpend2);
		if (cursig)
			(void) printf("  cursig = %s", signame(cursig));
		(void) printf("\n");
	}
}

static int
perr(s)
	const char *s;
{
	if (s == NULL || errno != ENOENT) {
		if (s)
			(void) fprintf(stderr, "%s: ", procname);
		else
			s = procname;
		perror(s);
	}
	return 1;
}

#define	ALLFLAGS	\
	(PR_STOPPED|PR_ISTOP|PR_DSTOP|PR_ASLEEP|PR_FORK|PR_RLC|PR_PTRACE \
	|PR_PCINVAL|PR_ISSYS|PR_STEP|PR_KLC|PR_ASYNC|PR_PCOMPAT|PR_MSACCT\
	|PR_BPTADJ|PR_ASLWP)

static char *
prflags(arg)
	register long arg;
{
	static char code_buf[100];
	register char * str = code_buf;

	if (arg == 0)
		return("0");

	if (arg & ~ALLFLAGS)
		(void) sprintf(str, "0x%.x", arg & ~ALLFLAGS);
	else
		*str = '\0';

	if (arg & PR_STOPPED)
		(void) strcat(str, "|PR_STOPPED");
	if (arg & PR_ISTOP)
		(void) strcat(str, "|PR_ISTOP");
	if (arg & PR_DSTOP)
		(void) strcat(str, "|PR_DSTOP");
#if 0		/* displayed elsewhere */
	if (arg & PR_ASLEEP)
		(void) strcat(str, "|PR_ASLEEP");
#endif
	if (arg & PR_FORK)
		(void) strcat(str, "|PR_FORK");
	if (arg & PR_RLC)
		(void) strcat(str, "|PR_RLC");
	if (arg & PR_PTRACE)
		(void) strcat(str, "|PR_PTRACE");
	if (arg & PR_PCINVAL)
		(void) strcat(str, "|PR_PCINVAL");
	if (arg & PR_ISSYS)
		(void) strcat(str, "|PR_ISSYS");
	if (arg & PR_STEP)
		(void) strcat(str, "|PR_STEP");
	if (arg & PR_KLC)
		(void) strcat(str, "|PR_KLC");
	if (arg & PR_ASYNC)
		(void) strcat(str, "|PR_ASYNC");
	if (arg & PR_PCOMPAT)
		(void) strcat(str, "|PR_PCOMPAT");
	if (arg & PR_MSACCT)
		(void) strcat(str, "|PR_MSACCT");
	if (arg & PR_BPTADJ)
		(void) strcat(str, "|PR_BPTADJ");
	if (arg & PR_ASLWP)
		(void) strcat(str, "|PR_ASLWP");

	if (*str == '|')
		str++;

	return str;
}

static char *
prwhy(why)
	int why;
{
	static char buf[20];
	register char * str;

	switch (why) {
	case PR_REQUESTED:
		str = "PR_REQUESTED";
		break;
	case PR_SIGNALLED:
		str = "PR_SIGNALLED";
		break;
	case PR_SYSENTRY:
		str = "PR_SYSENTRY";
		break;
	case PR_SYSEXIT:
		str = "PR_SYSEXIT";
		break;
	case PR_JOBCONTROL:
		str = "PR_JOBCONTROL";
		break;
	case PR_FAULTED:
		str = "PR_FAULTED";
		break;
	case PR_SUSPENDED:
		str = "PR_SUSPENDED";
		break;
	default:
		str = buf;
		(void) sprintf(str, "%d", why);
		break;
	}

	return str;
}

static char *
prwhat(why, what)
	int why;
	int what;
{
	static char buf[20];
	register char * str;

	switch (why) {
	case PR_SIGNALLED:
	case PR_JOBCONTROL:
		str = signame(what);
		break;
	case PR_SYSENTRY:
	case PR_SYSEXIT:
		str = sysname(what);
		break;
	case PR_FAULTED:
		str = fltname(what);
		break;
	default:
		(void) sprintf(str = buf, "%d", what);
		break;
	}

	return str;
}
