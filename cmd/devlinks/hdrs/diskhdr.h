/*
 * Copyright (c) 1991, 1992 by Sun Microsystems, Inc.
 */

#ident "@(#)diskhdr.h	1.2	92/06/24 SMI"

/*
 * Basic idea is to have a single logical data-structure per disk device.
 * This structure contains information both about the devices names in the
 * /dev/[r]mt and /devfs naming systems; so in some sense that data structure
 * represents the links that should be in place between the two name structures.
 *
 * In addition, there is information abot the /dev/[r]SA names used for the
 * device.
 */


#define MAX_NCTRLRS	100
#define MAXSUB		256	/* Allows for 256 disks/controller */
#define MAXPART		26	/* Max partitions per disk */
#define MAXSLICE	16	/* Max unix slices per disk */

struct disk {
    char *devdsknm;		/* Disk Name (the tNdN part) */
    char *devfsdsknm;		/* The devfs 'disk' identifier */
    struct {
	char *devfspart;	/* Block & Raw devfs partition */
	short  state;		/* State of link: valid, invalid or missing */
    } part[MAXPART][2];
    /*
     * Next data supports the /dev/[r]SA link
     */
    int SAnum;			/* Number in /dev/SA and /dev/rSA */
    short SAstate[2];		/* State of SA link */
};

struct diskctrlr {
    char *devfsctrlrnm;		/* Base controller name in /devfs-land */
    struct disk *dsk[MAXSUB];	/* Holders of disk links */
};

#define LN_VALID	0
#define LN_MISSING	1	/* Link missing */
#define LN_INVALID	2	/* Link invalid */
#define LN_DANGLING	3	/* Link Dangling */

#define LN_D_BLK	0
#define LN_D_RAW	1

#include "utilhdr.h"
