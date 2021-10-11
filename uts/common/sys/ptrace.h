/*
 * Copyright (c) 1985 by Sun Microsystems, Inc.
 */

#ifndef _SYS_PTRACE_H
#define	_SYS_PTRACE_H

#pragma ident	"@(#)ptrace.h	1.25	94/08/08 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Request values for the ptrace system call
 */

/*
 * XXX - SunOS 5.0 development version: 0-9 correspond to AT&T/SVID defined
 * requests. The remainder are extensions as defined for SunOS 4.1. Currently
 * only GETREGS, SETREGS, GETFPREGS, and SETFPREGS are implemented
 */
#define	PTRACE_TRACEME		0	/* 0, by tracee to begin tracing */
#define	PTRACE_CHILDDONE	0	/* 0, tracee is done with his half */
#define	PTRACE_PEEKTEXT		1	/* 1, read word from text segment */
#define	PTRACE_PEEKDATA		2	/* 2, read word from data segment */
#define	PTRACE_PEEKUSER		3	/* 3, read word from user struct */
#define	PTRACE_POKETEXT		4	/* 4, write word into text segment */
#define	PTRACE_POKEDATA		5	/* 5, write word into data segment */
#define	PTRACE_POKEUSER		6	/* 6, write word into user struct */
#define	PTRACE_CONT		7	/* 7, continue process */
#define	PTRACE_KILL		8	/* 8, terminate process */
#define	PTRACE_SINGLESTEP	9	/* 9, single step process */
#define	PTRACE_ATTACH		10	/* 10, attach to an existing process */
#define	PTRACE_DETACH		11	/* 11, detach from a process */
#define	PTRACE_GETREGS		12	/* 12, get all registers */
#define	PTRACE_SETREGS		13	/* 13, set all registers */
#define	PTRACE_GETFPREGS	14	/* 14, get all floating point regs */
#define	PTRACE_SETFPREGS	15	/* 15, set all floating point regs */
#define	PTRACE_READDATA		16	/* 16, read data segment */
#define	PTRACE_WRITEDATA	17	/* 17, write data segment */
#define	PTRACE_READTEXT		18	/* 18, read text segment */
#define	PTRACE_WRITETEXT	19	/* 19, write text segment */
#define	PTRACE_GETFPAREGS	20	/* 20, get all fpa regs */
#define	PTRACE_SETFPAREGS	21	/* 21, set all fpa regs */
#if defined(sparc) || defined(__sparc)
/* currently unimplemented */
#define	PTRACE_GETWINDOW	22	/* 22, get register window n */
#define	PTRACE_SETWINDOW	23	/* 23, set register window n */
#else	/* defined(sparc) || defined(__sparc) */
#define	PTRACE_22		22	/* 22, filler */
#define	PTRACE_23		23	/* 23, filler */
#endif	/* defined(sparc) || defined(__sparc) */
#define	PTRACE_SYSCALL		24	/* 24, trap next sys call */
#define	PTRACE_DUMPCORE		25	/* 25, dump process core */
#if defined(i386) || defined(__i386)
#define	PTRACE_SETWRBKPT	26	/* 26, set write breakpoint */
#define	PTRACE_SETACBKPT	27	/* 27, set access breakpoint */
#define	PTRACE_CLRDR7		28	/* 28, clear debug register 7 */
#else	/* defined(i386) || defined(__i386) */
#define	PTRACE_26		26	/* 26, filler */
#define	PTRACE_27		27	/* 27, filler */
#define	PTRACE_28		28	/* 28, filler */
#endif	/* defined(i386) || defined(__i386) */
#define	PTRACE_TRAPCODE		29	/* get proc's trap code */
#if defined(i386) || defined(__i386)
#define	PTRACE_SETBPP		30	/* set hw instruction breakpoint */
#endif

#ifdef	_KERNEL
/*
 * Tracing variables.  Used to pass trace command from parent
 * to child being traced.  This data base cannot be shared and
 * is locked per user.
 */

extern kmutex_t 	ptracelock;

struct ipc {
	int	ip_ppid;	/* pid of process who is ptracing */
	int	ip_tid;		/* thread doing ptracing */
	int	ip_pid;		/* pid of process being ptraced. */
	int	ip_req;
	int	*ip_addr;
	int	ip_data;
#ifdef	SUNPTRACE
	struct 	regs ip_regs;	/* The regs, psw, and pc	*/
#ifdef	FPU
	struct	fpu  ip_fpu;	/* Floating point processor	*/
#endif	/* FPU */
#endif	/* SUNPTRACE */
};
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PTRACE_H */
