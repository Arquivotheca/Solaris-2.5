/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)module.s	1.1	95/07/18 SMI"

#include <sys/asm_linkage.h>
#include <sys/mmu.h>

#if defined(lint)

int
getmcr(void)
{}

#else

	ENTRY(getmcr)
	retl
	lda	[%g0]ASI_MOD, %o0	! module control/status register
	SET_SIZE(getmcr)
#endif	/* lint */
