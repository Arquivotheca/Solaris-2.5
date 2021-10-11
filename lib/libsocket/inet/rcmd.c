/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)rcmd.c	1.23	94/06/03 SMI"	/* SVr4.0 1.6	*/

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
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <grp.h>
#include <arpa/inet.h>

#ifdef SYSV
#define	bcopy(s1, s2, len)	(void) memcpy(s2, s1, len)
#define	bzero(s, len)		(void) memset(s, 0, len)
#define	index(s, c)		strchr(s, c)
char	*strchr();
#else
char	*index();
#endif SYSV

static int _validuser(FILE *hostf, char *rhost, const char *luser,
			const char *ruser, int baselen);
static int _checkhost(char *rhost, char *lhost, int len);


#ifdef NIS
static char *domain;
#endif

int rcmd(char **ahost, unsigned short rport, const char *locuser,
    const char *remuser, const char *cmd, int *fd2p)
{
	int s, timo = 1, retval;
	pid_t pid;
	struct sockaddr_in sin, from;
	char c;
	int lport = IPPORT_RESERVED - 1;
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
	fd_set fdset;
	int selret;


	pid = getpid();
	hp = gethostbyname(*ahost);
	if (hp == 0) {
		char *straddr;

		bzero ((char *) numaddrlist, sizeof (numaddrlist));
		if ((numaddr.s_addr = inet_addr(*ahost)) == -1) {
			(void) fprintf(stderr, "%s: unknown host\n", *ahost);
			return (-1);
		} else {
			bzero((char *) &numhp, sizeof (numhp));
			bzero(numhostname, sizeof (numhostname));

			if ((straddr = inet_ntoa(numaddr)) == (char *) 0) {
				(void) fprintf(stderr, "%s: unknown host\n",
						*ahost);
				return (-1);
			}
			(void) strncpy (straddr, numhostname,
					sizeof (numhostname));
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
	(void) __sigaction (SIGPIPE, &newaction, &oldaction);

	/* block SIGURG */
	bzero((char *) &newmask, sizeof (newmask));
	(void) _sigaddset(&newmask, SIGURG);
	(void) _sigprocmask(SIG_BLOCK, &newmask, &oldmask);
#else
	oldmask = _sigblock(sigmask(SIGURG));
#endif SYSV
	for (;;) {
		s = rresvport(&lport);
		if (s < 0) {
			if (errno == EAGAIN)
				(void) fprintf(stderr,
						"socket: All ports in use\n");
			else
				perror("rcmd: socket");
#ifdef SYSV
			/* restore original SIGPIPE handler */
			(void) __sigaction(SIGPIPE, &oldaction,
					(struct sigaction *) 0);

			/* restore original signal mask */
			(void) _sigprocmask(SIG_SETMASK, &oldmask,
					(sigset_t *) 0);
#else
			sigsetmask(oldmask);
#endif SYSV
			return (-1);
		}
		(void) fcntl(s, F_SETOWN, pid);
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
			(void) sleep(timo);
			timo *= 2;
			continue;
		}
		if (hp->h_addr_list[1] != NULL) {
			int oerrno = errno;

			(void) fprintf(stderr,
				"connect to address %s: ",
				inet_ntoa(sin.sin_addr));
			errno = oerrno;
			perror(0);
			hp->h_addr_list++;
			bcopy(hp->h_addr_list[0], (caddr_t)&sin.sin_addr,
			    hp->h_length);
			(void) fprintf(stderr, "Trying %s...\n",
				inet_ntoa(sin.sin_addr));
			continue;
		}
		perror(hp->h_name);
#ifdef SYSV
		/* restore original SIGPIPE handler */
		(void) __sigaction(SIGPIPE, &oldaction,
				(struct sigaction *) 0);

		/* restore original signal mask */
		(void) _sigprocmask(SIG_SETMASK, &oldmask, (sigset_t *) 0);
#else
		sigsetmask(oldmask);
#endif SYSV
		return (-1);
	}
	lport--;
	if (fd2p == 0) {
		(void) write(s, "", 1);
		lport = 0;
	} else {
		char num[8];
		int s2 = rresvport(&lport), s3;
		int len = sizeof (from);

		if (s2 < 0)
			goto bad;
		(void) listen(s2, 1);
		(void) sprintf(num, "%d", lport);
		if (write(s, num, strlen(num)+1) != strlen(num)+1) {
			perror("write: setting up stderr");
			(void) close(s2);
			goto bad;
		}
		FD_ZERO(&fdset);
		FD_SET(s, &fdset);
		FD_SET(s2, &fdset);
		while ((selret = select(FD_SETSIZE, &fdset, (fd_set *) 0,
			(fd_set *) 0, (struct timeval *) 0)) > 0) {
			if (FD_ISSET(s, &fdset)) {
				/*
				 *	Something's wrong:  we should get no
				 *	data on this connection at this point,
				 *	so we assume that the connection has
				 *	gone away.
				 */
				(void) close(s2);
				goto bad;
			}
			if (FD_ISSET(s2, &fdset)) {
				/*
				 *	We assume this is an incoming connect
				 *	request and proceed normally.
				 */
				s3 = accept(s2, (struct sockaddr *)&from, &len);
				FD_CLR(s2, &fdset);
				(void) close(s2);
				if (s3 < 0) {
					perror("accept");
					lport = 0;
					goto bad;
				}
				else
					break;
			}
		}
		if (selret == -1) {
			/*
			 *	This should not happen, and we treat it as
			 *	a fatal error.
			 */
			(void) close(s2);
			goto bad;
		}

		*fd2p = s3;
		from.sin_port = ntohs((u_short)from.sin_port);
		if (from.sin_family != AF_INET ||
		    from.sin_port >= IPPORT_RESERVED) {
			(void) fprintf(stderr,
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
			(void) fprintf(stderr,
				"Protocol error, %s closed connection\n",
				*ahost);
		} else if (retval < 0) {
			perror(*ahost);
		} else {
			(void) fprintf(stderr,
				"Protocol error, %s sent %d bytes\n",
				*ahost, retval);
		}
		goto bad2;
	}
	if (c != 0) {
		while (read(s, &c, 1) == 1) {
			(void) write(2, &c, 1);
			if (c == '\n')
				break;
		}
		goto bad2;
	}
#ifdef SYSV
	/* restore original SIGPIPE handler */
	(void) __sigaction(SIGPIPE, &oldaction, (struct sigaction *) 0);

	/* restore original signal mask */
	(void) _sigprocmask(SIG_SETMASK, &oldmask, (sigset_t *) 0);
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
	(void) __sigaction(SIGPIPE, &oldaction, (struct sigaction *) 0);

	/* restore original signal mask */
	(void) _sigprocmask(SIG_SETMASK, &oldmask, (sigset_t *) 0);
#else
	sigsetmask(oldmask);
#endif SYSV
	return (-1);
}

rresvport(alport)
	int *alport;
{
	struct sockaddr_in sin;
	int s;

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return (-1);
	for (;;) {
		sin.sin_port = htons((u_short)*alport);
		if (bind(s, (struct sockaddr *)&sin, sizeof (sin)) >= 0)
			return (s);
		if (errno != EADDRINUSE) {
			(void) close(s);
			return (-1);
		}
		(*alport)--;
		if (*alport == IPPORT_RESERVED/2) {
			(void) close(s);
			errno = EAGAIN;		/* close */
			return (-1);
		}
	}
}

ruserok(rhost, superuser, ruser, luser)
	const char *rhost;
	int superuser;
	const char *ruser, *luser;
{
	FILE *hostf;
	char fhost[MAXHOSTNAMELEN];
	register const char *sp;
	register char *p;
	int baselen = -1;

	struct stat sbuf;
	struct passwd *pwd;
	char pbuf[MAXPATHLEN];
	uid_t uid = (uid_t)-1;
	gid_t gid = (gid_t)-1;
	gid_t grouplist[NGROUPS_MAX];
	int ngroups;

	sp = rhost;
	p = fhost;
	while (*sp) {
		if (*sp == '.') {
			if (baselen == -1)
				baselen = sp - rhost;
			*p++ = *sp++;
		} else {
			*p++ = isupper(*sp) ? tolower(*sp++) : *sp++;
		}
	}
	*p = '\0';

	/* check /etc/hosts.equiv */
	if (!superuser) {
		if ((hostf = fopen("/etc/hosts.equiv", "r")) != NULL) {
			if (!_validuser(hostf, fhost, luser, ruser, baselen)) {
				(void) fclose(hostf);
				return (0);
			}
			(void) fclose(hostf);
		}
	}

	/* check ~/.rhosts */

	if ((pwd = getpwnam(luser)) == NULL)
		return (-1);
	(void) strcpy(pbuf, pwd->pw_dir);
	(void) strcat(pbuf, "/.rhosts");

	/*
	 * Read .rhosts as the local user to avoid NFS mapping the root uid
	 * to something that can't read .rhosts.
	 */
	gid = getegid();
	uid = geteuid();
	if ((ngroups = getgroups(NGROUPS_MAX, grouplist)) == -1)
		return (-1);

	(void) setegid (pwd->pw_gid);
	initgroups(pwd->pw_name, pwd->pw_gid);
	(void) seteuid (pwd->pw_uid);
	if ((hostf = fopen(pbuf, "r")) == NULL) {
		if (gid != (gid_t)-1)
			(void) setegid (gid);
		if (uid != (uid_t)-1)
			(void) seteuid (uid);
		setgroups(ngroups, grouplist);
		return (-1);
	}
	(void) fstat(fileno(hostf), &sbuf);
	if (sbuf.st_uid && sbuf.st_uid != pwd->pw_uid) {
		(void) fclose(hostf);
		if (gid != (gid_t)-1)
			(void) setegid (gid);
		if (uid != (uid_t)-1)
			(void) seteuid (uid);
		setgroups(ngroups, grouplist);
		return (-1);
	}

	if (!_validuser(hostf, fhost, luser, ruser, baselen)) {
		(void) fclose(hostf);
		if (gid != (gid_t)-1)
			(void) setegid (gid);
		if (uid != (uid_t)-1)
			(void) seteuid (uid);
		setgroups(ngroups, grouplist);
		return (0);
	}

	(void) fclose(hostf);
	if (gid != (gid_t)-1)
		(void) setegid (gid);
	if (uid != (uid_t)-1)
		(void) seteuid (uid);
	setgroups(ngroups, grouplist);
	return (-1);
}

static int
_validuser(hostf, rhost, luser, ruser, baselen)
char *rhost;
const char *luser, *ruser;
FILE *hostf;
int baselen;
{
	char *user;
	char ahost[BUFSIZ];
	int hostmatch, usermatch;
	register char *p;

#ifdef NIS
	if (domain == NULL) {
		(void) usingypmap(&domain, NULL);
	}
#endif NIS

	while (fgets(ahost, sizeof (ahost), hostf)) {
		hostmatch = usermatch = 0;
		p = ahost;
		/*
		 * We can get a line bigger than our buffer.  If so we skip
		 * the offending line.
		 */
		if (strchr(p, '\n') == NULL) {
			while (fgets(ahost, sizeof (ahost), hostf)
			    && strchr(ahost, '\n') == NULL)
				;
			continue;
		}
		while (*p != '\n' && *p != ' ' && *p != '\t' && *p != '\0') {
			*p = isupper(*p) ? tolower(*p) : *p;
			p++;
		}
		if (*p == ' ' || *p == '\t') {
			*p++ = '\0';
			while (*p == ' ' || *p == '\t')
				p++;
			user = p;
			while (*p != '\n' && *p != ' ' && *p != '\t' &&
				*p != '\0')
				p++;
		} else
			user = p;
		*p = '\0';
		if (ahost[0] == '+' && ahost[1] == 0)
			hostmatch = 1;
#ifdef NIS
		else if (ahost[0] == '+' && ahost[1] == '@')
			hostmatch = innetgr(ahost + 2, rhost,
					    NULL, domain);
		else if (ahost[0] == '-' && ahost[1] == '@') {
			if (innetgr(ahost + 2, rhost, NULL, domain))
				break;
		}
#endif NIS
		else if (ahost[0] == '-') {
			if (_checkhost(rhost, ahost+1, baselen))
				break;
		}
		else
			hostmatch = _checkhost(rhost, ahost, baselen);
		if (user[0]) {
			if (user[0] == '+' && user[1] == 0)
				usermatch = 1;
#ifdef NIS
			else if (user[0] == '+' && user[1] == '@')
				usermatch = innetgr(user+2, NULL,
						    ruser, domain);
			else if (user[0] == '-' && user[1] == '@') {
				if (hostmatch &&
				    innetgr(user+2, NULL, ruser, domain))
					break;
			}
#endif NIS
			else if (user[0] == '-') {
				if (hostmatch && !strcmp(user+1, ruser))
					break;
			}
			else
				usermatch = !strcmp(user, ruser);
		}
		else
			usermatch = !strcmp(ruser, luser);
		if (hostmatch && usermatch)
			return (0);
	}
	return (-1);
}

static int
_checkhost(rhost, lhost, len)
char *rhost, *lhost;
int len;
{
	static char *ldomain;
	static char *domainp;
	static int nodomain;
	register char *cp;

	if (ldomain == NULL) {
		ldomain = (char *)malloc(MAXHOSTNAMELEN+1);
		if (ldomain == 0)
			return (0);
	}

	if (len == -1)
		return (!strcmp(rhost, lhost));
	if (strncmp(rhost, lhost, len))
		return (0);
	if (!strcmp(rhost, lhost))
		return (1);
	if (*(lhost + len) != '\0')
		return (0);
	if (nodomain)
		return (0);
	if (!domainp) {
		/*
		 * "domainp" points after the first dot in the host name
		 */
		if (gethostname(ldomain, MAXHOSTNAMELEN) == -1) {
			nodomain = 1;
			return (0);
		}
		ldomain[MAXHOSTNAMELEN] = NULL;
		if ((domainp = index(ldomain, '.')) == (char *)NULL) {
			nodomain = 1;
			return (0);
		}
		domainp++;
		cp = domainp;
		while (*cp) {
			*cp = isupper(*cp) ? tolower(*cp) : *cp;
			cp++;
		}
	}
	return (!strcmp(domainp, rhost + len +1));
}
