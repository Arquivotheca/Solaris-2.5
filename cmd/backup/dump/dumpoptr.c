/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ident	"@(#)dumpoptr.c 1.15 90/11/09 SMI" /* from UCB 5.1 6/5/85 */

#ident	"@(#)dumpoptr.c 1.51 95/07/13"

#include <errno.h>
#include "dump.h"
#include <config.h>
#include <operator.h>
#include <sys/socket.h>
#include <termio.h>
#include <grp.h>
#include <syslog.h>

static	FILE	*mail;

static unsigned int timeout;		/* current timeout */
static u_long	lastprompt;		/* last prompt issued */
static char *attnmessage, *saveattn;	/* attention message */
static char *context, *savecontext;	/* context of attention message */
static int	connected;		/* connected to operator daemon */
static int	doingprompt;		/* prompt state */
static int	dobroadcast;		/* broadcast state */
static char	opserver[BCHOSTNAMELEN];

#ifdef __STDC__
static void alarmcatch();
static void unhang();
static void sendmes(char *, char *);
#ifndef USG
static void setutent(void);
static struct utmp *getutent(void);
static void endutent(void);
#endif
static int idatesort(const void *, const void *);
static int gethelp(void);
/*
 * XXX broken string.h
 */
extern int strcasecmp(const char *, const char *);
#else /* !__STDC__ */
static void alarmcatch();
static void unhang();
static void sendmes();
#ifndef USG
static void setutent();
static struct utmp *getutent();
static void endutent();
#endif
static int idatesort();
static int gethelp();
#endif

#ifdef DEBUG
extern int xflag;
#endif

/*
 *	Query the operator; This fascist piece of code requires
 *	an exact response.
 *	It is intended to protect dump aborting by inquisitive
 *	people banging on the console terminal to see what is
 *	happening which might cause dump to croak, destroying
 *	a large number of hours of work.
 *
 *	Every time += 2 minutes we reprint the message, alerting others
 *	that dump needs attention.
 */
int
query(question, info)
	char	*question;
	char	*info;
{
	int def = -1;

	while (def == -1)
		def = query_once(question, info, -1);
	return (def);
}

static int in_query_once;
static jmp_buf sjalarmbuf;

/* real simple check-sum */
static int
addem(s)
	register char *s;
{
	int total = 0;

	if (s == (char *) NULL)
		return (total);
	while (*s)
		total += *s++;
	return (total);
}

int
query_once(question, info, def)
	char	*question;
	char	*info;
	int	def;
{
	static char *lastmsg, *lastinfo;
	static int lastmsgsum, lastinfosum;
	int	msgsum, infosum;
	char	replybuffer[MAXMSGLEN];
	int	back, status, timeclockstate;
	u_long	seq;
	fd_set	readfds;
	struct sigvec ignore, savesig, sv;

	/* special hook to flush timeout cache */
	if (question == NULL && info == NULL) {
		lastmsg = lastinfo = (char *) NULL;
		lastmsgsum = lastinfosum = 0;
		return (0);
	}

	if (setjmp(sjalarmbuf) != 0) {
		if (def != -1) {
			if (def)
				msgtail(gettext("YES\n"));
			else
				msgtail(gettext("NO\n"));
		}
		back = def;
		goto done;
	}
	attnmessage = question;
	context = info;
	/*
	 * Only reset the state if the message changed somehow
	 */
	msgsum = addem(question);
	infosum = addem(info);
	if (lastmsg != question || lastinfo != info ||
	    lastmsgsum != msgsum || lastinfosum != infosum) {
		timeout = 0;
		if (telapsed && tstart_writing)
			*telapsed += time((time_t *)0) - *tstart_writing;
		lastmsg = question;
		lastinfo = info;
		lastmsgsum = msgsum;
		lastinfosum = infosum;
	}
	timeclockstate = (int) timeclock(0);
	alarmcatch();
	in_query_once = 1;
	ignore.sv_handler = SIG_IGN;
#ifdef USG
	(void) sigemptyset(&ignore.sv_mask);
	ignore.sv_flags = SA_RESTART;
#else
	ignore.sv_mask = 0;
	ignore.sv_flags = 0;
#endif
	FD_ZERO(&readfds);
	if (isatty(fileno(stdin)))
		FD_SET(fileno(stdin), &readfds);
	for (;;) {
		status = oper_receive(&readfds, replybuffer, MAXMSGLEN, &seq);
		if (status == OPERMSG_READY) {
			if (!FD_ISSET(fileno(stdin), &readfds))
				continue;	/* sanity check */
			if (fgets(replybuffer, MAXMSGLEN, stdin) == NULL) {
				if (ferror(stdin)) {
					clearerr(stdin);
					continue;
				} else
					dumpabort();
			}
			if (mail) {
				(void) fprintf(mail, "%s", replybuffer);
				(void) fflush(mail);
			}
		} else if (status == OPERMSG_RCVD) {
			if (seq != lastprompt) {
#ifdef DEBUG

				/* XGETTEXT:  #ifdef DEBUG only */
				msg(gettext(
				    "Response %lu does not match prompt %lu\n"),
				    seq, lastprompt);
#endif
				continue;	/* not a reply to our prompt */
			} else {
				msgtail(replybuffer);
				if (isatty(fileno(stdin))) {
					(void) sigvec(SIGTTOU,
						&ignore, &savesig);
					if (ioctl(fileno(stdin), TCFLSH,
					    TCIFLUSH) < 0)
						msg(gettext(
				"Warning - error discarding terminal input\n"));
					(void) sigvec(SIGTTOU,
						&savesig, (struct sigvec *)0);
				}
			}
		} else {	/* OPERMSG_ERROR */
			if (ferror(stdin)) {
				clearerr(stdin);
				continue;
			} else
				dumpabort();
		}
		if (lastprompt)
			(void) oper_cancel(lastprompt, 1);
		timeout = 0;
		if (strcasecmp(replybuffer, gettext("yes\n")) == 0) {
				back = 1;
				lastmsg = lastinfo = (char *) NULL;
				lastmsgsum = lastinfosum = 0;
				goto done;
		} else if (strcasecmp(replybuffer, gettext("no\n")) == 0) {
				back = 0;
				lastmsg = lastinfo = (char *) NULL;
				lastmsgsum = lastinfosum = 0;
				goto done;
		} else {
			msg(gettext("\"yes\" or \"no\"?\n"));
			in_query_once = 0;
			alarmcatch();
			in_query_once = 1;
		}
	}
done:
	/*
	 * Turn off the alarm, and reset the signal to trap out..
	 */
	(void) alarm(0);
	attnmessage = NULL;
	sv.sv_handler = sigAbort;
	sv.sv_flags = SA_RESTART;
#ifdef USG
	(void) sigemptyset(&sv.sa_mask);
#else
	sv.sv_mask = 0;
#endif
	(void) sigvec(SIGALRM, &sv, (struct sigvec *)0);
	if (tstart_writing)
		(void) time(tstart_writing);
	(void) timeclock(timeclockstate);
	in_query_once = 0;
	return (back);
}
/*
 *	Alert the console operator, and enable the alarm clock to
 *	sleep for time += 2 minutes in case nobody comes to satisfy dump
 *	If the alarm goes off while in the query_once for loop, we just
 *	longjmp back there and return the default answer.
 */
static void
#ifdef __STDC__
alarmcatch(void)
#else
alarmcatch()
#endif
{
	static char cbuf[256];
	static time_t issued;
	struct sigvec sv;

	if (in_query_once) {
		if (lastprompt)
			(void) oper_cancel(lastprompt, 1);
		lastprompt = 0;
		longjmp(sjalarmbuf, 1);
	}
	if (timeout) {
		msgtail("\n");
		if (issued && (issued + 3600 <= time((time_t *)0))) {
			/*
			 * Try to get help (via mail) if we haven't
			 * gotten a response in an hour.  We only
			 * attempt this once per prompt.  If we aren't
			 * currently connected, however, we will try again
			 * later, when we might be connected.
			 */
			if (gethelp() == 0)
				issued = 0;
		}
	} else
		issued = time((time_t *)0);
	timeout += 120;
	msg(gettext("NEEDS ATTENTION: %s"), attnmessage);
	if (lastprompt)
		(void) oper_cancel(lastprompt, 1);
	doingprompt = 1;
	if (context)
		(void) sprintf(cbuf, " (%.*s):", sizeof (cbuf) - 5,  context);
	lastprompt = opermes(LOG_ALERT, gettext("NEEDS ATTENTION%s %s"),
	    context ? cbuf : ":", attnmessage);
	sv.sv_handler = alarmcatch;
	sv.sv_flags = SA_RESTART;
#ifdef USG
	(void) sigemptyset(&sv.sa_mask);
#else
	sv.sv_mask = 0;
#endif
	(void) sigvec(SIGALRM, &sv, (struct sigvec *)0);
	(void) alarm(timeout);
}

/*
 *	Here if an inquisitive operator interrupts the dump program
 */
/*ARGSUSED*/
void
interrupt(sig)
	int	sig;
{
	char *text =  gettext("Interrupt received");

	if (!saveattn) {
		saveattn = attnmessage;
		savecontext = context;
	}
	msg("%s.\n", text);
	(void) opermes(LOG_CRIT, "%s.\n", text);
	if (query(gettext("Do you want to abort dump?: (\"yes\" or \"no\") "),
	    text))
		dumpabort();
	if (saveattn) {
		attnmessage = saveattn;
		context = savecontext;
		saveattn = savecontext = NULL;
		alarmcatch();
	}
}

/*
 *	The following variables and routines manage alerting
 *	operators to the status of dump.
 *	This works much like wall(1) does.
 */
static struct group *gp;

/*
 *	Get the names from the group entry "operator" to notify.
 */
void
#ifdef __STDC__
set_operators(void)
#else
set_operators()
#endif
{
	if (!notify)		/* not going to notify */
		return;
	gp = getgrnam(OPGRENT);
	(void) endgrent();
	if (gp == (struct group *)0) {
		msg(gettext("No entry in %s for `%s'.\n"),
			"/etc/group", OPGRENT);
		notify = 0;
		return;
	}
}

#ifndef USG
static FILE *f_utmp = NULL;

static void
#ifdef __STDC__
setutent(void)
#else
setutent()
#endif
{
	f_utmp = fopen("/etc/utmp", "r");
}

static struct utmp *
#ifdef __STDC__
getutent(void)
#else
getutent()
#endif
{
	static struct utmp utmp;

	if (f_utmp == NULL) {
		msg("/etc/utmp: %s\n", strerror(errno));
		Exit(0);
	}
	if (fread((char *)&utmp, sizeof (utmp), 1, f_utmp) == 1)
		return (&utmp);
	return (NULL);
}

static void
#ifdef __STDC__
endutent(void)
#else
endutent()
#endif
{
	(void) fclose(f_utmp);
	f_utmp = NULL;
}
#endif

static struct tm *localclock;
static jmp_buf sjbuf;

static void
unhang()
{
	longjmp(sjbuf, 1);
}

/*
 *	We fork a child to do the actual broadcasting, so
 *	that the process control groups are not messed up
 */
void
broadcast(message)
	char	*message;
{
	time_t	clock;
	struct	utmp	*utp;
	char	**np;
	int	pid;
	static struct sigvec sv;

	if (!notify || gp == 0)
		return;
	dobroadcast = 1;
	(void) opermes(LOG_CRIT, message);
	switch (pid = fork()) {
	case -1:
		return;
	case 0:
		break;
	default:
		while (wait((int *)0) != pid)
			continue;
		return;
	}

	msginit();
	clock = time((time_t *)0);
	localclock = localtime(&clock);

	sv.sv_handler = unhang;
#ifdef USG
	(void) sigemptyset(&sv.sv_mask);
	sv.sv_flags = SA_RESTART;
#else
	sv.sv_mask = 0;
	sv.sv_flags = 0;
#endif

	(void) setreuid(-1, 0);
	setutent();
	while ((utp = getutent()) != NULL) {
		if (utp->ut_name[0] == 0)
			continue;
		for (np = gp->gr_mem; *np; np++) {
			if (strncmp(*np, utp->ut_name,
			    sizeof (utp->ut_name)) != 0)
				continue;
			/*
			 * Do not send messages to operators on dialups
			 */
			if (strncmp(utp->ut_line, DIALUP, strlen(DIALUP)) == 0)
				continue;
#ifdef DEBUG

			/* XGETTEXT:  #ifdef DEBUG only */
			msg(gettext("Message to %s at %s\n"),
				utp->ut_name, utp->ut_line);
#endif /* DEBUG */
			if (setjmp(sjbuf) != 0)
				continue;
			(void) sigvec(SIGALRM, &sv, (struct sigvec *)0);
			(void) alarm(10);
			sendmes(utp->ut_line, message);
			(void) alarm(0);
		}
	}
	endutent();
	Exit(0);	/* the wait in this same routine will catch this */
	/* NOTREACHED */
}

static void
sendmes(tty, message)
	char *tty, *message;
{
	char t[50], buf[BUFSIZ];
	register char *cp;
	register int c, ch;
	int	msize;
	FILE *f_tty;
	int fd;

	msize = strlen(message);
	(void) strcpy(t, "/dev/");
	(void) strcat(t, tty);

	/* check if device is really a tty, Bug IDs 1140162 & 1175539 */
	if ((fd = open(t, O_WRONLY|O_NOCTTY)) == -1) {
		fprintf(stderr, "Cannot open %s.\n", t);
		return;
	} else {
		if (!isatty(fd)) {
			fprintf(stderr, "%s in utmp is not a tty\n", tty);
			openlog("ufsdump", 0, LOG_AUTH);
			syslog(LOG_CRIT, "%s in utmp is not a tty\n", tty);
			closelog();
			close(fd);
			return;
		}
	}

	if ((f_tty = fdopen(fd, "w")) != NULL) {
		setbuf(f_tty, buf);
		(void) fprintf(f_tty, gettext(
"\n\007\007\007Message from the dump program to all operators at \
%d:%02d ...\r\n\n"),
		    localclock->tm_hour, localclock->tm_min);
		for (cp = message, c = msize; c-- > 0; cp++) {
			ch = *cp;
			if (ch == '\n')
				if (putc('\r', f_tty) == EOF)
					break;
			if (putc(ch, f_tty) == EOF)
				break;
		}
		(void) fclose(f_tty);
	}
	close(fd);
}

/*
 *	print out an estimate of the amount of time left to do the dump
 */
#define	EST_SEC	600			/* every 10 minutes */
void
timeest(force, blkswritten)
	int force;
	int blkswritten;
{
	static time_t tschedule = 0;
	time_t	tnow, deltat;
	char *msgp;

	if (tschedule == 0)
		tschedule = time((time_t *)0) + EST_SEC;
	(void) time(&tnow);
	if ((force || tnow >= tschedule) && blkswritten) {
		tschedule = tnow + EST_SEC;
		if (!force && blkswritten < 50 * ntrec)
			return;
		deltat = (*telapsed + (tnow - *tstart_writing))
				* ((double)esize / blkswritten - 1.0);
		msgp = gettext("%3.2f%% done, finished in %d:%02d\n");
		msg(msgp, (blkswritten*100.0)/esize,
			deltat/3600, (deltat%3600)/60);
	}
}

#include <stdarg.h>

/* VARARGS1 */
void
msg(const char *fmt, ...)
{
	char buf[1024], *cp;
	va_list args;

#ifdef USG
	va_start(args, fmt);
#else
	va_start(args);
#endif
	if (metamucil_mode == METAMUCIL)
		(void) strcpy(buf, "  HSMDUMP: ");
	else
		(void) strcpy(buf, "  DUMP: ");
	cp = &buf[strlen(buf)];
#ifdef TDEBUG
	(void) sprintf(cp, "pid=%d ", getpid());
	cp = &buf[strlen(buf)];
#endif
	(void) vsprintf(cp, fmt, args);
	(void) fputs(buf, stderr);
	(void) fflush(stdout);
	(void) fflush(stderr);
	if (mail) {
		(void) fputs(buf, mail);
		(void) fflush(mail);
	}
	va_end(args);
}

/* VARARGS1 */
void
msgtail(const char *fmt, ...)
{
	va_list args;

#ifdef USG
	va_start(args, fmt);
#else
	va_start(args);
#endif
	(void) vfprintf(stderr, fmt, args);
	if (mail) {
		(void) vfprintf(mail, fmt, args);
		(void) fflush(mail);
	}
	va_end(args);
}

#define	MINUTES(x)	((x) * 60)

/* VARARGS2 */
u_long
opermes(const int level, const char *fmt, ...)
{
	va_list args;
	int	flags, tries;
	time_t	ttl;
	static char msgbuf[MAXMSGLEN];
	u_long lastmsg;
	/*
	 * Messages will be kept around until
	 * their time-to-live expires.  For
	 * prompts, this is the greater of the
	 * current timeout or the appropriate
	 * table value.
	 */
	static time_t ttltab[LOG_PRIMASK+1] = {
		MINUTES(30),	/* LOG_EMERG */
		MINUTES(30),	/* LOG_ALERT */
		MINUTES(30),	/* LOG_CRIT */
		MINUTES(15),	/* LOG_ERR */
		MINUTES(15),	/* LOG_WARNING */
		MINUTES(15),	/* LOG_NOTICE */
		MINUTES(10),	/* LOG_INFO */
		MINUTES(1),	/* LOG_DEBUG */
	};
#ifndef USG
	int	omask;

	if (metamucil_mode == NOT_METAMUCIL)
		return (0);
	va_start(args);
#else
	if (metamucil_mode == NOT_METAMUCIL)
		return (0);
	va_start(args, fmt);
#endif
	if (level < 0 || level > LOG_PRIMASK) {
		msg("bad level %d to message system\n", level);
		va_end(args);
		return (0);
	}
	(void) vsprintf(msgbuf, fmt, args);
	flags = 0;
	if (doingprompt) {
		flags = MSG_DISPLAY|MSG_NEEDREPLY;
		ttl = timeout > ttltab[level] ? timeout : ttltab[level];
		doingprompt = 0;
	} else
		ttl = ttltab[level];
	if (dobroadcast) {
		flags |= MSG_OPERATOR;			/* don't display it */
		dobroadcast = 0;
	} else
		flags |= MSG_DISPLAY;
	for (lastmsg = 0L, tries = 2; lastmsg == 0 && tries > 0; tries--) {
#ifdef USG
		sigset_t mask, omask;

		(void) sigemptyset(&mask);
		(void) sigaddset(&mask, SIGINT);
		(void) sigprocmask(SIG_BLOCK, &mask, &omask);
#else
		omask = sigblock(sigmask(SIGINT));
#endif
		lastmsg = oper_send(ttl, level, flags, msgbuf);
#ifdef USG
		(void) sigprocmask(SIG_SETMASK, &omask, (sigset_t *)0);
#else
		(void) sigsetmask(omask);
#endif
		if (lastmsg == 0) {
			int wasconnected = connected;
			msginit();
			if (wasconnected && connected == 0)
				msg(gettext(
				    "Warning - disconnecting from rpc.operd\n"),
					opserver);
		}
	}
	va_end(args);
	return (lastmsg);
}

#ifndef USG
char *
strerror(err)
	int err;
{
	extern int sys_nerr;
	extern char *sys_errlist[];

	static char errmsg[32];

	if (err >= 0 && err < sys_nerr)
		return (sys_errlist[err]);

	(void) sprintf(errmsg, gettext("Error %d"), err);
	return (errmsg);
}
#endif

/*
 *	Tell the operator what has to be done;
 *	we don't actually do it
 */
void
lastdump(arg)		/* w ==> just what to do; W ==> most recent dumps */
	int	arg;
{
	char *lastname;
	char *date;
	register int i;
	time_t tnow;
	register struct mntent *dt;
	int dumpme;
	register struct idates *itwalk;

	(void) time(&tnow);
	mnttabread();		/* /etc/fstab input */
	inititimes();		/* /etc/dumpdates input */
	qsort((char *)idatev, nidates, sizeof (struct idates *), idatesort);

	if (arg == 'w')
		(void) fprintf(stdout, gettext("Dump these file systems:\n"));
	else
		(void) fprintf(stdout, gettext(
		    "Last dump(s) done (Dump '>' file systems):\n"));
	lastname = "??";
	ITITERATE(i, itwalk) {
		if (strncmp(lastname, itwalk->id_name,
		    sizeof (itwalk->id_name)) == 0)
			continue;
		date = (char *)ctime(&itwalk->id_ddate); /* XXX must be ctime */
		date[16] = '\0';	/* blast away seconds and year */
		lastname = itwalk->id_name;
		dt = mnttabsearch(itwalk->id_name, 0);
#ifdef USG
		dumpme = 1;	/* XXX - should get freq out of mntopts */
#else
		dumpme = ((dt != 0) &&
		    (dt->mnt_freq != 0) &&
		    (itwalk->id_ddate < tnow - (dt->mnt_freq*DAY)));
#endif
		if ((arg != 'w') || dumpme)
			(void) printf(gettext(
			    "%c %8s\t(%6s) Last dump: Level %c, Date %s\n"),
			    dumpme && (arg != 'w') ? '>' : ' ',
			    itwalk->id_name,
			    dt ? dt->mnt_dir : "",
			    (u_char)itwalk->id_incno,
			    date);
	}
}

static int
idatesort(v1, v2)
#ifdef __STDC__
	const void *v1;
	const void *v2;
#else
	void *v1;
	void *v2;
#endif
{
	struct idates **p1 = (struct idates **)v1;
	struct idates **p2 = (struct idates **)v2;
	int diff;

	/*
	 * XXX [sas]
	 * This was strncpy, w/sizeof (*p1)->id_name as the
	 * third arg.  Why?
	 */
	diff = strcoll((*p1)->id_name, (*p2)->id_name);
	if (diff == 0)
		return ((*p2)->id_ddate - (*p1)->id_ddate);
	else
		return (diff);
}

/*
 *	Get operator input.
 *	Every 2 minutes we reprint the message, alerting others
 *	that dump needs attention.
 */
char *
getinput(question, info)
	char	*question;
	char	*info;
{
	static char replybuffer[MAXMSGLEN];
	char	*cp;
	int	status;
	time_t	now;
	fd_set	readfds;
	u_long	seq;
	struct sigvec ignore, savesig, sv;

	attnmessage = question;
	context = info;
	timeout = 0;
	now = time((time_t *)0);
	if (telapsed && tstart_writing)
		*telapsed += now - *tstart_writing;
	alarmcatch();
	ignore.sv_handler = SIG_IGN;
#ifdef USG
	(void) sigemptyset(&ignore.sv_mask);
	ignore.sv_flags = SA_RESTART;
#else
	ignore.sv_mask = 0;
	ignore.sv_flags = 0;
#endif
	FD_ZERO(&readfds);
	if (isatty(fileno(stdin)))
		FD_SET(fileno(stdin), &readfds);
	for (;;) {
		status = oper_receive(&readfds, replybuffer, MAXMSGLEN, &seq);
		if (status == OPERMSG_READY) {
			if (!FD_ISSET(fileno(stdin), &readfds))
				continue;	/* sanity check */
			if (fgets(replybuffer, MAXMSGLEN, stdin) == NULL) {
				if (ferror(stdin)) {
					clearerr(stdin);
					continue;
				} else
					dumpabort();
			}
			if (mail) {
				(void) fprintf(mail, "%s", replybuffer);
				(void) fflush(mail);
			}
		} else if (status == OPERMSG_RCVD) {
			if (seq != lastprompt) {
#ifdef DEBUG

				/* XGETTEXT:  #ifdef DEBUG only */
				msg(gettext(
				    "Response %lu does not match prompt %lu\n"),
				    seq, lastprompt);
#endif
				continue;	/* not a reply to our prompt */
			} else {
				msgtail(replybuffer);
				if (isatty(fileno(stdin))) {
					(void) sigvec(SIGTTOU,
						&ignore, &savesig);
					if (ioctl(fileno(stdin), TCFLSH,
					    TCIFLUSH) < 0)
						msg(gettext(
				"Warning - error discarding terminal input\n"));
					(void) sigvec(SIGTTOU,
						&savesig, (struct sigvec *)0);
				}
			}
		} else {	/* OPERMSG_ERROR */
			if (ferror(stdin)) {
				clearerr(stdin);
				continue;
			} else
				dumpabort();
		}
		if (lastprompt)
			(void) oper_cancel(lastprompt, 1);
		timeout = 0;
		if (*replybuffer == '\n') {
			msg(gettext("You must provide a response\n"));
			(void) opermes(LOG_CRIT, gettext(
			    "You must provide a response\n"));
			alarmcatch();
		} else
			break;
	}
	/*
	 * Turn off the alarm, and reset the signal to trap out..
	 */
	(void) alarm(0);
	attnmessage = NULL;
	sv.sv_handler = sigAbort;
	sv.sv_flags = SA_RESTART;
#ifdef USG
	(void) sigemptyset(&sv.sa_mask);
#else
	sv.sv_mask = 0;
#endif
	(void) sigvec(SIGALRM, &sv, (struct sigvec *)0);
	if (tstart_writing)
		(void) time(tstart_writing);
	if (cp = strchr(replybuffer, '\n'))
		*cp = '\0';
	return (replybuffer);
}

void
#ifdef __STDC__
msginit(void)
#else
msginit()
#endif
{
	connected = 0;
	if (metamucil_mode == METAMUCIL) {
		(void) getopserver(opserver, sizeof (opserver));
#ifdef DEBUG
		if (oper_init(opserver, myname, 1) != OPERMSG_CONNECTED)
#else
		if (oper_init(opserver, myname, 0) != OPERMSG_CONNECTED)
#endif
			return;
		connected++;
#ifdef DEBUG

		/* XGETTEXT:  #ifdef DEBUG only */
		msg(gettext("Connected to operator daemon on %s\n"), opserver);
#endif
	}
#ifdef DEBUG
	if (xflag)
		printf("MSGINIT: connected = %d\n", connected);
#endif
}

void
#ifdef __STDC__
msgend(void)
#else
msgend()
#endif
{
	oper_end();
	connected = 0;
}

static char mailcmd[] = "/usr/lib/sendmail";
/*
 * Open a pipe to mail.  The opened stream
 * is shared across all processes.  The mail
 * program invoked here should not create
 * temporary files during the dump in order
 * to avoid conflicts with on-line locking.
 */
void
#ifdef __STDC__
setupmail(void)
#else
setupmail()
#endif
{
	struct sigvec ignore, savesig;
	char	*cmdbuf;
	char	maillist[1024];

	(void) getmail(maillist, sizeof (maillist));
	if (maillist[0] == '\0')
		return;
	cmdbuf = xmalloc((strlen(mailcmd)+strlen(maillist)+2));
	(void) sprintf(cmdbuf, "%s %s", mailcmd, maillist);
	ignore.sv_handler = SIG_IGN;
#ifdef USG
	(void) sigemptyset(&ignore.sv_mask);
	ignore.sv_flags = SA_RESTART;
#else
	ignore.sv_mask = 0;
	ignore.sv_flags = 0;
#endif
	(void) sigvec(SIGINT, &ignore, &savesig);
	mail = popen(cmdbuf, "w");
	(void) sigvec(SIGINT, &savesig, (struct sigvec *)0);
	if (mail == NULL) {
		msg(gettext("Cannot open pipe to mail program `%s': %s\n"),
		    mailcmd, strerror(errno));
		dumpabort();
	}
	free(cmdbuf);
	(void) fprintf(mail, gettext("%s: DUMP RESULTS FROM %s:%s\n\n"),
	    "Subject", spcl.c_host, filesystem ? filesystem : spcl.c_filesys);
}

void
#ifdef __STDC__
sendmail(void)
#else
sendmail()
#endif
{
	if (!mail)
		return;
	(void) pclose(mail);
}

static int
#ifdef __STDC__
gethelp(void)
#else
gethelp()
#endif
{
	struct sigvec ignore, savesig;
	char	*cmdbuf;
	FILE	*fp;

	if (!helplist)
		return (0);
	if (!connected)
		return (1);		/* so we'll try again later */

	cmdbuf = xmalloc((strlen(mailcmd)+strlen(helplist)+2));
	(void) sprintf(cmdbuf, "%s %s", mailcmd, helplist);
	ignore.sv_handler = SIG_IGN;
#ifdef USG
	(void) sigemptyset(&ignore.sv_mask);
	ignore.sv_flags = SA_RESTART;
#else
	ignore.sv_mask = 0;
	ignore.sv_flags = 0;
#endif
	(void) sigvec(SIGINT, &ignore, &savesig);
	fp = popen(cmdbuf, "w");
	(void) sigvec(SIGINT, &savesig, (struct sigvec *)0);
	if (fp == NULL) {
		msg(gettext("Cannot open pipe to mail program `%s': %s\n"),
		    cmdbuf, strerror(errno));
		dumpabort();
	}
	free(cmdbuf);
	(void) fprintf(fp, gettext("%s: DUMP PROBLEM\n\n"), "Subject");
	(void) fprintf(fp, gettext(
	    "The dump of %s:%s has encountered the following problem:\n\n"),
	    spcl.c_host, filesystem ? filesystem : spcl.c_filesys);
	(void) fprintf(fp, gettext("DUMP NEEDS ATTENTION"));
	if (context)
		(void) fprintf(fp, " (%s)", context);
	(void) fprintf(fp, ": %s\n\n", attnmessage);
	(void) fprintf(fp, gettext(
		"You may use opermon(1M) to resolve this problem.\n"));
	(void) pclose(fp);
	return (0);
}
