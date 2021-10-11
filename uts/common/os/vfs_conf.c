
#ident	"@(#)vfs_conf.c	1.40	94/03/22 SMI"	/* SunOS-4.1 1.16	*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/vfs.h>
#include <sys/t_lock.h>

extern	struct vfsops vfs_strayops;	/* XXX move here from vfs.c ? */

extern int swapinit(struct vfssw *vswp, int fstype);
extern struct vfsops swap_vfsops;

/*
 * WARNING: THE POSITIONS OF FILESYSTEM TYPES IN THIS TABLE SHOULD NOT
 * BE CHANGED. These positions are used in generating fsids and fhandles.
 * Thus, changing positions will cause a server to change the fhandle it
 * gives out for a file.
 *
 * XXX - is this still true?  AT&T's code doesn't appear to try to make
 * sure it is so.
 */

struct vfssw vfssw[] = {
	"BADVFS", NULL, &vfs_strayops, 0, 	/* invalid */
	"specfs", NULL, NULL, 0,		/* SPECFS */
	"ufs", NULL, NULL, 0,			/* UFS */
	"fifofs", NULL, NULL, 0,		/* FIFOFS */
	"namefs", NULL, NULL, 0,		/* NAMEFS */
	"proc", NULL, NULL, 0,			/* PROCFS */
	"s5fs", NULL, NULL, 0,			/* S5FS */
	"nfs", NULL, NULL, 0,			/* NFS Version 2 */
	"", NULL, NULL, 0,			/* was RFS before */
	"hsfs", NULL, NULL, 0,			/* HSFS */
	"lofs", NULL, NULL, 0,			/* LOFS */
	"tmpfs", NULL, NULL, 0,			/* TMPFS */
	"fd", NULL, NULL, 0,			/* FDFS */
	"pcfs", NULL, NULL, 0,			/* PCFS */
	"swapfs", swapinit, NULL, 0,		/* SWAPFS */
	"", NULL, NULL, 0,			/* reserved for loadable fs */
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0,
	"", NULL, NULL, 0
};

int nfstype = (sizeof (vfssw) / sizeof (vfssw[0]));
