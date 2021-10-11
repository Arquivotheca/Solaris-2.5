/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)sched.c	1.6	94/12/02 SMI"

#include "synonyms.h"
#include <sched.h>
#include <errno.h>


int
sched_setparam(pid_t pid, const struct sched_param *param)
{
	errno = ENOSYS;
	return (-1);
}

int
sched_getparam(pid_t pid, struct sched_param *param)
{
	errno = ENOSYS;
	return (-1);
}

int
sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
	errno = ENOSYS;
	return (-1);
}

int
sched_getscheduler(pid_t pid)
{
	errno = ENOSYS;
	return (-1);
}

int
sched_yield(void)
{
	if (_thr_main() == -1)
		yield();	/* single-threaded */
	else
		_thr_yield();	/* multithreaded, libthread has been linked */
	return (0);
}

int
sched_get_priority_max(int policy)
{
	errno = ENOSYS;
	return (-1);
}

int
sched_get_priority_min(int policy)
{
	errno = ENOSYS;
	return (-1);
}

int
sched_rr_get_interval(pid_t pid, struct timespec *interval)
{
	errno = ENOSYS;
	return (-1);
}
