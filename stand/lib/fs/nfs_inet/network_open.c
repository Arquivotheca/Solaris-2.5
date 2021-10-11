#ident	"@(#)network_open.c	1.10	94/09/20 SMI"

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

/*
 * This file contains functions that open the networking device.
 */

#include <rpc/types.h>
#include <sys/promif.h>
#include <local.h>
#include <sys/sainet.h>

/* set if network device has been opened */
int network_up = 0;

/*
 * device state structure - global so we don't have to keep passing it
 * as a parameter.
 */
bootdev_t bootd;

/*
 * network_open: open our network device. Use the prom lib code if we
 * have a OBP prom, otherwise: scan the devinfo tree for a match, then
 * call network_open() to open the device. We initialize our saioreq and
 * sainet global structs here as well. (we call inet_init() which
 * rarps our IP address for us.
 *
 * returns FALSE for failure, TRUE for success. net_handle is set as
 * a side affect for future prom_* routines.
 */

bool_t
network_open(char *str)
{
	/* functions */
	extern int inet_init(/* rarp our IP address */
				bootdev_t *,
				struct sainet *,
				char *);

	extern void init_netaddr(/* initialize net address code */
					struct sainet *);

	/* variables */
	extern int network_up;
	extern long ether_arp[];	/* scratch buffer for arp */
					/* defined in netaddr.c */
	struct sainet sainet;

#ifdef	DEBUG
	printf("network_open: opening OBP network driver.\n");
	printf("network_open: driver: %s\n", str);
#endif	/* DEBUG */
	/*
	 * OBP prom available. Type is always NETWORK. Otherwise,
	 * who else would load a diskless boot program? ;^)
	 */
	if ((bootd.handle = prom_open(str)) == 0) {
		printf("network_open: prom_open() of %s failed.\n",
		    str);
		return (FALSE);
	}

	/*
	 * Now that the network device is open, we might as well
	 * rarp for our network addresses, then initialize our
	 * address handling code.
	 */
	(void) inet_init(&bootd, &sainet, (char *)&ether_arp[0]);
	init_netaddr(&sainet);

	/* signify that the network is up - nfs_mountroot() needs this */
	network_up = 1;

	return (TRUE);
}
