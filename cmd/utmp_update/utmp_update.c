/*	Copyright (c) 1993 Sun Microsystems		*/
#pragma	ident	"@(#)utmp_update.c 1.13	95/09/08 SMI"	/* */

/*
 * utmp_update		- Update the /etc/utmp & /etc/utmpx files
 *
 *			This program runs set uid root on behalf of
 *			non-privileged user programs.  Normal programs cannot
 *			write to /etc/utmp.  If a program is not running as root
 *			and it calls pututline or pututxline these these
 *			libraries will invoke this program to write to the utmp
 *			files.  For security reasons some simple checks are
 *			made before updating the entry.
 */

/*
 * Header files
 */
#include	<stdio.h>
#include	<sys/param.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<utmp.h>
#include	<utmpx.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<string.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<pwd.h>
#include	<ctype.h>

#include <sys/signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>
#include <stropts.h>

/*
 * Invocation argument definitions
 */

#define	DO_UTMP		"-u"
#define	UTMP_NARGS	10
#define	DO_UTMPX	"-x"
#define	UTMPX_NARGS	15

/*
 * Return codes
 */
#define	NORMAL_EXIT	0
#define	BAD_ARGS	1
#define	PUTUTLINE_FAILURE	2
#define	FORK_FAILURE	3
#define	SETSID_FAILURE	4
#define	ALREADY_DEAD	5
#define	ENTRY_NOTFOUND	6
#define	ILLEGAL_ARGUMENT	7

/*
 * Sizes
 */

#define	MAX_SYSLEN	256		/* From utmpx.h host length */
#define	BUF_SIZE	256

/*
 * Other defines
 */
#define	ROOT_UID	0
/*
 * Debugging support
 */
#ifdef DEBUG
#define	dprintf	printf
#define	dprintf3 printf
static void display_args();
#else /* DEBUG */
#define	dprintf(x, y)
#define	dprintf3(w, x, y, z)
#endif

/*
 * Local functions
 */

static void load_utmp_struct();
static void load_utmpx_struct();
static void usage();
static void check_utmp();
static void check_utmpx();
static int bad_hostname();

static int invalid_utmp(struct utmp * eutmp, struct utmp *rutmp);
static int invalid_utmpx(struct utmpx * eutmpx, struct utmpx *rutmpx);
static int bad_line(char *line);

void
main(argc, argv)
	char *argv[];
{
	struct utmp entry;
	struct utmp *rutmp;
	struct utmpx *rutmpx;
	struct utmpx entryx;
	char *isutmp;
	int error = 0;
	int num_args;
	int	do_utmp;	/* 1 = pututline, 0 = pututxline */
#ifdef	DEBUG
	int	debugger = 1;
#endif	/*	DEBUG	*/


	isutmp = argv[1];

#ifdef DEBUG
	printf("%d\n", getpid());
	/*	Uncomment the following for attaching with dbx(1)	*/

	/*
	 * while (debugger) ;
	 */

	display_args(argc, argv);
#endif	/*	DEBUG	*/

	/*
	 * Determine if we are being called by pututline or pututxline
	 */

	if (argc < UTMP_NARGS)
		error++;
	else if (strcmp(isutmp, DO_UTMP) == 0) {
		do_utmp = 1;
		num_args = UTMP_NARGS;
	} else if (strcmp(isutmp, DO_UTMPX) == 0) {
			do_utmp = 0;
			num_args = UTMPX_NARGS;
	} else
		error++;

	/*
	 * Print usage messsage if wrong number of arguments
	 */
	if (error || argc != num_args) {
		usage();
		exit(BAD_ARGS);
	}
	/*
	 * we should never be called by root the code in libc already
	 * updates the file for root so no need to do it here. This
	 * assumption simpilfies the rest of code since we nolonger
	 * have to do special processing for the case when we are called
	 * by root
	 *
	 */
	if (getuid() == ROOT_UID) {
		usage();
		exit(ILLEGAL_ARGUMENT);
	}
	/*
	 * Load the utmp structure, verify it for security purposes
	 * and then call the appropriate putXXX interface.
	 */

	if (do_utmp) {
		load_utmp_struct(&entry, argv);
		check_utmp(&entry);
		/*
		 * Search for matching entry by line name before put operation
		 * scan over the whole file using getutent(3C) to ensure
		 * that the line name is the same. We can not use getutline(3C)
		 * because that will return LOGIN_PROCESS and USER_PROCESS
		 * records. Also check that the entry is for either a dead
		 * process or a current process that is valid (see
		 * invalid_utmp() for details of validation criteria).
		 */
		for (rutmp = getutent(); rutmp != (struct utmp *) NULL;
						rutmp = getutent()) {

			if (strncmp(entry.ut_line, rutmp->ut_line,
						sizeof (entry.ut_line)) == 0) {

				if (rutmp->ut_type == DEAD_PROCESS) {
					break;
				}

				if (rutmp->ut_type == USER_PROCESS) {
					if (invalid_utmp(&entry, rutmp)) {
						usage();
						exit(ILLEGAL_ARGUMENT);
					} else {
						break;
					}
				}
			}
		}

		if (pututline(&entry) == (struct utmp *) NULL) {
			exit(PUTUTLINE_FAILURE);
		}
	} else {
		/*
		 * Search for matching entry by line name before put operation
		 * (scan over the whole file using getutxent(3C) to ensure
		 * that the line name is the same. We can not use getutline(3C)
		 * because that will return LOGIN_PROCESS and USER_PROCESS
		 * records. Also check that the entry is for either a dead
		 * process or a current process that is valid (see
		 * invalid_utmpx() for details of validation criteria).
		 */
		load_utmpx_struct(&entryx, argv);
		check_utmpx(&entryx);
		for (rutmpx = getutxent(); rutmpx != (struct utmpx *) NULL;
						rutmpx = getutxent()) {

			if (strncmp(entryx.ut_line, rutmpx->ut_line,
						sizeof (entryx.ut_line)) == 0) {

				if (rutmpx->ut_type == DEAD_PROCESS) {
						break;
				}

				if (rutmpx->ut_type == USER_PROCESS) {
					if (invalid_utmpx(&entryx, rutmpx)) {
						usage();
						exit(ILLEGAL_ARGUMENT);
					} else {
						break;
					}
				}
			}
		}

		if (pututxline(&entryx) == (struct utmpx *) NULL) {
			exit(PUTUTLINE_FAILURE);
		}
	}

	exit(NORMAL_EXIT);
}


/*
 * load_utmp_struct	- Load up the utmp structure with information supplied
 *			as arguments in argv.
 */

static void
load_utmp_struct(entry, argv)
	struct utmp *entry;
	char *argv[];
{
	char *user, *id, *line, *pid, *type, *term,
	    *exitstatus, *xtime;
	int temp;

	user 	= argv[2];
	id 	= argv[3];
	line 	= argv[4];
	pid 	= argv[5];
	type 	= argv[6];
	term 	= argv[7];
	exitstatus = argv[8];
	xtime 	= argv[9];

	(void) strncpy(entry->ut_user, user, 8);
	(void) strncpy(entry->ut_id, id, 4);
	(void) strncpy(entry->ut_line, line, 12);

	(void) sscanf(pid, "%d", &temp);
	entry->ut_pid = temp;

	(void) sscanf(type, "%d", &temp);
	entry->ut_type = temp;


	(void) sscanf(term, "%d", &temp);
	entry->ut_exit.e_termination = temp;

	(void) sscanf(exitstatus, "%d", &temp);
	entry->ut_exit.e_exit = temp;

	/*
	 * Here's where we stamp the exit field of a USER_PROCESS
	 * record so that we know it was written by a normal user.
	 */

	if (entry->ut_type == USER_PROCESS)
		setuser(*entry);

	(void) sscanf(xtime, "%d", (int *)&entry->ut_time);

}

static
hex2bin(c)
	char c;
{
	return (c > '9' ? 10 + (c-'A') : c - '0');
}


/*
 * load_utmpx_struct	- Load up the utmpx structure with information supplied
 *			as arguments in argv.
 */

static void
load_utmpx_struct(entryx, argv)
	struct utmpx *entryx;
	char *argv[];
{
	char *user, *id, *line, *pid, *type, *term, *time_usec,
	    *exitstatus, *xtime, *session, *pad, *syslen, *host;
	int temp, i;

	(void) memset(entryx, 0, sizeof (struct utmpx));

	user 	= argv[2];
	id 	= argv[3];
	line 	= argv[4];
	pid 	= argv[5];
	type 	= argv[6];
	term 	= argv[7];
	exitstatus = argv[8];
	xtime 	= argv[9];
	time_usec = argv[10];
	session = argv[11];
	pad 	= argv[12];
	syslen	= argv[13];
	host	= argv[14];

	(void) strncpy(entryx->ut_user, user, 32);
	(void) strncpy(entryx->ut_id, id, 4);
	(void) strncpy(entryx->ut_line, line, 32);

	(void) sscanf(pid, "%d", &temp);
	entryx->ut_pid = temp;

	(void) sscanf(type, "%d", &temp);
	entryx->ut_type = temp;

	(void) sscanf(term, "%d", &temp);
	entryx->ut_exit.e_termination = temp;

	(void) sscanf(exitstatus, "%d", &temp);
	entryx->ut_exit.e_exit = temp;
	/*
	 * Here's where we stamp the exit field of a USER_PROCESS
	 * record so that we know it was written by a normal user.
	 */

	if (entryx->ut_type == USER_PROCESS)
		setuserx(*entryx);

	(void) sscanf(xtime, "%d", &temp);
	entryx->ut_tv.tv_sec = temp;

	(void) sscanf(time_usec, "%d", &temp);
	entryx->ut_tv.tv_usec = temp;

	(void) sscanf(session, "%d", &temp);
	entryx->ut_session = temp;

	temp = strlen(pad);
	for (i = 0; i < temp; i += 2)
		entryx->pad[i>>2] = hex2bin(pad[i]) << 4 |
		    hex2bin(pad[i+1]);

	(void) sscanf(syslen, "%d", &temp);
	entryx->ut_syslen = temp;

	(void) strncpy(entryx->ut_host, host, MAX_SYSLEN);
}

/*
 * usage	- There's no need to say more.  This program isn't supposed to
 *		be executed by normal users directly.
 */

static void
usage()
{
	(void) printf("Wrong number of arguments or invalid user \n");
}

/*
 * check_utmpx	- Verify the utmpx structure
 */

static void
check_utmpx(entryx)
	struct utmpx *entryx;
{
	char buf[BUF_SIZE];
	char *line = buf;
	struct passwd *pwd;
	int uid;
	int fd;
	char	*user;
	int	ruid = (int) getuid();

	(void) memset(buf, 0, BUF_SIZE);
	user = (char *) malloc(sizeof (entryx->ut_user) +1);
	(void) strncpy(user, entryx->ut_user, sizeof (entryx->ut_user));
	user[sizeof (entryx->ut_user)] = '\0';
	pwd = getpwnam(user);
	(void) free(user);

	(void) strncat(strcpy(line, "/dev/"), entryx->ut_line, 128);

	if (pwd != (struct passwd *)NULL) {
		uid = pwd->pw_uid;
		/*
		 * We nolonger permit the UID of the caller to be different
		 * the UID to be written to the utmp file. This was thought
		 * necessary to allow the utmp file to be updated when
		 * logging out from an xterm(1) window after running
		 * exec login. Instead we now rely upon utmpd(1) to update
		 * the utmp file for us.
		 *
		 */

		if (ruid != uid) {
			dprintf3("Bad uid: user %s  = %d uid = %d \n",
					entryx->ut_user, uid, getuid());
			exit(ILLEGAL_ARGUMENT);
		}

	} else if (entryx->ut_type != DEAD_PROCESS) {
		dprintf("Bad user name: %s \n", entryx->ut_user);
		exit(ILLEGAL_ARGUMENT);
	}

	/*
	 * Only USER_PROCESS and DEAD_PROCESS entries may be updated
	 */
	if (!(entryx->ut_type == USER_PROCESS ||
					entryx->ut_type == DEAD_PROCESS)) {
		dprintf("Bad type type = %d\n", entryx->ut_type);
		exit(ILLEGAL_ARGUMENT);
	}

	/*
	 * Verify that the pid of the entry field is the same pid as our
	 * parent, who should be the guy writing the entry.  This is commented
	 * out for now because this restriction is overkill.
	 */
#ifdef	VERIFY_PID
	if (entryx->ut_type == USER_PROCESS && entryx->ut_pid != getppid()) {
		dprintf("Bad pid = %d\n", entryx->ut_pid);
		exit(ILLEGAL_ARGUMENT);
	}
#endif	/* VERIFY_PID */

	if (bad_line(line) == 1) {
		dprintf("Bad line = %s\n", line);
		exit(ILLEGAL_ARGUMENT);
	}

	if (bad_hostname(entryx->ut_host, entryx->ut_syslen) == 1) {
		dprintf("Bad hostname name = %s\n", entryx->ut_host);
		exit(ILLEGAL_ARGUMENT);
	}
	check_id(entryx->ut_id, entryx->ut_line);
}

/*
 * bad_hostname		- Previously returned an error if a non alpha numeric
 *			was in the host field, but now just clears those so
 *			cmdtool entries will work.
 */

static int
bad_hostname(name, len)
	char *name;
	int len;
{
	int i;

	if (len < 0 || len > MAX_SYSLEN)
		return (1);
	/*
	 * Scan for non-alpha numerics
	 */
	for (i = 0; i < len; i++)
		if (isprint(name[i]) == 0)
			name[i] = ' ';
	return (0);
}


static void
check_utmp(entry)
	struct utmp *entry;
{
	char buf[BUF_SIZE];
	char *line = buf;
	struct passwd *pwd;
	int uid;
	int fd;
	struct stat statbuf;
	char	*user;
	int	ruid = (int) getuid();

	(void) memset(buf, 0, BUF_SIZE);
	user = (char *) malloc(sizeof (entry->ut_user) +1);
	(void) strncpy(user, entry->ut_user, sizeof (entry->ut_user));
	user[sizeof (entry->ut_user)] = '\0';
	pwd = getpwnam(user);
	(void) free(user);

	(void) strncat(strcpy(line, "/dev/"), entry->ut_line, 128);

	/*
	 * We reject a null name only when it is not for a dead_process entry
	 * see bugid 1153240
	 */
	if (pwd != (struct passwd *)NULL) {
		uid = pwd->pw_uid;
		/*
		 * We nolonger permit the UID of the caller to be different
		 * the UID to be written to the utmp file. This was thought
		 * necessary to allow the utmp file to be updated when
		 * logging out from an xterm(1) window after running
		 * exec login. Instead we now rely upon utmpd(1) to update
		 * the utmp file for us.
		 *
		 */
		if (ruid != uid) {
			dprintf3("Bad uid: user %s  = %d uid = %d \n",
					entry->ut_user, uid, getuid());
			exit(ILLEGAL_ARGUMENT);
		}
	} else if (entry->ut_type != DEAD_PROCESS) {
		dprintf("Bad user name: %s \n", entry->ut_user);
		exit(ILLEGAL_ARGUMENT);
	}

	/*
	 * Only USER_PROCESS and DEAD_PROCESS entries may be updated
	 */
	if (! (entry->ut_type == USER_PROCESS ||
					entry->ut_type == DEAD_PROCESS)) {
		dprintf("Bad type type = %d\n", entry->ut_type);
		exit(ILLEGAL_ARGUMENT);
	}

	/*
	 * Verify that the pid of the entry field is the same pid as our
	 * parent, who should be the guy writing the entry.  This is commented
	 * out for now because this restriction is overkill.
	 */
#ifdef	VERIFY_PID
	if (entry->ut_type == USER_PROCESS && entry->ut_type != getppid()) {
		dprintf("Bad type type = %d\n", entry->ut_type);
		exit(ILLEGAL_ARGUMENT);
	}
#endif	/* VERIFY_PID */

	if (bad_line(line) == 1) {
		dprintf("Bad line = %s\n", line);
		exit(ILLEGAL_ARGUMENT);
	}

	check_id(entry->ut_id, entry->ut_line);

}

/*
 * Workaround until the window system gets fixed.  Look for id's with
 * a '/' in them.  That means they are probably from libxview.
 * Then create a new id that is unique using the last 4 chars in the line.
 */

check_id(id, line)
	char id[];
	char *line;
{
	char temp[4];
	int i, len;

	if (id[1] == '/' && id[2] == 's' && id[3] == 't') {
		len = strlen(line);
		if (len > 0)
			len--;
		for (i = 0; i < 4; i++)
			id[i] = len - i < 0 ? 0 : line[len-i];
	}
}

/*
 * The function invalid_utmp() enforces the requirement that the record
 * being updating in the utmp file can not have been created by login(1)
 * or friends. Also that the id and username of the record to be written match
 * those found in the utmp file. We need this both for security and to ensure
 * that pututline(3C) will NOT reposition the file pointer in the utmp file,
 * so that the record is updated in place.
 *
 */

static int
invalid_utmp(struct utmp * eutmp, struct utmp *rutmp)
{
#define	SUTMP_ID	(sizeof (eutmp->ut_id))
#define	SUTMP_USER	(sizeof (eutmp->ut_user))

	return (!nonuser(*rutmp) ||
		strncmp(eutmp->ut_id, rutmp->ut_id, SUTMP_ID) != 0 ||
		strncmp(eutmp->ut_user, rutmp->ut_user, SUTMP_USER) != 0);
}

/*
 * The function invalid_utmpx() enforces the requirement that the record
 * being updating in the utmpx file can not have been created by login(1)
 * or friends. Also that the id and username of the record to be written match
 * those found in the utmpx file. We need this both for security and to ensure
 * that pututxline(3C) will NOT reposition the file pointer in the utmpx file,
 * so that the record is updated in place.
 *
 */
static int
invalid_utmpx(struct utmpx * eutmpx, struct utmpx *rutmpx)
{
#define	SUTMPX_ID	(sizeof (eutmpx->ut_id))
#define	SUTMPX_USER	(sizeof (eutmpx->ut_user))

	return (!nonuserx(*rutmpx) ||
		strncmp(eutmpx->ut_id, rutmpx->ut_id, SUTMPX_ID) != 0 ||
		strncmp(eutmpx->ut_user, rutmpx->ut_user, SUTMPX_USER) != 0);
}

int
bad_line(char *line)
{
	struct sigaction act;
	struct stat statbuf;
	int	fd;

	/*
	 * The line field must be a device file that we can write to,
	 * it should live under /dev which is enforced by requiring
	 * its name not to contain "../" and opening it as the user for
	 * writing.
	 */
	if (strstr(line, "../") != 0) {
		dprintf("Bad line = %s\n", line);
		return (1);
	}

	/*
	 * It has to be a tty. It can't be a bogus file, e.g. ../tmp/bogus.
	 */
	if (seteuid(getuid()) != 0)
		return (1);

	/*
	 * Check that the line refers to a character
	 * special device see bugid: 1136978
	 */
	if ((stat(line, &statbuf) < 0) || (statbuf.st_mode & S_IFCHR) == 0) {
		dprintf("Bad line (stat failed) (Not S_IFCHR) = %s\n", line);
		return (1);
	}

	/*
	 * We need to open the line without blocking so that it does not hang
	 */
	if ((fd = open(line, O_WRONLY|O_NOCTTY|O_NONBLOCK)) == -1) {
		dprintf("Bad line (Can't open/write) = %s\n", line);
		return (1);
	}

	/*
	 * Check that fd is a tty, if this fails all is not lost see below
	 */
	if (isatty(fd) == 1) {
		/*
		 * It really is a tty, so return success
		 */
		close(fd);
		if (seteuid(ROOT_UID) != 0)
			return (1);
		return (0);
	}

	/*
	 * Check that the line refers to a character
	 * special device see bugid: 1136978
	 */
	if ((fstat(fd, &statbuf) < 0) || (statbuf.st_mode & S_IFCHR) == 0) {
		dprintf("Bad line (fstat failed) (Not S_IFCHR) = %s\n", line);
		close(fd);
		return (1);
	}

	/*
	 * Check that the line refers to a streams device
	 */
	if (isastream(fd) != 1) {
		dprintf("Bad line (isastream failed) = %s\n", line);
		close(fd);
		return (1);
	}

	/*
	 * if isatty(3C) failed above we assume that the ptem module has
	 * been popped already and that caused the failure, so we push it
	 * and try again
	 */
	if (ioctl(fd, I_PUSH, "ptem") == -1) {
		dprintf("Bad line (I_PUSH of \"ptem\" failed) = %s\n", line);
		close(fd);
		return (1);
	}

	if (isatty(fd) != 1) {
		dprintf("Bad line (isatty failed) = %s\n", line);
		close(fd);
		return (1);
	}

	if (ioctl(fd, I_POP, 0) == -1) {
		dprintf("Bad line (I_POP of \"ptem\" failed) = %s\n", line);
		close(fd);
		return (1);
	}

	close(fd);

	if (seteuid(ROOT_UID) != 0)
		return (1);

	return (0);

}

#ifdef	DEBUG

/*
 * display_args		- This code prints out invocation arguments
 *			This is helpful since the program is called with
 *			up to 15 argumments.
 */

static void
display_args(argc, argv)
	int argc;
	char **argv;
{
	int i = 0;

	while (argc--) {
		printf("Argument #%d = %s\n", i, argv[i]);
		i++;
	}
}

fputmp(struct utmp *rutmp)
{
	printf("ut_user = \"%-8.8s\" \n", rutmp->ut_user);
	printf("ut_id = \"%-4.4s\" \n", rutmp->ut_id);
	printf("ut_line = \"%-12.12s\" \n", rutmp->ut_line);
	printf("ut_pid = \"%d\" \n", rutmp->ut_pid);
	printf("ut_type = \"%d\" \n", rutmp->ut_type);
	printf("ut_exit.e_termination = \"%d\" \n",
					rutmp->ut_exit.e_termination);
	printf("ut_exit.e_exit = \"%d\" \n", rutmp->ut_exit.e_exit);
	printf("ut_time = \"%d\" \n", rutmp->ut_time);
}

fputmpx(struct utmpx *rutmpx)
{
	printf("ut_user = \"%-32.32s\" \n", rutmpx->ut_user);
	printf("ut_id = \"%-4.4s\" \n", rutmpx->ut_id);
	printf("ut_line = \"%-32.32s\" \n", rutmpx->ut_line);
	printf("ut_pid = \"%d\" \n", rutmpx->ut_pid);
	printf("ut_type = \"%d\" \n", rutmpx->ut_type);
	printf("ut_exit.e_termination = \"%d\" \n",
					rutmpx->ut_exit.e_termination);
	printf("ut_exit.e_exit = \"%d\" \n", rutmpx->ut_exit.e_exit);
}

#endif DEBUG
