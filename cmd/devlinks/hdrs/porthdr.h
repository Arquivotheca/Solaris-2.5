#ident	"@(#)porthdr.h	1.8	95/02/26 SMI"
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

/*
 * Structure to represent port device
 */


#define MAX_NMPORTS	2048

#define MAX_SYSPORTS	26

struct port {
    char *devfsnm;		/* The devfs 'port' identifier */
    struct {
	int  state;		/* State of link: valid, invalid or missing */
    } opt[2];			/* Have 2 devices; one normal one no-carrier */
    int	pmstate;		/* Port-monitor entry state */
    char *pmtag;		/* Port Monitor entry */
};

#define P_SYSPORT	0
#define P_NORMPORT	1


#define P_DELAY		0	/* Port with Open delay if no carrier */
#define P_NODELAY	1	/* Port opens even without carrier */

#define LN_VALID	0
#define LN_MISSING	1	/* device missing */
#define LN_DANGLING	2	/* link present in /dev, poinitng nowhere */
#define LN_INVALID	0x10	/* device invalid */

#define PM_ABSENT	0	/* port-monitor entry absent (default state) */
#define PM_PRESENT	1	/* port-monitor entry present */

#include "utilhdr.h"

