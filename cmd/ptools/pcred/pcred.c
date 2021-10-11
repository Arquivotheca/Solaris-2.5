#ident	"@(#)pcred.c	1.1	94/11/10 SMI"

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

static	int	look();
static	int	perr();

static 	int	all = 0;
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
		(void) fprintf(stderr, "  (report process credentials)\n");
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
	char * pidp;
	prcred_t prcred;
	int grp;
	uid_t * group = NULL;
	unsigned ngroups;

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

	if (ioctl(fd, PIOCCRED, (int)&prcred) < 0)
		return perr("PIOCCRED");

again:
	ngroups = prcred.pr_ngroups;
	if (group != NULL)
		free((char *)group);
	group = (uid_t *)malloc((ngroups+1)*sizeof(uid_t));

	/* we may have slept in malloc(); see if ngroups has changed */
	if (ioctl(fd, PIOCCRED, (int)&prcred) < 0)
		return perr("PIOCCRED");
	if (ngroups != prcred.pr_ngroups)	/* unlikely */
		goto again;

	if (ioctl(fd, PIOCGROUPS, (int)group) < 0)
		return perr("PIOCGROUPS");

	(void) close(fd);

	(void) printf("%s:\t", pidp);

	if (!all && prcred.pr_ruid == prcred.pr_suid)
		(void) printf("e/r/suid=%d  ",
			prcred.pr_euid);
	else
		(void) printf("euid=%d ruid=%d suid=%d  ",
			prcred.pr_euid,
			prcred.pr_ruid,
			prcred.pr_suid);

	if (!all && prcred.pr_rgid == prcred.pr_sgid)
		(void) printf("e/r/sgid=%d\n",
			prcred.pr_egid);
	else
		(void) printf("egid=%d rgid=%d sgid=%d\n",
			prcred.pr_egid,
			prcred.pr_rgid,
			prcred.pr_sgid);

	if (ngroups != 0
	 && (all || ngroups != 1 || group[0] != prcred.pr_rgid)) {
		(void) printf("\tgroups:");
		for (grp = 0; grp < ngroups; grp++)
			(void) printf(" %d", group[grp]);
		(void) printf("\n");
	}

	if (group != NULL)
		free((char *)group);

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
