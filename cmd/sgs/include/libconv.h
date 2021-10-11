/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)libconv.h	1.6	95/06/05 SMI"

/*
 * Global include file for conversion library.
 */
#ifndef	LIBCONV_DOT_H
#define	LIBCONV_DOT_H

#include	<stdlib.h>
#include	<libelf.h>
#include	<dlfcn.h>
#include	"libld.h"
#include	"sgs.h"
#include	"machdep.h"


/*
 * Flags for reloc_entry->re_flags
 */
#define	FLG_RE_NOTREL		0x00
#define	FLG_RE_GOTREL		0x01
#define	FLG_RE_PCREL		0x02
#define	FLG_RE_RELPC		0x04
#define	FLG_RE_PLTREL		0x08
#define	FLG_RE_VERIFY		0x10		/* verify value fits */
#define	FLG_RE_UNALIGN		0x20		/* offset is not aligned */
#define	FLG_RE_WDISP16		0x40		/* funky sparc DISP16 rel */
#define	FLG_RE_SIGN		0x80		/* value is signed */


/*
 * Macros for testing relocation table flags
 */
#define	IS_PLT(X)		((reloc_table[(X)].re_flags & \
					FLG_RE_PLTREL) != 0)
#define	IS_GOT_RELATIVE(X)	((reloc_table[(X)].re_flags & \
					FLG_RE_GOTREL) != 0)
#define	IS_GOT_PC(X)		((reloc_table[(X)].re_flags & \
					FLG_RE_RELPC) != 0)
#define	IS_PC_RELATIVE(X)	((reloc_table[(X)].re_flags & \
					FLG_RE_PCREL) != 0)

/*
 * relocation table
 */
extern	reloc_entry	reloc_table[];

/*
 * Functions
 */


extern	const char *	conv_d_type_str(Elf_Type);
extern	const char *	conv_deftag_str(Symref);
extern	const char *	conv_dlmode_str(int);
extern	const char *	conv_dyntag_str(Sword);
extern	const char *	conv_eclass_str(Byte);
extern	const char *	conv_edata_str(Byte);
extern	const char *	conv_emach_str(Half);
extern	const char *	conv_ever_str(Word);
extern	const char *	conv_etype_str(Half);
extern	const char *	conv_info_bind_str(unsigned char);
extern	const char *	conv_info_type_str(unsigned char);
extern	const char *	conv_phdrflg_str(unsigned int);
extern	const char *	conv_phdrtyp_str(unsigned int);
extern	const char *	conv_reloc_type_str(Word);
extern	const char *	conv_secflg_str(unsigned int);
extern	const char *	conv_sectyp_str(unsigned int);
extern	const char *	conv_segaflg_str(unsigned int);
extern	const char *	conv_shndx_str(Half);
extern	const char *	conv_verflg_str(Half);
extern	int		do_reloc(unsigned char, unsigned char *, Word *,
				const char *, const char *);

#endif
