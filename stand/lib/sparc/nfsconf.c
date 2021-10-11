/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)nfsconf.c	1.1	94/08/01 SMI"

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/bootvfs.h>

/*
 *  Function prototypes (Global/Imported)
 */

/* NFS Support */
extern	struct boot_fs_ops	boot_nfs_ops;

struct boot_fs_ops *boot_fsw[] = {
	&boot_nfs_ops,
};

int boot_nfsw = sizeof (boot_fsw) / sizeof (boot_fsw[0]);
static char *fstype = "nfs";

/*ARGSUSED*/
char *
set_fstype(char *v2path)
{
	set_default_fs(fstype);
	return (fstype);
}
