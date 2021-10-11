!
!	"@(#)syscall.s 1.5 88/02/08"
!       Copyright (c) 1986 by Sun Microsystems, Inc.
!
!	.seg	"text"
	
	.file	"_syscall.s"

#include "SYS.h"

#define SYS_syscall 0			/* SYS_indir */

	BSDSYSCALL(syscall)
	RET

	SET_SIZE(_syscall)
