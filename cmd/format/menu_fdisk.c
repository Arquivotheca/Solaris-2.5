
/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)menu_fdisk.c	1.12	95/03/10 SMI"

/*
 * This file contains functions that implement the fdisk menu commands.
 */
#include "global.h"
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <sys/dktp/fdisk.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/dklabel.h>

#include "main.h"
#include "analyze.h"
#include "menu.h"
#include "menu_command.h"
#include "menu_defect.h"
#include "menu_partition.h"
#include "menu_fdisk.h"
#include "param.h"
#include "misc.h"
#include "label.h"
#include "startup.h"
#include "partition.h"
#include "prompts.h"
#include "checkmount.h"
#include "io.h"
#include "ctlr_scsi.h"
#include "auto_sense.h"

extern	struct menu_item menu_fdisk[];
static char fdisk_buf[256];


/*
 * This routine implements the 'fdisk' command.  It simply runs
 * the fdisk command on the current disk.
 * Use of this is restricted to interactive mode only.
 */
int
c_fdisk()
{

	char buf[256];

	/*
	 * We must be in interactive mode to use the fdisk command
	 */
	if (option_f != (char *)NULL || isatty(0) != 1 || isatty(1) != 1) {
		err_print("Fdisk command is for interactive use only!\n");
		return (-1);
	}

	/*
	 * There must be a current disk type and a current disk
	 */
	if (cur_dtype == NULL) {
		err_print("Current Disk Type is not set.\n");
		return (-1);
	}

	/*
	 * There must be a current disk type and a current disk
	 */
	if (cur_dtype == NULL) {
		err_print("Current Disk Type is not set.\n");
		return (-1);
	}

	/*
	 * Run the fdisk program.
	 */
	(void) close(cur_file);
	sprintf(buf, "fdisk /dev/rdsk/%sp0\n", cur_disk->disk_name);
	system(buf);
	sprintf(buf, "/dev/rdsk/%sp0", cur_disk->disk_name);
	if ((cur_file = open_disk(buf, O_RDWR | O_NDELAY)) < 0) {
		err_print(
"Error: can't open selected disk '%s'.\n", buf);
		fullabort();
	}
	/*
	 * Get solaris partition information in the fdisk partition table
	 */
	if (get_solaris_part(cur_file, &cur_disk->fdisk_part)) {
		err_print("No fdisk solaris partition found\n");
	}

	(void) close(cur_file);
	if ((cur_file = open_disk(cur_disk->disk_path,
					O_RDWR | O_NDELAY)) < 0) {
		err_print(
"Error: can't open selected disk '%s'.\n", buf);
		fullabort();
	}

	return (0);
}

int
get_solaris_part(int fd, struct ipart *ipart)
{
	int i, error = 0;
	struct ipart *ip;
	int status;

	lseek(fd, 0, 0);
	status = read(fd, (caddr_t)&boot_sec, NBPSCTR);

	if (status != NBPSCTR) {
		err_print("Bad read of fdisk partition. Status = %x\n", status);
		err_print("Cannot read fdisk partition information.\n");
		return (-1);
	}
	for (i = 0; i < 4; i++) {
		int ipc;
		ipc = i * sizeof (struct ipart);
		ip = (struct ipart *)&boot_sec.parts[ipc];
		if (ip->systid == SUNIXOS) {
			pcyl = ip->numsect / (nhead * nsect);
			xstart = ip->relsect / (nhead * nsect);
			solaris_offset = ip->relsect;
			ncyl = pcyl - acyl;
			break;
		}
	}

	if (i == 4) {
		err_print("Solaris part not found\n");
		return (-1);
	}

	if (bcmp(ip, ipart, sizeof (struct ipart)) && !error) {
		printf(
"\nWARNING: Solaris fdisk partition changed - Please relabel the disk\n");
	}
	bcopy(ip, ipart, sizeof (struct ipart));

	return (error);
}


int
copy_solaris_part(struct ipart *ipart)
{

	int status, i, fd;
	struct mboot	mboot;
	struct ipart *ip;
	char	buf[80];

	sprintf(buf, "/dev/rdsk/%sp0", cur_disk->disk_name);
	fd = open(buf, O_RDONLY);
	status = read(fd, (caddr_t)&mboot, sizeof (struct mboot));

	if (status != sizeof (struct mboot)) {
		err_print("Bad read of fdisk partition.\n");
		close(fd);
		return (-1);
	}

	for (i = 0; i < 4; i++) {
		int ipc;
		ipc = i * sizeof (struct ipart);
		ip = (struct ipart *)&mboot.parts[ipc];
		if (ip->systid == SUNIXOS) {
			pcyl = ip->numsect / (nhead * nsect);
			ncyl = pcyl - acyl;
			solaris_offset = ip->relsect;
			bcopy(ip, ipart, sizeof (struct ipart));
			break;
		}
	}

	close(fd);
	return (0);

}

int
auto_solaris_part(struct dk_label *label)
{

	int status, i, fd;
	struct mboot	mboot;
	struct ipart *ip;
	char	buf[80];

	sprintf(buf, "/dev/rdsk/%sp0", x86_devname);
	fd = open(buf, O_RDONLY);
	status = read(fd, (caddr_t)&mboot, sizeof (struct mboot));

	if (status != sizeof (struct mboot)) {
		err_print("Bad read of fdisk partition.\n");
		return (-1);
	}

	for (i = 0; i < 4; i++) {
		int ipc;
		ipc = i * sizeof (struct ipart);
		ip = (struct ipart *)&mboot.parts[ipc];
		if (ip->systid == SUNIXOS) {
			label->dkl_pcyl = ip->numsect /
			    (label->dkl_nhead * label->dkl_nsect);
			label->dkl_ncyl = label->dkl_pcyl - label->dkl_acyl;
			solaris_offset = ip->relsect;
			break;
		}
	}

	close(fd);

}

int
good_fdisk()
{

	if (cur_disk->fdisk_part.numsect > 0)
		return (1);
	else
		return (0);
}
