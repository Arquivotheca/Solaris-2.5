/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)diskread.c	1.1	94/08/01 SMI"

#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_inode.h>
#include <sys/sysmacros.h>
#include <sys/promif.h>
#include <sys/filep.h>

#ifdef sparc
static	char	prom_dev_type = -1;
#else
static	char	prom_dev_type = 0;
#endif

/*
 * Exported Functions
 */
extern	int	diskread(fileid_t *filep);

/*
 *	The various flavors of PROM make this grotesque.
 */
int
diskread(fileid_t *filep)
{
	int err;
	devid_t	*devp;

#ifdef sparc
	if (prom_dev_type == -1) {
		if (prom_getversion() == 0)
			prom_dev_type = BLOCK;
		else
			prom_dev_type = 0;
	}
#endif

	devp = filep->fi_devp;

	if ((err = prom_seek(devp->di_dcookie, 0,
	    filep->fi_blocknum*DEV_BSIZE)) == -1) {
#ifdef sparc
		if (prom_getversion() > 0) {
			printf("Seek error at block %x\n", filep->fi_blocknum);
			return (-1);
		}
		/*
		 * V0 proms will return -1 here.  That's ok since
		 * they don't really support seeks on block devices.
		 */
#endif
	}
	if ((err = prom_read(devp->di_dcookie, filep->fi_memp,
	    filep->fi_count, filep->fi_blocknum, prom_dev_type)) !=
	    filep->fi_count) {
#ifdef sparc
		if (prom_getversion() == 0) {
			if (err != filep->fi_count/DEV_BSIZE) {
				printf("Short read.  0x%x chars read\n",
					filep->fi_count);
				return (-1);
			}
		} else {
#endif
			printf("Short read.  0x%x chars read\n",
				filep->fi_count);
				return (-1);
#ifdef sparc
		}
#endif
	}
	/*
	 * The cache is not always consistent with I/O when going
	 * through the prom on V0 OBP's.  Flush it to be safe.
	 */
#ifdef sparc
	if (prom_getversion() == 0)
		sunm_vac_flush(filep->fi_memp, filep->fi_count);
#endif

	return (0);
}
