/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)entry.c	1.12	94/12/15 SMI"

/* LINTLIBRARY */

#include	<stdio.h>
#include	<memory.h>
#include	"debug.h"
#include	"_libld.h"


/*
 * The loader uses a `segment descriptor' list to describe the output
 * segments it can potentially create.   Additional segments may be added
 * using a map file.
 */
static const Sg_desc sg_desc[] = {
	{{PT_PHDR, 0, 0, 0, 0, 0, PF_R + PF_X, 0},
		"phdr", 0, 0, {NULL, NULL}, {NULL, NULL},
		(FLG_SG_TYPE | FLG_SG_FLAGS), NULL},
	{{PT_INTERP, 0, 0, 0, 0, 0, PF_R, 0},
		"interp", 0, 0, {NULL, NULL}, {NULL, NULL},
		(FLG_SG_TYPE | FLG_SG_FLAGS), NULL},
	{{PT_LOAD, 0, 0, 0, 0, 0, PF_R + PF_X, 0},
		"text", 0, 0, {NULL, NULL}, {NULL, NULL},
		(FLG_SG_TYPE | FLG_SG_FLAGS), NULL},
	{{PT_LOAD, 0, 0, 0, 0, 0, PF_R + PF_W + PF_X, 0},
		"data", 0, 0, {NULL, NULL}, {NULL, NULL},
		(FLG_SG_TYPE | FLG_SG_FLAGS), NULL},
	{{PT_DYNAMIC, 0, 0, 0, 0, 0, PF_R + PF_W + PF_X, 0},
		"dynamic", 0, 0, {NULL, NULL}, {NULL, NULL},
		(FLG_SG_TYPE | FLG_SG_FLAGS), NULL},
	{{PT_NOTE, 0, 0, 0, 0, 0, 0, 0},
		"note", 0, 0, {NULL, NULL}, {NULL, NULL},
		FLG_SG_TYPE, NULL},
	{{PT_NULL, 0, 0, 0, 0, 0, 0, 0},
		"", 0, 0, {NULL, NULL}, {NULL, NULL},
		FLG_SG_TYPE, NULL}
};

/*
 * The input processing of the loader involves matching the sections of its
 * input files to an `entrance descriptor definition'.  The entrance criteria
 * is different for either a static or dynamic linkage, and may even be
 * modified further using a map file.  Each entrance criteria is associated
 * with a segment descriptor, thus a mapping of input sections to output
 * segments is maintained.
 */
static const Ent_desc	stat_ent_desc[] = {
	{{NULL, NULL}, NULL, SHT_PROGBITS,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC,
		(Sg_desc *)LD_TEXT, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_STRTAB,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC,
		(Sg_desc *)LD_TEXT, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_PROGBITS,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC + SHF_WRITE,
		(Sg_desc *)LD_DATA, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_NOBITS,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC + SHF_WRITE,
		(Sg_desc *)LD_DATA, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_NOTE, 0, 0,
		(Sg_desc *)LD_NOTE, 0, FALSE},
	{{NULL, NULL}, NULL, 0, 0, 0,
		(Sg_desc *)LD_EXTRA, 0, FALSE}
};
static const int	stat_ent_size = sizeof (stat_ent_desc);


/*
 * Initialize new entrance and segment descriptors and add them as lists to
 * the output file descriptor.
 */
int
ent_setup(Ofl_desc * ofl)
{
	Ent_desc *	ent_desc, * enp;
	Sg_desc *	sgp;
	int		ent_size, size;

	/*
	 * Determine which entrance descriptor list is applicable for the
	 * required output file.
	 */
	if (ofl->ofl_flags & FLG_OF_DYNAMIC) {
		ent_desc = (Ent_desc *)dyn_ent_desc;
		ent_size = dyn_ent_size;
	} else {
		ent_desc = (Ent_desc *)stat_ent_desc;
		ent_size = stat_ent_size;
	}

	/*
	 * Allocate and initialize writable copies of both the entrance and
	 * segment descriptors.
	 */
	if ((sgp = (Sg_desc *)malloc(sizeof (sg_desc))) == 0)
		return (S_ERROR);
	(void) memcpy(sgp, sg_desc, sizeof (sg_desc));
	if ((enp = (Ent_desc *)malloc(ent_size)) == 0)
		return (S_ERROR);
	(void) memcpy(enp, ent_desc, ent_size);

	/*
	 * Traverse the new entrance descriptor list converting the segment
	 * pointer entries to the absolute address within the new segment
	 * descriptor list.  Add each entrance descriptor to the output file
	 * list.
	 */
	for (size = 0; size < ent_size; size += sizeof (Ent_desc)) {
		enp->ec_segment = &sgp[(int)enp->ec_segment];
		if ((list_append(&ofl->ofl_ents, enp)) == 0)
			return (S_ERROR);
		enp++;
	}

	/*
	 * Traverse the new segment descriptor list adding each entry to the
	 * segment descriptor list.  For each loadable segment initialize
	 * a default alignment (ld(1) and ld.so.1 initialize this differently).
	 */
	for (size = 0; size < sizeof (sg_desc); size += sizeof (Sg_desc)) {
		Phdr *	phdr = &(sgp->sg_phdr);

		if ((list_append(&ofl->ofl_segs, sgp)) == 0)
			return (S_ERROR);
		if (phdr->p_type == PT_LOAD)
			phdr->p_align = ofl->ofl_segalign;

		sgp++;
	}
	return (1);
}
