#ident	"@(#)rsubs.c 1.6 93/04/28"

/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#include "structs.h"
#include <myrcmd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <pwd.h>
#include <netdb.h>
#include <locale.h>
#include <stropts.h>
#include <poll.h>
#include <string.h>

#ifdef LOGFILE
static FILE *rlogfile;
#endif

static int	rfd2 = -1;
static struct sigaction osigint;
static struct sigaction osigquit;
static struct sigaction osigterm;
static struct sigaction osigpipe;
static int	sendsig();

#ifndef USG
#define	gettext(s)	(s)
#define	textdomain(d)	((char *) 0)
#endif

/*
 * remote_setup - setup remote shell services to a given host
 *	host	- remote host name
 *	user	- user name for permissions during remote execution
 *	cmd	- remote shell command string
 *	nflag	- set if no input path needed by remote command.
 *
 * returns	- a handle used to communicate with remote service
 *		  via remote_read and remote_write calls.
 *
 *	This function provides similar services as rsh. In addition,
 *	it allows the substitution of both remote and local user names
 *	to ease the access permissions needed on client machines
 *	by the dumpex host. This function is very defensive about talking
 *	to the remote client so that the dumpex run will not hang if
 *	the remote client crashes.
 */
rhp_t
remote_setup(char *host, char *user, char *cmd, int nflag)
{
	rhp_t rhp;
	char status;
	int rem, omask;
	struct passwd *pwd;
	struct servent *sp;
	struct sigaction sa;
	sigset_t sigset, osigset;

	if ((rhp = (rhp_t) malloc(sizeof (*rhp))) == 0)
		return (0);

	pwd = getpwuid(getuid());
	if (pwd == 0) {
		(void) fprintf(stderr,
			"remote_setup: Cannot find password entry for uid %d\n",
			getuid());
		free(rhp);
		return (0);
	}

	sp = getservbyname("shell", "tcp");
	if (sp == 0) {
		(void) fprintf(stderr,
			"remote_setup: Cannot find services entry for %s/%s\n",
			"shell", "tcp");
		free(rhp);
		return (0);
	}

#ifdef LOGFILE
	if (!rlogfile)
		rlogfile = fopen(LOGFILE, "a+");
	if (rlogfile) {
		(void) fprintf(rlogfile, "SETUP: %s\n", cmd);
		(void) fflush(rlogfile);
	}
#endif

	/*
	 * Setup the connection to the remote shell service (rshd in Solaris).
	 * Get 2 way stdin/stdout port and a 2 way signal/stderr port.
	 */
	rem = myrcmd(&host, sp->s_port, (user && *user) ? user : pwd->pw_name,
	    (user && *user) ? user : pwd->pw_name, cmd, &rfd2);
	if (rem < 0 || rfd2 < 0) {
		if (*myrcmd_stderr) {
			(void) fprintf(stderr, "%s", myrcmd_stderr);
			log("%s", myrcmd_stderr);
			logmail(NULL, myrcmd_stderr);
		}
		if (rem >= 0)
			(void) close(rem);
		if (rfd2 >= 0)
			(void) close(rfd2);
		rfd2 = -1;
		free(rhp);
		return (0);
	}
	rhp->rh_fd = rem;
	rhp->rh_err = rfd2;

	/*
	 * Setup signal handling. SIGINT, SIGQUIT and SIGTERM will be
	 * forwarded to the remote end. Ignore SIGPIPE so that detecting
	 * a crash of the remote machine can be done synchronously.
	 */
	(void) sigemptyset(&sigset);
	(void) sigaddset(&sigset, SIGINT);
	(void) sigaddset(&sigset, SIGQUIT);
	(void) sigaddset(&sigset, SIGTERM);
	(void) sigprocmask(SIG_BLOCK, &sigset, &osigset);
	(void) sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = (void (*)()) sendsig;
	(void) sigaction(SIGINT, &sa, &osigint);
	(void) sigaction(SIGQUIT, &sa, &osigquit);
	(void) sigaction(SIGTERM, &sa, &osigterm);
	sa.sa_handler = SIG_IGN;
	(void) sigaction(SIGPIPE, &sa, &osigpipe);

	if (nflag) {
		/*
		 * We do not expect the remote service to read any input.
		 * This will cause EOF if it does.
		 */
		(void) shutdown(rem, 1);
	}

	/*
	 * Unmask SIGINT, SIGQUIT and SIGTERM so they can be forwarded.
	 */
	(void) sigprocmask(SIG_SETMASK, &osigset, (sigset_t *) NULL);

	return (rhp);
}

/*
 * remote_read	- return data from the remote service.
 *
 * This routine has similar semantics to read(2), but takes a handle
 * obtained from remote_setup instead of a file descriptor.
 * It actually looks at data from 2 different channels - the stdout
 * and stderr of the remote service. Only data from stdout is returned
 * to the caller. If data arrives on the error channel, this fact is
 * noted in the return value and the data is logged.
 * If the caller wants the stderr data, it can get it by tying stdout
 * and stderr together in the original command string.
 */
int
remote_read(rhp_t rhp, char *buf, int len)
{
	int ret, n;
	char ebuf[BUFSIZ];
	int num_timeout;
	struct pollfd pfd[2];

	pfd[0].fd = rhp->rh_fd;
	pfd[1].fd = rhp->rh_err;
	pfd[0].events = POLLIN|POLLRDNORM|POLLRDBAND|POLLPRI;
	pfd[1].events = POLLIN|POLLRDNORM|POLLRDBAND|POLLPRI;
	num_timeout = 0;
	while (1) {
		/*
		 * 60 second timeout
		 */
		ret = poll(pfd, 2, 60000);
		if (ret < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				continue;
			} else {
				perror("remote_read: poll");
				return (-1);
			}
		} else if (ret == 0) {
			/*
			 * The poll timed out. "Ping" the remote service
			 * by sending a signal 0. If the service went down,
			 * this will result in an EPIPE. If the machine
			 * crashed, errno will also be EPIPE after the protocol
			 * times out.
			 */
			if (sendsig(0) < 0) {
				if (errno == EPIPE) {
					/*
					 * The remote service went away.
					 * Either it crashed or we hit a race
					 * where the data was in transit but
					 * the service is finished.
					 * Wait for 2 consecutive EPIPE errors
					 * before we consider the service gone.
					 */
#ifdef LOGFILE
					if (rlogfile) {
						(void) fprintf(rlogfile,
						    "\nTIMEOUT!\n");
						(void) fflush(rlogfile);
					}
#endif
					if (num_timeout++ == 0)
						continue;
					fprintf(stderr,
					    "rsh: remote service went down\n");
				} else {
					perror("rsh: remote service error");
				}
				errno = ETIMEDOUT;
				return (-1);
			}
			continue;
		}
		if (pfd[0].revents != 0) {
			/*
			 * Stuff on stdout; read it and return it.
			 * If we get a zero (or error) return, don't finish
			 * yet if there is pending stuff on stderr.
			 */
			n = read(rhp->rh_fd, buf, len);
			if (n > 0) {
#ifdef LOGFILE
				if (rlogfile) {
					(void) fprintf(rlogfile, "STDOUT: ");
					(void) fwrite(buf, n, 1, rlogfile);
					(void) fflush(rlogfile);
				}
#endif
				return (n);
			}
			return (n);
		}
		if (pfd[1].revents != 0) {
			/*
			 * Well, there is data on the error channel, just
			 * log it and go on. If something bad happens later,
			 * this could be a clue. If there is an error on
			 * this channel or it is closed, remove it from
			 * the polling list.
			 */
			n = read(rhp->rh_err, ebuf, BUFSIZ-1);
			if (n > 0) {
#ifdef LOGFILE
				if (rlogfile) {
					(void) fprintf(rlogfile, "STDERR: ");
					(void) fwrite(ebuf, n, 1, rlogfile);
					(void) fflush(rlogfile);
				}
#endif
				ebuf[n] = '\0';
				if (ebuf[0] != '\0')
					log("remote_read: %s\n", ebuf);
			} else if (n < 0) {
				log("remote_read: stderr read failure: %s\n",
					strerror(errno));
				pfd[1].fd = -1;
			} else {
				pfd[1].fd = -1;
			}
		}
	}
#ifdef notdef
	struct itimerval it;
	struct sigaction sa, osigalrm;

	while (1) {
		(void) sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		sa.sa_handler = (void (*)()) sendsig;
		(void) sigaction(SIGALRM, &sa, &osigalrm);
		it.it_interval.tv_sec = it.it_value.tv_sec = 60;
		it.it_interval.tv_usec = it.it_value.tv_usec = 0;
		(void) setitimer(ITIMER_REAL, &it, (struct itimerval *) 0);
		n = read(rhp->rh_fd, buf, len);
		it.it_value.tv_sec = it.it_value.tv_usec = 0;
		(void) setitimer(ITIMER_REAL, &it, (struct itimerval *) 0);
		(void) sigaction(SIGALRM, &osigalrm, (struct sigaction *) NULL);
		if (n < 0 && errno == EINTR)
			continue;
#ifdef LOGFILE
		if (n > 0 && rlogfile) {
			(void) fprintf(rlogfile, "STDOUT: ");
			(void) fwrite(buf, n, 1, rlogfile);
			(void) fflush(rlogfile);
		}
#endif
		return (n);
	}
#endif
}

/*
 * remote_write	- write data to the remote service on stdin
 *
 * Same semantics as write(2) except it takes a handle obtain from
 * remote_setup instead of a descriptor. Do not want a signal if
 * the remote service goes away - just let the write fail with
 * errno == EPIPE.
 */
int
remote_write(rhp_t rhp, char *buf, int len)
{
	return (write(rhp->rh_fd, buf, len));
}

void
remote_shutdown(rhp_t rhp)
{
	(void) sigaction(SIGINT, &osigint, (struct sigaction *) NULL);
	(void) sigaction(SIGQUIT, &osigquit, (struct sigaction *) NULL);
	(void) sigaction(SIGTERM, &osigterm, (struct sigaction *) NULL);
	(void) sigaction(SIGPIPE, &osigpipe, (struct sigaction *) NULL);
	(void) close(rhp->rh_fd);
	(void) close(rhp->rh_err);
	(void) free((void *)rhp);
}

int
sendsig(signo)
	int signo;
{
	void (*handler)();
	char csigno;

	if (signo != 0) {
		switch (signo) {
		case SIGINT:
			handler = osigint.sa_handler;
			break;
		case SIGQUIT:
			handler = osigquit.sa_handler;
			break;
		case SIGTERM:
			handler = osigterm.sa_handler;
			break;
		case SIGPIPE:
			handler = osigpipe.sa_handler;
			break;
#ifdef notdef
		case SIGALRM:
			signo = 0;
			/* FALL THROUGH */
#endif
		default:
			handler = (void (*)()) NULL;
			break;
		}
		if (handler && handler != (void (*)()) sendsig &&
		    handler != SIG_DFL && handler != SIG_ERR &&
		    handler != SIG_IGN && handler != SIG_HOLD)
			handler(signo);
	}
	csigno = signo;
	return (write(rfd2, &csigno, 1));
}
