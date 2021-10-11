/*
 *	Copyright (c) 1994 Sun Microsystems
 */

#ident	"@(#)utmpd.c 1.6	95/05/04 SMI"

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS CONTAINS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * utmpd	- utmp daemon
 *
 *		This program receives requests from pututline(3) & pututxline(3)
 *		via a named pipe to watch the process to make sure it cleans up
 *		its utmp entry on termination. The program keeps a list of procs
 *		and uses poll() on their /proc files to detect termination.
 *		Also the  program periodically scans the /etc/utmp[x] files for
 *		processes that aren't in the table so they can be watched.
 *
 *		If utmpd doesn't hear back over the pipe from pututline(3) that
 *		the process has removed its entry it cleans the entry when the
 *		the process terminates.
 *		The AT&T Copyright above is there since we borrowed the pipe
 *		mechanism from init(1m).
 */


#include	<sys/types.h>
#include	<signal.h>
#include	<stdio.h>
#include	<unistd.h>
#include	<utmpx.h>
#include	<errno.h>
#include	<termio.h>
#include	<sys/termios.h>
#include	<sys/tty.h>
#include	<ctype.h>
#include	<sys/stat.h>
#include	<sys/statvfs.h>
#include	<fcntl.h>
#include	<time.h>
#include	<sys/stropts.h>
#include	<wait.h>
#include	<syslog.h>
#include	<stdlib.h>
#include	<string.h>
#include	<poll.h>
#include	<deflt.h>
#include	<sys/procfs.h>
#include	<sys/resource.h>

#define	dprintf(x)	if (Debug) (void) printf x

/*
 * Memory allocation keyed off MAX_FDS
 */
#define	MAX_FDS		4064	/* Maximum # file descriptors */
#define	EXTRA_MARGIN	32	/* Allocate this many more FDS over Max_Fds */
/*
 * MAX_POLLNV & RESETS - paranoia to cover an error case that might not exist
 */
#define	MAX_POLL_ERRS	1024	/* Count of bad errors */
#define	MAX_RESETS	1024	/* Maximum times to reload tables */
#define	POLL_TIMEOUT	300	/* Default Timeout for poll() in seconds */
#define	CLEANIT		1	/* Used by rem_pid() */
#define	DONT_CLEAN	0	/* Used by rem_pid() */
#define	UTMP_DEFAULT	"/etc/default/utmpd"


/*
 * The pidrec structure describes the data shipped down the pipe to
 * us from the pututline() and pututxline() libraries in
 * lib/libc/port/gen/getut[x].c
 */

/*
 * pd_type's
 */
#define	ADDPID  1
#define	REMPID  2

struct  pidrec {
	int	pd_type;		/* Command type */
	pid_t	pd_pid;			/* pid to add or remove */
};


/*
 * Since this program uses poll(2) and poll takes an array of file descriptors
 * as an argument we maintain our data in tables.
 * One table is the file descriptor array for poll, another parallel
 * array is a table which contains the process ID of the corresponding
 * open fd.  These tables are kept sorted by process ID for quick lookups.
 */

struct  pidentry {
	pid_t	pl_pid;			/* pid to watch for */
	int 	pl_status;		/* Exit status of proc */
};

static struct pidentry *pidtable = NULL;

static pollfd_t *fdtable = NULL;

static int	pidcnt = 0;		/* Number of procs being watched */
static char	*prog_name;		/* To save the invocation name away */
static char	*UTMPPIPE_DIR =	"/etc";
static char	*UTMPPIPE = "/etc/utmppipe";
static int	Pfd = -1;		/* File descriptor of named pipe */
static int 	Poll_timeout = POLL_TIMEOUT;
static int	Debug = 0;		/* Set by command line argument */
static int	Max_fds		= MAX_FDS;

/*
 * This program has three main components plus utilities and debug routines
 *	Receiver - receives the process ID or process for us to watch.
 *		   (Uses a named pipe to get messages)
 *	Watcher	 - Use poll(2) to watch for processes to die so they
 *		   can be cleaned up (get marked as DEAD_PROCESS)
 *	Scanner  - periodically scans the utmp files for stale entries
 *		   or live entries that we don't know about.
 */

static int wait_for_pids();	/* Watcher - uses poll */
static void scan_utmps();	/* Scanner, reads utmp files */
static void drain_pipe();	/* Receiver - reads mesgs over UTMPPIPE */
static void setup_pipe();	/* For setting up receiver */

static void add_pid();		/* Adds a process to the table */
static void rem_pid();		/* Removes a process from the table */
static int find_pid();		/* Finds a process in the table */
static int proc_to_fd();	/* Takes a pid and returns an fd for its proc */
static void load_tables();	/* Loads up the tables the first time around */
static int pidcmp();		/* For sorting pids */

static void clean_entry();	/* Removes entry from our table and calls ... */
static void clean_utmpx_ent();	/* Cleans a utmpx entry */
static void clean_utmp_ent();	/* Cleans utmp entry */

static void fatal();		/* Prints error message and calls exit */
static void nonfatal();		/* Prints error message */
static void print_tables();	/* Prints out internal tables for Debug */
static int proc_is_alive(pid_t pid);	/* Check if a process is alive */

static void my_pututline(struct utmp *u);	/* For writing to /etc/utmp */
static struct utmp * my_getutent();		/* For reading "          " */
static void my_setutent();			/* For opening "          " */
static void my_endutent();			/* For closing "          " */

extern int defopen();

/*
 * main()	- Main does basic setup and calls wait_for_pids() to do the work
 */

void
main(argc, argv)
	char **argv;
{
	char *defp;
	struct rlimit rlim;
	int i;

	prog_name = argv[0];			/* Save invocation name */

	if (getuid() != 0)  {
		(void) fprintf(stderr,
			"You must be root to run this program\n");
		fatal("You must be root to run this program");
	}

	if (argc > 1) {
		if ((argc == 2 && (int) strlen(argv[1]) >= 2) &&
		    (argv[1][0] == '-' && argv[1][1] == 'd')) {
			Debug = 1;
		} else {
			(void) fprintf(stderr,
				"%s: Wrong number of arguments\n", prog_name);
			(void) fprintf(stderr,
				"Usage: %s [-debug]\n", prog_name);
			exit(-1);
		}
	}

	/*
	 * Read defaults file for poll timeout
	 */
	if (defopen(UTMP_DEFAULT) == 0) {
		if ((defp = defread("SCAN_PERIOD=")) != NULL) {
			Poll_timeout = atol(defp);
			dprintf(("Poll timeout set to %d\n", Poll_timeout));
		}
		/*
		 * Paranoia - if polling on large number of FDs is expensive /
		 * buggy the number can be set lower in the field.
		 */
		if ((defp = defread("MAX_FDS=")) != NULL) {
			Max_fds = atol(defp);
			dprintf(("Max_fds set to %d\n", Max_fds));
		}
		(void) defopen((char *)NULL);
	}

	if (Debug == 0) {
		/*
		 * Daemonize ourselves
		 */
		if (fork()) {
			exit(0);
		}
		(void) close(0);
		(void) close(1);
		(void) close(2);
		/*
		 * We open these to avoid accidentally writing to a proc file
		 */
		(void) open("/dev/null", O_RDONLY);
		(void) open("/dev/null", O_WRONLY);
		(void) open("/dev/null", O_WRONLY);
		(void) setsid();		/* release process from tty */
	}

	openlog(prog_name, LOG_PID, LOG_DAEMON);	/* For error messages */


	/*
	 * Allocate the pidtable and fdtable.  An earlier version did
	 * this as we go, but this is simpler.
	 */
	if ((pidtable = malloc(Max_fds * sizeof (struct pidentry))) == NULL)
		fatal("Malloc failed");
	if ((fdtable = malloc(Max_fds * sizeof (pollfd_t))) == NULL)
		fatal("Malloc failed");

	/*
	 * Up the limit on FDs
	 */
	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		rlim.rlim_cur = Max_fds + EXTRA_MARGIN + 1;
		rlim.rlim_max = Max_fds + EXTRA_MARGIN + 1;
		if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
			fatal("Out of File Descriptors");
		}
	} else
		fatal("getrlimit returned failure");


	/*
	 * Loop here scanning the utmp files and waiting for processes
	 * to terminate.  Most of the activity is directed out of wait_for_pids.
	 * If wait_for_pids fails we reload the table and try again.
	 */

	for (i = 0; i < MAX_RESETS; i++) {
		load_tables();
		while (wait_for_pids() == 1)
			;
	}
	/*
	 * We only get here if we had a bunch of resets - so give up
	 */
	fatal("Too many resets, giving up");
}

/*
 * load_tables()	- Designed to be called repeatedly if we need to
 *			  restart things.  Zeros the pidcount, and loads
 *			  the tables by scanning utmp
 */

static void
load_tables()
{
	int i;

	dprintf(("Load tables\n"));

	/*
	 * Close any open files.
	 */
	for (i = 0; i < pidcnt; i++)
		(void) close(fdtable[i].fd);

	pidcnt = 0;
	setup_pipe();		/* Setup the pipe to receive messages */
	scan_utmps();		/* Read in USER procs entries to watch */
}


/*
 *			*** The Watcher ***
 *
 * Wait_for_pids	- wait for the termination of a process in the table.
 *			  Returns 1 on normal exist, 0 on failure.
 */

static int
wait_for_pids()
{
	register struct pollfd *pfd;
	register int i;
	pid_t pid;
	int ret_val;
	int timeout;
	static time_t last_timeout  = 0;
	static int bad_error  = 0;	/* Count of POLL errors */

	/*
	 * First time through we initialize last_timeout to now.
	 */
	if (last_timeout == 0)
		last_timeout = time(NULL);

	/*
	 * Recalculate timeout - checking to see if time expired.
	 */

	if ((timeout = Poll_timeout - (time(NULL) - last_timeout)) <= 0) {
		timeout = Poll_timeout;
		last_timeout = time(NULL);
		scan_utmps();
	}

	/*
	 * Loop here while getting EAGAIN
	 */
	fdtable[0].events = POLLRDNORM;
	while ((ret_val = poll(fdtable, pidcnt, timeout*1000)) < 0) {
		if (errno == EAGAIN)
			(void) sleep(2);
		else
			fatal("poll");
	}

	/*
	 * If ret_val == 0 the poll timed out - reset last_time and
	 * call scan_utmps
	 */
	if (ret_val == 0) {
		last_timeout = time(NULL);
		scan_utmps();
		return (1);
	}

	/*
	 * Check the pipe file descriptor
	 */
	if (fdtable[0].revents & POLLRDNORM) {
		drain_pipe();
		fdtable[0].revents = 0;
		ret_val--;
	}

	(void) sleep(5);	/* Give parents time to cleanup children */

	/*
	 * We got here because the status of one of the pids that
	 * we are polling on has changed, so search the table looking
	 * for the entry.
	 *
	 * The table is scanned backwards so that entries can be removed
	 * while we go since the table is compacted from high down to low
	 */
	for (i = pidcnt - 1; i > 0; i--) {
		/*
		 * Break out of the loop if we've processed all the entries.
		 */
		if (ret_val == 0)
			break;

		pfd = &fdtable[i];

		if (pfd->fd < 0) {
			rem_pid((pid_t)0, i, DONT_CLEAN);
			continue;
		}
		/*
		 * POLLHUP	- Process terminated
		 */
		if (pfd->revents & POLLHUP) {
			prpsinfo_t psinfo;

			if (ioctl(pfd->fd, PIOCPSINFO,
			    (int)&psinfo) == 0) {
				dprintf(("! %d: terminated, status 0x%.4x\n", \
				(int) pidtable[i].pl_pid, psinfo.pr_wstat));
				pidtable[i].pl_status = psinfo.pr_wstat;

			} else {
				dprintf(("! %d: terminated\n", \
						(int) pidtable[i].pl_pid));
				pidtable[i].pl_status = 0;
			}
			/*
			 * PID gets removed when terminated only
			 */
			rem_pid((pid_t)0, i, CLEANIT);
			ret_val--;
			continue;
		}
		/*
		 * POLLNVAL and POLLERR
		 *	These error's shouldn't occurr but until their fixed
		 *	we perform some simple error recovery.
		 */
		if (pfd->revents & (POLLNVAL|POLLERR)) {
			dprintf(("Poll Err = %d pid = %d i = %d\n", \
				pfd->revents, \
				(int) pidtable[i].pl_pid, i));


			pid = pidtable[i].pl_pid; /* Save pid for below */
			/*
			 * If its POLLNVAL we just remove the process for
			 * now, it will get picked up in the next scan.
			 * POLLERR pids get re-added after being deleted.
			 */
			if (pfd->revents & POLLNVAL) {
				rem_pid((pid_t) 0, i, DONT_CLEAN);
			} else {			/* Else... POLLERR */
				rem_pid((pid_t) 0, i, DONT_CLEAN);
				add_pid(pid);
			}

			if (bad_error++ > MAX_POLL_ERRS) {
				bad_error = 0;
				return (0);	/* 0 Indicates severe error */
			}
			ret_val--;
			continue;
		}

		/*
		 * No more bits should be set in revents but check anyway
		 */
		if (pfd->revents != 0) {
			dprintf(("%d: unknown err %d\n", \
			    (int)pidtable[i].pl_pid, pfd->revents));

			rem_pid((pid_t)0, i, DONT_CLEAN);
			ret_val--;

			if (bad_error++ > MAX_POLL_ERRS) {
				bad_error = 0;
				return (0);	/* 0 Indicates severe error */
			}
			return (1);
		}
	}
	return (1);			/* 1 Indicates Everything okay */
}

/*
 *		*** The Scanner ***
 *
 * scan_utmps()		- Scan the utmpx and utmp files.
 *			  For each USER_PROCESS check
 *			  if its alive or dead.  If alive and its not in
 *			  our table to be watched, put it there.  If its
 *			  dead, remove it from our table and clean it up.
 */

static void
scan_utmps()
{
	struct	utmp	*utmp;
	struct	utmpx	*utmpx;
	int	i;

	dprintf(("Scan utmps\n"));
	/*
	 * Scan utmpx.
	 */
	setutxent();
	while ((utmpx = getutxent()) != NULL) {
		if (utmpx->ut_type == USER_PROCESS) {
			/*
			 * Is the process alive?
			 */
			if (proc_is_alive(utmpx->ut_pid)) {
				/*
				 * Yes, the process is alive, so add it if we
				 * don't have it in our table.
				 */
				if (find_pid(utmpx->ut_pid, &i) == 0)
					add_pid(utmpx->ut_pid);	/* No, add it */
			} else {
				/*
				 * No, the process is dead, so remove it if its
				 * in our table, otherwise just clean it.
				 */
				if (find_pid(utmpx->ut_pid, &i) == 1)
					rem_pid(utmpx->ut_pid, i, CLEANIT);
				else
					clean_utmpx_ent(utmpx);
			}
		}
	}
	/*
	 * Close it to flush the buffer.
	 */
	endutxent();

	/*
	 * Scan utmp	- Same logic as above, keep logic in synch.
	 */
	my_setutent();
	while ((utmp = my_getutent()) != NULL) {
		if (utmp->ut_type == USER_PROCESS) {
			if (proc_is_alive(utmp->ut_pid)) {
				if (find_pid((pid_t) utmp->ut_pid, &i) == 0)
					add_pid((pid_t) utmp->ut_pid);
			} else {
				if (find_pid((pid_t) utmp->ut_pid, &i) == 1)
					rem_pid((pid_t)utmp->ut_pid,
					    i, CLEANIT);
				else
					clean_utmp_ent(utmp);
			}
		}
	}
	my_endutent();
}


/*
 *			*** Receiver Routines ***
 */

/*
 * setup_pipe	- Set up the pipe to read pids over
 */

static void
setup_pipe()
{

	struct statvfs statvfs_buf;
	/*
	 * This code & comments swiped from init and left stock since it works
	 */

	if (Pfd < 0) {
		if ((statvfs(UTMPPIPE_DIR, &statvfs_buf) == 0) &&
		    ((statvfs_buf.f_flag & ST_RDONLY) == 0)) {
			(void) unlink(UTMPPIPE);
			(void) mknod(UTMPPIPE, S_IFIFO | 0600, 0);
		}
		Pfd = open(UTMPPIPE, O_RDWR | O_NDELAY);
	}
	if (Pfd < 0)
		nonfatal(UTMPPIPE);
	/*
	 * This code from init modified to be poll based instead of SIGPOLL,
	 * signal based.
	 */

	if (Pfd >= 0) {
		/*
		 * Read pipe in message discard mode.  When read reads a
		 * pidrec size record, the remainder of the message will
		 * be discarded.  Though there shouldn't be any it will
		 * help resynch if someone else wrote some garbage.
		 */
		(void) ioctl(Pfd, I_SRDOPT, RMSGD);
	}

	/*
	 * My code.  We use slot 0 in the table to hold the fd of the pipe
	 */
	add_pid(0);			/* Proc 0 guaranteed to get slot 0 */
	fdtable[0].fd = Pfd;		/* Pfd could be -1, should be okay */
	fdtable[0].events = POLLRDNORM;
}

/*
 * drain_pipe()		- The receiver routine that reads the pipe
 */

static void
drain_pipe()
{
	struct pidrec prec;
	register struct pidrec *p = &prec;
	int bytes_read;
	int i;

	for (;;) {
		/*
		 * Important Note: Either read will really fail (in which case
		 * return is all we can do) or will get EAGAIN (Pfd was opened
		 * O_NDELAY), in which case we also want to return.
		 */

		if ((bytes_read = read(Pfd, p, sizeof (struct pidrec))) !=
		    sizeof (struct pidrec))  {
			/*
			 * Something went wrong reading, so read until pipe
			 * is empty
			 */
			if (bytes_read > 0)
				while (read(Pfd, p, sizeof (struct pidrec)) > 0)
					;
			return;
		}

		dprintf(("drain_pipe: Recd command %d, pid %d\n",
			p->pd_type, (int) p->pd_pid));
		switch (p->pd_type) {
		case ADDPID:
			/*
			 * Check if we already have the process, adding it
			 * if we don't.
			 */
			if (find_pid(p->pd_pid, &i) == 0)
				add_pid(p->pd_pid);
			break;

		case REMPID:
			rem_pid(p->pd_pid, -1, DONT_CLEAN);
			break;
		default:
			nonfatal("Bad message on utmppipe\n");
				break;
		}
	}
}


/*
 *		*** Utilities for add and removing entries in the tables ***
 */

/*
 * add_pid	- add a pid to the fd table and the pidtable.
 *		  these tables are sorted tables for quick lookups.
 *
 */
static void
add_pid(pid)
	pid_t pid;
{
	int fd = 0;
	int i = 0, move_amt;
	int j;
	static int first_time = 1;

	/*
	 * Check to see if the pid is already in our table, or being passed
	 * pid zero.
	 */
	if (pidcnt != 0 && (find_pid(pid, &j) == 1 || pid == 0))
		return;

	if (pidcnt >= Max_fds) {
		if (first_time == 1) {
			/*
			 * Print this error only once
			 */
			nonfatal("File Descriptor limit exceeded");
			first_time = 0;
		}
		return;
	}
	/*
	 * Open the /proc file checking if there's still a valid proc file.
	 */
	if (pid != 0 && (fd = proc_to_fd(pid)) == -1) {
		/*
		 * No so the process died before we got to watch for him
		 */
		return;
	}

	/*
	 * We only do this code if we're not putting in the first element
	 * Which we know will be for proc zero which is used by setup_pipe
	 * for its pipe fd.
	 */
	if (pidcnt != 0) {
		for (i = 0; i < pidcnt; i++) {
			if (pid <= pidtable[i].pl_pid)
				break;
		}

		/*
		 * Handle the case where we're not sticking our entry on the
		 * the end, or overwriting an existing entry.
		 */
		if (i != pidcnt && pid != pidtable[i].pl_pid) {

			move_amt = pidcnt - i;
			/*
			 * Move table down
			 */
			if (move_amt != 0) {
				(void) memmove(&pidtable[i+1], &pidtable[i],
					move_amt * sizeof (struct pidentry));
				(void) memmove(&fdtable[i+1], &fdtable[i],
					move_amt * sizeof (pollfd_t));
			}
		}
	}

	/*
	 * Fill in the events field for poll and copy the entry into the array
	 */
	fdtable[i].events = 0;
	fdtable[i].revents = 0;
	fdtable[i].fd = fd;

	/*
	 * Likewise, setup pid field and pointer (index) to the fdtable entry
	 */
	pidtable[i].pl_pid = pid;

	pidcnt++;			/* Bump the pid count */
	dprintf(("  add_pid: pid = %d fd = %d index = %d pidcnt = %d\n",
		(int) pid, fd, i, pidcnt));
}


/*
 * rem_pid	- Remove an entry from the table and check to see if its
 *		  not in the utmp file.
 *		  If i != -1 don't look up the pid, use i as index
 */

static void
rem_pid(pid, i, clean_it)
	pid_t pid;	/* Pid of process to clean or 0 if we don't know it */
	int i;		/* Index into table or -1 if we need to look it up */
	int clean_it;	/* Clean the entry, or just remove from table? */
{
	int move_amt;

	dprintf(("  rem_pid: pid = %d i = %d", (int) pid, i));

	/*
	 * Don't allow slot 0 in the table to be removed - utmppipe fd
	 */
	if ((i == -1 && pid == 0) || (i == 0))	{
		dprintf((" - attempted to remove proc 0\n"));
		return;
	}

	if (i != -1 || find_pid(pid, &i) == 1) {	/* Found the entry */
		(void) close(fdtable[i].fd);	/* We're done with the fd */

		dprintf((" fd = %d\n", fdtable[i].fd));

		if (clean_it == CLEANIT)
			clean_entry(i);

		move_amt = (pidcnt - i) - 1;
		/*
		 * Remove entries from the tables.
		 */
		(void) memmove(&pidtable[i], &pidtable[i+1],
			move_amt * sizeof (struct pidentry));

		(void) memmove(&fdtable[i], &fdtable[i+1],
			move_amt * sizeof (pollfd_t));

		/*
		 * decrement the pid count - one less pid to worry about
		 */
		pidcnt--;
	}
	if (i == -1)
		dprintf((" - entry not found \n"));
}


/*
 * find_pid	- Returns an index into the pidtable of the specifed pid,
 *		  else -1 if not found
 */

static int
find_pid(pid, i)
	pid_t pid;
	int *i;
{
	struct pidentry pe;
	struct pidentry *p;

	pe.pl_pid = pid;
	p = bsearch(&pe, pidtable, pidcnt, sizeof (struct pidentry), pidcmp);

	if (p == NULL)
		return (0);
	else {
		*i = p - (struct pidentry *) pidtable;
		return (1);
	}
}


/*
 * Pidcmp - Used by besearch for sorting and finding  process IDs.
 */

static int
pidcmp(a, b)
	struct pidentry *a, *b;
{
	if (b == NULL || a == NULL)
		return (0);
	return (a->pl_pid - b->pl_pid);
}


/*
 * proc_to_fd	- Take a process ID and return an open file descriptor to the
 *		  /proc file for the specified process.
 */
static int
proc_to_fd(pid)
	pid_t pid;
{
	char procname[64];
	int fd, dfd;

	(void) sprintf((char *)procname, "/proc/%d", (int) pid);

	if ((fd = open(procname, O_RDONLY)) >= 0) {
		/*
		 * dup the fd above the low order values to assure
		 * stdio works for other fds - paranoia.
		 */
		if (fd < EXTRA_MARGIN) {
			dfd = fcntl(fd, F_DUPFD, EXTRA_MARGIN);
			if (dfd > 0) {
				(void) close(fd);
				fd = dfd;
			}
		}
		/*
		 * More paranoia - set the close on exec flag
		 */
		(void) fcntl(fd, F_SETFD, 1);
		return (fd);
	}
	if (errno == ENOENT)
		return (-1);

	if (errno == EMFILE) {
		/*
		 * This is fatal, since libc won't be able to allocate
		 * any fds for the pututxline() routines
		 */
		fatal("Out of file descriptors");
	}
	fatal(procname);		/* Only get here on error */
	return (-1);
}


/*
 *		*** Utmp Cleaning Utilities ***
 */

/*
 * Clean_entry	- Cleans the specified entry - where i is an index
 *		  into the pid_table.
 */
static void
clean_entry(i)
	int i;
{
	struct utmpx *u;

	if (pidcnt == 0)
		return;

	dprintf(("    Cleaning %d\n", (int) pidtable[i].pl_pid));

	/*
	 * Double check if the process is dead.
	 */
	if (proc_is_alive(pidtable[i].pl_pid)) {
		dprintf(("      Bad attempt to clean %d\n", \
			(int) pidtable[i].pl_pid));
		return;
	}

	/*
	 * Find the entry that corresponds to this pid.
	 * Do nothing if entry not found in utmp file.
	 */
	setutxent();
	while ((u = getutxent()) != NULL) {
		if (u->ut_pid == pidtable[i].pl_pid) {
			if (u->ut_type == USER_PROCESS) {
				/*
				 * Writing to utmpx will update utmp
				 */
				clean_utmpx_ent(u);
			}
		}
	}
	endutxent();
}


/*
 * clean_utmpx_ent	- Clean a utmpX entry
 */

static void
clean_utmpx_ent(u)
	struct utmpx *u;
{
	dprintf(("      clean_utmpx_ent: %d\n", (int) u->ut_pid));
	u->ut_type = DEAD_PROCESS;
	(void) time(&u->ut_xtime);
	(void) pututxline(u);
	/*
	 * This will update both wtmp & wtmpx files with data from utmpx
	 * struct, so it is not needed in clean_utmp_ent(). It is placed
	 * here because this is called before clean_utmp_ent().
	 *
	 */
	updwtmpx(WTMPX_FILE, u);
	/*
	 * XXX update wtmp for ! nonuser entries?
	 */
}

/*
 * clean_utmp_ent	- Clean a utmp entry
 */

static void
clean_utmp_ent(u)
	struct utmp *u;
{
	dprintf(("      clean_utmp_ent: %d\n", u->ut_pid));
	u->ut_type = DEAD_PROCESS;
	(void) time(&u->ut_time);
	(void) my_pututline(u);
}

/*
 *		*** Error Handling and Debugging Routines ***
 */

/*
 * fatal - Catastrophic failure
 */

static void
fatal(char *str)
{
	int oerrno = errno;

	syslog(LOG_ALERT, str);
	if (Debug == 1) {
		if ((errno = oerrno) != 0)
			perror(prog_name);
		dprintf(("%s\n", str));
	}
	exit(-1);
}

/*
 * nonfatal - Non-Catastrophic failure - print message and errno
 */

static void
nonfatal(char *str)
{
	syslog(LOG_WARNING, str);

	if (Debug == 1) {
		if (errno != 0)
			perror(prog_name);
		dprintf(("%c%s\n", 7, str));
		print_tables();
		(void) sleep(5);	/* Time to read debug messages */
	}
}

/*
 * print_tables	- Print internal tables - for debugging
 */

static void
print_tables()
{
	int i;

	if (Debug == 0)
		return;

	dprintf(("pidtable: "));
	for (i = 0; i < pidcnt; i++)
		dprintf(("%d: %d  ", i, (int) pidtable[i].pl_pid));
	dprintf(("\n"));
	dprintf(("fdtable:  "));
	for (i = 0; i < pidcnt; i++)
		dprintf(("%d: %d  ", i, fdtable[i].fd));
	dprintf(("\n"));
}

/*
 * proc_is_alive	- Check to see if a process is alive AND its
 *			  not a zombie.  Returns 1 if process is alive
 *			  and zero if it is dead or a zombie.
 */

static int
proc_is_alive(pid)
	pid_t pid;
{
	int fd;
	prstatus_t prstatus;

	if (kill(pid, 0) == 0) {
		/*
		 * A process exists, so check if its a zombie
		 */
		if ((fd = proc_to_fd(pid)) == -1 ||
		    ioctl(fd, PIOCSTATUS, &prstatus) == -1) {
			/*
			 * We either couldn't open the proc, or we did
			 * but PIOSTATUS failed so pid is a zombie so
			 * close the fd and return FALSE
			 */
			if (fd != -1)
				(void) close(fd);
			return (0);
		} else {
			(void) close(fd);
			return (1); 	/* Open and PIOCSTATUS okay */
		}
	} else
		return (0);		/* Kill failed - no process */
}

/*
 * my_pututline, my_getutent, my_setutent, my_endutent
 *
 * These routines were created to get around the problem of the
 * pututline family of routines not writing where they are
 * supposed to write.  If /etc/utmp is ever removed, you can yank out
 * the use of these things all together.
 */

static int utfd = -1;

/*
 * my_pututline	- backs up by one
 */
static void
my_pututline(struct utmp *u)
{
	if (utfd == -1)
		my_setutent();
	(void) lseek(utfd, - (long) (sizeof (struct utmp)), SEEK_CUR);
	if (write(utfd, u, sizeof (struct utmp)) != sizeof (struct utmp))
		fatal("Can't write to /etc/utmp");
}

static struct utmp *
my_getutent()
{
	static struct utmp ut_buf;

	if (utfd == -1)
		my_setutent();

	if (read(utfd, &ut_buf, sizeof (ut_buf)) != sizeof ut_buf) {
		/*
		 * Make sure ubuf is zeroed.
		 */
		(void) memset(&ut_buf, 0, sizeof (struct utmp));
		return (NULL);
	}
	(void) lseek(utfd, (long) 0, SEEK_CUR);
	return (&ut_buf);
}

static void
my_setutent()
{
	if (utfd == -1)
		if ((utfd = open("/etc/utmp", O_RDWR)) < 0)
			fatal("Can't open /etc/utmp");
	else
		(void) lseek(utfd, 0, SEEK_SET);
}

static void
my_endutent()
{
	if (utfd != -1) {
		(void) close(utfd);
		utfd = -1;
	}
}
