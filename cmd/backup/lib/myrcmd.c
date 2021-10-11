/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)rcmd.c	1.9	92/07/14 SMI"	/* SVr4.0 1.6	*/
#ident	"@(#)myrcmd.c 1.3 94/08/03"

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
 *		All rights reserved.
 *
 */

#include <myrcmd.h>
#include <limits.h>
#include <stdio.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/file.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#ifdef USG
#define	SYSV
#endif

#ifdef SYSV
#define	bcopy(s1, s2, len)	memcpy(s2, s1, len)
#define	bzero(s, len)		memset(s, 0, len)
#define	index(s, c)		strchr(s, c)
char	*strchr();
#else
char	*index();
#endif SYSV

extern	errno;
char	*inet_ntoa();

char myrcmd_stderr[1024];

int
myrcmd(char **ahost, unsigned short rport, char *locuser, char *remuser,
    char *cmd, int *fd2p)
{
	int s, timo, retval;
	int tries = 0;
	pid_t pid;
	struct sockaddr_in sin, from;
	char c;
	int lport;
	struct hostent *hp;
#ifdef SYSV
	sigset_t oldmask;
	sigset_t newmask;
	struct sigaction oldaction;
	struct sigaction newaction;
#else
	int oldmask;
#endif SYSV
	static struct hostent numhp;
	static char numhostname[32];	/* big enough for "255.255.255.255" */
	struct in_addr numaddr;
	struct in_addr *numaddrlist[2];

	myrcmd_stderr[0] = '\0';	/* empty error string */
	pid = getpid();
	hp = gethostbyname(*ahost);
	if (hp == 0) {
		char *straddr;

		bzero((char *) numaddrlist, sizeof (numaddrlist));
		if ((numaddr.s_addr = inet_addr(*ahost)) == -1) {
			sprintf(myrcmd_stderr, "%s: unknown host\n", *ahost);
			return (MYRCMD_NOHOST);
		} else {
			bzero((char *) &numhp, sizeof (numhp));
			bzero(numhostname, sizeof (numhostname));

			if ((straddr = inet_ntoa(numaddr)) == (char *) 0) {
				sprintf(myrcmd_stderr, "%s: unknown host\n",
					*ahost);
				return (MYRCMD_NOHOST);
			}
			strncpy(straddr, numhostname, sizeof (numhostname));
			numhp.h_name = numhostname;
			numhp.h_addrtype = AF_INET;
			numhp.h_length = sizeof (numaddr);
			numaddrlist[0] = &numaddr;
			numhp.h_addr_list = (char **) numaddrlist;
			hp = &numhp;
		}
	}
	*ahost = hp->h_name;
#ifdef SYSV
	/* ignore SIGPIPE */
	bzero((char *) &newaction, sizeof (newaction));
	newaction.sa_handler = SIG_IGN;
	newaction.sa_flags = SA_ONSTACK;
	(void) sigaction(SIGPIPE, &newaction, &oldaction);

	/* block SIGURG */
	bzero((char *) &newmask, sizeof (newmask));
	(void) sigaddset(&newmask, SIGURG);
	(void) sigprocmask(SIG_BLOCK, &newmask, &oldmask);
#else
	oldmask = sigblock(sigmask(SIGURG));
#endif SYSV
again:
	timo = 1;
	lport = IPPORT_RESERVED - 1;
	for (;;) {
		s = rresvport(&lport);
		if (s < 0) {
			int err;

			if (errno == EAGAIN) {
				sprintf(myrcmd_stderr,
					"socket: All ports in use\n");
				err = MYRCMD_ENOPORT;
			} else {
				sprintf(myrcmd_stderr, "rcmd: socket: %s\n",
					strerror(errno));
				err = MYRCMD_ENOSOCK;
			}
#ifdef SYSV
			/* restore original SIGPIPE handler */
			(void) sigaction(SIGPIPE, &oldaction,
			    (struct sigaction *) 0);

			/* restore original signal mask */
			(void) sigprocmask(SIG_SETMASK, &oldmask,
			    (sigset_t *) 0);
#else
			sigsetmask(oldmask);
#endif SYSV
			return (err);
		}
		fcntl(s, F_SETOWN, pid);
		sin.sin_family = hp->h_addrtype;
		bcopy(hp->h_addr_list[0], (caddr_t)&sin.sin_addr, hp->h_length);
		sin.sin_port = rport;
		if (connect(s, (struct sockaddr *)&sin, sizeof (sin)) >= 0)
			break;
		(void) close(s);
		if (errno == EADDRINUSE) {
			lport--;
			continue;
		}
		if (errno == ECONNREFUSED && timo <= 16) {
			sleep(timo);
			timo *= 2;
			continue;
		}
		if (hp->h_addr_list[1] != NULL) {
			int oerrno = errno;

			fprintf(stderr,
			    "connect to address %s: ", inet_ntoa(sin.sin_addr));
			errno = oerrno;
			perror(0);
			hp->h_addr_list++;
			bcopy(hp->h_addr_list[0], (caddr_t)&sin.sin_addr,
			    hp->h_length);
			fprintf(stderr, "Trying %s...\n",
				inet_ntoa(sin.sin_addr));
			continue;
		}
		sprintf(myrcmd_stderr, "%s: %s\n", hp->h_name, strerror(errno));
#ifdef SYSV
		/* restore original SIGPIPE handler */
		(void) sigaction(SIGPIPE, &oldaction,
		    (struct sigaction *) 0);

		/* restore original signal mask */
		(void) sigprocmask(SIG_SETMASK, &oldmask, (sigset_t *) 0);
#else
		sigsetmask(oldmask);
#endif SYSV
		return (MYRCMD_ENOCONNECT);
	}
	lport--;
	if (fd2p == 0) {
		write(s, "", 1);
		lport = 0;
	} else {
		char num[8];
		int s2 = rresvport(&lport), s3;
		int len = sizeof (from);

		if (s2 < 0)
			goto bad;
		listen(s2, 1);
		(void) sprintf(num, "%d", lport);
		if (write(s, num, strlen(num)+1) != strlen(num)+1) {
			sprintf(myrcmd_stderr, "write: setting up stderr: %s\n",
				strerror(errno));
			(void) close(s2);
			goto bad;
		}
		s3 = accept(s2, (struct sockaddr *)&from, &len);
		if (s3 < 0) {
			sprintf(myrcmd_stderr, "accept: %s\n", strerror(errno));
			(void) close(s2);
			lport = 0;
			goto bad;
		}
		(void) close(s2);
		*fd2p = s3;
		from.sin_port = ntohs((u_short)from.sin_port);
		if (from.sin_family != AF_INET ||
		    from.sin_port >= IPPORT_RESERVED) {
			sprintf(myrcmd_stderr,
			    "socket: protocol failure in circuit setup.\n");
			goto bad2;
		}
	}
	(void) write(s, locuser, strlen(locuser)+1);
	(void) write(s, remuser, strlen(remuser)+1);
	(void) write(s, cmd, strlen(cmd)+1);
	retval = read(s, &c, 1);
	if (retval != 1) {
		if (retval == 0) {
			/*
			 * XXX - Solaris 2.0 bug alert.  Sometimes, if the
			 * tapehost is a Solaris 2.0 system, the connection
			 * will be dropped at this point.  Let's try again,
			 * three times, before we throw in the towel.
			 */
			if (++tries != 3) {
				if (lport)
					(void) close(*fd2p);
				(void) close(s);
				goto again;
			}
			sprintf(myrcmd_stderr,
			    "Protocol error, %s closed connection\n", *ahost);
		} else if (retval < 0) {
			sprintf(myrcmd_stderr, "%s: %s\n", *ahost,
			    strerror(errno));
		} else {
			sprintf(myrcmd_stderr,
			    "Protocol error, %s sent %d bytes\n",
			    *ahost, retval);
		}
		goto bad2;
	}
	if (c != 0) {
		char *cp = myrcmd_stderr;

		while (read(s, &c, 1) == 1) {
			*cp++ = c;
			if (c == '\n')
				break;
		}
		*cp = '\0';
		goto bad2;
	}
#ifdef SYSV
	/* restore original SIGPIPE handler */
	(void) sigaction(SIGPIPE, &oldaction, (struct sigaction *) 0);

	/* restore original signal mask */
	(void) sigprocmask(SIG_SETMASK, &oldmask, (sigset_t *) 0);
#else
	sigsetmask(oldmask);
#endif SYSV
	return (s);
bad2:
	if (lport)
		(void) close(*fd2p);
bad:
	(void) close(s);
#ifdef SYSV
	/* restore original SIGPIPE handler */
	(void) sigaction(SIGPIPE, &oldaction, (struct sigaction *) 0);

	/* restore original signal mask */
	(void) sigprocmask(SIG_SETMASK, &oldmask, (sigset_t *) 0);
#else
	sigsetmask(oldmask);
#endif SYSV
	return (MYRCMD_EBAD);
}
