/*	Copyright (c) 1994 by Sun Microsystems, Inc.		*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/


.ident	"@(#)syscall_cancelpoint.s	1.6 95/08/22 SMI" /* SVr4.0 1.9 */


	.file	"syscall_cancelpoint.s"

#include <sys/asm_linkage.h>

/*
 * This file lists all the libc(POSIX.1) and libposix4(POSIX.1b)
 * which have been declared as CANCELLATION POINTS in POSIX.1c.
 *
 * SYSCALL_CANCELPOINT() macro provides the required wrapper to
 * interpose any call defined in an library. It assumes followings:
 * 	1. libthread must ne linked before that library
 *	2. that function should have "_" definition to interpose on.
 * For example, if a function libcfoo() declared in libc and its a
 * system call then define _libcfoo symbol also.
 * Then insert here:
 * 			SYSCALL_CANCELPOINT(libcfoo)
 *
 * This will interpose libcfoo symbol here and
 * wrapper, after creating cancellation point, will call _libcfoo.
 */


/* C library -- read						*/
/* int read (int fildes, void *buf, unsigned nbyte);		*/

/* C library -- close						*/
/* int close (int fildes);					*/

/* C library -- open						*/
/* int open (const char *path, int oflag, [ mode_t mode ]);	*/

/* C library -- write						*/
/* int write (int fildes, void *buf, unsigned nbyte);		*/

/* C library -- fcntl						*/
/* int fcntl (int fildes, int cmd [, arg]);			*/

/* C library -- pause						*/
/* int pause (void);						*/

/* C library -- sigsuspend					*/
/* int sigsuspend (sigset_t *set);				*/

/* C library -- wait						*/
/* int wait (int *stat_loc);					*/

/* C library -- creat						*/
/* int creat (char *path, mode_t mode);				*/

/* C library -- fsync						*/
/* int fsync (int fildes, void *buf, unsigned nbyte);		*/

/* C library -- sleep						*/
/* int sleep (unsigned sleep_tm);				*/
/* ALREADY A CANCELLATION POINT: IT CALLS siggsuspend()		*/

/* C library -- msync						*/
/* int msync (caddr_t addr, size_t  len, int flags);		*/

/* C library -- tcdrain						*/
/* int tcdrain (int fildes);					*/

/* C library -- waitpid						*/
/* int waitpid (pid_t pid, int *stat_loc, int options);		*/

/* C library -- system						*/
/* int system (const char *s);					*/
/* ALREADY A CANCELLATION POINT: IT CALLS waipid()		*/

/* POSIX.4 functions are defined as cancellation points in libposix4 */

/* POSIX.4 library -- sigtimedwait				*/
/* int sigtimedwait (const sigset_t *set, siginfo_t *info, 	*/
/*			const struct timespec *timeout);	*/

/* POSIX.4 library -- sigtimeinfo				*/
/* int sigtwaitinfo (const sigset_t *set, siginfo_t *info); 	*/

/* POSIX.4 library -- nanosleep					*/
/* int nanosleep (const struct timespec *rqtp, struct timespec *rqtp); */

/* POSIX.4 library -- sem_wait					*/
/* int sem_wait (sem_t *sp);					*/

/* POSIX.4 library -- mq_receive				*/
/* int mq_receive ();						*/

/* POSIX.4 library -- mq_send					*/
/* int mq_send ();						*/


#include "sparc/SYS_CANCEL.h"

	SYSCALL_CANCELPOINT(read)

	SYSCALL_CANCELPOINT(close)

	SYSCALL_CANCELPOINT(open)

	SYSCALL_CANCELPOINT(write)

	SYSCALL_CANCELPOINT(fcntl)

	SYSCALL_CANCELPOINT(pause)

	SYSCALL_CANCELPOINT(wait)

	SYSCALL_CANCELPOINT(creat)

	SYSCALL_CANCELPOINT(fsync)

	SYSCALL_CANCELPOINT(msync)

	SYSCALL_CANCELPOINT(tcdrain)

	SYSCALL_CANCELPOINT(waitpid)

/*
 * End of syscall file
 */
