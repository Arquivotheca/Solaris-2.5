/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef	_NETINET_TCP_H
#define	_NETINET_TCP_H

#pragma ident	"@(#)tcp.h	1.5	93/08/18 SMI"
/* tcp.h 1.11 88/08/19 SMI; from UCB 7.2 10/28/86	*/

#include <sys/isa_defs.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef	u_long	tcp_seq;
/*
 * TCP header.
 * Per RFC 793, September, 1981.
 */
struct tcphdr {
	u_short	th_sport;		/* source port */
	u_short	th_dport;		/* destination port */
	tcp_seq	th_seq;			/* sequence number */
	tcp_seq	th_ack;			/* acknowledgement number */
#ifdef _BIT_FIELDS_LTOH
	u_int	th_x2:4,		/* (unused) */
		th_off:4;		/* data offset */
#else
	u_int	th_off:4,		/* data offset */
		th_x2:4;		/* (unused) */
#endif
	u_char	th_flags;
#define	TH_FIN	0x01
#define	TH_SYN	0x02
#define	TH_RST	0x04
#define	TH_PUSH	0x08
#define	TH_ACK	0x10
#define	TH_URG	0x20
	u_short	th_win;			/* window */
	u_short	th_sum;			/* checksum */
	u_short	th_urp;			/* urgent pointer */
};

#define	TCPOPT_EOL	0
#define	TCPOPT_NOP	1
#define	TCPOPT_MAXSEG	2

/*
 * Default maximum segment size for TCP.
 * With an IP MSS of 576, this is 536,
 * but 512 is probably more convenient.
 */
#ifdef	lint
#define	TCP_MSS	536
#else
#define	TCP_MSS	MIN(512, IP_MSS - sizeof (struct tcpiphdr))
#endif

/*
 * User-settable options (used with setsockopt).
 */
#define	TCP_NODELAY	0x01	/* don't delay send to coalesce packets */
#define	TCP_MAXSEG	0x02	/* set maximum segment size */

#define	TCP_NOTIFY_THRESHOLD		0x10
#define	TCP_ABORT_THRESHOLD		0x11
#define	TCP_CONN_NOTIFY_THRESHOLD	0x12
#define	TCP_CONN_ABORT_THRESHOLD	0x13


#ifdef	__cplusplus
}
#endif

#endif	/* _NETINET_TCP_H */
