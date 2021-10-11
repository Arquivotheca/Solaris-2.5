#ident	"@(#)prun.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>

static	int	start();
static	int	perr();

static	char	procname[64];

/*ARGSUSED*/
static void
alrm(sig)
int sig;
{
}

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
		(void) fprintf(stderr, "  (set stopped processes running)\n");
		return 2;
	}

	(void) sigset(SIGALRM, alrm);
	while (--argc > 0)
		rc += start(*++argv);

	return rc;
}

static int
start(arg)
char *arg;
{
	int fd;
	char * pidp;

	if (strchr(arg, '/') != NULL)
		(void) strncpy(procname, arg, sizeof(procname));
	else {
		(void) strcpy(procname, "/proc/");
		(void) strncat(procname, arg, sizeof(procname)-6);
	}
	pidp = strrchr(procname, '/')+1;
	while (*pidp == '0' && *(pidp+1) != '\0')
		pidp++;

	/*
	 * If run as super-user, this will succeed even if
	 * someone else has the process open for writing.
	 */
	(void) alarm(2);
	if ((fd = open(procname, O_RDWR)) >= 0) {
#ifdef SIGCONT
		int sig = SIGCONT;
		if (ioctl(fd, PIOCKILL, (int)&sig) == -1 && errno == EBUSY)
			errno = 0;
#endif
		if (ioctl(fd, PIOCRUN, 0) == -1 && errno == EBUSY)
			errno = 0;
		(void) close(fd);
	}
	(void) alarm(0);

	return perr(NULL);
}

static int
perr(s)
char *s;
{
	if (errno == 0)
		return 0;
	if (s)
		(void) fprintf(stderr, "%s: ", procname);
	else
		s = procname;
	perror(s);
	return 1;
}
