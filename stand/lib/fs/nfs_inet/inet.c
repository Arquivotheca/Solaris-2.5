#ident	"@(#)inet.c	1.13	94/10/11 SMI" /* from SunOS 4.1 1.11 88/12/06 */

/*
 * Copyright (c) 1986-1994 by Sun Microsystems, Inc.
 */

/*
 * Standalone IP send and receive - specific to Ethernet
 * Includes ARP and Reverse ARP
 */

#include <sys/machtypes.h>
#include <local.h>
#include <net/if_arp.h>
#include <sys/sainet.h>
#include <sys/idprom.h>
#include <netaddr.h>
#include <sys/promif.h>

ether_addr_t etherbroadcastaddr = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

struct	in_addr	my_in_addr;
struct	in_addr	null_in_addr;

static printhex(int val, int digs);

struct arp_packet {
	struct ether_header	arp_eh;
	struct ether_arp	arp_ea;
#define	used_size (sizeof (struct ether_header) + sizeof (struct ether_arp))
	char	filler[ETHERMIN - sizeof (struct ether_arp)];
};

#define	WAITCNT	2	/* 4 seconds before bitching about arp/revarp */

/*
 * Initialize IP state
 * Find out our Ethernet address and call Reverse ARP
 * to find out our Internet address
 * Set the ARP cache to the broadcast host
 */
inet_init(bootdev_t *bd, struct sainet *sain, char *tmpbuf)
{
	/* get MAC address */
	if (prom_getmacaddr(bd->handle, (caddr_t) &sain->sain_myether) != 0)
		prom_panic("Could not obtain ethernet address for client.\n");

	bzero((caddr_t)&sain->sain_myaddr, sizeof (struct in_addr));
	bzero((caddr_t)&sain->sain_hisaddr, sizeof (struct in_addr));
	bcopy((caddr_t)etherbroadcastaddr, (caddr_t)sain->sain_hisether,
	    sizeof (ether_addr_t));
#ifdef	DEBUG
	printf("inet_init: ethernet address at %x is: ", &sain->sain_myether);
	ether_print(sain->sain_myether);
#endif	/* DEBUG */

	revarp(bd, sain, tmpbuf);
}


/*
 * Output an IP packet
 * Cause ARP to be invoked if necessary
 */
ip_output(
		bootdev_t *bd, caddr_t buf, short len,
		struct sainet *sain, caddr_t tmpbuf)
{
	register struct ether_header *eh;
	register struct ip *ip;
	unsigned short	ipcksum(caddr_t, unsigned short);

	eh = (struct ether_header *)buf;
	ip = (struct ip *)(buf + sizeof (struct ether_header));
	if (bcmp((caddr_t)&ip->ip_dst,
		(caddr_t)&sain->sain_hisaddr,
		sizeof (struct in_addr)) != 0) {
			bcopy((caddr_t)&ip->ip_dst,
				(caddr_t)&sain->sain_hisaddr,
				sizeof (struct in_addr));
			arp(bd, sain, tmpbuf);
	}
	eh->ether_type = htons(ETHERTYPE_IP);
	bcopy((caddr_t)sain->sain_myether, (caddr_t)&eh->ether_shost,
	    sizeof (ether_addr_t));
	bcopy((caddr_t)sain->sain_hisether, (caddr_t)&eh->ether_dhost,
	    sizeof (ether_addr_t));
	/* checksum the packet */
	ip->ip_sum = 0;
	ip->ip_sum = ipcksum((caddr_t)ip, sizeof (struct ip));
	if (len < ETHERMIN + sizeof (struct ether_header)) {
		len = ETHERMIN+sizeof (struct ether_header);
	}

#ifdef sparc
	if (prom_getversion() < 0) {
#ifdef	DEBUG
		printf("ip_output: SUNMON: xmit: from %x, bytes: %x\n",
		    buf, len);
#endif	/* DEBUG */
		return (*bd->sa.si_sif->sif_xmit)(bd->sa.si_devdata,
		    buf, len);
	} else {
#endif

#ifdef	DEBUG
		printf("ip_output: OBP: xmit: from %x, bytes: %x hd: %x\n",
		    buf, len, bd->handle);
#endif	/* DEBUG */
		if (prom_write(bd->handle, buf, len, 0, NETWORK) != len)
			return (-1);
		else
			return (0);
#ifdef sparc
	}
#endif
}

/*
 * Check incoming packets for IP packets
 * addressed to us. Also, respond to ARP packets
 * that wish to know about us.
 * Returns a length for any IP packet addressed to us, 0 otherwise.
 */
ip_input(bootdev_t *bd, caddr_t buf, struct sainet *sain)
{
	register short len;
	register struct ether_header *eh;
	register struct ip *ip;
	register struct ether_arp *ea;

	/*
	 * The following is to cover a calvin prom (v2) bug where
	 * prom_read returns a nonzero length, even when it's not
	 * read a packet
	 */
	bzero(buf, sizeof (struct ether_header));

#ifdef sparc
	if (prom_getversion() < 0)
		len = (*bd->sa.si_sif->sif_poll)(bd->sa.si_devdata, buf);
	else
#endif
		len = prom_read(bd->handle, buf, MAX_PKT_SIZE, 0, NETWORK);

	if (!len)
		return (0);

	eh = (struct ether_header *)buf;
	if (eh->ether_type == ntohs(ETHERTYPE_IP) &&
	    len >= sizeof (struct ether_header) + sizeof (struct ip)) {
		ip = (struct ip *)(buf + sizeof (struct ether_header));
#ifdef NOREVARP
		if ((sain->sain_hisaddr.s_addr & 0xFF000000) == 0 &&
		    bcmp((caddr_t)&etherbroadcastaddr,
			(caddr_t)&eh->ether_dhost,
			sizeof (ether_addr_t)) != 0 &&
		    (in_broadaddr(sain->sain_hisaddr) ||
		    in_lnaof(ip->ip_src) == in_lnaof(sain->sain_hisaddr))) {
			sain->sain_myaddr = ip->ip_dst;
			sain->sain_hisaddr = ip->ip_src;
			sain->sain_hisether =
			    eh->ether_shost;
		}
#endif
		if (bcmp((caddr_t)&ip->ip_dst,
		    (caddr_t)&sain->sain_myaddr,
		    sizeof (struct in_addr)) != 0)
			return (0);
		return (len);
	}
	if (eh->ether_type == ntohs(ETHERTYPE_ARP) &&
	    len >= sizeof (struct ether_header) + sizeof (struct ether_arp)) {
		ea = (struct ether_arp *)(buf + sizeof (struct ether_header));
		if (ea->arp_pro != ntohs(ETHERTYPE_IP))
			return (0);
		if (bcmp((caddr_t)ea->arp_spa,
			(caddr_t)&sain->sain_hisaddr,
			sizeof (struct in_addr)) == 0)
			bcopy((caddr_t)ea->arp_sha,
			    (caddr_t)sain->sain_hisether,
			    sizeof (ether_addr_t));
		if (ea->arp_op == ARPOP_REQUEST &&
		    (bcmp((caddr_t)ea->arp_tpa,
		    (caddr_t)&sain->sain_myaddr,
		    sizeof (struct in_addr)) == 0)) {
			ea->arp_op = ARPOP_REPLY;
			bcopy((caddr_t)ea->arp_sha,
			    (caddr_t)&eh->ether_dhost,
			    sizeof (ether_addr_t));
			bcopy((caddr_t)sain->sain_myether,
			    (caddr_t)&eh->ether_shost,
			    sizeof (ether_addr_t));
			bcopy((caddr_t)ea->arp_sha,
			    (caddr_t)ea->arp_tha,
			    sizeof (ether_addr_t));
			bcopy((caddr_t)ea->arp_spa,
			    (caddr_t)ea->arp_tpa,
			    sizeof (ea->arp_tpa));
			bcopy((caddr_t)sain->sain_myether,
			    (caddr_t)ea->arp_sha,
			    sizeof (ether_addr_t));
			bcopy((caddr_t)&sain->sain_myaddr,
			    (caddr_t)ea->arp_spa,
			    sizeof (ea->arp_spa));

#ifdef sparc
			if (prom_getversion() < 0) {
				(*bd->sa.si_sif->sif_xmit)(
				    bd->sa.si_devdata, buf,
				    sizeof (struct arp_packet));
			} else {
#endif
				(void) prom_write(bd->handle, buf,
				    sizeof (struct arp_packet), 0, NETWORK);
#ifdef sparc
			}
#endif
		}
		return (0);
	}
#ifdef DEBUG
	printf("ip_input: Unknown packet: type 0x%x, addr 0x%x len: 0x%x\n",
	eh->ether_type, (int)eh, len);
	printf("ip_input: ether source: ");
	ether_print(eh->ether_shost);
	printf("ip_input: ether dest: ");
	ether_print(eh->ether_dhost);
#endif /* DEBUG */
	return (0);
}

/*
 * arp
 * Broadcasts to determine Ethernet address given IP address
 * See RFC 826
 */
arp(bootdev_t *bd, struct sainet *sain, char *tmpbuf)
{
	struct arp_packet out;

#ifndef NOREVARP
	if (in_broadaddr(sain->sain_hisaddr)) {
#else
	if (in_broadaddr(sain->sain_hisaddr) ||
	    (sain->sain_hisaddr.s_addr & 0xFF000000) == 0) {
#endif /* !NOREVARP */
		bcopy((caddr_t)etherbroadcastaddr,
		    (caddr_t)sain->sain_hisether,
		    sizeof (ether_addr_t));
		return;
	}
	out.arp_eh.ether_type = htons(ETHERTYPE_ARP);
	out.arp_ea.arp_op = htons(ARPOP_REQUEST);
	bcopy((caddr_t)etherbroadcastaddr,
	    (caddr_t)out.arp_ea.arp_tha,
	    sizeof (ether_addr_t));
	bcopy((caddr_t)&sain->sain_hisaddr,
	    (caddr_t)out.arp_ea.arp_tpa,
	    sizeof (out.arp_ea.arp_tpa));
	comarp(bd, sain, &out, tmpbuf);
}

/*
 * Reverse ARP client side
 * Determine our Internet address given our Ethernet address
 * See RFC 903
 */
revarp(bootdev_t *bd, struct sainet *sain, char *tmpbuf)
{
	struct arp_packet out;

	bzero(&out, sizeof (struct arp_packet));

#ifdef NOREVARP
	bzero((caddr_t)&sain->sain_myaddr, sizeof (struct in_addr));
	bcopy((caddr_t)&sain->sain_myether.ether_addr_octet[3],
		(caddr_t)(&sain->sain_myaddr)+1, 3);
#else
	out.arp_eh.ether_type = htons(ETHERTYPE_REVARP);
	out.arp_ea.arp_op = htons(REVARP_REQUEST);
	bcopy((caddr_t)sain->sain_myether,
	    (caddr_t)out.arp_ea.arp_tha,
	    sizeof (ether_addr_t));
	/* What we want to find out... */
	bzero(out.arp_ea.arp_tpa, sizeof (struct in_addr));
	comarp(bd, sain, &out, tmpbuf);
#endif
	bcopy((caddr_t)&sain->sain_myaddr,
	    (caddr_t)&my_in_addr,
	    sizeof (struct in_addr));
}

/*
 * Common ARP code
 * Broadcast the packet and wait for the right response.
 * Fills in *sain with the results
 */
comarp(bootdev_t *bd, struct sainet *sain,
	struct arp_packet *out, char *tmpbuf)
{
	register struct arp_packet *in = (struct arp_packet *)tmpbuf;
	register int e, count, time, feedback, len, delay = 2;
	char    *ind = "-\\|/";
	struct in_addr tmp_ia;

	bcopy((caddr_t)etherbroadcastaddr,
	    (caddr_t)&out->arp_eh.ether_dhost,
	    sizeof (ether_addr_t));
	bcopy((caddr_t)sain->sain_myether,
	    (caddr_t)&out->arp_eh.ether_shost,
	    sizeof (ether_addr_t));
	out->arp_ea.arp_hrd =  htons(ARPHRD_ETHER);
	out->arp_ea.arp_pro = htons(ETHERTYPE_IP);
	out->arp_ea.arp_hln = sizeof (ether_addr_t);
	out->arp_ea.arp_pln = sizeof (struct in_addr);
	bcopy((caddr_t)sain->sain_myether,
	    (caddr_t)out->arp_ea.arp_sha,
	    sizeof (ether_addr_t));
	bcopy((caddr_t)&sain->sain_myaddr, (caddr_t)out->arp_ea.arp_spa,
	    sizeof (out->arp_ea.arp_spa));
	feedback = 0;

	for (count = 0; /* no conditional */; count++) {
		if (count == WAITCNT) {
			if (out->arp_ea.arp_op == ARPOP_REQUEST) {
				printf("\nRequesting Ethernet address for ");
				bcopy((caddr_t)out->arp_ea.arp_tpa,
				    (caddr_t)&tmp_ia, sizeof (tmp_ia));
				inet_print(tmp_ia);
			} else {
				printf("\nRequesting Internet address for ");
				ether_print(out->arp_ea.arp_tha);
			}
		}

#ifdef sparc
		if (prom_getversion() < 0) {
#ifdef	DEBUG
			printf("comarp: SUNMON: from: %x, bytes: %x\n",
			    out, sizeof (*out));
#endif	/* DEBUG */
			e = (*bd->sa.si_sif->sif_xmit)(bd->sa.si_devdata,
			    (caddr_t)out, sizeof (*out));
		} else {
#endif

#ifdef	DEBUG
			printf("comarp: OBP: from: %x, bytes: %x hd: %x\n",
			    out, sizeof (*out), bd->handle);
#endif	/* DEBUG */
			e = prom_write(bd->handle, (caddr_t)out,
			    sizeof (*out), 0, NETWORK);
#ifdef sparc
		}
#endif
		if (e)
			printf("X\b");
		else
			printf("%c\b", ind[feedback++ % 4]); /* Show activity */

		time = prom_gettime() + (delay * 1000);	/* broadcast delay */
		while (prom_gettime() <= time) {
#ifdef	DEBUG
			printf("comarp: POLLING...\n");
#endif	/* DEBUG */

#ifdef sparc
			if (prom_getversion() < 0) {
				len = (*bd->sa.si_sif->sif_poll)(
				    bd->sa.si_devdata, tmpbuf);
			} else {
#endif
				len = prom_read(bd->handle, tmpbuf,
				    MAX_PKT_SIZE, 0, NETWORK);
#ifdef sparc
			}
#endif
			if (len < used_size)
				continue;
			if (in->arp_ea.arp_pro != ntohs(ETHERTYPE_IP))
				continue;
			if (out->arp_ea.arp_op == ntohs(ARPOP_REQUEST)) {
				if (in->arp_eh.ether_type !=
				    ntohs(ETHERTYPE_ARP))
					continue;
				if (in->arp_ea.arp_op != ntohs(ARPOP_REPLY))
					continue;
				if (bcmp((caddr_t)in->arp_ea.arp_spa,
				    (caddr_t)out->arp_ea.arp_tpa,
				    sizeof (struct in_addr)) != 0)
					continue;
				if (count >= WAITCNT) {
					printf("Found at ");
					ether_print(in->arp_ea.arp_sha);
				}
				bcopy((caddr_t)in->arp_ea.arp_sha,
				    (caddr_t)sain->sain_hisether,
				    sizeof (ether_addr_t));
				return;
			} else {		/* Reverse ARP */
				if (in->arp_eh.ether_type !=
				    ntohs(ETHERTYPE_REVARP))
					continue;
				if (in->arp_ea.arp_op != ntohs(REVARP_REPLY))
					continue;
				if (bcmp((caddr_t)in->arp_ea.arp_tha,
				    (caddr_t)out->arp_ea.arp_tha,
				    sizeof (ether_addr_t)) != 0)
					continue;

				if (count >= WAITCNT) {
					printf("Internet address is ");
					bcopy((caddr_t)in->arp_ea.arp_tpa,
					    (caddr_t)&tmp_ia, sizeof (tmp_ia));
					inet_print(tmp_ia);
				}
				bcopy((caddr_t)in->arp_ea.arp_tpa,
				    (caddr_t)&sain->sain_myaddr,
				    sizeof (sain->sain_myaddr));
				/*
				 * short circuit first ARP
				 */
				bcopy((caddr_t)in->arp_ea.arp_spa,
				    (caddr_t)&sain->sain_hisaddr,
				    sizeof (sain->sain_hisaddr));
				bcopy((caddr_t)in->arp_ea.arp_sha,
				    (caddr_t)sain->sain_hisether,
				    sizeof (ether_addr_t));
				return;
			}
		}

		delay = delay * 2;	/* Double the request delay */
		if (delay > 64)		/* maximum delay is 64 seconds */
			delay = 64;

#ifdef	XXXX	/* no reset in obp/sunmon drivers */
		(*sip->si_sif->sif_reset)(sip->si_devdata);
#endif	/* XXXX */
	}
	/*NOTREACHED*/
}

#ifdef NOREVARP
/*
 * Return the host portion of an internet address.
 */
u_long
in_lnaof(struct in_addr in)
{
	register u_long i = ntohl(in.s_addr);

	if (IN_CLASSA(i))
		return ((i)&IN_CLASSA_HOST);
	else if (IN_CLASSB(i))
		return ((i)&IN_CLASSB_HOST);
	else
		return ((i)&IN_CLASSC_HOST);
}
#endif /* NOREVARP */

/*
 * Test for broadcast IP address
 */
in_broadaddr(struct in_addr in)
{
	u_long i;
	bcopy((caddr_t)&in, (caddr_t)&i, sizeof (in));

	if (IN_CLASSA(i)) {
		i &= IN_CLASSA_HOST;
		return (i == 0 || i == 0xFFFFFF);
	} else if (IN_CLASSB(i)) {
		i &= IN_CLASSB_HOST;
		return (i == 0 || i == 0xFFFF);
	} else if (IN_CLASSC(i)) {
		i &= IN_CLASSC_HOST;
		return (i == 0 || i == 0xFF);
	} else
		return (0);
	/*NOTREACHED*/
}

/*
 * Compute one's complement checksum
 * for IP packet headers
 */
unsigned short
ipcksum(caddr_t cp, unsigned short count)
{
	register unsigned short	*sp = (unsigned short *)cp;
	register unsigned long	sum = 0;
	register unsigned long	oneword = 0x00010000;

	if (count == 0)
		return (0);
	count >>= 1;
	while (count--) {
		sum += *sp++;
		if (sum >= oneword) {		/* Wrap carries into low bit */
			sum -= oneword;
			sum++;
		}
	}
	return ((unsigned short)~sum);
}
/*
 * Description: Prints the internet address
 *
 * Synopsis:	status = inet_print(s)
 *		status	:(null)
 *		s	:(char *) pointer to internet address
 */
inet_print(struct in_addr s)
{
	int	len = 2;

	printf("%d.%d.%d.%d = ", s.S_un.S_un_b.s_b1, s.S_un.S_un_b.s_b2,
		s.S_un.S_un_b.s_b3, s.S_un.S_un_b.s_b4);

	printhex(s.S_un.S_un_b.s_b1, len);
	printhex(s.S_un.S_un_b.s_b2, len);
	printhex(s.S_un.S_un_b.s_b3, len);
	printhex(s.S_un.S_un_b.s_b4, len);
	printf("\n");
}
ether_print(ether_addr_t ea)
{
	printf("%x:%x:%x:%x:%x:%x\n", ea[0], ea[1], ea[2], ea[3],
	    ea[4], ea[5]);
}

char chardigs[] = "0123456789ABCDEF";

/*
 * printhex() prints rightmost <digs> hex digits of <val>
 */
static
printhex(int val, int digs)
{

	extern void prom_putchar(char);

	/* digs == 0 => print 8 digits */
	for (digs = ((digs-1)&7)<<2; digs >= 0; digs -= 4)
		prom_putchar(chardigs[(val>>digs)&0xF]);
}
