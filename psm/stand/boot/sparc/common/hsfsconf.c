/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)hsfsconf.c	1.1	94/08/01 SMI"

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/bootvfs.h>

/*
 *  Function prototypes (Global/Imported)
 */

/* HSFS Support */
extern	struct boot_fs_ops	boot_hsfs_ops;

struct boot_fs_ops *boot_fsw[] = {
	&boot_hsfs_ops,
};

int boot_nfsw = sizeof (boot_fsw) / sizeof (boot_fsw[0]);
static char *fstype = "hsfs";

/*ARGSUSED*/
char *
set_fstype(char *v2path)
{
	set_default_fs(fstype);
	return (fstype);
}
