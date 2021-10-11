/*
 *       Copyright (c) 1986 by Sun Microsystems, Inc.
 */

#ifndef	_LIBC_SPARC_INC_SYS_H
#define	_LIBC_SPARC_INC_SYS_H

#ident	"@(#)SYS.h	1.23	92/09/05 SMI"

/*
 * This file defines common code sequences for system calls. Note that it
 * is assumed that _cerror is within the short branch distance from all
 * the traps (so that a simple bcs can follow the trap, rather than a
 * position independent code sequence.)
 */
#include <sys/asm_linkage.h>
#include <sys/syscall.h>
#include <sys/errno.h>
#include "synonyms.h"
#ifdef PIC
#include "PIC.h"
#endif

/*
 * Trap number for system calls
 */
#define	SYSCALL_TRAPNUM	8
#define FASTSCALL_TRAPNUM 9

/*
 * Define the external symbol _cerror for all files.
 */
	.global	_cerror

/*
 * SYSTRAP provides the actual trap sequence. It assumes that an entry
 * of the form SYS_name exists (probably from syscall.h).
 */
#define SYSTRAP(name) \
	mov	SYS_/**/name, %g1; \
	t	SYSCALL_TRAPNUM

/*
 * XXX: Not sure if the following is necessary...delete if not
 */
#define FASTSYSTRAP(name) \
        mov     SYS_/**/name, %g1; \
        add     %g1, 0x800, %g1; \
        t       FASTSCALL_TRAPNUM

/*
 * SYSCERROR provides the sequence to branch to _cerror if an error is
 * indicated by the carry-bit being set upon return from a trap.
 */
#ifdef PIC 
#define	SYSCERROR \
	bcc	1f; \
	PIC_SETUP(o5); \
	ld	[%o5 + _cerror], %o5; \
	jmp	%o5; \
	nop; \
1:
#else 
#define	SYSCERROR \
	bcs	_cerror; \
	nop 
#endif

/*
 * SYSLWPERR provides the sequence to return 0 on a successful trap
 * and the error code if unsuccessful.
 * XXX - ERESTART is converted to EINTR, just as _cerror does - why is this
 * error converted ?
 * Error is indicated by the carry-bit being set upon return from a trap.
 */
#define SYSLWPERR \
        bcc,a   1f; \
        clr     %o0; \
        cmp     %o0, ERESTART; \
        beq,a   1f; \
        mov     EINTR, %o0; \
1:
 

/*
 * SYSREENTRY provides the entry sequence for restartable system calls.
 */
#define SYSREENTRY(name) \
	ENTRY(name); \
	st	%o0,[%sp+68]; \
.restart_/**/name:

/*
 * SYSRESTART provides the error handling sequence for restartable
 * system calls.
 */
#ifdef PIC
#define	SYSRESTART(name) \
	bcc	1f; \
	cmp	%o0, ERESTART; \
	be,a	name; \
	ld	[%sp+68], %o0; \
	PIC_SETUP(o5); \
	ld	[%o5 + _cerror], %o5; \
	jmp	%o5; \
	nop	; \
1:
#else
#define	SYSRESTART(name) \
	bcc	1f; \
	cmp	%o0, ERESTART; \
	be,a	name; \
	ld	[%sp+68], %o0; \
	ba	_cerror; \
	nop	; \
1:
#endif
 
/*
 * SYSINTR_RESTART provides the error handling sequence for restartable
 * system calls in case of EINTR or ERESTART.
 */
#define SYSINTR_RESTART(name) \
        bcc,a   1f; \
        clr     %o0; \
        cmp     %o0, ERESTART; \
        be,a    name; \
        ld      [%sp+68], %o0; \
        cmp     %o0, EINTR; \
        be,a    name; \
        ld      [%sp+68], %o0; \
1:
 
/*
 * SYSCALL provides the standard (i.e.: most common) system call sequence.
 */
#define SYSCALL(name) \
	ENTRY(name); \
	SYSTRAP(name); \
	SYSCERROR
/*
 * XXX: Not sure if this is necessary...see FASTSYSTRAP() above.
 * FASTSYSCALL works likes SYSCALL without the crap.
 */
#define FASTSYSCALL(name) \
        ENTRY(name); \
        FASTSYSTRAP(name); \
        SYSCERROR

/*
 * SYSCALL_RESTART provides the most common restartable system call sequence.
 */
#define SYSCALL_RESTART(name) \
	SYSREENTRY(name); \
	SYSTRAP(name); \
	SYSRESTART(.restart_/**/name)

/*
 * SYSCALL_NOERROR provides the most common system call sequence for those
 * system calls which don't check the error reture (carry bit).
 */
#define SYSCALL_NOERROR(name) \
	ENTRY(name); \
	SYSTRAP(name)

/*
 * Standard syscall return sequence.
 */
#define RET \
	retl; \
	nop

/*
 * Syscall return sequence with return code forced to zero.
 */
#define RETC \
	retl; \
	clr	%o0

#endif	/* _LIBC_SPARC_INC_SYS_H */
