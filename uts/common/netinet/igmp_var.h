/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef	_NETINET_IGMP_VAR_H
#define	_NETINET_IGMP_VAR_H

#pragma ident	"@(#)igmp_var.h	1.7	93/04/22 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Internet Group Management Protocol (IGMP),
 * implementation-specific definitions.
 *
 * Written by Steve Deering, Stanford, May 1988.
 *
 * MULTICAST 1.1
 */

struct igmpstat {
	u_int	igps_rcv_total;		/* total IGMP messages received    */
	u_int	igps_rcv_tooshort;	/* received with too few bytes	   */
	u_int	igps_rcv_badsum;	/* received with bad checksum	   */
	u_int	igps_rcv_queries;	/* received membership queries	   */
	u_int	igps_rcv_badqueries;	/* received invalid queries	   */
	u_int	igps_rcv_reports;	/* received membership reports	   */
	u_int	igps_rcv_badreports;	/* received invalid reports	   */
	u_int	igps_rcv_ourreports;	/* received reports for our groups */
	u_int	igps_snd_reports;	/* sent membership reports	   */
};

#ifdef _KERNEL
struct igmpstat igmpstat;

#define	IGMP_TIMEOUT_FREQUENCY	5	/* 5 times per second */
#define	IGMP_TIMEOUT_INTERVAL	(1000/IGMP_TIMEOUT_FREQUENCY)
					/* milliseconds */

/*
 * Macro to compute a random timer value between 1 and (IGMP_MAX_REPORTING_
 * DELAY * countdown frequency).  We generate a "random" number by adding
 * the millisecond clock, the address of the "first" interface on the machine,
 * and the multicast address being timed-out.  The 4.3 random() routine really
 * ought to be available in the kernel!
 */
#define	IGMP_RANDOM_DELAY(multiaddr, ipif)				\
	/* u_long multiaddr; ipif_t * ipif */				\
	((LBOLT_TO_MS(lbolt) +						\
	(ipif ? ipif->ipif_local_addr : 0) +				\
	multiaddr) %							\
	(IGMP_MAX_HOST_REPORT_DELAY * IGMP_TIMEOUT_FREQUENCY) + 1)

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _NETINET_IGMP_VAR_H */
