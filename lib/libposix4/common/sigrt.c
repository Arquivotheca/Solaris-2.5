/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)sigrt.c	1.4	94/10/26 SMI"

#ifdef __STDC__
	#pragma weak sigwaitinfo = _sigwaitinfo
	#pragma weak sigtimedwait = _sigtimedwait
	#pragma weak sigqueue = _sigqueue
#endif

#include <signal.h>
#include <siginfo.h>

int
_sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
	return (__sigtimedwait(set, info, NULL));
}

int
_sigtimedwait(const sigset_t *set, siginfo_t *info,
    const struct timespec *timeout)
{
	return (__sigtimedwait(set, info, timeout));
}

int
_sigqueue(pid_t pid, int signo, const union sigval value)
{
	return (__sigqueue(pid, signo, value));
}
