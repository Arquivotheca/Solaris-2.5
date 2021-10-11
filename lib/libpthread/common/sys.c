/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	" @(#)sys.c 1.12 95/09/21 SMI"

#include <sys/types.h>
#include <stropts.h>
#include <poll.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/uio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <ucontext.h>


#pragma init(__fork_init)

__fork_init()
{
	printf("Error:Calling libpthread Init Locally\n");
	return (0);
}

_getfp() {
	return (0);
}

/*
 * Stub library for programmer's interface to the dynamic linker.  Used
 * to satisfy ld processing, and serves as a precedence place-holder at
 * execution-time.  These routines are never actually called.
 */

#ifdef DEBUG

#pragma weak _lwp_cond_wait = __lwp_cond_wait
#pragma weak _lwp_cond_timedwait = __lwp_cond_timedwait

int
__lwp_cond_wait(cond_t *cv, mutex_t *mp)
{
	return (0);
}

int
__lwp_cond_timedwait(cond_t *cv, mutex_t *mp, timestruc_t *ts)
{
	return (0);
}
#endif /* DEBUG */

#ifdef __STDC__

#pragma weak fork = _fork
#pragma weak fork1 = _fork1
#pragma weak sigaction = _sigaction
#pragma weak sigprocmask = _sigprocmask
#pragma weak sigwait = _sigwait
#pragma weak sigsuspend = _sigsuspend
#pragma weak sigsetjmp = _sigsetjmp
#pragma weak siglongjmp = _siglongjmp
#pragma weak sleep = _sleep
#pragma weak alarm = _alarm
#pragma weak setitimer = _setitimer

#endif /* __STDC__ */

/*
 * Following functions have been interposed in libthread,
 * weak as well as strong symbol
 */

int
_fork1()
{
	return (0);
}

int
_fork()
{
	return (0);
}

_sigaction(int sig, const struct sigaction *nact, struct sigaction *oact)
{
	return (0);
}

int
_sigprocmask(int how, sigset_t *set, sigset_t *oset)
{
	return (0);
}

int
_sigwait(sigset_t *set)
{
	return (0);
}

int
_sigsuspend(sigset_t *set)
{
	return (0);
}

int
_sigsetjmp(sigjmp_buf env, int savemask)
{
	return (0);
}

void
_siglongjmp(sigjmp_buf env, int val)
{
	return;
}

unsigned
_sleep(unsigned sleep_tm)
{
	return (0);
}

unsigned
_alarm(unsigned sec)
{
	return (0);
}

int
_setitimer(int which, const struct itimerval *value,
			struct itimerval *ovalue)
{
	return (0);
}

/*
 * Following functions do not have pragma weak version.
 * These have been interposed as they are in libthread.
 */

int
__sigtimedwait(const sigset_t *set, siginfo_t *info)
{
	return (0);
}

/*
 * Following functions do not have their "_" version in libthread.
 * XXX: Should libthread interpose on both weak and strong symbols?
 */

int
setcontext(ucontext_t *ucp)
{
	return (0);
}

int
sigpending(sigset_t *set)
{
	return (0);
}

/*
 * Following C library entry points are defined here because they are
 * cancellation points and libthread interpose them.
 */

/* C library -- read						*/
int
read(int fildes, void *buf, unsigned nbyte)
{
	return (-1);
}

/* C library -- close						*/
int
close(int fildes)
{
	return (-1);
}

/* C library -- open						*/
int
open(const char *path, int oflag, mode_t mode)
{
	return (-1);
}

/* C library -- write						*/
int
write(int fildes, void *buf, unsigned nbyte)
{
	return (-1);
}

/* C library -- fcntl						*/
int
fcntl(int fildes, int cmd, int arg)
{
	return (-1);
}

/* C library -- pause						*/
int
pause(void)
{
	return (-1);
}


/* C library -- wait						*/
int
wait(int *stat_loc)
{
	return (-1);
}

/* C library -- creat						*/
int
creat(char *path, mode_t mode)
{
	return (-1);
}

/* C library -- fsync						*/
int
fsync(int fildes, void *buf, unsigned nbyte)
{
	return (-1);
}


msync(caddr_t addr, size_t  len, int flags)
{
	return (-1);
}

/* C library -- tcdrain						*/
int
tcdrain(int fildes)
{
	return (-1);
}

/* C library -- waitpid						*/
int
waitpid(pid_t pid, int *stat_loc, int options)
{
	return (-1);
}
