/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sysconf.c	1.19	95/08/24 SMI"	/* SVr4.0 1.6	*/

/* sysconf(3C) - returns system configuration information */

#ifdef __STDC__
#pragma weak sysconf = _sysconf
#endif

#include "synonyms.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/sysconfig.h>
#include <sys/errno.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <nss_dbdefs.h>

long
sysconf(name)
int name;
{
extern int __xpg4;

	switch (name) {
		default:
			errno = EINVAL;
			return (-1);

		case _SC_ARG_MAX:
			return (ARG_MAX);

		case _SC_CLK_TCK:
			return (_sysconfig(_CONFIG_CLK_TCK));

		case _SC_JOB_CONTROL:
			return (_POSIX_JOB_CONTROL);

		case _SC_SAVED_IDS:
			return (_POSIX_SAVED_IDS);

		case _SC_CHILD_MAX:
			return (_sysconfig(_CONFIG_CHILD_MAX));

		case _SC_NGROUPS_MAX:
			return (_sysconfig(_CONFIG_NGROUPS));

		case _SC_OPEN_MAX:
			return (_sysconfig(_CONFIG_OPEN_FILES));

		case _SC_VERSION:
			return (_sysconfig(_CONFIG_POSIX_VER));

		case _SC_PAGESIZE:
			return (_sysconfig(_CONFIG_PAGESIZE));

		case _SC_XOPEN_VERSION:
			if (__xpg4 == 0)
				return (3);
			else
				return (4);

		case _SC_XOPEN_XCU_VERSION:
			return (_XOPEN_XCU_VERSION);

		case _SC_PASS_MAX:
			return (PASS_MAX);

		case _SC_LOGNAME_MAX:
			return (LOGNAME_MAX);

		case _SC_STREAM_MAX:
			return (_sysconfig(_CONFIG_OPEN_FILES));

		case _SC_TZNAME_MAX:
			return (-1);

		case _SC_NPROCESSORS_CONF:
			return (_sysconfig(_CONFIG_NPROC_CONF));

		case _SC_NPROCESSORS_ONLN:
			return (_sysconfig(_CONFIG_NPROC_ONLN));

		/* POSIX.4 names */
		case _SC_ASYNCHRONOUS_IO:
#ifdef _POSIX_ASYNCHRONOUS_IO
			return (1);
#else
			return (-1);
#endif

		case _SC_FSYNC:
#ifdef _POSIX_FSYNC
			return (1);
#else
			return (-1);
#endif

		case _SC_MAPPED_FILES:
#ifdef _POSIX_MAPPED_FILES
			return (1);
#else
			return (-1);
#endif

		case _SC_MEMLOCK:
#ifdef _POSIX_MEMLOCK
			return (1);
#else
			return (-1);
#endif

		case _SC_MEMLOCK_RANGE:
#ifdef _POSIX_MEMLOCK_RANGE
			return (1);
#else
			return (-1);
#endif

		case _SC_MEMORY_PROTECTION:
#ifdef _POSIX_MEMORY_PROTECTION
			return (1);
#else
			return (-1);
#endif

		case _SC_MESSAGE_PASSING:
#ifdef _POSIX_MESSAGE_PASSING
			return (1);
#else
			return (-1);
#endif

		case _SC_PRIORITIZED_IO:
#ifdef _POSIX_PRIORITIZED_IO
			return (1);
#else
			return (-1);
#endif

		case _SC_PRIORITY_SCHEDULING:
#ifdef _POSIX_PRIORITY_SCHEDULING
			return (1);
#else
			return (-1);
#endif

		case _SC_REALTIME_SIGNALS:
#ifdef _POSIX_REALTIME_SIGNALS
			return (1);
#else
			return (-1);
#endif

		case _SC_SEMAPHORES:
#ifdef _POSIX_SEMAPHORES
			return (1);
#else
			return (-1);
#endif

		case _SC_SHARED_MEMORY_OBJECTS:
#ifdef _POSIX_SHARED_MEMORY_OBJECTS
			return (1);
#else
			return (-1);
#endif

		case _SC_SYNCHRONIZED_IO:
#ifdef _POSIX_SYNCHRONIZED_IO
			return (1);
#else
			return (-1);
#endif

		case _SC_TIMERS:
#ifdef _POSIX_TIMERS
			return (1);
#else
			return (-1);
#endif

		case _SC_AIO_LISTIO_MAX:
			return (_sysconfig(_CONFIG_AIO_LISTIO_MAX));

		case _SC_AIO_MAX:
			return (_sysconfig(_CONFIG_AIO_MAX));

		case _SC_AIO_PRIO_DELTA_MAX:
			return (_sysconfig(_CONFIG_AIO_PRIO_DELTA_MAX));

		case _SC_DELAYTIMER_MAX:
			return (_sysconfig(_CONFIG_DELAYTIMER_MAX));

		case _SC_MQ_OPEN_MAX:
			return (_sysconfig(_CONFIG_MQ_OPEN_MAX));

		case _SC_MQ_PRIO_MAX:
			return (_sysconfig(_CONFIG_MQ_PRIO_MAX));

		case _SC_RTSIG_MAX:
			return (_sysconfig(_CONFIG_RTSIG_MAX));

		case _SC_SEM_NSEMS_MAX:
			return (_sysconfig(_CONFIG_SEM_NSEMS_MAX));

		case _SC_SEM_VALUE_MAX:
			return (_sysconfig(_CONFIG_SEM_VALUE_MAX));

		case _SC_SIGQUEUE_MAX:
			return (_sysconfig(_CONFIG_SIGQUEUE_MAX));

		case _SC_SIGRT_MAX:
			return (_sysconfig(_CONFIG_SIGRT_MAX));

		case _SC_SIGRT_MIN:
			return (_sysconfig(_CONFIG_SIGRT_MIN));

		case _SC_TIMER_MAX:
			return (_sysconfig(_CONFIG_TIMER_MAX));

		case _SC_PHYS_PAGES:
			return (_sysconfig(_CONFIG_PHYS_PAGES));

		case _SC_AVPHYS_PAGES:
			return (_sysconfig(_CONFIG_AVPHYS_PAGES));

		case _SC_2_C_BIND:
			return (_POSIX2_C_BIND);

		case _SC_2_CHAR_TERM:
			return (_POSIX2_CHAR_TERM);

		case _SC_2_C_DEV:
			return (_POSIX2_C_DEV);

		case _SC_2_C_VERSION:
			return (_POSIX2_C_VERSION);

		case _SC_2_FORT_DEV:
		case _SC_2_FORT_RUN:
			return (-1);

		case _SC_2_LOCALEDEF:
			return (_POSIX2_LOCALEDEF);

		case _SC_2_SW_DEV:
			return (_POSIX2_SW_DEV);

		case _SC_2_UPE:
			return (_POSIX2_UPE);

		case _SC_2_VERSION:
			return (_POSIX2_VERSION);

		case _SC_BC_BASE_MAX:
			return (BC_BASE_MAX);

		case _SC_BC_DIM_MAX:
			return (BC_DIM_MAX);

		case _SC_BC_SCALE_MAX:
			return (BC_SCALE_MAX);

		case _SC_BC_STRING_MAX:
			return (BC_STRING_MAX);

		case _SC_COLL_WEIGHTS_MAX:
			return (COLL_WEIGHTS_MAX);

		case _SC_EXPR_NEST_MAX:
			return (EXPR_NEST_MAX);

		case _SC_LINE_MAX:
			return (LINE_MAX);

		case _SC_RE_DUP_MAX:
			return (RE_DUP_MAX);

		case _SC_XOPEN_CRYPT:
			return (1);

		case _SC_XOPEN_ENH_I18N:
			return (1);

		case _SC_XOPEN_SHM:
			return (1);

		case _SC_THREAD_DESTRUCTOR_ITERATIONS:
			return (-1);

		case _SC_GETGR_R_SIZE_MAX:
			return (NSS_BUFLEN_GROUP);

		case _SC_GETPW_R_SIZE_MAX:
			return (NSS_BUFLEN_PASSWD);

		case _SC_LOGIN_NAME_MAX:
			return (LOGNAME_MAX + 1);

		case _SC_THREAD_KEYS_MAX:
			return (-1);

		case _SC_THREAD_STACK_MIN:
			return (_thr_min_stack());

		case _SC_THREAD_THREADS_MAX:
			return (-1);

		case _SC_TTY_NAME_MAX:
			return (TTYNAME_MAX);

		case _SC_THREADS:
#ifdef _POSIX_THREADS
			return (1);
#else
			return (-1);
#endif

		case _SC_THREAD_ATTR_STACKADDR:
#ifdef _POSIX_THREAD_ATTR_STACKADDR
			return (1);
#else
			return (-1);
#endif

		case _SC_THREAD_ATTR_STACKSIZE:
#ifdef _POSIX_THREAD_ATTR_STACKSIZE
			return (1);
#else
			return (-1);
#endif

		case _SC_THREAD_PRIORITY_SCHEDULING:
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING
			return (1);
#else
			return (-1);
#endif

		case _SC_THREAD_PRIO_INHERIT:
#ifdef _POSIX_THREAD_PRIO_INHERIT
			return (1);
#else
			return (-1);
#endif

		case _SC_THREAD_PRIO_PROTECT:
#ifdef _POSIX_THREAD_PRIO_PROTECT
			return (1);
#else
			return (-1);
#endif

		case _SC_THREAD_PROCESS_SHARED:
#ifdef _POSIX_THREAD_PROCESS_SHARED
			return (1);
#else
			return (-1);
#endif

		case _SC_THREAD_SAFE_FUNCTIONS:
#ifdef _POSIX_THREAD_SAFE_FUNCTIONS
			return (1);
#else
			return (-1);
#endif

	}
}
