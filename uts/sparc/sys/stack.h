/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef _SYS_STACK_H
#define	_SYS_STACK_H

#pragma ident	"@(#)stack.h	1.8	95/02/08 SMI"
/* Extracted from asm_linkage from SunOS 4.0 1.4 (not strictly asm related) */

#if !defined(_ASM)

#include <sys/types.h>

#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * A stack frame looks like:
 *
 * %fp->|				|
 *	|-------------------------------|
 *	|  Locals, temps, saved floats	|
 *	|-------------------------------|
 *	|  outgoing parameters past 6	|
 *	|-------------------------------|-\
 *	|  6 words for callee to dump	| |
 *	|  register arguments		| |
 *	|-------------------------------|  > minimum stack frame
 *	|  One word struct-ret address	| |
 *	|-------------------------------| |
 *	|  16 words to save IN and	| |
 * %sp->|  LOCAL register on overflow	| |
 *	|-------------------------------|-/
 */

/*
 * Constants defining a stack frame.
 */
#define	WINDOWSIZE	(16*4)		/* size of window save area */
#define	ARGPUSHSIZE	(6*4)		/* size of arg dump area */
#define	ARGPUSH		(WINDOWSIZE+4)	/* arg dump area offset */
#define	MINFRAME	(WINDOWSIZE+ARGPUSHSIZE+4) /* min frame */

#define	STACK_GROWTH_DOWN /* stacks grow from high to low addresses */

/*
 * Stack alignment macros.
 */
#define	STACK_ALIGN	8
#define	SA(X)	(((X)+(STACK_ALIGN-1)) & ~(STACK_ALIGN-1))

#ifdef	__sparcv9
/*
 * A 64b stack frame looks like:
 *
 *      |				|
 *	|-------------------------------|
 *	|  Locals, temps, saved floats	|
 *	|-------------------------------|
 *	|  outgoing parameters past 6	|
 *	|-------------------------------|-\
 *	|  One extra xword per frame	| |
 *	|-------------------------------|  > minimum stack frame
 *	|  16 xwords to save IN and	| |
 *      |  LOCAL register on overflow	| |
 *	|-------------------------------|-/-\
 *      |				|   |
 *      |				|    > v9 abi bias
 *      |				|   |
 * %sp->|-------------------------------|---/
 */

/*
 * Constants defining a stack frame.
 */
#define	WINDOWSIZE64	(16*8)		/* size of window save area */
#define	MINFRAME64	(WINDOWSIZE64+8)	/* min frame */
#define	V9BIAS64	(2048-1)	/* v9 abi stack bias */

#define	STACK_ALIGN64	16
#define	SA64(X)		(((X)+(STACK_ALIGN64-1)) & ~(STACK_ALIGN64-1))
#endif

#if defined(_KERNEL) && !defined(_ASM)

void flush_windows(void);
void flush_user_windows(void);
void flush_user_windows_to_stack(void);
void trash_user_windows(void);
void traceback(caddr_t);
void tracedump(void);

#endif	/* defined(_KERNEL) && !defined(_ASM) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_STACK_H */
