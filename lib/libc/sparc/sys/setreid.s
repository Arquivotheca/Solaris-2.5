/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)setreid.s	1.1	95/07/14 SMI"

	.file	"setreid.s"

#include <sys/asm_linkage.h>

#include "SYS.h"

	SYSCALL(setreuid)
	RET
	SET_SIZE(setreuid)

	SYSCALL(setregid)
	RET
	SET_SIZE(setregid)
