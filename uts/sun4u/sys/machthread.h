/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_MACHTHREAD_H
#define	_SYS_MACHTHREAD_H

#pragma ident	"@(#)machthread.h	1.6	95/03/22 SMI"

#include <sys/asi.h>
#include <sys/spitasi.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	PROC_REG	%g6		/* pointer to current proc struct */
#define	THREAD_REG	%g7		/* pointer to current thread data */

/*
 * CPU_INDEX(r)
 * Returns cpu id in r.
 * On Sun5 machines, this is equivalent to the mid field of the
 * UPA Config register.
 */
#define	CPU_INDEX(r)			\
	ldxa	[%g0]ASI_UPA_CONFIG, r;	\
	srlx	r, 17, r;		\
	and	r, 0x1F, r

/*
 * Assembly macro to find address of the current CPU.
 * Used when coming in from a user trap - cannot use THREAD_REG.
 * Args are destination register and one scratch register.
 */
#define	CPU_ADDR(reg, scr) 		\
	.global	cpu;			\
	CPU_INDEX(scr);			\
	sll	scr, 2, scr;		\
	set	cpu, reg;		\
	ld	[reg + scr], reg

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MACHTHREAD_H */
