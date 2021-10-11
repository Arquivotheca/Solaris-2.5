/*
 * Copyright (c) 1992, by Sun Microsystems Inc.
 */

#pragma	ident	"@(#)inst_sync.s 1.1     92/05/06 SMI"

/*
 * System call:
 *		int inst_sync(char *pathname, int flags);
 */

#include <sys/asm_linkage.h>
#include <sys/trap.h>
#include <sys/syscall.h>

	.file	"inst_sync.s"

	.global	_cerror
#ifdef notdef
	ANSI_PRAGMA_WEAK(inst_sync,function)
#endif
	ENTRY(inst_sync)

	mov	SYS_inst_sync, %g1
	t	ST_SYSCALL
	bcs	_cerror
	nop
	retl
	nop

	SET_SIZE(inst_sync)
