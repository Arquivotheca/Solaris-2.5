#ident	"@(#)pwait.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <stropts.h>
#include <poll.h>
#include <sys/procfs.h>
#include <sys/resource.h>

/* slop to account for file descriptors open at exec() */
#define	SLOP	5

main(int argc, char **argv)
{
	unsigned long remain = 0;
	struct pollfd *pollfd;
	register struct pollfd *pfd;
	struct rlimit rlim;
	register char *arg;
	register unsigned i;
	int verbose = 0;
	char *cmd;

	cmd = strrchr(argv[0], '/');
	if (cmd++ == NULL)
		cmd = argv[0];

	argc--;
	argv++;

	if (argc > 0 && strcmp(argv[0], "-v") == 0) {
		verbose = 1;
		argc--;
		argv++;
	}

	if (argc <= 0) {
		(void) fprintf(stderr, "usage:  %s [-v] pid ...\n", cmd);
		(void) fprintf(stderr, "  (wait for processes to terminate)\n");
		return (2);
	}

	/* make sure we have enough file descriptors */
	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		for (i = 3; i < rlim.rlim_cur; i++)
			(void) close(i);
		if (rlim.rlim_cur < argc+3) {
			rlim.rlim_cur = argc+3;
			if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
				(void) fprintf(stderr,
					"%s: insufficient file descriptors\n",
					cmd);
				return(2);
			}
		}
	}

	pollfd = (struct pollfd *)malloc(argc*sizeof(struct pollfd));
	if (pollfd == NULL) {
		perror("malloc");
		return (2);
	}

	for (i = 0; i < argc; i++) {
		char procname[64];

		arg = argv[i];
		if (strchr(arg, '/') != NULL)
			(void) strncpy(procname, arg, sizeof(procname));
		else {
			(void) strcpy(procname, "/proc/");
			(void) strncat(procname, arg, sizeof(procname)-6);
		}

		pfd = &pollfd[i];
		if ((pfd->fd = open(procname, O_RDONLY)) >= 0) {
			remain++;
			pfd->events = POLLPRI;	/* first time only */
			pfd->revents = 0;
		} else if (errno == ENOENT) {
			if (verbose)
				(void) printf("%s: no such process\n", arg);
		} else {
			perror(arg);
		}
	}

	while (remain != 0) {
		while (poll(pollfd, argc, INFTIM) < 0) {
			if (errno != EAGAIN) {
				perror("poll");
				return (2);
			}
			(void) sleep(2);
		}
		for (i = 0; i < argc; i++) {
			pfd = &pollfd[i];
			if (pfd->fd < 0 || (pfd->revents & ~POLLPRI) == 0) {
				/*
				 * We don't care if the process stopped.
				 * Don't check for that again.
				 */
				pfd->events = 0;
				pfd->revents = 0;
				continue;
			}

			if (verbose) {
				arg = argv[i];
				if (pfd->revents & POLLHUP) {
					prpsinfo_t psinfo;

					if (ioctl(pfd->fd, PIOCPSINFO,
					    (int)&psinfo) == 0) {
						(void) printf(
					"%s: terminated, wait status 0x%.4x\n",
							arg, psinfo.pr_wstat);
					} else {
						(void) printf(
						    "%s: terminated\n",
							arg);
					}
				}
				if (pfd->revents & POLLNVAL)
					(void) printf("%s: system process\n",
						arg);
				if (pfd->revents & ~(POLLPRI|POLLHUP|POLLNVAL))
					(void) printf("%s: unknown error\n",
						arg);
			}

			(void) close(pfd->fd);
			pfd->fd = -1;
			remain--;
		}
	}

	return 0;
}
