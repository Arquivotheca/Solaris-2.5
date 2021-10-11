/* 
 * int pread (int fildes, ioreq_t *req, int nreq)
 */

	.file	"pread.s"

#include <sys/asm_linkage.h>

#include "SYS.h"

	ENTRY(aio_pread)
	SYSTRAP(pread)
	bcc	1f;
	nop;
	st	%o0, [%o4]
	mov	-1, %o0
1:
	RET
	SET_SIZE(aio_pread)
