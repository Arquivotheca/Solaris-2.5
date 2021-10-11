/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)syslog.c	1.18	95/09/12 SMI"	/* SVr4.0 1.6	*/

/* from "syslog.c 1.18 88/02/08 SMI"; from UCB 5.9 5/7/86 */

/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *  		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986,1987,1988,1989  Sun Microsystems, Inc.
 *  	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	    All rights reserved.
 */

/*
 * SYSLOG -- print message on log file
 *
 * This routine looks a lot like printf, except that it
 * outputs to the log file instead of the standard output.
 * Also:
 *	adds a timestamp,
 *	prints the module name in front of the message,
 *	has some other formatting types (or will sometime),
 *	adds a newline on the end of the message.
 *
 * The output of this routine is intended to be read by /etc/syslogd.
 */

#ifndef DSHLIB
#ifdef __STDC__
#pragma weak syslog = _syslog
#pragma weak vsyslog = _vsyslog
#pragma weak openlog = _openlog
#pragma weak closelog = _closelog
#pragma weak setlogmask = _setlogmask
#endif
#endif
#include "synonyms.h"
#include <sys/types.h>
#include <sys/stropts.h>
#include <sys/strlog.h>
#include <sys/stream.h>		/* for sys/log.h - sigh */
#include <sys/log.h>		/* for LOG_MAXPS */
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
/* #include <vfork.h> */
#include <errno.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#define	MAXLINE		1024		/* max message size (but see below) */
#define	NULL		0		/* manifest */

#define	PRIMASK(p)	(1 << ((p) & LOG_PRIMASK))
#define	PRIFAC(p)	(((p) & LOG_FACMASK) >> 3)
#define	IMPORTANT 	LOG_ERR

#define	logname		"/dev/conslog"
#define	ctty		"/dev/syscon"
#define	pidfile		"/etc/syslog.pid"

int __vsnprintf();
int __snprintf();

static struct __syslog {
	int	_LogFile;
	int	_LogStat;
	const char	*_LogTag;
	int	_LogMask;
	char	*_SyslogHost;
	int	_LogFacility;
	int	_PidFile;
} *__syslog;

#define	LogFile (__syslog->_LogFile)
#define	LogStat (__syslog->_LogStat)
#define	LogTag (__syslog->_LogTag)
#define	LogMask (__syslog->_LogMask)
#define	SyslogHost (__syslog->_SyslogHost)
#define	LogFacility (__syslog->_LogFacility)
#define	PidFile (__syslog->_PidFile)

#ifdef _REENTRANT
static mutex_t _syslog_lk = DEFAULTMUTEX;
#endif _REENTRANT

static int
allocstatic()
{
	_mutex_lock(&_syslog_lk);
	if (__syslog != 0) {
		_mutex_unlock(&_syslog_lk);
		return (1);
	}
	__syslog = (struct __syslog *)calloc(1, sizeof (struct __syslog));
	if (__syslog == 0) {
		_mutex_unlock(&_syslog_lk);
		return (0);	/* can't do it */
	}
	LogFile = -1;		/* fd for log */
	PidFile = -1;		/* fd for syslog.pid */
	LogStat = 0;		/* status bits, set by openlog() */
	LogTag = "syslog";	/* string to tag the entry with */
	LogMask = 0xff;		/* mask of priorities to be logged */
	LogFacility = LOG_USER;	/* default facility code */
	_mutex_unlock(&_syslog_lk);
	return (1);
}

/*VARARGS2*/
void
syslog(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap,);
	vsyslog(pri, fmt, ap);
	va_end(ap);
}

void
vsyslog(int pri, const char *fmt, va_list ap)
{
	register char *b, *f, *o;
	register int c;
	char buf[MAXLINE + 2];
	char outline[MAXLINE + 256];  /* pad to allow date, system name... */
	long now;
	pid_t pid;
	struct log_ctl hdr;
	struct strbuf dat;
	struct strbuf ctl;
	sigset_t sigs;
	char timestr[26];	/* hardwired value 26 due to Posix */
	int taglen;
	int olderrno = errno;
/*
 * Maximum tag length is 256 (the pad in outline) minus the size of the
 * other things that can go in the pad.
 */
#define	MAX_TAG		230

	if (__syslog == 0 && !allocstatic())
		return;
	/* see if we should just throw out this message */
	if (pri < 0 || PRIFAC(pri) >= LOG_NFACILITIES ||
	    (PRIMASK(pri) & LogMask) == 0)
		return;
	if (LogFile < 0)
		openlog(LogTag, LogStat | LOG_NDELAY, 0);

	/* set default facility if none specified */
	if ((pri & LOG_FACMASK) == 0)
		pri |= LogFacility;

	/* build the header */
	hdr.pri = pri;
	hdr.flags = SL_CONSOLE;
	hdr.level = 0;

	/* build the message */
	/*
	 * To avoid potential security problems, bounds checking is done
	 * on outline and buf.
	 * The following code presumes that the header information will
	 * fit in 250-odd bytes, as was accounted for in the buffer size
	 * allocation.  This is dependent on the assumption that the LogTag
	 * and the string returned by sprintf() for getpid() will return
	 * be less than 230-odd characters combined.
	 */
	o = outline;
	time(&now);
	sprintf(o, "%.15s ", ctime_r(&now, timestr, 26) + 4);
	o += strlen(o);

	if (LogTag) {
		taglen = strlen(LogTag) < MAX_TAG ? strlen(LogTag) : MAX_TAG;
		strncpy(o, LogTag, taglen);
		o[taglen] = '\0';
		o += strlen(o);
	}
	if (LogStat & LOG_PID) {
		sprintf(o, "[%d]", getpid());
		o += strlen(o);
	}
	if (LogTag) {
		strcpy(o, ": ");
		o += 2;
	}

	b = buf;
	f = (char *)fmt;
	while ((c = *f++) != '\0' && b < &buf[MAXLINE]) {
		char *errmsg;
		if (c != '%') {
			*b++ = c;
			continue;
		}
		if ((c = *f++) != 'm') {
			*b++ = '%';
			*b++ = c;
			continue;
		}
		if ((errmsg = strerror(olderrno)) == NULL)
			__snprintf(b, &buf[MAXLINE] - b, "error %d",
			    olderrno);
		else {
			while (*errmsg != '\0' && b < &buf[MAXLINE]) {
				if (*errmsg == '%') {
					strcpy(b, "%%");
					b += 2;
				}
				else
					*b++ = *errmsg;
				errmsg++;
			}
			*b = '\0';
		}
		b += strlen(b);
	}
	if (b > buf && *(b-1) != '\n')	/* ensure at least one newline */
		*b++ = '\n';
	*b = '\0';
	__vsnprintf(o, &outline[sizeof(outline)] - o, buf, ap);
	c = strlen(outline) + 1;	/* add one for NULL byte */
	if (c > MAXLINE) {
		c = MAXLINE;
		outline[MAXLINE-1] = '\0';
	}

	/*
	 * 1136432 points out that the underlying log driver actually
	 * refuses to accept (ERANGE) messages longer than LOG_MAXPS
	 * bytes.  So it really doesn't make much sense to putmsg a
	 * longer message..
	 */
	if (c > LOG_MAXPS) {
		c = LOG_MAXPS;
		outline[LOG_MAXPS-1] = '\0';
	}

	/* set up the strbufs */
	ctl.maxlen = sizeof (struct log_ctl);
	ctl.len = sizeof (struct log_ctl);
	ctl.buf = (caddr_t)&hdr;
	dat.maxlen = sizeof (outline);
	dat.len = c;
	dat.buf = outline;

	if (syslogd_ok()) {
		/* output the message to the local logger */
		if (putmsg(LogFile, &ctl, &dat, 0) >= 0)
			return;
	}
	if (!(LogStat & LOG_CONS))
		return;

	/* output the message to the console */
	pid = fork();
	if (pid == -1)
		return;
	if (pid == 0) {
		int fd;

		signal(SIGALRM, SIG_DFL);
		sigprocmask(SIG_BLOCK, NULL, &sigs);
		sigdelset(&sigs, SIGALRM);
		sigprocmask(SIG_SETMASK, &sigs, NULL);
		alarm(5);
		fd = open(ctty, O_WRONLY);
		if (fd >= 0) {
			alarm(0);
			strcat(o, "\r");
			write(fd, outline, c+1);
			close(fd);
		}
		_exit(0);
	}
	if (!(LogStat & LOG_NOWAIT))
		waitpid(pid, (int *)0, 0);
}

static int
syslogd_ok()
{
	/*
	 * Read lock on whole file
	 */
	flock_t rflk = {F_RDLCK, 0, 0, 0};
	/*
	 * unlock if read lock succeeds
	 */
	flock_t uflk = {F_UNLCK, 0, 0, 0};

	if (PidFile < 0) {
		PidFile = open(pidfile, O_RDONLY);
		if (PidFile < 0)
			return (0);
		fcntl(PidFile, F_SETFD, 1);
	}
	/*
	 * if syslogd is up, this should fail
	 */
	if (fcntl(PidFile, F_SETLK, &rflk) != -1) {
		fcntl(PidFile, F_SETLK, &uflk);
		return (0);
	}
	return (1);
}

/*
 * OPENLOG -- open system log
 */

void
openlog(const char *ident, int logstat, int logfac)
{
	if (__syslog == 0 && !allocstatic())
		return;
	if (ident != NULL)
		LogTag = ident;
	LogStat = logstat;
	if (logfac != 0)
		LogFacility = logfac & LOG_FACMASK;
	if (LogFile >= 0)
		return;
	if (LogStat & LOG_NDELAY) {
		LogFile = open(logname, O_WRONLY);
		fcntl(LogFile, F_SETFD, 1);
	}
}

/*
 * CLOSELOG -- close the system log
 */

void
closelog()
{

	if (__syslog == 0)
		return;
	(void) close(LogFile);
	LogFile = -1;
	(void) close(PidFile);
	PidFile = -1;
}

/*
 * SETLOGMASK -- set the log mask level
 */
int
setlogmask(int pmask)
{
	int omask;

	if (__syslog == 0 && !allocstatic())
		return;
	omask = LogMask;
	if (pmask != 0)
		LogMask = pmask;
	return (omask);
}
