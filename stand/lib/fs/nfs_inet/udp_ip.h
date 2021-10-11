/*
 * udp_ip.h. Contains definitions for udp_ip.c
 *
 * Copyright (c) 1991, Sun Microsystems, Inc.
 */

#ifndef _UDP_IP_H
#define	_UDP_IP_H

#ident	"@(#)udp_ip.h	1.19	94/08/09 SMI"

#include <netaddr.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	REXMIT_MSEC	500	/* start at 1/2 sec between retransmits */
#define	RCVWAIT_MSEC	120000	/* how long to wait for a response. */
#define	FRAG_MAX	10	/* maximum number of ip fragments per udp */
#define	FRAG_SUCCESS	0	/* ip_addfrag() successful */
#define	FRAG_DUP	1	/* duplicate ip fragment */
#define	FRAG_NOSLOTS	2	/* no more ip fragment slots */

/*
 * Maximum data that can be stored in an ethernet packet w/ ip and udp
 * headers. Note that ethernet header size is not subtracted from the
 * maximum - so the *real* MAX_PKT_SIZE (including the ethernet header)
 * is 1514.
 */
#define	DATA_SIZE (MAX_PKT_SIZE - (sizeof (struct ether_header) + \
	sizeof (struct ip) + sizeof (struct udphdr)))
/*
 * Given a pointer to the udp header, produce the size of JUST the data.
 */
#define	UDP_DATALEN(x)	(ntohs((x)->uh_ulen) - sizeof (struct udphdr))
/*
 * Given a pointer to the ip header, produce the size of JUST the data,
 * counting any internal headers as data.
 */
#define	IP_DATALEN(x)	(ntohs((x)->ip_len) - sizeof (struct ip))

/*
 * io block  - a internet UDP/IP packet of maximum ethernet size.
 */
struct udpip_pkt {
	struct ether_header e_h;	/* ethernet header */
	struct ip ip_h;			/* ip header */
	struct udphdr udp_h;		/* udp header */
	char data[DATA_SIZE];		/* our data */
};

/*
 * ip fragmentation data structure
 */
struct ip_frag {
	short offset;
	u_short length;
};

/*
 * pseudo header + udp header needed for calculating UDP checksums.
 */
struct pseudo_udp {
	struct in_addr	src;
	struct in_addr	dst;
	u_char		notused;	/* always zero */
	u_char		proto;		/* protocol used */
	u_short		len;		/* UDP len */
	struct udphdr	hdr;		/* UDP header */
};

/*
 * external function declarations.
 */
extern int ip_output(bootdev_t *, char *,
			short, struct sainet *, char *);

extern int bcopy(char *, char *, int);

extern unsigned short ipcksum(char *, unsigned short);

extern struct sainet *get_sainet();

#ifdef	__cplusplus
}
#endif

#endif /* _UDP_IP_H */
