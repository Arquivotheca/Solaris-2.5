/*
 * Copyright (c) 1990,1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident  "@(#)klmmod.c 1.13     95/03/17 SMI"

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <nfs/lm.h>

static struct modlmisc modlmisc = {
	&mod_miscops, "lock mgr common module"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

char _depends_on[] = "strmod/rpcmod fs/nfs";

_init()
{
	int retval;

	retval = mod_install(&modlinkage);
	if (retval != 0) {
		return (retval);
	}

	mutex_init(&lm_lck, "lockmgr global lock", MUTEX_DEFAULT, NULL);
	cv_init(&lm_status_cv, "lockmgr status cv", CV_DEFAULT, NULL);
	rw_init(&lm_sysids_lock, "lockmgr sysid lock", RW_DEFAULT, NULL);

	return (0);
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
