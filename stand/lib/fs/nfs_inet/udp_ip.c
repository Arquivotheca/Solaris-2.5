#ident	"@(#)udp_ip.c	1.32	94/09/01 SMI"

/*
 * udp_ip.c, Code implementing the UDP/IP network protocol levels.
 *
 * Copyright (C) 1991-1994, Sun Microsystems, Inc.
 */

#include <rpc/types.h>
#include <local.h>
#include <net/if_arp.h>
#include <sys/sainet.h>

#include <sys/promif.h>

#include <sys/bootconf.h>
#include <sys/fcntl.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <sys/t_lock.h>
#include <rpc/clnt.h>
#include <udp_ip.h>
#include <netaddr.h>

#define	WRAP	0x00010000	/* short (16bit) wrap - for udp cksums */

/* globals */
static struct udpip_pkt	pkt_buf[2];		/* our pkt buffers */
static struct ip_frag	fragment[FRAG_MAX];	/* ip fragment buffers */
static struct pseudo_udp	ck;		/* udp checksum header */
#ifdef i386 /* udp_cksum = 0 won't work for i386 right now */
int udp_cksum = 1;				/* cksum on if set */
#else
int udp_cksum = 0;				/* cksum on if set */
#endif

/*
 * This routine is responsible for transmitting, handling timeouts,
 * error handling, retransmits, and handles receiving incoming packets
 * (weeding out only those coming in on the proper port) It is also
 * responsible for reassembling ip fragments on input.
 *
 * Returns: RPC_SUCCESS for success, Closest RPC_* if an error occurred.
 */
enum clnt_stat
xmit(
	caddr_t ckie,		/* outgoing "cookie" */
	register int ckie_size,	/* size of outgoing "cookie" */
	caddr_t rcv_addr,	/* address to put incoming packet in. */
	register int *rcv_size,	/* var to hold rcv data size */
	u_short *sport,		/* udp source port */
	u_short dport,		/* udp destination port */
	int rexmit,		/* retransmission interval (secs) */
	int wait_secs,		/* total number of secs to wait for resp */
	struct sainet *net)	/* network addresses */
{
	extern int 		network_up;	/* set if network up */
	extern int		udp_cksum;	/* set if udp cksum desired */
	extern struct pseudo_udp ck;		/* for udp cksums */
	extern bootdev_t	bootd;		/* our device */
	register int		xdelay;		/* delay between retrys */
	register int		xtime;		/* xmit time */
	register int		pktlen;		/* len of incoming packet */
	struct udpip_pkt	*o_p; 		/* outgoing pkt */
	struct udpip_pkt	*i_p; 		/* incoming pkt */
	struct sainet		*netaddr;	/* our addresses */
	register int		status;		/* return status */
	register u_short	ip_id;		/* current ip pkt id */
	register u_short	datalen;	/* size of UDP pkt */
	register int		ip_hlen;	/* ip header length */
	register caddr_t	curr_rcv;	/* current rcv buf addr */
	register caddr_t	data_start;	/* start of data in pkt */
	static void		frag_flush();	/* zero frag list */
#ifdef DEBUG
	static void		frag_disp(u_short);	/* dumps frag list */
#endif /* DEBUG */
	static int		frag_chk(u_short);	/* see if we have */
							/* them all */
	static int		frag_add(short,		/* add to list */
					u_short);
	static int		udp_chkport(
					struct ip *,	/* input ip */
					short,		/* in dest */
					short);		/* out source */
	static u_short		udp_checksum(caddr_t);	/* udp checksum */

	/* initialize */
	status = RPC_TIMEDOUT;
	ip_id = datalen = 0;
	curr_rcv = (caddr_t)0;
	data_start = (caddr_t)0;
	i_p = &pkt_buf[0];
	o_p = &pkt_buf[1];

	/* check if network device initialized. */
	if (network_up == 0) {
		printf("xmit: network not initialized.\n");
		return (RPC_CANTSEND);
	} else {
		if (net == (struct sainet *)0)
			netaddr = get_sainet();
		else
			netaddr = net;	/* don't use preset */
	}

	/*
	 * xmit() works by sending a request then polling for a reply.  Any
	 * packet received that's not addressed to us causes it to reenter
	 * the transmit-then-poll loop.  In particular, old, stale replies
	 * or broadcasts will cause this behavior.  We try to improve the
	 * situation slightly by flushing any packets that have previously
	 * been received by the interface before we transmit our first packet.
	 */
	while (ip_input(&bootd, (caddr_t)i_p, netaddr) > 0)
		;

	/* BEGIN building outgoing packet */
	/* First, IP... */
	o_p->ip_h.ip_v = IPVERSION;
	o_p->ip_h.ip_hl = sizeof (struct ip) / 4;
	o_p->ip_h.ip_ttl = MAXTTL;
	o_p->ip_h.ip_p = IPPROTO_UDP;
	o_p->ip_h.ip_src = netaddr->sain_myaddr; /* structure copy */
	o_p->ip_h.ip_dst = netaddr->sain_hisaddr; /* structure copy */

	/* Now, UDP... */
	if (*sport == 0) {
		/*
		 * Set our local port to a random number within the
		 * range of IPPORT_RESERVED/2 and IPPORT_RESERVED, since
		 * we are a privileged "process". After all, we are loading
		 * the OS. ;-)
		 */
		*sport = IPPORT_RESERVED - (u_short)(prom_gettime() &
		    (IPPORT_RESERVED/4 - 1)) - 1;
	}
	o_p->udp_h.uh_sport = htons(*sport);
	o_p->udp_h.uh_dport = htons(dport);

	/* copy in cookie */
	(void) bcopy(ckie, o_p->data, ckie_size);

	/* calculate the packet header lengths */
	o_p->udp_h.uh_ulen = htons(sizeof (struct udphdr) + ckie_size);
	o_p->ip_h.ip_len = htons(sizeof (struct ip) +
		htons(o_p->udp_h.uh_ulen));
	o_p->udp_h.uh_sum = 0;

	/* checksum packet */
	if (udp_cksum != 0) {
		/* structure copies */
		ck.src = o_p->ip_h.ip_src;
		ck.dst = o_p->ip_h.ip_dst;
		ck.hdr = o_p->udp_h;

		ck.proto = o_p->ip_h.ip_p;
		ck.len = o_p->udp_h.uh_ulen;
		if ((o_p->udp_h.uh_sum = udp_checksum(o_p->data)) == 0) {
			o_p->udp_h.uh_sum = (u_short)0xffff;
		}
	}
	/* END building outgoing packet */

retry_xmit:
	/* initialize xmit/rcv state */
	if (rexmit == 0)
		xdelay = REXMIT_MSEC;
	else
		xdelay = rexmit * 1000;	/* arg in secs, convert to msecs */
	if (wait_secs == 0)
		wait_secs = RCVWAIT_MSEC;
	else
		wait_secs *= 1000;	/* arg in secs, convert to msecs */
	xtime = prom_gettime();
	wait_secs += xtime;	/* set time to wait */

#ifdef sparc
	/*
	 * prom_gettime() is a hoax (just a dumb counter)
	 * for sun4's (SUNMON proms), so to make
	 * the timer setup below more accurate, we need to use a time
	 * multiplier for sun4's.  At least for the notoriously slow
	 * 4/110's, prom_gettime() was counting up too quickly and
	 * this caused too many "RPC: timed out" warning messages.
	 */
	if (prom_getversion() == SUNMON_ROMVEC_VERSION)
		wait_secs *= 4;
#endif

	while ((prom_gettime() < wait_secs) && (status != RPC_SUCCESS)) {
		/*
		 * initial transmit / retransmit loop. Here we try
		 * to transmit, and increase the delay for unanswered
		 * packets exponentially by multiplying the current
		 * delay by 2, up to a maximum of 64 seconds. We will
		 * try retransmitting as many times as we can before
		 * we run out of wait_secs.
		 */
		if (prom_gettime() >= xtime) {
			if (ip_id != 0) {
				/*
				 * we ran out of time before we could
				 * complete the pkt. Start over. Maybe
				 * we'll have enough time with the new
				 * delay.
				 */
#ifdef DEBUG
				printf("xmit: timeout before getting \
all the fragments.\n");
				frag_disp(datalen);
#endif /* DEBUG */
				ip_id = datalen = 0; /* reset */
				frag_flush();
			}
			/* bump up the retransmit delay - limit to 64 sec */
			xdelay = xdelay < 64000 ? xdelay * 2 : 64000;
			xtime = prom_gettime() + xdelay;

			/* transmit the packet */
			if (ip_output(&bootd, (caddr_t)o_p,
			    ntohs(o_p->ip_h.ip_len) +
			    sizeof (struct ether_header),
			    netaddr, (caddr_t)i_p) != 0) {
				int until;

				/*
				 * The network may be jammed .. sleep for
				 * a second, then try again. See 1136973
				 * for what happens if you don't do this.
				 *
				 * XXX	This algorithm should probably be
				 *	a bit more sophisticated.
				 */
#ifdef	DEBUG
				printf("xmit: ip_output failed.\n");
#endif	/* DEBUG */
				until = prom_gettime() + 1000;
				while (prom_gettime() < until)
					;
#ifdef	DEBUG
				printf("retrying\n");
#endif	/* DEBUG */
				status = RPC_CANTSEND;
				continue;
			}
		}

		/* INPUT SECTION: START */
		if (ip_input(&bootd, (caddr_t)i_p, netaddr) == 0)
			continue;

		/* checksum ip header */
		ip_hlen = i_p->ip_h.ip_hl << 2;
		if (ip_hlen < sizeof (struct ip))
			continue; /* header too short */

		if (ipcksum((caddr_t)&(i_p->ip_h), (u_short)ip_hlen) != 0)
			continue; /* bad checksum */

		/*
		 * check for ip fragmentation.
		 */
		if ((ntohs(i_p->ip_h.ip_off) & ~IP_DF) != 0) {
			if (ip_id == 0) {
				/* first frag - check UDP */
				if (udp_chkport(&(i_p->ip_h),
				    i_p->udp_h.uh_dport,
				    o_p->udp_h.uh_sport) == 0) {
					continue;
				}
				datalen =
				    (u_short)UDP_DATALEN(&(i_p->udp_h));
				ip_id = i_p->ip_h.ip_id;

				/*
				 * set up pseudo header for later udp cksum.
				 */
				if ((udp_cksum != 0) &&
				    (i_p->udp_h.uh_sum != 0)) {
					/* structure copies */
					ck.src = i_p->ip_h.ip_src;
					ck.dst = i_p->ip_h.ip_dst;
					ck.hdr = i_p->udp_h;

					ck.proto = i_p->ip_h.ip_p;
					ck.len = i_p->udp_h.uh_ulen;
				}

				/* first pkt needs udp hdr removed. */
				pktlen = (IP_DATALEN(&(i_p->ip_h)) -
				    sizeof (struct udphdr));
				data_start = i_p->data;
				(void) frag_add(i_p->ip_h.ip_off, pktlen);
				curr_rcv = rcv_addr;
			} else {
				/* wrong frag id - skip */
				if (ip_id != i_p->ip_h.ip_id)
					continue;
				/* no udp header on fragments */
				pktlen = IP_DATALEN(&(i_p->ip_h));
				data_start = (caddr_t)&(i_p->udp_h);

				switch (frag_add(i_p->ip_h.ip_off, pktlen)) {
				case FRAG_SUCCESS:
				/*
				 * Must account for the initial
				 * udp header.
				 */
				    curr_rcv = rcv_addr +
					(short)((i_p->ip_h.ip_off) << 3) -
					sizeof (struct udphdr);
				    break;
				case FRAG_NOSLOTS:
				    printf("xmit: no slots: too many IP \
fragments.\n");
				    status = RPC_CANTRECV;
				    goto done;
				case FRAG_DUP:
#ifdef DEBUG
				    printf("Duplicate fragment\n");
#endif /* DEBUG */
				    continue; /* skip */
				}
				/*
				 * Check if ip fragments reassembled ok.
				 */
				if (((i_p->ip_h.ip_off & IP_MF) == 0) &&
				    (frag_chk(datalen) != 0)) {
					/* finished - reset */
					*rcv_size = datalen;
					ip_id = datalen = 0;
					frag_flush();
					status = RPC_SUCCESS;
				}
				/*
				 * else - keep going until we get all
				 * the fragments or timeout.
				 */
			}
		} else {
			/*
			 * unfragmented packet - check UDP hdr.
			 */
			if (udp_chkport(&(i_p->ip_h), i_p->udp_h.uh_dport,
			    o_p->udp_h.uh_sport) == 0) {
				continue;
			} else {
				data_start = i_p->data;
				datalen =
				    (u_short)UDP_DATALEN(&(i_p->udp_h));
				*rcv_size = pktlen = datalen;
				curr_rcv = rcv_addr;
				status = RPC_SUCCESS;
				/*
				 * set up pseudo header for later udp cksum.
				 */
				if ((udp_cksum != 0) &&
				    (i_p->udp_h.uh_sum != 0)) {
					/* structure copies */
					ck.src = i_p->ip_h.ip_src;
					ck.dst = i_p->ip_h.ip_dst;
					ck.hdr = i_p->udp_h;

					ck.proto = i_p->ip_h.ip_p;
					ck.len = i_p->udp_h.uh_ulen;
				}
			}
		}
		/*
		 * load return buffer with packet, minus
		 * headers.
		 */
		if (pktlen != 0)
			(void) bcopy(data_start, curr_rcv, pktlen);

		/*
		 * Check udp checksum. Why now? To save the bcopy in the
		 * case where ip fragmentation is required.
		 */
		if ((udp_cksum != 0) && (status == RPC_SUCCESS) &&
		    (ck.hdr.uh_sum != 0)) {
			if ((udp_checksum(rcv_addr)) != 0) {
#ifdef	DEBUG
				printf("bad udp checksum - retrying...\n");
#endif	/* DEBUG */
				status = RPC_TIMEDOUT;
				xtime = prom_gettime(); /* retry now! */
			}
		}
		/* END INPUT SECTION */
	}
	/*
	 * if net argument is non-null, reinitialize the
	 * destination fields to reflect where this pkt(s)
	 * came from.
	 */
	if ((status == RPC_SUCCESS) && (net != (struct sainet *)0)) {
		net->sain_hisaddr = i_p->ip_h.ip_src; /* struct copy */
		(void) bcopy(
		    (caddr_t)&(i_p->e_h.ether_shost.ether_addr_octet),
		    (caddr_t)&(net->sain_hisether), sizeof (ether_addr_t));
	}
done:
	/* if status is still RPC_TIMEDOUT, we timed out */
	return (status);
}


/*
 * this routine is a repeat of the same code from the above routine.
 * except this routine doesn't transmit first and retry.  This one
 * just waits around for a packet and returns it.
 * the timeout allows you keep from waiting too long.
 */
enum clnt_stat
recv(
	caddr_t rcv_addr,	/* address to put incoming packet in. */
	register int *rcv_size,	/* var to hold rcv data size */
	int wait_secs,		/* total number of secs to wait for msg */
	u_short dport,		/* udp destination port */
	u_short *sport,		/* port number where packet came from */
	struct sainet *net)	/* network addresses */
{
	extern int 		network_up;	/* set if network up */
	extern bootdev_t	bootd;		/* our device */
	register int		pktlen;		/* len of incoming packet */
	struct udpip_pkt	*i_p; 		/* incoming pkt */
	struct sainet		*netaddr;	/* our addresses */
	register int		status;		/* return status */
	register u_short	ip_id;		/* current ip pkt id */
	register int		ip_hlen;	/* ip header length */
	register u_short	datalen;	/* size of UDP pkt */
	register caddr_t	curr_rcv;	/* current rcv buf addr */
	register caddr_t	data_start;	/* start of data in pkt */
	static void		frag_flush();	/* zero frag list */
	static int		frag_chk(u_short);	/* see if we have */
							/* them all */
	static int		frag_add(short,		/* add to list */
					u_short);
	static int		udp_chkport(
					struct ip *,	/* input ip */
					short,		/* in dest */
					short);		/* out source */
	static u_short		udp_checksum(caddr_t);	/* udp checksum */

	/* initialize */
	status = RPC_TIMEDOUT;
	ip_id = datalen = 0;
	curr_rcv = (caddr_t)0;
	data_start = (caddr_t)0;
	i_p = &pkt_buf[0];

	/* check if network device initialized. */
	if (network_up == 0) {
		printf("xmit: network not initialized.\n");
		return (RPC_CANTSEND);
	} else {
		if (net == (struct sainet *)0)
			netaddr = get_sainet();
		else
			netaddr = net;	/* don't use preset */
	}

	while ((prom_gettime() < wait_secs) && (status != RPC_SUCCESS)) {
		if (ip_input(&bootd, (caddr_t)i_p, netaddr) == 0)
			continue;

		/* checksum ip header */
		ip_hlen = i_p->ip_h.ip_hl << 2;
		if (ip_hlen < sizeof (struct ip))
			continue; /* header too short */

		if (ipcksum((caddr_t)&(i_p->ip_h), (u_short)ip_hlen) != 0)
			continue; /* bad checksum */

		/*
		 * check for ip fragmentation.
		 */
		if ((i_p->ip_h.ip_off & ~IP_DF) != 0) {
			if (ip_id == 0) {
				/* first frag - check UDP */
				if (udp_chkport(&(i_p->ip_h),
				    i_p->udp_h.uh_dport, dport) == 0) {
					continue;
				}
				datalen =
				    (u_short)UDP_DATALEN(&(i_p->udp_h));
				ip_id = i_p->ip_h.ip_id;

				/*
				 * set up pseudo header for later udp cksum.
				 */
				if ((udp_cksum != 0) &&
				    (i_p->udp_h.uh_sum != 0)) {
					/* structure copies */
					ck.src = i_p->ip_h.ip_src;
					ck.dst = i_p->ip_h.ip_dst;
					ck.hdr = i_p->udp_h;

					ck.proto = i_p->ip_h.ip_p;
					ck.len = i_p->udp_h.uh_ulen;
				}

				/* first pkt needs udp hdr removed. */
				pktlen = (IP_DATALEN(&(i_p->ip_h)) -
				    sizeof (struct udphdr));
				data_start = i_p->data;
				(void) frag_add(i_p->ip_h.ip_off, pktlen);
				curr_rcv = rcv_addr;
			} else {
				/* wrong frag id - skip */
				if (ip_id != i_p->ip_h.ip_id)
					continue;
				/* no udp header on fragments */
				pktlen = IP_DATALEN(&(i_p->ip_h));
				data_start = (caddr_t)&(i_p->udp_h);

				switch (frag_add(i_p->ip_h.ip_off, pktlen)) {
				case FRAG_SUCCESS:
				/*
				 * Must account for the initial
				 * udp header.
				 */
				    curr_rcv = rcv_addr +
					(short)((i_p->ip_h.ip_off) << 3) -
					sizeof (struct udphdr);
				    break;
				case FRAG_NOSLOTS:
				    printf("xmit: no slots: too many IP \
fragments.\n");
				    status = RPC_CANTRECV;
				    goto done;
				case FRAG_DUP:
#ifdef DEBUG
				    printf("Duplicate fragment\n");
#endif /* DEBUG */
				    continue; /* skip */
				}
				/*
				 * Check if ip fragments reassembled ok.
				 */
				if (((i_p->ip_h.ip_off & IP_MF) == 0) &&
				    (frag_chk(datalen) != 0)) {
					/* finished - reset */
					*rcv_size = datalen;
					ip_id = datalen = 0;
					frag_flush();
					status = RPC_SUCCESS;
				}
				/*
				 * else - keep going until we get all
				 * the fragments or timeout.
				 */
			}
		} else {
			/*
			 * unfragmented packet - check UDP hdr.
			 */
			if (udp_chkport(&(i_p->ip_h), i_p->udp_h.uh_dport,
			    dport) == 0) {
				continue;
			} else {
				data_start = i_p->data;
				datalen =
				    (u_short)UDP_DATALEN(&(i_p->udp_h));
				*rcv_size = pktlen = datalen;
				curr_rcv = rcv_addr;
				status = RPC_SUCCESS;
				/*
				 * set up pseudo header for later udp cksum.
				 */
				if ((udp_cksum != 0) &&
				    (i_p->udp_h.uh_sum != 0)) {
					/* structure copies */
					ck.src = i_p->ip_h.ip_src;
					ck.dst = i_p->ip_h.ip_dst;
					ck.hdr = i_p->udp_h;

					ck.proto = i_p->ip_h.ip_p;
					ck.len = i_p->udp_h.uh_ulen;
				}
			}
		}
		/*
		 * load return buffer with packet, minus
		 * headers.
		 */
		if (pktlen != 0)
			(void) bcopy(data_start, curr_rcv, pktlen);

		/*
		 * Check udp checksum. Why now? To save the bcopy in the
		 * case where ip fragmentation is required.
		 */
		if ((udp_cksum != 0) && (status == RPC_SUCCESS) &&
		    (ck.hdr.uh_sum != 0)) {
			if ((udp_checksum(rcv_addr)) != 0) {
#ifdef	DEBUG
				printf("bad udp checksum - retrying...\n");
#endif	/* DEBUG */
				status = RPC_TIMEDOUT;
			}
		}
		/* END INPUT SECTION */
	}
	/*
	 * if net argument is non-null, reinitialize the
	 * destination fields to reflect where this pkt(s)
	 * came from.
	 */
	if ((status == RPC_SUCCESS) && (net != (struct sainet *)0)) {
		net->sain_hisaddr = i_p->ip_h.ip_src; /* struct copy */
		(void) bcopy(
		    (caddr_t)&(i_p->e_h.ether_shost.ether_addr_octet),
		    (caddr_t)&(net->sain_hisether), sizeof (ether_addr_t));
		if (sport != (u_short *)0)
			*sport = i_p->udp_h.uh_sport;
	}
done:
	/* if status is still RPC_TIMEDOUT, we timed out */
	return (status);
}

/*
 * Check if we got a udp pkt, and if the port numbers correct.
 *
 * Returns 0 if not our UDP packet, 1 if it is.
 */
static int
udp_chkport(
		struct ip * ip,	/* input ip packet */
		short dport,	/* incoming destination port */
		short sport)	/* outgoing source port */
{
	if ((ip->ip_p != IPPROTO_UDP) || (sport != dport))
		return (0);
	else
		return (1);
}

/*
 * Perform UDP checksum as per RFC 768. Adapted from ipcksum() in inet.c.
 * One's complement checksum of pseudo header, udp header, and data.
 */
static u_short
udp_checksum(caddr_t	addr)	/* start of data */
{
	/* variables */
	extern struct pseudo_udp ck;
	register u_long		end_hdr;
	register u_long		sum = 0;
	register u_short	cnt;
	register u_short	*sp;
	register int		flag = 0;

	/*
	 * Start on the pseudo header. Note that pseudo_udp already takes
	 * acount for the udphdr...
	 */
	sp = (u_short *)&ck;
	cnt = htons(ck.len) + sizeof (struct pseudo_udp) -
		sizeof (struct udphdr);
	end_hdr = (u_long)sp + (u_long)sizeof (struct pseudo_udp);

	cnt >>= 1;
	while (cnt--) {
		sum += *sp++;
		if (sum >= WRAP) {	/* Wrap carries into low bit */
			sum -= WRAP;
			sum++;
		}
		if (!flag && ((u_long)sp >= end_hdr)) {
			sp = (u_short *)addr;	/* Start on the data */
			flag = 1;
		}
	}
	return ((u_short)~sum);
}

/*
 * This function adds a fragment to the current pkt fragment list. Returns
 * FRAG_NOSLOTS if there are no more slots, FRAG_DUP if the fragment is
 * a duplicate, or FRAG_SUCCESS if it is successful.
 */
static int
frag_add(
	short offset,	/* offset in ip packet */
	u_short len)	/* length of data portion of ip packet */
{
	extern struct ip_frag	fragment[];
	register int		i;		/* counter */

	for (i = 0; i < FRAG_MAX; i++) {
		if (fragment[i].offset == offset)
			return (FRAG_DUP);
		if (fragment[i].length == 0) {
			fragment[i].offset = offset;
			fragment[i].length = len;
			return (FRAG_SUCCESS);
		}
	}
	return (FRAG_NOSLOTS);
}

/*
 * Analyze the fragment list - see if we captured all our fragments.
 *
 * Returns 1 if we've got all the fragments, and 0 if we don't.
 */
static int
frag_chk(u_short size)
{
	extern struct ip_frag	fragment[];
	register int		i;
	register u_short	total = 0;	/* culmulative lengths */

	/*
	 * we know we have the first packet and last packets.
	 * check and see if the lengths all add up.
	 */
	for (i = 0; i < FRAG_MAX; i++)
		total += fragment[i].length;
	if (total == size)
		return (1);
	else
		return (0);
/*NOTREACHED*/
}

/*
 * zero the frag list.
 */
static void
frag_flush()
{
	extern struct ip_frag	fragment[];

	bzero((caddr_t)&fragment[0], sizeof (struct ip_frag) * FRAG_MAX);
}

#ifdef DEBUG
/*
 * display the fragment list. For debugging purposes.
 */
static void
frag_disp(u_short size)
{
	extern struct ip_frag	fragment[];
	register int		i;
	register short		total = 0;

	printf("Dumping fragment info:\n\n");
	printf("Offset:		Length:\n");
	for (i = 0; i < FRAG_MAX; i++) {
		printf("%d		%d\n",
		    (short)(fragment[i].offset << 3), fragment[i].length);
		total += fragment[i].length;
	}
	printf("Total length is: %d. It should be: %d\n\n",
	    total, size);
}
#endif /* DEBUG */
