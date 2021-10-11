#ident	"@(#)vfs.c	1.7	95/03/02 SMI"		/* SVr4.0 1.4.6.1 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * This file contains code for the crash functions:  vfs (mount).
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/var.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <sys/vnode.h>
#include <sys/elf.h>
#include "crash.h"

Elf32_Sym *Vfs;			/* namelist symbol */
extern Elf32_Sym *Vfssw;	/* declared in vfssw.c */

/* get arguments for vfs (mount) function */
int
getvfsarg()
{
	int full = 0;
	int phys = 0;
	int c;
	long vfsp;

	if (!Vfs)
		if (!(Vfs = symsrch("rootvfs")))
			error("vfs list not found\n");
	if (!Vfssw)
		if ((Vfssw = symsrch("vfssw")) == NULL)
			error("vfssw not found in symbol table\n");

	optind = 1;
	while ((c = getopt(argcnt, args, "fpw:")) != EOF) {
		switch (c) {
			case 'f' :	full = 1;
					break;
			case 'w' :	redirect();
					break;
			case 'p' :	phys = 1;
					break;
			default  :	longjmp(syn, 0);
		}
	}

	fprintf(fp, " FSTYP  BSZ  MAJ/MIN      FSID    VNCOVERED   PDATA");
	fprintf(fp, "      BCOUNT  FLAGS\n");
	if (args[optind]) {
		do {
			vfsp = strcon(args[optind], 'h');
			if (vfsp == -1)
				continue;
			else
				prvfs(full, phys, vfsp);
			vfsp = -1;
		} while (args[++optind]);
	} else {
		readmem(Vfs->st_value, 1, -1, (char *)&vfsp, sizeof (vfsp),
			"head of vfs list");
		while (vfsp)
			vfsp = prvfs(full, phys, vfsp);
	}
	return (0);
}

/* print vfs list */
prvfs(full, phys, addr)
int full, phys;
long addr;
{
	struct vfs vfsbuf;
	char fsname[FSTYPSZ];
	extern long lseek();
	struct vfssw vfsswbuf;

	readbuf(addr, 0, phys, -1, (char *)&vfsbuf, sizeof (vfsbuf),
		"vfs list");
	readmem((Vfssw->st_value+vfsbuf.vfs_fstype*sizeof (vfsswbuf)),
		1, -1, (char *)&vfsswbuf, sizeof (vfsswbuf),
		"file system switch table");
	readmem((long)vfsswbuf.vsw_name, 1, -1, fsname, sizeof (fsname),
	    "fs_name");

	fprintf(fp, "%6s %4u",
		fsname,
		vfsbuf.vfs_bsize);
	fprintf(fp, " %4u,%-5u",
		getemajor(vfsbuf.vfs_dev),
		geteminor(vfsbuf.vfs_dev));
	fprintf(fp, " %8x   %8x  %8x %10d",
		vfsbuf.vfs_fsid.val[0],
		vfsbuf.vfs_vnodecovered,
		vfsbuf.vfs_data,
		vfsbuf.vfs_bcount);
	fprintf(fp, "  %s%s%s%s%s",
		(vfsbuf.vfs_flag & VFS_RDONLY) ? " rd" : "",
		(vfsbuf.vfs_flag & VFS_NOSUID) ? " nosu" : "",
		(vfsbuf.vfs_flag & VFS_REMOUNT) ? " remnt" : "",
		(vfsbuf.vfs_flag & VFS_NOTRUNC) ? " notr" : "",
		(vfsbuf.vfs_flag & VFS_UNLINKABLE) ? " nolnk" : "");
	fprintf(fp, "\n");
	if (full) {
		fprintf(fp, "\tvfs_next: %x   vfs_ops: %x\n",
		vfsbuf.vfs_next,
		vfsbuf.vfs_op);
		fprintf(fp, "\tvfs_list: %x   vfs_nsubmounts: %d\n",
		vfsbuf.vfs_list,
		vfsbuf.vfs_nsubmounts);
	}
	return ((long)vfsbuf.vfs_next);
}
