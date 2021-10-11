#ident	"@(#)pmap.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <link.h>
#include <libelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>
#include "dynlib.h"

#define	TRUE	1
#define	FALSE	0

/* obsolete flags */
#ifndef	MA_BREAK
#	define	MA_BREAK	0
#endif
#ifndef	MA_STACK
#	define	MA_STACK	0
#endif

static	int	look();
static	int	perr();
static	char *	mflags();
static	void	print_extra(caddr_t, u_long, char *);

static 	int	all = 0;
static	char	procname[64];

static	u_long	pagesize;

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
		(void) fprintf(stderr, "  (report process address maps)\n");
		return 2;
	}

	if (argc > 1 && strcmp(argv[1], "-a") == 0) {
		all = 1;
		argc--;
		argv++;
	}

	/*
	 * Some common places where objects are found.
	 * (mostly for ld.so.1)
	 */
	load_lib_dir("/usr/lib");
	load_lib_dir("/etc/lib");

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
	prmap_t * prmapp = NULL;
	register prmap_t * pmp;
	int nmap;
	int n;
	char *s;
	register int i;
	prstatus_t prstatus;
	prpsinfo_t psinfo;
	int heap_done = FALSE;
	int stack_done = FALSE;

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

	if (ioctl(fd, PIOCNMAP, (int)&n) < 0)
		return perr("PIOCNMAP");

again:
	nmap = n;
	if (prmapp != NULL)
		free((char *)prmapp);
	prmapp = (prmap_t *)malloc((nmap+1)*sizeof(prmap_t));

	/* we may have slept in malloc(); see if nmap has changed */
	if (ioctl(fd, PIOCNMAP, (int)&n) != 0)
		return perr("PIOCNMAP");
	if (nmap != n)			/* unlikely */
		goto again;

	if (ioctl(fd, PIOCMAP, (int)prmapp) != 0)
		return perr("PIOCMAP");

	pagesize = prmapp->pr_pagesize;

	if (ioctl(fd, PIOCSTATUS, (int)&prstatus) < 0)
		return perr("PIOCSTATUS");

	if (ioctl(fd, PIOCPSINFO, (int)&psinfo) < 0)
		return perr("PIOCPSINFO");

	(void) printf("%s:\t%.70s\n", pidp, psinfo.pr_psargs);

	if (prstatus.pr_flags & PR_ISSYS)
		goto out;

	if ((s = strchr(psinfo.pr_psargs, ' ')) != NULL)
		*s = '\0';
	make_exec_name(psinfo.pr_psargs);
	load_ldd_names(fd);

	for (i = 0, pmp = &prmapp[0]; i < nmap; i++, pmp++) {
		int mfd;
		char * lname = NULL;

		if (!heap_done && pmp->pr_vaddr > prstatus.pr_brkbase
		 && prstatus.pr_brksize != 0) {
			heap_done = TRUE;
			print_extra(prstatus.pr_brkbase, prstatus.pr_brksize,
				"heap");
		}
		if (!stack_done && pmp->pr_vaddr > prstatus.pr_stkbase
		 && prstatus.pr_stksize != 0) {
			stack_done = TRUE;
			print_extra(prstatus.pr_stkbase, prstatus.pr_stksize,
				"stack");
		}
		if ((mfd = ioctl(fd, PIOCOPENM, (int)&pmp->pr_vaddr)) >= 0) {
			struct stat statb;
			if (fstat(mfd, &statb) == 0)
				lname = lookup_file(statb.st_dev, statb.st_ino);
			(void) close(mfd);
		}
		pmp->pr_mflags &= ~(MA_BREAK|MA_STACK);
		(void) printf(
			lname?
			    "%.8X%5dK %-18s %s\n" :
			    "%.8X%5dK %s\n",
			pmp->pr_vaddr,
			(pmp->pr_size+1023)/1024,
			mflags(pmp->pr_mflags),
			lname);
	}

	if (!heap_done && prstatus.pr_brksize != 0)
		print_extra(prstatus.pr_brkbase, prstatus.pr_brksize, "heap");
	if (!stack_done && prstatus.pr_stksize != 0)
		print_extra(prstatus.pr_stkbase, prstatus.pr_stksize, "stack");

out:
	(void) close(fd);

	if (prmapp != NULL)
		free((char *)prmapp);

	return 0;
}

static void
print_extra(base, size, name)
	caddr_t	base;
	u_long	size;
	char *	name;
{
	caddr_t xbase = (caddr_t)(((u_long)base+pagesize-1) & ~(pagesize-1));
	u_long xsize = ((((u_long)base+size+pagesize-1) & ~(pagesize-1))
			- (u_long)xbase);

	(void) printf("%.8X%5dK     [ %s ]\n",
		xbase, xsize/1024, name);
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
mflags(arg)
register int arg;
{
	static char code_buf[80];
	register char * str = code_buf;

	if (arg == 0)
		return("-");

	if (arg & ~(MA_READ|MA_WRITE|MA_EXEC|MA_SHARED))
		(void) sprintf(str, "0x%.x",
			arg & ~(MA_READ|MA_WRITE|MA_EXEC|MA_SHARED));
	else
		*str = '\0';

	if (arg & MA_READ)
		(void) strcat(str, "/read");
	if (arg & MA_WRITE)
		(void) strcat(str, "/write");
	if (arg & MA_EXEC)
		(void) strcat(str, "/exec");
	if (arg & MA_SHARED)
		(void) strcat(str, "/shared");

	if (*str == '/')
		str++;

	return str;
}
