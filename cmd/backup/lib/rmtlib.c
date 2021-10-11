/*LINTLIBRARY*/
/*PROTOLIB1*/
/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ident	"@(#)rmtlib.c 1.16 90/11/09 SMI" /* from UCB 5.4 12/11/85 */

#ident	"@(#)rmtlib.c 1.8 95/02/22"

#include <config.h>
#include <myrcmd.h>
#include <stdio.h>
#include <locale.h>
#include <ctype.h>
#include <pwd.h>
#include <string.h>
#include <signal.h>
#include <sys/mtio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <locale.h>
#include <stdlib.h>
#include <errno.h>

#ifdef USG
#define	sigvec		sigaction
#define	sv_handler	sa_handler
#endif
#include <netinet/in.h>

#define	TP_BSIZE	1024

#define	TS_CLOSED	0
#define	TS_OPEN		1

static int	rmtstate = TS_CLOSED;
static int	rmtape = -1;
static int	rmtversion = 0;
static char	*rmtpeer, *rmtpeer_malloc;
static int	ntrec;			/* blocking factor on tape */

static char *domainname = "hsm_libdump";	/* for dgettext() */

#ifdef __STDC__
static void rmtmsg(const char *, ...);	/* package print routine */
static void rmtconnaborted(int);
static void rmtgetconn(void);
static int rmtstatus_extended(struct mtget *);
static int rmtioctl_extended(int, int);
static int map_extended_ioctl(int);
static int okname(char *);
static int rmtcall(char *, char *);
static int rmtreply(char *);
static void rmtgets(char *, int);

/*
 * XXX header file problems
 */
extern int setsockopt(int, int, int, const char *, int);
extern int recv(int, char *, int, int);

static void (*print)(const char *, ...);	/* print routine */
static void (*Exit)(int);			/* exit routine */
#else
static void rmtmsg();
static void rmtconnaborted();
static void rmtgetconn();
static int okname();
static int rmtstatus_extended();
static int rmtioctl_extended();
static int map_extended_ioctl();
static int rmtcall();
static int rmtreply();
static void rmtgets();

static void (*print)();
static void (*Exit)();

extern void exit();
#endif

/*
 * Get a program-specific print and exit routine into
 * the package.  This is primarily for dump's benefit.
 * This routine is optional -- if not called the two
 * default to fprintf(stderr) and exit.
 */
#ifdef __STDC__
void
rmtinit(
	void (*errmsg)(const char *, ...),	/* print routine */
	void (*errexit)(int))			/* exit routine */
#else
void
rmtinit(errmsg, errexit)
	void (*errmsg)();			/* print routine */
	void (*errexit)();			/* exit routine */
#endif
{
	print = errmsg;
	Exit = errexit;
}

rmthost(host, blocksize)
	char	*host;
	int	blocksize;			/* in Kbytes per tape block */
{
	struct sigvec sv;

#ifdef __STDC__
	if (print == (void (*)(const char *, ...))0)
#else
	if (print == (void (*)())0)
#endif
		print = rmtmsg;
#ifdef __STDC__
	if (Exit == (void (*)(int))0)
#else
	if (Exit == (void (*)())0)
#endif
		Exit = exit;
	if (rmtape >= 0 && rmtstate != TS_OPEN) {
		(void) close(rmtape);
		rmtape = -1;
	}
	if (rmtpeer_malloc)
		(void) free(rmtpeer_malloc);
	rmtpeer = rmtpeer_malloc = strdup(host);
	if (rmtpeer == (char *)0)
		return (0);
	ntrec = blocksize;
#ifdef USG
	sv.sa_flags = SA_RESTART;
	(void) sigemptyset(&sv.sa_mask);
#else
	sv.sv_flags = 0;
	sv.sv_mask = 0;
#endif
	sv.sv_handler = rmtconnaborted;
	(void) sigvec(SIGPIPE, &sv, (struct sigvec *)0);
	rmtgetconn();
	if (rmtape < 0)
		return (0);
	return (1);
}

/* ARGSUSED */
static void
rmtconnaborted(sig)
	int	sig;
{
	print(dgettext(domainname, "Lost connection to remote host.\n"));
	Exit(1);
}

static void
#ifdef __STDC__
rmtgetconn(void)
#else
rmtgetconn()
#endif
{
	static struct servent *sp = 0;
	static struct passwd *pwd = 0;
	char *tuser, *host, *device;
	int size;

	if (sp == 0) {
		sp = getservbyname("shell", "tcp");
		if (sp == 0) {
			print(dgettext(domainname,
				"shell/tcp: unknown service\n"));
			Exit(1);
		}
		pwd = getpwuid(getuid());
		if (pwd == 0) {
			print(dgettext(domainname,
				"Cannot find password entry for uid %d\n"),
				getuid());
			Exit(1);
		}
	}
	host = strrchr(rmtpeer, '@');
	if (host) {
		tuser = rmtpeer;
		*host++ = 0;
		rmtpeer = host;
		if (!okname(tuser))
			Exit(1);
	} else {
		host = rmtpeer;
		tuser = pwd->pw_name;
	}
	device = strrchr(host, ':');
	if (device)
		*device = 0;	/* throw away device name */
	rmtape = myrcmd(&rmtpeer, (u_short)sp->s_port, pwd->pw_name,
			tuser, "/etc/rmt", (int *)0);
	if (rmtape < 0) {
		if (*myrcmd_stderr)
			print("%s", myrcmd_stderr);
	} else {
		size = ntrec * TP_BSIZE;
		while (size > TP_BSIZE &&
		    setsockopt(rmtape, SOL_SOCKET, SO_SNDBUF, (char *)&size,
		    sizeof (size)) < 0)
			size -= TP_BSIZE;
	}
}

static int
okname(cp0)
	char *cp0;
{
	register char *cp;
	register int c;

	for (cp = cp0; *cp; cp++) {
		c = *cp;
		if (!isascii((u_char)c) || !(isalnum((u_char)c) ||
		    c == '_' || c == '-')) {
			print(dgettext(domainname,
				"invalid user name %s\n"), cp0);
			return (0);
		}
	}
	return (1);
}

rmtopen(tape, mode)
	char *tape;
	int mode;
{
	char buf[256];
	int fd;

	(void) sprintf(buf, "O%s\n%d\n", tape, mode);
	rmtstate = TS_OPEN;
	fd = rmtcall(tape, buf);
	if (fd != -1) {
		/* see if the rmt server supports the extended protocol */
		rmtversion = rmtioctl(-1, 0);
	}
	return (fd);
}

void
#ifdef __STDC__
rmtclose(void)
#else
rmtclose()
#endif
{
	if (rmtstate != TS_OPEN)
		return;
	(void) rmtcall("close", "C\n");
	rmtstate = TS_CLOSED;
}

rmtstatus(mt)
	struct mtget *mt;
{
	char *buf = (char *)mt;
	int n, i, cc;

	if (rmtversion > 0)
		return (rmtstatus_extended(mt));

	n = rmtcall("status", "S");
	if (n < 0) {
		return (-1);
	}
	i = 0;
	while (i < n) {
		cc = read(rmtape, buf+i, n - i);
		if (cc <= 0)
			rmtconnaborted(0);
		i += cc;
	}
	return (n);
}

static int
rmtstatus_extended(mt)
	struct mtget *mt;
{
	if ((mt->mt_type = rmtcall("status", "sT")) == -1)
		return (-1);
	mt->mt_dsreg = rmtcall("status", "sD");
	mt->mt_erreg = rmtcall("status", "sE");
	mt->mt_resid = rmtcall("status", "sR");
	mt->mt_fileno = rmtcall("status", "sF");
	mt->mt_blkno = rmtcall("status", "sB");
	mt->mt_flags = rmtcall("status", "sf");
	mt->mt_bf = rmtcall("status", "sb");
	return (0);
}

rmtread(buf, count)
	char *buf;
	int count;
{
	char line[30];
	int n, i, cc;

	(void) sprintf(line, "R%d\n", count);
	n = rmtcall("read", line);
	if (n < 0) {
		return (-1);
	}
	i = 0;
	while (i < n) {
		cc = read(rmtape, buf+i, n - i);
		if (cc <= 0)
			rmtconnaborted(0);
		i += cc;
	}
	return (n);
}

rmtwrite(buf, count)
	char *buf;
	int count;
{
	char line[30];

	(void) sprintf(line, "W%d\n", count);
	(void) write(rmtape, line, strlen(line));
	(void) write(rmtape, buf, count);
	return (rmtreply("write"));
}

int
rmtseek(offset, pos)
	int offset, pos;
{
	char line[80];

	(void) sprintf(line, "L%d\n%d\n", offset, pos);
	return (rmtcall("seek", line));
}

int
rmtioctl(cmd, count)
	int cmd, count;
{
	char buf[256];
	int xcmd;

	if (count < 0)
		return (-1);

	if ((xcmd = map_extended_ioctl(cmd)) != -1)
		return (rmtioctl_extended(xcmd, count));

	(void) sprintf(buf, "I%d\n%d\n", cmd, count);
	return (rmtcall("ioctl", buf));
}

/*
 * Map from the standard Sun ioctl commands into the extended version,
 * if possible.
 */
static int
map_extended_ioctl(cmd)
	int cmd;
{
	int xcmd;

	if (rmtversion <= 0)
		return (-1);		/* extended protocol not supported */

	switch (cmd) {
	case MTRETEN:
		xcmd = 2;
		break;
	case MTERASE:
		xcmd = 3;
		break;
	case MTEOM:
		xcmd = 4;
		break;
	case MTNBSF:
		xcmd = 5;
		break;
	default:
		xcmd = -1;		/* not supported */
		break;
	}
	return (xcmd);
}

static int
rmtioctl_extended(cmd, count)
	int cmd, count;
{
	char buf[256];

	(void) sprintf(buf, "i%d\n%d\n", cmd, count);
	return (rmtcall("ioctl", buf));
}

static int
rmtcall(cmd, buf)
	char *cmd, *buf;
{
	if (write(rmtape, buf, strlen(buf)) != strlen(buf))
		rmtconnaborted(0);
	return (rmtreply(cmd));
}

static int
rmtreply(cmd)
	char *cmd;
{
	char code[30], emsg[BUFSIZ];
	extern int errno;

	rmtgets(code, sizeof (code));
	if (*code == 'E' || *code == 'F') {
		rmtgets(emsg, sizeof (emsg));
		/*
		 * don't print error message for ioctl or status;
		 * or if we are opening up a full path (i.e. device)
		 * and the tape is not loaded (EIO error)
		 */
		if (strcmp(cmd, "ioctl") != 0 &&
		    strcmp(cmd, "status") != 0 &&
		    !(cmd[0] == '/' && atoi(code + 1) == EIO))
			print("%s: %s\n", cmd, emsg);
		errno = atoi(code + 1);
		if (*code == 'F') {
			rmtstate = TS_CLOSED;
			return (-1);
		}
		return (-1);
	}
	if (*code != 'A') {
		print(dgettext(domainname,
			"Protocol to remote tape server botched (code %s?).\n"),
			code);
		rmtconnaborted(0);
	}
	return (atoi(code + 1));
}

static void
rmtgets(cp, len)
	char *cp;
	int len;
{
	register int i, n;

	n = recv(rmtape, cp, len-1, MSG_PEEK);
	for (i = 0; i < n; i++)
		if (cp[i] == '\n')
			break;
	n = i + 1;			/* characters to read at once */
	for (i = 0; i < len; i += n, n = 1) {
		n = read(rmtape, cp, n);
		if (n <= 0)
			rmtconnaborted(0);
		cp += n;
		if (cp[-1] == '\n') {
			cp[-1] = '\0';
			return;
		}
	}
	print(dgettext(domainname,
		"Protocol to remote tape server botched (in rmtgets).\n"));
	rmtconnaborted(0);
}

#ifdef __STDC__
#include <stdarg.h>

/* VARARGS1 */
static void
rmtmsg(const char *fmt, ...)
{
	va_list	args;

	va_start(args, fmt);
	(void) vfprintf(stderr, fmt, args);
	(void) fflush(stderr);
}
#else
#include <varargs.h>

/* VARARGS */
static void
rmtmsg(va_alist)
	va_dcl
{
	va_list	args;
	char	*fmt;

	va_start(args);
	fmt = va_arg(args, char *);
	(void) vfprintf(stderr, fmt, args);
	(void) fflush(stderr);
}
#endif
