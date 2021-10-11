/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)cpu2name.c	1.3	94/12/19 SMI"

#include <sys/cpu.h>
#include <sys/platnames.h>

/*
 * Historical cruft for sun4c and early sun4m machines.
 */
struct cputype2name _cputype2name_tbl[] = {
	/* cputype	mfg-name	impl-arch-name */
	{ CPU_SUN4C_20,	"Sun 4/20",	"SUNW,Sun_4_20" },
	{ CPU_SUN4C_25, "Sun 4/25",	"SUNW,Sun_4_25" },
	{ CPU_SUN4C_40, "Sun 4/40",	"SUNW,Sun_4_40" },
	{ CPU_SUN4C_50, "Sun 4/50",	"SUNW,Sun_4_50" },
	{ CPU_SUN4C_60, "Sun 4/60",	"SUNW,Sun_4_60" },
	{ CPU_SUN4C_65, "Sun 4/65",	"SUNW,Sun_4_65" },
	{ CPU_SUN4C_75, "Sun 4/75",	"SUNW,Sun_4_75" },
	{ CPU_SUN4M_60, "Sun 4/600",	"SUNW,Sun_4_600" },
	{ CPU_NONE,	(char *)0,	(char *)0 }
};
