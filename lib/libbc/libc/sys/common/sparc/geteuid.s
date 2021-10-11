!
!	"@(#)geteuid.s 1.8 92/01/16"
!       Copyright (c) 1986 by Sun Microsystems, Inc.
!
	.seg	".text"

#include "SYS.h"

	PSEUDO(geteuid,getuid)
	retl			/* euid = geteuid(); */
	mov	%o1, %o0

	SET_SIZE(geteuid)
