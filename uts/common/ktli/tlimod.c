/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ident  "@(#)tlimod.c 1.4     94/04/14 SMI"

#include <sys/types.h>
#include <sys/sunddi.h>
#include <sys/errno.h>
#include <sys/modctl.h>

static struct modlmisc modlmisc = {
	&mod_miscops, "TLI misc module"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

_init()
{

	return (mod_install(&modlinkage));
}

_fini()
{

	return (EBUSY);
}

_info(modinfop)
	struct modinfo *modinfop;
{

	return (mod_info(&modlinkage, modinfop));
}
