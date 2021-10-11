/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_MACHTHREAD_H
#define	_SYS_MACHTHREAD_H

#pragma ident	"@(#)machthread.h	1.17	93/05/27 SMI"

#include <sys/mmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	PROC_REG	%g6		/* pointer to current proc struct */
#define	FLAG_REG	PROC_REG	/* also used as flag for mutex_enter */
#define	THREAD_REG	%g7		/* pointer to current thread data */

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
