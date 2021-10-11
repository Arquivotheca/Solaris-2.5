/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)mfgname.c	1.4	94/12/19 SMI"

#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/promif.h>

#include <sys/platnames.h>

#define	MAXNMLEN	80		/* # of chars in an impl-arch name */

/*
 * Return the manufacturer name for this platform.
 *
 * This is exported (solely) as the rootnode name property in
 * the kernel's devinfo tree via the 'mfg-name' boot property.
 * So it's only used by boot, not the boot blocks.
 */
char *
get_mfg_name(void)
{
	dnode_t n;
	struct cputype2name *p;
	short cputype;
	int len;

	static char mfgname[MAXNMLEN];

	if ((n = prom_rootnode()) != OBP_NONODE &&
	    (len = prom_getproplen(n, "name")) > 0 && len < MAXNMLEN) {
		(void) prom_getprop(n, "name", mfgname);
		mfgname[len] = '\0'; /* broken clones don't terminate name */
		return (mfgname);
	}

	/*
	 * Not an OpenBoot PROM, or no 'name' property?
	 * Fall back to the cputype junk.
	 *
	 * Note:
	 *	With the demise of sun4 and sun4e platforms, this is
	 *	redundant - we'll probably never get here.
	 */
	if ((cputype = _get_cputype()) != CPU_NONE)
		for (p = _cputype2name_tbl; p->cputype != CPU_NONE; p++)
			if (p->cputype == cputype)
				return (p->mfgname);
	return ("Unknown");
}
