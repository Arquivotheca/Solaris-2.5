/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)ident_pcfs.c 1.7	95/02/02 SMI"

#include	<stdio.h>
#include	<fcntl.h>
#include	<rpc/types.h>
#include	<sys/types.h>
#include	<sys/fs/pc_label.h>
#include	<unistd.h>

#include	<rmmount.h>


/*
 * We call it a pcfs file system iff:
 *	The "media type" descriptor in the label == the media type
 *		descriptor that's supposed to be the first byte
 *		of the FAT.
 *	The second byte of the FAT is 0xff.
 *	The third byte of the FAT is 0xff.
 *
 *	Assumptions:
 *
 *	1.	I don't really know how safe this is, but it is
 *	mentioned as a way to tell whether you have a dos disk
 *	in my book (Advanced MSDOS Programming, Microsoft Press).
 *	Actually it calls it an "IBM-compatible disk" but that's
 *	good enough for me.
 *
 * 	2.	The FAT is right after the reserved sector(s), and both
 *	the sector size and number of reserved sectors must be gotten
 *	from the boot sector.
 */
/*ARGSUSED*/
int
ident_fs(int fd, char *rawpath, int *clean, int verbose)
{
	u_char	pc_stuff[PC_SECSIZE * 4];
	uint_t	fat_off;
	int	res = FALSE;


	/*
	 * pcfs is always clean... at least there's no way to tell if
	 * it isn't!
	 */
	*clean = TRUE;

	/* go to start of image */
	if (lseek(fd, 0, SEEK_SET) < 0) {
		perror("pcfs seek");	/* should be able to seek to 0 */
		goto dun;
	}

	/* read the boot sector (plus some) */
	if (read(fd, pc_stuff, sizeof (pc_stuff)) < 0) {
		perror("pcfs read");	/* should be able to read 4 sectors */
		goto dun;
	}

	/* no need to go farther if magic# is wrong */
	if ((*pc_stuff != (uchar_t)DOS_ID1) &&
	    (*pc_stuff != (uchar_t)DOS_ID2a)) {
		goto dun;	/* magic# wrong */
	}

	/* calculate where FAT starts */
	fat_off = ltohs(pc_stuff[PCB_BPSEC]) * ltohs(pc_stuff[PCB_RESSEC]);

	/* if offset is too large we probably have garbage */
	if (fat_off >= sizeof (pc_stuff)) {
		goto dun;	/* FAT offset out of range */
	}

	if ((pc_stuff[PCB_MEDIA] == pc_stuff[fat_off]) &&
	    ((uchar_t)0xff == pc_stuff[fat_off + 1]) &&
	    ((uchar_t)0xff == pc_stuff[fat_off + 2])) {
		res = TRUE;
	}

dun:
	return (res);
}
