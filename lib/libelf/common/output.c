/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)output.c	1.9	94/08/01 SMI" 	/* SVr4.0 1.3	*/

/*LINTLIBRARY*/

#include "syn.h"

#ifdef MMAP_IS_AVAIL
#	include <sys/mman.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <libelf.h>
#include "decl.h"
#include "error.h"


/* File output
 *	These functions write output files.
 *	On SVR4 and newer systems use mmap(2).  On older systems (or on
 *	file systems that don't support mmap), use write(2).
 */


/*ARGSUSED*/
char *
_elf_outmap(fd, sz, pflag)
	int		fd;
	size_t		sz;
	unsigned int	*pflag;
{
	char		*p;

	*pflag = 0;
#ifdef MMAP_IS_AVAIL
	/*
	 *	Must be running svr4 or later to use mmap.
	 *	Set file length to allow mapping
	 *
	 *	Note: Some NFS implimentations do not provide from enlarging a
	 *	file via ftruncate(), thus this may fail with ENOSUP.  In this
	 *	case the fallthrough to the malloc() mechanism will occur.
	 */

	if (ftruncate(fd, (off_t)sz) == 0
	&& (p = mmap((char *)0, sz, PROT_READ+PROT_WRITE,
			MAP_SHARED, fd, (off_t)0)) != (char *)-1)
	{
		*pflag = 1;
		return p;
	}

	/*	If mmap fails, try malloc.  Some file systems don't mmap
	 */
#endif
	if ((p = (char *)malloc(sz)) == 0)
		_elf_err = EMEM_OUT;
	return p;
}


/*ARGSUSED*/
size_t
_elf_outsync(fd, p, sz, flag)
	int		fd;
	char		*p;
	size_t		sz;
	unsigned	flag;
{
#ifdef MMAP_IS_AVAIL
	if (flag != 0)
	{
		fd = msync(p, sz, MS_SYNC);
		(void)munmap(p, sz);
		if (fd == 0)
			return sz;
		_elf_err = EIO_SYNC;
		return 0;
	}
#endif
	if (lseek(fd, 0L, 0) == 0) {
		if (write(fd, p, sz) == sz) {
			(void)free(p);
			return sz;
		}
	}
	_elf_err = EIO_WRITE;
	return 0;
}
