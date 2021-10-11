!	.data
!	.asciz	"@(#)start_float.s 1.4 92/01/29 SMI"

!	Copyright (c) 1989 by Sun Microsystems, Inc.

	.file	"start_float.s"

#include <SYS.h>

	ENTRY(.start_float)
	retl
	nop				! [internal]

	SET_SIZE(.start_float)
