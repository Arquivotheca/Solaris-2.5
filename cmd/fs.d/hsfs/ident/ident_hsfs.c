#ifndef lint
#ident  "@(#)ident_hsfs.c 1.4     93/01/25 SMI"
#endif

#include	<stdio.h>
#include	<fcntl.h>
#include	<rpc/types.h>
#include	<sys/types.h>
#include	<sys/fs/hsfs_isospec.h>

#include	<rmmount.h>

/*
 * We call it an hsfs file system iff:
 *	The string "CD001" is present at the second byte of the PVD.
 *
 *	Assumptions:
 */
int
ident_fs(int fd, char *rawpath, int *clean, int verbose)
{
	u_char	pvd[ISO_SECTOR_SIZE];
	char	volid[ISO_VOL_ID_STRLEN+1];

	/* hsfs is always clean */
	*clean = TRUE;

	if (lseek(fd, ISO_VOLDESC_SEC * ISO_SECTOR_SIZE, SEEK_SET) < 0) {
		perror("hsfs seek");
		return (FALSE);
	}

	if (read(fd, pvd, ISO_SECTOR_SIZE) < 0) {
		perror("hsfs read");
		return (FALSE);
	}

	if (!strncmp(ISO_std_id(pvd), ISO_ID_STRING, ISO_ID_STRLEN)) {
		memcpy(volid, ISO_vol_id(pvd), ISO_VOL_ID_STRLEN);
		volid[ISO_VOL_ID_STRLEN] = '\0';
		dprintf("volume id is %s\n", volid);
		return (TRUE);
	}
	return (FALSE);
}

