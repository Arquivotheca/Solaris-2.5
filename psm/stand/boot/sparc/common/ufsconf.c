/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)ufsconf.c	1.1	94/08/01 SMI"

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/bootvfs.h>

/*
 *  Function prototypes (Global/Imported)
 */

/* UFS Support */
extern	struct boot_fs_ops	boot_ufs_ops;

struct boot_fs_ops *boot_fsw[] = {
	&boot_ufs_ops,
};

int boot_nfsw = sizeof (boot_fsw) / sizeof (boot_fsw[0]);
static char *fstype = "ufs";

/*ARGSUSED*/
char *
set_fstype(char *v2path)
{
	set_default_fs(fstype);
	return (fstype);
}
