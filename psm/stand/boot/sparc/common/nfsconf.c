/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma ident "@(#)nfsconf.c	1.3	94/11/21 SMI"

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/bootvfs.h>

/*
 *  Function prototypes (Global/Imported)
 */

/*
 * Can we set this to 8K now that we don't have to worry about old machines
 * with ie interfaces that choke on larger packets?
 */
int nfs_readsize = 1024;

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
