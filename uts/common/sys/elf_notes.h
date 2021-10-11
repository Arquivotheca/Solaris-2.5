/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef _SYS_ELF_NOTES_H
#define	_SYS_ELF_NOTES_H

#pragma ident	"@(#)elf_notes.h	1.3	95/02/09 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Sun defined names for elf note sections.
 */
#define	ELF_NOTE_SOLARIS		"SUNW Solaris"
#define	ELF_NOTE_PACKHINT		"SUNW packing"
#define	ELF_NOTE_PACKSIZE		"SUNW pack size"

/*
 * Describes the desired pagesize of elf PT_LOAD segments.
 * Descriptor is 1 word in length, and contains the desired pagesize.
 */
#define	ELF_NOTE_PAGESIZE_HINT		1

/*
 * Describes a type for a given note
 *
 */
#define	ELF_NOTE_MODULE_PACKING	2
#define	ELF_NOTE_MODULE_SIZE	3

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ELF_NOTES_H */
