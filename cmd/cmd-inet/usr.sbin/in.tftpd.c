/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)in.tftpd.c	1.13	95/07/28 SMI"	/* SVr4.0 1.9   */

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 * 
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 * 
 * 
 * 
 * 		Copyright Notice 
 * 
 * Notice of copyright on this source code product does not indicate 
 * publication.
 * 
 * 	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 * 	          All rights reserved.
 *  
 */

/*
 * Trivial file transfer protocol server.  A top level process runs in
 * an infinite loop fielding new TFTP requests.  A child process,
 * communicating via a pipe with the top level process, sends delayed
 * NAKs for those that we can't handle.  A new child process is created
 * to service each request that we can handle.  The top level process
 * exits after a period of time during which no new requests are
 * received.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <arpa/tftp.h>

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <netdb.h>
#include <setjmp.h>
#include <syslog.h>
#include <sys/param.h>
#include <fcntl.h>
#include <pwd.h>
#include <string.h>

#ifdef SYSV
#define	bzero(s,n)	memset((s), 0, (n))
#define signal(s,f)	sigset(s,f)
#define	setjmp(e)	sigsetjmp(e, 1)
#define	longjmp(e, v)	siglongjmp(e, v)
#define	jmp_buf		sigjmp_buf
#endif /* SYSV */

#ifndef UID_NOBODY
#ifdef SYSV
#define	UID_NOBODY	60001
#define	GID_NOBODY	60001
#else
#define UID_NOBODY	65534
#define GID_NOBODY	65534
#endif /* SYSV */
#endif /* UID_NOBODY */

#define	TIMEOUT		5
#define	PKTSIZE		SEGSIZE+4
#define	DELAY_SECS	3
#define DALLYSECS 60

extern	int optind, getopt();
extern	char *optarg;
extern	int errno;

struct	sockaddr_in sin = { AF_INET };
int	peer;
int	rexmtval = TIMEOUT;
int	maxtimeout = 5*TIMEOUT;
char	buf[PKTSIZE];
char	ackbuf[PKTSIZE];
struct	sockaddr_in from;
int	fromlen;
pid_t	child;			/* pid of child handling delayed replys */
int	delay_fd [2];		/* pipe for communicating with child */
FILE	*file;
struct	delay_info {
	long	timestamp;		/* time request received */
	int	ecode;			/* error code to return */
	struct	sockaddr_in from;	/* address of client */
};

int	initted = 0;
int	securetftp = 0;
int	debug = 0;
int	disable_pnp = 0;
char	*filename;
uid_t	uid_nobody = UID_NOBODY;
uid_t	gid_nobody = GID_NOBODY;

/*
 * Default directory for unqualified names
 * Used by TFTP boot procedures
 */
char	*homedir = "/tftpboot";

#ifndef SYSV
void
childcleanup ()
{
	wait3 ((union wait *) 0, WNOHANG, (struct rusage *) 0);
	(void) signal (SIGCHLD, (void (*)())childcleanup);
}
#endif /* SYSV */

main (argc, argv)
	int argc;
	char **argv;
{
	register struct tftphdr *tp;
	register int n;
	int on = 1;
	int c;
	struct	passwd *pwd;		/* for "nobody" entry */

	openlog("tftpd", LOG_PID, LOG_DAEMON);

	while ((c = getopt (argc, argv, "dsp")) != EOF)
		switch (c) {
		case 'd':		/* enable debug */
			debug++;
			continue;
		case 's':		/* secure daemon */
			securetftp = 1;
			continue;
		case 'p':		/* disable name pnp mapping */
			disable_pnp = 1;
			continue;
		case '?':
		default:
usage:
			fprintf (stderr, 
				 "usage:  %s [-spd] [home-directory]\n",
				 argv[0]);
			for ( ; optind < argc; optind++)
				syslog (LOG_ERR, "bad argument %s", 
					argv [optind]);
			exit (1);
		}

	if (optind < argc)
		if (optind == argc - 1 && *argv [optind] == '/')
			homedir = argv [optind];
		else
			goto usage;
	
	if (pipe (delay_fd) < 0) {
		syslog (LOG_ERR, "pipe (main): %m");
		exit (1);
	}

#ifdef SYSV
	(void) signal (SIGCHLD, SIG_IGN); /*no zombies please*/
#else
	(void) signal (SIGCHLD, (void (*)())childcleanup);
#endif /* SYSV */

	pwd = getpwnam("nobody");
	if (pwd != (struct passwd *) NULL) {
		uid_nobody = pwd->pw_uid;
		gid_nobody = pwd->pw_gid;
	}

	(void) chdir(homedir);

	if ((child = fork ()) < 0) {
		syslog (LOG_ERR, "fork (main): %m");
		exit (1);
	}

	if (child == 0) {
		delayed_responder();
	} /* child */

	/* close read side of pipe */
	(void) close (delay_fd[0]);


	/*
	 * Top level handling of incomming tftp requests.  Read a request
	 * and pass it off to be handled.  If request is valid, handling
	 * forks off and parent returns to this loop.  If no new requests
	 * are received for DALLYSECS, exit and return to inetd.
	 */

	for (;;) {
		fd_set readfds;
		struct timeval dally;

		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
		dally.tv_sec = DALLYSECS;
		dally.tv_usec = 0;

		n = select (1, &readfds, NULL, 
			    NULL, &dally);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			syslog (LOG_ERR, "select: %m");
		  	(void) kill (child, SIGKILL);
			exit (1);
		}
		if (n == 0) {
			/* Select timed out.  Its time to die. */
		  	(void) kill (child, SIGKILL);
			exit (0);
		}

		fromlen = sizeof (from);
		n = recvfrom(0, buf, sizeof (buf), 0,
			     (struct sockaddr *) &from, &fromlen);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			syslog(LOG_ERR, "recvfrom: %m");
			(void) kill (child, SIGKILL);
			exit(1);
		}

		(void) alarm(0);

		peer = socket(AF_INET, SOCK_DGRAM, 0);
		if (peer < 0) {
			syslog(LOG_ERR, "socket (main): %m");
			(void) kill (child, SIGKILL);
			exit(1);
		}

		bzero ((char *) &sin, sizeof (sin));
		sin.sin_family = AF_INET;
		if (bind(peer, (struct sockaddr *)&sin, sizeof (sin)) < 0) {
			syslog(LOG_ERR, "bind (main): %m");
			(void) kill (child, SIGKILL);
			exit(1);
		}

		from.sin_family = AF_INET;
		if (connect(peer, (struct sockaddr *)&from, sizeof(from)) 
		    < 0) {
			syslog(LOG_ERR, "connect (main): %m");
			(void) kill (child, SIGKILL);
			exit(1);
		}

		tp = (struct tftphdr *)buf;
		tp->th_opcode = ntohs((u_short) tp->th_opcode);
		if (tp->th_opcode == RRQ || tp->th_opcode == WRQ)
			tftp(tp, n);

		(void) close (peer);
		(void) fclose (file);
	}
}

delayed_responder()
{
	struct delay_info dinfo;
	long now;
	
	/* we don't use the descriptors passed in to the parent */
	(void) close (0);
	(void) close (1);

	if (setgid(gid_nobody) < 0) {
		syslog (LOG_ERR, "setgid(%d): %m", gid_nobody);
		exit(1);
	}
	if (setuid(uid_nobody) < 0) {
		syslog (LOG_ERR, "setuid(%d): %m", uid_nobody);
		exit(1);
	}

	/* close write side of pipe */
	(void) close (delay_fd[1]);
	
	for (;;) {
		if (read (delay_fd [0], (char *) &dinfo, 
			  sizeof (dinfo)) != sizeof (dinfo)) {
			if (errno == EINTR)
				continue;
			syslog (LOG_ERR, "read from pipe: %m");
			exit (1);
		}
		
		peer = socket(AF_INET, SOCK_DGRAM, 0);
		if (peer < 0) {
			syslog(LOG_ERR, "socket (delay): %m");
			exit(1);
		}
		
		bzero ((char *) &sin, sizeof (sin));
		sin.sin_family = AF_INET;
		if (bind(peer, (struct sockaddr *) &sin, 
			 sizeof (sin)) < 0) {
			syslog(LOG_ERR, "bind (delay): %m");
			exit(1);
		}
		
		dinfo.from.sin_family = AF_INET;
		if (connect(peer, (struct sockaddr *) &dinfo.from,
			    sizeof(dinfo.from)) < 0) {
			syslog(LOG_ERR, "connect (delay): %m");
			exit(1);
			break;
		}
		
		/*
		 * only sleep if DELAY_SECS has not elapsed since 
		 * original request was received.  Ensure that `now' 
		 * is not earlier than `dinfo.timestamp'
		 */
		now = time(0);
		if ((u_int)(now - dinfo.timestamp) < DELAY_SECS)
			sleep (DELAY_SECS - (now - dinfo.timestamp));
		nak (dinfo.ecode);
		(void) close (peer);
	} /* for */
	/*NOTREACHED*/
}

int	validate_access();
int	sendfile(), recvfile();

struct formats {
	char	*f_mode;
	int	(*f_validate)();
	int	(*f_send)();
	int	(*f_recv)();
	int	f_convert;
} formats[] = {
	{ "netascii",	validate_access,	sendfile,	recvfile, 1 },
	{ "octet",	validate_access,	sendfile,	recvfile, 0 },
#ifdef notdef
	{ "mail",	validate_user,		sendmail,	recvmail, 1 },
#endif
	{ 0 }
};


/*
 * Handle initial connection protocol.
 */
tftp(tp, size)
	struct tftphdr *tp;
	int size;
{
	register char *cp;
	int first = 1, ecode;
	register struct formats *pf;
	char *mode;
	pid_t pid;
	struct delay_info dinfo;
	int fd;
	static int firsttime = 1;

	filename = cp = (char *) &tp->th_stuff;
again:
	while (cp < buf + size) {
		if (*cp == '\0')
			break;
		cp++;
	}
	if (*cp != '\0') {
		nak(EBADOP);
		exit(1);
	}
	if (first) {
		mode = ++cp;
		first = 0;
		goto again;
	}
	for (cp = mode; *cp; cp++)
		if (isupper(*cp))
			*cp = tolower(*cp);
	for (pf = formats; pf->f_mode; pf++)
		if (strcmp(pf->f_mode, mode) == 0)
			break;
	if (pf->f_mode == 0) {
		nak(EBADOP);
		exit(1);
	}
		
	/* 
	 * XXX fork a new process to handle this request before
	 * chroot(), otherwise the parent won't be able to create a
	 * new socket as that requires library access to system files
	 * and devices.
	 */
	pid = fork();
	if (pid < 0) {
		syslog(LOG_ERR, "fork (tftp): %m");
		return;
	}

	if (pid)
		return;

	/*
	 * Try to see if we can access the file.  The access can still
	 * fail later if we are running in secure mode because of
	 * the chroot() call.  We only want to execute the chroot()  once.
	 */
	if (securetftp && firsttime) {
		firsttime = 0;
		if (chroot(homedir) == -1) {
			syslog(LOG_ERR, 
			       "tftpd: cannot chroot to directory %s: %m\n",
			       homedir);
			return (EACCESS);
		}
		(void) chdir("/");  /* cd to  new root */
	}

	/*
	 * Temporarily set uid/gid to someone who is only
	 * allowed "public" access to the file.
	 */
	if (setegid(gid_nobody) < 0) {
		syslog (LOG_ERR, "setgid(%d): %m", gid_nobody);
		exit(1);
	}
	if (seteuid(uid_nobody) < 0) {
		syslog (LOG_ERR, "setuid(%d): %m", uid_nobody);
		exit(1);
	}
	ecode = (*pf->f_validate)(tp->th_opcode);
	/*
	 * Go back to root access so that the chroot() and the
	 * main loop still work!  Perhaps we should always run as
	 * nobody after doing one chroot()?
	 */
	(void) setegid(0);
	(void) seteuid(0);

	if (ecode) {
		/*
                 * The most likely cause of an error here is that
                 * someone has broadcast an RRQ packet because s/he's
                 * trying to boot and doesn't know who the server is.
                 * Rather then sending an ERROR packet immediately, we
                 * wait a while so that the real server has a better chance
                 * of getting through (in case client has lousy Ethernet
                 * interface).  We write to a child that handles delayed
		 * ERROR packets to avoid delaying service to new
		 * requests.  Of course, we would rather just not answer
		 * RRQ packets that are broadcast, but there's no way
		 * for a user process to determine this.
                 */
		dinfo.timestamp = time(0);

		/*
		 * If running in secure mode, we map all errors to EACCESS
		 * so that the client gets no information about which files
		 * or directories exist.
		 */
		if (securetftp)
			dinfo.ecode = EACCESS;
		else
			dinfo.ecode = ecode;

		dinfo.from = from;
		if (write (delay_fd [1], (char *) &dinfo, sizeof (dinfo)) !=
		    sizeof (dinfo)) {
			syslog (LOG_ERR, "delayed write failed.");
			(void) kill (child, SIGKILL);
			exit (1);
		}
		exit (0);
	}

	/* we don't use the descriptors passed in to the parent */
	(void) close (0);
	(void) close (1);
	
	/*
	 * Need to do all file access as someone who is only
	 * allowed "public" access to the file.
	 */
	if (setgid(gid_nobody) < 0) {
		syslog (LOG_ERR, "setgid(%d): %m", gid_nobody);
		exit(1);
	}
	if (setuid(uid_nobody) < 0) {
		syslog (LOG_ERR, "setuid(%d): %m", uid_nobody);
		exit(1);
	}
	
	/* 
	 * try to open file only after setuid/setgid.  Note that
	 * a chroot() has already been done.
	 */
	fd = open(filename, 
		  tp->th_opcode == RRQ ? O_RDONLY : (O_WRONLY|O_TRUNC));
	if (fd < 0) {
		nak(errno + 100);
		exit(1);
	}
	file = fdopen(fd, (tp->th_opcode == RRQ)? "r":"w");
	if (file == NULL) {
		nak(errno + 100);
		exit(1);
	}
	
	if (tp->th_opcode == WRQ)
		(*pf->f_recv)(pf);
	else
		(*pf->f_send)(pf);
	
	exit (0);
}

/*
 *	Maybe map filename into another one.
 *
 *	For PNP, we get TFTP boot requests for filenames like 
 *	<Unknown Hex IP Addr>.<Architecture Name>.   We must
 *	map these to 'pnp.<Architecture Name>'.  Note that
 *	uppercase is mapped to lowercase in the architecture names.
 *
 *	For names <Hex IP Addr> there are two cases.  First,
 *	it may be a buggy prom that omits the architecture code.
 *	So first check if <Hex IP Addr>.<arch> is on the filesystem.
 *	Second, this is how most Sun3s work; assume <arch> is sun3.
 */

char *
pnp_check (origname)
	char *origname;
{
	static char buf [MAXNAMLEN + 1];
	char *arch, *s;
	long ipaddr;
	int len = (origname ? strlen (origname) : 0);
	struct hostent *hp;
	DIR *dir;
	struct dirent *dp;
	struct stat statb;

	if (securetftp || disable_pnp || len < 8 || len > 14)
		return (char *) NULL;

	/* XXX see if this cable allows pnp; if not, return NULL
	 * Requires YP support for determining this!
	 */
	
	ipaddr = htonl (strtol (origname, &arch, 16));
	if (!arch || (len > 8 && *arch != '.'))
		return (char *) NULL;
	if (len == 8)
		arch = "SUN3";
	else
		arch++;

	/* allow <Hex IP Addr>* filename request to to be
	 * satisfied by <Hex IP Addr><Any Suffix> rather
	 * than enforcing this to be Sun3 systems.  Also serves
	 * to make case of suffix a don't-care.
	 */
	if ((dir = opendir ("/tftpboot")) == (DIR *) NULL)
		return (char *) NULL;
	while ((dp = readdir (dir)) != (struct dirent *)NULL) {
		if (strncmp (origname, dp->d_name, 8) == 0) {
			strcpy (buf, dp->d_name);
			closedir (dir);
			return buf;
		}
	}
	closedir (dir);

	/* XXX maybe call YP master for most current data iff
	 * pnp is enabled.
	 */

	hp = gethostbyaddr ((char *)&ipaddr, sizeof (ipaddr), AF_INET);

	/*
	 * only do mapping PNP boot file name for machines that 
	 * are not in the hosts database.
	 */
	if (!hp) {
		strcpy (buf, "pnp.");
		for (s = &buf [4]; *arch; )
			if (isupper (*arch))
				*s++ = tolower (*arch++);
			else
				*s++ = *arch++;
		return buf;
	} else {
		return (char *) NULL;
	}
}


/*
 * Try to validate file access. File must file to exist and be publicly
 * readable/writable.
 */
validate_access(mode)
	int mode;
{
	struct stat stbuf;
	int	fd;
	char *origfile;

	if (stat(filename, &stbuf) < 0) {
		if (errno != ENOENT)
			return EACCESS;
		if (mode != RRQ)
			return(ENOTFOUND);

		/* try to map requested filename into a pnp filename */
		origfile = filename;
		filename = pnp_check (origfile);
		if (filename == (char *) NULL)
			return(ENOTFOUND);
		
		if (stat (filename, &stbuf) < 0)
			return (errno == ENOENT ? ENOTFOUND : EACCESS);
		syslog (LOG_NOTICE, "%s -> %s\n", origfile, filename);
	}
	
	if (mode == RRQ) {
		if ((stbuf.st_mode&S_IROTH) == 0)
			return (EACCESS);
	} else {
		if ((stbuf.st_mode&S_IWOTH) == 0)
			return (EACCESS);
	}
	if ((stbuf.st_mode & S_IFMT) != S_IFREG)
                return (EACCESS);
	return (0);
}

int	timeout;
jmp_buf	timeoutbuf;

timer()
{
	
	timeout += rexmtval;
	if (timeout >= maxtimeout)
		exit(1);
	longjmp(timeoutbuf, 1);
}

/*
 * Send the requested file.
 */
sendfile(pf)
	struct formats *pf;
{
	struct tftphdr *dp, *r_init();
	struct tftphdr *ap;    /* ack packet */
	int block = 1, size, n;
	
	signal(SIGALRM, (void (*)())timer);
	dp = r_init();
	ap = (struct tftphdr *)ackbuf;
	do {
		size = readit(file, &dp, pf->f_convert);
		if (size < 0) {
			nak(errno + 100);
			goto abort;
		}
		dp->th_opcode = htons((u_short)DATA);
		dp->th_block = htons((u_short)block);
		timeout = 0;
		(void) setjmp(timeoutbuf);
		
send_data:
		if (send(peer, (char *)dp, size + 4, 0) != size + 4) {
			if ((errno == ENETUNREACH) ||
			    (errno == EHOSTUNREACH) ||
			    (errno == ECONNREFUSED))
				syslog(LOG_WARNING, "send (data): %m");
			else
				syslog(LOG_ERR, "send (data): %m");
			goto abort;
		}
		read_ahead(file, pf->f_convert);
		for ( ; ; ) {
			alarm(rexmtval);        /* read the ack */
			n = recv(peer, ackbuf, sizeof (ackbuf), 0);
			alarm(0);
			if (n < 0) {
				if (errno == EINTR)
					continue;
				if ((errno == ENETUNREACH) ||
				    (errno == EHOSTUNREACH) ||
				    (errno == ECONNREFUSED))
					syslog(LOG_WARNING, "recv (ack): %m");
				else
					syslog(LOG_ERR, "recv (ack): %m");
				goto abort;
			}
			ap->th_opcode = ntohs((u_short)ap->th_opcode);
			ap->th_block = ntohs((u_short)ap->th_block);
			
			if (ap->th_opcode == ERROR)
				goto abort;
			
			if (ap->th_opcode == ACK) {
				if (ap->th_block == block) {
					break;
				}
				/* Re-synchronize with the other side */
				(void) synchnet(peer);
				if (ap->th_block == (block -1)) {
					goto send_data;
				}
			}
			
		}
		block++;
	} while (size == SEGSIZE);
abort:
	(void) fclose(file);
}

justquit()
{
	exit(0);
}


/*
 * Receive a file.
 */
recvfile(pf)
	struct formats *pf;
{
	struct tftphdr *dp, *w_init();
	struct tftphdr *ap;    /* ack buffer */
	int block = 0, n, size;

	signal(SIGALRM, (void (*)())timer);
	dp = w_init();
	ap = (struct tftphdr *)ackbuf;
	do {
		timeout = 0;
		ap->th_opcode = htons((u_short)ACK);
		ap->th_block = htons((u_short)block);
		block++;
		(void) setjmp(timeoutbuf);
send_ack:
		if (send(peer, ackbuf, 4, 0) != 4) {
			syslog(LOG_ERR, "send (ack): %m\n");
			goto abort;
		}
		write_behind(file, pf->f_convert);
		for ( ; ; ) {
			alarm(rexmtval);
			n = recv(peer, (char *)dp, PKTSIZE, 0);
			alarm(0);
			if (n < 0) {            /*really? */
				if (errno == EINTR)
					continue;
				syslog(LOG_ERR, "recv (data): %m");
				goto abort;
			}
			dp->th_opcode = ntohs((u_short)dp->th_opcode);
			dp->th_block = ntohs((u_short)dp->th_block);
			if (dp->th_opcode == ERROR)
				goto abort;
			if (dp->th_opcode == DATA) {
				if (dp->th_block == block) {
					break;   /* normal */
				}
				/* Re-synchronize with the other side */
				(void) synchnet(peer);
				if (dp->th_block == (block-1))
					goto send_ack;          /* rexmit */
			}
		}
		/*  size = write(file, dp->th_data, n - 4); */
		size = writeit(file, &dp, n - 4, pf->f_convert);
		if (size != (n-4)) {                    /* ahem */
			if (size < 0) nak(errno + 100);
			else nak(ENOSPACE);
			goto abort;
		}
	} while (size == SEGSIZE);
	write_behind(file, pf->f_convert);
	(void) fclose(file);            /* close data file */

	ap->th_opcode = htons((u_short)ACK);    /* send the "final" ack */
	ap->th_block = htons((u_short)(block));
	(void) send(peer, ackbuf, 4, 0);

	signal(SIGALRM, (void (*)())justquit);      /* just quit on timeout */
	alarm(rexmtval);
	n = recv(peer, buf, sizeof (buf), 0); /* normally times out and quits */
	alarm(0);
	if (n >= 4 &&                   /* if read some data */
	    dp->th_opcode == DATA &&    /* and got a data block */
	    block == dp->th_block) {	/* then my last ack was lost */
		(void) send(peer, ackbuf, 4, 0);     /* resend final ack */
	}
 abort:
	return;
}

struct errmsg {
	int	e_code;
	char	*e_msg;
} errmsgs[] = {
	{ EUNDEF,	"Undefined error code" },
	{ ENOTFOUND,	"File not found" },
	{ EACCESS,	"Access violation" },
	{ ENOSPACE,	"Disk full or allocation exceeded" },
	{ EBADOP,	"Illegal TFTP operation" },
	{ EBADID,	"Unknown transfer ID" },
	{ EEXISTS,	"File already exists" },
	{ ENOUSER,	"No such user" },
	{ -1,		0 }
};

/*
 * Send a nak packet (error message).
 * Error code passed in is one of the
 * standard TFTP codes, or a UNIX errno
 * offset by 100.
 */
nak(error)
	int error;
{
	register struct tftphdr *tp;
	int length;
	register struct errmsg *pe;

	tp = (struct tftphdr *)buf;
	tp->th_opcode = htons((u_short)ERROR);
	tp->th_code = htons((u_short)error);
	for (pe = errmsgs; pe->e_code >= 0; pe++)
		if (pe->e_code == error)
			break;
	if (pe->e_code < 0) {
		pe->e_msg = strerror(error - 100);
		tp->th_code = EUNDEF;   /* set 'undef' errorcode */
	}
	strcpy(tp->th_msg, pe->e_msg);
	length = strlen(pe->e_msg);
	tp->th_msg[length] = '\0';
	length += 5;
	if (send(peer, buf, length, 0) != length)
		syslog(LOG_ERR, "tftpd: nak: %m\n");
}
