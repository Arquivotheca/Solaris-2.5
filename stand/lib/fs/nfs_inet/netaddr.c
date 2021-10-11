#ident	"@(#)netaddr.c	1.18	94/08/09 SMI"

/*
 * Copyright (c) 1991-1994 by Sun Microsystems, Inc.
 *
 * This file contains routines which manipulate the stand alone network
 * structure, and get/set things like source/destination ip and ethernet
 * addresses.
 */

#include <sys/types.h>
#include <local.h>
#include <netinet/in_var.h>
#include <net/if_arp.h>
#include <sys/sainet.h>
#include <netaddr.h>
#include <sys/promif.h>

#define	WAITCNT		2	/* 4 seconds before showing arp messages */
#define	ARP_TRIES	5	/* try five times before giving up */
#define	ARP_DELAY	2	/* initial delay before retransmitting */
#define	ARP_SIZE	(sizeof (struct ether_header) + \
			sizeof (struct ether_arp))

/* global - local to this file */
static struct sainet sain_s;	/* our ether/IP address (us/server) */
static struct arp_packet {
	struct ether_header	arp_eh;
	struct ether_arp	arp_ea;
	char 			filler[ETHERMIN - sizeof (struct ether_arp)];
};

/* global - scratch buffer for use for arp/rarp return pkts. */
long	ether_arp[(MAX_PKT_SIZE + sizeof (long) - 1) / sizeof (long)];

/*
 * set_netip: set various IP fields in our sainet structure.
 */
void
set_netip(
		struct in_addr *req,	/* contains request */
		enum netaddr_type type)	/* Which field to change */
{
	extern struct sainet sain_s;

	if (type == SOURCE)
		sain_s.sain_myaddr = *req; /* structure copy */
	else 	/* DESTIN */
		sain_s.sain_hisaddr = *req; /* structure copy */
}

/*
 * set_netether: set various ethernet fields in our sainet structure.
 */
void
set_netether(
		ether_addr_t *ea,	/* request */
		enum netaddr_type type)	/* which one */
{
	extern struct sainet sain_s;

	if (type ==  DESTIN)
		(void) bcopy((caddr_t)ea, (caddr_t)&sain_s.sain_hisether,
		    sizeof (ether_addr_t));
	else	/* SOURCE */
		(void) bcopy((caddr_t)ea, (caddr_t)&sain_s.sain_myether,
		    sizeof (ether_addr_t));
}

/*
 * get_netip: returns ptr to in_addr structure of appropriate type.
 */
struct in_addr *
get_netip(enum netaddr_type type)
{
	extern struct sainet sain_s;

	if (type == SOURCE)
		return (&(sain_s.sain_myaddr));
	else	/* DESTIN */
		return (&(sain_s.sain_hisaddr));
/*NOTREACHED*/
}

/*
 * get_netether: returns ptr to ether_addr_t of appropriate type.
 */
ether_addr_t *
get_netether(enum netaddr_type type)
{
	extern struct sainet sain_s;

	if (type == DESTIN)
		return (&(sain_s.sain_hisether));
	else	/* SOURCE */
		return (&(sain_s.sain_myether));
/*NOTREACHED*/
}

/*
 * init_netaddr: initialize our sainet structure.
 */
void
init_netaddr(struct sainet *s)
{
	extern struct sainet sain_s;

	sain_s.sain_myaddr = s->sain_myaddr; /* structure copy */
	sain_s.sain_hisaddr = s->sain_hisaddr; /* structure copy */
	(void) bcopy((caddr_t)&(s->sain_myether),
	    (caddr_t)&sain_s.sain_myether, sizeof (ether_addr_t));
	(void) bcopy((caddr_t)&(s->sain_hisether),
		    (caddr_t)&sain_s.sain_hisether, sizeof (ether_addr_t));
}

/*
 * get_sainet: return ptr to sainet.
 */
struct sainet *
get_sainet()
{
	extern struct sainet sain_s;

	return (&sain_s);
}

/*
 * Given an IP address, return the interface broadcast address taking
 * into account its class. Returns ptr to a struct in_addr.
 */
struct in_addr *
brdcast_netip(struct in_addr *in)
{
	/* variables */
	register u_long net, netmask;
	register u_long subnet, subnetmask;
	u_long ipaddr;
	static u_long addr;	/* pointer to this value is returned */

	/* alignment */
	bcopy((caddr_t)in, (caddr_t)&ipaddr, sizeof (u_long));
	ipaddr = ntohl(ipaddr);

	if (IN_CLASSA(ipaddr) != 0)
		netmask = IN_CLASSA_NET;
	else if (IN_CLASSB(ipaddr) != 0)
		netmask = IN_CLASSB_NET;
	else
		netmask = IN_CLASSC_NET;

	net = ipaddr & netmask;

	/*
	 * The subnet mask includes at least the standard network part, but
	 * may already have been set to a larger value.
	 */
	subnetmask |= netmask;
	subnet = ipaddr & subnetmask;

	/*
	 * Now that we have the subnet address, build the broadcast
	 * address.
	 */
	addr = htonl(net | (INADDR_BROADCAST & ~netmask));
	return ((struct in_addr *)&addr);
}

/*
 * get_arp: Given an ip address, broadcast to determine ethernet address.
 * (see inet.c). The argument sainet ptr is assumed to be pointed at
 * an initialized sainet structure, with source fields filled in, and
 * the destination ip address (whom we want the ethernet address for) also
 * filled in. The destination ethernet address will be set to the
 * appropriate ethernet address.
 *
 * Adapted from inet.c(arp/comparp). This one trys ARP_TRIES before
 * giving up. Useful for trying for an arp request; then doing something
 * else if it fails. inet.c(arp) tries forever.
 *
 * Returns (1) if arp succeeded, 0 if it failed.
 */
int
get_arp(struct sainet *s)
{
	/* functions */
	extern int		in_broadaddr();	/* defined in inet.c */

	/* variables */
	extern bootdev_t	bootd;		/* defined in net_open */
	extern long 		ether_arp[];	/* defined here */
	extern ether_addr_t	etherbroadcastaddr; /* defined in inet.c */
	struct arp_packet	out;		/* buffer for outgoing */
	register struct arp_packet *in;		/* incoming buffer */
	register int		count;		/* number of tries so far */
	register int		bytes;		/* number of bytes xmit */
	register u_int		time;		/* next transmit time */
	register int		feedback;	/* progress counter */
	char    		*ind;		/* progress indicator */
	register int		len;		/* response size */
	register int		delay;		/* tranmit delay */

	/* if it's an ip broadcast, then it's simple */
	if (in_broadaddr(s->sain_hisaddr)) {
		bcopy((caddr_t)&etherbroadcastaddr,
		    (caddr_t)&(s->sain_hisether),
		    sizeof (ether_addr_t));
		return (1);
	}

	/* initialize */
	in = (struct arp_packet *)&ether_arp[0];
	ind = "-\\|/";
	delay = ARP_DELAY;
	out.arp_eh.ether_type =	htons(ETHERTYPE_ARP);
	out.arp_ea.arp_op =	htons(ARPOP_REQUEST);
	out.arp_ea.arp_hrd =	htons(ARPHRD_ETHER);
	out.arp_ea.arp_pro =	htons(ETHERTYPE_IP);
	out.arp_ea.arp_hln =	sizeof (ether_addr_t);
	out.arp_ea.arp_pln =	sizeof (struct in_addr);
	bcopy((caddr_t)&etherbroadcastaddr,
	    (caddr_t)&(out.arp_ea.arp_tha), sizeof (ether_addr_t));
	bcopy((caddr_t)&(s->sain_hisaddr),
	    (caddr_t)&out.arp_ea.arp_tpa,
	    sizeof (out.arp_ea.arp_tpa));
	bcopy((caddr_t)&etherbroadcastaddr,
	    (caddr_t)&(out.arp_eh.ether_dhost.ether_addr_octet),
		sizeof (ether_addr_t));
	bcopy((caddr_t)&(s->sain_myether),
	    (caddr_t)&(out.arp_eh.ether_shost.ether_addr_octet),
	    sizeof (ether_addr_t));
	bcopy((caddr_t)&(s->sain_myether),
	    (caddr_t)&(out.arp_ea.arp_sha), sizeof (ether_addr_t));
	bcopy((caddr_t)&(s->sain_myaddr),
	    (caddr_t)&(out.arp_ea.arp_spa),
	    sizeof (out.arp_ea.arp_spa));

	for (count = 0, feedback = 0; count <= ARP_TRIES; count++) {
		if (count == WAITCNT) {
			printf("\nRequesting Ethernet address for ");
			inet_print(out.arp_ea.arp_tpa);
		}
		/* transmit */
#ifdef sparc
		if (prom_getversion() < 0) {
			bytes = (*(bootd.sa.si_sif->sif_xmit))(
			    bootd.sa.si_devdata, (caddr_t)&out,
			    sizeof (struct arp_packet));
		} else {
#endif
			bytes = prom_write(bootd.handle, (caddr_t)&out,
			    sizeof (struct arp_packet), 0, NETWORK);
#ifdef sparc
		}
#endif

		if (bytes != 0) {
			printf("X\b");
		} else {
			/* Show activity */
			printf("%c\b", ind[feedback++ % 4]);
		}

		/* broadcast delay */
		time = prom_gettime() + (delay * 1000);
		while (prom_gettime() <= time) {
#ifdef sparc
			if (prom_getversion() < 0) {
				len = (*(bootd.sa.si_sif->sif_poll))(
				    bootd.sa.si_devdata, (char *)in);
			} else {
#endif
				len = prom_read(bootd.handle, (char *)in,
				    MAX_PKT_SIZE, 0, NETWORK);
#ifdef sparc
			}
#endif
			if (len < ARP_SIZE)
				continue;
			if (in->arp_ea.arp_pro != ntohs(ETHERTYPE_IP))
				continue;
			if (in->arp_eh.ether_type != ntohs(ETHERTYPE_ARP))
				continue;
			if (in->arp_ea.arp_op != ntohs(ARPOP_REPLY))
				continue;
			if (bcmp((caddr_t)in->arp_ea.arp_spa,
			    (caddr_t)&out.arp_ea.arp_tpa,
			    sizeof (struct in_addr)) != 0)
				continue;
			if (count >= WAITCNT) {
				printf("Found at ");
				ether_print(in->arp_ea.arp_sha);
			}
			bcopy((caddr_t)in->arp_ea.arp_sha,
			    (caddr_t)s->sain_hisether, sizeof (ether_addr_t));
			return (1); /* got it */
		}
		/* set new transmit delay. Max delay is 8 seconds */
		delay = delay < 8 ? delay * 2 : 8;

	}
	printf("No reply received.\n");
	return (0);
}

#ifdef _LITTLE_ENDIAN

unsigned long
htonl(unsigned long in)
{
	unsigned long	i;

	i = (unsigned long)((in & (unsigned long)0xff000000) >> 24) +
	    (unsigned long)((in & (unsigned long)0x00ff0000) >> 8) +
	    (unsigned long)((in & (unsigned long)0x0000ff00) << 8) +
	    (unsigned long)((in & (unsigned long)0x000000ff) << 24);
	return (i);
}

unsigned long
ntohl(unsigned long in)
{
	return (htonl(in));
}

unsigned short
htons(unsigned short in)
{
	register int arg = (int)in;
	unsigned short i;

	i = (unsigned short)(((arg & 0xff00) >> 8) & 0xff);
	i |= (unsigned short)((arg & 0xff) << 8);
	return ((unsigned short)i);
}

unsigned short
ntohs(unsigned short in)
{
	return (htons(in));
}

#endif
