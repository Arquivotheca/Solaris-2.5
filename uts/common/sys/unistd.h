/*
 *	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
 *	  All Rights Reserved
 *	Copyright (c) 1993, by Sun Microsystems, Inc.
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 */

/*
 * WARNING: This is an implementation-specific header,
 * its contents are not guaranteed. Applications
 * should include <unistd.h> and not this header.
 */

#ifndef _SYS_UNISTD_H
#define	_SYS_UNISTD_H

#pragma ident	"@(#)unistd.h	1.23	95/08/24 SMI"	/* From SVR4.0 1.3 */

#include <sys/feature_tests.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* command names for POSIX sysconf */

/* POSIX.1 names */
#define	_SC_ARG_MAX	1
#define	_SC_CHILD_MAX	2
#define	_SC_CLK_TCK	3
#define	_SC_NGROUPS_MAX 4
#define	_SC_OPEN_MAX	5
#define	_SC_JOB_CONTROL 6
#define	_SC_SAVED_IDS	7
#define	_SC_VERSION	8
/* SVR4 names */
#define	_SC_PASS_MAX	9
#define	_SC_LOGNAME_MAX	10
#define	_SC_PAGESIZE	11
#define	_SC_XOPEN_VERSION	12
/* 13 reserved for SVr4-ES/MP _SC_NACLS_MAX */
#define	_SC_NPROCESSORS_CONF	14
#define	_SC_NPROCESSORS_ONLN	15
#define	_SC_STREAM_MAX	16
#define	_SC_TZNAME_MAX	17
/* POSIX.4 names */
#define	_SC_AIO_LISTIO_MAX	18
#define	_SC_AIO_MAX	19
#define	_SC_AIO_PRIO_DELTA_MAX	20
#define	_SC_ASYNCHRONOUS_IO	21
#define	_SC_DELAYTIMER_MAX	22
#define	_SC_FSYNC	23
#define	_SC_MAPPED_FILES	24
#define	_SC_MEMLOCK	25
#define	_SC_MEMLOCK_RANGE	26
#define	_SC_MEMORY_PROTECTION	27
#define	_SC_MESSAGE_PASSING	28
#define	_SC_MQ_OPEN_MAX	29
#define	_SC_MQ_PRIO_MAX	30
#define	_SC_PRIORITIZED_IO	31
#define	_SC_PRIORITY_SCHEDULING	32
#define	_SC_REALTIME_SIGNALS	33
#define	_SC_RTSIG_MAX	34
#define	_SC_SEMAPHORES	35
#define	_SC_SEM_NSEMS_MAX	36
#define	_SC_SEM_VALUE_MAX	37
#define	_SC_SHARED_MEMORY_OBJECTS	38
#define	_SC_SIGQUEUE_MAX	39
#define	_SC_SIGRT_MIN	40
#define	_SC_SIGRT_MAX	41
#define	_SC_SYNCHRONIZED_IO	42
#define	_SC_TIMERS	43
#define	_SC_TIMER_MAX	44
/* XPG4 names */
#define	_SC_2_C_BIND		45
#define	_SC_2_C_DEV    		46
#define	_SC_2_C_VERSION		47
#define	_SC_2_FORT_DEV 		48
#define	_SC_2_FORT_RUN 		49
#define	_SC_2_LOCALEDEF		50
#define	_SC_2_SW_DEV   		51
#define	_SC_2_UPE		52
#define	_SC_2_VERSION		53
#define	_SC_BC_BASE_MAX		54
#define	_SC_BC_DIM_MAX 		55
#define	_SC_BC_SCALE_MAX	56
#define	_SC_BC_STRING_MAX	57
#define	_SC_COLL_WEIGHTS_MAX	58
#define	_SC_EXPR_NEST_MAX	59
#define	_SC_LINE_MAX 		60
#define	_SC_RE_DUP_MAX		61
#define	_SC_XOPEN_CRYPT		62
#define	_SC_XOPEN_ENH_I18N	63
#define	_SC_XOPEN_SHM		64

/* command name for XPG confstr */
#define	_CS_PATH	65

/* additional XSH4/XCU4 command names for sysconf */
#define	_SC_2_CHAR_TERM		66
#define	_SC_XOPEN_XCU_VERSION	67


#define	_SC_PHYS_PAGES		500
#define	_SC_AVPHYS_PAGES	501

/*
 * POSIX.1c (pthreads) names. These values are defined above
 * the sub-500 range. See psarc case 1995/257.
 */
#define	_SC_THREAD_DESTRUCTOR_ITERATIONS 568
#define	_SC_GETGR_R_SIZE_MAX		569
#define	_SC_GETPW_R_SIZE_MAX		570
#define	_SC_LOGIN_NAME_MAX		571
#define	_SC_THREAD_KEYS_MAX		572
#define	_SC_THREAD_STACK_MIN		573
#define	_SC_THREAD_THREADS_MAX		574
#define	_SC_TTY_NAME_MAX		575
#define	_SC_THREADS			576
#define	_SC_THREAD_ATTR_STACKADDR	577
#define	_SC_THREAD_ATTR_STACKSIZE	578
#define	_SC_THREAD_PRIORITY_SCHEDULING	579
#define	_SC_THREAD_PRIO_INHERIT		580
#define	_SC_THREAD_PRIO_PROTECT		581
#define	_SC_THREAD_PROCESS_SHARED	582
#define	_SC_THREAD_SAFE_FUNCTIONS	583

/* command names for POSIX pathconf */

/* POSIX.1 names */
#define	_PC_LINK_MAX	1
#define	_PC_MAX_CANON	2
#define	_PC_MAX_INPUT	3
#define	_PC_NAME_MAX	4
#define	_PC_PATH_MAX	5
#define	_PC_PIPE_BUF	6
#define	_PC_NO_TRUNC	7
#define	_PC_VDISABLE	8
#define	_PC_CHOWN_RESTRICTED	9
/* POSIX.4 names */
#define	_PC_ASYNC_IO	10
#define	_PC_PRIO_IO	11
#define	_PC_SYNC_IO	12
#define	_PC_LAST	12

#ifndef	_POSIX_VERSION
#define	_POSIX_VERSION		199506L	/* Supports ISO POSIX-1c DIS */
#endif

#ifndef	_POSIX2_VERSION
#define	_POSIX2_VERSION		199209L	/* Supports ISO POSIX-2 DIS C */
#endif					/* Language binding. */

#ifndef	_POSIX2_C_VERSION
#define	_POSIX2_C_VERSION	199209L	/* Supports ISO POSIX-2 DIS */
#endif

#ifndef	_XOPEN_VERSION
#define	_XOPEN_VERSION		3	/* Supports XPG3 */
#define	_XOPEN_XPG3
#endif

#ifndef	_XOPEN_XCU_VERSION
#define	_XOPEN_XCU_VERSION	4	/* Supports XCU4 */
#endif

#define	_POSIX2_C_BIND		1 	/* Supports C Language Bindings */
#define	_POSIX2_CHAR_TERM	1 	/* Supports at least 1 terminal type */
#define	_POSIX2_LOCALEDEF	1 	/* Supports creation of locales */
#define	_POSIX2_C_DEV		1	/* Supports C language dev utility */
#define	_POSIX2_SW_DEV		1 	/* Supports S/W Devlopement Utility */
#define	_POSIX2_UPE		1 	/* Supports User Portability Utility */

#define	_XOPEN_XPG4			/* Supports XPG4 */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UNISTD_H */
