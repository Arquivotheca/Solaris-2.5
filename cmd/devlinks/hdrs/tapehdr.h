#ident	"@(#)tapehdr.h	1.3	93/08/05 SMI"
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */
/*
 * Basic idea is to have a single logical data-structure per tape device.
 * This structure contains information both about the devices names in the
 * /dev/[r]mt and /devfs naming systems; so in some sense that data structure
 * represents the links that should be in place between the two name structures.
 *
 * In addition, there is information about the /dev/[r]SA names used for the
 * device.
 */


#define MAX_NTAPES	128
#define MAXSUB		32

struct tapedev {
    char *devfsname;		/* Base name in /devfs-land */
    char *def_devvar;		/* 'default' variant */
    uchar_t def_state;		/* State of default entry */
    struct {
	char *devvar;		/* Variant name in /dev */
	char *devfsvar;		/* Variant name in /devfs */
	uchar_t state;		/* State of link: valid, invalid or missing */
    } link[MAXSUB];
};

#define LN_VALID	0
#define LN_MISSING	1	/* Link missing */
#define LN_INVALID	2	/* Link invalid */
#define LN_DANGLING	3	/* Link dangling */


#include "utilhdr.h"
