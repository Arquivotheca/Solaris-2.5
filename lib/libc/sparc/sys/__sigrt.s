/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)__sigrt.s	1.3	94/01/19 SMI"

#include <sys/asm_linkage.h>
#include "SYS.h"

	.file "__sigrt.s"


/*
 * int
 * __sigqueue(pid, signo, value)
 *	pid_t pid;
 *	int signo;
 *	const union sigval value;
 */

	ENTRY(__sigqueue)
	ld	[%o2],%o2	/* SPARC ABI passes union in memory */
	SYSTRAP(sigqueue)
	SYSCERROR
	RET
	SET_SIZE(__sigqueue)

/*
 * int
 * __sigtimedwait(set, info, timeout)
 *	const sigset_t *set;
 *	siginfo_t *info;
 *	const struct timespec *timeout;
 */

	ENTRY(__sigtimedwait)
	SYSTRAP(sigtimedwait)
	SYSCERROR
	RET
	SET_SIZE(__sigtimedwait)
