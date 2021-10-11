/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)caller.s	1.3	92/08/31 SMI"

/*
 * Return the pc of the calling routine.
 */
#if	defined(lint)

/* ARGSUSED */
int
caller()
{
	int pc = 0;
	return (pc);
}

#else

#include	<sys/asm_linkage.h>

	.file	"caller.s"

	ENTRY(caller)
	retl
	mov	%i7, %o0

	SET_SIZE(caller)
#endif
