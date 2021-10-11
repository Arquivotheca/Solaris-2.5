/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)igmp.c	1.15	94/11/07 SMI"

/*
 * Internet Group Management Protocol (IGMP) routines.
 *
 * Written by Steve Deering, Stanford, May 1988.
 *
 * MULTICAST 1.1
 */

#ifndef	MI_HDRS

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/dlpi.h>
#include <sys/stropts.h>
#include <sys/strlog.h>
#include <sys/systm.h>
#include <sys/tihdr.h>
#include <sys/tiuser.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>

#include <sys/param.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <sys/sockio.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/igmp_var.h>

#include <inet/common.h>
#include <inet/mi.h>
#include <inet/nd.h>
#include <inet/arp.h>
#include <inet/ip.h>
#include <inet/ip_multi.h>

#include <netinet/ip_mroute.h>
#include <netinet/igmp.h>

#else

#include <types.h>
#include <stream.h>
#include <dlpi.h>
#include <stropts.h>
#include <strlog.h>
#include <lsystm.h>
#include <tihdr.h>
#include <tiuser.h>

#include <param.h>
#include <socket.h>
#include <if.h>
#include <if_arp.h>
#include <sockio.h>
#include <route.h>
#include <in.h>
#include <igmp_var.h>

#include <common.h>
#include <mi.h>
#include <nd.h>
#include <arp.h>
#include <ip.h>
#include <ip_multi.h>
#include <ip_mroute.h>
#include <igmp.h>

#endif

static void	igmp_sendreport(ilm_t *ilm);

/* We only send out reports for non-local groups i.e.
 * exceeding 224.0.0.255. The BSD behavior is 
 * to only block out 224.0.0.0 and 224.0.0.1.
 */
#define REPORT_GROUP(addr) (ntohl(addr) > (u_long)INADDR_MAX_LOCAL_GROUP)

static int	igmp_timers_are_running;
static int 	igmp_time_since_last;	/* Time since last timeout */


/* Return 0 if the message is OK and should be handed to "raw" receivers.
 */
/* ARGSUSED */
int
igmp_input (q, mp, ipif)
	queue_t	* q;
	mblk_t	* mp;
	ipif_t	* ipif;
{
	igmpa_t	* igmpa;
	ipha_t 	* ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	int 	iphlen;
	int 	igmplen;
	ilm_t 	* ilm;
	u32	src, dst, group;
	unsigned	next;

	++igmpstat.igps_rcv_total;

	iphlen = IPH_HDR_LENGTH(ipha);
	if ((mp->b_wptr - mp->b_rptr) < (iphlen + IGMP_MINLEN)) {
		if (!pullupmsg(mp, iphlen + IGMP_MINLEN)) {
			++igmpstat.igps_rcv_tooshort;
			freemsg(mp);
			return -1;
		}
		ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	}
	igmplen = ntohs(ipha->ipha_length) - iphlen;

	/*
	 * Validate lengths
	 */
	if (igmplen < IGMP_MINLEN) {
		++igmpstat.igps_rcv_tooshort;
		freemsg(mp);
		return -1;
	}

	/*
	 * Validate checksum
	 */
	if (IP_CSUM(mp, iphlen, 0)) {
		++igmpstat.igps_rcv_badsum;
		freemsg(mp);
		return -1;
	}
	igmpa = (igmpa_t *)ALIGN32(&mp->b_rptr[iphlen]);
	src = ipha->ipha_src;
	dst = ipha->ipha_dst;

	ip1dbg(("igmp_input: src 0x%x, dst 0x%x on %s\n",
		(int)ntohl(src), (int)ntohl(dst),
		ipif ? ipif->ipif_ill->ill_name : "(null)"));

	switch (igmpa->igmpa_type) {

	case IGMP_HOST_MEMBERSHIP_QUERY:
		++igmpstat.igps_rcv_queries;

		if (dst != ntohl(INADDR_ALLHOSTS_GROUP)) {
			++igmpstat.igps_rcv_badqueries;
			freemsg(mp);
			return -1;
		}

		if (!ipif) {
			ip0dbg(("igmp_input: membership query without ipif set\n"));
			break;
		}

		/*
		 * Start the timers in all of our membership records for
		 * the physical interface on which the query arrived, except 
		 * those that are already running and those that are for the
		 * "local subnets" groups.
		 */
		next = (unsigned)-1;
		for (ilm = ipif->ipif_ill->ill_ilm; ilm; ilm = ilm->ilm_next) {
			if (ilm->ilm_timer == 0 &&
			    REPORT_GROUP(ilm->ilm_addr)) {
				ilm->ilm_timer =
					IGMP_RANDOM_DELAY(ilm->ilm_addr,
							  ilm->ilm_ipif);
				if (ilm->ilm_timer < next)
					next = ilm->ilm_timer;
			}
		}
		
		if (next != (unsigned)-1 && !igmp_timers_are_running) {
			igmp_timers_are_running = 1;
			igmp_time_since_last = next;
			igmp_timeout_start(next);
		}
		break;

	case IGMP_HOST_MEMBERSHIP_REPORT:
		++igmpstat.igps_rcv_reports;

		group = igmpa->igmpa_group;
		if (!CLASSD(group) || group != dst) {
			++igmpstat.igps_rcv_badreports;
			freemsg(mp);
			return -1;
		}

		if (!ipif) {
			ip0dbg(("igmp_input: membership report without ipif set\n"));
			break;
		}

		/*
		 * KLUDGE: if the IP source address of the report has an
		 * unspecified (i.e., zero) subnet number, as is allowed for
		 * a booting host, replace it with the correct subnet number
		 * so that a process-level multicast routing demon can
		 * determine which subnet it arrived from.  This is necessary
		 * to compensate for the lack of any way for a process to
		 * determine the arrival interface of an incoming packet.
		 */
		/* This requires that a copy of *this* message it passed up
		 * to the raw interface which is done by our caller.
		 */
		if ((src & htonl(0xFF000000)) == 0) {	/* Minimum net mask */
			src = ipif->ipif_net_mask & ipif->ipif_local_addr;
			ip1dbg(("igmp_input: changed src to 0x%x\n", 
				(int)ntohl(src)));
			ipha->ipha_src = src;
		}

		/*
		 * If we belong to the group being reported, stop
		 * our timer for that group. Do this for all logical interfaces
		 * on the given physical interface.
		 */
		for (ipif = ipif->ipif_ill->ill_ipif; ipif; 
		     ipif = ipif->ipif_next) {
			ilm = ilm_lookup_exact(ipif, group);
			if (ilm != NULL) {
				ilm->ilm_timer = 0;
				++igmpstat.igps_rcv_ourreports;
			}
		}
		break;
	}

	/*
	 * Pass all valid IGMP packets up to any process(es) listening
	 * on a raw IGMP socket. Do not free the packet.
	 */
	return 0;
}

void
igmp_joingroup(ilm)
	ilm_t *ilm;
{
	if (!REPORT_GROUP(ilm->ilm_addr))
		ilm->ilm_timer = 0;
	else {
		igmp_sendreport(ilm);
		ilm->ilm_timer = IGMP_RANDOM_DELAY(ilm->ilm_addr, 
						   ilm->ilm_ipif);
		if (!igmp_timers_are_running) {
			igmp_timers_are_running = 1;
			igmp_time_since_last = ilm->ilm_timer;
			igmp_timeout_start(igmp_time_since_last);
		}
	}
}

void
igmp_leavegroup(ilm)
	ilm_t *ilm;
{
	/*
	 * No action required on leaving a group.
	 */
#ifdef lint
	ilm = ilm;
#endif
}

/*
 * Called when there are timeout events.
 * Returns number of 200 ms ticks to next event (or 0 if none.)
 */
int
igmp_timeout_handler()
{
	ill_t	*ill;
	ilm_t 	*ilm;
	u_long	next = (u_long)0xffffffff;
	int	elapsed;	/* Since last call */

	/*
	 * Quick check to see if any work needs to be done, in order
	 * to minimize the overhead of fasttimo processing.
	 */
	if (!igmp_timers_are_running)
		return (0);

	elapsed = igmp_time_since_last;
	if (elapsed == 0)
		elapsed = 1;
		
	igmp_timers_are_running = 0;
	for (ill = ill_g_head; ill; ill = ill->ill_next) 
		for (ilm = ill->ill_ilm; ilm; ilm = ilm->ilm_next) {
			if (ilm->ilm_timer != 0) {
				if (ilm->ilm_timer <= elapsed) {
					ilm->ilm_timer = 0;
					igmp_sendreport(ilm);
				}
				else {
					ilm->ilm_timer -= elapsed;
					igmp_timers_are_running = 1;
					if (ilm->ilm_timer < next)
						next = ilm->ilm_timer;
				}
			}
		}
	if (next == (unsigned)-1)
		next = 0;
	igmp_time_since_last = next;
	return (next);
}

/* This will send to ip_wput like icmp_inbound 
 * Note that the lower ill (on which the membership is kept) is used
 * as an upper ill to pass in the multicast parameters.
 *
 * This routine will be called timeout!! 
 */
static void
igmp_sendreport(ilm)
	ilm_t *ilm;
{
	mblk_t	*mp;
	igmpa_t *igmpa;
	ipha_t 	*ipha;
	int	size = sizeof(ipha_t) + sizeof(igmpa_t);
	ipif_t *ipif = ilm->ilm_ipif;
	ill_t	*ill = ipif->ipif_ill;	/* Will be the "lower" ill */
	
	ip1dbg(("igmp_sendreport: for 0x%x on %s\n",
		(int)ntohl(ilm->ilm_addr),
		ill->ill_name));
	mp = allocb(size, BPRI_HI);
	if (mp == NULL)
		return;
	bzero((char *)mp->b_rptr, size);
	mp->b_wptr = mp->b_rptr + size;
	mp->b_datap->db_type = M_DATA;

	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	igmpa = (igmpa_t *)ALIGN32(mp->b_rptr + sizeof(ipha_t));
	igmpa->igmpa_type   = IGMP_HOST_MEMBERSHIP_REPORT;
	igmpa->igmpa_code   = 0;
	igmpa->igmpa_group  = ilm->ilm_addr;
	igmpa->igmpa_cksum  = 0;
	igmpa->igmpa_cksum  = IP_CSUM(mp, sizeof(ipha_t), 0);

	ipha->ipha_version_and_hdr_length = (IP_VERSION << 4) | IP_SIMPLE_HDR_LENGTH_IN_WORDS;
	ipha->ipha_type_of_service = 0;
	ipha->ipha_length = htons(IGMP_MINLEN + IP_SIMPLE_HDR_LENGTH);
	ipha->ipha_fragment_offset_and_flags = 0;
	ipha->ipha_ttl = 1;
	ipha->ipha_protocol = IPPROTO_IGMP;
	ipha->ipha_hdr_checksum = 0;
	ipha->ipha_src = 0;
	ipha->ipha_dst = igmpa->igmpa_group;

	ipha->ipha_src = ipif->ipif_local_addr;
	/*
	 * Request loopback of the report if we are acting as a multicast
	 * router, so that the process-level routing demon can hear it.
	 */
	/* This will run multiple times for the same group if there are members
	 * on the same group for multiple ipif's on the same ill. The 
	 * igmp_input code will suppress this due to the loopback thus we 
	 * always loopback membership report.
	 */
	ip_multicast_loopback(ill->ill_rq, mp);
		
	ip_wput_multicast(ill->ill_wq, mp, ipif);

	++igmpstat.igps_snd_reports;
}
