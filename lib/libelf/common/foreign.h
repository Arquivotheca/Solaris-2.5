/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)foreign.h	1.4	92/07/17 SMI" 
	/* SVr4.0 1.2	*/


/*	This file declares functions for handling foreign (non-ELF)
 *	file formats.
 */

#if	COFF_FILE_CONVERSION
int		_elf_coff _((Elf *));
#endif

extern int	(*const _elf_foreign[]) _((Elf *));
