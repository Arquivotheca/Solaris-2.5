#ident	"@(#)psig.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>

extern	char *	signame(int);

static	char *	sigflags(int, int);
static	int	look(char *);
static	int	perr(char *);

static	int	all = 0;
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
		(void) fprintf(stderr, "  (report process signal actions)\n");
		return 2;
	}

	if (argc > 1 && strcmp(argv[1], "-a") == 0) {
		all = 1;
		argc--;
		argv++;
	}

	while (--argc > 0)
		rc += look(*++argv);

	return rc;
}


static int
look(arg)
char *arg;
{
	register int fd;
	register int sig;
	char * pidp;
	sigset_t holdmask;
	int maxsig;
	struct sigaction * action;
	prpsinfo_t psinfo;

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

	if (ioctl(fd, PIOCMAXSIG, (int)&maxsig) < 0)
		return perr("PIOCMAXSIG");

	if (ioctl(fd, PIOCGHOLD, (int)&holdmask) < 0)
		return perr("PIOCGHOLD");

	if (ioctl(fd, PIOCPSINFO, (int)&psinfo) < 0)
		return perr("PIOCPSINFO");

	action = (struct sigaction *)malloc(maxsig*sizeof(struct sigaction));
	if (action == NULL) {
		(void) fprintf(stderr,
			"cannot malloc() space for %d sigaction structures\n",
			maxsig);
		return 1;
	}

	if (ioctl(fd, PIOCACTION, (int)action) < 0) {
		free((char *)action);
		return perr("PIOCACTION");
	}

	(void) close(fd);

	(void) printf("%s:\t%.70s\n", pidp, psinfo.pr_psargs);

	for (sig = 1; sig <= maxsig; sig++) {
		register struct sigaction *sp = &action[sig-1];
		int caught = 0;

		/* signame() returns "SIG..."; skip the "SIG" part */
		(void) printf("%s\t", signame(sig)+3);

		if (prismember(&holdmask, sig))
			(void) printf("blocked,");

		if (sp->sa_handler == SIG_DFL)
			(void) printf("default");
		else if (sp->sa_handler == SIG_IGN)
			(void) printf("ignored");
		else {
			caught = 1;
			(void) printf("caught");
		}

		if (caught || all) {
			register int anyb = 0;
			register int bsig;

			(void) printf("%s", sigflags(sig, sp->sa_flags));
			for (bsig = 1; bsig <= maxsig; bsig++) {
				if (prismember(&sp->sa_mask, bsig)) {
					(void) printf(anyb++? "," : "\t");
					(void) printf("%s", signame(bsig)+3);
				}
			}
		}
		(void) printf("\n");
	}

	free((char *)action);
	return 0;
}

static int
perr(s)
char *s;
{
	if (s)
		(void) fprintf(stderr, "%s: ", procname);
	else
		s = procname;
	perror(s);
	return 1;
}

static char *
sigflags(int sig, register int flags)
{
	static char code_buf[100];
	register char * str = code_buf;
	register int flagmask =
		(SA_ONSTACK|SA_RESETHAND|SA_RESTART|SA_SIGINFO|SA_NODEFER);

	switch (sig) {
	case SIGCLD:
		flagmask |= (SA_NOCLDSTOP|SA_NOCLDWAIT);
		break;
	case SIGWAITING:
		flagmask |= SA_WAITSIG;
		break;
	}

	if (flags & ~flagmask)
		(void) sprintf(str, ",0x%x,", flags & ~flagmask);
	else if (flags == 0)
		(void) strcpy(str, ",0");
	else
		*str = '\0';

	if (flags & SA_RESTART)
		(void) strcat(str, ",RESTART");
	if (flags & SA_RESETHAND)
		(void) strcat(str, ",RESETHAND");
	if (flags & SA_ONSTACK)
		(void) strcat(str, ",ONSTACK");
	if (flags & SA_SIGINFO)
		(void) strcat(str, ",SIGINFO");
	if (flags & SA_NODEFER)
		(void) strcat(str, ",NODEFER");

	switch (sig) {
	case SIGCLD:
		if (flags & SA_NOCLDWAIT)
			(void) strcat(str, ",NOCLDWAIT");
		if (flags & SA_NOCLDSTOP)
			(void) strcat(str, ",NOCLDSTOP");
		break;
	case SIGWAITING:
		if (flags & SA_WAITSIG)
			(void) strcat(str, ",WAITSIG");
		break;
	}

	*str = '\t';

	return str;
}
