#ident	"@(#)subs.c 1.45 93/04/28"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include "tapelib.h"
#include <config.h>
#include <lfile.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <grp.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>

#ifdef __STDC__
static int longestbadcycle(char *, int, int);
static void growstring(struct string_f *);
#else
#endif

/*
 * print an informative message, then exit
 */
/* VARARGS1 */
void
die(const char *format, ...)
{
	va_list args;

#ifdef USG
	va_start(args, format);
#else
	va_start(args);
#endif
	(void) fprintf(stderr, "%s: ", progname);
	(void) vfprintf(stderr, format, args);
	va_end(args);
	exit(1);
}


/*
 * print an informative message
 */
/* VARARGS1 */
void
warn(const char *format, ...)
{
	va_list args;

#ifdef USG
	va_start(args, format);
#else
	va_start(args);
#endif
	(void) fprintf(stderr, gettext("WARNING %s: "), progname);
	(void) vfprintf(stderr, format, args);
	va_end(args);
	if (thisisedit)
		(void) sleep(3);
	else
		exit(1);
}


char *
checkalloc(n)
	unsigned n;
{				/* n == how many bytes to allocate */
	char	*p = malloc(n);
	if (p == NULL)
		die(gettext("Out of memory (%d)\n"), 1);
	return (p);
}

char *
checkrealloc(old, n)		/* n == how many total bytes to allocate */
	char	*old;
	unsigned n;
{
	char	*p;

	if (old == NULL)
		p = malloc(n);
	else
		p = realloc(old, n);
	if (p == NULL)
		die(gettext("Out of memory (%d)\n"), 2);
	return (p);
}

char *
checkcalloc(n)
	unsigned n;
{				/* n == how many bytes to allocate */
	char	*p = calloc((unsigned) 1, n);
	if (p == NULL)
		die(gettext("Out of memory (%d)\n"), 3);
	return (p);
}

/* add `add' to end of `orig' and return pointer to the last null */
/* very useful for appending lots of things to a string */

char *
strappend(orig, add)
	char	*orig;
	char	*add;
{
	while (*orig++)			/* scream to end of string */
		/* empty */
		;
	orig--;
	while (*orig++ = *add++)	/* copy string across */
		/* empty */
		;
	return (--orig);		/* return pointer to null */
}

static char	dumpflags[100];		/* More than ever possible XXX */
static char	*dumpargs[100];		/* likewise XXX */
static int	ndumpargs;		/* number of dumpargs */

void
initdumpargs(void)
{
	int	i;
	dumpflags[0] = 0;
	for (i = 0; i < ndumpargs; i++)
		free(dumpargs[i]);
	ndumpargs = 0;
}

void
adddumpflag(s)
	char	*s;
{
	(void) strcat(dumpflags, s);
}

void
adddumpflagc(c)
	int	c;
{
	char	scr[2];
	scr[0] = (char)c;
	scr[1] = '\0';
	(void) strcat(dumpflags, scr);
}

void
adddumparg(s)
	char	*s;
{
	dumpargs[ndumpargs++] = strdup(s);
}

void
printlfile(mesg)
	char	*mesg;
{
	FILE   *in;
	char	line[MAXLINELEN];

	if (debug == 0)
		return;
	(void) printf("%s", mesg);

	in = fopen(lfilename, "r");
	if (in == NULL) {
		(void) fprintf(stderr, gettext(
			"%s: Cannot open volume label file `%s'\n"),
			"printlfile", lfilename);
	} else {
		while (fgets(line, MAXLINELEN, in) != NULL)
			if (fputs(line, stdout) == EOF)
				break;
		(void) fclose(in);
	}
}

void
makedumpcommand(void)
{
	int	i;

	/*
	 * remote commands look like this: (all in one string)
	 *
	 *	exec /usr/bin/rsh -l rdevuser rhost "sh -c
	 *		'( /opt/.../hsmdump flags args ) 2>&1; exec echo ==$?'
	 *
	 * the local commands look like this:
	 *
	 *	( /opt/.../hsmdump flags args ) 2>&1; echo ==$?
	 *
	 */

	dumpcommand = newstring();
	if (remote[0]) {
		stringapp(dumpcommand, "sh -c '");
	}
	stringapp(dumpcommand, "( ");
	if (usehsmroot == 0)
		stringapp(dumpcommand, "/usr/etc/dump ");
	else {
		stringapp(dumpcommand, gethsmpath(sbindir));
		stringapp(dumpcommand, "/hsmdump ");
	}
	stringapp(dumpcommand, dumpflags);
	for (i = 0; i < ndumpargs; i++) {
		stringapp(dumpcommand, " ");
		stringapp(dumpcommand, dumpargs[i]);
	}
	if (remote[0])
		stringapp(dumpcommand, " ) 2>&1; exec echo ==$?'");
	else
		stringapp(dumpcommand, " ) 2>&1; echo ==$?");
}

#define	MARKER	-2

/*
 * determine how long to keep things around:
 */
void
figurekeep(level, fullcycle, dumplevels)
	int	level;		/* what level dump we're doing */
	int	fullcycle;	/* current fullcycle number */
	char	*dumplevels;	/* "05>555" */
{
	int	i;
	char	*p, *q;
	char	*line;

	keepdays = MARKER;
	keepminavail = MARKER;
	for (i = 0; i < ncf_keep; i++) {	/* determine `keep times' */
		if (cf_keep[i].k_level != level)
			continue;
		/* only precisely correct for full dumps: */
		if (cf_keep[i].k_multiple <= 0 ||
		    (fullcycle == 0 && cf_keep[i].k_multiple != 1) ||
		    (fullcycle % cf_keep[i].k_multiple) != 0)
			continue;
		if (cf_keep[i].k_days == -1) {
			keepdays = -1;
			keepminavail = 0;
			break;
		}
		if (cf_keep[i].k_days > keepdays)
			keepdays = cf_keep[i].k_days;
		if (cf_keep[i].k_minavail > keepminavail)
			keepminavail = cf_keep[i].k_minavail;
	}
	if (keepdays == MARKER)
		keepdays = 30;
	if (keepminavail == MARKER)
		keepminavail = 0;

	line = checkalloc(strlen(dumplevels) + 1);
	for (q = line, p = dumplevels; *p; p++)
		if (*p == '>')
			continue;
		else
			*q++ = *p;
	*q++ = '\0';
	keeptil = longestbadcycle(line, level, keepminavail) + cf_mastercycle;
	free(line);
}

/* VARARGS1 */
void
log(const char *format, ...)
{
	va_list args;
	time_t  timeval;
	struct tm *tm;

#ifdef USG
	va_start(args, format);
#else
	va_start(args);
#endif
	if (logfile == NULL)
		return;
	if (time(&timeval) == -1)
		die(gettext("%s: Cannot determine current time\n"), "log");
	tm = localtime(&timeval);
	(void) fprintf(logfile, "%02.2d%02.2d%02.2d %02.2d%02.2d %s/%s ",
		tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, hostname, filename);
	(void) vfprintf(logfile, format, args);
	(void) fflush(logfile);
	va_end(args);
}

/*
 * add on stuff to a per-filesystem mail log.  either the log or the
 * string may be NULL.  if the log is NULL, use the previous log.  if
 * the string is NULL, don't append anything (useful to set up a default
 * log).  if both arguments are NULL, the default is passed to
 * freestring().
 */
void
logmail(l, s)
struct string_f *l;
char *s;
{
	static struct string_f *def = NULL;

	if ((l == NULL) && (s == NULL) && (def != NULL)) {
		freestring(def);
		def = NULL;
		return;
	}

	if ((l == NULL) && (def))
		l = def;
	else
		def = l;

	if ((s) && (l)) {
		stringapp(l, "> ");
		stringapp(l, s);
	}
}

/*
 * create L file: N-00002 N-new P-Partial E-Error F-Full
 */
int
writelfile(void)
{
	struct tapes_f *t;
	char scratch[MAXLINELEN];

	if (nswitch)
		return (0);

	if ((lfilefid = fopen(lfilename, "w")) == NULL) {
		(void) sprintf(scratch, gettext(
			"%s: Cannot create volume label file `%s': %s\n"),
			progname, lfilename, strerror(errno));
		log("%s", scratch);
		logmail(NULL, scratch);
		(void) fprintf(stderr, "%s", scratch);
		return (1);
	}
	(void) fprintf(lfilefid, "%s", LF_HEADER);	/* security header */
	(void) fprintf(lfilefid, "%s\n", cf_tapelib);	/* tape library */
	for (t = tapes_head.ta_next; t != &tapes_head; t = t->ta_next) {
		(void) fprintf(lfilefid,
			"%c-%05.5d\n", t->ta_status, t->ta_number);
	}
	if (fclose(lfilefid) == EOF) {
		(void) sprintf(scratch, gettext(
			"%s: Cannot write volume label file `%s': %s\n"),
			progname, lfilename, strerror(errno));
		log("%s", scratch);
		logmail(NULL, scratch);
		(void) fprintf(stderr, "%s", scratch);
		(void) unlink(lfilename);
		return (1);
	}
	return (0);
}

/*
 * Find longest cycle NOT containing n of the indicated characters
 * "13333", '3', 1 -> 1
 * "12333", '3', 1 -> 2
 * "12322", '3', 1 -> 4
 * "12322", '4', 1 -> -1
 * "1232233342223", '2', 3 -> 9
 * This algorithm averages about 19 microseconds/call when strlen==13
 * and n == 3
 */
static int
longestbadcycle(string, ch, n)
	char	*string;	/* find desired cycle length here  */
	int	ch;		/* character of interest */
	int	n;		/* number of cycles to check */
{
	char	*front, *back;
	int	cyclelen;	/* running counter of cycle length */
	int	maxcyclelen;	/* longest length */
	int	seensofar;	/* how many ch's we've seen so far */

	maxcyclelen = 0;
	if (index(string, (char)ch) == NULL)
		return (-1);
	for (front = string; *front; front++) {
		if (*front != (char)ch)
			continue;
		back = front + 1;
		cyclelen = 1;
		seensofar = 0;
		while (seensofar < n) {
			if (*back == 0)
				back = string;
			if (*back == (char)ch)
				seensofar++;
			back++, cyclelen++;
		}
		if (cyclelen > maxcyclelen)
			maxcyclelen = cyclelen;
	}
	return (maxcyclelen);
}

/* all numbers/letters */

filenamecheck(filename, maxlen)
	char	*filename;
{
	char	*p;
	if ((int)strlen(filename) > maxlen)
		return (1);
	for (p = filename; *p; p++)
		if (*p != '_' && isalnum((u_char)*p) == 0)
			return (1);
	return (0);
}

/*
 * 1  full-then-incremental	05555 55555
 * 2  full-incr-incr2		09999 59999
 * 3  full-true-incr		0xxxx xxxxx
 * 4  full-true-incr2		0xxxx 5xxxx
 */
char *
genlevelstring(type, staggerdist, len, sublen)
{
	char	*p = malloc((unsigned) (len + 1));
	char	*q;
	int	i;

	p[len] = '\0';
	switch (type) {
	case LEV_FULL_INCR:
		for (q = p + staggerdist, i = 0; i < len; i++) {
			*q = i == 0 ? '0' : '5';
			if (++q >= p + len)
				q = p;
		}
		return (p);
	case LEV_FULL_INCRx2:
		for (q = p + staggerdist, i = 0; i < len; i++) {
			*q = i == 0 ? '0' : '9';
			if (i != 0 && (i % sublen) == 0)
				*q = '5';
			if (++q >= &p[len])
				q = p;
		}
		return (p);
	case LEV_FULL_TRUEINCR:
		for (q = p + staggerdist, i = 0; i < len; i++) {
			*q = i == 0 ? '0' : 'x';
			if (++q >= p + len)
				q = p;
		}
		return (p);
	case LEV_FULL_TRUEINCRx2:
		for (q = p + staggerdist, i = 0; i < len; i++) {
			*q = i == 0 ? '0' : 'x';
			if (i != 0 && (i % sublen) == 0)
				*q = '5';
			if (++q >= &p[len])
				q = p;
		}
		return (p);
	default:
		(void) printf(gettext(
		    "%s: `%d' is invalid (internal error)\n"),
			"genlevelstring", type);
		nocurses();
		exit(1);
	}
	/* NOTREACHED */
}

#ifdef USELOCKF
void
exunlock(void)
{
	if (nswitch)
		return;
	if (lseek(lockfid, (off_t) 0, 0) == -1) { /* lockf uses offset... */
		(void) fprintf(stderr, gettext(
			"%s: Cannot seek to beginning of file\n"),
			progname);
	(void) lockf(lockfid, F_ULOCK, (long) 0);
}

exlock(file, diemesg)
	char	*file;
	char	*diemesg;
{
	int	lockfid;

	if (nswitch)
		return (-1);
	lockfid = open(file, O_RDWR);
	if (lockfid == -1)
		die(gettext("Cannot open configuration file `%s'\n"), file);
	if (lseek(lockfid, (off_t) 0, 0) == -1)	/* lockf uses offset... */
		die(gettext("%s: Cannot seek to beginning of file `%s'\n"),
			"exlock", file);
	if (lockf(lockfid, F_TLOCK, (long) 0) == 0) {
		return (lockfid);
	}
	if (errno == EACCES) {
		if (isatty(1) == 0) {
			char	mailcommand[MAXPATHLEN];

			if (ncf_notifypeople) {
				char	*p = mailcommand;
				FILE   *cmd;
				int	i;
				p[0] = '\0';
				p = strappend(p, "/usr/bin/mail");

				for (i = 0; i < ncf_notifypeople; i++) {
					p = strappend(p, " ");
					p = strappend(p, cf_notifypeople[i]);
				}
				cmd = popen(mailcommand, "w");
				if (cmd == NULL) {
					log(gettext(
					    "Cannot execute `%s'\n"),
					    mailcommand);
				} else {
					(void) fprintf(cmd, gettext(
				    "%s: DUMPEX PROBLEMS FROM %s/%s\n\n"),
					    "Subject", hostname, filename);
					(void) fprintf(cmd, gettext(
			    "The lock failed with the following message:\n"));
					(void) fprintf(cmd, "%s", diemesg);
					(void) fprintf(cmd, ".\n");
					(void) pclose(cmd);
				}
			}
		}
		die(diemesg);
	} else
		die(gettext("%s: lock returned errno=%d\n"),
			"exlock", errno);
}

#else				/* use flock */
#include <sys/file.h>

#if defined(USG) && !defined(FLOCK)

#include <fcntl.h>
/*
 * Trump up a version of flock based on fcntl.
 * You don't need this if your implementation
 * has flock -- just compile with -DFLOCK.
 */
flock(fd, operation)
	int	fd;
	int	operation;
{
	struct flock fl;
	int block = 0;
	int status;

	if (operation & LOCK_SH)
		fl.l_type = F_RDLCK;
	else if (operation & LOCK_EX)
		fl.l_type = F_WRLCK;
	else if (operation & LOCK_UN)
		fl.l_type = F_UNLCK;
	if ((operation & LOCK_NB) == 0)
		block = 1;
	fl.l_whence = SEEK_SET; /* XXX ? */
	fl.l_start = (off_t)0;
	fl.l_len = (off_t)0;    /* till EOF */
	while ((status = fcntl(fd, F_SETLK, (char *)&fl)) < 0 &&
	    (errno == EACCES || errno == EAGAIN) && block) {
		(void) sleep(1);
	}
	if (status < 0 && block) {
		perror("F_SETLK");
		if (fcntl(fd, F_GETLK, (char *)&fl) < 0)
			perror("F_GETLK");
	}
	return (status);
}
#endif

void
exunlock(void)
{
	if (nswitch)
		return;
	(void) flock(lockfid, LOCK_UN);
}


exlock(file, diemesg)
	char	*file;
	char	*diemesg;
{
	int	lockfid;

	if (nswitch)
		return (-1);
	lockfid = open(file, O_RDWR);
	if (lockfid == -1)
		die(gettext("%s: Cannot open configuration file `%s'\n"),
			"exlock", file);
	if (flock(lockfid, LOCK_EX | LOCK_NB) == 0)
		return (lockfid);
	switch (errno) {
	case EACCES:
	case EAGAIN:
		if (isatty(1) == 0) {
			char	mailcommand[MAXPATHLEN];

			if (ncf_notifypeople) {
				char	*p = mailcommand;
				FILE   *cmd;
				int	i;
				p[0] = '\0';
				p = strappend(p, "/usr/bin/mail");

				for (i = 0; i < ncf_notifypeople; i++) {
					p = strappend(p, " ");
					p = strappend(p, cf_notifypeople[i]);
				}
				cmd = popen(mailcommand, "w");
				if (cmd == NULL) {
					log(gettext(
					    "%s: Cannot execute `%s'\n"),
					    progname, mailcommand);
				} else {
					(void) fprintf(cmd, gettext(
				    "%s: DUMPEX PROBLEMS FROM %s/%s\n\n"),
					    "Subject", hostname, filename);
					(void) fprintf(cmd, gettext(
			    "The lock failed with the following message:\n"));
					(void) fprintf(cmd, "%s", diemesg);
					(void) fprintf(cmd, ".\n");
					(void) pclose(cmd);
				}
			}
		}
		die(diemesg);
		/* NOTREACHED */
	case EBADF:
		die(gettext("%s: Cannot lock `%s', file descriptor is bad.\n"),
			"exlock", file);
		/* NOTREACHED */
	case EOPNOTSUPP:
		die(gettext("%s: Cannot lock `%s', it is not a file\n"),
			"exlock", file);
		/* NOTREACHED */
	default:
		die(gettext("%s: flock returned errno=%d\n"), "exlock", errno);
		/* NOTREACHED */
	}
}

#endif

struct string_f *
newstring(void)
{
	struct string_f *s =
		/*LINTED [alignment ok]*/
		(struct string_f *) checkalloc(sizeof (struct string_f));
	s->s_string = checkalloc(GROW);
	s->s_string[0] = '\0';
	s->s_max = &s->s_string[GROW];
	s->s_last = &s->s_string[0];
	return (s);
}

void
freestring(s)
	struct string_f *s;
{
	free(s->s_string);
	free(s);
}

void
stringapp(s, add)
	struct string_f *s;
	char	*add;
{
	char	*p;
	while (s->s_last + strlen(add) >= s->s_max)
		growstring(s);
	p = s->s_last;
	while (*add)
		*p++ = *add++;
	*(s->s_last = p) = 0;
}

static void
growstring(s)
	struct string_f *s;
{
	char	*sp = s->s_string;
	int	max = s->s_max - sp;
	int	last = s->s_last - sp;
	sp = s->s_string = checkrealloc(sp, max + GROW);
	s->s_max = &sp[max + GROW];
	s->s_last = &sp[last];
}

void
chop(p)
	char	*p;
{
	char	*q = p;
	while (*p != 0)
		p++;
	if (p != q)
		*--p = '\0';	/* don't shorten 0-length strings */
}

/*ARGSUSED*/
void
checkroot(groupok)
	int	groupok;	/* if non-zero, group sys/operator is ok */
{
#ifndef notdef
	if (geteuid() != 0) {
		(void) printf(gettext(
		    "You must be superuser to run this program.\n"));
		exit(1);
	}
#else
#ifdef USG

	/* XGETTEXT: The group allowed to read disks on Solaris 2.0 */
	char *gname = gettext("sys");
	struct group *gp = getgrnam(gname));
#else

	/* XGETTEXT: The group allowed to read disks on Solaris 1.0 */
	char *gname = gettext("operator");
	struct group *gp = getgrnam(gname);
#endif
	register int i;
	gid_t	gid = getgid();
	gid_t	*gidset;
	int	gidsetsize;

	if (getuid() != 0 && (groupok == 0 || gp == (struct group *)0)) {
		(void) printf(gettext(
		    "You must be superuser to run this program.\n"));
		exit(1);
	}
	if (gid != gp->gr_gid) {
		gidsetsize = (int)sysconf(_SC_NGROUPS_MAX);
		/* LINTED [alignment ok] */
		gidset = (gid_t *)checkalloc(gidsetsize * sizeof (gid_t));
		i = getgroups(gidsetsize, gidset);
		while (i-- > 0) {
			if (gidset[i] == gp->gr_gid)
				break;
		}
		free(gidset);
		if (i < 0) {
			(void) printf(gettext(
	"You must be superuser or in group `%s' to run this program.\n"),
				gname);
			exit(1);
		}
	}
#endif
}

/*
 * The initial call should specify a NULL "statusp" pointer.  In this case,
 * all other arguments are ignored and the statically-returned "line"
 * variable is reset.
 *
 * A NULL pointer is returned when the line is still partial (the newline
 * has not yet been entered).  Otherwise, a pointer to a static string
 * is returned.
 *
 * If the "logit" variable is set, all output will be sent to stderr and
 * the log/logmail functions.  Exit status lines (starting with "==")
 * will not be sent to the log, however.
 */
char *
gatherline(c, logit, statusp)
	int c;
	int logit;
	int *statusp;
{
	static char line[MAXLINELEN];		/* output gathered here */
	static char *writefrom = line;		/* where next output goes */
	char *linep = writefrom;
	char *retcp = (char *) NULL;

	if (statusp == (int *) NULL) {
		writefrom = line;
		return ((char *) NULL);
	}

	/* this conditional should never be true:  over buffer? */
	if (writefrom >= line + MAXLINELEN - 1) {
		/* blow off first MAXLINELEN chars and continue: */
		writefrom = linep = line;
	}

	*linep++ = c;			/* save the character */
	if (c == '\n') {		/* complete line in gatherline? */
		*linep = '\0';		/* terminate line */
		if (line[0] == '=' && line[1] == '=') {
			*statusp = atoi(&line[2]);	/* return code */
			writefrom = line;
			return ((char *) NULL);
		}
		if (logit) {
			log("--- %s", line);
			logmail(NULL, line);
			(void) fwrite(writefrom, linep - writefrom, 1, stderr);
			/*
			 * output -- prob only 1 char
			 * must do because some input
			 * requests don't have \n
			 */
			(void) fflush(stderr);
		}
		retcp = linep = line;		/* restart gather */
	} else if (logit && linep != writefrom && line[0] != '=') {
		/* non-full line & not return line */
		(void) fwrite(writefrom, linep - writefrom, 1, stderr);
		/* prob only 1 char */
		(void) fflush(stderr);
	}
	writefrom = linep;		/* continue gathering */
	*statusp = NORETURNCODE;
	return (retcp);
}

#ifndef System
/*
 * my own system takes care to close any extraneous files in the child
 * it also establishes an alarm to kill the child if necessary
 */
static int System_pid;
/*ARGSUSED*/
static void
alarm_handler(int sig)
{
	if (System_pid != 0 && System_pid != -1)
		(void) kill(System_pid, SIGHUP);
	System_pid = 0;
}

int
System(s)
	char *s;
{
	int status, w;
	void (*istat)(), (*qstat)(), (*cstat)(), (*astat)();

	if ((System_pid = vfork()) == 0) {
		int i;

		for (i = 3; i < 32; i++)
			(void) close(i);
		(void) execl("/bin/sh", "sh", "-c", s, (char *)0);
		_exit(127);
	}
	istat = signal(SIGINT, SIG_IGN);
	qstat = signal(SIGQUIT, SIG_IGN);
	cstat = signal(SIGCLD, SIG_DFL);
	astat = signal(SIGALRM, alarm_handler);
	alarm(60*4);				/* 4-minute alarm */

	w = waitpid(System_pid, &status, 0);

	alarm(0);
	(void) signal(SIGINT, istat);
	(void) signal(SIGQUIT, qstat);
	(void) signal(SIGCLD, cstat);
	(void) signal(SIGALRM, astat);
	return ((w == -1)? w: status);
}
#endif
