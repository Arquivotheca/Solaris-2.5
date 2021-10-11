#ident	"@(#)pldd.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <link.h>
#include <libelf.h>
#include <sys/procfs.h>
#include "dynlib.h"

static	int	look(char *);
static	void	perr(char *);

static	char	procname[64];
static	char *	command;

main(argc, argv)
	int argc;
	char **argv;
{
	int rc = 0;

	command = strrchr(argv[0], '/');
	if (command++ == NULL)
		command = argv[0];

	if (argc <= 1) {
		(void) fprintf(stderr,
			"usage:  %s pid ...\n", command);
		(void) fprintf(stderr,
			"  (report process dynamic libraries)\n");
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
	int 		i;
	int		pfd;
	prpsinfo_t	psinfo;
	char *		pidp;
	char *		name;

	/*
	 * Generate the /proc filename and a pointer to the pid portion.
	 */
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
	 * Open the process to be examined.
	 */
	if ((pfd = open(procname, O_RDONLY)) < 0) {
		perr(NULL);
		return 1;
	}
	if (ioctl(pfd, PIOCPSINFO, &psinfo) < 0) {
		perr("PIOCPSINFO");
		(void) close(pfd);
		return 1;
	}

	load_ldd_names(pfd);
	(void) close(pfd);

	(void) printf("%s:\t%.70s\n", pidp, psinfo.pr_psargs);
	for (i = 0; (name = index_name(i)) != NULL; i++)
		(void) printf("%s\n", name);

	clear_names();

	return 0;
}

static void
perr(s)
char *s;
{
	char message[100];

	if (s)
		(void) sprintf(message, "%s: %s: %s", command, procname, s);
	else
		(void) sprintf(message, "%s: %s", command, procname);
	perror(message);
}
