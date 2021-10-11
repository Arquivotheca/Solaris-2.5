/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)mmap.s	1.7	92/07/14 SMI"	/* SVr4.0 1.1	*/

/* C library -- mmap	*/
/* caddr_t mmap(caddr_t addr, size_t len, int prot,
	int flags, int fd, off_t off)				*/

	.file	"mmap.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(mmap,function)

#include "SYS.h"
#include <sys/mman.h>		/* Need _MAP_NEW definition	*/

/*
 * Note that the code depends upon the _MAP_NEW flag being in the top bits
 */

#define FLAGS   %o3

	ENTRY(mmap)
	sethi   %hi(_MAP_NEW), %g1
	or      %g1, FLAGS, FLAGS
	SYSTRAP(mmap)
	SYSCERROR
	RET

	SET_SIZE(mmap)
