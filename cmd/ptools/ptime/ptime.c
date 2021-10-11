#ident	"@(#)ptime.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>

static	int	look( char * );
static	void	hr_min_sec( char * , long );
static	void	prtime( char * , timestruc_t * );
static	int	perr( const char * );

void	tsadd( timestruc_t * result , timestruc_t * a , timestruc_t * b );
void	tssub( timestruc_t * result , timestruc_t * a , timestruc_t * b );
void	tszero( timestruc_t * );
int	tsiszero( timestruc_t * );
int	tscmp( timestruc_t * a , timestruc_t * b );

static	char	procname[64];

main(argc, argv)
int argc;
char **argv;
{
	char * cmd = strrchr(argv[0], '/');
	int rc = 0;
	int pfd;
	long flags;
	pid_t pid;
	char cpid[8];
	struct siginfo info;

	if (argc <= 1) {
		if (cmd++ == NULL)
			cmd = argv[0];
		(void) fprintf(stderr,
			"usage:\t%s command [ args ... ]\n", cmd);
		(void) fprintf(stderr,
			"  (time a command using microstate accounting)\n");
		return 2;
	}

	switch (pid = fork()) {
	case -1:
		(void) fprintf(stderr, "%s: cannot fork\n", cmd);
		return 2;
	case 0:
		/* open the /proc file and turn on microstate accounting */
		(void) sprintf(procname, "/proc/%d", getpid());
		pfd = open(procname, O_RDWR);
		flags = PR_MSACCT;
		(void) ioctl(pfd, PIOCSET, &flags);
		(void) close(pfd);
		(void) execvp(argv[1], &argv[1]);
		(void) fprintf(stderr, "%s: exec failed\n", cmd);
		_exit(2);
	}

	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	(void) waitid(P_PID, pid, &info, WEXITED | WNOWAIT);

	(void) sprintf(cpid, "%d", pid);
	look(cpid);

	return rc;
}

static int
look(arg)
	char *arg;
{
	int rval = 0;
	int fd;
	int i;
	char * pidp;
	int plen;
	prpsinfo_t prpsinfo;
	prusage_t prusage;
	int error;
	int pos;
	int len;
	timestruc_t total;
	timestruc_t real, user, sys;
	register prusage_t *pup = &prusage;

	if (strchr(arg, '/') != NULL)
		(void) strncpy(procname, arg, sizeof(procname));
	else {
		(void) strcpy(procname, "/proc/");
		(void) strncat(procname, arg, sizeof(procname)-6);
	}
	pidp = strrchr(procname, '/')+1;
	while (*pidp == '0' && *(pidp+1) != '\0')
		pidp++;
	plen = strlen(pidp);

	if ((fd = open(procname, O_RDONLY)) < 0)
		return perr(NULL);
	else if (ioctl(fd, PIOCPSINFO, (int)&prpsinfo) != 0)
		rval = perr("PIOCPSINFO");
	else if (ioctl(fd, PIOCUSAGE, (int)&prusage) != 0)
		rval = perr("PIOCUSAGE");

	if (rval) {
		(void) close(fd);
		if (errno == ENOENT) {
			(void) printf("%s\t<defunct>\n", pidp);
			return 0;
		}
		return rval;
	}

	real = pup->pr_rtime;
	user = pup->pr_utime;
	sys = pup->pr_stime;
	tsadd(&sys, &sys, &pup->pr_ttime);

	(void) fprintf(stderr, "\n");
	prtime("real", &real);
	prtime("user", &user);
	prtime("sys", &sys);

	(void) close(fd);
	return 0;
}

static void
hr_min_sec(char * buf, long sec)
{
	if (sec >= 3600)
		(void) sprintf(buf, "%d:%.2d:%.2d",
			sec / 3600, (sec % 3600) / 60, sec % 60);
	else if (sec >= 60)
		(void) sprintf(buf, "%d:%.2d",
			sec / 60, sec % 60);
	else {
		(void) sprintf(buf, "%d", sec);
	}
}

static void
prtime(char *name, timestruc_t *ts)
{
	char buf[32];

	hr_min_sec(buf, ts->tv_sec);
	(void) fprintf(stderr, "%-4s %8s.%.3lu\n",
		name, buf, ts->tv_nsec/1000000);
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

void
tsadd( timestruc_t * result , timestruc_t * a , timestruc_t * b )
{
	result->tv_sec = a->tv_sec + b->tv_sec;
	if ((result->tv_nsec = a->tv_nsec + b->tv_nsec) >= 1000000000) {
		result->tv_nsec -= 1000000000;
		result->tv_sec += 1;
	}
}

void
tssub( timestruc_t * result , timestruc_t * a , timestruc_t * b )
{
	result->tv_sec = a->tv_sec - b->tv_sec;
	if ((result->tv_nsec = a->tv_nsec - b->tv_nsec) < 0) {
		result->tv_nsec += 1000000000;
		result->tv_sec -= 1;
	}
}

void
tszero( timestruc_t * a )
{
	a->tv_sec = 0;
	a->tv_nsec = 0;
}

int
tsiszero( timestruc_t * a )
{
	return (a->tv_sec == 0 && a->tv_nsec == 0);
}

int
tscmp( timestruc_t * a , timestruc_t * b )
{
	if (a->tv_sec > b->tv_sec)
		return 1;
	if (a->tv_sec < b->tv_sec)
		return -1;
	if (a->tv_nsec > b->tv_nsec)
		return 1;
	if (a->tv_nsec < b->tv_nsec)
		return -1;
	return 0;
}
