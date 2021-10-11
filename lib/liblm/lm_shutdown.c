/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All Rights Reserved
 */

#ident	"@(#)lm_shutdown.c	1.2	94/12/16 SMI"		/* SVr4.0 1.3 */

#include <sys/types.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <nfs/lm.h>

int
lm_shutdown()
{
	return (_nfssys(KILL_LOCKMGR, NULL));
}
