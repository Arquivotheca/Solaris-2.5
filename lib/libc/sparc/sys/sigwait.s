#ident "@(#)sigwait.s 1.8 93/03/10"

	.file "sigwait.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(sigwait,function)

#include	"SYS.h"

#define NULLP	0

	ENTRY(sigwait)
	mov	NULLP, %o1
	mov	NULLP, %o2
	SYSTRAP(sigtimedwait)
	SYSCERROR
	RET

	SET_SIZE(sigwait)
