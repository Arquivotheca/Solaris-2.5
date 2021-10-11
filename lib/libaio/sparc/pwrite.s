/*
 * int pwrite (int fildes, char *buf, int bufsz, off_t offset)
 */

	.file	"pwrite.s"

#include <sys/asm_linkage.h>

#include "SYS.h"

	ENTRY(aio_pwrite)
	SYSTRAP(pwrite)
	bcc	1f;
	nop;
	st	%o0, [%o4]
	mov	-1, %o0
1:
	RET
	SET_SIZE(aio_pwrite)
