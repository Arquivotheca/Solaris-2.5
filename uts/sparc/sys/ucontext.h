/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_UCONTEXT_H
#define	_SYS_UCONTEXT_H

#pragma ident	"@(#)ucontext.h	1.13	94/03/17 SMI"	/* from SVr4.0 1.10 */

#include <sys/types.h>
#include <sys/regset.h>
#include <sys/signal.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct ucontext {
	u_long		uc_flags;
	struct ucontext	*uc_link;
	sigset_t   	uc_sigmask;
	stack_t 	uc_stack;
	mcontext_t 	uc_mcontext;
	long		uc_filler[23];
} ucontext_t;

#define	GETCONTEXT	0
#define	SETCONTEXT	1

/*
 * values for uc_flags
 * these are implementation dependent flags, that should be hidden
 * from the user interface, defining which elements of ucontext
 * are valid, and should be restored on call to setcontext
 */

#define	UC_SIGMASK	001
#define	UC_STACK	002
#define	UC_CPU		004
#define	UC_MAU		010
#define	UC_FPU		UC_MAU
#define	UC_INTR		020

#define	UC_MCONTEXT	(UC_CPU|UC_FPU)

/*
 * UC_ALL specifies the default context
 */

#define	UC_ALL		(UC_SIGMASK|UC_STACK|UC_MCONTEXT)

#ifdef _KERNEL

void savecontext(ucontext_t *, k_sigset_t);

#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_UCONTEXT_H */
