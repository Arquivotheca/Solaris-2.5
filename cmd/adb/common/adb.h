/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 */

#ifndef _ADB_ADB_H
#define	_ADB_ADB_H

#ident	"@(#)adb.h	1.40	95/06/13 SMI"

/*
 * adb - a debugger
 *
 * symbolic and kernel enhanced 4.2bsd version.
 *
 * this is a 32 bit machine version of this program.
 * it keeps large data structures in core, and expects
 * in several places that an int can represent 32 integers
 * and file indices.
 */

#include <sys/types.h>

#if ADB
#	if !defined(_KERNEL) /* Egregious kludge */
#		define _KERNEL
#		include <sys/siginfo.h>
#		undef _KERNEL
#	endif
#endif	/* ADB */

#include <sys/user.h>
#include <sys/procfs.h>

#ifdef	__ELF
#include <sys/elf.h>
#else	/* __ELF */
#include <a.out.h>
#endif	/* __ELF */
#define _a_out_h
/*
 * XXX - The above define should ultimately be taken out.  It's a
 * kluge until <stab.h> is corrected.
 *
 * <kvm.h> includes <nlist.h>, "adb.h" includes <stab.h>.
 * Both <nlist.h> and <stab.h> define 'nlist'.  One of them had to go,
 * chose the one in <stab.h>.  This problem is avoided in sparc by
 * having a local "stab.h" which is not desirable.
 */

#include <stab.h>
#include <sys/param.h>
#ifdef sparc
#include <vfork.h>
#endif sparc

#if	defined(__STDC__) && !defined(KADB)
#include <stdlib.h>
#endif	/* __STDC__ */

#include <ctype.h>

#undef NFILE    /* from sys/param.h */

#if defined(sparc)
#	include "../sparc/sparc.h"
#elif defined(i386)
#	include "../i386/i386.h"
#else
#error	ISA not supported
#endif

#include "process.h"


/* Address space constants	  cmd	which space		*/
#define NSP	0		/* =	(no- or number-space)	*/
#define	ISP	1		/* ?	(object/executable)	*/
#define	DSP	2		/* /	(core file)		*/
#define	SSP	3		/* @	(source file addresses) */

#define STAR	4

#define	NSYM	0		/* no symbol space */
#define	ISYM	1		/* symbol in I space */
#define	DSYM	1		/* symbol in D space (== I space on VAX) */

#define NVARS	10+26+26	/* variables [0..9,a..z,A..Z] */
#define ADB_DEBUG_MAX 9		/* max 'level' in db_printf(level, ...) */
#define PROMPT_LEN_MAX 35	/* max length of prompt */
#define LIVE_KERNEL_NAMELIST "/dev/ksyms"
#define LIVE_KERNEL_COREFILE "/dev/mem"

/* result type declarations */
char	*exform();

#ifndef	__STDC__		/* std C uses prototype in stdio.h. */
/* VARARGS */
int	printf();

char	*malloc(), *realloc(), *calloc();

#endif	/* (__STDC__ not defined) */
int	ptrace();
long	lseek();

char	*strcpy();

/* miscellaneous globals */
char	*errflg;	/* error message, synchronously set */
char	*lp;		/* input buffer pointer */
int	interrupted;	/* was command interrupted ? */
int	ditto;		/* last address expression */
int	lastcom;	/* last command (=, /, ? or @) */
int	var[NVARS];	/* variables [0..9,a..z,A..Z] */
int	expv;		/* expression value from last expr() call */
char	sigpass[NSIG];	/* pass-signal-to-subprocess flags	  */
int	adb_pgrpid;	/* used by SIGINT and SIGQUIT signal handlers */

/*
 * Earlier versions of adb kept the registers within the "struct core".
 * On the sparc, I broke them out into their own separate structure
 * to make it easier to deal with the differences among adb, kadb and
 * adb -k.  This structure "allregs" is defined in "allregs.h" and the
 * variable (named "adb_regs") is declared only in accesssr.c.
 */
prstatus_t	Prstatus;

#ifdef sparc
prxregset_t	xregs;
#endif
#if !defined(i386) || !defined(KADB)
prfpregset_t	Prfpregs;
#endif

struct adb_raddr adb_raddr;

/*
 * Used for extended command processing
 */
struct ecmd {
	char *name;
	int (*func)();
	char *help;
};

#define MAKE_LL(upper, lower)	\
	(((u_longlong_t)(upper) << 32) | (u_long)(lower))

extern	errno;

int	hadaddress;	/* command had an address ? */
int	address;	/* address on command */
int	hadcount;	/* command had a count ? */
int	count;		/* count on command */

int	radix;		/* radix for output */
int	maxoff;

char	nextc;		/* next character for input */
char	lastc;		/* previous input character */

int	xargc;		/* externally available argc */
int	wtflag;		/* -w flag ? */

void	(*sigint)();
void	(*sigqit)();

#endif	/* _ADB_ADB_H */
