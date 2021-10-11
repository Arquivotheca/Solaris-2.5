
#ifndef lint
#ident	"@(#)checkmount.c	1.6	95/01/16 SMI"
#endif	lint

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

/*
 * This file contians miscellaneous routines.
 */
#include "global.h"

#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/autoconf.h>

#include <signal.h>
#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <ctype.h>
#include "misc.h"
#include "checkmount.h"



/*
 * This routine checks to see if there are mounted partitions overlapping
 * a given portion of a disk.  If the start parameter is < 0, it means
 * that the entire disk should be checked.
 */
int
checkmount(start, end)
	daddr_t	start, end;
{
	FILE		*fp;
	int		mfd;
	int		found = 0;
	struct dk_cinfo	dkinfo;
	struct dk_map	*map;
	int		part;
	struct mnttab	mnt_record;
	struct mnttab	*mp = &mnt_record;
	struct stat	stbuf;
	char		raw_device[MAXPATHLEN];


	/*
	 * If we are only checking part of the disk, the disk must
	 * have a partition map to check against.  If it doesn't,
	 * we hope for the best.
	 */
	if (cur_parts == NULL && start >= 0)
		return (0);
	/*
	 * Lock out interrupts because of the mntent protocol.
	 */
	enter_critical();
	/*
	 * Open the mount table.
	 */
	fp = fopen(MNTTAB, "r");
	if (fp == NULL) {
		err_print("Unable to open mount table.\n");
		fullabort();
	}
	/*
	 * Loop through the mount table until we run out of entries.
	 */
	while ((getmntent(fp, mp)) != -1) {
		/*
		 * Map the block device name to the raw device name.
		 * If it doesn't appear to be a device name, skip it.
		 */
		if (!match_substr(mp->mnt_special, "/dev/"))
			continue;
		(void) strcpy(raw_device, "/dev/r");
		(void) strcat(raw_device, mp->mnt_special + strlen("/dev/"));
		/*
		 * Determine if this appears to be a disk device.
		 * First attempt to open the device.  If if fails, skip it.
		 */
		if ((mfd = open(raw_device, O_RDWR | O_NDELAY)) < 0) {
			continue;
		}
		/*
		 * Must be a character device
		 */
		if (fstat(mfd, &stbuf) == -1 || !S_ISCHR(stbuf.st_mode)) {
			(void) close(mfd);
			continue;
		}
		/*
		 * Attempt to read the configuration info on the disk.
		 */
		if (ioctl(mfd, DKIOCINFO, &dkinfo) < 0) {
			(void) close(mfd);
			continue;
		}
		/*
		 * Finished with the opened device
		 */
		(void) close(mfd);
		/*
		 * If it's not the disk we're interested in, it doesn't apply.
		 */
		if (cur_disk->disk_dkinfo.dki_ctype != dkinfo.dki_ctype ||
			cur_disk->disk_dkinfo.dki_cnum != dkinfo.dki_cnum ||
			cur_disk->disk_dkinfo.dki_unit != dkinfo.dki_unit ||
			strcmp(cur_disk->disk_dkinfo.dki_dname,
					dkinfo.dki_dname) != 0) {
				continue;
		}
		/*
		 * It's a mount on the disk we're checking.  If we are
		 * checking whole disk, then we found trouble.  We can
		 * quit searching.
		 */
		if (start < 0) {
			found = -1;
			break;
		}
		/*
		 * Extract the partition that is mounted.
		 */
		part = PARTITION(stbuf.st_dev);
		/*
		 * If the partition overlaps the zone we're checking,
		 * then we found trouble.  We can quit searching.
		 */
		map = &cur_parts->pinfo_map[part];
		if ((start >= (int)(map->dkl_cylno * spc() + map->dkl_nblk)) ||
				(end < (int)(map->dkl_cylno * spc()))) {

			continue;
		}
		found = -1;
		break;
	}
	/*
	 * Close down the mount table.
	 */
	(void) fclose(fp);
	/*
	 * If we found trouble and we're running from a command file,
	 * quit before doing something we really regret.
	 */
	if (found && option_f) {
		err_print("Operation on mounted disks must be interactive.\n");
		exit_critical();
		cmdabort(SIGINT);
	}
	exit_critical();
	/*
	 * Return the result.
	 */
	return (found);
}

/*
 * Check the new label with the existing label on the disk,
 * to make sure that any mounted partitions are not being
 * affected by writing the new label.
 */
int
check_label_with_mount()
{
	FILE			*fp;
	int			mfd;
	int			found = 0;
	struct dk_cinfo		dkinfo;
	struct mnttab		mnt_record;
	struct mnttab		*mp = &mnt_record;
	struct stat		stbuf;
	char			raw_device[MAXPATHLEN];
	struct dk_map		*n, *o;
	unsigned int		bm_mounted = 0;
	struct dk_allmap	old_map;
	int			part;
	int			i;

	/*
	 * If we are only checking part of the disk, the disk must
	 * have a partition map to check against.  If it doesn't,
	 * we hope for the best.
	 */
	if (cur_parts == NULL)
		return (0);	/* Will be checked later */
	/*
	 * Lock out interrupts because of the mntent protocol.
	 */
	enter_critical();
	/*
	 * Open the mount table.
	 */
	fp = fopen(MNTTAB, "r");
	if (fp == NULL) {
		err_print("Unable to open mount table.\n");
		fullabort();
	}
	/*
	 * Loop through the mount table until we run out of entries.
	 */
	while ((getmntent(fp, mp)) != -1) {
		/*
		 * Map the block device name to the raw device name.
		 * If it doesn't appear to be a device name, skip it.
		 */
		if (!match_substr(mp->mnt_special, "/dev/"))
			continue;
		(void) strcpy(raw_device, "/dev/r");
		(void) strcat(raw_device, mp->mnt_special + strlen("/dev/"));
		/*
		 * Determine if this appears to be a disk device.
		 * First attempt to open the device.  If if fails, skip it.
		 */
		if ((mfd = open(raw_device, O_RDWR | O_NDELAY)) < 0) {
			continue;
		}
		/*
		 * Must be a character device
		 */
		if (fstat(mfd, &stbuf) == -1 || !S_ISCHR(stbuf.st_mode)) {
			(void) close(mfd);
			continue;
		}
		/*
		 * Attempt to read the configuration info on the disk.
		 */
		if (ioctl(mfd, DKIOCINFO, &dkinfo) < 0) {
			(void) close(mfd);
			continue;
		}
		/*
		 * Finished with the opened device
		 */
		(void) close(mfd);
		/*
		 * If it's not the disk we're interested in, it doesn't apply.
		 */
		if (cur_disk->disk_dkinfo.dki_ctype != dkinfo.dki_ctype ||
			cur_disk->disk_dkinfo.dki_cnum != dkinfo.dki_cnum ||
			cur_disk->disk_dkinfo.dki_unit != dkinfo.dki_unit ||
			strcmp(cur_disk->disk_dkinfo.dki_dname,
					dkinfo.dki_dname) != 0) {
				continue;
		}
		/*
		 * Extract the partition that is mounted.
		 */
		part = PARTITION(stbuf.st_dev);

		/*
		 * Construct a bit map of the partitions that are mounted
		 */
		bm_mounted |= (1 << part);
	}
	/*
	 * Close down the mount table.
	 */
	(void) fclose(fp);
	exit_critical();

	/*
	 * Now we need to check that the current partition list and the
	 * previous partition list (which there must be if we actually
	 * have partitions mounted) overlap  in any way on the mounted
	 * partitions
	 */

	/*
	 * Get the "real" (on-disk) version of the partition table
	 */
	if (ioctl(cur_file, DKIOCGAPART, &old_map) == -1) {
		err_print("Unable to get current partition map.\n");
		return (-1);
	}
	for (i = 0; i < NDKMAP; i++) {
		if (bm_mounted & (1 << i)) {
			/*
			 * This partition is mounted
			 */
			o = &old_map.dka_map[i];
			n = &cur_parts->pinfo_map[i];
#ifdef DEBUG
			fmt_print(
"check_label_to_mount: checking partition '%c'", i + PARTITION_BASE);
#endif
			/*
			 * If partition is identical, we're fine.
			 * If the partition grows, we're also fine, because
			 * the routines in partition.c check for overflow.
			 * It will (ultimately) be up to the routines in
			 * partition.c to warn about creation of overlapping
			 * partitions
			 */

			if (o->dkl_cylno == n->dkl_cylno &&
					o->dkl_nblk <= n->dkl_nblk) {
#ifdef	DEBUG
				if (o->dkl_nblk < n->dkl_nblk) {
					fmt_print(
"- new partition larger by %d blocks", n->dkl_nblk-o->dkl_nblk);
				}
				fmt_print("\n");
#endif
				continue;
			}
#ifdef DEBUG
			fmt_print("- changes; old (%d,%d)->new (%d,%d)\n",
				o->dkl_cylno, o->dkl_nblk, n->dkl_cylno,
				n->dkl_nblk);
#endif
			found = -1;
		}
		if (found)
			break;
	}

	/*
	 * If we found trouble and we're running from a command file,
	 * quit before doing something we really regret.
	 */

	if (found && option_f) {
		err_print("Operation on mounted disks must be interactive.\n");
		cmdabort(SIGINT);
	}
	/*
	 * Return the result.
	 */
	return (found);
}
