/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)dump.h	6.3	94/08/31 SMI"

#include	<sys/elf.h>

#define DATESIZE 60

typedef struct scntab {
	char *		scn_name;
	Elf32_Shdr *	p_shdr;
	Elf_Scn *	p_sd;
} SCNTAB;
