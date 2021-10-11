/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_ELFTYPES_H
#define	_SYS_ELFTYPES_H

#pragma ident	"@(#)elftypes.h	1.10	93/02/04 SMI"	/* SVr4.0 1.1	*/

#ifdef	__cplusplus
extern "C" {
#endif

typedef unsigned long	Elf32_Addr;
typedef unsigned short	Elf32_Half;
typedef unsigned long	Elf32_Off;
typedef long		Elf32_Sword;
typedef unsigned long	Elf32_Word;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ELFTYPES_H */
