/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef _LINK_H
#define	_LINK_H

#pragma ident	"@(#)link.h	1.16	94/07/14 SMI"	/* SVr4.0 1.9	*/

#include <sys/link.h>

#ifndef _ASM
#include <libelf.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM
/*
 * ld support library calls
 */
#ifdef __STDC__
extern void	ld_start(const char *, const Elf32_Half, const char *);
extern void	ld_atexit(int);
extern void	ld_file(const char *, const Elf_Kind, int, Elf *);
extern void	ld_section(const char *, Elf32_Shdr *, Elf32_Word,
			Elf_Data *, Elf *);
#else
extern void	ld_start();
extern void	ld_atexit();
extern void	ld_file();
extern void	ld_section();
#endif

/*
 * flags passed to ld support calls
 */
#define	LD_SUP_DERIVED		0x1	/* derived filename */
#define	LD_SUP_INHERITED	0x2	/* file inherited from .so DT_NEEDED */
#define	LD_SUP_EXTRACTED	0x4	/* file extracted from archive */
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _LINK_H */
