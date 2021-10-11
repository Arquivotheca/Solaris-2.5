/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)ip.c	1.79	95/03/01 SMI"

#ifndef	MI_HDRS
#include <sys/types.h>
#include <sys/stream.h>
#include <sys/dlpi.h>
#include <sys/stropts.h>
#include <sys/strlog.h>
#include <sys/tihdr.h>
#include <sys/tiuser.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

#include <sys/systm.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/vtrace.h>
#include <sys/isa_defs.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/route.h>
#include <sys/sockio.h>
#include <netinet/in.h>

#include <inet/common.h>
#include <inet/mi.h>
#include <inet/mib2.h>
#include <inet/nd.h>
#include <inet/arp.h>
#include <inet/snmpcom.h>

#include <netinet/igmp_var.h>

#else

#include <types.h>
#include <stream.h>
#include <dlpi.h>
#include <stropts.h>
#include <strlog.h>
#include <tihdr.h>
#include <tiuser.h>

#include <socket.h>
#include <vtrace.h>
#include <isa_defs.h>
#include <if.h>
#include <if_arp.h>
#include <route.h>
#include <sockio.h>
#include <in.h>

#include <common.h>
#include <mi.h>
#include <mib2.h>
#include <nd.h>
#include <arp.h>
#include <snmpcom.h>

#include <igmp_var.h>

#endif

/*
 * Synchronization notes:
 *
 * At all points in this code
 * where exclusive, writer, access is required, we pass a message to a
 * subroutine by invoking "become_writer" which will arrange to call the
 * routine only after all reader threads have exited the shared resource, and
 * the writer lock has been acquired.  For uniprocessor, single-thread,
 * nonpreemptive environments, become_writer can simply be a macro which
 * invokes the routine immediately.
 */
#undef become_writer
#define	become_writer(q, mp, func) become_exclusive(q, mp, func)

#define	IP_DEBUG	/**/

#define	IP_CSUM(mp, off, sum)		(~ip_cksum(mp, off, sum) & 0xFFFF) /**/
#define	IP_CSUM_PARTIAL(mp, off, sum)	ip_cksum(mp, off, sum)		/**/

#define	ILL_FRAG_HASH(s, i) ((s ^ (i ^ (i >> 8))) % ILL_FRAG_HASH_TBL_COUNT)
#define	ILL_FRAG_HASH_TBL_COUNT	((unsigned int)64)	/**/
#define	ILL_FRAG_HASH_TBL_SIZE	(ILL_FRAG_HASH_TBL_COUNT * sizeof (ipfb_t)) /**/

#define	IP_DEV_NAME			"/dev/ip"	/**/
#define	IP_MOD_NAME			"ip"		/**/
#define	IP_ADDR_LEN			4		/**/



#define	IP_ARP_PROTO_TYPE		0x0800		/**/

#define	IP_TCP_HASH(ip_src, ports)					\
		((unsigned)(ntohl(ip_src) ^ (ports >> 24) ^ (ports >> 16) \
		^ (ports > 8) ^ ports) % A_CNT(ipc_tcp_fanout))
#ifdef _BIG_ENDIAN
#define	IP_UDP_HASH(port)		((port) & 0xFF)
#else	/* _BIG_ENDIAN */
#define	IP_UDP_HASH(port)		((port) > 8)
#endif	/* _BIG_ENDIAN */

#define	TCP_MATCH(ipc, ipha, ports)					\
		((ipc)->ipc_tcp_ports == (ports) &&			\
		    (ipc)->ipc_tcp_faddr == (ipha)->ipha_src &&		\
		    (ipc)->ipc_tcp_laddr == (ipha)->ipha_dst)

#define	IP_UDP_MATCH(ipc, port, dst)					\
		(((ipc)->ipc_udp_port == (port)) &&			\
		    (((ipc)->ipc_udp_addr == 0) ||			\
			(ipc)->ipc_udp_addr == (dst)))

#define	IP_VERSION			4		/**/
#define	IP_SIMPLE_HDR_LENGTH_IN_WORDS	5		/**/
#define	IP_SIMPLE_HDR_LENGTH		20		/**/
#define	IP_MAX_HDR_LENGTH		64		/**/
#define	IP_SIMPLE_HDR_VERSION					/**/	\
	((IP_VERSION << 4) | IP_SIMPLE_HDR_LENGTH_IN_WORDS)

#define	IS_SIMPLE_IPH(ipha)						\
	((ipha)->ipha_version_and_hdr_length == IP_SIMPLE_HDR_VERSION)

#define	IP_IOCTL			(('i'<<8)|'p')	/**/
#define	IP_IOC_IRE_DELETE		4		/**/
#define	IP_IOC_IRE_DELETE_NO_REPLY	5		/**/
#define	IP_IOC_IRE_ADVISE_NO_REPLY	6		/**/

/** IP Options */
#define	IPOPT_EOL		0x00		/**/
#define	IPOPT_NOP		0x01		/**/
#define	IPOPT_SEC		0x82		/**/
#define	IPOPT_LSRR		0x83		/**/
#define	IPOPT_IT		0x44		/**/
#define	IPOPT_RR		0x07		/**/
#define	IPOPT_SID		0x88		/**/
#define	IPOPT_SSRR		0x89		/**/

/** Bits in the option value */
#define	IPOPT_COPY		0x80		/**/

/** IP option header indexes */
#define	IPOPT_POS_VAL		0		/**/
#define	IPOPT_POS_LEN		1		/**/
#define	IPOPT_POS_OFF		2		/**/
#define	IPOPT_POS_OV_FLG	3		/**/

/** Minimum for src and record route options */
#define	IPOPT_MINOFF_SR	4		/**/

/** Minimum for timestamp option */
#define	IPOPT_MINOFF_IT	5		/**/
#define	IPOPT_MINLEN_IT	5		/**/


/** Timestamp option flag bits */
#define	IPOPT_IT_TIME		0	/** Only timestamp */
#define	IPOPT_IT_TIME_ADDR	1	/** Timestamp + IP address */
#define	IPOPT_IT_SPEC		3	/** Only listed routers */
#define	IPOPT_IT_SPEC_BSD	2	/** Defined fopr BSD compat */

#define	IPOPT_IT_TIMELEN	4	/** Timestamp size */


/** Controls forwarding of IP packets, set via ndd */
#define	IP_FORWARD_NEVER	0			/**/
#define	IP_FORWARD_ALWAYS	1			/**/
#define	IP_FORWARD_AUTOMATIC	2			/**/

#ifdef	MI_HDRS
/* RFC1122 Conformance */
#define	IP_FORWARD_DEFAULT	IP_FORWARD_NEVER
#else
/* SunOS Compatibility */
#define	IP_FORWARD_DEFAULT	IP_FORWARD_AUTOMATIC
#endif

/* TODO: more interface-specific forwarding restrictions? */
#define	WE_ARE_FORWARDING					/**/	\
		(ip_g_forward == IP_FORWARD_ALWAYS ||			\
		    (ip_g_forward == IP_FORWARD_AUTOMATIC &&		\
		    ipif_g_count > 1))
/*
 * We don't use the iph in the following definition, but we might want
 * to some day.
 */
#define	IP_OK_TO_FORWARD_THRU(ipha, ire)				\
		(WE_ARE_FORWARDING)

#define	IPH_HDR_LENGTH(ipha)				/**/		\
	((int)(((iph_t *)ipha)->iph_version_and_hdr_length & 0xF) << 2) /**/

/**
 * IP reassembly macros.  We hide starting and ending offsets in b_next and
 * b_prev of messages on the reassembly queue.  The messages are chained using
 * b_cont.  These macros are used in ip_reassemble() so we don't have to see
 * the ugly casts and assignments.
 */
#define	IP_REASS_START(mp)		((u_long)((mp)->b_next))	/**/
#define	IP_REASS_SET_START(mp, u)	((mp)->b_next = (mblk_t *)(u))	/**/
#define	IP_REASS_END(mp)		((u_long)((mp)->b_prev))	/**/
#define	IP_REASS_SET_END(mp, u)		((mp)->b_prev = (mblk_t *)(u))	/**/

#define	ATOMIC_32_INC(u1, p)		(u1 = ++(*(p)))		/**/
#define	ATOMIC_32_INIT(au32p)		/**/			/**/
#define	ATOMIC_32_ASSIGN(au32p, val)	((au32p)[0] = (val))	/**/
#define	ATOMIC_32_VAL(au32p)		((au32p)[0])		/**/

/*
 * Use a wrapping int unless some hardware level
 * instruction offers true atomic inc on 32 bitters.
 * Some compilers have difficulty generating efficient
 * code for 32 bitters.
 */
#ifndef	A_U32			/**/
#define	A_U32	unsigned long	/**/
#endif				/**/

#define	IRE_HASH_TBL_COUNT	256			/**/
#define	INE_HASH_TBL_COUNT	32			/**/
#define	HASH_BIT_COUNT		8			/**/

#ifdef	_BIG_ENDIAN					/**/
#define	IRE_ADDR_HASH(l1)				/**/		\
		((u32)((l1 >> HASH_BIT_COUNT) ^ (l1))			\
		& ~(~0 << HASH_BIT_COUNT))
#define	IP_HDR_CSUM_TTL_ADJUST	256
#define	IP_TCP_CSUM_COMP	IPPROTO_TCP
#define	IP_UDP_CSUM_COMP	IPPROTO_UDP
#else							/**/
#define	IRE_ADDR_HASH(l1)				/**/		\
		((u32)((l1 << HASH_BIT_COUNT) ^ (l1)) >>		\
		(u32)(32 - HASH_BIT_COUNT))
#define	IP_HDR_CSUM_TTL_ADJUST	1
#define	IP_TCP_CSUM_COMP	(IPPROTO_TCP << 8)
#define	IP_UDP_CSUM_COMP	(IPPROTO_UDP << 8)
#endif							/**/

#define	INE_ADDR_HASH(l1)				/**/		\
		((((l1 >> 16) ^ (l1)) ^ (((l1 >> 16) ^ (l1)) >> 8))	\
		% INE_HASH_TBL_COUNT)

#define	ILL_MAX_NAMELEN		63

#ifndef	IRE_DB_TYPE		/**/
#define	IRE_DB_TYPE	M_SIG	/**/
#endif				/**/

#ifndef	IRE_DB_REQ_TYPE		/**/
#define	IRE_DB_REQ_TYPE	M_PCSIG	/**/
#endif				/**/

#define	IRE_IS_LOCAL(ire)						\
		((ire) && ((ire)->ire_type & (IRE_LOCAL|IRE_LOOPBACK)))

#define	IRE_IS_TARGET(ire)	((ire) && ((ire)->ire_type !=	/**/	\
				IRE_BROADCAST))

#define	IREP_LOOKUP(addr)	&ire_hash_tbl[IRE_ADDR_HASH(addr)]	/**/
#define	IREP_ASSOC_LOOKUP(addr)	&ire_assoc_hash_tbl[IRE_ADDR_HASH(addr)]/**/

/* Privilege check for an IP instance of unknown type.  wq is write side. */
#define	IS_PRIVILEGED_QUEUE(wq)						\
		((wq)->q_next ? (((ill_t *)(wq)->q_ptr)->ill_priv_stream) \
		: (((ipc_t *)(wq)->q_ptr)->ipc_priv_stream))

/** IP Fragmentation Reassembly Header */
typedef struct ipf_s {			/**/
	struct ipf_s	* ipf_hash_next;
	struct ipf_s	** ipf_ptphn;	/* Pointer to previous hash next. */
	u16	ipf_ident;	/* Ident to match. */
	u8	ipf_protocol;	/* Protocol to match. */
	u32	ipf_src;	/* Source address. */
	u_long	ipf_timestamp;	/* Reassembly start time. */
	mblk_t	* ipf_mp;	/* mblk we live in. */
	mblk_t	* ipf_tail_mp;	/* Frag queue tail pointer. */
	int	ipf_hole_cnt;	/* Number of holes (hard-case). */
	int	ipf_end;	/* Tail end offset (0 implies hard-case). */
	int	ipf_stripped_hdr_len;	/* What we've stripped from the
					 * lead mblk. */
	u16	ipf_checksum;	/* Partial checksum of fragment data */
	u16	ipf_checksum_valid;
	u_long	ipf_gen;	/* Frag queue generation */
	u_long	ipf_count;	/* Count of bytes used by frag */
} ipf_t;

/** IP packet count structure */
typedef struct ippc_s {			/**/
	u32	ippc_addr;
	u_long	ippc_ib_pkt_count;
	u_long	ippc_ob_pkt_count;
	u_long	ippc_fo_pkt_count;
} ippc_t;

/** ICMP types */
#define	ICMP_ECHO_REPLY			0	/**/
#define	ICMP_DEST_UNREACHABLE		3	/**/
#define	ICMP_SOURCE_QUENCH		4	/**/
#define	ICMP_REDIRECT			5	/**/
#define	ICMP_ECHO_REQUEST		8	/**/
#define	ICMP_ROUTER_ADVERTISEMENT	9	/**/
#define	ICMP_ROUTER_SOLICITATION	10	/**/
#define	ICMP_TIME_EXCEEDED		11	/**/
#define	ICMP_PARAM_PROBLEM		12	/**/
#define	ICMP_TIME_STAMP_REQUEST		13	/**/
#define	ICMP_TIME_STAMP_REPLY		14	/**/
#define	ICMP_INFO_REQUEST		15	/**/
#define	ICMP_INFO_REPLY			16	/**/
#define	ICMP_ADDRESS_MASK_REQUEST	17	/**/
#define	ICMP_ADDRESS_MASK_REPLY		18	/**/

/** ICMP_TIME_EXCEEDED codes */
#define	ICMP_TTL_EXCEEDED		0	/**/
#define	ICMP_REASSEMBLY_TIME_EXCEEDED	1	/**/

/** ICMP_DEST_UNREACHABLE codes */
#define	ICMP_NET_UNREACHABLE		0	/**/
#define	ICMP_HOST_UNREACHABLE		1	/**/
#define	ICMP_PROTOCOL_UNREACHABLE	2	/**/
#define	ICMP_PORT_UNREACHABLE		3	/**/
#define	ICMP_FRAGMENTATION_NEEDED	4	/**/
#define	ICMP_SOURCE_ROUTE_FAILED	5	/**/

/** ICMP Header Structure */
typedef struct icmph_s {		/**/
	u_char	icmph_type;
	u_char	icmph_code;
	u16	icmph_checksum;
	union {
		struct { /* ECHO request/response structure */
			u16	u_echo_ident;
			u16	u_echo_seqnum;
		} u_echo;
		struct { /* Destination unreachable structure */
			u16	u_du_zero;
			u16	u_du_mtu;
		} u_du;
		struct { /* Parameter problem structure */
			u8	u_pp_ptr;
			u8	u_pp_rsvd[3];
		} u_pp;
		struct { /* Redirect structure */
			u32	u_rd_gateway;
		} u_rd;
	} icmph_u;
} icmph_t;
#define	icmph_echo_ident	icmph_u.u_echo.u_echo_ident	/**/
#define	icmph_echo_seqnum	icmph_u.u_echo.u_echo_seqnum	/**/
#define	icmph_du_zero		icmph_u.u_du.u_du_zero		/**/
#define	icmph_du_mtu		icmph_u.u_du.u_du_mtu		/**/
#define	icmph_pp_ptr		icmph_u.u_pp.u_pp_ptr		/**/
#define	icmph_rd_gateway	icmph_u.u_rd.u_rd_gateway	/**/
#define	ICMPH_SIZE	8		/**/

/** Unaligned IP header */
typedef struct iph_s {			/**/
	u_char	iph_version_and_hdr_length;
	u_char	iph_type_of_service;
	u_char	iph_length[2];
	u_char	iph_ident[2];
	u_char	iph_fragment_offset_and_flags[2];
	u_char	iph_ttl;
	u_char	iph_protocol;
	u_char	iph_hdr_checksum[2];
	u_char	iph_src[4];
	u_char	iph_dst[4];
} iph_t;

/** Aligned IP header */
typedef struct ipha_s {			/**/
	u_char	ipha_version_and_hdr_length;
	u_char	ipha_type_of_service;
	u16	ipha_length;
	u16	ipha_ident;
	u16	ipha_fragment_offset_and_flags;
	u_char	ipha_ttl;
	u_char	ipha_protocol;
	u16	ipha_hdr_checksum;
	u32	ipha_src;
	u32	ipha_dst;
} ipha_t;

#define	IPH_DF		0x4000	/** Don't fragment */
#define	IPH_MF		0x2000	/** More fragments to come */
#define	IPH_OFFSET	0x1FFF	/** Where the offset lives */

/** IP Mac info structure */
typedef struct ip_m_s {			/**/
	u_long	ip_m_mac_type;
	u_long	ip_m_sap;
	int	ip_m_sap_length;
	u_long	ip_m_brdcst_addr_length;
	u_char	* ip_m_brdcst_addr;
} ip_m_t;

/** Internet Routing Entry */
#ifdef _KERNEL				/**/
typedef struct ire_s {			/**/
	struct	ire_s	* ire_next;	/* The hash chain must be first. */
	struct	ire_s	** ire_ptpn;	/* Pointer to previous next. */
	mblk_t	* ire_mp;		/* mblk we are in. */
	mblk_t	* ire_ll_hdr_mp;	/* Resolver cookie for this IRE. */
	queue_t	* ire_rfq;		/* recv from this queue */
	queue_t	* ire_stq;		/* send to this queue */
	u32	ire_src_addr;		/* Source address to use. */
	u_int	ire_max_frag;		/* MTU (next hop or path). */
	u32	ire_frag_flag;		/* IPH_DF or zero. */
	A_U32	ire_atomic_ident;	/* Per IRE IP ident. */
	u32	ire_tire_mark;		/* Saved ident for overtime parking. */
	u32	ire_mask;		/* Mask for matching this IRE. */
	u32	ire_addr;		/* Address this IRE represents. */
	u32	ire_gateway_addr;	/* Gateway address (if IRE_GATEWAY). */
	u_int	ire_type;
	u_long	ire_rtt;		/* Guestimate in millisecs */
	u_long	ire_ib_pkt_count;	/* Inbound packets for ire_addr */
	u_long	ire_ob_pkt_count;	/* Outbound packets to ire_addr */
	u_int	ire_ll_hdr_length;	/* Non-zero if we do M_DATA prepends */
	u32	ire_create_time;	/* Time (in secs) IRE was created. */
	mblk_t	* ire_ll_hdr_saved_mp;	/* For SIOCGARP whe using fastpath */
	kmutex_t ire_ident_lock;	/* Protects ire_atomic_ident */
} ire_t;
#endif					/**/

/** Router entry types */
#define	IRE_BROADCAST		0x0001	/**/
#define	IRE_GATEWAY		0x0002	/**/
#define	IRE_LOCAL		0x0004	/**/
#define	IRE_LOOPBACK		0x0008	/**/
#define	IRE_NET			0x0010	/**/
#define	IRE_ROUTE		0x0020	/**/
#define	IRE_SUBNET		0x0040	/**/
#define	IRE_RESOLVER		0x0080	/**/
#define	IRE_ROUTE_ASSOC		0x0100	/**/
#define	IRE_ROUTE_REDIRECT	0x0200	/**/
#define	IRE_INTERFACE		(IRE_SUBNET|IRE_RESOLVER)	/**/

/* Leave room for ip_newroute to tack on the src and target addresses */
#define	OK_RESOLVER_MP(mp)						\
		((mp) && ((mp)->b_wptr - (mp)->b_rptr) >= (2 * IP_ADDR_LEN))

/** Group membership list per upper ill */
typedef struct ilg_s {			/**/
	u32		ilg_group;
	struct ipif_s	* ilg_lower;	/* Interface we are member on */
} ilg_t;

/** Multicast address list entry for lower ill */
typedef struct ilm_s {			/**/
	u32		ilm_addr;
	int		ilm_refcnt;
	u_int		ilm_timer;	/* IGMP */
	struct ipif_s	* ilm_ipif;	/* Back pointer to ipif */
	struct ilm_s	* ilm_next;	/* Linked list for each ill */
} ilm_t;

/** IP client structure, allocated when ip_open indicates a STREAM device */
#ifdef	_KERNEL				/**/
typedef struct ipc_s {			/**/
	struct	ipc_s	* ipc_hash_next;/* Hash chain must be first */
	struct	ipc_s	** ipc_ptphn;	/* Pointer to previous hash next. */
	queue_t	* ipc_rq;
	queue_t	* ipc_wq;
	
	/* Protocol specific fanout information union. */
	union {
		struct {
			u_int	ipcu_udp_port; /* UDP port number. */
			u32	ipcu_udp_addr; /* Local IP address. */
		} ipcu_udp;

		struct {
			u32	ipcu_tcp_laddr;
			u32	ipcu_tcp_faddr;
			u32	ipcu_tcp_ports; /* Rem port, local port */
		} ipcu_tcp;
	} ipc_ipcu;
#define	ipc_udp_port	ipc_ipcu.ipcu_udp.ipcu_udp_port
#define	ipc_udp_addr	ipc_ipcu.ipcu_udp.ipcu_udp_addr
#define	ipc_tcp_laddr	ipc_ipcu.ipcu_tcp.ipcu_tcp_laddr
#define	ipc_tcp_faddr	ipc_ipcu.ipcu_tcp.ipcu_tcp_faddr
#define	ipc_tcp_ports	ipc_ipcu.ipcu_tcp.ipcu_tcp_ports

	ilg_t	* ipc_ilg;		/* Group memberships */
	int	ipc_ilg_allocated;	/* Number allocated */
	int	ipc_ilg_inuse;		/* Number currently used */
	struct ipif_s	* ipc_multicast_ipif;	/* IP_MULTICAST_IF */
	uint	ipc_multicast_ttl;	/* IP_MULTICAST_TTL */
	unsigned int
		ipc_dontroute : 1,	/* SO_DONTROUTE state */
		ipc_loopback : 1,	/* SO_LOOPBACK state */
		ipc_broadcast : 1,	/* SO_BROADCAST state */
		ipc_looping : 1,	/* Semaphore (phooey) */
		
		ipc_reuseaddr : 1,	/* SO_REUSEADDR state */
		ipc_multicast_loop : 1,	/* IP_MULTICAST_LOOP */
		ipc_multi_router : 1,	/* Wants all multicast packets */
		ipc_priv_stream : 1,	/* Privileged client? */
		
		ipc_draining : 1,	/* ip_wsrv running */
		ipc_did_putbq : 1,	/* ip_wput did a putbq */

		ipc_pad_to_bit_31 : 22;
	MI_HRT_DCL(ipc_wtime)
	MI_HRT_DCL(ipc_wtmp)
} ipc_t;
#endif	/**_KERNEL*/

/* IP interface structure, one per local address */
typedef struct ipif_s {			/**/
	struct ipif_s	* ipif_next;
	struct ill_s	* ipif_ill;	/* Back pointer to our ill */
	long	ipif_id;		/* Logical unit number */
	u_int	ipif_mtu;		/* Starts at ipif_ill->ill_max_frag */
	u32	ipif_local_addr;	/* Local IP address for this if. */
	u32	ipif_net_mask;		/* Net mask for this interface. */
	u32	ipif_broadcast_addr;	/* Broadcast addr for this interface. */
	u32	ipif_pp_dst_addr;	/* Point-to-point dest address. */
	u_int	ipif_flags;		/* Interface flags. */
	u_int	ipif_metric;		/* BSD if metric, for compatibility. */
	u_int	ipif_ire_type;		/* LOCAL or LOOPBACK */
	mblk_t	* ipif_arp_down_mp;	/* Allocated at time arp comes up to
					 * prevent awkward out of mem condition
					 * later
					 */
	mblk_t	* ipif_saved_ire_mp;	/* Allocated for each extra IRE_SUBNET/
					 * RESOLVER on this interface so that
					 * they can survive ifconfig down.
					 */
	/*
	 * The packet counts in the ipif contain the sum of the
	 * packet counts in dead IREs that were affiliated with
	 * this ipif.
	 */
	u_long	ipif_fo_pkt_count;	/* Forwarded thru our dead IREs */
	u_long	ipif_ib_pkt_count;	/* Inbound packets for our dead IREs */
	u_long	ipif_ob_pkt_count;	/* Outbound packets to our dead IREs */
	unsigned int
		ipif_multicast_up : 1,	/* We have joined the allhosts group */
		: 0;
} ipif_t;
/* Macros for easy backreferences to the ill. */
#define	ipif_wq			ipif_ill->ill_wq		/**/
#define	ipif_rq			ipif_ill->ill_rq		/**/
#define	ipif_subnet_type	ipif_ill->ill_subnet_type	/**/
#define	ipif_resolver_mp	ipif_ill->ill_resolver_mp	/**/
#define	ipif_ipif_up_count	ipif_ill->ill_ipif_up_count	/**/
#define	ipif_ipif_pending	ipif_ill->ill_ipif_pending	/**/
#define	ipif_bcast_mp		ipif_ill->ill_bcast_mp		/**/

/*
 * Fragmentation hash bucket
 */
typedef struct ipfb_s {			/**/
	struct ipf_s	* ipfb_ipf;	/* List of ... */
	u_long		ipfb_count;	/* Count of bytes used by frag(s) */
	kmutex_t	ipfb_lock;	/* Protect all ipf in list */
} ipfb_t;

/*
 * IP Lower level Structure.
 * Instance data structure in ip_open when there is a device below us.
 */
typedef struct ill_s {			/**/
	struct	ill_s	* ill_next;	/* Chained in at ill_g_head. */
	struct	ill_s	** ill_ptpn;	/* Pointer to previous next. */
	queue_t	* ill_rq;		/* Read queue. */
	queue_t	* ill_wq;		/* Write queue. */

	int	ill_error;		/* Error value sent up by device. */

	ipif_t	* ill_ipif;		/* Interface chain for this ILL. */
	u_int	ill_ipif_up_count;	/* Number of IPIFs currently up. */
	u_int	ill_max_frag;		/* Max IDU. */
	char	* ill_name;		/* Our name. */
	u_int	ill_name_length;	/* Name length, incl. terminator. */
	u_int	ill_subnet_type;	/* IRE_RESOLVER or IRE_SUBNET. */
	u_int	ill_ppa;		/* Physical Point of Attachment num. */
	u_long	ill_sap;
	int	ill_sap_length;		/* Including sign (for position) */
	u_int	ill_phys_addr_length;	/* Excluding the sap. */
	mblk_t	* ill_frag_timer_mp;	/* Reassembly timer state. */
	ipfb_t	* ill_frag_hash_tbl;	/* Fragment hash list head. */

	queue_t	* ill_bind_pending_q;	/* Queue waiting for DL_BIND_ACK. */
	ipif_t	* ill_ipif_pending;	/* IPIF waiting for DL_BIND_ACK. */

	/* ill_hdr_length and ill_hdr_mp will be non zero if
	 * the underlying device supports the M_DATA fastpath
	 */
	int	ill_hdr_length;

	ilm_t	* ill_ilm;		/* Multicast mebership for lower ill */

	/* All non-nil cells between 'ill_first_mp_to_free' and
	 * 'ill_last_mp_to_free' are freed in ill_delete.
	 */
#define	ill_first_mp_to_free	ill_hdr_mp
	mblk_t	* ill_hdr_mp;		/* Contains fastpath template */
	mblk_t	* ill_bcast_mp;		/* DLPI header for broadcasts. */
	mblk_t	* ill_bind_pending;	/* T_BIND_REQ awaiting completion. */
	mblk_t	* ill_resolver_mp;	/* Resolver template. */
	mblk_t	* ill_attach_mp;
	mblk_t	* ill_bind_mp;
	mblk_t	* ill_unbind_mp;
	mblk_t	* ill_detach_mp;
#define	ill_last_mp_to_free	ill_detach_mp

	u_int
		ill_frag_timer_running : 1,
		ill_needs_attach : 1,
		ill_is_ptp : 1,
		ill_priv_stream : 1,
		ill_unbind_pending : 1,

		ill_pad_to_bit_31 : 27;
	MI_HRT_DCL(ill_rtime)
	MI_HRT_DCL(ill_rtmp)

	/*
	 * Used in SIOCSIFMUXID and SIOCGIFMUXID for 'ifconfig unplumb'.
	 */
	int	ill_arp_muxid;		/* muxid returned from plink for arp */
	int	ill_ip_muxid;		/* muxid returned from plink for ip */
	/*
	 * Used for IP frag reassembly throttling on a per ILL basis.
	 *
	 * Note: frag_count is approximate, its added to and subtracted from
	 *	 without any locking, so simultaneous load/modify/stores can
	 *	 collide, also ill_frag_purge() recalculates its value by
	 *	 summing all the ipfb_count's without locking out updates
	 *	 to the ipfb's.
	 */
	u_long	ill_ipf_gen;		/* Generation of next fragment queue */
	u_long	ill_frag_count;		/* Approx count of all mblk bytes */
} ill_t;

/* Named Dispatch Parameter Management Structure */
typedef struct ipparam_s {	/**/
	u_long	ip_param_min;
	u_long	ip_param_max;
	u_long	ip_param_value;
	char	* ip_param_name;
} ipparam_t;

/** Common fields for IP IOCTLs. */
typedef struct ipllcmd_s {	/**/
	u_long	ipllc_cmd;
	u_long	ipllc_name_offset;
	u_long	ipllc_name_length;
} ipllc_t;

/** IP IRE Change Command Structure. */
typedef struct ipic_s {		/**/
	ipllc_t	ipic_ipllc;
	u_long	ipic_ire_type;
	u_long	ipic_max_frag;
	u_long	ipic_addr_offset;
	u_long	ipic_addr_length;
	u_long	ipic_mask_offset;
	u_long	ipic_mask_length;
	u_long	ipic_src_addr_offset;
	u_long	ipic_src_addr_length;
	u_long	ipic_ll_hdr_offset;
	u_long	ipic_ll_hdr_length;
	u_long	ipic_gateway_addr_offset;
	u_long	ipic_gateway_addr_length;
	u_long	ipic_rtt;
} ipic_t;
#define	ipic_cmd		ipic_ipllc.ipllc_cmd		/**/
#define	ipic_ll_name_length	ipic_ipllc.ipllc_name_length	/**/
#define	ipic_ll_name_offset	ipic_ipllc.ipllc_name_offset	/**/

/** IP IRE Delete Command Structure. */
typedef struct ipid_s {		/**/
	ipllc_t	ipid_ipllc;
	u_long	ipid_ire_type;
	u_long	ipid_addr_offset;
	u_long	ipid_addr_length;
	u_long	ipid_mask_offset;
	u_long	ipid_mask_length;
} ipid_t;
#define	ipid_cmd		ipid_ipllc.ipllc_cmd		/**/

/** IP Address Structure. */
typedef struct ipaddr_s {					/**/
	u16	ip_family;		/* Presumably AF_INET. */
	u_char	ip_port[2];		/* Port in net-byte order. */
	u_char	ip_addr[4];		/* Internet addr. in net-byte order. */
	u_char	ip_pad[8];
} ipa_t;

/** Name/Value Descriptor. */
typedef struct nv_s {	/**/
	int	nv_value;
	char	* nv_name;
} nv_t;

/** IP Forwarding Ticket */
typedef	struct ipftk_s {	/**/
	queue_t	* ipftk_queue;
	u32	ipftk_dst;
} ipftk_t;
#define	IPFT_EXTERNAL_ORIGIN	1	/* Marker destination */

typedef	struct iocblk	* IOCP;

/* Can't include these until after we have all the typedefs. */
#ifndef	MI_HDRS
#include <netinet/igmp.h>
#include <inet/ip_multi.h>
#include <inet/ip_if.h>
#include <inet/ip_ire.h>
#include <netinet/ip_mroute.h>
#else
#include <igmp.h>
#include <ip_multi.h>
#include <ip_if.h>
#include <ip_ire.h>
#include <ip_mroute.h>
#endif

staticf void	icmp_frag_needed(   queue_t * q, mblk_t * mp, int code   );
staticf void	icmp_inbound(   queue_t * q, mblk_t * mp, int ire_type,
			     ipif_t *ipif, int sum_valid, u32 sum   );
staticf	void	icmp_inbound_error(   queue_t * q, mblk_t * mp   );
staticf void	icmp_options_update (   ipha_t * ipha   );
staticf	void	icmp_param_problem(   queue_t * q, mblk_t * mp, int code   );
staticf	void	icmp_pkt(   queue_t * q, mblk_t * mp, char * stuff, int len   );
staticf	mblk_t	* icmp_pkt_err_ok(   mblk_t * mp   );
staticf void	icmp_redirect(   queue_t * q, mblk_t * mp   );
staticf void	icmp_send_redirect(   queue_t * q, mblk_t *mp, u32 gateway   );
staticf void	icmp_source_quench(   queue_t * q, mblk_t * mp   );
	void	icmp_time_exceeded(   queue_t * q, mblk_t * mp, int code   );
staticf	void	icmp_unreachable(   queue_t * q, mblk_t * mp, int code   );

staticf	void	igmp_timeout(   void   );
	
staticf	void	ip_arp_news(   queue_t * q, mblk_t * mp   );
staticf void	ip_bind(   queue_t * q, mblk_t * mp   );
staticf mblk_t	* ip_carve_mp(   mblk_t ** mpp, int len   );
extern	u_int	ip_cksum(   mblk_t * mp, int offset, u32 prev_sum   );
extern	u_int	ip_cksum_partial(   mblk_t * mp, int offset, u32 prev_sum   );
staticf	int	ip_close(   queue_t * q   );
extern	u16	ip_csum(   mblk_t * mp, int offset, u32 prev_sum   );
extern	u16	ip_csum_partial(   mblk_t * mp, int offset, u32 prev_sum   );
	u_int	ip_csum_hdr(   ipha_t * ipha   );
	mblk_t	* ip_dlpi_alloc(   int len, long prim   );
	char	* ip_dot_addr(   u32 addr, char * buf   );
staticf char	* ip_dot_saddr(   u_char * addr, char * buf   );
#if 0
	mblk_t	* ip_forwarding_ticket(   queue_t * q, u32 dst   );
#endif
staticf	int	ip_hdr_complete (ipha_t * ipha);
staticf void	ip_lrput(   queue_t * q, mblk_t * mp   );
staticf void	ip_lwput(   queue_t * q, mblk_t * mp   );
	u32	ip_massage_options(   ipha_t * ipha   );
	u32	ip_net_mask(   u32 addr   );
staticf void	ip_newroute(   queue_t * q, mblk_t * mp, u32 dst   );
staticf	void	ip_newroute_ipif (queue_t * q, mblk_t * mp, ipif_t * ipif,
					u32 dst);
#if 0
staticf	void	ip_nothing(   void   );
#endif
	char	* ip_nv_lookup(   nv_t * nv, int value   );
staticf	int	ip_open(   queue_t * q, dev_t * devp, int flag, int sflag,
			cred_t * credp   );
staticf	void	ip_optmgmt_req(   queue_t * q, mblk_t * mp   );
staticf	int	ip_optmgmt_writer(   mblk_t * mp   );
staticf	void	ip_param_cleanup(   void   );
staticf int	ip_param_get(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf boolean_t	ip_param_register(   ipparam_t * ippa, int cnt   );
staticf int	ip_param_set(   queue_t * q, mblk_t * mp, char * value,
			caddr_t cp   );
staticf boolean_t	ip_reassemble(   mblk_t * mp, ipf_t * ipf,
			u32 offset_and_flags, int stripped_hdr_len   );
	void	ip_rput(   queue_t * q, mblk_t * mp   );
staticf void	ip_rput_dlpi(   queue_t * q, mblk_t * mp   );
staticf void	ip_rput_dlpi_writer(   queue_t * q, mblk_t * mp   );
	void	ip_rput_forward(   ire_t * ire, ipha_t * ipha, mblk_t * mp   );
staticf int	ip_rput_forward_options(   mblk_t * mp, ipha_t * ipha, 
					ire_t *ire   );
	void	ip_rput_local(   queue_t * q, mblk_t * mp, ipha_t * ipha,
			ire_t * ire   );
staticf int	ip_rput_local_options(   queue_t *q, mblk_t *mp, 
				      ipha_t * ipha, ire_t *ire   );
staticf u32	ip_rput_options(   queue_t *q, mblk_t *mp, ipha_t * ipha   );
staticf void	ip_rput_other(   queue_t * q, mblk_t * mp   );
staticf void	ip_rsrv(   queue_t * q   );
staticf	int	ip_snmp_get(   queue_t * q, mblk_t * mpctl   );
staticf void	ip_snmp_get2(   ire_t * ire, mblk_t ** mpdata   );
staticf	int	ip_snmp_set(   queue_t * q, int level, int name, u_char * ptr,
			       int len   );
staticf boolean_t	ip_source_routed(   ipha_t * ipha   );
staticf boolean_t	ip_source_route_included(   ipha_t * ipha   );
#ifdef	MI_HRTIMING
staticf	int	ip_time_flush(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf	int	ip_time_report(   queue_t * q, mblk_t * mp, caddr_t cp   );
staticf	int	ip_time_reset(   queue_t * q, mblk_t * mp, caddr_t cp   );
#endif
staticf	void	ip_trash(   queue_t * q, mblk_t * mp   );
staticf	void	ip_unbind(   queue_t * q, mblk_t * mp   );
	void	ip_wput(   queue_t * q, mblk_t * mp   );
	void	ip_wput_ire(   queue_t * q, mblk_t * mp, ire_t *ire   );
staticf void	ip_wput_frag(   ire_t * ire, mblk_t * mp, u_long *pkt_count, 
			     u32 max_frag, u32 frag_flag);
staticf mblk_t 	* ip_wput_frag_copyhdr (   u_char * rptr, int hdr_len, 
					int offset   );
	void	ip_wput_local(   queue_t * q, ipha_t * ipha, mblk_t * mp,
			int ire_type, queue_t * rq   );
staticf	void	ip_wput_local_options(   ipha_t * ipha    );
staticf void	ip_wput_nondata(   queue_t * q, mblk_t * mp   );
staticf int	ip_wput_options(   queue_t *q, mblk_t *mp, ipha_t * ipha   );
	void	ip_wsrv(   queue_t * q   );

staticf	void	ipc_hash_insert_first(   ipc_t ** ipcp, ipc_t * ipc   );
staticf	void	ipc_hash_insert_last(   ipc_t ** ipcp, ipc_t * ipc   );
staticf	void	ipc_hash_remove(   ipc_t * ipc   );

staticf	void	ipc_qenable(   ipc_t * ipc, caddr_t arg   );
	void	ipc_walk(   pfv_t func, caddr_t arg   );
staticf	void	ipc_walk_nontcp(   pfv_t func, caddr_t arg   );
staticf boolean_t	ipc_wantpacket(   ipc_t * ipc, u32 dst   );
	void	ill_frag_prune(   ill_t * ill, u_long max_count   );

	struct ill_s * ill_g_head;	/* ILL List Head */

	ill_t	* ip_timer_ill;		/* ILL for IRE expiration timer. */
	mblk_t	* ip_timer_mp;		/* IRE expiration timer. */
	u_long	ip_ire_time_elapsed;	/* Time since IRE cache last flushed */
static	u_long	ip_ire_rd_time_elapsed;	/* Time since redirect IREs last flushed */
static	u_long	ip_ire_pmtu_time_elapsed;/* Time since path mtu increase */

	ill_t	* igmp_timer_ill;	/* ILL for IGMP timer. */
	mblk_t	* igmp_timer_mp;	/* IGMP timer */
	int	igmp_timer_interval = IGMP_TIMEOUT_INTERVAL;

	ire_t	* ip_def_gateway;	/* Default gateway list head. */
	u_int	ip_def_gateway_count;	/* Number of gateways. */
	u_int	ip_def_gateway_index;	/* Walking index used to mod in */
	u32	ip_g_all_ones = (u32)~0;
	u_long	icmp_pkt_err_last = 0;	/* Time since last icmp_pkt_err */

/* How long, in seconds, we allow frags to hang around. */
#define	IP_FRAG_TIMEOUT	60
static	u_long	ip_g_frag_timeout = IP_FRAG_TIMEOUT;
static	u_long	ip_g_frag_timo_ms = IP_FRAG_TIMEOUT * 1000;

static	void	* ip_g_head;		/* Instance Data List Head */
	caddr_t	ip_g_nd;		/* Named Dispatch List Head */

static	long	ip_rput_pullups;

int	ip_max_mtu;			/** Used by udp/icmp */

/*
 * MIB-2 stuff for SNMP (both IP and ICMP )
 */
	mib2_ip_t	ip_mib;
static	mib2_icmp_t	icmp_mib;

ulong_t	loopback_packets = 0;	/* loopback interface statistics */

/*
 * Named Dispatch Parameter Table.
 * All of these are alterable, within the min/max values given, at run time.
 */
static	ipparam_t	lcl_param_arr[] = {
	/* min	max	value	name */
	{  0,	2,	IP_FORWARD_DEFAULT,	"ip_forwarding"},
	{  0,	1,	0,	"ip_respond_to_address_mask_broadcast"},
	{  0,	1,	1,	"ip_respond_to_echo_broadcast"},
	{  0,	1,	1,	"ip_respond_to_timestamp"},
	{  0,	1,	1,	"ip_respond_to_timestamp_broadcast"},
	{  0,	1,	1,	"ip_send_redirects"},
	{  0,	1,	1,	"ip_forward_directed_broadcasts"},
	{  0,	10,	0,	"ip_debug"},
	{  0,	10,	0,	"ip_mrtdebug"},
	{  5000, 999999999,    30000, "ip_ire_cleanup_interval" },
	{  60000, 999999999, 1200000, "ip_ire_flush_interval" },
	{  60000, 999999999,   60000, "ip_ire_redirect_interval" },
	{  1,	255,	255,	"ip_def_ttl" },
	{  0,	1,	1,	"ip_forward_src_routed"},
	{  0,	256,	32,	"ip_wroff_extra" },
	{  5000, 999999999, 600000, "ip_ire_pathmtu_interval" },
	{  8,	65536,  64,	"ip_icmp_return_data_bytes" },
	{  0,	1,	0,	"ip_send_source_quench" },
	{  0,	1,	1,	"ip_path_mtu_discovery" },
	{  0,	240,	30,	"ip_ignore_delete_time" },
	{  0,	1,	0,	"ip_ignore_redirect" },
	{  0,	1,	1,	"ip_output_queue" },
	{  1,	254,	1,	"ip_broadcast_ttl" },
	{  0,	99999,	500,	"ip_icmp_err_interval" },
	{  0,	999999999,	1000000, "ip_reass_queue_bytes" },
	{  0,	1,	0,	"ip_strict_dst_multihoming" },
};
	ipparam_t	* ip_param_arr = lcl_param_arr;

/*
 * ip_g_forward controls IP forwarding.  It takes three values:
 * 	0: IP_FORWARD_NEVER	Don't forward packets ever.
 *	1: IP_FORWARD_ALWAYS	Forward packets for elsewhere.
 *	2: IP_FORWARD_AUTOMATIC	Forward packets if more than one interface
 *				is configured, otherwise, do not forward.
 * RFC1122 says there must be a configuration switch to control forwarding,
 * but that the default MUST be to not forward packets ever.  Implicit
 * control based on configuration of multiple interfaces MUST NOT be
 * implemented (Section 3.1).  SunOS 4.1 did provide the "automatic" capability
 * and, in fact, it was the default.  For now, Sun will maintain compatability
 * with the old behavior, while the rest of us will follow RFC1122.
 */
#define	ip_g_forward			ip_param_arr[0].ip_param_value	/**/

#define	ip_respond_to_address_mask_broadcast ip_param_arr[1].ip_param_value /**/
#define	ip_g_resp_to_echo_bcast		ip_param_arr[2].ip_param_value
#define	ip_g_resp_to_timestamp		ip_param_arr[3].ip_param_value
#define	ip_g_resp_to_timestamp_bcast	ip_param_arr[4].ip_param_value
#define	ip_g_send_redirects		ip_param_arr[5].ip_param_value
#define	ip_g_forward_directed_bcast	ip_param_arr[6].ip_param_value
#define	ip_debug			ip_param_arr[7].ip_param_value	/**/
#define ip_mrtdebug			ip_param_arr[8].ip_param_value	/**/
#define	ip_timer_interval		ip_param_arr[9].ip_param_value	/**/
#define	ip_ire_flush_interval		ip_param_arr[10].ip_param_value /**/
#define	ip_ire_redir_interval		ip_param_arr[11].ip_param_value
#define	ip_def_ttl			ip_param_arr[12].ip_param_value
#define	ip_forward_src_routed		ip_param_arr[13].ip_param_value
#define	ip_wroff_extra			ip_param_arr[14].ip_param_value
#define	ip_ire_pathmtu_interval		ip_param_arr[15].ip_param_value
#define	ip_icmp_return			ip_param_arr[16].ip_param_value
#define	ip_send_source_quench		ip_param_arr[17].ip_param_value
#define	ip_path_mtu_discovery		ip_param_arr[18].ip_param_value /**/
#define	ip_ignore_delete_time		ip_param_arr[19].ip_param_value /**/
#define	ip_ignore_redirect		ip_param_arr[20].ip_param_value
#define	ip_output_queue			ip_param_arr[21].ip_param_value
#define	ip_broadcast_ttl		ip_param_arr[22].ip_param_value
#define	ip_icmp_err_interval		ip_param_arr[23].ip_param_value
#define	ip_reass_queue_bytes		ip_param_arr[24].ip_param_value
#define	ip_strict_dst_multihoming	ip_param_arr[25].ip_param_value

/*
 * Network byte order macros
 */
#ifdef	_BIG_ENDIAN					/**/
#define	N_IN_CLASSD_NET		IN_CLASSD_NET		/**/
#define	N_INADDR_UNSPEC_GROUP	INADDR_UNSPEC_GROUP	/**/
#else /* _BIG_ENDIAN */					/**/
#define	N_IN_CLASSD_NET		0x000000f0		/**/
#define	N_INADDR_UNSPEC_GROUP	0x000000e0		/**/
#endif /* _BIG_ENDIAN */				/**/
#define CLASSD(addr)	(((addr) & N_IN_CLASSD_NET) == N_INADDR_UNSPEC_GROUP) /**/

#ifdef IP_DEBUG						/**/
#define ip0dbg(a)	printf a 			/**/
#define ip1dbg(a)	if (ip_debug) printf a		/**/
#define ip2dbg(a)	if (ip_debug>1) printf a	/**/
#define ip3dbg(a)	if (ip_debug>2) printf a	/**/
#else							/**/
#define	ip0dbg(a)	/* */				/**/
#define	ip1dbg(a)	/* */				/**/
#define	ip2dbg(a)	/* */				/**/
#define	ip3dbg(a)	/* */				/**/
#endif IP_DEBUG						/**/

static	ipc_t	* ipc_tcp_fanout[256];		/* TCP fanout hash list. */
static	ipc_t	* ipc_udp_fanout[256];		/* UDP fanout hash list. */
static	ipc_t	* ipc_proto_fanout[256];	/* Misc. fanout hash list. */

	u_int	ipif_g_count;			/* Count of IPIFs "up". */

	/* Main IRE hash table. */
	ire_t	* ire_hash_tbl[IRE_HASH_TBL_COUNT];
	
	/* Host route hash table. */
	ire_t	* ire_assoc_hash_tbl[IRE_HASH_TBL_COUNT];
	
	/* Net route hash table. */
	ire_t	* ire_net_hash_tbl[INE_HASH_TBL_COUNT];

	/* Local subnet IRE list. */
	ire_t	* ire_subnet_head;

static	nv_t	ire_nv_arr[] = {
	{ IRE_BROADCAST, "IRE_BROADCAST" },
	{ IRE_LOCAL, "IRE_LOCAL" },
	{ IRE_LOOPBACK, "IRE_LOOPBACK" },
	{ IRE_ROUTE, "IRE_ROUTE" },
	{ IRE_GATEWAY, "IRE_GATEWAY" },
	{ IRE_NET, "IRE_NET" },
	{ IRE_SUBNET, "IRE_SUBNET" },
	{ IRE_RESOLVER, "IRE_RESOLVER" },
	{ IRE_ROUTE_ASSOC, "IRE_ROUTE_ASSOC" },
	{ IRE_ROUTE_REDIRECT, "IRE_ROUTE_REDIRECT" },
	{ 0 }
};
	nv_t	* ire_nv_tbl = ire_nv_arr;

/* Simple ICMP IP Header Template */
static	ipha_t icmp_ipha = {
	IP_SIMPLE_HDR_VERSION, 0, 0, 0, 0, 0, IPPROTO_ICMP
};

static struct module_info info = {
	0, "ip", 1, INFPSZ, 65536, 1024
};

static struct qinit rinit = {
	(pfi_t)ip_rput, (pfi_t)ip_rsrv, ip_open, ip_close, nil(pfi_t), &info
};

static struct qinit winit = {
	(pfi_t)ip_wput, (pfi_t)ip_wsrv, ip_open, ip_close, nil(pfi_t), &info
};

static struct qinit lrinit = {
	(pfi_t)ip_lrput, nil(pfi_t), ip_open, ip_close, nil(pfi_t), &info
};

static struct qinit lwinit = {
	(pfi_t)ip_lwput, nil(pfi_t), ip_open, ip_close, nil(pfi_t), &info
};

/* (Please don't reformat this.  gh likes it this way!) */
	struct streamtab ipinfo = {
		&rinit, &winit, &lrinit, &lwinit
};

	int	ipdevflag = 0;

	MI_HRT_DCL(ip_g_rtime)	/* (gh bait) */
	MI_HRT_DCL(ip_g_wtime)	/* (gh bait) */
	
#if 0
static	frtn_t	ip_bogo_frtn = { ip_nothing };
#endif

/* Generate an ICMP fragmentation needed message. */
staticf void
icmp_frag_needed (q, mp, mtu)
	queue_t	* q;
	mblk_t	* mp;
	int	mtu;
{
	icmph_t	icmph;

	if (!(mp = icmp_pkt_err_ok(mp)))
		return;
	bzero((char *)&icmph, sizeof(icmph_t));
	icmph.icmph_type = ICMP_DEST_UNREACHABLE;
	icmph.icmph_code = ICMP_FRAGMENTATION_NEEDED;
	icmph.icmph_du_mtu = htons(mtu);
	BUMP_MIB(icmp_mib.icmpOutFragNeeded);
	BUMP_MIB(icmp_mib.icmpOutDestUnreachs);
	icmp_pkt(q, mp, (char *)&icmph, sizeof(icmph_t));
}

/*
 * All arriving ICMP messages are sent here from ip_rput.
 * When ipif is NULL the queue has to be an "ill queue".
 */
staticf void
icmp_inbound (q, mp, ire_type, ipif, sum_valid, sum)
	queue_t	* q;
	mblk_t	* mp;
	int	ire_type;
	ipif_t	* ipif;
	int	sum_valid;
	u32	sum;
{
reg	icmph_t	* icmph;
	ipc_t	* ipc;
reg	ipha_t	* ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	int	iph_hdr_length;
	boolean_t	interested;
	u32	u1;
	u_char	* wptr;

	BUMP_MIB(icmp_mib.icmpInMsgs);
	iph_hdr_length = IPH_HDR_LENGTH(ipha);
	if ((mp->b_wptr - mp->b_rptr) < (iph_hdr_length + ICMPH_SIZE)) {
		/* Last chance to get real. */
		if (!pullupmsg(mp, iph_hdr_length + ICMPH_SIZE)) {
			BUMP_MIB(icmp_mib.icmpInErrors);
			freemsg(mp);
			return;
		}
		/* Refresh iph following the pullup. */
		ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	}
	/* ICMP header checksum, including checksum field, should be zero. */
	if (sum_valid ? (sum != 0 && sum != 0xFFFF) :
	    IP_CSUM(mp, iph_hdr_length, 0)) {
		BUMP_MIB(icmp_mib.icmpInCksumErrs);
		freemsg(mp);
		return;
	}
	/* The IP header will always be a multiple of four bytes */
	icmph = (icmph_t *)ALIGN32(&mp->b_rptr[iph_hdr_length]);
	wptr = (u_char *)icmph + ICMPH_SIZE;
	/* We will set "interested" to "true" if we want a copy */
	interested = false;
	switch (icmph->icmph_type) {
	case ICMP_ECHO_REPLY:
		BUMP_MIB(icmp_mib.icmpInEchoReps);
		break;
	case ICMP_DEST_UNREACHABLE:
		if ( icmph->icmph_code == ICMP_FRAGMENTATION_NEEDED )
			BUMP_MIB(icmp_mib.icmpInFragNeeded);
		interested = true;	/* Pass up to transport */
		BUMP_MIB(icmp_mib.icmpInDestUnreachs);
		break;
	case ICMP_SOURCE_QUENCH:
		interested = true;	/* Pass up to transport */
		BUMP_MIB(icmp_mib.icmpInSrcQuenchs);
		break;
	case ICMP_REDIRECT:
		if (!ip_ignore_redirect)
			interested = true;
		BUMP_MIB(icmp_mib.icmpInRedirects);
		break;
	case ICMP_ECHO_REQUEST:
		/*
		 * Whether to respond to echo requests that come in as IP
		 * broadcasts is subject to debate (what isn't?).  We aim
		 * to please, you pick it.  Default is do it.
		 */
		if (ip_g_resp_to_echo_bcast  ||  ire_type != IRE_BROADCAST)
			interested = true;
		BUMP_MIB(icmp_mib.icmpInEchos);
		break;
	case ICMP_ROUTER_ADVERTISEMENT:
	case ICMP_ROUTER_SOLICITATION:
		break;
	case ICMP_TIME_EXCEEDED:
		interested = true;	/* Pass up to transport */
		BUMP_MIB(icmp_mib.icmpInTimeExcds);
		break;
	case ICMP_PARAM_PROBLEM:
		interested = true;	/* Pass up to transport */
		BUMP_MIB(icmp_mib.icmpInParmProbs);
		break;
	case ICMP_TIME_STAMP_REQUEST:
		/* Response to Time Stamp Requests is local policy. */
		if ( ip_g_resp_to_timestamp
		/* So is whether to respond if it was an IP broadcast. */
		&&  (ire_type != IRE_BROADCAST || ip_g_resp_to_timestamp_bcast)
		/* Why do we think we can count on this ??? */
		/* TODO m_pullup of complete header? */
		&&  (mp->b_datap->db_lim - wptr) >= (3 * sizeof(u32)) )
			interested = true;
		BUMP_MIB(icmp_mib.icmpInTimestamps);
		break;
	case ICMP_TIME_STAMP_REPLY:
		BUMP_MIB(icmp_mib.icmpInTimestampReps);
		break;
	case ICMP_INFO_REQUEST:
		/* Per RFC 1122 3.2.2.7, ignore this. */
	case ICMP_INFO_REPLY:
		break;
	case ICMP_ADDRESS_MASK_REQUEST:
		if ((ip_respond_to_address_mask_broadcast  ||  ire_type != IRE_BROADCAST)
		/* TODO m_pullup of complete header? */
		&& (mp->b_datap->db_lim - wptr) >= IP_ADDR_LEN)
			interested = true;
		BUMP_MIB(icmp_mib.icmpInAddrMasks);
		break;
	case ICMP_ADDRESS_MASK_REPLY:
		BUMP_MIB(icmp_mib.icmpInAddrMaskReps);
		break;
	default:
		interested = true;	/* Pass up to transport */
		BUMP_MIB(icmp_mib.icmpInUnknowns);
		break;
	}
	/* See if their is an ICMP client. */
	ipc = ipc_proto_fanout[IPPROTO_ICMP];
	if (ipc) {
		/* If there is an ICMP client and we want one too, copy it. */
		mblk_t * mp1 = interested ? copymsg(mp) : mp;
		if (mp1) {
			mblk_t * mp2;
			queue_t	* rq;
			u32 	dst = ipha->ipha_dst;

			do {
				/*
				 * Walk the hash list giving dups to all
				 * ICMP clients.
				 */
				if ((ipc->ipc_udp_addr != 0 &&
				     ipc->ipc_udp_addr != dst) ||
				    !ipc_wantpacket(ipc, dst)) {
					ipc = ipc->ipc_hash_next;
					if (ipc == nilp(ipc_t)) {
						freemsg(mp1);
						break;
					}
					mp2 = nilp(mblk_t);
					continue;
				}
				rq = ipc->ipc_rq;
				if (ipc->ipc_hash_next == NULL ||
				    ((mp2 = dupmsg(mp1)) == NULL &&
				    (mp2 = copymsg(mp1)) == NULL))
					mp2 = mp1;
				if (canputnext(rq)) {
					BUMP_MIB(ip_mib.ipInDelivers);
					putnext(rq, mp2);
				} else {
					BUMP_MIB(icmp_mib.icmpInOverflows);
					icmp_source_quench(WR(rq), mp2);
				}
				ipc = ipc->ipc_hash_next;
			} while (mp2 != mp1);
		}
		if (!interested)
			return;
	} else if (!interested) {
		freemsg(mp);
		return;
	}
	/* We want to do something with it. */
	/* Check db_ref to make sure we can modify the packet. */
	if (mp->b_datap->db_ref > 1) {
		mblk_t	*mp1;

		mp1 = copymsg(mp);
		freemsg(mp);
		if (!mp1) {
			BUMP_MIB(icmp_mib.icmpOutDrops);
			return;
		}
		mp = mp1;
		ipha = (ipha_t *)ALIGN32(mp->b_rptr);
		icmph = (icmph_t *)ALIGN32(&mp->b_rptr[iph_hdr_length]);
		wptr = (u_char *)icmph + ICMPH_SIZE;
	}
	switch (icmph->icmph_type) {
	case ICMP_ADDRESS_MASK_REQUEST:
		/* Set ipif for the non-broadcast case. */
		if ( !ipif ) {
			ill_t	* ill = (ill_t *)q->q_ptr;
			
			if ( ill->ill_ipif_up_count == 1 )
				ipif = ill->ill_ipif;
			else {
				u32	src;
				src = ipha->ipha_src;
				ipif = ipif_lookup_remote(ill, src);
				if ( !ipif ) {
					freemsg(mp);
					return;
				}
			}
		}
		icmph->icmph_type = ICMP_ADDRESS_MASK_REPLY;
		bcopy((char *)&ipif->ipif_net_mask, (char *)wptr, IP_ADDR_LEN);
		BUMP_MIB(icmp_mib.icmpOutAddrMaskReps);
		break;
	case ICMP_ECHO_REQUEST:
		icmph->icmph_type = ICMP_ECHO_REPLY;
		BUMP_MIB(icmp_mib.icmpOutEchoReps);
		break;
	case ICMP_TIME_STAMP_REQUEST: {
		u32 *tsp;

		icmph->icmph_type = ICMP_TIME_STAMP_REPLY;
		tsp = (u32 *)ALIGN32(wptr);
		tsp++;		/* Skip past 'originate time' */
		/* Compute # of milliseconds since midnight */
		u1 = ((time_in_secs % (24 * 60 * 60)) * 1000) +
			(LBOLT_TO_MS(lbolt) % 1000);

		*tsp++ = htonl(u1);;	/* Lay in 'receive time' */
		*tsp++ = htonl(u1);;	/* Lay in 'send time' */
		BUMP_MIB(icmp_mib.icmpOutTimestampReps);
		break;
	}
	case ICMP_REDIRECT:
		become_writer(q, mp, (pfi_t)icmp_redirect);
		return;
	case ICMP_DEST_UNREACHABLE:
		if ( icmph->icmph_code == ICMP_FRAGMENTATION_NEEDED )
			become_writer(q, mp, (pfi_t)icmp_inbound_error);
		else
			icmp_inbound_error(q, mp);
		return;
	default:
		icmp_inbound_error(q, mp);
		return;
	}
	icmph->icmph_checksum = 0;
	icmph->icmph_checksum = IP_CSUM(mp, iph_hdr_length, 0);
	if (ire_type == IRE_BROADCAST) {
		/*
		 * Make it look like it was directed to us, so we don't look
		 * like a fool with a broadcast source address.
		 * Note: ip_?put_local guarantees that ipif is set here.
		 */
		ipha->ipha_dst = ipif->ipif_local_addr;
	}
	/* Reset time to live. */
	ipha->ipha_ttl = ip_def_ttl;
	
	{
		/* Swap source and destination addresses */
		u32	tmp;

		tmp = ipha->ipha_src;
		ipha->ipha_src = ipha->ipha_dst;
		ipha->ipha_dst = tmp;
	}
	if (!IS_SIMPLE_IPH(ipha))
		icmp_options_update(ipha);

	BUMP_MIB(icmp_mib.icmpOutMsgs);
	put(WR(q), mp);
}

/* Table from RFC 1191 */
static int icmp_frag_size_table[] =
{ 32000, 17914, 8166, 4352, 2002, 1496, 1006, 508, 296, 68 };

/*
 * Process received ICMP error packets including Dest Unreachables.
 * Must be called as a writer for fragmentation needed unreachables!
 */
staticf void
icmp_inbound_error (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
reg	icmph_t * icmph;
reg	ipha_t	* ipha;
	int	iph_hdr_length;
	int	hdr_length;
	ire_t	* ire;
	int	mtu;
	ipc_t	* ipc;
	mblk_t	* mp1;

	if (!OK_32PTR(mp->b_rptr)) {
		if (!pullupmsg(mp, -1)) {
			BUMP_MIB(ip_mib.ipInDiscards);
			goto drop_pkt;
		}
	}
	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	iph_hdr_length = IPH_HDR_LENGTH(ipha);
	if (((mp->b_wptr - mp->b_rptr) - iph_hdr_length) < sizeof(icmph_t)) {
		BUMP_MIB(icmp_mib.icmpInErrors);
		goto drop_pkt;
	}
	icmph = (icmph_t *)ALIGN32(&mp->b_rptr[iph_hdr_length]);
	ipha = (ipha_t *)&icmph[1];
	if ((u_char *)&ipha[1] > mp->b_wptr) {
		if (!pullupmsg(mp, (u_char *)&ipha[1] - mp->b_rptr)) {
			BUMP_MIB(ip_mib.ipInDiscards);
			goto drop_pkt;
		}
		icmph = (icmph_t *)ALIGN32(&mp->b_rptr[iph_hdr_length]);
		ipha = (ipha_t *)&icmph[1];
	}
	hdr_length = IPH_HDR_LENGTH(ipha);
	if ((u_char *)ipha + hdr_length > mp->b_wptr) {
		if (!pullupmsg(mp, (u_char *)ipha + hdr_length - mp->b_rptr)) {
			BUMP_MIB(ip_mib.ipInDiscards);
			goto drop_pkt;
		}
		icmph = (icmph_t *)ALIGN32(&mp->b_rptr[iph_hdr_length]);
		ipha = (ipha_t *)&icmph[1];
	}

	if (icmph->icmph_type == ICMP_DEST_UNREACHABLE &&
	    icmph->icmph_code == ICMP_FRAGMENTATION_NEEDED) {
		ire = ire_lookup(ipha->ipha_dst);
		/* Verify that the ire is correct */
		if (!ire || ire->ire_type != IRE_ROUTE) {
			ip1dbg(("icmp_unreach: no route for 0x%x\n",
				(int)ntohl(ipha->ipha_dst)));
			freemsg(mp);
			return;
		}
		/* Drop if the original packet contained a source route */
		if (ip_source_route_included(ipha)) {
			freemsg(mp);
			return;
		}
		/* Check for MTU discovery advice as described in RFC 1191 */
		mtu = ntohs(icmph->icmph_du_mtu);
		if ( icmph->icmph_du_zero == 0  &&  mtu > 68) {
			/* Reduce the IRE max frag value as advised. */
			ire->ire_max_frag = MIN(ire->ire_max_frag, mtu);
			ip1dbg(("Received mtu from router: %d\n", mtu));
		} else {
			u32	length;
			int	i;

			/*
			 * Use the table from RFC 1191 to figure out
			 * the next "platau" based on the length in
			 * the original IP packet.
			 */
			length = ntohs(ipha->ipha_length);
			if (ire->ire_max_frag <= length &&
			    ire->ire_max_frag >= length - hdr_length) {
				/*
				 * Handle broken BSD 4.2 systems that
				 * return the wrong iph_length in ICMP
				 * errors.
				 */
				ip1dbg(("Wrong mtu: sent %d, ire %d\n",
					length, ire->ire_max_frag));
				length -= hdr_length;
			}
			for (i = 0; i < A_CNT(icmp_frag_size_table); i++) {
				if (length > icmp_frag_size_table[i])
					break;
			}
			if (i == A_CNT(icmp_frag_size_table)) {
				/* Smaller than 68! */
				ip1dbg(("Too big for packet size %d\n",
					length));
				ire->ire_max_frag = MIN(ire->ire_max_frag,
							576);
				ire->ire_frag_flag = 0;
			} else {
				mtu = icmp_frag_size_table[i];
				ip1dbg(("Calculated mtu %d, packet size %d, before %d",
					mtu, length, ire->ire_max_frag));
				ire->ire_max_frag = MIN(ire->ire_max_frag,
							mtu);
				ip1dbg((", after %d\n", ire->ire_max_frag));
			}
		}
		/* Record the new max frag size for the ULP. */
		icmph->icmph_du_zero = 0;
		icmph->icmph_du_mtu = htons(ire->ire_max_frag);
	}
	/* Try to pass the ICMP message upstream in case the ULP cares. */
	switch (ipha->ipha_protocol) {
	case IPPROTO_UDP: {
		u16 *up;
		u16 srcport;

		/* Verify that we have at least 8 bytes of the UDP header */
		if ((u_char *)ipha + hdr_length + 8 > mp->b_wptr) {
			if (!pullupmsg(mp, (u_char *)ipha + hdr_length + 8 -
				       mp->b_rptr)) {
				BUMP_MIB(ip_mib.ipInDiscards);
				goto drop_pkt;
			}
			icmph = (icmph_t *)ALIGN32(&mp->b_rptr[iph_hdr_length]);
			ipha = (ipha_t *)&icmph[1];
		}
		up = (u16 *)ALIGN16(((u_char *)ipha) + hdr_length);
		srcport = up[0];	/* Local port in net byte order */
		/*
		 * Attempt to find a client stream based on port.
		 * Note that we do a reverse lookup since the header is
		 * in the form we sent it out.
		 */
		ipc = (ipc_t *)&ipc_udp_fanout[IP_UDP_HASH(srcport)];
		do {
			ipc = ipc->ipc_hash_next;
			if (!ipc) {
				/*
				 * No one bound to this port.  Is
				 * there a client that wants all
				 * unclaimed datagrams?
				 */
				ip2dbg(("icmp_inbound: no udp for 0x%x/%d\n",
					ntohl(ipha->ipha_src),
					(int)ntohs(srcport)));
				ipc = ipc_proto_fanout[IPPROTO_UDP];
				if (ipc)
					goto wildcard;
				freemsg(mp);
				return;
			}
		} while (!IP_UDP_MATCH(ipc, srcport, ipha->ipha_src));
		/* Found a client.  Send it upstream. */
		q = ipc->ipc_rq;
		if (!canputnext(q)) {
			BUMP_MIB(icmp_mib.icmpInOverflows);
			goto drop_pkt;
		}
		/* Have to change db_type after any pullupmsg */
		mp->b_datap->db_type = M_CTL;
		putnext(q, mp);
		return;
	}
	case IPPROTO_TCP: {
		u16 *up;
		u32 ports;
		ipha_t ripha;	/* With reversed addresses */

		/* Verify that we have at least 8 bytes of the TCP header */
		if ((u_char *)ipha + hdr_length + 8 > mp->b_wptr) {
			if (!pullupmsg(mp, (u_char *)ipha + hdr_length + 8 -
				       mp->b_rptr)) {
				BUMP_MIB(ip_mib.ipInDiscards);
				goto drop_pkt;
			}
			icmph = (icmph_t *)ALIGN32(&mp->b_rptr[iph_hdr_length]);
			ipha = (ipha_t *)&icmph[1];
		}
		up = (u16 *)ALIGN16(((u_char *)ipha) + hdr_length);
		/*
		 * Find a TCP client stream for this packet.
		 * Note that we do a reverse lookup since the header is
		 * in the form we sent it out.
		 * The ripha header is only used for the TCP_MATCH and we
		 * only set the src and dst addresses.
		 */
		ripha.ipha_src = ipha->ipha_dst;
		ripha.ipha_dst = ipha->ipha_src;
		((u16 *)&ports)[0] = up[1];
		((u16 *)&ports)[1] = up[0];

		ipc = (ipc_t *)&ipc_tcp_fanout[IP_TCP_HASH(ripha.ipha_src,
								ports)];
		do {
			ipc = ipc->ipc_hash_next;
			if (!ipc) {
				/*
				 * No hard-bound match.  Look for a
				 * stream that wants all unclaimed.  Note
				 * that TCP must normally make sure that
				 * there is such a stream, otherwise it
				 * will be tough to get inbound connections
				 * going.
				 */
				ip2dbg(("icmp_inbound: no tcp for local 0x%x/%d remote 0x%x/%d\n",
					ntohl(ripha.ipha_dst),
					(int)ntohs(((u16 *)&ports)[1]),
					ntohl(ripha.ipha_src),
					(int)ntohs(((u16 *)&ports)[0])));
				ipc = ipc_proto_fanout[IPPROTO_TCP];
				if (ipc)
					goto wildcard;
				freemsg(mp);
				return;
			}
		} while (!TCP_MATCH(ipc, &ripha, ports));
		/* Got a client, up it goes. */
		q = ipc->ipc_rq;
		/* Have to change db_type after any pullupmsg */
		mp->b_datap->db_type = M_CTL;
		putnext(q, mp);
		return;
	}
	default:
		break;
	}

	/*
	 * Handle protocols with which IP is less intimate.  There
	 * can be more than one stream bound to a particular
	 * protocol.  When this is the case, each one gets a copy
	 * of any incoming packets.
	 */
	ipc = ipc_proto_fanout[ipha->ipha_protocol];
	if (!ipc) {
		ip2dbg(("icmp_inbound: no ipc for %d\n",
			ipha->ipha_protocol));
		freemsg(mp);
		return;
	}
wildcard:;
	/* Have to change db_type after any pullupmsg */
	mp->b_datap->db_type = M_CTL;

	for (;;) {
		if ((ipc->ipc_udp_addr != 0 &&
		     ipc->ipc_udp_addr != ipha->ipha_src) ||
		    !ipc_wantpacket(ipc, ipha->ipha_src)) {
			ipc = ipc->ipc_hash_next;
			if (ipc == nilp(ipc_t)) {
				/* No more interested parties */
				freemsg(mp);
				return;
			}
			continue;
		}
		q = ipc->ipc_rq;
		ipc = ipc->ipc_hash_next;
		if (!ipc  ||  !(mp1 = dupmsg(mp))) {
			if (!canputnext(q)) {
				BUMP_MIB(icmp_mib.icmpInOverflows);
				goto drop_pkt;
			}
			putnext(q, mp);
			return;
		}
		if (!canputnext(q)) {
			BUMP_MIB(icmp_mib.icmpInOverflows);
			freemsg(mp);
		} else {
			putnext(q, mp);
		}
		mp = mp1;
	}

drop_pkt:;
	ip1dbg(("icmp_inbound_error: drop pkt\n"));
	freemsg(mp);
}

/*
 * Update any record route or timestamp options to include this host.
 * Reverse any source route option.
 */
staticf void
icmp_options_update (ipha)
	ipha_t	* ipha;
{
	u32	totallen;
	u_char	* opt;
	u32	optval;
	u32	optlen;
	u32	src;		/* Our local address */
	u32	dst;

	ip2dbg(("icmp_options_update\n"));
	src = ipha->ipha_src;
	dst = ipha->ipha_dst;
	totallen = ipha->ipha_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	opt = (u_char *)&ipha[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			return;
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}
		ip2dbg(("icmp_options_update: opt %d, len %d\n", 
			  optval, optlen));

		if (optlen == 0 || optlen > totallen)
			break;

		switch (optval) {
			int	off1, off2;
		case IPOPT_SSRR:
		case IPOPT_LSRR: 

			/* Reverse source route */
			/* First entry should be the next to last one in the
			 * current source route (the last entry is our 
			 * address.)
			 * The last entry should be the final destination.
			 */
			off1 = IPOPT_MINOFF_SR - 1;
			off2 = opt[IPOPT_POS_OFF] - IP_ADDR_LEN - 1;
			if (off2 < 0) {
				/* No entries in source route */
				ip1dbg(("icmp_options_update: bad src route\n"));
				break;
			}
			bcopy((char *)opt + off2, (char *)&dst, IP_ADDR_LEN);
			bcopy((char *)&ipha->ipha_dst, 
			      (char *)opt + off2, IP_ADDR_LEN);
			bcopy((char *)&dst, (char *)&ipha->ipha_dst,
			      IP_ADDR_LEN);
			off2 -= IP_ADDR_LEN;

			while (off1 < off2) {
				bcopy((char *)opt + off1, (char *)&src,
				      IP_ADDR_LEN);
				bcopy((char *)opt + off2, (char *)opt + off1, 
				      IP_ADDR_LEN);
				bcopy((char *)&src, (char *)opt + off2, 
				      IP_ADDR_LEN);
				off1 += IP_ADDR_LEN;
				off2 -= IP_ADDR_LEN;
			}
			opt[IPOPT_POS_OFF] = IPOPT_MINOFF_SR;
			break;
		}
		totallen -= optlen;
		opt += optlen;
	}
}

/* Process received ICMP Redirect messages.  Must be called as a writer! */
/* ARGSUSED */
staticf void
icmp_redirect (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
reg	ipha_t	* ipha;
	int	iph_hdr_length;
	icmph_t	* icmph;
	ipha_t	* ipha_err;
	ire_t	* ire;
	ire_t	* prev_ire;
	u32	src, dst, gateway;

	if (!OK_32PTR(mp->b_rptr)) {
		if (!pullupmsg(mp, -1)) {
			BUMP_MIB(ip_mib.ipInDiscards);
			freemsg(mp);
			return;
		}
	}
	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	iph_hdr_length = IPH_HDR_LENGTH(ipha);
	if (((mp->b_wptr - mp->b_rptr) - iph_hdr_length) <
	    sizeof(icmph_t) + IP_SIMPLE_HDR_LENGTH) {
		BUMP_MIB(icmp_mib.icmpInErrors);
		freemsg(mp);
		return;
	}
	icmph = (icmph_t *)ALIGN32(&mp->b_rptr[iph_hdr_length]);
	ipha_err = (ipha_t *)&icmph[1];
	src = ipha->ipha_src;
	dst = ipha_err->ipha_dst;
	gateway = icmph->icmph_rd_gateway;
	/* Make sure the new gateway is reachable somehow. */
	ire = ire_lookup(gateway);
	/*
	 * Make sure we had a route for the dest in question and that
	 * that route was pointing to the old gateway (the source of the
	 * redirect packet.)
	 */
	prev_ire = ire_lookup_exact(dst, IRE_ROUTE, src);
	/*
	 * Check that 
	 *	the redirect was not from ourselves
	 *	the redirect arrived from the current gateway
	 *	the new gateway and the old gateway are directly reachable
	 */
	if (!prev_ire
	||  !ire
	||   ire->ire_type == IRE_LOCAL
	||   prev_ire == ire
	||   prev_ire->ire_gateway_addr != src
	||  !ire_lookup_interface(gateway, IRE_INTERFACE)
	||  !ire_lookup_interface(src, IRE_INTERFACE)) {
		/*
		 * The last check ensures that the sender of this redirect at
		 * least claims to be on a directly connected net.
		 * TODO:  Are there other/better things to do for security?
		 */
		BUMP_MIB(icmp_mib.icmpInBadRedirects);
		freemsg(mp);
		return;
	}
	ire_delete(prev_ire);
	/*
	 * TODO: more precise handling for cases 0, 2, 3, the latter two
	 * require TOS routing
	 */
	switch (icmph->icmph_code) {
	case 0:
	case 1:
		/* TODO: TOS specificity for cases 2 and 3 */
	case 2:
	case 3:
		break;
	default:
		freemsg(mp);
		return;
	}
	/*
	 * Create a Route Association.  This will allow us to remember that
	 * someone we believe told us to use the particular gateway.
	 */
	ire = ire_create(
		(u_char *)&dst,				/* dest addr */
		(u_char *)&ip_g_all_ones,		/* mask */
		(u_char *)&ire->ire_src_addr,		/* source addr */
		(u_char *)&gateway,    			/* gateway addr */
		ire->ire_max_frag,			/* max frag */
		nilp(mblk_t),				/* xmit header */
		nilp(queue_t),				/* no rfq */
		nilp(queue_t),				/* no stq */
		IRE_ROUTE_REDIRECT,
		ire->ire_rtt,
		ire->ire_ll_hdr_length
	);
	if (ire)
		(void)ire_add(ire);
	/*
	 * Delete any existing IRE_ROUTE_REDIRECT for this destination.
	 * This together with the added IRE has the effect of
	 * modifying an existing redirect.
	 */
	prev_ire = ire_lookup_exact(dst, IRE_ROUTE_REDIRECT, src);
	if (prev_ire)
		ire_delete(prev_ire);

	freemsg(mp);
}

/* Generate an ICMP parameter problem message.  (May be called as writer.) */
staticf void
icmp_param_problem (q, mp, ptr)
	queue_t	* q;
	mblk_t	* mp;
	int	ptr;
{
	icmph_t	icmph;

	if (!(mp = icmp_pkt_err_ok(mp)))
		return;
	bzero((char *)&icmph, sizeof(icmph_t));
	icmph.icmph_type = ICMP_PARAM_PROBLEM;
	icmph.icmph_pp_ptr = (u8)ptr;
	BUMP_MIB(icmp_mib.icmpOutParmProbs);
	icmp_pkt(q, mp, (char *)&icmph, sizeof(icmph_t));
}

/*
 * Build and ship an ICMP message using the packet data in mp, and the ICMP
 * header pointed to by "stuff".  (May be called as writer.)
 * Note: assumes that icmp_pkt_err_ok has been called to verify that
 * and icmp error packet can be sent.
 */
staticf void
icmp_pkt (q, mp, stuff, len)
	queue_t	* q;
	mblk_t	* mp;
	char	* stuff;
	int	len;
{
	u32	dst;
	icmph_t	* icmph;
	ipha_t	* ipha;
	u_int	len_needed;
	u_int	msg_len;
	mblk_t	* mp1;

	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	/* Remember our eventual destination */
	dst = ipha->ipha_src;

	/* 
	 * Check if we can send back more then 8 bytes in addition
	 * to the IP header. We will include as much as 64 bytes.
	 */
	len_needed = IPH_HDR_LENGTH(ipha) + ip_icmp_return;
	msg_len = msgdsize(mp);
	if (msg_len > len_needed) {
		adjmsg(mp, len_needed - msg_len);
		msg_len = len_needed;
	}
	mp1 = allocb(sizeof(icmp_ipha) + len, BPRI_HI);
	if (!mp1) {
		BUMP_MIB(icmp_mib.icmpOutErrors);
		freemsg(mp);
		return;
	}
	mp1->b_cont = mp;
	mp = mp1;
	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	mp1->b_wptr = (u_char *)ipha + (sizeof(icmp_ipha) + len);
	*ipha = icmp_ipha;
	ipha->ipha_dst = dst;
	ipha->ipha_ttl = ip_def_ttl;
	msg_len += sizeof(icmp_ipha) + len;
	ipha->ipha_length = htons(msg_len);
	icmph = (icmph_t *)&ipha[1];
	bcopy(stuff, (char *)icmph, len);
	icmph->icmph_checksum = 0;
	icmph->icmph_checksum = IP_CSUM(mp, sizeof(ipha_t), 0);
	BUMP_MIB(icmp_mib.icmpOutMsgs);
	put(q, mp);
}

/*
 * Check if it is ok to send an ICMP error packet in response to the
 * IP packet in mp.
 * Free the message and return null if no ICMP error packet should be sent.
 */
staticf mblk_t *
icmp_pkt_err_ok (mp)
	mblk_t	*mp;
{
	icmph_t	* icmph;
	ipha_t	* ipha;
	u_int	len_needed;

	if (!mp)
		return nilp(mblk_t);
	if (icmp_pkt_err_last > LBOLT_TO_MS(lbolt))
		/* 100HZ lbolt in ms for 32bit arch wraps every 49.7 days */
		icmp_pkt_err_last = 0;
	if (icmp_pkt_err_last + ip_icmp_err_interval > LBOLT_TO_MS(lbolt)) {
		/*
		 * Only send ICMP error packets every so often.
		 * This should be done on a per port/source basis,
		 * but for now this will due.
		 */
		freemsg(mp);
		return nilp(mblk_t);
	}
	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	if ( ip_csum_hdr(ipha) ) {
		BUMP_MIB(ip_mib.ipInCksumErrs);
		freemsg(mp);
		return nilp(mblk_t);
	}
	if (ire_lookup_exact(ipha->ipha_dst, IRE_BROADCAST, 0)
	||  ire_lookup_exact(ipha->ipha_src, IRE_BROADCAST, 0)
	||  CLASSD(ipha->ipha_dst)
	||  CLASSD(ipha->ipha_src)
	||  (ntohs(ipha->ipha_fragment_offset_and_flags) & IPH_OFFSET) ) {
		/* Note: only errors to the fragment with offset 0 */
		BUMP_MIB(icmp_mib.icmpOutDrops);
		freemsg(mp);
		return nilp(mblk_t);
	}
	if ( ipha->ipha_protocol == IPPROTO_ICMP ) {
		/*
		 * Check the ICMP type.  RFC 1122 sez:  don't send ICMP
		 * errors in response to any ICMP errors.
		 */
		len_needed = IPH_HDR_LENGTH(ipha) + ICMPH_SIZE;
		if ( mp->b_wptr - mp->b_rptr < len_needed ) {
			if ( !pullupmsg(mp, len_needed) ) {
				BUMP_MIB(icmp_mib.icmpInErrors);
				freemsg(mp);
				return nilp(mblk_t);
			}
			ipha = (ipha_t *)ALIGN32(mp->b_rptr);
		}
		icmph = (icmph_t *)ALIGN32(&((char *)ipha)[IPH_HDR_LENGTH(ipha)]);
		switch ( icmph->icmph_type ) {
		case ICMP_DEST_UNREACHABLE:
		case ICMP_SOURCE_QUENCH:
		case ICMP_TIME_EXCEEDED:
		case ICMP_PARAM_PROBLEM:
			BUMP_MIB(icmp_mib.icmpOutDrops);
			freemsg(mp);
			return nilp(mblk_t);
		default:
			break;
		}
	}
	icmp_pkt_err_last = LBOLT_TO_MS(lbolt);
	return mp;
}

/* Generate an ICMP redirect message. */
staticf void
icmp_send_redirect(q, mp, gateway)
	queue_t	* q;
	mblk_t	* mp;
	u32 	gateway;
{
	icmph_t	icmph;

	if (!(mp = icmp_pkt_err_ok(mp)))
		return;
	bzero((char *)&icmph, sizeof(icmph_t));
	icmph.icmph_type = ICMP_REDIRECT;
	icmph.icmph_code = 1;
	icmph.icmph_rd_gateway = gateway;
	BUMP_MIB(icmp_mib.icmpOutRedirects);
	icmp_pkt(q, mp, (char *)&icmph, sizeof(icmph_t));
	
}

/* Generate an ICMP source quench message.  (May be called as writer.) */
staticf void
icmp_source_quench (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	icmph_t	icmph;

	if (!ip_send_source_quench) {
		freemsg(mp);
		return;
	}
	if (!(mp = icmp_pkt_err_ok(mp)))
		return;
	bzero((char *)&icmph, sizeof(icmph_t));
	icmph.icmph_type = ICMP_SOURCE_QUENCH;
	BUMP_MIB(icmp_mib.icmpOutSrcQuenchs);
	icmp_pkt(q, mp, (char *)&icmph, sizeof(icmph_t));
}

/* Generate an ICMP time exceeded message.  (May be called as writer.) */
void
icmp_time_exceeded (q, mp, code)
	queue_t	* q;
	mblk_t	* mp;
	int	code;
{
	icmph_t	icmph;

	if (!(mp = icmp_pkt_err_ok(mp)))
		return;
	bzero((char *)&icmph, sizeof(icmph_t));
	icmph.icmph_type = ICMP_TIME_EXCEEDED;
	icmph.icmph_code = (u8)code;
	BUMP_MIB(icmp_mib.icmpOutTimeExcds);
	icmp_pkt(q, mp, (char *)&icmph, sizeof(icmph_t));
}

/* Generate an ICMP unreachable message.  (May be called as writer.) */
staticf void
icmp_unreachable (q, mp, code)
	queue_t	* q;
	mblk_t	* mp;
	int	code;
{
	icmph_t	icmph;

	if (!(mp = icmp_pkt_err_ok(mp)))
		return;
	bzero((char *)&icmph, sizeof(icmph_t));
	icmph.icmph_type = ICMP_DEST_UNREACHABLE;
	icmph.icmph_code = (u8)code;
	BUMP_MIB(icmp_mib.icmpOutDestUnreachs);
	icmp_pkt(q, mp, (char *)&icmph, sizeof(icmph_t));
}

staticf void
igmp_timeout ()
{
	int next;

	next = igmp_timeout_handler();
	if ( next != 0 && igmp_timer_ill )
		mi_timer(igmp_timer_ill->ill_rq, igmp_timer_mp, 
			 next * igmp_timer_interval);
}

void
igmp_timeout_start(int next)
{
	if ( next != 0 && igmp_timer_ill )
		mi_timer(igmp_timer_ill->ill_rq, igmp_timer_mp, 
			 next * igmp_timer_interval);
}
	
/*
 * News from ARP.  ARP sends notification of interesting events down
 * to its clients using M_CTL messages with the interesting ARP packet
 * attached via b_cont.  We are called as writer in case we need to
 * blow away any IREs.
 */
staticf	void
ip_arp_news (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	arcn_t	* arcn;
	arh_t	* arh;
	char	* cp1;
	u_char	* cp2;
	ire_t	* ire;
	int	i1;
	char	hbuf[128];
	char	sbuf[16];
	u32	src;

	if ( (mp->b_wptr - mp->b_rptr) < sizeof(arcn_t)
	||  !mp->b_cont ) {
		if ( q->q_next ) {
			putnext(q, mp);
		} else
			freemsg(mp);
		return;
	}

	arh = (arh_t *)mp->b_cont->b_rptr;
	/* Is it one we are interested in? */
	if ( BE16_TO_U16(arh->arh_proto) != IP_ARP_PROTO_TYPE ) {
		freemsg(mp);
		return;
	}

	bcopy((char *)&arh[1] + (arh->arh_hlen & 0xFF), (char *)&src, 
	      IP_ADDR_LEN);
	ire = ire_lookup(src);

	arcn = (arcn_t *)ALIGN32(mp->b_rptr);
	switch ( arcn->arcn_code ) {
	case AR_CN_BOGON:
		/*
		 * Someone is sending ARP packets with a source protocol
		 * address which we have published.  Either they are
		 * pretending to be us, or we have been asked to proxy
		 * for a machine that can do fine for itself, or two
		 * different machines are providing proxy service for the
		 * same protocol address, or something.  We try and do
		 * something appropriate here.
		 */
		cp2 = (u_char *)&arh[1];
		cp1 = hbuf;
		*cp1 = '\0';
		for ( i1 = arh->arh_hlen; i1--; cp1 += 3)
			mi_sprintf(cp1, "%02x:", *cp2++ & 0xff);
		if ( cp1 != hbuf )
			cp1[-1] = '\0';
		(void)ip_dot_addr(src, sbuf);
		if ( ire
		&&  IRE_IS_LOCAL(ire) ) {
			mi_strlog(q, 1, SL_TRACE|SL_CONSOLE,
				"IP: Hardware address '%s' trying to be our address %s!",
				hbuf, sbuf);
		} else {
			mi_strlog(q, 1, SL_TRACE,
				"IP: Proxy ARP problem?  Hardware address '%s' thinks it is %s",
				hbuf, sbuf);
		}
		break;
	case AR_CN_ANNOUNCE:
		/*
		 * ARP gives us a copy of any broadcast packet with identical
		 * sender and receiver protocol address, in
		 * case we want to intuit something from it.  Such a packet
		 * usually means that a machine has just come up on the net.
		 * If we have an IRE_ROUTE, we blow it away.  This way we will
		 * immediately pick up the rare case of a host changing
		 * hardware address.
		 */
		if ( ire ) {
			if ( ire->ire_type == IRE_ROUTE )
				ire_delete(ire);
			ire = ire_lookup_exact(src, IRE_GATEWAY, 0);
			if ( ire )
				ire_delete_routes(ire);
		}
		break;
	default:
		break;
	}
	freemsg(mp);
	return;
}

/*
 * Upper level protocols pass through bind requests to IP for inspection
 * and to arrange for power-fanout assist.  The ULP is identified by
 * adding a single byte at the end of the original bind message.
 * A ULP other than UDP or TCP that wishes to be recognized passes
 * down a bind with a zero length address.  (Always called as writer.)
 */
staticf void
ip_bind (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	int	error = 0;
reg	ipa_t	* ipa;
reg	ipc_t	* ipc = (ipc_t *)q->q_ptr;
	int	len;
	int	protocol;
reg	struct T_bind_req	* tbr;
	u32	src_addr;
	u32	dst_addr;
	u32	ports;	/* remote port, local port */
	u_char	* ucp;
	ire_t   *src_ire;
	ipif_t	*ipif;

	len = mp->b_wptr - mp->b_rptr;
	if (len < (sizeof(*tbr) + 1)) {
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			"ip_bind: bogus msg, len %d", len);
		freemsg(mp);
		return;
	}
	/* Back up and extract the protocol identifier. */
	mp->b_wptr--;
	protocol = *mp->b_wptr & 0xFF;
	tbr = (struct T_bind_req *)ALIGN32(mp->b_rptr);
	/* Reset the message type in preparation for shipping it back. */
	mp->b_datap->db_type = M_PCPROTO;

	/*
	 * Check for a zero length address.  This is from a protocol that
	 * wants to register to receive all packets of its type.
	 */
	if (tbr->ADDR_length == 0) {
		/* No hash here really.  The table is big enough. */
		ipc->ipc_udp_addr = 0;
		ipc_hash_insert_last(&ipc_proto_fanout[protocol], ipc);
		tbr->PRIM_type = T_BIND_ACK;
		qreply(q, mp);
		return;
	}

	/* Extract the address pointer from the message. */
	/* TODO alignment? */
	ipa = (ipa_t *)ALIGN32(mi_offset_param(mp, tbr->ADDR_offset, 
					      tbr->ADDR_length));
	if (!ipa) {
		ip1dbg(("ip_bind: no ipa_t\n"));
		goto bad_addr;
	}

	src_ire = (ire_t *) NULL; /* no src ire */
	switch (protocol) {
	case IPPROTO_UDP:
	default: {
		/*
		 * Here address is verified to be a valid local address.
		 * Additionally, protocols can also request a copy of the
		 * IRE corresponding to this address by appending a
		 * IRE_DB_REQ_TYPE mp to T_BIND_REQ.
		 * If the IRE_DB_REQ_TYPE mp is present, a broadcast
		 * address is also considered a valid local address.
		 * In the case of a broadcast address, however, the
		 * upper protocol is expected to reset the src address
		 * to 0 if it sees a IRE_BROADCAST type returned so that
		 * no packets are emitted with broadcast address as
		 * source address (that violates hosts requirements RFC1122)
		 * The addresses valid for bind are:
		 * 	(1) - INADDR_ANY (0)
		 *	(2) - IP address of an UP interface
		 *	(3) - IP address of a DOWN interface
		 *	(4) - valid local IP broadcast addresses. In this case
		 *	      the ipc will only receive packets destined to
		 *	      the specified broadcast address.
		 *	(5) - a multicast address. In this case
		 *	      the ipc will only receive packets destined to
		 *	      the specified multicast address. Note: the
		 *	      application still has to issue an
		 *	      IP_ADD_MEMBERSHIP socket option.
		 *
		 */
		ipc_t	**ipcp;
		mblk_t *mp1;
		u_int ire_requested;

		mp1 = mp->b_cont;	/* trailing mp if any */
		ire_requested = (mp1 && mp1->b_datap->db_type == IRE_DB_REQ_TYPE);

		/*
		 * UDP and rawip either bind with length 0 (handled above) or
		 * pass down a full ipa_t.
		 */
		if (tbr->ADDR_length != sizeof(ipa_t)) {
			ip1dbg(("ip_bind: bad UDP address length %d\n", 
				(int)tbr->ADDR_length));
			break;
		}
		src_addr = *(u32 *)ALIGN32(ipa->ip_addr);
		if (src_addr) {
			src_ire = ire_lookup(src_addr);
			/*
			 * If an address other than 0.0.0.0 is requested,
			 * we verify that it is a valid address for bind
			 * Note: Following code is in if-else-if form for
			 * readability compared to a condition check.
			 */
			/* LINTED - statement has no consequent */
			if (IRE_IS_LOCAL(src_ire)) {
				/*
				 * (2) Bind to address of local UP interface
				 */
			} else if (src_ire && src_ire->ire_type == IRE_BROADCAST) {
				/*
				 * (4) Bind to broadcast adddress
				 * Note: permitted only from transports that
				 * request IRE
				 */
				if (!ire_requested)
					error = EADDRNOTAVAIL;
			} else if ((ipif = ipif_lookup_addr(src_addr)) != NULL) {
				/*
				 * (3) Bind to address of local DOWN interface
				 * (ipif_lookup_addr() looks up all interfaces
				 * but we do not get here for UP interfaces
				 * - case (2) above)
				 */
				;
			} else if (CLASSD(src_addr)) {

				/*
				 * (5) bind to multicast address.
				 * Fake out the IRE returned to udp to
				 * be a broadcast IRE.
				 */
				src_ire = ire_lookup(~0);
				if (src_ire == NULL || !ire_requested)
					error = EADDRNOTAVAIL;

			} else {
				/*
				 * Not a valid address for bind
				 */
				error = EADDRNOTAVAIL;
			}

			if (error) {
				/* Red Alert!  Attempting to be a bogon! */
				ip1dbg(("ip_bind: bad UDP address 0x%x\n",
					(int)ntohl(src_addr)));
				break;
			}
		}
		/*
		 * Note the requested port number and IP address for use
		 * in the inbound fanout.  Validation (and uniqueness) of
		 * the port/address request is UDPs business.
		 */
		ipc->ipc_udp_addr = src_addr;
		if (protocol == IPPROTO_UDP) {
			ipc->ipc_udp_port = *(u16 *)ALIGN16(ipa->ip_port);
			ipcp = &ipc_udp_fanout[IP_UDP_HASH(ipc->ipc_udp_port)];
		} else {
			/* No hash here really.  The table is big enough. */
			ipcp = &ipc_proto_fanout[protocol];
		}
		/*
		 * Insert entries with a specified local address first
		 * in the list to give them precedence over INADDR_ANY
		 * entries.
		 */
		if (src_addr)
			ipc_hash_insert_first(ipcp, ipc);
		else
			ipc_hash_insert_last(ipcp, ipc);
		/*
		 * If IRE requested, send back a copy of the ire
		 */
		if (src_addr && ire_requested) {
			if (src_ire) {
				/*
				 * mp1 initilalized above to IRE_DB_REQ_TYPE
				 * appended mblk. Its UDP/<upper protocol>'s
				 * job to make sure there is room.
				 */
				if ((mp1->b_datap->db_lim - mp1->b_rptr)
				    < sizeof (ire_t))
					break;
				mp1->b_datap->db_type = IRE_DB_TYPE;
				mp1->b_wptr = mp1->b_rptr + sizeof (ire_t);
				bcopy((char *) src_ire, (char *) mp1->b_rptr,
				      sizeof (ire_t));
			} else {
				/*
				 * No IRE was found (but address is local)
				 * Free IRE request.
				 */
				mp->b_cont = NULL;
				freemsg(mp1);
			}
		}
		/* Send it home. */
		tbr->PRIM_type = T_BIND_ACK;
		qreply(q, mp);
		return;
	}
	case IPPROTO_TCP:
		/*
		 * The TCP case is a bit more complicated.  TCP needs to be
		 * able to verify the address used when the applications does
		 * a bind, but this happens before TCP is ready to give IP
		 * complete fanout information.  We handle two different
		 * kinds of requests from TCP here:
		 * - A four byte address is treated as a request to validate
		 * that the address is a valid local address, appropriate for
		 * an application to bind to.  IP does the verification, but
		 * does not make any note of the address at this time.
		 * - A 12 byte address contains complete fanout information
		 * consisting of local and remote addresses and ports.  In
		 * this case, the addresses are both validated as appropriate
		 * for this operation, and, if so, the information is retained
		 * for use in the inbound fanout.
		 *
		 * More value added:  In the 12-byte case, TCP can append an
		 * additional message of db_type IRE_DB_REQ_TYPE.  This
		 * indicates that TCP wants a copy of the destination IRE.
		 */
		if (tbr->ADDR_length < IP_ADDR_LEN) {
			ip1dbg(("ip_bind: bad TCP address length %d\n", 
				(int)tbr->ADDR_length));
			break;
		}
		ucp = (u_char *)ipa;
		src_addr = *(u32 *)ALIGN32(ucp);
		/*
		 * If a source address other than 0.0.0.0 was requested, try
		 * to get a corresponding IRE.
		 */
		if (tbr->ADDR_length == IP_ADDR_LEN) {
			if (src_addr) {
				/*
				 * Just verify that a valid local source
				 * address was given. We do interface lookup
				 * here (and not ire_lookup() as in UDP case
				 * where we are concerned with binding to
				 * broadcast address).
				 * This permits binding to addresses
				 * of interfaces that are down
				 * (Necessary foR BSD compatibility)
				 */
				ipif = ipif_lookup_addr(src_addr);
				if (ipif == NULL) {
					ip1dbg(("ip_bind:bad TCP source1 0x%x\n",
						(int)ntohl(src_addr)));
					error = EADDRNOTAVAIL;
					break;
				}
			}
		} else if (tbr->ADDR_length == 12) {
			mblk_t	* mp1;
			ire_t	* dst_ire;

			dst_addr = *(u32 *)ALIGN32(ucp + IP_ADDR_LEN);
			dst_ire = ire_lookup_loop(dst_addr, nilp(u32));

			/* dst_ire can't be a broadcast. */
			if (!IRE_IS_TARGET(dst_ire)) {
				ip1dbg(("ip_bind: bad TCP dst 0x%x\n", 
					(int)ntohl(dst_addr)));
				error = EHOSTUNREACH;
				break;
			}
			/*
			 * Supply a local source addr if no address was
			 * specified.
			 */
			if (!src_addr)
				src_addr = dst_ire->ire_src_addr;
			/*
			 * We do ire_lookup() here (and not
			 * interface lookup as we assert that
			 * src_addr should only come from an
			 * UP interface for hard binding.
			 */
			src_ire = ire_lookup(src_addr);
			ASSERT(src_ire != NULL);

			/*
			 * If the source address is a loopback address, the
			 * destination had best be local.
			 */
			if (src_ire->ire_type == IRE_LOOPBACK &&
			    !IRE_IS_LOCAL(dst_ire)) {
				ip1dbg(("ip_bind: bad TCP loopback\n"));
				break;
			}
			/* Check to see if TCP wants a copy of the dest IRE. */
			mp1 = mp->b_cont;
			if (mp1  &&  mp1->b_datap->db_type == IRE_DB_REQ_TYPE) {
				/* Its TCP's job to make sure there is room. */
				if ( (mp1->b_datap->db_lim - mp1->b_rptr) <
					sizeof(ire_t) )
					break;
				mp1->b_datap->db_type = IRE_DB_TYPE;
				mp1->b_wptr = mp1->b_rptr + sizeof(ire_t);
				bcopy((char *)dst_ire, (char *)mp1->b_rptr,
					sizeof(ire_t));
			}
			/* Copy the fanout information into the IPC. */
			ipc->ipc_tcp_faddr = dst_addr;
			ipc->ipc_tcp_laddr = src_addr;
			ports = *(u32 *)ALIGN32(ucp + 2 * IP_ADDR_LEN);
			ipc->ipc_tcp_ports = ports;
			/* Insert the IPC in the TCP fanout hash table. */
			ipc_hash_insert_first(&ipc_tcp_fanout[IP_TCP_HASH(
				dst_addr, ports)], ipc);
		} else
			break;
		/* Looked good, ship it back. */
		mp->b_datap->db_type = M_PCPROTO;
		tbr->PRIM_type = T_BIND_ACK;
		qreply(q, mp);
		return;
	}
bad_addr:
	if (error)
		mp = mi_tpi_err_ack_alloc(mp, TSYSERR, error);
	else
		mp = mi_tpi_err_ack_alloc(mp, TBADADDR, 0);
	if (mp)
		qreply(q, mp);
}

/*
 * Carve "len" bytes out of an mblk chain, consuming any we empty, and duping
 * the final piece where we don't.  Return a pointer to the first mblk in the
 * result, and update the pointer to the next mblk to chew on.  If anything
 * goes wrong (i.e., dupb fails), we waste everything in sight and return a
 * nil pointer.
 */
staticf mblk_t *
ip_carve_mp (mpp, len)
	mblk_t	** mpp;
	int	len;
{
reg	mblk_t	* mp0;
reg	mblk_t	* mp1;
reg	mblk_t	* mp2;

	if ( !len  ||  !mpp  || !(mp0 = *mpp) )
		return nilp(mblk_t);
	/* If we aren't going to consume the first mblk, we need a dup. */
	if ( mp0->b_wptr - mp0->b_rptr > len ) {
		mp1 = dupb(mp0);
		if ( mp1 ) {
			/* Partition the data between the two mblks. */
			mp1->b_wptr = mp1->b_rptr + len;
			mp0->b_rptr = mp1->b_wptr;
		}
		return mp1;
	}
	/* Eat through as many mblks as we need to get len bytes. */
	len -= mp0->b_wptr - mp0->b_rptr;
	for (mp2 = mp1 = mp0; (mp2 = mp2->b_cont) != 0 && len; mp1 = mp2) {
		if ( mp2->b_wptr - mp2->b_rptr > len ) {
			/*
			 * We won't consume the entire last mblk.  Like
			 * above, dup and partition it.
			 */
			mp1->b_cont = dupb(mp2);
			mp1 = mp1->b_cont;
			if ( !mp1 ) {
				/*
				 * Trouble.  Rather than go to a lot of
				 * trouble to clean up, we free the messages.
				 * This won't be any worse than losing it on
				 * the wire.
				 */
				freemsg(mp0);
				freemsg(mp2);
				*mpp = nilp(mblk_t);
				return nilp(mblk_t);
			}
			mp1->b_wptr = mp1->b_rptr + len;
			mp2->b_rptr = mp1->b_wptr;
			*mpp = mp2;
			return mp0;
		}
		/* Decrement len by the amount we just got. */
		len -= mp2->b_wptr - mp2->b_rptr;
	}
	/*
	 * len should be reduced to zero now.  If not our caller has
	 * screwed up.
	 */
	if ( len ) {
		/* Shouldn't happen! */
		freemsg(mp0);
		*mpp = nilp(mblk_t);
		return nilp(mblk_t);
	}
	/*
	 * We consumed up to exactly the end of an mblk.  Detach the part
	 * we are returning from the rest of the chain.
	 */
	mp1->b_cont = nilp(mblk_t);
	*mpp = mp2;
	return mp0;
}

/* IP module close routine.  (Always called as writer.) */
staticf int
ip_close (q)
	queue_t	* q;
{
	TRACE_1(TR_FAC_IP, TR_IP_CLOSE,
		"ip_close: q %X", q);

	qprocsoff(q);

	if (!q->q_ptr)
		return 0;
	/*
	 * Call the appropriate delete routine depending on whether this is
	 * a module or device.
	 */
	if (WR(q)->q_next) {
		MI_HRT_ACCUMULATE(ip_g_rtime,
			((ill_t *)q->q_ptr)->ill_rtime);
		ill_delete((ill_t *)q->q_ptr);
	} else {
		MI_HRT_ACCUMULATE(ip_g_wtime,
			((ipc_t *)q->q_ptr)->ipc_wtime);
		ipc_hash_remove((ipc_t *)q->q_ptr);
		if (q == ip_g_mrouter  ||  WR(q) == ip_g_mrouter)
			(void)ip_mrouter_done();
		ilg_delete_all((ipc_t *)q->q_ptr);
	}
	/* mi_close_comm frees the Instance Data structure. */
	mi_close_comm(&ip_g_head, q);
	q->q_ptr = (char *)0;
	/* Check if it is time to clean up the Named Dispatch table. */
	ip_param_cleanup();
	return 0;
}

/* Return the IP checksum for the IP header at "iph". */
u_int
ip_csum_hdr (ipha)
	ipha_t	* ipha;
{
	u16	* uph;
	u32	sum;
	int	u1;

	u1 = (ipha->ipha_version_and_hdr_length & 0xF) -
		IP_SIMPLE_HDR_LENGTH_IN_WORDS;
	uph = (u16 *)ipha;
	sum = uph[0] + uph[1] + uph[2] + uph[3] + uph[4] +
		uph[5] + uph[6] + uph[7] + uph[8] + uph[9];
	if (u1 > 0) {
		do {
			sum += uph[10];
			sum += uph[11];
			uph += 2;
		} while (--u1);
	}
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = ~(sum + (sum >> 16)) & 0xFFFF;
	if (sum == 0xffff)
		sum = 0;
	return (u_int)sum;
}

/*
 * Allocate and initialize a DLPI template of the specified length.  (May be
 * called as writer.)
 */
mblk_t *
ip_dlpi_alloc (len, prim)
	int	len;
	long	prim;
{
	mblk_t	* mp;

	mp = allocb(len, BPRI_MED);
	if (!mp)
		return nilp(mblk_t);
	mp->b_datap->db_type = M_PROTO;
	mp->b_wptr = mp->b_rptr + len;
	bzero((char *)mp->b_rptr, len);
	((dl_unitdata_req_t *)ALIGN32(mp->b_rptr))->dl_primitive = prim;
	return mp;
}

/*
 * Debug formatting routine.  Returns a character string representation of the
 * addr in buf, of the form xxx.xxx.xxx.xxx.  This routine takes the address
 * in the form of a u32 and calls ip_dot_saddr with a pointer.
 */
char *
ip_dot_addr (addr, buf)
	u32	addr;
	char	* buf;
{
	return ip_dot_saddr((u_char *)&addr, buf);
}

/*
 * Debug formatting routine.  Returns a character string representation of the
 * addr in buf, of the form xxx.xxx.xxx.xxx.  This routine takes the address
 * as a pointer.  The "xxx" parts including left zero padding so the final
 * string will fit easily in tables.  It would be nice to take a padding
 * length argument instead.
 */
staticf char *
ip_dot_saddr (addr, buf)
	u_char	* addr;
	char	* buf;
{
	mi_sprintf(buf, "%03d.%03d.%03d.%03d",
		addr[0] & 0xFF, addr[1] & 0xFF, addr[2] & 0xFF, addr[3] & 0xFF);
	return buf;
}

#if 0
/* Allocate and initialize an internal IP forwarding ticket. */
mblk_t *
ip_forwarding_ticket (q, dst)
	queue_t	* q;
	u32	dst;
{
	ipftk_t	* ipftk;
	mblk_t	* mp1;
	
	mp1 = allocb(sizeof(ipftk_t), BPRI_LO);
	if ( mp1 ) {
		ipftk = (ipftk_t *)ALIGN32(mp1->b_rptr);
		ipftk->ipftk_queue = q;
		ipftk->ipftk_dst = dst;
		mp1->b_wptr = mp1->b_rptr + sizeof(ipftk_t);
		mp1->b_datap->db_type = M_BREAK;
	}
	return mp1;
}
#endif

/* Complete the ip_wput header so that it is possible to generated ICMP
 * errors.
 */
staticf int
ip_hdr_complete (ipha)
	ipha_t	* ipha;
{
	ire_t	*ire = nilp(ire_t);

	if (ipha->ipha_src)
		ire = ire_lookup(ipha->ipha_src);
	if (!ire || (ire->ire_type & (IRE_LOCAL|IRE_LOOPBACK)) == 0) {
		ire = ire_lookup_local();
	}
	if (!ire || (ire->ire_type & (IRE_LOCAL|IRE_LOOPBACK)) == 0) {
		ip0dbg(("ip_hdr_complete: no source IRE\n"));
		return 1;
	}
	ipha->ipha_src = ire->ire_addr;
	ipha->ipha_ttl = ip_def_ttl;
	ipha->ipha_hdr_checksum = 0;
	ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);
	return 0;
}

/* Nobody should be sending packets up this stream */
staticf void
ip_lrput (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	mblk_t * mp1;

	switch (mp->b_datap->db_type) {
	case M_FLUSH:
		/* Turn around */
		if (*mp->b_rptr & FLUSHW) {
			*mp->b_rptr &= ~FLUSHR;
			qreply(q, mp);
			return;
		}
		break;
	}
	/* Could receive messages that passed through ar_rput */
	for (mp1 = mp; mp1; mp1 = mp1->b_cont)
		mp1->b_prev = mp1->b_next = nilp(mblk_t);
	freemsg(mp);
}

/* Nobody should be sending packets down this stream */
/* ARGSUSED */
staticf void
ip_lwput (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	freemsg(mp);
}

/* 
 * Move the first hop in any source route to ipha_dst 
 * and remove that part of the source route.
 * Called by other protocols.
 * Errors in option formatting are ignored - will be handled by ip_wput_options
 * Return the final destination (either ipha_dst or the last entry in 
 * a source route.)
 */
u32
ip_massage_options (ipha)
	ipha_t	* ipha;
{
	u32	totallen;
	u_char	* opt;
	u32	optval;
	u32	optlen;
	u32	dst;
	int	i;

	ip2dbg(("ip_massge_options\n"));
	dst = ipha->ipha_dst;
	totallen = ipha->ipha_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	opt = (u_char *)&ipha[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			return dst;
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}
		if (optlen == 0 || optlen > totallen) 
			return dst;

		switch (optval) {
			u32 off;
		case IPOPT_SSRR:
		case IPOPT_LSRR: 
			off = opt[IPOPT_POS_OFF];
			if (off < IPOPT_MINOFF_SR) {
				ip1dbg(("ip_massge_options: bad option offset %d\n",
					off));
				return dst;
			}
			off--;
			if (optlen < IP_ADDR_LEN ||
			    off > optlen - IP_ADDR_LEN) {
				/* End of source route */
				ip1dbg(("ip_massge_options: end of SR\n"));
				break;
			}
			bcopy((char *)opt + off, (char *)&dst, IP_ADDR_LEN);
			ip1dbg(("ip_massge_options: next hop 0x%x\n",
				(int)ntohl(dst)));
			/*
			 * Update ipha_dst to be the first hop and remove the
			 * first hop from the source route (by overwriting
			 * part of the option with NOP options)
			 */
			ipha->ipha_dst = dst;
			/* Put the last entry in dst */
			off = ((optlen - IP_ADDR_LEN - 3) &
			       ~(IP_ADDR_LEN-1)) + 3;
			bcopy((char *)&opt[off], (char *)&dst, IP_ADDR_LEN);
			ip1dbg(("ip_massge_options: last hop 0x%x\n",
				(int)ntohl(dst)));
			/* Move down and overwrite */
			opt[IP_ADDR_LEN] = opt[0];
			opt[IP_ADDR_LEN+1] = opt[IPOPT_POS_LEN] - IP_ADDR_LEN;
			opt[IP_ADDR_LEN+2] = opt[IPOPT_POS_OFF];
			for (i = 0; i < IP_ADDR_LEN; i++)
				opt[i] = IPOPT_NOP;
			break;
		}
		totallen -= optlen;
		opt += optlen;
	}
	return dst;
}


/* Return the network mask associated with the specified address. */
u32
ip_net_mask (addr)
	u32	addr;
{
	u_char	* up = (u_char *)&addr;
	u32	mask = 0;
	u_char	* maskp = (u_char *)&mask;

#ifdef i386
#define TOTALLY_BRAIN_DAMAGED_C_COMPILER
#endif
#ifdef  TOTALLY_BRAIN_DAMAGED_C_COMPILER
	maskp[0] = maskp[1] = maskp[2] = maskp[3] = 0;
#endif
	if (CLASSD(addr)) {
		maskp[0] = 0xF0;
		return mask;
	}
	if (addr == 0)
		return 0;
	maskp[0] = 0xFF;
	if ((up[0] & 0x80) == 0)
		return mask;

	maskp[1] = 0xFF;
	if ((up[0] & 0xC0) == 0x80)
		return mask;

	maskp[2] = 0xFF;
	if ((up[0] & 0xE0) == 0xC0)
		return mask;

	/* Must be experimental or multicast, indicate as much */
	return (u32)0;
}

/*
 * ip_newroute is called by ip_rput or ip_wput whenever we need to send
 * out a packet to a destination address for which we do not have specific
 * routing information.
 */
staticf void
ip_newroute (q, mp, dst)
	queue_t	* q;
	mblk_t	* mp;
	u32	dst;
{
	areq_t	* areq;
	u32	gw = 0;
	ire_t	* ire;
	mblk_t	* res_mp;
	queue_t	* stq;
	u32	* u32p;

	ip1dbg(("ip_newroute: dst 0x%x\n", (int)ntohl(dst)));

	/* All multicast lookups come through ip_newroute_ipif() */
	if (CLASSD(dst)) {
		ip0dbg(("ip_newroute: CLASSD 0x%x (b_prev 0x%x, b_next 0x%x)\n",
			(int)ntohl(dst), (int)mp->b_prev, (int)mp->b_next));
		freemsg(mp);
		return;
	}
	/*
	 * Get what we can from ire_lookup_loop which will follow an IRE
	 * chain until it gets the most specific information available.
	 * For example, we know that there is no IRE_ROUTE for this dest,
	 * but there may be an IRE_NET which specifies a gateway.
	 * ire_lookup_loop will look up the gateway, etc.
	 */
	ire = ire_lookup_loop(dst, &gw);
	if (!ire)
		goto err_ret;
	
	if ( gw )
		ip_def_gateway_index++;
	ip2dbg(("ip_newroute: first hop 0x%x\n", (int)ntohl(gw)));
	ip2dbg(("\tire type %s (%d)\n",
		 ip_nv_lookup(ire_nv_tbl, ire->ire_type), ire->ire_type));
	stq = ire->ire_stq;
	switch (ire->ire_type) {
	case IRE_ROUTE:
		/* We have what we need to build an IRE_ROUTE. */
		ire = ire_create(
			(u_char *)&dst,			/* dest address */
			(u_char *)&ip_g_all_ones,	/* mask */
			(u_char *)&ire->ire_src_addr,	/* source address */
			(u_char *)&gw,			/* gateway address */
			ire->ire_max_frag,
			ire->ire_ll_hdr_mp,		/* xmit header */
			ire->ire_rfq,			/* recv-from queue */
			stq,				/* send-to queue */
			IRE_ROUTE,
			ire->ire_rtt,
			ire->ire_ll_hdr_length
		);
		if (!ire)
			break;

		/* Remember the packet we want to xmit */
		ire->ire_mp->b_cont = mp;

		/*
		 * Need to become writer to add the IRE.  We continue
		 * in ire_add_then_put.
		 */
		become_writer(q, ire->ire_mp, (pfi_t)ire_add_then_put);
		return;
	case IRE_SUBNET: {
		/* We have what we need to build an IRE_ROUTE. */
		mblk_t	*ll_hdr_mp;
		ill_t	*ill;

		/*
		 * Create a new ll_hdr_mp with the
		 * IP gateway address in destination address in the DLPI hdr.
		 */
		ill = ire_to_ill(ire);
		if (!ill) {
			ip0dbg(("ip_newroute: ire_to_ill failed\n"));
			break;
		}
		if (ill->ill_phys_addr_length == IP_ADDR_LEN) {
			u_char *addr;

			if (gw)
				addr = (u_char *)&gw;
			else
				addr = (u_char *)&dst;
			ll_hdr_mp = ill_dlur_gen(addr,
						 ill->ill_phys_addr_length,
						 ill->ill_sap,
						 ill->ill_sap_length);
		} else if (ire->ire_ll_hdr_mp)
			ll_hdr_mp = dupb(ire->ire_ll_hdr_mp);
		else
			break;

		if (!ll_hdr_mp)
			break;
		
		ire = ire_create(
			(u_char *)&dst,			/* dest address */
			(u_char *)&ip_g_all_ones,	/* mask */
			(u_char *)&ire->ire_src_addr,	/* source address */
			(u_char *)&gw,			/* gateway address */
			ire->ire_max_frag,
			ll_hdr_mp,			/* xmit header */
			ire->ire_rfq,			/* recv-from queue */
			stq,				/* send-to queue */
			IRE_ROUTE,
			ire->ire_rtt,
			ire->ire_ll_hdr_length
		);
		freeb(ll_hdr_mp);
		if (!ire)
			break;

		/* Remember the packet we want to xmit */
		ire->ire_mp->b_cont = mp;

		/*
		 * Need to become writer to add the IRE.  We continue
		 * in ire_add_then_put.
		 */
		become_writer(q, ire->ire_mp, (pfi_t)ire_add_then_put);
		return;
	}
	case IRE_RESOLVER:
		/*
		 * We can't build an IRE_ROUTE yet, but at least we found a
		 * resolver that can help.
		 */
		res_mp = ire->ire_ll_hdr_mp;
		if (!stq
		||  !OK_RESOLVER_MP(res_mp)
		||  !(res_mp = copyb(res_mp)))
			break;
		/*
		 * To be at this point in the code with a non-zero gw means
		 * that dst is reachable through a gateway that we have never
		 * resolved.  By changing dst to the gw addr we resolve the
		 * gateway first.  When ire_add_then_put() tries to put the IP
		 * dg to dst, it will reenter ip_newroute() at which time we
		 * will find the IRE_ROUTE for the gw and create another
		 * IRE_ROUTE in case IRE_ROUTE above.
		 */
		if ( gw ) {
			dst = gw;
			gw = 0;
		}
		/* NOTE: a resolvers rfq is nil and its stq points upstream. */
		/*
		 * We obtain a partial IRE_ROUTE which we will pass along with
		 * the resolver query.  When the response comes back it will be
		 * there ready for us to add.
		 */
		ire = ire_create(
			(u_char *)&dst,			/* dest address */
			(u_char *)&ip_g_all_ones,	/* mask */
			(u_char *)&ire->ire_src_addr,	/* source address */
			(u_char *)&gw,			/* gateway address */
			ire->ire_max_frag,
			res_mp,				/* xmit header */
			stq,				/* recv-from queue */
			OTHERQ(stq),			/* send-to queue */
			IRE_ROUTE,
			ire->ire_rtt,
			ire->ire_ll_hdr_length
		);
		freeb(res_mp);
		if (!ire)
			break;

		/*
		 * Construct message chain for the resolver of the form:
		 * 	ARP_REQ_MBLK-->IRE_MBLK-->Packet
		 */
		ire->ire_mp->b_cont = mp;
		mp = ire->ire_ll_hdr_mp;
		ire->ire_ll_hdr_mp = nilp(mblk_t);
		linkb(mp, ire->ire_mp);

		/*
		 * Fill in the source and dest addrs for the resolver.
		 * NOTE: this depends on memory layouts imposed by ill_init().
		 */
		areq = (areq_t *)ALIGN32(mp->b_rptr);
		u32p = (u32 *)ALIGN32((char *)areq + areq->areq_sender_addr_offset);
		*u32p = ire->ire_src_addr;
		u32p = (u32 *)ALIGN32((char *)areq + areq->areq_target_addr_offset);
		*u32p = dst;
		/* Up to the resolver. */
		putnext(stq, mp);
		/*
		 * The response will come back in ip_wput with db_type
		 * IRE_DB_TYPE.
		 */
		return;
	default:
		break;
	}

err_ret:
	ip1dbg(("ip_newroute: dropped\n"));
	/* Did this packet originate externally? */
	if (mp->b_prev) {
		mp->b_next = nilp(mblk_t);
		mp->b_prev = nilp(mblk_t);
		q = WR(q);
	} else {
		/*
		 * Since ip_wput() isn't close to finished, we fill
		 * in enough of the header for credible error reporting.
		 */
		if (ip_hdr_complete((ipha_t *)ALIGN32(mp->b_rptr))) {
			/* Failed */
			freemsg(mp);
			return;
		}
	}
	if (ip_source_routed((ipha_t *)ALIGN32(mp->b_rptr))) {
		icmp_unreachable(q, mp, ICMP_SOURCE_ROUTE_FAILED);
		return;
	}
	/* 
	 * TODO: more precise characterization, host or net.
	 * We can only send a net redirect if the destination network
	 * is not subnetted.
	 */
	icmp_unreachable(q, mp, ICMP_HOST_UNREACHABLE);
}

/*
 * ip_newroute_ipif is called by ip_wput_multicast and 
 * ip_rput_forward_multicast whenever we need to send
 * out a packet to a destination address for which we do not have specific
 * routing information. It is used when the packet will be sent out
 * on a specific interface.
 */
staticf void
ip_newroute_ipif (q, mp, ipif, dst)
	queue_t	* q;
	mblk_t	* mp;
	ipif_t	* ipif;
	u32	dst;
{
	areq_t	* areq;
	ire_t	* ire;
	mblk_t	* res_mp;
	queue_t	* stq;
	u32	* u32p;

	ip1dbg(("ip_newroute_ipif: dst 0x%x, if %s\n", (int)ntohl(dst),
		 ipif->ipif_ill->ill_name));
	/*
	 * If the interface is a pt-pt interface we look for an IRE_RESOLVER
	 * or IRE_SUBNET that matches both the local_address and the pt-pt
	 * destination address. Other wise we just match the local address.
	 */
	if (!(ipif->ipif_flags & IFF_MULTICAST))
		goto err_ret;

	ire = ipif_to_ire(ipif);
	if (!ire)
		goto err_ret;
	
	ip1dbg(("ip_newroute_ipif: interface type %s (%d), address 0x%x\n", 
		ip_nv_lookup(ire_nv_tbl, ire->ire_type), ire->ire_type,
		(int)ntohl(ire->ire_src_addr)));
	stq = ire->ire_stq;
	switch (ire->ire_type) {
	case IRE_SUBNET: {
		/* We have what we need to build an IRE_ROUTE. */
		mblk_t	*ll_hdr_mp;
		ill_t	*ill;

		/*
		 * Create a new ll_hdr_mp with the
		 * IP gateway address in destination address in the DLPI hdr.
		 */
		ill = ire_to_ill(ire);
		if (!ill) {
			ip0dbg(("ip_newroute: ire_to_ill failed\n"));
			break;
		}
		if (ill->ill_phys_addr_length == IP_ADDR_LEN) {
			ll_hdr_mp = ill_dlur_gen((u_char *)&dst,
						 ill->ill_phys_addr_length,
						 ill->ill_sap,
						 ill->ill_sap_length);
		} else if (ire->ire_ll_hdr_mp)
			ll_hdr_mp = dupb(ire->ire_ll_hdr_mp);
		else
			break;

		if (!ll_hdr_mp)
			break;
		
		ire = ire_create(
			(u_char *)&dst,			/* dest address */
			(u_char *)&ip_g_all_ones,	/* mask */
			(u_char *)&ire->ire_src_addr,	/* source address */
			nilp(u_char),			/* gateway address */
			ire->ire_max_frag,
			ll_hdr_mp,			/* xmit header */
			ire->ire_rfq,			/* recv-from queue */
			stq,				/* send-to queue */
			IRE_ROUTE,
			ire->ire_rtt,
			ire->ire_ll_hdr_length
		);
		freeb(ll_hdr_mp);
		if (!ire)
			break;

		/* Remember the packet we want to xmit */
		ire->ire_mp->b_cont = mp;

		/*
		 * Need to become writer to add the IRE.  We continue
		 * in ire_add_then_put.
		 */
		become_writer(q, ire->ire_mp, (pfi_t)ire_add_then_put);
		return;
	}
	case IRE_RESOLVER:
		/*
		 * We can't build an IRE_ROUTE yet, but at least we found a
		 * resolver that can help.
		 */
		res_mp = ire->ire_ll_hdr_mp;
		if (!stq
		||  !OK_RESOLVER_MP(res_mp)
		||  !(res_mp = copyb(res_mp)))
			break;

		/* NOTE: a resolvers rfq is nil and its stq points upstream. */
		/*
		 * We obtain a partial IRE_ROUTE which we will pass along with
		 * the resolver query.  When the response comes back it will be
		 * there ready for us to add.
		 */
		ire = ire_create(
			(u_char *)&dst,			/* dest address */
			(u_char *)&ip_g_all_ones,	/* mask */
			(u_char *)&ire->ire_src_addr,	/* source address */
			nilp(u_char),			/* gateway address */
			ire->ire_max_frag,
			res_mp,				/* xmit header */
			stq,				/* recv-from queue */
			OTHERQ(stq),			/* send-to queue */
			IRE_ROUTE,
			ire->ire_rtt,
			ire->ire_ll_hdr_length
		);
		freeb(res_mp);
		if (!ire)
			break;

		/*
		 * Construct message chain for the resolver of the form:
		 * 	ARP_REQ_MBLK-->IRE_MBLK-->Packet
		 */
		ire->ire_mp->b_cont = mp;
		mp = ire->ire_ll_hdr_mp;
		ire->ire_ll_hdr_mp = nilp(mblk_t);
		linkb(mp, ire->ire_mp);

		/*
		 * Fill in the source and dest addrs for the resolver.
		 * NOTE: this depends on memory layouts imposed by ill_init().
		 */
		areq = (areq_t *)ALIGN32(mp->b_rptr);
		u32p = (u32 *)ALIGN32((char *)areq + areq->areq_sender_addr_offset);
		*u32p = ire->ire_src_addr;
		u32p = (u32 *)ALIGN32((char *)areq + areq->areq_target_addr_offset);
		*u32p = dst;
		/* Up to the resolver. */
		putnext(stq, mp);
		/*
		 * The response will come back in ip_wput with db_type
		 * IRE_DB_TYPE.
		 */
		return;
	default:
		break;
	}

err_ret:
	ip1dbg(("ip_newroute_ipif: dropped\n"));
	/* Did this packet originate externally? */
	if (mp->b_prev || mp->b_next) {
		mp->b_next = nilp(mblk_t);
		mp->b_prev = nilp(mblk_t);
	} else {
		/*
		 * Since ip_wput() isn't close to finished, we fill
		 * in enough of the header for credible error reporting.
		 */
		if (ip_hdr_complete((ipha_t *)ALIGN32(mp->b_rptr))) {
			/* Failed */
			freemsg(mp);
			return;
		}
	}
	/* 
	 * TODO: more precise characterization, host or net.
	 * We can only send a net redirect if the destination network
	 * is not subnetted.
	 */
	icmp_unreachable(q, mp, ICMP_HOST_UNREACHABLE);
}

#if 0
/* Do-nothing esballoc callback routine. */
staticf	void
ip_nothing () {
	return;
}
#endif

/* Name/Value Table Lookup Routine */
char *
ip_nv_lookup (nv, value)
	nv_t	* nv;
	int	value;
{
	if (!nv)
		return nilp(char);
	for ( ; nv->nv_name; nv++) {
		if (nv->nv_value == value)
			return nv->nv_name;
	}
	return "unknown";
}

/* IP Module open routine.  (Always called as writer.) */
staticf int
ip_open (q, devp, flag, sflag, credp)
reg	queue_t	* q;
	dev_t	* devp;
	int	flag;
	int	sflag;
	cred_t	* credp;
{
	int	err;
reg	ipc_t	* ipc;
	queue_t	* downstreamq;
	boolean_t	priv = false;

	TRACE_1(TR_FAC_IP, TR_IP_OPEN,
		"ip_open: q %X", q);

	/* Allow reopen. */
	if (q->q_ptr)
		return 0;

	/*
	 * If this is the first open, set up the Named Dispatch table and
	 * initialize the loopback device.
	 */
	if ( !ip_g_nd
	&& ( !nd_load(&ip_g_nd, "ip_ill_status", ip_ill_report, nil(pfi_t),
		nil(caddr_t))
	||   !nd_load(&ip_g_nd, "ip_ipif_status", ip_ipif_report, nil(pfi_t),
		nil(caddr_t))
	||   !nd_load(&ip_g_nd, "ip_ire_status", ip_ire_report, nil(pfi_t),
		nil(caddr_t))
	||   !nd_load(&ip_g_nd, "ip_rput_pullups", nd_get_long, nd_set_long,
		(caddr_t)&ip_rput_pullups)
#ifdef	MI_HRTIMING
	||   !nd_load(&ip_g_nd, "ip_times", ip_time_report, ip_time_reset,
		nil(caddr_t))
	||   !nd_load(&ip_g_nd, "ip_times_flush", nil(pfi_t), ip_time_flush,
		nil(caddr_t))
#endif
	||   !ip_param_register(lcl_param_arr, A_CNT(lcl_param_arr))
	||   !ipif_loopback_init()) ) {
		ip_param_cleanup();
		return ENOMEM;
	}
	
	/*
	 * We are D_MTPERMOD so it is safe to do qprocson before allocating
	 * q_ptr.
	 */
	qprocson(q);
	/*
	 * We are either opening as a device or module.  In the device case,
	 * this is an IP client stream, and we allocate an ipc_t as the
	 * instance data.  If it is a module open, then this is a control
	 * stream for access to a DLPI device.  We allocate an ill_t as the
	 * instance data in this case.
	 */
	downstreamq = WR(q)->q_next;
	if (err = mi_open_comm(&ip_g_head,
		downstreamq ? sizeof(ill_t) : sizeof(ipc_t), q, devp, flag,
		sflag, credp)) {
		ip_param_cleanup();
		qprocsoff(q);
		return err;
	}
	if ( credp  &&  drv_priv(credp) == 0 )
		priv = true;
	if (downstreamq) {
		/* Initialize the new ILL. */
		if (err = ill_init(q, (ill_t *)q->q_ptr)) {
			mi_close_comm(&ip_g_head, q);
			ip_param_cleanup();
			qprocsoff(q);
			return err;
		}
		if ( priv )
			((ill_t *)q->q_ptr)->ill_priv_stream = 1;
	} else {
		/* Initialize the new IPC. */
		ipc = (ipc_t *)q->q_ptr;
		ipc->ipc_rq = q;
		ipc->ipc_wq = WR(q);
		/* Non-zero default values */
		ipc->ipc_multicast_ttl = IP_DEFAULT_MULTICAST_TTL;
		ipc->ipc_multicast_loop = IP_DEFAULT_MULTICAST_LOOP;
		if ( priv )
			ipc->ipc_priv_stream = 1;
	}
	return 0;
}

/*
 * Options management requests that set values are passed down to IP from TCP
 * or UDP.  We scan through the new options and note the ones of interest.
 * The message is a copy, which we discard silently when through amusing
 * ourselves.  The upper level protocol has already scanned and approved of the
 * format of the buffer, so we don't do sanity check here.  (Always called
 * as writer.)
 */
staticf void
ip_optmgmt_req (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	int	error = 0;
	ipc_t	* ipc = (ipc_t *)q->q_ptr;
	int	* i1;
	struct opthdr * opt;
	struct opthdr * opt_end;
	boolean_t	priv;
	struct T_optmgmt_req * tor = 
		(struct T_optmgmt_req *)ALIGN32(mp->b_rptr);
	
	priv = IS_PRIVILEGED_QUEUE(q);
	opt = (struct opthdr *)ALIGN32((u_char *)tor + tor->OPT_offset);
	opt_end = (struct opthdr *)ALIGN32((u_char *)opt + tor->OPT_length);
	for ( ; opt < opt_end;
		opt = (struct opthdr *)ALIGN32((u_char *)&opt[1] + opt->len) ) {
		i1 = (int *)&opt[1];
		switch ( opt->level ) {
		case SOL_SOCKET:
			switch ( opt->name ) {
			case SO_BROADCAST:
				/* TODO: check someplace? */
				ipc->ipc_broadcast = *i1 ? 1 : 0;
				break;
			case SO_USELOOPBACK:
				/* TODO: check someplace? */
				ipc->ipc_loopback = *i1 ? 1 : 0;
				break;
			case SO_DONTROUTE:
				ipc->ipc_dontroute = *i1 ? 1 : 0;
				break;
			case SO_REUSEADDR:
				ipc->ipc_reuseaddr = *i1 ? 1 : 0;
				break;
			default:
				break;
			}
			break;
		case IPPROTO_IP:
			switch ( opt->name ) {
			case IP_MULTICAST_IF:
				ip2dbg(("ip_optmgt_req: MULTICAST IF\n"));
				if (*i1 == 0) {	/* Reset */
					ipc->ipc_multicast_ipif =
						nilp(ipif_t);
					break;
				}
				ipc->ipc_multicast_ipif =
					ipif_lookup_addr((u32)*i1);
				if (!ipc->ipc_multicast_ipif)
					error = EHOSTUNREACH;
				break;
			case IP_MULTICAST_TTL:
				ip2dbg(("ip_optmgt_req: MULTICAST TTL\n"));
				ipc->ipc_multicast_ttl = *(char *)&opt[1];
				break;
			case IP_MULTICAST_LOOP:
				ip2dbg(("ip_optmgt_req: MULTICAST LOOP\n"));
				ipc->ipc_multicast_loop =
					*(char *)&opt[1] ? 1 : 0;
				break;
			case IP_ADD_MEMBERSHIP:
				ip2dbg(("ip_optmgt_req: ADD MEMBER\n"));
				error = ip_opt_add_group(ipc, (u32)i1[0], 
							 (u32)i1[1]);
				break;
			case IP_DROP_MEMBERSHIP:
				ip2dbg(("ip_optmgt_req: DROP MEMBER\n"));
				error = ip_opt_delete_group(ipc, (u32)i1[0], 
							    (u32)i1[1]);
				break;
			case IP_HDRINCL:
			case IP_OPTIONS:
			case IP_TOS:
			case IP_TTL:
				break;
			default:
				if (!priv) {
					error = EPERM;
					break;
				}
				error = ip_mrouter_cmd((int)opt->name, q, 
					(char *)&opt[1], (int)opt->len);
				break;
			}
			break;
			/* Can not run this as become_writer due to 
			 * reordering problems.
			 */
		default:
			if (opt->level > IPPROTO_MAX) {
				if (snmpcom_req(q, mp, ip_snmp_set,
						ip_snmp_get, priv))
					return;
			}
			break;
		}
	}
	if (tor->PRIM_type == T_OPTMGMT_ACK) {
		ip2dbg(("ip_optmgmt_req: optcom replied\n"));
		freemsg(mp);
		return;
	}

	if (error) {
		/* Send error ACK */
		ip1dbg(("ip_optmgmt_req: error %d\n", error));
		if ((mp = mi_tpi_err_ack_alloc(mp, TSYSERR, error)) != NULL)
			qreply(q, mp);
	} else {
		/* OK ACK already set up by caller except this */
		tor->PRIM_type = T_OPTMGMT_ACK;
		ip2dbg(("ip_optmgmt_req: OK ACK\n"));
		qreply(q, mp);
	}
}

/*
 * Return 1 is there is something for ip_optmgmt_req to do that
 * requires the write lock in IP.
 */
staticf int
ip_optmgmt_writer (mp)
	mblk_t	* mp;
{
	struct opthdr * opt;
	struct opthdr * opt_end;
	struct T_optmgmt_req * tor = 
		(struct T_optmgmt_req *)ALIGN32(mp->b_rptr);
	
	opt = (struct opthdr *)ALIGN32((u_char *)tor + tor->OPT_offset);
	opt_end = (struct opthdr *)ALIGN32((u_char *)opt + tor->OPT_length);
	for ( ; opt < opt_end;
		opt = (struct opthdr *)ALIGN32((u_char *)&opt[1] + opt->len) ) {
		switch ( opt->level ) {
		case SOL_SOCKET:
			switch ( opt->name ) {
			case SO_BROADCAST:
			case SO_USELOOPBACK:
			case SO_DONTROUTE:
			case SO_REUSEADDR:
				return (1);
			default:
				break;
			}
			break;
		case IPPROTO_IP:
			return (1);
		default:
			if (opt->level > IPPROTO_MAX) {
				/* For snmpcom_req */
				return (1);
			}
			break;
		}
	}
	return (0);
}

/*
 * ip_param_cleanup is called whenever an IP Instance Data structure has been
 * freed in ip_close, or when an open fails.  We check to see if the last
 * Instance has gone away, in which case we free the Named Dispatch
 * table.
 */
staticf void
ip_param_cleanup ()
{
	if (!ip_g_head)
		nd_free(&ip_g_nd);
}

/* Named Dispatch routine to get a current value out of our parameter table. */
/* ARGSUSED */
staticf int
ip_param_get (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	ipparam_t	* ippa = (ipparam_t *)ALIGN32(cp);

	mi_mpprintf(mp, "%ld", ippa->ip_param_value);
	return 0;
}

/*
 * Walk through the param array specified registering each element with the
 * Named Dispatch handler.
 */
staticf boolean_t
ip_param_register (ippa, cnt)
reg	ipparam_t	* ippa;
	int	cnt;
{
	for ( ; cnt-- > 0; ippa++) {
		if (ippa->ip_param_name  &&  ippa->ip_param_name[0]) {
			if ( !nd_load(&ip_g_nd, ippa->ip_param_name,
				ip_param_get, ip_param_set,
				(caddr_t)ippa) ) {
				nd_free(&ip_g_nd);
				return false;
			}
		}
	}
	return true;
}

/* Named Dispatch routine to negotiate a new value for one of our parameters. */
/* ARGSUSED */
staticf int
ip_param_set (q, mp, value, cp)
	queue_t	* q;
	mblk_t	* mp;
	char	* value;
	caddr_t	cp;
{
	char	* end;
	long	new_value;
	ipparam_t	* ippa = (ipparam_t *)ALIGN32(cp);

	new_value = mi_strtol(value, &end, 10);
	if (end == value
	||  new_value < ippa->ip_param_min
	||  new_value > ippa->ip_param_max)
		return EINVAL;
	ippa->ip_param_value = new_value;
	return 0;
}

/*
 * Hard case IP fragmentation reassembly.  If a fragment shows up out of order,
 * it gets passed in here.  When an ipf is passed here for the first time, if
 * we already have in-order fragments on the queue, we convert from the fast-
 * path reassembly scheme to the hard-case scheme.  From then on, additional
 * fragments are reassembled here.  We keep track of the start and end offsets
 * of each piece, and the number of holes in the chain.  When the hole count
 * goes to zero, we are done!
 *
 * The ipf_count will be updated to account for any mblk(s) added (pointed to
 * by mp) or subtracted (freeb()ed dups), upon return the caller must update
 * ipfb_count and ill_frag_count by the difference of ipf_count before and
 * after the call to ip_reassemble().
 */
staticf boolean_t
ip_reassemble (mp, ipf, offset_and_flags, stripped_hdr_len)
	mblk_t	* mp;
	ipf_t	* ipf;
	u32	offset_and_flags;
	int	stripped_hdr_len;
{
	u32	end;
	mblk_t	* next_mp;
reg	mblk_t	* mp1;
	boolean_t	more = offset_and_flags & IPH_MF;
	u32	start = (offset_and_flags & IPH_OFFSET) << 3;
	u32	u1;

	if ( ipf->ipf_end ) {
		/*
		 * We were part way through in-order reassembly, but now there
		 * is a hole.  We walk through messages already queued, and
		 * mark them for hard case reassembly.  We know that up till
		 * now they were in order starting from offset zero.
		 */
		u1 = 0;
		for ( mp1 = ipf->ipf_mp->b_cont; mp1; mp1 = mp1->b_cont ) {
			IP_REASS_SET_START(mp1, u1);
			if ( u1 )
				u1 += mp1->b_wptr - mp1->b_rptr;
			else
				u1 = (mp1->b_wptr - mp1->b_rptr) -
					IPH_HDR_LENGTH(mp1->b_rptr);
			IP_REASS_SET_END(mp1, u1);
		}
		/* One hole at the end. */
		ipf->ipf_hole_cnt = 1;
		/* Brand it as a hard case, forever. */
		ipf->ipf_end = 0;
	}
	/* Walk through all the new pieces. */
	do {
		end = start + (mp->b_wptr - mp->b_rptr);
		if (start == 0)
			/* First segment */
			end -= IPH_HDR_LENGTH(mp->b_rptr);

		next_mp = mp->b_cont;
		if (start == end) {
			/* Empty.  Blast it. */
			IP_REASS_SET_START(mp, 0);
			IP_REASS_SET_END(mp, 0);
			freeb(mp);
			continue;
		}
		/* Add in byte count */
		ipf->ipf_count += mp->b_datap->db_lim -
				  mp->b_datap->db_base;
		mp->b_cont = nilp(mblk_t);
		IP_REASS_SET_START(mp, start);
		IP_REASS_SET_END(mp, end);
		if ( !ipf->ipf_tail_mp ) {
			ipf->ipf_tail_mp = mp;
			ipf->ipf_mp->b_cont = mp;
			if ( start == 0  ||  !more )
				ipf->ipf_hole_cnt = 1;
			else
				ipf->ipf_hole_cnt = 2;
			ipf->ipf_stripped_hdr_len = stripped_hdr_len;
			continue;
		}
		/* New stuff at or beyond tail? */
		u1 = IP_REASS_END(ipf->ipf_tail_mp);
		if ( start >= u1 ) {
			/* Link it on end. */
			ipf->ipf_tail_mp->b_cont = mp;
			ipf->ipf_tail_mp = mp;
			if ( more ) {
				if ( start != u1 )
					ipf->ipf_hole_cnt++;
			} else if ( start == u1 )
				ipf->ipf_hole_cnt--;
			continue;
		}
		mp1 = ipf->ipf_mp->b_cont;
		u1 = IP_REASS_START(mp1);
		/* New stuff at the front? */
		if ( start < u1 ) {
			ipf->ipf_stripped_hdr_len = stripped_hdr_len;
			if ( start == 0 ) {
				if ( end >= u1 ) {
					/* Nailed the hole at the begining. */
					ipf->ipf_hole_cnt--;
				}
			} else if ( end < u1 ) {
				/*
				 * A hole, stuff, and a hole where there used
				 * to be just a hole.
				 */
				ipf->ipf_hole_cnt++;
			}
			mp->b_cont = mp1;
			/* Check for overlap. */
			while ( end > u1 ) {
				if ( end < IP_REASS_END(mp1) ) {
					mp->b_wptr -= (int)(end - u1);
					IP_REASS_SET_END(mp, u1);
					BUMP_MIB(ip_mib.ipReasmPartDups);
					break;
				}
				/* Did we cover another hole? */
				if ( (mp1->b_cont
				     &&  IP_REASS_END(mp1)
				     != IP_REASS_START(mp1->b_cont)
				     &&  end >= IP_REASS_START(mp1->b_cont))
				||  !more )
					ipf->ipf_hole_cnt--;
				/* Clip out mp1. */
				if (! (mp->b_cont = mp1->b_cont))
					/*
					 * After clipping out mp1, this guy
					 * is now hanging off the end.
					 */
					ipf->ipf_tail_mp = mp;
				IP_REASS_SET_START(mp1, 0);
				IP_REASS_SET_END(mp1, 0);
				/* Subtract byte count */
				ipf->ipf_count -= mp1->b_datap->db_lim -
						  mp1->b_datap->db_base;
				freeb(mp1);
				BUMP_MIB(ip_mib.ipReasmPartDups);
				mp1 = mp->b_cont;
				if ( !mp1 )
					break;
				u1 = IP_REASS_START(mp1);
			}
			ipf->ipf_mp->b_cont = mp;
			continue;
		}
		/*
		 * The new piece starts somewhere between the start of the head
		 * and before the end of the tail.
		 */
		for ( ; mp1; mp1 = mp1->b_cont ) {
			u1 = IP_REASS_END(mp1);
			if ( start < u1 ) {
				if ( end <= u1 ) {
					/* Nothing new. */
					IP_REASS_SET_START(mp, 0);
					IP_REASS_SET_END(mp, 0);
					/* Subtract byte count */
					ipf->ipf_count -= mp->b_datap->db_lim -
							  mp->b_datap->db_base;
					freeb(mp);
					BUMP_MIB(ip_mib.ipReasmDuplicates);
					break;
				}
				/*
				 * Trim redundant stuff off beginning of new
				 * piece.
				 */
				IP_REASS_SET_START(mp, u1);
				mp->b_rptr += (int)(u1 - start);
				BUMP_MIB(ip_mib.ipReasmPartDups);
				start = u1;
				if ( !mp1->b_cont ) {
					/*
					 * After trimming, this guy is now
					 * hanging off the end.
					 */
					mp1->b_cont = mp;
					ipf->ipf_tail_mp = mp;
					if ( !more )
						ipf->ipf_hole_cnt--;
					break;
				}
			}
			if ( start >= IP_REASS_START(mp1->b_cont) )
				continue;
			/* Fill a hole */
			if ( start > u1 )
				ipf->ipf_hole_cnt++;
			mp->b_cont = mp1->b_cont;
			mp1->b_cont = mp;
			mp1 = mp->b_cont;
			u1 = IP_REASS_START(mp1);
			if ( end >= u1 ) {
				ipf->ipf_hole_cnt--;
				/* Check for overlap. */
				while ( end > u1 ) {
					if ( end < IP_REASS_END(mp1) ) {
						mp->b_wptr -= (int)(end - u1);
						IP_REASS_SET_END(mp, u1);
						/* TODO we might bump
						 * this up twice if there is
						 * overlap at both ends.
						 */
						BUMP_MIB(ip_mib.ipReasmPartDups);
						break;
					}
					/* Did we cover another hole? */
					if ( (mp1->b_cont
					     &&  IP_REASS_END(mp1)
					     != IP_REASS_START(mp1->b_cont)
					     &&  end >= IP_REASS_START(mp1->b_cont))
					||  !more )
						ipf->ipf_hole_cnt--;
					/* Clip out mp1. */
					if (! (mp->b_cont = mp1->b_cont))
						/*
						 * After clipping out mp1,
						 * this guy is now hanging
						 * off the end.
						 */
						ipf->ipf_tail_mp = mp;
					IP_REASS_SET_START(mp1, 0);
					IP_REASS_SET_END(mp1, 0);
					/* Subtract byte count */
					ipf->ipf_count -= mp1->b_datap->db_lim -
							  mp1->b_datap->db_base;
					freeb(mp1);
					BUMP_MIB(ip_mib.ipReasmPartDups);
					mp1 = mp->b_cont;
					if ( !mp1 )
						break;
					u1 = IP_REASS_START(mp1);
				}
			}
			break;
		}
	} while ( start = end, mp = next_mp );
	/* Still got holes? */
	if ( ipf->ipf_hole_cnt )
		return (false);
	/* Clean up overloaded fields to avoid upstream disasters. */
	for ( mp1 = ipf->ipf_mp->b_cont; mp1; mp1 = mp1->b_cont ) {
		IP_REASS_SET_START(mp1, 0);
		IP_REASS_SET_END(mp1, 0);
	}
	return (true);
}

/* Read side put procedure.  Packets coming from the wire arrive here. */
void
ip_rput(q, mp)
	queue_t	* q;
reg	mblk_t	* mp;
{
	u32	dst;
reg	ire_t	* ire;
	mblk_t	* mp1;
reg	ipha_t	* ipha;
	ill_t	* ill;
	u_int	pkt_len;
	int	len;
	u_int	u1;
	int	ll_multicast;

	TRACE_1(TR_FAC_IP, TR_IP_RPUT_START,
		"ip_rput_start: q %X", q);

#define	rptr	((u_char *)ipha)

	MI_HRT_SET(((ill_t *)q->q_ptr)->ill_rtmp);

	/*
	 * ip_rput fast path
	 */

	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	/* mblk type is not M_DATA */
	if (mp->b_datap->db_type != M_DATA) goto notdata;

	ll_multicast = 0;
	BUMP_MIB(ip_mib.ipInReceives);
	len = mp->b_wptr - rptr;

	/* IP header ptr not aligned? */
	if (!OK_32PTR(rptr)) goto notaligned;

	/* IP header not complete in first mblock */
	if (len < IP_SIMPLE_HDR_LENGTH) goto notaligned;

	/* multiple mblk or too short */
	if (len - ntohs(ipha->ipha_length)) goto multimblk;

	/* IP version bad or there are IP options */
	if (ipha->ipha_version_and_hdr_length - (u_char) IP_SIMPLE_HDR_VERSION)
		goto ipoptions;

	dst = ipha->ipha_dst;

	/* packet is multicast */
	if (CLASSD(dst)) goto multicast;

	/* Try to get an IRE for dst.  Loop until an exact match. */
	ire = (ire_t *)&ire_hash_tbl[IRE_ADDR_HASH(dst)];
	do {
		ire = ire->ire_next;
		if (!ire) goto noirefound;
	} while (ire->ire_addr != dst);

	/* broadcast? */
	if (ire->ire_type == IRE_BROADCAST) goto broadcast;

	/* fowarding? */
	if (ire->ire_stq != 0) goto forward;

	/* packet not for us */
	if (ire->ire_rfq != q) goto notforus;

	TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
		"ip_rput_end: q %X (%S)", q, "end");

	ip_rput_local(q, mp, ipha, ire);
	return;

notdata:
	switch (mp->b_datap->db_type) {
	case M_DATA:
		/*
		 * A fastpath device may send us M_DATA.  Fastpath
		 * messages start with the network header and are
		 * never used for packets that were broadcast or multicast
		 * by the link layer.
		 * M_DATA are also used to pass back decapsulated packets
		 * from ip_mroute_decap. For those packets we will force
		 * ll_multicast to one below.
		 */
		ll_multicast = 0;
		break;
	case M_PROTO:
	case M_PCPROTO:
		if (((dl_unitdata_ind_t *)ALIGN32(rptr))->dl_primitive !=
			DL_UNITDATA_IND) {
			/* Go handle anything other than data elsewhere. */
			ip_rput_dlpi(q, mp);
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
				"ip_rput_end: q %X (%S)", q, "proto");
			return;
		}
#define	dlur	((dl_unitdata_ind_t *)ALIGN32(rptr))
		ll_multicast = dlur->dl_group_address;
#undef dlur
		/* Ditch the DLPI header. */
		mp1 = mp;
		mp = mp->b_cont;
		freeb(mp1);
		ipha = (ipha_t *)ALIGN32(mp->b_rptr);
		break;
	case M_BREAK:
		/*
		 * A packet arrives as M_BREAK following a cycle through
		 * ip_rput, ip_newroute, ... and finally ire_add_then_put.
		 * This is an IP datagram sans lower level header.
		 * M_BREAK are also used to pass back in multicast packets
		 * that are encapsulated with a source route.
		 */
		/* Ditch the M_BREAK mblk */
		mp1 = mp->b_cont;
		freeb(mp);
		mp = mp1;
		ipha = (ipha_t *)ALIGN32(mp->b_rptr);
		/* Get the number of words of IP options in the IP header. */
		u1 = ipha->ipha_version_and_hdr_length - IP_SIMPLE_HDR_VERSION;
		dst = (u32)mp->b_next;
		mp->b_next = nilp(mblk_t);
		ll_multicast = 0;
		goto hdrs_ok;
	case M_IOCACK:
		if (((struct iocblk *)ALIGN32(mp->b_rptr))->ioc_cmd !=
		    DL_IOC_HDR_INFO) {
			MI_HRT_CLEAR(((ill_t *)q->q_ptr)->ill_rtmp);
			putnext(q, mp);
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
				"ip_rput_end: q %X (%S)", q, "iocack");
			return;
		}
		/* FALLTHRU */
	case M_ERROR:
	case M_HANGUP:
		MI_HRT_CLEAR(((ill_t *)q->q_ptr)->ill_rtmp);
		become_writer(q, mp, ip_rput_other);
		TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
			"ip_rput_end: q %X (%S)", q, "ip_rput_other");
		return;
	case M_IOCNAK:
		if (((struct iocblk *)ALIGN32(mp->b_rptr))->ioc_cmd == DL_IOC_HDR_INFO) {
			MI_HRT_CLEAR(((ill_t *)q->q_ptr)->ill_rtmp);
			freemsg(mp);
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
				"ip_rput_end: q %X (%S)", q, "iocnak - hdr_info");
			return;
		}
		/* FALLTHRU */
	default:
		MI_HRT_CLEAR(((ill_t *)q->q_ptr)->ill_rtmp);
		putnext(q, mp);
		TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
			"ip_rput_end: q %X (%S)", q, "default");
		return;
	}
	/*
	 * Make sure that we at least have a simple IP header accessible and
	 * that we have at least long-word alignment.  If not, try a pullup.
	 */
	BUMP_MIB(ip_mib.ipInReceives);
	len = mp->b_wptr - rptr;
	if (!OK_32PTR(rptr)
	||  len < IP_SIMPLE_HDR_LENGTH) {
notaligned:
		/* Guard against bogus device drivers */
		if (len < 0) {
			/* clear b_prev - used by ip_mroute_decap */
			mp->b_prev = NULL;
			goto in_hdr_errors;
		}
		if (ip_rput_pullups++ == 0) {
			ill = (ill_t *)q->q_ptr;
			mi_strlog(q, 1, SL_ERROR|SL_TRACE,
				"ip_rput: %s forced us to pullup pkt, hdr len %d, hdr addr 0x%x",
				ill->ill_name, len, (u_long)ipha);
		}
		if (!pullupmsg(mp, IP_SIMPLE_HDR_LENGTH)) {
			/* clear b_prev - used by ip_mroute_decap */
			mp->b_prev = NULL;
			BUMP_MIB(ip_mib.ipInDiscards);
			goto drop_pkt;
		}
		ipha = (ipha_t *)ALIGN32(mp->b_rptr);
		len = mp->b_wptr - rptr;
	}
multimblk:
ipoptions:
	pkt_len = ntohs(ipha->ipha_length);
	len -= pkt_len;
	if (len) {
		/*
		 * Make sure we have data length consistent with the
		 * IP header.
		 */
		if (!mp->b_cont) {
			if (len < 0)
				goto in_hdr_errors;
			mp->b_wptr = rptr + pkt_len;
		} else if (len += msgdsize(mp->b_cont)) {
			if (len < 0
			||  pkt_len < IP_SIMPLE_HDR_LENGTH)
				goto in_hdr_errors;
			mi_adjmsg(mp, -len);
		}
	}
	/* Get the number of words of IP options in the IP header. */
	u1 = ipha->ipha_version_and_hdr_length - (u_char)IP_SIMPLE_HDR_VERSION;
	if (u1) {
		/* IP Options present!  Validate and process. */
		if (u1 > (15 - IP_SIMPLE_HDR_LENGTH_IN_WORDS)) {
			/* clear b_prev - used by ip_mroute_decap */
			mp->b_prev = NULL;
			goto in_hdr_errors;
		}
		/*
		 * Recompute complete header length and make sure we
		 * have access to all of it.
		 */
		len = (u1 + IP_SIMPLE_HDR_LENGTH_IN_WORDS) << 2;
		if (len > (mp->b_wptr - rptr)) {
			if (len > pkt_len) {
				/* clear b_prev - used by ip_mroute_decap */
				mp->b_prev = NULL;
				goto in_hdr_errors;
			}
			if (!pullupmsg(mp, len)) {
				BUMP_MIB(ip_mib.ipInDiscards);
				/* clear b_prev - used by ip_mroute_decap */
				mp->b_prev = NULL;
				goto drop_pkt;
			}
			ipha = (ipha_t *)ALIGN32(mp->b_rptr);
		}
		/*
		 * Go off to ip_rput_options which returns the next hop
		 * destination address, which may have been affected
		 * by source routing.
		 */
		dst = ip_rput_options(q, mp, ipha);
		if (dst == (u32)-1) {
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
				"ip_rput_end: q %X (%S)", q, "baddst");
			return;
		}
	} else {
		/* No options.  We know we have alignment so grap the dst. */
		dst = ipha->ipha_dst;
	}
hdrs_ok:;
	if (CLASSD(dst)) {
multicast:
		ill = (ill_t *)q->q_ptr;
		if (ip_g_mrouter) {
			if (ip_mforward(ill, ipha, mp) != 0) {
				/* ip_mforward updates mib variables 
				 * if needed */
				goto drop_pkt;
			}
			/* clear b_prev - used by ip_mroute_decap */
			mp->b_prev = NULL;
			/* If we are running a multicast router we need to
			 * see all igmp packet.
			 */
			if (ipha->ipha_protocol == IPPROTO_IGMP)
				goto ours;
		}
		/* This could use ilm_lookup_exact but this doesn't really 
		 * buy anything and it is more expensive since we would
		 * have to determine the ipif.
		 */
		if (!ilm_lookup(ill, dst)) {
			/* This might just be caused by the fact that
			 * multiple IP Multicast addresses map to the same
			 * link layer multicast - no need to increment counter!
			 */
			MI_HRT_CLEAR(((ill_t *)q->q_ptr)->ill_rtmp);
			freemsg(mp);
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
				"ip_rput_end: q %X (%S)", q, "badilm");
			return;
		}
	ours:
		ip2dbg(("ip_rput: multicast for us: 0x%x\n", (int)ntohl(dst)));
		/*
		 * This assume the we deliver to all streams for multicast
		 * and broadcast packets.
		 * We have to force ll_multicast to 1 to handle the
		 * M_DATA messages passed in from ip_mroute_decap.
		 */
		dst = (u_long)~0;
		ll_multicast = 1;
	}
	ire = (ire_t *)&ire_hash_tbl[IRE_ADDR_HASH(dst)];
	/* Try to get an IRE for dst.  We loop until we get an exact match. */
	do {
		ire = ire->ire_next;
		if (!ire) {
noirefound:
			/*
			 * No IRE for this destination, so it can't be for us.
			 * Unless we are forwarding, drop the packet.
			 * We have to let source routed packets through
			 * since we don't yet know if they are 'ping -l'
			 * packets i.e. if they will go out over the
			 * same interface as they came in on.
			 */
			if (ll_multicast) 
				goto drop_pkt;
			if (!WE_ARE_FORWARDING && 
			    !ip_source_routed(ipha)) {
				BUMP_MIB(ip_mib.ipForwProhibits);
				goto drop_pkt;
			}

			/* Mark this packet as having originated externally */
			mp->b_prev = (mblk_t *)q;

			/* Remember the dst, we may have gotten it from
			 * ip_rput_options.
			 */
			mp->b_next = (mblk_t *)dst;
			
			/*
			 * Now hand the packet to ip_newroute.
			 * It may come back.
			 */
			MI_HRT_CLEAR(((ill_t *)q->q_ptr)->ill_rtmp);
			ip_newroute(q, mp, dst);
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
				"ip_rput_end: q %X (%S)", q, "no ire");
			return;
		}
	} while (ire->ire_addr != dst);
	
	/*
	 * We now have a final IRE for the destination address.  If ire_stq
	 * is non-nil, (and it is not a broadcast IRE), then this packet
	 * needs to be forwarded, if allowable.
	 */
	/* Directed broadcast forwarding: if the packet came in over a 
	 * different interface then it is routed out over we can forward it.
	 */
	if (ire->ire_type == IRE_BROADCAST) {
broadcast:
		if (ip_g_forward_directed_bcast && !ll_multicast) {
			/* Verify that there are not more then one
			 * IRE_BROADCAST with this broadcast address which 
			 * has ire_stq set.
			 * TODO: simplify, loop over all IRE's
			 */
			ire_t	* ire1;
			int	num_stq = 0;
			mblk_t	* mp1;

			/* Find the first one with ire_stq set */
			for (ire1 = ire; ire1 && !ire1->ire_stq && 
			     ire1->ire_addr == ire->ire_addr; 
			     ire1 = ire1->ire_next)
				;
			if (ire1)
				ire = ire1;
		
			/* Check if there are additional ones with stq set */
			for (ire1 = ire; ire1; ire1 = ire1->ire_next) {
				if (ire->ire_addr != ire1->ire_addr)
					break;
				if (ire1->ire_stq) {
					num_stq++;
					break;
				}
			}
			if (num_stq == 1 && ire->ire_stq) {
				ip1dbg(("ip_rput: directed broadcast to 0x%x\n",
					(int)ntohl(ire->ire_addr)));
				mp1 = copymsg(mp);
				if (mp1) 
					ip_rput_local(q, mp1, 
						      (ipha_t *)ALIGN32(mp1->b_rptr), 
						      ire);
				/*
				 * Adjust ttl to 2 (1+1 - the forward engine
				 * will decrement it by one.
				 */
				if (ip_csum_hdr(ipha)) {
					BUMP_MIB(ip_mib.ipInCksumErrs);
					goto drop_pkt;
				}
				ipha->ipha_ttl = ip_broadcast_ttl + 1;
				ipha->ipha_hdr_checksum = 0;
				ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);
				goto forward;
			}
			ip1dbg(("ip_rput: NO directed broadcast to 0x%x\n",
				(int)ntohl(ire->ire_addr)));
		}
	} else if (ire->ire_stq) {
		queue_t	* dev_q;

		if (ll_multicast) 
			goto drop_pkt;
	forward:
		/* 
		 * Check if we want to forward this one at this time.
		 * We allow source routed packets on a host provided that 
		 * the go out the same interface as they came in on.
		 */
		if (!IP_OK_TO_FORWARD_THRU(ipha, ire) &&
		    !(ip_source_routed(ipha) && ire->ire_rfq == q)) {
			BUMP_MIB(ip_mib.ipForwProhibits);
			if (ip_source_routed(ipha)) {
				q = WR(q);
				icmp_unreachable(q, mp, 
						 ICMP_SOURCE_ROUTE_FAILED);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
					"ip_rput_end: q %X (%S)", q, "route failed");
				return;
			}
			goto drop_pkt;
		}
		dev_q = ire->ire_stq->q_next;
		if ((dev_q->q_next  ||  dev_q->q_first)  &&  !canput(dev_q)) {
			BUMP_MIB(ip_mib.ipInDiscards);
			q = WR(q);
			icmp_source_quench(q, mp);
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
				"ip_rput_end: q %X (%S)", q, "srcquench");
			return;
		}
		MI_HRT_CLEAR(((ill_t *)q->q_ptr)->ill_rtmp);
		if (ire->ire_rfq == q && ip_g_send_redirects) {
			/*
			 * It wants to go out the same way it came in.
			 * Check the source address to see if it originated
			 * on the same logical subnet it is going back out on
			 * (by comparing the ire_src_addr).  If
			 * so, we should be able to send it a redirect.
			 * Avoid sending a redirect if the destination
			 * is directly connected (gw_addr == 0),
			 * if the ire was derived from a default route,
			 * or if the packet was source routed out this 
			 * interface.
			 */
			u32	src;
			mblk_t	* mp1;
			ire_t	* ire1;

			src = ipha->ipha_src;
			if (ire->ire_gateway_addr &&
			    (ire1 = ire_lookup_interface(src, IRE_INTERFACE)) &&
			    ire1->ire_src_addr == ire->ire_src_addr &&
			    (ire1 = ire_lookup_noroute(dst)) != 0 &&
			    ire1->ire_type != IRE_GATEWAY &&
			    !ip_source_routed(ipha)) {
				/* 
				 * The source is directly connected.
				 * Just copy the ip header (which is in
				 * the first mblk)
				 */
				 mp1 = copyb(mp);
				 if (mp1)
					 icmp_send_redirect(WR(q), mp1, 
						    ire->ire_gateway_addr);
			}
		}

		TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
			"ip_rput_end: q %X (%S)", q, "forward");
		ip_rput_forward(ire, ipha, mp);
		return;
	}
	/*
	 * It's for us.  We need to check to make sure the packet came in
	 * on the queue associated with the destination IRE.
	 * Note that for multicast packets and broadcast packets sent to
	 * a broadcast address which is shared betwen multiple interfaces
	 * we should not do this since we just got a random broadcast ire.
	 */
	if (ire->ire_rfq != q) {
notforus:
		if (ire->ire_rfq && ire->ire_type != IRE_BROADCAST) {
			mblk_t	* mp1;
			/*
			 * This packet came in on an interface other than the
			 * one associated with the destination address.
			 * "Gateway" it to the appropriate interface here.
			 */
			if (ip_strict_dst_multihoming && !WE_ARE_FORWARDING) {
				/* Drop packet */
				BUMP_MIB(ip_mib.ipForwProhibits);
				freemsg(mp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
					"ip_rput_end: q %X (%S)", q,
					"strict_dst_multihoming");
				return;
			}

			MI_HRT_INCREMENT(((ill_t *)q->q_ptr)->ill_rtime,
				((ill_t *)q->q_ptr)->ill_rtmp, 1);
			MI_HRT_CLEAR(((ill_t *)q->q_ptr)->ill_rtmp);
			q = ire->ire_rfq;
			/* Set up to skip past ip_rput_options ... */
			/* Prevent problems with the unclean b_next by
			 * prepending an empty M_BREAK mblk.
			 */
			mp1 = allocb(0, BPRI_HI);
			if (!mp1) {
				/* TODO source quench */
				BUMP_MIB(ip_mib.ipInDiscards);
				freemsg(mp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
					"ip_rput_end: q %X (%S)", q, "allocbfail1");
				return;
			}
			mp1->b_datap->db_type = M_BREAK;
			mp1->b_cont = mp;
			mp->b_next = (mblk_t *)ipha->ipha_dst;
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
				"ip_rput_end: q %X (%S)", q, "gateway");
			put(q, mp1);
			return;
		}
		/* Must be broadcast.  We'll take it. */
	}
	
	TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
		"ip_rput_end: q %X (%S)", q, "end");
	ip_rput_local(q, mp, ipha, ire);
	return;
	
in_hdr_errors:;
	BUMP_MIB(ip_mib.ipInHdrErrors);
	/* FALLTHRU */
drop_pkt:;
	MI_HRT_CLEAR(((ill_t *)q->q_ptr)->ill_rtmp);
	ip2dbg(("ip_rput: drop pkt\n"));
	freemsg(mp);
	TRACE_2(TR_FAC_IP, TR_IP_RPUT_END,
		"ip_rput_end: q %X (%S)", q, "drop");
#undef	rptr
}

staticf void
ip_rput_unbind_done (ill)
	ill_t	* ill;
{
	mblk_t	* mp;

	ill->ill_unbind_pending = 0;
	if ((mp = ill->ill_detach_mp) != NULL) {
		ip1dbg(("ip_rput_unbind_done: detach\n"));
		ill->ill_detach_mp = mp->b_next;
		mp->b_next = nilp(mblk_t);
		putnext(ill->ill_wq, mp);
	}
	if ((mp = ill->ill_attach_mp) != NULL) {
		ip1dbg(("ip_rput_unbind_done: attach\n"));
		ill->ill_attach_mp = nilp(mblk_t);
		putnext(ill->ill_wq, mp);
	}
	if ((mp = ill->ill_bind_mp) != NULL) {
		ip1dbg(("ip_rput_unbind_done: bind\n"));
		ill->ill_bind_mp = nilp(mblk_t);
		putnext(ill->ill_wq, mp);
	}
}

/*
 * ip_rput_dlpi is called by ip_rput to handle all DLPI messages other
 * than DL_UNITDATA_IND messages.
 */
staticf void
ip_rput_dlpi (q, mp)
	queue_t	* q;
	mblk_t	*mp;
{
	dl_ok_ack_t	* dloa = (dl_ok_ack_t *)ALIGN32(mp->b_rptr);
	dl_error_ack_t	* dlea = (dl_error_ack_t *)dloa;
	char		* err_str = nilp(char);
	ill_t		* ill;

	ill = (ill_t *)q->q_ptr;
	switch (dloa->dl_primitive) {
	case DL_ERROR_ACK:
		ip0dbg(("ip_rput: DL_ERROR_ACK for %d, errno %d, unix %d\n",
			(int)dlea->dl_error_primitive,
			(int)dlea->dl_errno,
			(int)dlea->dl_unix_errno));
		switch (dlea->dl_error_primitive) {
		case DL_UNBIND_REQ:
		case DL_BIND_REQ:
		case DL_ENABMULTI_REQ:
			become_writer(q, mp, ip_rput_dlpi_writer);
			return;

		case DL_DETACH_REQ:
			err_str = "DL_DETACH";
			break;
		case DL_ATTACH_REQ:
			ip0dbg(("ip_rput: attach failed on %s\n",
				ill->ill_name));
			err_str = "DL_ATTACH";
			break;
		case DL_DISABMULTI_REQ:
			ip1dbg(("DL_ERROR_ACK to disabmulti\n"));
			freemsg(mp);	/* Don't want to pass this up */
			return;
		case DL_PROMISCON_REQ:
			ip1dbg(("DL_ERROR_ACK to promiscon\n"));
			err_str = "DL_PROMISCON_REQ";
			break;
		case DL_PROMISCOFF_REQ:
			ip1dbg(("DL_ERROR_ACK to promiscoff\n"));
			err_str = "DL_PROMISCOFF_REQ";
			break;
		default:
			err_str = "DL_???";
			break;
		}
		/*
		 * There is no IOCTL hanging out on these.  Log the
		 * problem and hope someone notices.
		 */
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			  "ip_rput_dlpi: %s failed dl_errno %d dl_unix_errno %d",
			  err_str, dlea->dl_errno, dlea->dl_unix_errno);
		freemsg(mp);
		return;
	case DL_INFO_ACK:
	case DL_BIND_ACK:
		become_writer(q, mp, ip_rput_dlpi_writer);
		return;
	case DL_OK_ACK:
#ifdef IP_DEBUG
		ip1dbg(("ip_rput: DL_OK_ACK for %d\n",
			(int)dloa->dl_correct_primitive));
		switch (dloa->dl_correct_primitive) {
		case DL_UNBIND_REQ:
			ip1dbg(("ip_rput: UNBIND OK\n"));
			break;
		case DL_ENABMULTI_REQ:
		case DL_DISABMULTI_REQ:
			ip1dbg(("DL_OK_ACK to add/delmulti\n"));
			break;
		case DL_PROMISCON_REQ:
		case DL_PROMISCOFF_REQ:
			ip1dbg(("DL_OK_ACK to promiscon/off\n"));
			break;
		}
#endif	/*IP_DEBUG*/
		if (dloa->dl_correct_primitive == DL_UNBIND_REQ) {
			become_writer(q, mp, ip_rput_dlpi_writer);
			return;
		}
		break;
	default:
		break;
	}
	freemsg(mp);
}

/*
 * Handling of DLPI messages that require exclusive access to IP
 */
staticf void
ip_rput_dlpi_writer (q, mp)
	queue_t	* q;
	mblk_t	*mp;
{
	dl_ok_ack_t	* dloa = (dl_ok_ack_t *)ALIGN32(mp->b_rptr);
	dl_error_ack_t	* dlea = (dl_error_ack_t *)dloa;
	int		err = 0;
	char		* err_str = nilp(char);
	ill_t		* ill;
	ipif_t		* ipif;
	mblk_t		* mp1 = nilp(mblk_t);

	ill = (ill_t *)q->q_ptr;
	ipif = ill->ill_ipif_pending;
	switch (dloa->dl_primitive) {
	case DL_ERROR_ACK:
		switch (dlea->dl_error_primitive) {
		case DL_UNBIND_REQ:
			ip_rput_unbind_done(ill);
			err_str = "DL_UNBIND";
			break;
		case DL_BIND_REQ:
			/*
			 * Something went wrong with the bind.  We presumably
			 * have an IOCTL hanging out waiting for completion.
			 * Find it, take down the interface that was coming
			 * up, and complete the IOCTL with the error noted.
			 */
			mp1 = ill->ill_bind_pending;
			if ( mp1 ) {
				ill->ill_ipif_pending = nilp(ipif_t);
				ipif_down(ipif);
				ill->ill_bind_pending = nilp(mblk_t);
				q = ill->ill_bind_pending_q;
			}
			break;
		case DL_ENABMULTI_REQ:
			ip1dbg(("DL_ERROR_ACK to enabmulti\n"));
			freemsg(mp);	/* Don't want to pass this up */
			if (dlea->dl_errno != DL_SYSERR) {
				ipif_t *ipif;
				
				printf("ip: joining multicasts failed on %s - will use link layer\n",
					ill->ill_name);
				printf("broadcasts for multicast\n");
				for (ipif = ill->ill_ipif; ipif; ipif = 
				     ipif->ipif_next) {
					ipif->ipif_flags |= IFF_MULTI_BCAST;
					(void)ipif_arp_up(ipif, 
						    ipif->ipif_local_addr);
				}
			}
			return;
		}
		if (err_str) {
			/*
			 * There is no IOCTL hanging out on these.  Log the
			 * problem and hope someone notices.
			 */
			mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			"ip_rput_dlpi: %s failed dl_errno %d dl_unix_errno %d",
				err_str, dlea->dl_errno, dlea->dl_unix_errno);
			freemsg(mp);
			return;
		}
		/* Note the error for IOCTL completion. */
		err = dlea->dl_unix_errno ? dlea->dl_unix_errno : ENXIO;
		break;
	case DL_INFO_ACK:
		/* Call a routine to handle this one. */
		ip_ll_subnet_defaults(ill, mp);
		return;
	case DL_BIND_ACK:
		/* We should have an IOCTL waiting on this. */
		mp1 = ill->ill_bind_pending;
		if ( mp1 ) {
			ipif_t	* ipif;

			ill->ill_bind_pending = nilp(mblk_t);
			/* Retrieve the originating queue pointer. */
			q = ill->ill_bind_pending_q;
			/* Complete initialization of the interface. */
			ill->ill_ipif_up_count++;
			ipif = ill->ill_ipif_pending;
			ill->ill_ipif_pending = nilp(ipif_t);
			ipif->ipif_flags |= IFF_UP;
			/* Increment the global interface up count. */
			ipif_g_count++;
			if (ill->ill_bcast_mp)
				ill_fastpath_probe(ill, ill->ill_bcast_mp);
			/* 
			 * Need to add all multicast addresses. This 
			 * had to be deferred until we had attached.
			 */
			ill_add_multicast(ill);

			/* This had to be deferred until we had bound. */
			for (ipif = ill->ill_ipif; ipif
			; ipif = ipif->ipif_next) {
				/* Broadcast an address mask reply. */
				ipif_mask_reply(ipif);
			}
		}
		break;
	case DL_OK_ACK:
		if (dloa->dl_correct_primitive == DL_UNBIND_REQ)
			ip_rput_unbind_done(ill);
		break;
	default:
		break;
	}
	freemsg(mp);
	if (mp1) {
		/* Complete the waiting IOCTL. */
		mi_copy_done(q, mp1, err);
	}
}

/*
 * ip_rput_other is called by ip_rput to handle messages modifying the
 * global state in IP.
 * Always called as a writer.
 */
staticf void
ip_rput_other (q, mp)
	queue_t	* q;
	mblk_t	*mp;
{
	ill_t	* ill;

	switch (mp->b_datap->db_type) {
	case M_ERROR:
	case M_HANGUP:
		/*
		 * The device has a problem.  We force the ILL down.  It can
		 * be brought up again manually using SIOCIFFLAGS (via ifconfig
		 * or equivalent).
		 */
		ill = (ill_t *)q->q_ptr;
		if (mp->b_rptr < mp->b_wptr)
			ill->ill_error = (int)(*mp->b_rptr & 0xFF);
		if (ill->ill_error == 0)
			ill->ill_error = ENXIO;
		ill_down(ill);
		freemsg(mp);
		return;
	case M_IOCACK:
		if (((struct iocblk *)ALIGN32(mp->b_rptr))->ioc_cmd == DL_IOC_HDR_INFO) {
			ill = (ill_t *)q->q_ptr;
			ill_fastpath_ack(ill, mp);
			return;
		}
		/* FALLTHRU */
	}
}

/* (May be called as writer.) */
void
ip_rput_forward (ire, ipha, mp)
	ire_t	* ire;
	ipha_t	* ipha;
	mblk_t	* mp;
{
	mblk_t	* mp1;
	u32	pkt_len;
	queue_t	* q;
	u32	sum;
	u32	u1;
#define	rptr	((u_char *)ipha)
	u32	max_frag;

	pkt_len = ntohs(ipha->ipha_length);

	/* Adjust the checksum to reflect the ttl decrement. */
	sum = (int)ipha->ipha_hdr_checksum + IP_HDR_CSUM_TTL_ADJUST;
	ipha->ipha_hdr_checksum = (u16)(sum + (sum >> 16));

	if (ipha->ipha_ttl-- <= 1) {
		if (ip_csum_hdr(ipha)) {
			BUMP_MIB(ip_mib.ipInCksumErrs);
			goto drop_pkt;
		}
		/*
		 * Note: ire_stq this will be nil for multicast
		 * datagrams using the long path through arp (the IRE
		 * is not an IRE_ROUTE). This should not cause
		 * problems since we don't generate ICMP errors for
		 * multicast packets.
		 */
		q = ire->ire_stq;
		if (q)
			icmp_time_exceeded(q, mp, ICMP_TTL_EXCEEDED);
		else
			freemsg(mp);
		return;
	}

	/* Check if there are options to update */
	if (!IS_SIMPLE_IPH(ipha)) {
		if (ip_csum_hdr(ipha)) {
			BUMP_MIB(ip_mib.ipInCksumErrs);
			goto drop_pkt;
		}
		if (ip_rput_forward_options(mp, ipha, ire))
			return;

		ipha->ipha_hdr_checksum = 0;
		ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);
	}
	max_frag = ire->ire_max_frag;
	if (pkt_len > max_frag) {
		/*
		 * It needs fragging on its way out.  We haven't
		 * verified the header checksum yet.  Since we
		 * are going to put a surely good checksum in the
		 * outgoing header, we have to make sure that it
		 * was good coming in.
		 */
		if (ip_csum_hdr(ipha)) {
			BUMP_MIB(ip_mib.ipInCksumErrs);
			goto drop_pkt;
		}
		ip_wput_frag(ire, mp, &ire->ire_ib_pkt_count, max_frag, 0);

		return;
	}
	u1 = ire->ire_ll_hdr_length;
	mp1 = ire->ire_ll_hdr_mp;
	/*
	 * If the driver accepts M_DATA prepends
	 * and we have enough room to lay it in ...
	 */
	if (u1  &&  (rptr - mp->b_datap->db_base) >= u1) {
		/* XXX: ipha is only used as an alias for rptr */
		ipha = (ipha_t *)ALIGN32(rptr - u1);
		mp->b_rptr = rptr;
		/* TODO: inline this small copy */
		bcopy((char *)mp1->b_rptr, (char *)rptr, u1);
	} else {
		mp1 = copyb(mp1);
		if (!mp1) {
			BUMP_MIB(ip_mib.ipInDiscards);
			goto drop_pkt;
		}
		mp1->b_cont = mp;
		mp = mp1;
	}
	q = ire->ire_stq;
	/* TODO: make this atomic */
	ire->ire_ib_pkt_count++;
	BUMP_MIB(ip_mib.ipForwDatagrams);
	putnext(q, mp);
	return;

drop_pkt:;
	ip1dbg(("ip_rput_forward: drop pkt\n"));
	freemsg(mp);
#undef	rptr
}

void
ip_rput_forward_multicast (dst, mp, ipif)
	u32 	dst;
	mblk_t	* mp;
	ipif_t	* ipif;
{
	queue_t * stq;
	ire_t	* ire;
	u32	local_addr;

	/*
	 * Find an IRE which matches the destination and the outgoing
	 * queue (i.e. the outgoing interface).
	 */
	if (ipif->ipif_flags & IFF_POINTOPOINT)
		dst = ipif->ipif_pp_dst_addr;
	stq = ipif->ipif_ill->ill_wq;
	local_addr = ipif->ipif_local_addr;
	
	ire = (ire_t *)&ire_hash_tbl[IRE_ADDR_HASH(dst)];
	do {
		ire = ire->ire_next;
		if (!ire) {
			/*
			 * Mark this packet to make it be delivered to
			 * ip_rput_forward after the new ire has been
			 * created.
			 */
			mp->b_prev = nilp(mblk_t);
			mp->b_next = mp;
			ip_newroute_ipif(ipif->ipif_ill->ill_wq, mp, ipif, 
					 dst); 
			return;
		}
	} while (ire->ire_addr != dst || ire->ire_stq != stq ||
		 ire->ire_src_addr != local_addr);
	
	ip_rput_forward(ire, (ipha_t *)ALIGN32(mp->b_rptr), mp);
}

/* Update any source route, record route or timestamp options */
staticf int
ip_rput_forward_options (mp, ipha, ire)
	mblk_t	* mp;
	ipha_t	* ipha;
	ire_t	* ire;
{
	u32	totallen;
	u_char	* opt;
	u32	optval;
	u32	optlen;
	u32	dst;
	u32	u1;

	ip2dbg(("ip_rput_forward_options\n"));
	dst = ipha->ipha_dst;
	totallen = ipha->ipha_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	opt = (u_char *)&ipha[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			return 0;
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}
		ip2dbg(("ip_rput_forward_options: opt %d, len %d\n", 
			  optval, optlen));

		if (optlen == 0 || optlen > totallen)
			break;

		switch (optval) {
			u32 off;
		case IPOPT_SSRR:
		case IPOPT_LSRR: 
			/* Check if adminstratively disabled */
			if (!ip_forward_src_routed) {
				BUMP_MIB(ip_mib.ipForwProhibits);
				if (ire->ire_stq)
					icmp_unreachable(ire->ire_stq, mp, 
						 ICMP_SOURCE_ROUTE_FAILED);
				else {
					ip0dbg(("ip_rput_fw_options: unable to send unreach\n"));
					freemsg(mp);
				}
				return -1;
			}
		    
			if (!ire_lookup_myaddr(dst)) {
				/* Must be partial since ip_rput_options 
				 * checked for strict.
				 */
				break;
			}
			off = opt[IPOPT_POS_OFF];
			off--;
			if (optlen < IP_ADDR_LEN ||
			    off > optlen - IP_ADDR_LEN) {
				/* End of source route */
				ip1dbg(("ip_rput_forward_options: end of SR\n"));
				break;
			}
			bcopy((char *)opt + off, (char *)&dst, IP_ADDR_LEN);
			bcopy((char *)&ire->ire_src_addr, (char *)opt + off,
			      IP_ADDR_LEN);
			ip1dbg(("ip_rput_forward_options: next hop 0x%x\n", 
				(int)ntohl(dst)));
			ipha->ipha_dst = dst;
			opt[IPOPT_POS_OFF] += IP_ADDR_LEN;
			break;
		case IPOPT_RR:
			off = opt[IPOPT_POS_OFF];
			off--;
			if (optlen < IP_ADDR_LEN ||
			    off > optlen - IP_ADDR_LEN) {
				/* No more room - ignore */
				ip1dbg(("ip_rput_forward_options: end of RR\n"));
				break;
			}
			bcopy((char *)&ire->ire_src_addr, (char *)opt + off,
			      IP_ADDR_LEN);
			opt[IPOPT_POS_OFF] += IP_ADDR_LEN;
			break;
		case IPOPT_IT:
			/* Insert timestamp if there is romm */
			switch (opt[IPOPT_POS_OV_FLG] & 0x0F) {
			case IPOPT_IT_TIME:
				off = IPOPT_IT_TIMELEN;
				break;
			case IPOPT_IT_SPEC:
#ifdef IPOPT_IT_SPEC_BSD
			case IPOPT_IT_SPEC_BSD:
#endif
				/* Verify that the address matched */
				off = opt[IPOPT_POS_OFF] - 1;
				bcopy((char *)opt + off, (char *)&dst, 
				      IP_ADDR_LEN);
				if (!ire_lookup_myaddr(dst))
					/* Not for us */
					break;
				/* FALLTHRU */
			case IPOPT_IT_TIME_ADDR:
				off = IP_ADDR_LEN + IPOPT_IT_TIMELEN;
				break;
			}
			if (opt[IPOPT_POS_OFF] - 1 + off > optlen) {
				/* Increase overflow counter */
				off = (opt[IPOPT_POS_OV_FLG] >> 4) + 1;
				opt[IPOPT_POS_OV_FLG] = 
					(opt[IPOPT_POS_OV_FLG] & 0x0F) | (off << 4);
				break;
			}
			off = opt[IPOPT_POS_OFF] - 1;
			switch (opt[IPOPT_POS_OV_FLG] & 0x0F) {
			case IPOPT_IT_SPEC:
#ifdef IPOPT_IT_SPEC_BSD
			case IPOPT_IT_SPEC_BSD:
#endif
			case IPOPT_IT_TIME_ADDR:
				bcopy((char *)&ire->ire_src_addr, 
				      (char *)opt + off,
				      IP_ADDR_LEN);
				opt[IPOPT_POS_OFF] += IP_ADDR_LEN;
				/* FALLTHRU */
			case IPOPT_IT_TIME:
				off = opt[IPOPT_POS_OFF] - 1;
				/* Compute # of milliseconds since midnight */
				u1 = ((time_in_secs % (24 * 60 * 60)) * 1000) +
					(LBOLT_TO_MS(lbolt) % 1000);
				bcopy((char *)&u1, (char *)opt + off,
				      IP_ADDR_LEN);
				opt[IPOPT_POS_OFF] += IPOPT_IT_TIMELEN;
				break;
			}
			break;
		}
		totallen -= optlen;
		opt += optlen;
	}
	return (0);
}

/* This routine is needed for loopback when forwarding multicasts. */
void
ip_rput_local(q, mp, ipha, ire)
	queue_t	* q;
	mblk_t	* mp;
	ipha_t	* ipha;
	ire_t	* ire;
{
	dblk_t	* dp = mp->b_datap;
	u32	dst;
	ill_t	* ill;
	ipc_t	* ipc;
	mblk_t	* mp1;
	u32	sum;
	u32	u1;
	u32	u2;
	int	sum_valid;
	u16	* up;
	int	offset;
	int	len;
	u32	ports;

	TRACE_1(TR_FAC_IP, TR_IP_RPUT_LOCL_START,
		"ip_rput_locl_start: q %X", q);

#define	rptr	((u_char *)ipha)
#define	UDPH_SIZE 8
#define	TCP_MIN_HEADER_LENGTH 20

#ifdef	MI_HRTIMING
	ill = (ill_t *)q->q_ptr;
#endif

	/*
	 * FAST PATH for udp packets
	 */

	/* u1 is # words of IP options */
	u1 = ipha->ipha_version_and_hdr_length - (u_char)((IP_VERSION << 4)
	    + IP_SIMPLE_HDR_LENGTH_IN_WORDS);

	/* Check the IP header checksum.  */
#define	uph	((u16 *)ALIGN16(ipha))
	sum = uph[0] + uph[1] + uph[2] + uph[3] + uph[4] + uph[5] + uph[6] +
		uph[7] + uph[8] + uph[9];
#undef  uph
	/* IP options present (udppullup uses u1) */
	if (u1) goto ipoptions;

	/* finish doing IP checksum */
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = ~(sum + (sum >> 16)) & 0xFFFF;

	/* verify checksum */
	if (sum && sum != 0xFFFF) goto cksumerror;

	/* count for SNMP of inbound packets for ire */
	ire->ire_ib_pkt_count++;

	/* packet part of fragmented IP packet? */
	u2 = ntohs(ipha->ipha_fragment_offset_and_flags);
	sum_valid = 0;
	u1 = u2 & (IPH_MF | IPH_OFFSET);
	if (u1) goto fragmented;

	/* u1 = IP header length (20 bytes) */
	u1 = IP_SIMPLE_HDR_LENGTH;

	dst = ipha->ipha_dst;
	/* protocol type not UDP (ports used as temporary) */
	if ((ports = ipha->ipha_protocol) != IPPROTO_UDP) goto notudp;

	/* packet does not contain complete IP & UDP headers */
	if ((mp->b_wptr - rptr) < (IP_SIMPLE_HDR_LENGTH + UDPH_SIZE))
		goto udppullup;

	/* up points to UDP header */
	up = (u16 *)ALIGN16(((u_char *)ipha) + IP_SIMPLE_HDR_LENGTH);

#define	iphs	((u16 *)ipha)
	/* if udp hdr cksum != 0, then need to checksum udp packet */
	if (up[3] && IP_CSUM(mp, (u_char *)up - (u_char *)ipha,
	    IP_UDP_CSUM_COMP + iphs[6] + iphs[7] + iphs[8] +
	    iphs[9] + up[2])) {
		goto badchecksum;
	}
	/* u1 = UDP destination port */
	u1 = up[1];	/* Destination port in net byte order */

	/* broadcast IP packet? */
	if (ire->ire_type == IRE_BROADCAST) goto udpbroadcast;

	/* look for matching stream based on UDP addresses */
	ipc = (ipc_t *)&ipc_udp_fanout[IP_UDP_HASH(u1)];
	do {
		ipc = ipc->ipc_hash_next;
		if (!ipc) goto noudpentry;
	} while (!IP_UDP_MATCH(ipc, u1, dst));

	q = ipc->ipc_rq;

	/* stream blocked? */
	if (!canputnext(q)) goto nocanput;

	/* statistics */
	BUMP_MIB(ip_mib.ipInDelivers);

#ifdef  MI_HRTIMING
	if (MI_HRT_IS_SET(ill->ill_rtmp)) {
		MI_HRT_INCREMENT(ill->ill_rtime,
			ill->ill_rtmp, 1);
		MI_HRT_CLEAR(ill->ill_rtmp);
	}
#endif /* MI_HRTIMING */

	TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
		"ip_rput_locl_end: q %X (%S)", q, "xx-putnext up");

	/* pass packet up to sockmod */
	putnext(q, mp);
	return;

	/*
	 * FAST PATH for tcp packets
	 */
notudp:
	/* if not TCP, then just use default code */
	if (ports != IPPROTO_TCP) goto nottcp;

	/* does packet contain IP+TCP headers? */
	len = mp->b_wptr - rptr;
	if (len < (IP_SIMPLE_HDR_LENGTH + TCP_MIN_HEADER_LENGTH))
		goto tcppullup;

	/* TCP options present? */
	offset = ((u_char *)ipha)[IP_SIMPLE_HDR_LENGTH + 12] >> 4;
	if (offset != 5) goto tcpoptions;

	/* multiple mblocks of tcp data? */
	if (mp->b_cont) goto multipkttcp;
	up = (u16 *)ALIGN16(rptr + IP_SIMPLE_HDR_LENGTH);

	ports = *(u32 *)ALIGN32(up);

	/* part of pseudo checksum */
	u1 = len - IP_SIMPLE_HDR_LENGTH;
#ifdef  _BIG_ENDIAN
	u1 += IPPROTO_TCP;
#else
	u1 = ((u1 >> 8) & 0xFF) + (((u1 & 0xFF) + IPPROTO_TCP) << 8);
#endif
	u1 += iphs[6] + iphs[7] + iphs[8] + iphs[9];
	/* if not a duped mblk, then postpone the checksum, else doit */
	if (dp->db_ref == 1) {
		u1 = (u1 >> 16) + (u1 & 0xffff);
		u1 += (u1 >> 16);
		*(u16 *)dp->db_struioun.data = u1;
		dp->db_struiobase = (u_char *)up;
		dp->db_struioptr = (u_char *)up;
		dp->db_struiolim = mp->b_wptr;
		dp->db_struioflag = STRUIO_SPEC|STRUIO_IP;
	} else if (IP_CSUM(mp, (u_char *)up - rptr, u1)) {
                goto tcpcksumerr;
        }

	/* Find a TCP client stream for this packet. */
	ipc = (ipc_t *)&ipc_tcp_fanout[IP_TCP_HASH(ipha->ipha_src, ports)];
	do {
		ipc = ipc->ipc_hash_next;
		if (!ipc) goto notcpport;
	} while (!TCP_MATCH(ipc, ipha, ports));

	/* Got a client */
	q = ipc->ipc_rq;
	/* ip module statistics */
	BUMP_MIB(ip_mib.ipInDelivers);
#ifdef MI_HRTIMING
	if (MI_HRT_IS_SET(ill->ill_rtmp)) {
		MI_HRT_INCREMENT(ill->ill_rtime, ill->ill_rtmp, 1);
		MI_HRT_CLEAR(ill->ill_rtmp);
	}
#endif /* MI_HRTIMING */
	TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
		"ip_rput_locl_end: q %X (%S)", q, "tcp");
	putnext(q, mp);
	return;

ipoptions:
	{
		/* Add in IP options. */
		u16	* uph = &((u16 *)ipha)[10];
		do {
			sum += uph[0];
			sum += uph[1];
			uph += 2;
		} while (--u1);
		if (ip_rput_local_options(q, mp, ipha, ire)) {
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
				"ip_rput_locl_end: q %X (%S)", q, "badopts");
			return;
		}
	}
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = ~(sum + (sum >> 16)) & 0xFFFF;
	if (sum && sum != 0xFFFF) {
cksumerror:
		BUMP_MIB(ip_mib.ipInCksumErrs);
		goto drop_pkt;
	}

	/* TODO: make this atomic */
	ire->ire_ib_pkt_count++;
	/* Check for fragmentation offset. */
	u2 = ntohs(ipha->ipha_fragment_offset_and_flags);
	sum_valid = 0;
	u1 = u2 & (IPH_MF | IPH_OFFSET);
	if (u1) {
	reg	ipf_t	* ipf;
		ipf_t	** ipfp;
		ipfb_t	* ipfb;
		u32	ident;
		u32	offset;
		u32	src;
		u_int	hdr_length;
		u32	end;
		u32	proto;
		mblk_t	* mp1;
		u_long	count;

fragmented:
		ident = ipha->ipha_ident;
		offset = (u1 << 3) & 0xFFFF;
		src = ipha->ipha_src;
		hdr_length = (ipha->ipha_version_and_hdr_length & 0xF) << 2;
		end = ntohs(ipha->ipha_length) - hdr_length;
		proto = ipha->ipha_protocol;

		/*
		 * Fragmentation reassembly.  Each ILL has a hash table for
		 * queueing packets undergoing reassembly for all IPIFs
		 * associated with the ILL.  The hash is based on the packet
		 * IP ident field.  The ILL frag hash table was allocated
		 * as a timer block at the time the ILL was created.  Whenever
		 * there is anything on the reassembly queue, the timer will
		 * be running.
		 */
		ill = (ill_t *)q->q_ptr;
		/*
		 * Compute a partial checksum before acquiring the lock
		 */
		sum = IP_CSUM_PARTIAL(mp, hdr_length, 0);

		if (offset) {
			/*
			 * If this isn't the first piece, strip the header, and
			 * add the offset to the end value.
			 */
			mp->b_rptr += hdr_length;
			end += offset;
		}
		/*
		 * If the reassembly list for this ILL is to big, prune it.
		 */
		if (ill->ill_frag_count > ip_reass_queue_bytes)
			ill_frag_prune(ill, ip_reass_queue_bytes);
		ipfb = &ill->ill_frag_hash_tbl[ILL_FRAG_HASH(src, ident)];
		mutex_enter(&ipfb->ipfb_lock);
		ipfp = &ipfb->ipfb_ipf;
		/* Try to find an existing fragment queue for this packet. */
		for (;;) {
			ipf = ipfp[0];
			if (ipf) {
				/*
				 * It has to match on ident and source
				 * address.
				 */
				if (ipf->ipf_ident == ident &&
				    ipf->ipf_src == src &&
				    ipf->ipf_protocol == proto) {
					/* Found it. */
					break;
				}
				ipfp = &ipf->ipf_hash_next;
				continue;
			}
			/* New guy.  Allocate a frag message. */
			mp1 = allocb(sizeof (*ipf), BPRI_MED);
			if (!mp1) {
				BUMP_MIB(ip_mib.ipInDiscards);
				freemsg(mp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_ERR,
				    "ip_rput_locl_err: q %X (%S)", q,
				    "ALLOCBFAIL");
reass_done:;
				mutex_exit(&ipfb->ipfb_lock);
#ifdef MI_HRTIMING
				if ( MI_HRT_IS_SET(ill->ill_rtmp) ) {
					MI_HRT_INCREMENT(ill->ill_rtime,
						ill->ill_rtmp, 1);
					MI_HRT_CLEAR(ill->ill_rtmp);
				}
#endif /* MI_HRTIMING */
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
					"ip_rput_locl_end: q %X (%S)", q, "reass_done");
				return;
			}
			BUMP_MIB(ip_mib.ipReasmReqds);
			mp1->b_cont = mp;
			
			/* Initialize the fragment header. */
			ipf = (ipf_t *)ALIGN32(mp1->b_rptr);
			ipf->ipf_checksum = 0;
			ipf->ipf_checksum_valid = 1;
			ipf->ipf_mp = mp1;
			ipf->ipf_ptphn = ipfp;
			ipfp[0] = ipf;
			ipf->ipf_hash_next = nilp(ipf_t);
			ipf->ipf_ident = ident;
			ipf->ipf_protocol = proto;
			ipf->ipf_src = src;
			/* Record reassembly start time. */
			ipf->ipf_timestamp = time_in_secs;
			/* Record ipf generation and account for frag header */
			ipf->ipf_gen = ill->ill_ipf_gen++;
			ipf->ipf_count = mp1->b_datap->db_lim -
					 mp1->b_datap->db_base;
			/*
			 * We handle reassembly two ways.  In the easy case,
			 * where all the fragments show up in order, we do
			 * minimal bookkeeping, and just clip new pieces on
			 * the end.  If we ever see a hole, then we go off
			 * to ip_reassemble which has to mark the pieces and
			 * keep track of the number of holes, etc.  Obviously,
			 * the point of having both mechanisms is so we can
			 * handle the easy case as efficiently as possible.
			 */
			if (offset == 0) {
				/* Easy case, in-order reassembly so far. */
				/* Compute a partial checksum and byte count */
				sum += ipf->ipf_checksum;
				sum = (sum & 0xFFFF) + (sum >> 16);
				sum = (sum & 0xFFFF) + (sum >> 16);
				ipf->ipf_checksum = sum;
				ipf->ipf_count += mp->b_datap->db_lim -
						  mp->b_datap->db_base;
				while (mp->b_cont) {
					mp = mp->b_cont;
					ipf->ipf_count += mp->b_datap->db_lim -
							  mp->b_datap->db_base;
				}
				ipf->ipf_tail_mp = mp;
				/*
				 * Keep track of next expected offset in
				 * ipf_end.
				 */
				ipf->ipf_end = end;
				ipf->ipf_stripped_hdr_len = 0;
			} else {
				/* Hard case, hole at the beginning. */
				ipf->ipf_tail_mp = nilp(mblk_t);
				/*
				 * ipf_end == 0 means that we have given up
				 * on easy reassembly.
				 */
				ipf->ipf_end = 0;
				/* Toss the partial checksum since it is to 
				 * hard to compute it with potentially 
				 * overlapping fragments.
				 */
				ipf->ipf_checksum_valid = 0;
				/*
				 * ipf_hole_cnt and ipf_stripped_hdr_len are
				 * set by ip_reassemble.
				 *
				 * ipf_count is updated by ip_reassemble.
				 */
				(void)ip_reassemble(mp, ipf, u1, hdr_length);
			}
			/* Update per ipfb and ill byte counts */
			ipfb->ipfb_count += ipf->ipf_count;
			ill->ill_frag_count += ipf->ipf_count;
			/* If the frag timer wasn't already going, start it. */
			if (!ill->ill_frag_timer_running) {
				ill->ill_frag_timer_running = true;
				mi_timer(ill->ill_rq, ill->ill_frag_timer_mp,
					(long)ip_g_frag_timo_ms);
			}
			goto reass_done;
		}

		/*
		 * We have a new piece of a datagram which is already being
		 * reassembled.
		 */

		if (offset  &&  ipf->ipf_end == offset) {
			/* The new fragment fits at the end */
			ipf->ipf_tail_mp->b_cont = mp;
			/* Update the partial checksum and byte count */
			sum += ipf->ipf_checksum;
			sum = (sum & 0xFFFF) + (sum >> 16);
			sum = (sum & 0xFFFF) + (sum >> 16);
			ipf->ipf_checksum = sum;
			count = mp->b_datap->db_lim -
				mp->b_datap->db_base;
			while (mp->b_cont) {
				mp = mp->b_cont;
				count += mp->b_datap->db_lim -
					 mp->b_datap->db_base;
			}
			ipf->ipf_count += count;
			/* Update per ipfb and ill byte counts */
			ipfb->ipfb_count += count;
			ill->ill_frag_count += count;
			if (u1 & IPH_MF) {
				/* More to come. */
				ipf->ipf_end = end;
				ipf->ipf_tail_mp = mp;
				goto reass_done;
			}
		} else {
			/* Go do the hard cases. */
			/* Toss the partial checksum since it is to 
			 * hard to compute it with potentially 
			 * overlapping fragments.
			 * Call ip_reassable().
			 */
			boolean_t ret;

			/* Save current byte count */
			count = ipf->ipf_count;
			ipf->ipf_checksum_valid = 0;
			ret = ip_reassemble(mp, ipf, u1, offset ? hdr_length:0);
			/* Count of bytes added and subtracted (freeb()ed) */
			count = ipf->ipf_count - count;
			if (count) {
				/* Update per ipfb and ill byte counts */
				ipfb->ipfb_count += count;
				ill->ill_frag_count += count;
			}
			if (! ret)
				goto reass_done;
			/* Return value of 'true' means mp is complete. */
		}
		/*
		 * We have completed reassembly.  Unhook the frag header from
		 * the reassembly list.
		 */
		BUMP_MIB(ip_mib.ipReasmOKs);
		ipfp = ipf->ipf_ptphn;
		mp1 = ipf->ipf_mp;
		sum_valid = ipf->ipf_checksum_valid;
		sum = ipf->ipf_checksum;
		count = ipf->ipf_count;
		ipf = ipf->ipf_hash_next;
		if ( ipf )
			ipf->ipf_ptphn = ipfp;
		ipfp[0] = ipf;
		ill->ill_frag_count -= count;
		ASSERT(ipfb->ipfb_count >= count);
		ipfb->ipfb_count -= count;
		mutex_exit(&ipfb->ipfb_lock);
		/* Ditch the frag header. */
		mp = mp1->b_cont;

		freeb(mp1);
		
		/* Restore original IP length in header. */
		u2 = msgdsize(mp);

		if(mp->b_datap->db_ref > 1) {
			mblk_t *mp2;
 
			mp2 = copymsg(mp);
			freemsg(mp);
			if(!mp2){  
				BUMP_MIB(ip_mib.ipReasmFails);
				return; 
			}
			mp = mp2;
		}
		dp = mp->b_datap;
                ipha = (ipha_t *)ALIGN32(mp->b_rptr);

		ipha->ipha_length = htons(u2);
		/* We're now complete, zip the frag state */
		ipha->ipha_fragment_offset_and_flags = 0;
	}
	
	/* Now we have a complete datagram, destined for this machine. */
	u1 = (ipha->ipha_version_and_hdr_length & 0xF) << 2;
	dst = ipha->ipha_dst;
nottcp:
	switch (ipha->ipha_protocol) {
	case IPPROTO_ICMP:
	case IPPROTO_IGMP: {
		/* Figure out the the incomming logical interface */
		ipif_t	*ipif = nilp(ipif_t);

		MI_HRT_CLEAR(ill->ill_rtmp);
		if (ire->ire_type == IRE_BROADCAST) {
			u32	src;
			
			src = ipha->ipha_src;
			if (!q)
				mi_panic("ip_rput_local");
			ipif = ipif_lookup_remote((ill_t *)q->q_ptr, src);
			if (!ipif) {
#ifdef IP_DEBUG
				ipif_t	*ipif;
				ipif = ((ill_t *)q->q_ptr)->ill_ipif;
				ip0dbg(("ip_rput_local: No source for broadcast/multicast:\n\tsrc 0x%x dst 0x%x ill 0x%x ipif_local_addr 0x%x\n",
					(int)ntohl(src), (int)ntohl(dst),
					(int)q->q_ptr, 
					(int)(ipif ? ipif->ipif_local_addr : -1)));
#endif
				freemsg(mp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
					"ip_rput_locl_end: q %X (%S)", q, "noipif");
				return;
			}
		}
		if (ipha->ipha_protocol == IPPROTO_ICMP) {
			icmp_inbound(q, mp, ire->ire_type, ipif, sum_valid,
				     sum);
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
				"ip_rput_locl_end: q %X (%S)", q, "icmp");
			return;
		}
		if (igmp_input(q, mp, ipif)) {
			/* Bad packet - discarded by igmp_input */
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
				"ip_rput_locl_end: q %X (%S)", q, "igmp");
			return;
		}
		if (!ipc_proto_fanout[ipha->ipha_protocol]) {
			/* No user-level listener for IGMP packets */
			goto drop_pkt;
		}
		/* deliver to local raw users */
		break;
	}
	case IPPROTO_ENCAP:
		if (ip_g_mrouter) {
			mblk_t	*mp2;

			if (ipc_proto_fanout[IPPROTO_ENCAP] == nilp(ipc_t)) {
				ip_mroute_decap(q, mp);
				return;
			}
			mp2 = dupmsg(mp);
			if (mp2)
				ip_mroute_decap(q, mp2);
		}
		break;
	case IPPROTO_UDP:
		{
		/* Pull up the UDP header, if necessary. */
		if ((mp->b_wptr - mp->b_rptr) < (u1 + 8)) {
udppullup:
			if (!pullupmsg(mp, u1 + 8)) {
				BUMP_MIB(ip_mib.ipInDiscards);
				freemsg(mp);
				MI_HRT_CLEAR(ill->ill_rtmp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
					"ip_rput_locl_end: q %X (%S)", q, "udp");
				return;
			}
			ipha = (ipha_t *)ALIGN32(mp->b_rptr);
		}
		/*
		 * Validate the checksum.  This code is a bit funny looking
		 * but may help out the compiler in this crucial spot.
		 */
		up = (u16 *)ALIGN16(((u_char *)ipha) + u1);
		if ( up[3] ) {
			if (sum_valid) {
				sum += IP_UDP_CSUM_COMP + iphs[6] + iphs[7] + 
					iphs[8] + iphs[9] + up[2];
				sum = (sum & 0xFFFF) + (sum >> 16);
				sum = ~(sum + (sum >> 16)) & 0xFFFF;
				if (sum  &&  sum != 0xFFFF) {
					ip1dbg(("ip_rput_local: bad udp checksum 0x%x\n", sum));
					BUMP_MIB(ip_mib.udpInCksumErrs);
					goto drop_pkt;
				}
			} else {
				if ( IP_CSUM(mp, (u_char *)up - (u_char *)ipha,
					     IP_UDP_CSUM_COMP + iphs[6] + 
					     iphs[7] + iphs[8] +
					     iphs[9] + up[2]) ) {
badchecksum:
					BUMP_MIB(ip_mib.udpInCksumErrs);
					goto drop_pkt;
				}
			}
		}
		u1 = up[1];	/* Destination port in net byte order */
		}
		/* Attempt to find a client stream based on destination port. */
		if (ire->ire_type != IRE_BROADCAST) {
			/* Not broadcast or multicast */
			ipc = (ipc_t *)&ipc_udp_fanout[IP_UDP_HASH(u1)];
			do {
				ipc = ipc->ipc_hash_next;
				if (!ipc) {
noudpentry:
					/*
					 * No one bound to this port.  Is
					 * there a client that wants all
					 * unclaimed datagrams?
					 */
					ipc = ipc_proto_fanout[IPPROTO_UDP];
					if (ipc)
						goto wildcard;
					q = WR(q);
					BUMP_MIB(ip_mib.udpNoPorts);
					MI_HRT_CLEAR(ill->ill_rtmp);
					/*
					 * Have to correct checksum since
					 * the packet might have been 
					 * fragmented and the reassembly code
					 * does not restore the IP checksum.
					 */
					ipha->ipha_hdr_checksum = 0;
					ipha->ipha_hdr_checksum =
						ip_csum_hdr(ipha);
					icmp_unreachable(q, mp, 
						 ICMP_PORT_UNREACHABLE);
					TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
						"ip_rput_locl_end: q %X (%S)", q, "badudpport");
					return;
				}
			} while (!IP_UDP_MATCH(ipc, u1, ire->ire_addr));
			/* Found a client.  Send it upstream. */
			q = ipc->ipc_rq;
			if (!canputnext(q)) {
nocanput:
				BUMP_MIB(ip_mib.udpInOverflows);
				MI_HRT_CLEAR(ill->ill_rtmp);
				/*
				 * Have to correct checksum since
				 * the packet might have been 
				 * fragmented and the reassembly code
				 * does not restore the IP checksum.
				 */
				ipha->ipha_hdr_checksum = 0;
				ipha->ipha_hdr_checksum =
					ip_csum_hdr(ipha);
				icmp_source_quench(WR(q), mp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
					"ip_rput_locl_end: q %X (%S)", q, "fc-drop");
				return;
			}
			BUMP_MIB(ip_mib.ipInDelivers);
#ifdef MI_HRTIMING
			if ( MI_HRT_IS_SET(ill->ill_rtmp) ) {
				MI_HRT_INCREMENT(ill->ill_rtime,
					ill->ill_rtmp, 1);
				MI_HRT_CLEAR(ill->ill_rtmp);
			}
#endif /* MI_HRTIMING */
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
			    "ip_rput_locl_end: q %X (%S)", q, "xx-putnext up");
			putnext(q, mp);
			return;
		}
udpbroadcast:
		MI_HRT_CLEAR(ill->ill_rtmp);
		/* 
		 * Broadcast and multicast case 
		 * (CLASSD addresses will come in on an IRE_BROADCAST)
		 */
		ipc = (ipc_t *)&ipc_udp_fanout[IP_UDP_HASH(u1)];
		do {
			ipc = ipc->ipc_hash_next;
			if (!ipc) {
				/*
				 * No one bound to this port.  Is there a
				 * client that wants all unclaimed datagrams?
				 */
				ipc = ipc_proto_fanout[IPPROTO_UDP];
				if (ipc)
					goto wildcard;
				BUMP_MIB(ip_mib.udpNoPorts);
				freemsg(mp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
					"ip_rput_locl_end: q %X (%S)", q, "badipc");
				return;
			}
		} while (!IP_UDP_MATCH(ipc, u1, dst)
		|| !ipc_wantpacket(ipc, dst));
		/*
		 * If SO_REUSEADDR is set all multicast and broadcast packets
		 * will be delivered to all streams bound to the same port.
		 */
		if (ipc->ipc_reuseaddr) {
			ipc_t	* first_ipc = ipc;
			mblk_t	* mp1;
			
			for (;;) {
				for ( ipc = ipc->ipc_hash_next
				; ipc
				; ipc = ipc->ipc_hash_next ) {
					if ( IP_UDP_MATCH(ipc, u1, dst) &&
					    ipc_wantpacket(ipc, dst))
						break;
				}
				if (!ipc  ||  !(mp1 = copymsg(mp))) {
					ipc = first_ipc;
					break;
				}
				q = ipc->ipc_rq;
				if (!canputnext(q)) {
					BUMP_MIB(ip_mib.udpInOverflows);
					freemsg(mp1);
				} else {
					BUMP_MIB(ip_mib.ipInDelivers);
					putnext(q, mp1);
				}
			}
		}
		/* Found a client.  Send it upstream. */
		q = ipc->ipc_rq;
		if (!canputnext(q)) {
			BUMP_MIB(ip_mib.udpInOverflows);
			freemsg(mp);
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
				"ip_rput_locl_end: q %X (%S)", q, "udp:fc");
			return;
		}
		BUMP_MIB(ip_mib.ipInDelivers);
		putnext(q, mp);
		return;
	case IPPROTO_TCP:
		{
		len = mp->b_wptr - mp->b_rptr;

		/* Pull up a minimal TCP header, if necessary. */
		if (len < (u1 + 20)) {
tcppullup:
			if (!pullupmsg(mp, u1 + 20)) {
				BUMP_MIB(ip_mib.ipInDiscards);
				freemsg(mp);
				MI_HRT_CLEAR(ill->ill_rtmp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
					"ip_rput_locl_end: q %X (%S)", q, "tcp:badpullup");
				return;
			}
			ipha = (ipha_t *)ALIGN32(mp->b_rptr);
			len = mp->b_wptr - mp->b_rptr;
		}
		
		/*
		 * Extract the offset field from the TCP header.  As usual, we
		 * try to help the compiler more than the reader.
		 */
		offset = ((u_char *)ipha)[u1 + 12] >> 4;
		if (offset != 5) {
tcpoptions:
			if (offset < 5) {
				freemsg(mp);
				MI_HRT_CLEAR(ill->ill_rtmp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
					"ip_rput_locl_end: q %X (%S)", q, "tcp_badoff");
				return;
			}
			/* There must be TCP options.  Make sure we can them. */
			offset <<= 2;
			offset += u1;
			if (len < offset) {
				if (!pullupmsg(mp, offset)) {
					BUMP_MIB(ip_mib.ipInDiscards);
					freemsg(mp);
					MI_HRT_CLEAR(ill->ill_rtmp);
					TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
						"ip_rput_locl_end: q %X (%S)", q, "tcp:pullupfailed");
					return;
				}
				ipha = (ipha_t *)ALIGN32(mp->b_rptr);
				len = mp->b_wptr - rptr;
			}
		}
		
		/* Get the total packet length in len, including the headers. */
		if (mp->b_cont) {
multipkttcp:
			len = msgdsize(mp);
		}
		
		/*
		 * Check the TCP checksum by pulling together the pseudo-
		 * header checksum, and passing it to ip_csum to be added in
		 * with the TCP datagram.
		 */
		up = (u16 *)ALIGN16(rptr + u1);/* TCP header pointer. */
		ports = *(u32 *)ALIGN32(up);
		u1 = len - u1;			/* TCP datagram length. */
#ifdef	_BIG_ENDIAN
		u1 += IPPROTO_TCP;
#else
		u1 = ((u1 >> 8) & 0xFF) + (((u1 & 0xFF) + IPPROTO_TCP) << 8);
#endif
		u1 += iphs[6] + iphs[7] + iphs[8] + iphs[9];
		if (dp->db_type != M_DATA || dp->db_ref > 1) {
			/*
			 * Not M_DATA mblk or its a dup, so do the checksum now.
			 */
			if (IP_CSUM(mp, (u_char *)up - rptr, u1)) {
tcpcksumerr:
				BUMP_MIB(ip_mib.tcpInErrs);
				goto drop_pkt;
			}
		} else {
			/*
			 * M_DATA mblk and not a dup, so postpone the checksum.
			 */
			mblk_t	* mp1 = mp;
			dblk_t	* dp1;

			u1 = (u1 >> 16) + (u1 & 0xffff);
			u1 += (u1 >> 16);
			*(u16 *)dp->db_struioun.data = u1;
			dp->db_struiobase = (u_char *)up;
			dp->db_struioptr = (u_char *)up;
			dp->db_struiolim = mp->b_wptr;
			dp->db_struioflag = STRUIO_SPEC|STRUIO_IP;
			while ((mp1 = mp1->b_cont) != NULL) {
				dp1 = mp1->b_datap;
				*(u16 *)dp1->db_struioun.data = 0;
				dp1->db_struiobase = mp1->b_rptr;
				dp1->db_struioptr = mp1->b_rptr;
				dp1->db_struiolim = mp1->b_wptr;
				dp1->db_struioflag = STRUIO_SPEC|STRUIO_IP;
			}
		}

		/* Find a TCP client stream for this packet. */
		ipc = (ipc_t *)&ipc_tcp_fanout[IP_TCP_HASH(ipha->ipha_src,
								ports)];
		do {
			ipc = ipc->ipc_hash_next;
			if (!ipc) {
notcpport:
				if (dp->db_struioflag & STRUIO_IP) {
					/*
					* Do the postponed checksum now.
					*/
					mblk_t * mp1;
					int off = dp->db_struioptr - mp->b_rptr;

					if (IP_CSUM(mp, off, 0))
						goto tcpcksumerr;
					mp1 = mp;
					do {
						mp1->b_datap->db_struioflag = 0;
					} while ((mp1 = mp1->b_cont) != NULL);
				}
				/*
				 * No hard-bound match.  Look for a
				 * stream that wants all unclaimed.  Note
				 * that TCP must normally make sure that
				 * there is such a stream, otherwise it
				 * will be tough to get inbound connections
				 * going.
				 */
				ipc = ipc_proto_fanout[IPPROTO_TCP];
				if (ipc)
					goto wildcard;
				q = WR(q);
				/*
				 * Have to correct checksum since
				 * the packet might have been 
				 * fragmented and the reassembly code
				 * does not restore the IP checksum.
				 */
				ipha->ipha_hdr_checksum = 0;
				ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);
				icmp_unreachable(q, mp, ICMP_PORT_UNREACHABLE);
				MI_HRT_CLEAR(ill->ill_rtmp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
					"ip_rput_locl_end: q %X (%S)", q, "tcp:badipc");
				return;
			}
		} while (!TCP_MATCH(ipc, ipha, ports));
		/* Got a client, up it goes. */
		q = ipc->ipc_rq;
		BUMP_MIB(ip_mib.ipInDelivers);
#ifdef MI_HRTIMING
		if ( MI_HRT_IS_SET(ill->ill_rtmp) ) {
			MI_HRT_INCREMENT(ill->ill_rtime, ill->ill_rtmp, 1);
			MI_HRT_CLEAR(ill->ill_rtmp);
		}
#endif /* MI_HRTIMING */
		TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
			"ip_rput_locl_end: q %X (%S)", q, "tcp");
		putnext(q, mp);
		return;
		}
	default:
		break;
	}

	/*
	 * Handle protocols with which IP is less intimate.  There
	 * can be more than one stream bound to a particular
	 * protocol.  When this is the case, each one gets a copy
	 * of any incoming packets.
	 */
	ipc = ipc_proto_fanout[ipha->ipha_protocol];
	if (!ipc) {
		BUMP_MIB(ip_mib.ipInUnknownProtos);
		MI_HRT_CLEAR(ill->ill_rtmp);
		/*
		 * Have to correct checksum since
		 * the packet might have been 
		 * fragmented and the reassembly code
		 * does not restore the IP checksum.
		 */
		ipha->ipha_hdr_checksum = 0;
		ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);
		icmp_unreachable(WR(q), mp, ICMP_PROTOCOL_UNREACHABLE);
		TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
			"ip_rput_locl_end: q %X (%S)", q, "other:badipc");
		return;
	}
wildcard:;
	for (;;) {
		if ((ipc->ipc_udp_addr != 0 && ipc->ipc_udp_addr != dst) ||
		    !ipc_wantpacket(ipc, dst)) {
			ipc = ipc->ipc_hash_next;
			if (ipc == nilp(ipc_t)) {
				freemsg(mp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
					"ip_rput_locl_end: q %X (%S)", q, "wild:nil");
				return;
			}
			continue;
		}
		q = ipc->ipc_rq;
		ipc = ipc->ipc_hash_next;
		if (!ipc  ||  !(mp1 = dupmsg(mp))) {
			if (!canputnext(q)) {
				BUMP_MIB(ip_mib.rawipInOverflows);
				MI_HRT_CLEAR(ill->ill_rtmp);
				/*
				 * Have to correct checksum since
				 * the packet might have been 
				 * fragmented and the reassembly code
				 * does not restore the IP checksum.
				 */
				ipha->ipha_hdr_checksum = 0;
				ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);
				icmp_source_quench(WR(q), mp);
				TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
					"ip_rput_locl_end: q %X (%S)", q, "wild:fc");
				return;
			}
			BUMP_MIB(ip_mib.ipInDelivers);
			TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
				"ip_rput_locl_end: q %X (%S)", q, "wild:err");
			putnext(q, mp);
			return;
		}
		if (!canputnext(q)) {
			BUMP_MIB(ip_mib.rawipInOverflows);
			/*
			 * Have to correct checksum since
			 * the packet might have been 
			 * fragmented and the reassembly code
			 * does not restore the IP checksum.
			 */
			ipha->ipha_hdr_checksum = 0;
			ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);
			icmp_source_quench(WR(q), mp);
		} else {
			BUMP_MIB(ip_mib.ipInDelivers);
			putnext(q, mp);
		}
		mp = mp1;
	}

drop_pkt:;
	MI_HRT_CLEAR(ill->ill_rtmp);
	ip1dbg(("ip_rput_local: drop pkt\n"));
	freemsg(mp);
	TRACE_2(TR_FAC_IP, TR_IP_RPUT_LOCL_END,
		"ip_rput_locl_end: q %X (%S)", q, "droppkt");
#undef	rptr
}
	
/* Update any source route, record route or timestamp options */
/* Check that we are at end of strict source route */
staticf int
ip_rput_local_options (q, mp, ipha, ire)
	queue_t	* q;
	mblk_t	* mp;
	ipha_t	* ipha;
	ire_t	* ire;
{
	u32	totallen;
	u_char	* opt;
	u32	optval;
	u32	optlen;
	u32	dst;
	u32	u1;

	ip2dbg(("ip_rput_local_options\n"));
	totallen = ipha->ipha_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	opt = (u_char *)&ipha[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			return 0;
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}
		ip2dbg(("ip_rput_local_options: opt %d, len %d\n", 
			  optval, optlen));

		if (optlen == 0 || optlen > totallen)
			break;

		switch (optval) {
			u32 off;
		case IPOPT_SSRR:
		case IPOPT_LSRR: 
			off = opt[IPOPT_POS_OFF];
			off--;
			if (optlen < IP_ADDR_LEN ||
			    off > optlen - IP_ADDR_LEN) {
				/* End of source route */
				ip1dbg(("ip_rput_local_options: end of SR\n"));
				break;
			}
			/* This will only happen if two consecutive entries
			 * in the source route contains our address or if
			 * it is a packet with a loose source route which 
			 * reaches us before consuming the whole source route
			 */
			ip1dbg(("ip_rput_local_options: not end of SR\n"));
			if (optval == IPOPT_SSRR) {
				goto bad_src_route;
			}
			/* Hack: instead of dropping the packet truncate the
			 * source route to what has been used.
			 */
			bzero((char *)opt + off, optlen - off);
			opt[IPOPT_POS_LEN] = off;
			break;
		case IPOPT_RR:
			off = opt[IPOPT_POS_OFF];
			off--;
			if (optlen < IP_ADDR_LEN ||
			    off > optlen - IP_ADDR_LEN) {
				/* No more room - ignore */
				ip1dbg(("ip_rput_forward_options: end of RR\n"));
				break;
			}
			bcopy((char *)&ire->ire_src_addr, (char *)opt + off,
			      IP_ADDR_LEN);
			opt[IPOPT_POS_OFF] += IP_ADDR_LEN;
			break;
		case IPOPT_IT:
			/* Insert timestamp if there is romm */
			switch (opt[IPOPT_POS_OV_FLG] & 0x0F) {
			case IPOPT_IT_TIME:
				off = IPOPT_IT_TIMELEN;
				break;
			case IPOPT_IT_SPEC:
#ifdef IPOPT_IT_SPEC_BSD
			case IPOPT_IT_SPEC_BSD:
#endif
				/* Verify that the address matched */
				off = opt[IPOPT_POS_OFF] - 1;
				bcopy((char *)opt + off, (char *)&dst, 
				      IP_ADDR_LEN);
				if (!ire_lookup_myaddr(dst))
					/* Not for us */
					break;
				/* FALLTHRU */
			case IPOPT_IT_TIME_ADDR:
				off = IP_ADDR_LEN + IPOPT_IT_TIMELEN;
				break;
			}
			if (opt[IPOPT_POS_OFF] - 1 + off > optlen) {
				/* Increase overflow counter */
				off = (opt[IPOPT_POS_OV_FLG] >> 4) + 1;
				opt[IPOPT_POS_OV_FLG] = 
					(opt[IPOPT_POS_OV_FLG] & 0x0F) | (off << 4);
				break;
			}
			off = opt[IPOPT_POS_OFF] - 1;
			switch (opt[IPOPT_POS_OV_FLG] & 0x0F) {
			case IPOPT_IT_SPEC:
#ifdef IPOPT_IT_SPEC_BSD
			case IPOPT_IT_SPEC_BSD:
#endif
			case IPOPT_IT_TIME_ADDR:
				bcopy((char *)&ire->ire_src_addr, 
				      (char *)opt + off,
				      IP_ADDR_LEN);
				opt[IPOPT_POS_OFF] += IP_ADDR_LEN;
				/* FALLTHRU */
			case IPOPT_IT_TIME:
				off = opt[IPOPT_POS_OFF] - 1;
				/* Compute # of milliseconds since midnight */
				u1 = ((time_in_secs % (24 * 60 * 60)) * 1000) +
					(LBOLT_TO_MS(lbolt) % 1000);
				bcopy((char *)&u1, (char *)opt + off,
				      IP_ADDR_LEN);
				opt[IPOPT_POS_OFF] += IPOPT_IT_TIMELEN;
				break;
			}
			break;
		}
		totallen -= optlen;
		opt += optlen;
	}
	return 0;

bad_src_route:
	q = WR(q);
	icmp_unreachable(q, mp, ICMP_SOURCE_ROUTE_FAILED);
	return -1;

}
	
/*
 * Process IP options in an inbound packet.  If an option affects the
 * effective destination address, return the next hop address.
 * Returns -1 if something fails in which case an ICMP error has been sent
 * and mp freed.
 */
staticf u32
ip_rput_options (q, mp, ipha)
	queue_t	* q;
	mblk_t	* mp;
	ipha_t	* ipha;
{
	u32	totallen;
	u_char	* opt;
	u32	optval;
	u32	optlen;
	u32	dst;
	int	code = 0;

	ip2dbg(("ip_rput_options\n"));
	dst = ipha->ipha_dst;
	totallen = ipha->ipha_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	opt = (u_char *)&ipha[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			return dst;
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}
		if (optlen == 0 || optlen > totallen) {
			ip1dbg(("ip_rput_options: bad option len %d, %d\n",
				optlen, totallen));
			code = (char *)&opt[IPOPT_POS_LEN] - (char *)ipha;
			goto param_prob;
		}
		ip2dbg(("ip_rput_options: opt %d, len %d\n", 
			  optval, optlen));
		/*
		 * Note: we need to verify the checksum before we
		 * modify anything thus this routine only extracts the next
		 * hop dst from any source route.
		 */
		switch (optval) {
			u32 off;
		case IPOPT_SSRR:
		case IPOPT_LSRR: 
			if (!ire_lookup_myaddr(dst)) {
				if (optval == IPOPT_SSRR) {
					ip1dbg(("ip_rput_options: not next strict source route 0x%x\n",
						(int)ntohl(dst)));
					code = (char *)&ipha->ipha_dst -
						(char *)ipha;
					goto param_prob; /* RouterReq's*/
				}
				ip2dbg(("ip_rput_options: not next source route 0x%x\n",
					(int)ntohl(dst)));
				break;
			}
			off = opt[IPOPT_POS_OFF];
			if (off < IPOPT_MINOFF_SR) {
				ip1dbg(("ip_rput_options: bad option offset %d\n",
					off));
				code = (char *)&opt[IPOPT_POS_OFF] -
					(char *)ipha;
				goto param_prob;
			}
			off--;
			if (optlen < IP_ADDR_LEN ||
			    off > optlen - IP_ADDR_LEN) {
				/* End of source route */
				ip1dbg(("ip_rput_options: end of SR\n"));
				break;
			}
			bcopy((char *)opt + off, (char *)&dst, IP_ADDR_LEN);
			ip1dbg(("ip_rput_options: next hop 0x%x\n",
				(int)ntohl(dst)));
			/*
			 * For strict: verify that dst is directly 
			 * reachable.
			 */
			if (optval == IPOPT_SSRR &&
			    !ire_lookup_interface(dst, IRE_INTERFACE)) {
				ip1dbg(("ip_rput_options: SSRR not directly reachable: 0x%x\n", 
					(int)ntohl(dst)));
				goto bad_src_route;
			}
			/*
			 * Defer update of the offset and the record route
			 * until the packet is forwarded.
			 */
			break;
		case IPOPT_RR:
			off = opt[IPOPT_POS_OFF];
			if (off < IPOPT_MINOFF_SR) {
				ip1dbg(("ip_rput_options: bad option offset %d\n",
					off));
				code = (char *)&opt[IPOPT_POS_OFF] -
					(char *)ipha;
				goto param_prob;
			}
			break;
		case IPOPT_IT: 
			/* Verify that length >=5 and that there is either
			 * room for another timestamp or that the overflow 
			 * counter is not maxed out.
			 */
			code = (char *)&opt[IPOPT_POS_LEN] - (char *)ipha;
			if (optlen < IPOPT_MINLEN_IT) {
				goto param_prob;
			}
			switch (opt[IPOPT_POS_OV_FLG] & 0x0F) {
			case IPOPT_IT_TIME:
				off = IPOPT_IT_TIMELEN;
				break;
			case IPOPT_IT_TIME_ADDR:
			case IPOPT_IT_SPEC:
#ifdef IPOPT_IT_SPEC_BSD
			case IPOPT_IT_SPEC_BSD:
#endif
				off = IP_ADDR_LEN + IPOPT_IT_TIMELEN;
				break;
			default:
				code = (char *)&opt[IPOPT_POS_OV_FLG] -
					(char *)ipha;
				goto param_prob;
			}
			if (opt[IPOPT_POS_OFF] - 1 + off > optlen &&
			    (opt[IPOPT_POS_OV_FLG] & 0xF0) == 0xF0) {
				/* No room and the overflow counter is 15
				 * already.
				 */
				goto param_prob;
			}
			off = opt[IPOPT_POS_OFF];
			if (off < IPOPT_MINOFF_IT) {
				ip1dbg(("ip_rput_options: bad option offset %d\n",
					off));
				code = (char *)&opt[IPOPT_POS_OFF] -
					(char *)ipha;
				goto param_prob;
			}
			break;
		}
		totallen -= optlen;
		opt += optlen;
	}
	return dst;

param_prob:
	q = WR(q);
	icmp_param_problem(q, mp, code);
	return (u32)-1;

bad_src_route:
	q = WR(q);
	icmp_unreachable(q, mp, ICMP_SOURCE_ROUTE_FAILED);
	return (u32)-1;
}

/*
 * Read service routine.  We see three kinds of messages here.  The first is
 * fragmentation reassembly timer messages.  The second is data packets
 * that were queued to avoid possible intra-machine looping conditions
 * on Streams implementations that do not support QUEUEPAIR synchronization.
 * The third is the IRE expiration timer message.
 */
staticf void
ip_rsrv (q)
	queue_t	* q;
{
reg	ill_t	* ill = (ill_t *)q->q_ptr;
reg	mblk_t	* mp;

	TRACE_1(TR_FAC_IP, TR_IP_RSRV_START,
		"ip_rsrv_start: q %X", q);

	while (mp = getq(q)) {
		/* TODO need define for M_PCSIG! */
		if (mp->b_datap->db_type == M_PCSIG) {
			/* Timer */
			if (!mi_timer_valid(mp))
				continue;
			if (mp == ill->ill_frag_timer_mp) {
				/*
				 * The frag timer.  If mi_timer_valid says it
				 * hasn't been reset since it was queued, call
				 * ill_frag_timeout to do garbage collection.
				 * ill_frag_timeout returns 'true' if there are
				 * still fragments left on the queue, in which
				 * case we restart the timer.
				 */
				if ((ill->ill_frag_timer_running = 
				     ill_frag_timeout(ill,
						      ip_g_frag_timeout)) )
					mi_timer(q, mp, 
					   (long)(ip_g_frag_timo_ms >> 1));
				continue;
			}
			if ( mp == ip_timer_mp ) {
				/* It's the IRE expiration timer. */
				become_writer(q, mp, (pfi_t)ip_trash);
				continue;
			}
			if ( mp == igmp_timer_mp ) {
				/* It's the IGMP timer. */
				igmp_timeout();
				continue;
			}
		}
		/* Intramachine packet forwarding. */
		putnext(q, mp);
	}
	TRACE_1(TR_FAC_IP, TR_IP_RSRV_END,
		"ip_rsrv_end: q %X", q);
}

/* IP & ICMP info in 5 msg's ...
 *  - ip fixed part (mib2_ip_t)
 *  - icmp fixed part (mib2_icmp_t)
 *  - ipAddrEntryTable (ip 20)		our ip_ipif_status
 *  - ipRouteEntryTable (ip 21)		our ip_ire_status for all IREs
 *  - ipNetToMediaEntryTable (ip 22)	ip 21 with different info?
 * ip_21 and ip_22 are augmented in arp to include arp cache entries not
 * already present.
 * NOTE: original mpctl is copied for msg's 2..5, since its ctl part
 * already filled in by caller.
 */
staticf	int
ip_snmp_get (q, mpctl)
	queue_t		* q;
	mblk_t		* mpctl;
{
	mblk_t			* mpdata[2];
	mblk_t			* mp2ctl;
	struct opthdr		* optp;
	ill_t			* ill;
	ipif_t			* ipif;
	uint			bitval;
	char			buf[sizeof(mib2_ipAddrEntry_t)];
	mib2_ipAddrEntry_t	* ap = (mib2_ipAddrEntry_t *)ALIGN32(buf);

	if (!mpctl 
	|| !(mpdata[0] = mpctl->b_cont)
	|| !(mp2ctl = copymsg(mpctl)))
		return 0;

	/* fixed length IP structure... */
	optp = (struct opthdr *)ALIGN32(
		 &mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	optp->level = MIB2_IP;
	optp->name = 0;
	SET_MIB(ip_mib.ipForwarding,          (WE_ARE_FORWARDING ? 1 : 2));
	SET_MIB(ip_mib.ipDefaultTTL,          ip_def_ttl);
	SET_MIB(ip_mib.ipReasmTimeout,        ip_g_frag_timeout);
	SET_MIB(ip_mib.ipAddrEntrySize,       sizeof(mib2_ipAddrEntry_t));
	SET_MIB(ip_mib.ipRouteEntrySize,      sizeof(mib2_ipRouteEntry_t));
	SET_MIB(ip_mib.ipNetToMediaEntrySize, sizeof(mib2_ipNetToMediaEntry_t));
	if (!snmp_append_data(mpdata[0], (char *)&ip_mib, sizeof(ip_mib))) {
		ip0dbg(("ip_snmp_get: failed %d bytes\n", sizeof(ip_mib)));
	}

	optp->len = msgdsize(mpdata[0]);
	ip1dbg(("ip_snmp_get: level %d, name %d, len %d\n",
		(int)optp->level, (int)optp->name, (int)optp->len));
	qreply(q, mpctl);

	/* fixed length ICMP structure... */
	mpctl = mp2ctl;
	mpdata[0] = mpctl->b_cont;
	mp2ctl = copymsg(mpctl);
	optp = (struct opthdr *)ALIGN32(
			 &mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	optp->level = MIB2_ICMP;
	optp->name = 0;
	if (!snmp_append_data(mpdata[0], (char *)&icmp_mib, sizeof(icmp_mib))){
		ip0dbg(("ip_snmp_get: failed %d bytes\n", sizeof(icmp_mib)));
	}
	optp->len = msgdsize(mpdata[0]);
	ip1dbg(("ip_snmp_get: level %d, name %d, len %d\n",
		(int)optp->level, (int)optp->name, (int)optp->len));
	qreply(q, mpctl);
	if (!mp2ctl)
		return 1;

	/* fixed length IGMP structure... */
	mpctl = mp2ctl;
	mpdata[0] = mpctl->b_cont;
	mp2ctl = copymsg(mpctl);
	optp = (struct opthdr *)ALIGN32(
			 &mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	optp->level = EXPER_IGMP;
	optp->name = 0;
	if (!snmp_append_data(mpdata[0], (char *)&igmpstat, sizeof(igmpstat))){
		ip0dbg(("ip_snmp_get: failed %d bytes\n", sizeof(igmpstat)));
	}
	optp->len = msgdsize(mpdata[0]);
	ip1dbg(("ip_snmp_get: level %d, name %d, len %d\n",
		(int)optp->level, (int)optp->name, (int)optp->len));
	qreply(q, mpctl);
	if (!mp2ctl)
		return 1;

	/* fixed length multicast routing statistics structure... */
	mpctl = mp2ctl;
	mpdata[0] = mpctl->b_cont;
	mp2ctl = copymsg(mpctl);
	optp = (struct opthdr *)ALIGN32(
			 &mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	if (!ip_mroute_stats(optp, mpdata[0])) {
		ip0dbg(("ip_mroute_stats: failed\n"));
	}
	optp->len = msgdsize(mpdata[0]);
	ip1dbg(("ip_snmp_get: level %d, name %d, len %d\n",
		(int)optp->level, (int)optp->name, (int)optp->len));
	qreply(q, mpctl);
	if (!mp2ctl)
		return 1;

	/* ipAddrEntryTable */
	mpctl = mp2ctl;
	mpdata[0] = mpctl->b_cont;
	mp2ctl = copymsg(mpctl);
	optp = (struct opthdr *)ALIGN32(
			 &mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	optp->level = MIB2_IP;
	optp->name = MIB2_IP_20;
	for ( ill = ill_g_head; ill; ill = ill->ill_next ) {
		for ( ipif = ill->ill_ipif; ipif; ipif = ipif->ipif_next ) {
			(void)ipif_get_name(ipif, ap->ipAdEntIfIndex.o_bytes, OCTET_LENGTH);
			ap->ipAdEntIfIndex.o_length = mi_strlen(ap->ipAdEntIfIndex.o_bytes);
			ap->ipAdEntAddr             = ipif->ipif_local_addr;
			ap->ipAdEntNetMask          = ipif->ipif_net_mask;
			for (bitval = 1; bitval && !(bitval & ipif->ipif_broadcast_addr); bitval <<= 1)
				noop;
			ap->ipAdEntBcastAddr       = bitval;
			ap->ipAdEntReasmMaxSize    = 65535;
			ap->ipAdEntInfo.ae_mtu         = ipif->ipif_mtu,
			ap->ipAdEntInfo.ae_metric      = ipif->ipif_metric,
			ap->ipAdEntInfo.ae_broadcast_addr = ipif->ipif_broadcast_addr,
			ap->ipAdEntInfo.ae_pp_dst_addr    = ipif->ipif_pp_dst_addr,
			ap->ipAdEntInfo.ae_flags       = ipif->ipif_flags;
			if (!snmp_append_data(mpdata[0], buf, sizeof(mib2_ipAddrEntry_t))) {
				ip0dbg(("ip_snmp_get: failed %d bytes\n", 
					 sizeof(mib2_ipAddrEntry_t)));
			}
		}
	}
	optp->len = msgdsize(mpdata[0]);
	ip1dbg(("ip_snmp_get: level %d, name %d, len %d\n",
		(int)optp->level, (int)optp->name, (int)optp->len));
	qreply(q, mpctl);
	if (!mp2ctl)
		return 1;
	
	/* ipGroupMember table */
	mpctl = mp2ctl;
	mpdata[0] = mpctl->b_cont;
	mp2ctl = copymsg(mpctl);
	optp = (struct opthdr *)ALIGN32(
			 &mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	optp->level = MIB2_IP;
	optp->name = EXPER_IP_GROUP_MEMBERSHIP;
	for ( ill = ill_g_head; ill; ill = ill->ill_next ) {
		ip_member_t	ipm;
		ilm_t		*ilm;

		for ( ipif = ill->ill_ipif; ipif; ipif = ipif->ipif_next ) {
			(void)ipif_get_name(ipif, ipm.ipGroupMemberIfIndex.o_bytes, OCTET_LENGTH);
			ipm.ipGroupMemberIfIndex.o_length = mi_strlen(ipm.ipGroupMemberIfIndex.o_bytes);
			for (ilm = ill->ill_ilm; ilm; ilm = ilm->ilm_next) {
				if (ilm->ilm_ipif != ipif)
					continue;
				ipm.ipGroupMemberAddress = ilm->ilm_addr;
				ipm.ipGroupMemberRefCnt = ilm->ilm_refcnt;
				if (!snmp_append_data(mpdata[0], (char *)&ipm,
						      sizeof(ipm))) {
					ip0dbg(("ip_snmp_get: failed %d bytes\n", 
						  sizeof(ipm)));
				}
			}
		}
	}
	optp->len = msgdsize(mpdata[0]);
	ip1dbg(("ip_snmp_get: level %d, name %d, len %d\n",
		(int)optp->level, (int)optp->name, (int)optp->len));
	qreply(q, mpctl);
	if (!mp2ctl)
		return 1;
	
	/* Create the multicast virtual interface table... */
	mpctl = mp2ctl;
	mpdata[0] = mpctl->b_cont;
	mp2ctl = copymsg(mpctl);
	optp = (struct opthdr *)ALIGN32(
			 &mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	if (!ip_mroute_vif(optp, mpdata[0])){
		ip0dbg(("ip_mroute_vif: failed\n"));
	}
	optp->len = msgdsize(mpdata[0]);
	ip1dbg(("ip_snmp_get: level %d, name %d, len %d\n",
		(int)optp->level, (int)optp->name, (int)optp->len));
	qreply(q, mpctl);
	if (!mp2ctl)
		return 1;

	/* Create the multicast routing table... */
	mpctl = mp2ctl;
	mpdata[0] = mpctl->b_cont;
	mp2ctl = copymsg(mpctl);
	optp = (struct opthdr *)ALIGN32(
			 &mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	if (!ip_mroute_mrt(optp, mpdata[0])){
		ip0dbg(("ip_mroute_mrt: failed\n"));
	}
	optp->len = msgdsize(mpdata[0]);
	ip1dbg(("ip_snmp_get: level %d, name %d, len %d\n",
		(int)optp->level, (int)optp->name, (int)optp->len));
	qreply(q, mpctl);
	if (!mp2ctl)
		return 1;

	/* create both ipRouteEntryTable (mpctl) & ipNetToMediaEntryTable (mp2ctl) in one IRE walk */
	mpctl = mp2ctl;
	mpdata[0] = mpctl->b_cont;
	mp2ctl = copymsg(mpctl);
	if (!mp2ctl) {
		freemsg(mpctl);
		return 1;
	}
	mpdata[1] = mp2ctl->b_cont;
	ire_walk(ip_snmp_get2, (char *)mpdata);

	/* ipRouteEntryTable */
	optp = (struct opthdr *)ALIGN32(
			 &mpctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	optp->level = MIB2_IP;
	optp->name = MIB2_IP_21;
	optp->len = msgdsize(mpdata[0]);
	ip1dbg(("ip_snmp_get: level %d, name %d, len %d\n",
		(int)optp->level, (int)optp->name, (int)optp->len));
	qreply(q, mpctl);
	
	/* ipNetToMediaEntryTable */
	optp = (struct opthdr *)ALIGN32(
			 &mp2ctl->b_rptr[sizeof(struct T_optmgmt_ack)]);
	optp->level = MIB2_IP;
	optp->name = MIB2_IP_22;
	optp->len = msgdsize(mpdata[1]);
	ip1dbg(("ip_snmp_get: level %d, name %d, len %d\n",
		(int)optp->level, (int)optp->name, (int)optp->len));
	qreply(q, mp2ctl);
	return 1;
}
/*
 * ire_walk routine to create both ipRouteEntryTable & ipNetToMediaEntryTable in one IRE walk
 */
staticf void
ip_snmp_get2 (ire, mpdata)
	ire_t	* ire;
	mblk_t	* mpdata[];
{
	ill_t				* ill;
	ipif_t				* ipif;
	mblk_t				* llmp;
	dl_unitdata_req_t		* dlup;
	mib2_ipRouteEntry_t		re;
	mib2_ipNetToMediaEntry_t	ntme;
	/*
	 * Return all IRE types for route table... let caller pick and choose
	 */
	re.ipRouteDest = ire->ire_addr;
	ipif = NULL;
	if (ire->ire_type & IRE_INTERFACE)
		ipif = ire_interface_to_ipif(ire);
	if (ipif == NULL)	
		ipif = ire_to_ipif(ire);
	re.ipRouteIfIndex.o_length = 0;
	if (ipif) {
		char buf[32];
		char *name;

		name = ipif_get_name(ipif, buf, sizeof(buf));
		re.ipRouteIfIndex.o_length = strlen(name) + 1;
		bcopy(name, re.ipRouteIfIndex.o_bytes, 
		      re.ipRouteIfIndex.o_length);
	}
	re.ipRouteMetric1 = -1;
	re.ipRouteMetric2 = -1;
	re.ipRouteMetric3 = -1;
	re.ipRouteMetric4 = -1;
	if (ire->ire_type & (IRE_INTERFACE|IRE_LOOPBACK|IRE_BROADCAST))
		re.ipRouteNextHop = ire->ire_src_addr;
	else
		re.ipRouteNextHop = ire->ire_gateway_addr;
	/* indirect(4) or direct(3) */
	re.ipRouteType = ire->ire_gateway_addr ? 4 : 3;
	re.ipRouteProto = -1;
	re.ipRouteAge = (u32)time_in_secs - ire->ire_create_time;
	re.ipRouteMask = ire->ire_mask;
	re.ipRouteMetric5 = -1;
	re.ipRouteInfo.re_max_frag  = ire->ire_max_frag;
	re.ipRouteInfo.re_frag_flag = ire->ire_frag_flag;
	re.ipRouteInfo.re_rtt       = ire->ire_rtt;
	if (ire->ire_ll_hdr_length)
		llmp = ire->ire_ll_hdr_saved_mp;
	else
		llmp = ire->ire_ll_hdr_mp;
	re.ipRouteInfo.re_ref       = llmp ? llmp->b_datap->db_ref : 0;
	re.ipRouteInfo.re_src_addr  = ire->ire_src_addr;
	re.ipRouteInfo.re_ire_type  = ire->ire_type;
	re.ipRouteInfo.re_obpkt     = ire->ire_ob_pkt_count;
	re.ipRouteInfo.re_ibpkt     = ire->ire_ib_pkt_count;
	if (!snmp_append_data(mpdata[0], (char *)&re, sizeof(re))) {
		ip0dbg(("ip_snmp_get2: failed %d bytes\n", sizeof(re)));
	}

	if (ire->ire_type != IRE_ROUTE || ire->ire_gateway_addr)
		return;
	/*
	 * only IRE_ROUTE entries that are for a directly connected subnet
	 * get appended to net -> phys addr table 
	 * (others in arp)
	 */
	ntme.ipNetToMediaIfIndex.o_length = 0;
	ill = ire_to_ill(ire);
	if (ill) {
		ntme.ipNetToMediaIfIndex.o_length = ill->ill_name_length;
		bcopy(ill->ill_name, ntme.ipNetToMediaIfIndex.o_bytes, 
		      ntme.ipNetToMediaIfIndex.o_length);
	}
	ntme.ipNetToMediaPhysAddress.o_length = 0;
	if (llmp) {
		u_char	* addr;

		dlup = (dl_unitdata_req_t *)ALIGN32(llmp->b_rptr);
		/* Remove sap from  address */
		if (ill->ill_sap_length < 0) 
			addr = llmp->b_rptr + dlup->dl_dest_addr_offset;
		else
			addr = llmp->b_rptr + dlup->dl_dest_addr_offset +
				ill->ill_sap_length;
		
		ntme.ipNetToMediaPhysAddress.o_length = 
			MIN(OCTET_LENGTH, ill->ill_phys_addr_length);
		bcopy((char *)addr,
		      ntme.ipNetToMediaPhysAddress.o_bytes,
		      ntme.ipNetToMediaPhysAddress.o_length);
	}
	ntme.ipNetToMediaNetAddress = ire->ire_addr;
	/* assume dynamic (may be changed in arp) */
	ntme.ipNetToMediaType = 3;		
	ntme.ipNetToMediaInfo.ntm_mask.o_length = sizeof(u32);
	bcopy((char *)&ire->ire_mask, ntme.ipNetToMediaInfo.ntm_mask.o_bytes,
			ntme.ipNetToMediaInfo.ntm_mask.o_length);
	ntme.ipNetToMediaInfo.ntm_flags = ACE_F_RESOLVED;
	if (!snmp_append_data(mpdata[1], (char *)&ntme, sizeof(ntme))) {
		ip0dbg(("ip_snmp_get2: failed %d bytes\n", sizeof(ntme)));
	}
	return;
}

/* Return 0 if invalid set request, 1 otherwise, including non-tcp requests  */

/* ARGSUSED */
staticf	int
ip_snmp_set (q, level, name, ptr, len)
	queue_t	* q;
	int	level;
	int	name;
	u_char	* ptr;
	int	len;
{
	switch ( level ) {
	case MIB2_IP:
	case MIB2_ICMP:
		switch (name) {
		default:
			break;
		}
		return 1;
	default:
		return 1;
	}
}

/* 
 * Called before the options are updated to check if this packet will 
 * be source routed from here.
 * This routine assumes that the options are well formed i.e. that they
 * have already been checked.
 */
staticf boolean_t	
ip_source_routed(ipha)
	ipha_t	* ipha;
{
	u32	totallen;
	u_char	* opt;
	u32	optval;
	u32	optlen;
	u32	dst;

	ip2dbg(("ip_source_routed\n"));
	totallen = ipha->ipha_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	if (totallen == 0)
		return false;
	dst = ipha->ipha_dst;
	opt = (u_char *)&ipha[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			return false;
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}
		ip2dbg(("ip_source_routed: opt %d, len %d\n", 
			  optval, optlen));

		if (optlen == 0 || optlen > totallen)
			break;

		switch (optval) {
			u32 off;
		case IPOPT_SSRR:
		case IPOPT_LSRR: 
			/* If dst is one of our addresses and there are some
			 * entries left in the source route return true.
			 */
			if (!ire_lookup_myaddr(dst)) {
				ip2dbg(("ip_source_routed: not next source route 0x%x\n",
					(int)ntohl(dst)));
				return false;
			}
			off = opt[IPOPT_POS_OFF];
			off--;
			if (optlen < IP_ADDR_LEN ||
			    off > optlen - IP_ADDR_LEN) {
				/* End of source route */
				ip1dbg(("ip_source_routed: end of SR\n"));
				return false;
			}
			return true;
		}
		totallen -= optlen;
		opt += optlen;
	}
	return false;
}

/* 
 * Check if the packet contains any source route.
 * This routine assumes that the options are well formed i.e. that they
 * have already been checked.
 */
staticf boolean_t	
ip_source_route_included(ipha)
	ipha_t	* ipha;
{
	u32	totallen;
	u_char	* opt;
	u32	optval;
	u32	optlen;

	totallen = ipha->ipha_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	if (totallen == 0)
		return false;
	opt = (u_char *)&ipha[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			return false;
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}

		if (optlen == 0 || optlen > totallen)
			break;

		switch (optval) {
		case IPOPT_SSRR:
		case IPOPT_LSRR: 
			return true;
		}
		totallen -= optlen;
		opt += optlen;
	}
	return false;
}

#ifdef	MI_HRTIMING
/*
 * ip_time_flush is called as writer to flush any timer accumulations out of
 * the active instances and into the global accumulators wher they can be
 * accessed by reader threads for reporting.
 */
staticf	int
ip_time_flush (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	ipc_t	* ipc;
	
	for ( ipc = (ipc_t *)mi_first_ptr(&ip_g_head)
	; ipc
	; ipc = (ipc_t *)mi_next_ptr(&ip_g_head, (IDP)ipc) ) {
		/* Read times are in ill's and write times are in ipc's. */
		if ( ipc->ipc_wq->q_next ) {
			ill_t	* ill = (ill_t *)ipc;
			MI_HRT_ACCUMULATE(ip_g_rtime, ill->ill_rtime);
			MI_HRT_CLEAR(ill->ill_rtime);
		} else {
			MI_HRT_ACCUMULATE(ip_g_wtime, ipc->ipc_wtime);
			MI_HRT_CLEAR(ipc->ipc_wtime);
		}
	}
	return 0;
}

/* Report rput and wput average times. */
staticf	int
ip_time_report (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	long	ops;
	long	tot;
	long	usecs;
	
	/* Report the average times per operation, in microseconds. */
	MI_HRT_TO_USECS(ip_g_rtime, usecs);
	ops = MI_HRT_OPS(ip_g_rtime);
	tot = ops * usecs;
	mi_mpprintf(mp, "ip rput:  %d operations, avg %d usecs.", ops, usecs);
	MI_HRT_TO_USECS(ip_g_wtime, usecs);
	ops = MI_HRT_OPS(ip_g_wtime);
	tot += ops * usecs;
	mi_mpprintf(mp, "ip wput:  %d operations, avg %d usecs.", ops, usecs);
	
	/* Report the total time spent in IP, in milliseconds. */
	mi_mpprintf(mp, "(ip total time: %d msecs.)", tot / 1000);
	
	/*
	 * Report the elapsed time attributable to timer operations.  This
	 * overhead is NOT included in the averages and total reported above.
	 */
	mi_mpprintf(mp, "(ip timer overhead: %d msecs.)",
		(MI_HRT_OHD(ip_g_rtime) + MI_HRT_OHD(ip_g_wtime)) / 1000);

	return 0;
}

/* Reset rput and wput timers. */
staticf	int
ip_time_reset (q, mp, cp)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	cp;
{
	MI_HRT_CLEAR(ip_g_rtime);
	MI_HRT_CLEAR(ip_g_wtime);
	return 0;
}
#endif

/* Called as writer from ip_rsrv when the IRE expiration timer fires. */
/* ARGSUSED */
staticf	void
ip_trash (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	int	flush_flag = 0;
	
	if ( ip_ire_time_elapsed >= ip_ire_flush_interval ) {
		/* Nuke 'em all this time.  We're compliant kind of guys. */
		flush_flag |= 0x1;
		ip_ire_time_elapsed = 0;
	}
	if ( ip_ire_rd_time_elapsed >= ip_ire_redir_interval ) {
		/* Nuke 'em all this time.  We're compliant kind of guys. */
		flush_flag |= 0x2;
		ip_ire_rd_time_elapsed = 0;
	}
	if ( ip_ire_pmtu_time_elapsed >= ip_ire_pathmtu_interval ) {
		/* Increase path mtu */
		flush_flag |= 0x4;
		ip_ire_pmtu_time_elapsed = 0;
	}
	/*
	 * Walk all IRE's and nuke the ones that haven't been used since the
	 * previous interval.
	 */
	ire_walk(ire_expire, (char *)flush_flag);
	if (ip_timer_ill) {
		ip_ire_time_elapsed += ip_timer_interval;
		ip_ire_rd_time_elapsed += ip_timer_interval;
		ip_ire_pmtu_time_elapsed += ip_timer_interval;
		mi_timer(ip_timer_ill->ill_rq, ip_timer_mp, ip_timer_interval);
	}
}

/*
 * ip_unbind is called when a copy of an unbind request is received from the
 * upper level protocol.  We remove this ipc from any fanout hash list it is
 * on, and zero out the bind information.  No reply is expected up above.
 * (Always called as writer.)
 */
staticf void
ip_unbind (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
reg	ipc_t	* ipc = (ipc_t *)q->q_ptr;

	ipc_hash_remove(ipc);
	bzero((char *)&ipc->ipc_ipcu, sizeof(ipc->ipc_ipcu));

	/* Send a T_OK_ACK to the user */
	if ((mp = mi_tpi_ok_ack_alloc(mp)) != NULL)
		qreply(q, mp);
}

/*
 * Write side put procedure.  Outbound data, IOCTLs, responses from
 * resolvers, etc, come down through here.
 */
void
ip_wput(q, mp)
	queue_t	* q;
reg	mblk_t	* mp;
{
reg	ipha_t	* ipha;
#define	rptr	((u_char *)ipha)
reg	ire_t	* ire;
reg	queue_t	* stq;
	ipc_t	* ipc;
reg	u32	v_hlen_tos_len;
reg	u32	dst;

#ifdef	_BIG_ENDIAN
#define	V_HLEN	(v_hlen_tos_len >> 24)
#else
#define	V_HLEN	(v_hlen_tos_len & 0xFF)
#endif

	TRACE_1(TR_FAC_IP, TR_IP_WPUT_START,
		"ip_wput_start: q %X", q);

	/*
	 * ip_wput fast path
	 */

	/* is packet from ARP ? */
	if (q->q_next) goto qnext;
	ipc = (ipc_t *)q->q_ptr;

	MI_HRT_SET(ipc->ipc_wtmp);

	/* is queue flow controlled? */
	if (q->q_first && !ipc->ipc_draining) goto doputq;

	/* mblock type is not DATA */
	if (mp->b_datap->db_type != M_DATA) goto notdata;
	ipha = (ipha_t *)ALIGN32(mp->b_rptr);

	/* is IP header non-aligned or mblock smaller than basic IP header */
#ifndef SAFETY_BEFORE_SPEED
	if (!OK_32PTR(rptr) ||
	    (mp->b_wptr - rptr) < IP_SIMPLE_HDR_LENGTH) goto hdrtoosmall;
#endif
	v_hlen_tos_len = ((u32 *)ipha)[0];

	/* is wrong version or IP options present */
	if (V_HLEN != IP_SIMPLE_HDR_VERSION) goto version_hdrlen_check;
	dst = ipha->ipha_dst;

	/* is packet multicast? */
	if (CLASSD(dst)) goto multicast;

	/* bypass routing checks and go directly to interface */
	if (ipc->ipc_dontroute) goto dontroute;

	/* Try to get an IRE for dst.  Loop until an exact match. */
	ire = (ire_t *)&ire_hash_tbl[IRE_ADDR_HASH(dst)];
	do {
		ire = ire->ire_next;
		if (!ire) goto noirefound;
	} while (ire->ire_addr != dst);

	TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
		"ip_wput_end: q %X (%S)", q, "end");

	ip_wput_ire(q, mp, ire);
	return;

doputq:
	(void) putq(q, mp);
	return;

qnext:
	ipc = nilp(ipc_t);

	/*
	 * Upper Level Protocols pass down complete IP datagrams
	 * as M_DATA messages.  Everything else is a sideshow.
	 */
	if (mp->b_datap->db_type != M_DATA) {
notdata:
		ip_wput_nondata(q, mp);
#ifdef MI_HRTIMING
		if (ipc)
			MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
		TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
			"ip_wput_end: q %X (%S)", q, "nondata");
		return;
	}
	/* We have a complete IP datagram heading outbound. */
	ipha = (ipha_t *)ALIGN32(mp->b_rptr);

#ifndef SPEED_BEFORE_SAFETY
	/*
	 * Make sure we have a full-word aligned message and that at least
	 * a simple IP header is accessible in the first message.  If not,
	 * try a pullup.
	 */
	if (!OK_32PTR(rptr) ||
	    (mp->b_wptr - rptr) < IP_SIMPLE_HDR_LENGTH) {
hdrtoosmall:
		if (!pullupmsg(mp, IP_SIMPLE_HDR_LENGTH)) {
			BUMP_MIB(ip_mib.ipOutDiscards);
			freemsg(mp);
#ifdef MI_HRTIMING
			if (ipc)
				MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
			TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
				"ip_wput_end: q %X (%S)", q, "pullupfailed");
			return;
		}
		ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	}
#endif

	/* Most of the code below is written for speed, not readability */
	v_hlen_tos_len = ((u32 *)ipha)[0];

	/*
	 * If ip_newroute() fails, we're going to need a full
	 * header for the icmp wraparound.
	 */
	if (V_HLEN != IP_SIMPLE_HDR_VERSION) {
		u_int	v_hlen;
version_hdrlen_check:
		v_hlen = V_HLEN;
		if ((v_hlen >> 4) != IP_VERSION) {
			BUMP_MIB(ip_mib.ipOutDiscards);
			freemsg(mp);
#ifdef MI_HRTIMING
			if (ipc)
				MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
			TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
				"ip_wput_end: q %X (%S)", q, "badvers");
			return;
		}
		/*
		 * Is the header length at least 20 bytes?
		 *
		 * Are there enough bytes accessible in the header?  If
		 * not, try a pullup.
		 */
		v_hlen &= 0xF;
		v_hlen <<= 2;
		if (v_hlen < IP_SIMPLE_HDR_LENGTH) {
			BUMP_MIB(ip_mib.ipOutDiscards);
			freemsg(mp);
#ifdef MI_HRTIMING
			if (ipc)
				MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
			TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
				"ip_wput_end: q %X (%S)", q, "badlen");
			return;
		}
		if (v_hlen > (mp->b_wptr - rptr)) {
			if (!pullupmsg(mp, v_hlen)) {
				BUMP_MIB(ip_mib.ipOutDiscards);
				freemsg(mp);
#ifdef MI_HRTIMING
				if (ipc)
					MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
				    "ip_wput_end: q %X (%S)", q, "badpullup2");
				return;
			}
			ipha = (ipha_t *)ALIGN32(mp->b_rptr);
		}
		/*
		 * Move first entry from any source route into ipha_dst and
		 * verify the options
		 */
		if (ip_wput_options(q, mp, ipha)) {
#ifdef MI_HRTIMING
			if (ipc)
				MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
			TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
				"ip_wput_end: q %X (%S)", q, "badopts");
			return;
		}
	}
	dst = ipha->ipha_dst;

	/*
	 * Try to get an IRE_ROUTE for the destination address.  If we can't,
	 * we have to run the packet through ip_newroute which will take
	 * the appropriate action to arrange for an IRE_ROUTE, such as querying
	 * a resolver, or assigning a default gateway, etc.
	 */
	/* Guard against coming in from arp in which case ipc is nil. */
	if (CLASSD(dst) && ipc) {
		ipif_t	* ipif;
		u32	local_addr;

multicast:
		ip2dbg(("ip_wput: CLASSD\n"));
		ipif = ipc->ipc_multicast_ipif;
		if (!ipif) {
			/*
			 * We must do this check here else we could pass
			 * through ip_newroute and come back here without the
			 * ill context (we would have a lower ill).
			 *
			 * Note: we do late binding i.e. we bind to
			 * the interface when the first packet is sent.
			 * For performance reasons we do not rebind on
			 * each packet.
			 */
			ipif = ipif_lookup_group(htonl(INADDR_UNSPEC_GROUP));
			if (ipif == nilp(ipif_t)) {
				ip1dbg(("ip_wput: No ipif for multicast\n"));
				BUMP_MIB(ip_mib.ipOutNoRoutes);
				goto drop_pkt;
			}
			/* Bind until the next IP_MULTICAST_IF option */
			ipc->ipc_multicast_ipif = ipif;
		}
#ifdef IP_DEBUG
		else {
			ip2dbg(("ip_wput: multicast_ipif set: addr 0x%x\n",
				(int)ntohl(ipif->ipif_local_addr)));
		}
#endif
		ipha->ipha_ttl = ipc->ipc_multicast_ttl;
		ip2dbg(("ip_wput: multicast ttl %d\n", ipha->ipha_ttl));
		/*
		 * Find an IRE which matches the destination and the outgoing
		 * queue (i.e. the outgoing interface.)
		 */
		if (ipif->ipif_flags & IFF_POINTOPOINT)
			dst = ipif->ipif_pp_dst_addr;
		stq = ipif->ipif_ill->ill_wq;
		local_addr = ipif->ipif_local_addr;

		ire = (ire_t *)&ire_hash_tbl[IRE_ADDR_HASH(dst)];
		do {
			ire = ire->ire_next;
			if (!ire) {
				/*
				 * Have to check local loopback etc at this
				 * point since when we come back through
				 * ire_add_then_put we no longer have the ipc
				 * so we can't check ipc_loopback.
				 */
				ill_t *ill = ipif->ipif_ill;

				/* Set the source address for loopback */
				ipha->ipha_src = ipif->ipif_local_addr;

				/*
				 * Note that the loopback function will not come
				 * in through ip_rput - it will only do the
				 * client fanout thus we need to do an mforward
				 * as well.  The is different from the BSD
				 * logic.
				 */
				if (ipc->ipc_multicast_loop &&
				    ilm_lookup(ill, ipha->ipha_dst)) {
					/* Pass along the virtual output q. */
					ip_multicast_loopback(ill->ill_rq, mp);
				}
				if (ipha->ipha_ttl == 0) {
					/*
					 * 0 => only to this host i.e. we are
					 * done.
					 */
					freemsg(mp);
					MI_HRT_CLEAR(ipc->ipc_wtmp);
					TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
						"ip_wput_end: q %X (%S)",
						q, "loopback");
					return;
				}
				/*
				 * IFF_MULTICAST is checked in ip_newroute
				 * i.e. we don't need to check it here since
				 * all IRE_ROUTEs come from ip_newroute.
				 */
				if (ip_g_mrouter) {
					/*
					 * The checksum has not been
					 * computed yet
					 */
					ipha->ipha_hdr_checksum = 0;
					ipha->ipha_hdr_checksum =
						ip_csum_hdr(ipha);

					if (ip_mforward(ill, ipha, mp)) {
						freemsg(mp);
						ip1dbg(("ip_wput: mforward failed\n"));
						MI_HRT_CLEAR(ipc->ipc_wtmp);
						TRACE_2(TR_FAC_IP,
						    TR_IP_WPUT_END,
						    "ip_wput_end: q %X (%S)",
						    q, "mforward failed");
						return;
					}
				}
				/*
				 * Mark this packet to make it be delivered to
				 * ip_wput_ire after the new ire has been
				 * created.
				 */
				mp->b_prev = nilp(mblk_t);
				mp->b_next = nilp(mblk_t);
				ip_newroute_ipif(q, mp, ipif, dst);
				MI_HRT_CLEAR(ipc->ipc_wtmp);
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
					"ip_wput_end: q %X (%S)", q, "noire");
				return;
			}
		} while (ire->ire_addr != dst || ire->ire_stq != stq ||
		    ire->ire_src_addr != local_addr);
	} else {
		/*
		 * Guard against coming in from arp in which case ipc is
		 * nil.
		 */
		if (ipc && ipc->ipc_dontroute) {
dontroute:
			/*
			 * Set TTL to 1 if SO_DONTROUTE is set to prevent
			 * routing protocols from seeing false direct
			 * connectivity.
			 */
			ipha->ipha_ttl = 1;
		}
		/* Pun alert:  ire_next must be the first element of an ire. */
		ire = (ire_t *)&ire_hash_tbl[IRE_ADDR_HASH(dst)];
		do {
			ire = ire->ire_next;
			if (!ire) {
noirefound:
				/*
				 * Mark this packet as having originated on
				 * this machine.  This will be noted in
				 * ire_add_then_put, which needs to know
				 * whether to run it back through ip_wput or
				 * ip_rput following successful resolution.
				 */
				mp->b_prev = nilp(mblk_t);
				mp->b_next = nilp(mblk_t);
#ifdef MI_HRTIMING
				if (ipc)
					MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
				ip_newroute(q, mp, dst);
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
				    "ip_wput_end: q %X (%S)", q, "newroute");
				return;
			}
		} while (ire->ire_addr != dst);
	}
	/* We now know where we are going with it. */

	TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
		"ip_wput_end: q %X (%S)", q, "end");
	ip_wput_ire(q, mp, ire);
	return;

drop_pkt:
	ip1dbg(("ip_wput: dropped packet\n"));
	freemsg(mp);
	TRACE_2(TR_FAC_IP, TR_IP_WPUT_END,
		"ip_wput_end: q %X (%S)", q, "droppkt");
}

/* (May be called as writer.) */
void
ip_wput_ire(q, mp, ire)
	queue_t	* q;
reg	mblk_t	* mp;
reg	ire_t	* ire;
{
reg	ipha_t	* ipha;
#define	rptr	((u_char *)ipha)
reg	mblk_t	* mp1;
reg	queue_t	* stq;
	ipc_t	* ipc;
reg	u32	v_hlen_tos_len;
reg	u32	ttl_protocol;
reg	u32	src;
reg	u32	dst;
	u32	orig_src;
	ire_t	* ire1;
	mblk_t	* next_mp;
	u_int	hlen;
	u16	*up;
	u32	max_frag;

	TRACE_1(TR_FAC_IP, TR_IP_WPUT_IRE_START,
		"ip_wput_ire_start: q %X", q);
#ifdef	MI_HRTIMING
	ipc = q->q_next ? nilp(ipc_t) : (ipc_t *)q->q_ptr;
#endif

	/*
	 * Fast path for ip_wput_ire
	 */

	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	v_hlen_tos_len = ((u32 *)ipha)[0];
	dst = ipha->ipha_dst;

#ifdef IP_DEBUG
	if (CLASSD(dst)) {
		ip1dbg(("ip_wput_ire: to 0x%x ire %s addr 0x%x\n",
			(int)ntohl(dst),
			ip_nv_lookup(ire_nv_tbl, ire->ire_type),
			(int)ntohl(ire->ire_addr)));
	}
#endif

/* Macros to extract header fields from data already in registers */
#ifdef	_BIG_ENDIAN
#define	V_HLEN	(v_hlen_tos_len >> 24)
#define	LENGTH	(v_hlen_tos_len & 0xFFFF)
#define	PROTO	(ttl_protocol & 0xFF)
#else
#define	V_HLEN	(v_hlen_tos_len & 0xFF)
#define	LENGTH	((v_hlen_tos_len >> 24) | ((v_hlen_tos_len >> 8) & 0xFF00))
#define	PROTO	(ttl_protocol >> 8)
#endif


	orig_src = src = ipha->ipha_src;
	/* (The loop back to "another" is explained down below.) */
another:;
	/*
	 * Assign an ident value for this packet.  We assign idents on a
	 * per destination basis out of the IRE.  There could be other threads
	 * targeting the same destination, so we have to arrange for a atomic
	 * increment, using whatever mechanism is most efficient for a
	 * particular machine, therefore ATOMIC_32_INC should be defined in
	 * an environment specific header.
	 *
	 * Note: use ttl_protocol as a temporary
	 */
	mutex_enter(&ire->ire_ident_lock);
	ATOMIC_32_INC(ttl_protocol, &ire->ire_atomic_ident);
	mutex_exit(&ire->ire_ident_lock);
#ifndef _BIG_ENDIAN
	ttl_protocol = (ttl_protocol << 8) | ((ttl_protocol >> 8) & 0xff);
#endif
	ipha->ipha_ident = (u16)ttl_protocol;

	if (!src) {
		/*
		 * Assign the appropriate source address from the IRE
		 * if none was specified.
		 */
		src = ire->ire_src_addr;
		ipha->ipha_src = src;
	}
	stq = ire->ire_stq;

	ire1 = ire->ire_next;
	/*
	 * We only allow ire chains for broadcasts since there will
	 * be multiple IRE_ROUTE entries for the same multicast
	 * address (one per ipif).
	 */
	next_mp = nilp(mblk_t);

	/* broadcast packet */
	if (ire->ire_type == IRE_BROADCAST) goto broadcast;

	/* loopback ? */
	if (!stq) goto nullstq;

	BUMP_MIB(ip_mib.ipOutRequests);
	ttl_protocol = ((u16 *)ipha)[4];

	/* pseudo checksum (doit in parts for IP header checksum) */
	dst = (dst >> 16) + (dst & 0xFFFF);
	src = (src >> 16) + (src & 0xFFFF);
	dst += src;
#define	IPH_UDPH_CHECKSUMP(ipha, hlen)	\
		((u16 *) ALIGN16(((u_char *)ipha)+(hlen + 6)))
#define	IPH_TCPH_CHECKSUMP(ipha, hlen)	\
		((u16 *) ALIGN16(((u_char *)ipha)+(hlen + 16)))
	if (PROTO != IPPROTO_TCP) {
		queue_t * dev_q = stq->q_next;
		/* flow controlled */
		if ((dev_q->q_next || dev_q->q_first) &&
			!canput(dev_q)) goto blocked;
		if (PROTO == IPPROTO_UDP) {
			hlen = (V_HLEN & 0xF) << 2;
			up = IPH_UDPH_CHECKSUMP(ipha, hlen);
			if (*up) {
				u_int   u1;
				u1 = IP_CSUM(mp, hlen,
					dst+IP_UDP_CSUM_COMP);
				*up = (u16)(u1 ? u1 : ~u1);
			}
		}
	} else {
		hlen = (V_HLEN & 0xF) << 2;
		up = IPH_TCPH_CHECKSUMP(ipha, hlen);
		*up = IP_CSUM(mp, hlen, dst + IP_TCP_CSUM_COMP);
	}

	/* multicast packet? */
	if (CLASSD(ipha->ipha_dst) && !q->q_next) goto multi_loopback;

	/* checksum */
	dst += ttl_protocol;

	max_frag = ire->ire_max_frag;
	/* fragment the packet */
	if (max_frag < (unsigned int)LENGTH) goto fragmentit;

	/* checksum */
	dst += ipha->ipha_ident;

	if ((V_HLEN == IP_SIMPLE_HDR_VERSION ||
	    !ip_source_route_included(ipha)) &&
	    !CLASSD(ipha->ipha_dst))
		ipha->ipha_fragment_offset_and_flags |=
				htons(ire->ire_frag_flag);
	/* checksum */
	dst += (v_hlen_tos_len >> 16)+(v_hlen_tos_len & 0xFFFF);
	dst += ipha->ipha_fragment_offset_and_flags;

	/* IP options present */
	hlen = (V_HLEN & 0xF) - IP_SIMPLE_HDR_LENGTH_IN_WORDS;
	if (hlen) goto checksumoptions;

	/* calculate hdr checksum */
	dst = ((dst & 0xFFFF) + (dst >> 16));
	dst = ~(dst + (dst >> 16));
	{
		u32	u1 = dst;
		ipha->ipha_hdr_checksum = u1;
	}

	hlen = ire->ire_ll_hdr_length;
	mp1 = ire->ire_ll_hdr_mp;

	/* if not MDATA fast path or fp hdr doesn'f fit */
	if (!hlen || (rptr - mp->b_datap->db_base) < hlen) goto noprepend;

	ipha = (ipha_t *)(rptr - hlen);
	mp->b_rptr = rptr;
	bcopy((char *)mp1->b_rptr, (char *)rptr, hlen);
	ire->ire_ob_pkt_count++;

#ifdef MI_HRTIMING
	if (ipc && MI_HRT_IS_SET(ipc->ipc_wtmp)) {
		MI_HRT_INCREMENT(ipc->ipc_wtime, ipc->ipc_wtmp, 1);
		MI_HRT_CLEAR(ipc->ipc_wtmp);
	}
#endif /* MI_HRTIMING */
	TRACE_2(TR_FAC_IP, TR_IP_WPUT_IRE_END,
		"ip_wput_ire_end: q %X (%S)",
		q, "last copy out");
	putnext(stq, mp);
	return;

	/*
	 * ire->ire_type == IRE_BROADCAST (minimize diffs)
	 */
broadcast:
	{
		/*
		 * Avoid broadcast storms by setting the ttl to 1
		 * for broadcasts
		 */
		ipha->ipha_ttl = ip_broadcast_ttl;

		if (ire1 && ire1->ire_addr == dst) {
			/*
			 * IRE chain.  The next IRE has the same
			 * address as the current one.  We make a copy
			 * for the next interface now, before we
			 * change anything.  This feature is currently
			 * used to get broadcasts sent to multiple
			 * interfaces, when the broadcast address
			 * being used applies to multiple interfaces.
			 * For example, a whole net broadcast will be
			 * replicated on every connected subnet of
			 * the target net.
			 */
			next_mp = copymsg(mp);
		}
	}

	if (stq) {
		/*
		 * A non-nil send-to queue means this packet is going
		 * out of this machine.
		 */

		BUMP_MIB(ip_mib.ipOutRequests);
		ttl_protocol = ((u16 *)ipha)[4];
		/*
		 * We accumulate the pseudo header checksum in dst.
		 * This is pretty hairy code, so watch close.  One
		 * thing to keep in mind is that UDP and TCP have
		 * stored their respective datagram lengths in their
		 * checksum fields.  This lines things up real nice.
		 */
		dst = (dst >> 16) + (dst & 0xFFFF);
		src = (src >> 16) + (src & 0xFFFF);
		dst += src;
		/*
		 * We assume the udp checksum field contains the
		 * length, so to compute the pseudo header checksum,
		 * all we need is the protocol number and src/dst.
		 */
		/* Provide the checksums for UDP and TCP. */
		if (PROTO == IPPROTO_TCP) {
			/* hlen gets the number of u_chars in the IP header */
			hlen = (V_HLEN & 0xF) << 2;
			up = IPH_TCPH_CHECKSUMP(ipha, hlen);
			*up = IP_CSUM(mp, hlen, dst + IP_TCP_CSUM_COMP);
		} else {
			queue_t * dev_q = stq->q_next;
			if ((dev_q->q_next || dev_q->q_first) &&
			    !canput(dev_q)) {
blocked:
				if (next_mp)
					freemsg(next_mp);
				ipc = q->q_next ? nilp(ipc_t) :
					(ipc_t *)q->q_ptr;
				if (ip_output_queue && ipc != NULL) {
					if (ipc->ipc_draining) {
						ipc->ipc_did_putbq = 1;
						(void) putbq(ipc->ipc_wq, mp);
					} else
						(void) putq(ipc->ipc_wq, mp);
					return;
				}
				BUMP_MIB(ip_mib.ipOutDiscards);
#ifdef MI_HRTIMING
				if (ipc)
					MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
				if (ip_hdr_complete(ipha))
					freemsg(mp);
				else
					icmp_source_quench(q, mp);
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_IRE_END,
					"ip_wput_ire_end: q %X (%S)",
					q, "discard");
				return;
			}
			if (PROTO == IPPROTO_UDP) {
			/* hlen gets the number of u_chars in the IP header */
				hlen = (V_HLEN & 0xF) << 2;
				up = IPH_UDPH_CHECKSUMP(ipha, hlen);
				if (*up) {
					u_int	u1;
				/* NOTE: watch out for compiler high bits */
					u1 = IP_CSUM(mp, hlen,
						dst+IP_UDP_CSUM_COMP);
					*up = (u16)(u1 ? u1 : ~u1);
				}
			}
		}
		/*
		 * Need to do this even when fragmenting. The local
		 * loopback can be done without compututing checksums
		 * but forwarding out other interface must be done
		 * after the IP checksum (and ULP checksums) have been
		 * computed.
		 */
		if (CLASSD(ipha->ipha_dst) && !q->q_next) {
			ill_t	* ill;
multi_loopback:
			ill = nilp(ill_t);
			ipc = (ipc_t *)q->q_ptr;
			/*
			 * Local loopback of multicasts?  Check the
			 * ill.
			 */
			ip2dbg(("ip_wput: multicast, loop %d\n",
			    ipc->ipc_multicast_loop));
			/*
			 * Note that the loopback function will not come
			 * in through ip_rput - it will only do the
			 * client fanout thus we need to do an mforward
			 * as well.  The is different from the BSD
			 * logic.
			 */
			if (ipc->ipc_multicast_loop
			    &&  (ill = ire_to_ill(ire))
			    &&   ilm_lookup(ill, ipha->ipha_dst)) {
				/* Pass along the virtual output q. */
				ip_multicast_loopback(ill->ill_rq, mp);
			}
			if (ipha->ipha_ttl == 0) {
				/*
				 * 0 => only to this host i.e. we are
				 * done.
				 */
				freemsg(mp);
				MI_HRT_CLEAR(ipc->ipc_wtmp);
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_IRE_END,
					"ip_wput_ire_end: q %X (%S)",
					q, "loopback");
				return;
			}
			/*
			 * IFF_MULTICAST is checked in ip_newroute
			 * i.e. we don't need to check it here since
			 * all IRE_ROUTEs come from ip_newroute.
			 */
			if ( ip_g_mrouter
			    &&  (ill  ||  (ill = ire_to_ill(ire)))) {
				/* The checksum has not been computed yet */
				ipha->ipha_hdr_checksum = 0;
				ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);

				if (ip_mforward(ill, ipha, mp) ) {
					freemsg(mp);
					ip1dbg(("ip_wput: mforward failed\n"));
					MI_HRT_CLEAR(ipc->ipc_wtmp);
					TRACE_2(TR_FAC_IP, TR_IP_WPUT_IRE_END,
						"ip_wput_ire_end: q %X (%S)",
						q, "mforward failed");
					return;
				}
			}
		}

		max_frag = ire->ire_max_frag;
		dst += ttl_protocol;
		if (max_frag >= (unsigned int)LENGTH) {
			/* No fragmentation required for this one. */
			/* Complete the IP header checksum. */
			dst += ipha->ipha_ident;
			/*
			 * Don't use frag_flag if source routed or if
			 * multicast (since multicast packets do not solicit
			 * ICMP "packet too big" messages).
			 */
			if ((V_HLEN == IP_SIMPLE_HDR_VERSION ||
			     !ip_source_route_included(ipha)) &&
			    !CLASSD(ipha->ipha_dst))
				ipha->ipha_fragment_offset_and_flags |= 
					htons(ire->ire_frag_flag);

			dst += (v_hlen_tos_len >> 16)+(v_hlen_tos_len & 0xFFFF);
			dst += ipha->ipha_fragment_offset_and_flags;
			hlen = (V_HLEN & 0xF) - IP_SIMPLE_HDR_LENGTH_IN_WORDS;
			if ( hlen ) {
checksumoptions:
				/*
				 * Account for the IP Options in the IP
				 * header checksum.
				 */
				up = (u16 *)ALIGN16(rptr+IP_SIMPLE_HDR_LENGTH);
				do {
					dst += up[0];
					dst += up[1];
					up += 2;
				} while (--hlen);
			}
			dst = ((dst & 0xFFFF) + (dst >> 16));
			dst = ~(dst + (dst >> 16));
			/* Help some compilers treat dst as a register */
			{
				u32	u1 = dst;
				ipha->ipha_hdr_checksum = u1;
			}

			hlen = ire->ire_ll_hdr_length;
			mp1 = ire->ire_ll_hdr_mp;
			/*
			 * If the driver accepts M_DATA prepends
			 * and we have enough room to lay it in ...
			 */
			if (hlen  &&  (rptr - mp->b_datap->db_base) >= hlen) {
				/* XXX ipha is not aligned here */
				ipha = (ipha_t *)(rptr - hlen);
				mp->b_rptr = rptr;
				/* TODO: inline this small copy */
				bcopy((char *)mp1->b_rptr, (char *)rptr, hlen);
				mp1 = mp;
			} else {
noprepend:
				mp1 = copyb(mp1);
				if (!mp1) {
					BUMP_MIB(ip_mib.ipOutDiscards);
					if (next_mp)
						freemsg(next_mp);
#ifdef MI_HRTIMING
					if ( ipc )
						MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
					if (ip_hdr_complete(ipha))
						freemsg(mp);
					else
						icmp_source_quench(q, mp);
					TRACE_2(TR_FAC_IP, TR_IP_WPUT_IRE_END,
						"ip_wput_ire_end: q %X (%S)",
						q, "discard MDATA");
					return;
				}
				mp1->b_cont = mp;
			}
			/* TODO: make this atomic */
			ire->ire_ob_pkt_count++;
			if (!next_mp) {
				/*
				 * Last copy going out (the ultra-common
				 * case).  Note that we intentionally replicate
				 * the putnext rather than calling it before
				 * the next_mp check in hopes of a little
				 * tail-call action out of the compiler.
				 */
#ifdef MI_HRTIMING
				if (ipc && MI_HRT_IS_SET(ipc->ipc_wtmp)) {
					MI_HRT_INCREMENT(ipc->ipc_wtime,
						ipc->ipc_wtmp, 1);
					MI_HRT_CLEAR(ipc->ipc_wtmp);
				}
#endif /* MI_HRTIMING */
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_IRE_END,
					"ip_wput_ire_end: q %X (%S)",
					q, "last copy out(1)");
				putnext(stq, mp1);
				return;
			}
			/* More copies going out below. */
#ifdef MI_HRTIMING
			if (ipc)
				MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
			putnext(stq, mp1);
		} else {
fragmentit:
#ifdef MI_HRTIMING
			if ( ipc )
				MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
			/* Pass it off to ip_wput_frag to chew up */
#ifndef SPEED_BEFORE_SAFETY
			/* Check that ipha_length is consistent with
			 * the mblk length */
			if (LENGTH != (mp->b_cont ? msgdsize(mp) : 
				       mp->b_wptr - rptr)) {
				ip0dbg(("Packet length mismatch: %d, %d\n",
					 LENGTH, msgdsize(mp)));
				freemsg(mp);
				freemsg(next_mp);
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_IRE_END,
					"ip_wput_ire_end: q %X (%S)", q,
					"packet length mismatch");
				return;
			}
#endif
			/*
			 * Don't use frag_flag if source routed or if
			 * multicast (since multicast packets do not solicit
			 * ICMP "packet too big" messages).
			 */
			if ((V_HLEN != IP_SIMPLE_HDR_VERSION &&
			     ip_source_route_included(ipha)) ||
			    CLASSD(ipha->ipha_dst)) {
				if (!next_mp) {
					TRACE_2(TR_FAC_IP, TR_IP_WPUT_IRE_END,
						"ip_wput_ire_end: q %X (%S)",
						q, "source routed");
					ip_wput_frag(ire, mp, 
						     &ire->ire_ob_pkt_count,
						     max_frag, 0);
					return;
				}
				ip_wput_frag(ire, mp, &ire->ire_ob_pkt_count,
					     max_frag, 0);
			} else {
				if (!next_mp) {
					TRACE_2(TR_FAC_IP, TR_IP_WPUT_IRE_END,
						"ip_wput_ire_end: q %X (%S)",
						q, "not source routed");
					ip_wput_frag(ire, mp, 
						     &ire->ire_ob_pkt_count,
						     max_frag,
						     ire->ire_frag_flag);
					return;
				}
				ip_wput_frag(ire, mp, &ire->ire_ob_pkt_count,
					     max_frag,
					     ire->ire_frag_flag);
			}
		}
	} else {
nullstq:
		/* A nil stq means the destination address is local. */
		/* TODO: make this atomic */
		ire->ire_ob_pkt_count++;
		if (!next_mp) {
			TRACE_2(TR_FAC_IP, TR_IP_WPUT_IRE_END,
				"ip_wput_ire_end: q %X (%S)",
				q, "local address");
			ip_wput_local(RD(q), ipha, mp, ire->ire_type,
				ire->ire_rfq);
			return;
		}
#ifdef MI_HRTIMING
		if ( ipc )
			MI_HRT_CLEAR(ipc->ipc_wtmp);
#endif /* MI_HRTIMING */
		ip_wput_local(RD(q), ipha, mp, ire->ire_type, ire->ire_rfq);
	}

	/* More copies going out to additional interfaces. */
	ire = ire->ire_next;
	mp = next_mp;
	dst = ire->ire_addr;
	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	/* Restore src so that we will pick up ire->ire_src_addr if src was 0 */
	src = orig_src;
	goto another;

#undef	rptr
}

/* Outbound IP fragmentation routine.  (May be called as writer.) */
staticf void
ip_wput_frag (ire, mp_orig, pkt_count, max_frag, frag_flag)
	ire_t	* ire;
	mblk_t	* mp_orig;
	u_long	* pkt_count;
	u32	max_frag;
	u32	frag_flag;
{
	int	i1;
	mblk_t	* ll_hdr_mp;
	int	hdr_len;
	mblk_t	* hdr_mp;
reg	ipha_t	* ipha;
	int	ip_data_end;
	int	len;
reg	mblk_t	* mp = mp_orig;
	int	offset;
	queue_t	* q;
	u32	v_hlen_tos_len;
	
	TRACE_0(TR_FAC_IP, TR_IP_WPUT_FRAG_START,
		"ip_wput_frag_start:");

	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	
	/*
	 * If the Don't Fragment flag is on, generate an ICMP destination
	 * unreachable, fragmentation needed.
	 */
	offset = ntohs(ipha->ipha_fragment_offset_and_flags);
	if (offset & IPH_DF) {
		BUMP_MIB(ip_mib.ipFragFails);
		icmp_frag_needed(ire->ire_stq, mp, max_frag);
		TRACE_1(TR_FAC_IP, TR_IP_WPUT_FRAG_END,
			"ip_wput_frag_end:(%S)",
			"don't fragment");
		return;
	}
	/*
	 * Establish the starting offset.  May not be zero if we are fragging
	 * a fragment that is being forwarded.
	 */
	offset = offset & IPH_OFFSET;
	
	/* TODO why is this test needed? */
	v_hlen_tos_len = ((u32 *)ipha)[0];
	if ( ((max_frag - LENGTH) & ~7) < 8 ) {
		/* TODO: notify ulp somehow */
		BUMP_MIB(ip_mib.ipFragFails);
		freemsg(mp);
		TRACE_1(TR_FAC_IP, TR_IP_WPUT_FRAG_END,
			"ip_wput_frag_end:(%S)",
			"len < 8");
		return;
	}

	hdr_len = (V_HLEN & 0xF) << 2;
	ipha->ipha_hdr_checksum = 0;
	
	/* Get a copy of the header for the trailing frags */
	hdr_mp = ip_wput_frag_copyhdr((u_char *)ipha, hdr_len, offset);
	if ( !hdr_mp ) {
		BUMP_MIB(ip_mib.ipOutDiscards);
		freemsg(mp);
		TRACE_1(TR_FAC_IP, TR_IP_WPUT_FRAG_END,
			"ip_wput_frag_end:(%S)",
			"couldn't copy hdr");
		return;
	}
	
	/* Store the starting offset, with the MoreFrags flag. */
	i1 = offset | IPH_MF | frag_flag;
	ipha->ipha_fragment_offset_and_flags = htons(i1);
	
	/* Establish the ending byte offset, based on the starting offset. */
	offset <<= 3;
	ip_data_end = offset + ntohs(ipha->ipha_length) - hdr_len;
	
	/*
	 * Establish the number of bytes maximum per frag, after putting
	 * in the header.
	 */
	len = (max_frag - hdr_len) & ~7;

	/* Store the length of the first fragment in the IP header. */
	i1 = len + hdr_len;
	ipha->ipha_length = htons(i1);
	
	/*
	 * Compute the IP header checksum for the first frag.  We have to
	 * watch out that we stop at the end of the header.
	 */
	ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);
	
	/*
	 * Now carve off the first frag.  Note that this will include the
	 * original IP header.
	 */
	if (!(mp = ip_carve_mp(&mp_orig, i1))) {
		BUMP_MIB(ip_mib.ipOutDiscards);
		freeb(hdr_mp);
		freemsg(mp_orig);
		TRACE_1(TR_FAC_IP, TR_IP_WPUT_FRAG_END,
			"ip_wput_frag_end:(%S)",
			"couldn't carve first");
		return;
	}
	ll_hdr_mp = ire->ire_ll_hdr_mp;

	/* If there is a transmit header, get a copy for this frag. */
	/* TODO: should check db_ref before calling ip_carve_mp since
	 * it might give us a dup.
	 */
	if (!ll_hdr_mp) {
		/* No xmit header. */
		ll_hdr_mp = mp;
	} else if (mp->b_datap->db_ref == 1 &&
		   ire->ire_ll_hdr_length &&
		   ll_hdr_mp->b_wptr - ll_hdr_mp->b_rptr < 
		   mp->b_rptr - mp->b_datap->db_base) {
		/* M_DATA fastpath */
		mp->b_rptr -= ll_hdr_mp->b_wptr - ll_hdr_mp->b_rptr;
		bcopy((char *)ll_hdr_mp->b_rptr, (char *)mp->b_rptr,
		      ire->ire_ll_hdr_length);
		ll_hdr_mp = mp;
	} else if (!(ll_hdr_mp = copyb(ll_hdr_mp))) {
		BUMP_MIB(ip_mib.ipOutDiscards);
		freeb(hdr_mp);
		freemsg(mp);
		freemsg(mp_orig);
		TRACE_1(TR_FAC_IP, TR_IP_WPUT_FRAG_END,
			"ip_wput_frag_end:(%S)",
			"discard");
		return;
	} else
		ll_hdr_mp->b_cont = mp;

	q = ire->ire_stq;
	BUMP_MIB(ip_mib.ipFragCreates);
	putnext(q, ll_hdr_mp);
	pkt_count[0]++;

	/* Advance the offset to the second frag starting point. */
	offset += len;
	/* 
	 * Update hdr_len from the copied header - there might be less options
	 * in the later fragments.
	 */
	hdr_len = (hdr_mp->b_rptr[0] & 0xF) << 2;
	/* Loop until done. */
	for (;;) {
		u16	u1;
		
		if ( ip_data_end - offset > len ) {
			/*
			 * Carve off the appropriate amount from the original
			 * datagram.
			 */
			if (!(ll_hdr_mp = ip_carve_mp(&mp_orig, len))) {
				mp = NULL;
				break;
			}
			/*
			 * More frags after this one.  Get another copy
			 * of the header.
			 */
			if (ll_hdr_mp->b_datap->db_ref == 1 &&
			    hdr_mp->b_wptr - hdr_mp->b_rptr < 
			    ll_hdr_mp->b_rptr - ll_hdr_mp->b_datap->db_base) {
				/* Inline IP header */
				ll_hdr_mp->b_rptr -= hdr_mp->b_wptr - 
					hdr_mp->b_rptr;
				bcopy((char *)hdr_mp->b_rptr, 
				      (char *)ll_hdr_mp->b_rptr,
				      hdr_mp->b_wptr - hdr_mp->b_rptr);
				mp = ll_hdr_mp;
			} else {
				if (!(mp = copyb(hdr_mp))) {
					freemsg(ll_hdr_mp);
					break;
				}
				mp->b_cont = ll_hdr_mp;
			}
			ipha = (ipha_t *)ALIGN32(mp->b_rptr);
			u1 = IPH_MF;
		} else {
			/*
			 * Last frag.  Consume the header. Set len to
			 * the length of this last piece.
			 */
			len = ip_data_end - offset;

			/*
			 * Carve off the appropriate amount from the original
			 * datagram.
			 */
			if (!(ll_hdr_mp = ip_carve_mp(&mp_orig, len))) {
				mp = NULL;
				break;
			}
			if (ll_hdr_mp->b_datap->db_ref == 1 && 
			    hdr_mp->b_wptr - hdr_mp->b_rptr < 
			    ll_hdr_mp->b_rptr - ll_hdr_mp->b_datap->db_base) {
				/* Inline IP header */
				ll_hdr_mp->b_rptr -= hdr_mp->b_wptr - 
					hdr_mp->b_rptr;
				bcopy((char *)hdr_mp->b_rptr, 
				      (char *)ll_hdr_mp->b_rptr,
				      hdr_mp->b_wptr - hdr_mp->b_rptr);
				mp = ll_hdr_mp;
				freeb(hdr_mp);
				hdr_mp = mp;
			} else {
				mp = hdr_mp;
				mp->b_cont = ll_hdr_mp;
			}
			ipha = (ipha_t *)ALIGN32(mp->b_rptr);
			/* A frag of a frag might have IPH_MF non-zero */
			u1 = ntohs(ipha->ipha_fragment_offset_and_flags) &
				IPH_MF;
		}
		u1 |= (offset >> 3);
		u1 |= frag_flag;
		/* Store the offset and flags in the IP header. */
		ipha->ipha_fragment_offset_and_flags = htons(u1);
		
		/* Store the length in the IP header. */
		u1 = len + hdr_len;
		ipha->ipha_length = htons(u1);
		
		/*
		 * Set the IP header checksum.  Note that mp is just
		 * the header, so this is easy to pass to ip_csum.
		 */
		ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);

		/* Attach a transmit header, if any, and ship it. */
		ll_hdr_mp = ire->ire_ll_hdr_mp;
		pkt_count[0]++;
		if (!ll_hdr_mp) {
			ll_hdr_mp = mp;
		} else if (mp->b_datap->db_ref == 1 && 
			   ire->ire_ll_hdr_length &&
			   ll_hdr_mp->b_wptr - ll_hdr_mp->b_rptr < 
			   mp->b_rptr - mp->b_datap->db_base) {
			/* M_DATA fastpath */
			mp->b_rptr -= ll_hdr_mp->b_wptr - ll_hdr_mp->b_rptr;
			bcopy((char *)ll_hdr_mp->b_rptr, (char *)mp->b_rptr,
			      ire->ire_ll_hdr_length);
			ll_hdr_mp = mp;
		} else if (ll_hdr_mp = copyb(ll_hdr_mp)) {
			ll_hdr_mp->b_cont = mp;
		} else
			break;
		BUMP_MIB(ip_mib.ipFragCreates);
		putnext(q, ll_hdr_mp);
			
		/* All done if we just consumed the hdr_mp. */
		if (mp == hdr_mp) {
			BUMP_MIB(ip_mib.ipFragOKs);
			TRACE_1(TR_FAC_IP, TR_IP_WPUT_FRAG_END,
				"ip_wput_frag_end:(%S)",
				"consumed hdr_mp");
			return;
		}
		/* Otherwise, advance and loop. */
		offset += len;
	}

	/* Clean up following allocation failure. */
	BUMP_MIB(ip_mib.ipOutDiscards);
	freemsg(mp);
	if ( mp != hdr_mp )
		freeb(hdr_mp);
	if ( mp != mp_orig )
		freemsg(mp_orig);
	TRACE_1(TR_FAC_IP, TR_IP_WPUT_FRAG_END,
		"ip_wput_frag_end:(%S)",
		"end--alloc failure");
}

/*
 * Copy the header plus those options which have the copy bit set
 */
staticf mblk_t *
ip_wput_frag_copyhdr (rptr, hdr_len, offset)
	u_char	* rptr;
	int 	hdr_len;
	int	offset;
{
	mblk_t	* mp;
	u_char	* up;

	/* Quick check if we need to look for options without the copy bit 
	 * set
	 */
	mp = allocb(ip_wroff_extra + hdr_len, BPRI_HI);
	if (!mp)
		return mp;
	mp->b_rptr += ip_wroff_extra;
	if (hdr_len == IP_SIMPLE_HDR_LENGTH || offset != 0) {
		bcopy((char *)rptr, (char *)mp->b_rptr, hdr_len);
		mp->b_wptr += hdr_len + ip_wroff_extra;
		return mp;
	}
	up  = mp->b_rptr;
	bcopy((char *)rptr, (char *)up, IP_SIMPLE_HDR_LENGTH);
	up += IP_SIMPLE_HDR_LENGTH;
	rptr += IP_SIMPLE_HDR_LENGTH;
	hdr_len -= IP_SIMPLE_HDR_LENGTH;
	while (hdr_len > 0) {
		u32 optval;
		u32 optlen;

		optval = *rptr;
		if (optval == IPOPT_EOL)
			break;
		if (optval == IPOPT_NOP)
			optlen = 1;
		else
			optlen = rptr[1];
		if (optval & IPOPT_COPY) {
			bcopy((char *)rptr, (char *)up, optlen);
			up += optlen;
		}
		rptr += optlen;
		hdr_len -= optlen;
	}
	/* Make sure that we drop an even number of words by filling
	 * with EOL to the next word boundary.
	 */
	for (hdr_len = up - (mp->b_rptr + IP_SIMPLE_HDR_LENGTH); 
	     hdr_len & 0x3; hdr_len++) 
		*up++ = IPOPT_EOL;
	mp->b_wptr = up;
	/* Update header length */
	mp->b_rptr[0] = (IP_VERSION << 4) | ((up - mp->b_rptr) >> 2);
	return mp;
}
	    
/*
 * Delivery to local recipients including fanout to multiple recipients.
 * Does not do checksumming of UDP/TCP.
 * Note: q should be the read side queue for either the ill or ipc.
 * Note: rq should be the read side q for the lower (ill) stream.
 * (May be called as writer.)
 */
void
ip_wput_local (q, ipha, mp, ire_type, rq)
	queue_t	* q;
	ipha_t	* ipha;
	mblk_t	* mp;
	int	ire_type;
	queue_t	* rq;
{
	u32	dst;
	ipc_t	* ipc;
#ifdef	IP_NEED_LOOP_PROTECTION
	ipc_t	* ipca;
#endif
	mblk_t	* mp1;
#ifdef MI_HRTIMING
	ipc_t	* oipc;
#endif /* MI_HRTIMING */
	u32	u1;

#define	rptr	((u_char *)ipha)

	TRACE_1(TR_FAC_IP, TR_IP_WPUT_LOCAL_START,
		"ip_wput_local_start: q %X", q);

	loopback_packets++;
#ifdef	MI_HRTIMING
	oipc = WR(q)->q_next ? nilp(ipc_t) : (ipc_t *)q->q_ptr;
	if ( oipc  &&  !MI_HRT_IS_SET(oipc->ipc_wtmp) )
		oipc = nilp(ipc_t);
#endif

	ip2dbg(("ip_wput_local: from 0x%x to 0x%x\n",
		(int)ntohl(ipha->ipha_src), (int)ntohl(ipha->ipha_dst)));
	if (!IS_SIMPLE_IPH(ipha)) {
		ip_wput_local_options(ipha);
	}
	u1 = ipha->ipha_protocol;
	dst = ipha->ipha_dst;
	switch (u1) {
	case IPPROTO_ICMP: 
	case IPPROTO_IGMP: {
		/* Figure out the the incoming logical interface */
		ipif_t	* ipif = nilp(ipif_t);

		if (ire_type == IRE_BROADCAST || rq == NULL) {
			u32	src;
			
			src = ipha->ipha_src;
			if (rq == NULL)
				ipif = ipif_lookup_addr(src);
			else
				ipif = ipif_lookup_remote((ill_t *)rq->q_ptr,
							  src);
			if (!ipif) {
				ip1dbg(("ip_wput_local: No source for broadcast/multicast\n"));
				freemsg(mp);
#ifdef MI_HRTIMING
				if ( oipc )
					MI_HRT_CLEAR(oipc->ipc_wtmp);
#endif /* MI_HRTIMING */
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
					"ip_wput_local_end: q %X (%S)",
					q, "no source for broadcast");
				return;
			}
			ip1dbg(("ip_wput_local:received %s:%d from 0x%x\n",
				ipif->ipif_ill->ill_name, (int)ipif->ipif_id,
				(int)ntohl(src)));
		}
		if (u1 == IPPROTO_ICMP) {
#ifdef MI_HRTIMING
			if ( oipc )
				MI_HRT_CLEAR(oipc->ipc_wtmp);
#endif /* MI_HRTIMING */
			if (rq == NULL)		/* For IRE_LOOPBACK */
				rq = q;
			icmp_inbound(rq, mp, ire_type, ipif, 0, 0);
			TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
				"ip_wput_local_end: q %X (%S)",
				q, "icmp");
			return;
		}
		if (igmp_input(q, mp, ipif)) {
			/* Bad packet - discarded by igmp_input */
#ifdef MI_HRTIMING
			if ( oipc )
				MI_HRT_CLEAR(oipc->ipc_wtmp);
#endif /* MI_HRTIMING */
			TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
				"ip_wput_local_end: q %X (%S)",
				q, "igmp_input--bad packet");

			return;
		}
		/* deliver to local raw users */
		break;
	}
	case IPPROTO_ENCAP:
		if (ip_g_mrouter) {
			mblk_t	*mp2;

			if (ipc_proto_fanout[IPPROTO_ENCAP] == nilp(ipc_t)) {
				ip_mroute_decap(q, mp);
				return;
			}
			mp2 = dupmsg(mp);
			if (mp2)
				ip_mroute_decap(q, mp2);
		}
		break;
	case IPPROTO_UDP: {
		u16	* up;

		up = (u16 *)ALIGN16(rptr + IPH_HDR_LENGTH(ipha));
		/* Force a 'valid' checksum. */
		up[3] = 0;
		u1 = up[1];	/* Destination port in net byte order */
		/* Find a client stream. */
		if (ire_type != IRE_BROADCAST) {
			ipc = (ipc_t *)&ipc_udp_fanout[IP_UDP_HASH(u1)];
			do {
				ipc = ipc->ipc_hash_next;
				if (!ipc) {
					ipc = ipc_proto_fanout[IPPROTO_UDP];
					if (ipc)
						goto wildcard;
#ifdef MI_HRTIMING
					if ( oipc )
						MI_HRT_CLEAR(oipc->ipc_wtmp);
#endif /* MI_HRTIMING */
					if (ip_hdr_complete(ipha))
						freemsg(mp);
					else
						icmp_unreachable(WR(q), mp, 
							ICMP_PORT_UNREACHABLE);
					TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
						"ip_wput_local_end: q %X (%S)",
						q, "udp unreachable");
					return;
				}
			} while (!IP_UDP_MATCH(ipc, u1, dst));
			q = ipc->ipc_rq;
			if (canputnext(q)) {
				BUMP_MIB(ip_mib.ipInDelivers);
#ifdef MI_HRTIMING
				if ( oipc ) {
					MI_HRT_INCREMENT(oipc->ipc_wtime,
						oipc->ipc_wtmp, 1);
					MI_HRT_CLEAR(oipc->ipc_wtmp);
				}
#endif /* MI_HRTIMING */
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
					"ip_wput_local_end: q %X (%S)",
					q, "udp putnext");
				putnext(q, mp);
				return;
			}
			BUMP_MIB(ip_mib.udpInOverflows);
			q = WR(q);
#ifdef MI_HRTIMING
			if ( oipc )
				MI_HRT_CLEAR(oipc->ipc_wtmp);
#endif /* MI_HRTIMING */
			if (ip_hdr_complete(ipha))
				freemsg(mp);
			else
				icmp_source_quench(q, mp);
			TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
				"ip_wput_local_end: q %X (%S)",
				q, "udp overflow");
			return;
		}
		/* Multicast and broadcast case */
#ifdef MI_HRTIMING
		if ( oipc )
			MI_HRT_CLEAR(oipc->ipc_wtmp);
#endif /* MI_HRTIMING */
		ipc = (ipc_t *)&ipc_udp_fanout[IP_UDP_HASH(u1)];
		do {
			ipc = ipc->ipc_hash_next;
			if (!ipc) {
				ipc = ipc_proto_fanout[IPPROTO_UDP];
				if (ipc)
					goto wildcard;
				freemsg(mp);
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
					"ip_wput_local_end: q %X (%S)",
					q, "udp no fanout");
				return;
			}
		} while (!IP_UDP_MATCH(ipc, u1, dst)
		||  !ipc_wantpacket(ipc, dst));
		if (ipc->ipc_reuseaddr) {
			ipc_t	* first_ipc = ipc;
			
			for (;;) {
				for ( ipc = ipc->ipc_hash_next
				; ipc
				; ipc = ipc->ipc_hash_next ) {
					if (IP_UDP_MATCH(ipc, u1, dst)
					&&  ipc_wantpacket(ipc, dst) )
						break;
				}
				if (!ipc  ||  !(mp1 = copymsg(mp))) {
					ipc = first_ipc;
					break;
				}
				q = ipc->ipc_rq;
				if (canputnext(q)) {
					BUMP_MIB(ip_mib.ipInDelivers);
					putnext(q, mp1);
				} else {
					BUMP_MIB(ip_mib.udpInOverflows);
					freemsg(mp1);
				}
			}
		}
		q = ipc->ipc_rq;
		if (canputnext(q)) {
			BUMP_MIB(ip_mib.ipInDelivers);
			TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
				"ip_wput_local_end: q %X (%S)",
				q, "udp ipInDelivers");
			putnext(q, mp);
			return;
		}
		BUMP_MIB(ip_mib.udpInOverflows);
		freemsg(mp);
		TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
			"ip_wput_local_end: q %X (%S)",
			q, "udp InOverflows");
		return;
	}
	case IPPROTO_TCP: {
		u32	ports;

		if (mp->b_datap->db_type == M_DATA) {
			/*
			 * M_DATA mblk, so init mblk (chain) for no struio().
			 */
			mblk_t	* mp1 = mp;

			do
				mp1->b_datap->db_struioflag = 0;
			while ((mp1 = mp1->b_cont) != NULL);
		}
		ports = *(u32 *)ALIGN32(rptr + IPH_HDR_LENGTH(ipha));
		ipc = (ipc_t *)&ipc_tcp_fanout[IP_TCP_HASH(dst, ports)];
		/* Find a client stream. */
		do {
			ipc = ipc->ipc_hash_next;
			if (!ipc) {
				ipc = ipc_proto_fanout[IPPROTO_TCP];
				if (ipc)
					goto wildcard;
#ifdef MI_HRTIMING
				if ( oipc )
					MI_HRT_CLEAR(oipc->ipc_wtmp);
#endif /* MI_HRTIMING */
				if (ip_hdr_complete(ipha))
					freemsg(mp);
				else
					icmp_unreachable(WR(q), mp, 
						ICMP_PORT_UNREACHABLE);
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
					"ip_wput_local_end: q %X (%S)",
					q, "tcp icmp_unreachable");
				return;
			}
		} while (!TCP_MATCH(ipc, ipha, ports));
		q = ipc->ipc_rq;
#ifdef MI_HRTIMING
		if ( oipc ) {
			MI_HRT_INCREMENT(oipc->ipc_wtime, oipc->ipc_wtmp, 1);
			MI_HRT_CLEAR(oipc->ipc_wtmp);
		}
#endif /* MI_HRTIMING */
		BUMP_MIB(ip_mib.ipInDelivers);
#ifdef	IP_NEED_LOOP_PROTECTION
		/*
		 * NOTE: this stinks.  It avoids problems on Streams
		 * implementations that do not properly support QUEUEPAIR
		 * synchronization.
		 */
		ipca = (ipc_t *)q->q_ptr;
		if ( ipc->ipc_looping  ||  ipc == ipca ) {
			(void) putq(q, mp);
			qenable(q);
			TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
				"ip_wput_local_end: q %X (%S)",
				q, "tcp not queuepair");
			return;
		}
		TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
			"ip_wput_local_end: q %X (%S)",
			q, "tcp (no loop protection)");
		ipca->ipc_looping = true;
		putnext(q, mp);
		ipca->ipc_looping = false;
#else
		TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
			"ip_wput_local_end: q %X (%S)",
			q, "tcp (no loop protection)");
		putnext(q, mp);
#endif
		return;
	}
	}
wildcard:;
#ifdef MI_HRTIMING
	if ( oipc )
		MI_HRT_CLEAR(oipc->ipc_wtmp);
#endif /* MI_HRTIMING */
	/*
	 * Find a client for some other protocol.  We give
	 * copies to multiple clients, if more than one is
	 * bound.
	 */
	ipc = ipc_proto_fanout[ipha->ipha_protocol];
	if (!ipc) {
		if (ip_hdr_complete(ipha))
			freemsg(mp);
		else
			icmp_unreachable(WR(q), mp, ICMP_PROTOCOL_UNREACHABLE);
		TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
			"ip_wput_local_end: q %X (%S)",
			q, "other unreachable");
		return;
	}
#ifdef	IP_NEED_LOOP_PROTECTION
	ipca = (ipc_t *)q->q_ptr;
#endif
	do {
		if ((ipc->ipc_udp_addr != 0 && ipc->ipc_udp_addr != dst) ||
		    !ipc_wantpacket(ipc, dst)) {
			ipc = ipc->ipc_hash_next;
			if (ipc == nilp(ipc_t)) {
				freemsg(mp);
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
					"ip_wput_local_end: q %X (%S)",
					q, "nilp");
				return;
			}
			mp1 = nilp(mblk_t);
			continue;
		}
		if (!ipc->ipc_hash_next  ||  (!(mp1 = dupmsg(mp))))
			mp1 = mp;
		q = ipc->ipc_rq;
		if (!canputnext(q)) {
			BUMP_MIB(ip_mib.rawipInOverflows);
			if (ip_hdr_complete((ipha_t *)ALIGN32(mp1->b_rptr)))
				freemsg(mp1);
			else
				icmp_source_quench(WR(q), mp1);
		} else {
			BUMP_MIB(ip_mib.ipInDelivers);
#ifdef	IP_NEED_LOOP_PROTECTION
			if ( ipc->ipc_looping  ||  ipc == ipca ) {
				(void) putq(q, mp1);
				qenable(q);
				TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
					"ip_wput_local_end: q %X (%S)",
					q, "other looping");
				return;
			}
			ipca->ipc_looping = true;
			putnext(q, mp1);
			ipca->ipc_looping = false;
#else
			putnext(q, mp1);
#endif
		}
		ipc = ipc->ipc_hash_next;
	} while (mp1 != mp);
	TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
		"ip_wput_local_end: q %X (%S)",
		q, "end loop mp1 != mp");
	return;

drop_pkt:;
	ip1dbg(("ip_wput_local: dropped\n"));
	freemsg(mp);
	TRACE_2(TR_FAC_IP, TR_IP_WPUT_LOCAL_END,
		"ip_wput_local_end: q %X (%S)",
		q, "end drop_pkt");

#undef	rptr
}

/* Update any source route, record route or timestamp options */
/* Check that we are at end of strict source route */
staticf void
ip_wput_local_options (ipha)
	ipha_t	* ipha;
{
	u32	totallen;
	u_char	* opt;
	u32	optval;
	u32	optlen;
	u32	dst;
	u32	u1;

	ip2dbg(("ip_wput_local_options\n"));
	totallen = ipha->ipha_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	opt = (u_char *)&ipha[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			return;
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}
		if (optlen == 0 || optlen > totallen)
			break;

		switch (optval) {
			u32 off;
		case IPOPT_SSRR:
		case IPOPT_LSRR: 
			off = opt[IPOPT_POS_OFF];
			off--;
			if (optlen < IP_ADDR_LEN ||
			    off > optlen - IP_ADDR_LEN) {
				/* End of source route */
				break;
			}
			/* This will only happen if two consecutive entries
			 * in the source route contains our address or if
			 * it is a packet with a loose source route which 
			 * reaches us before consuming the whole source route
			 */
			ip1dbg(("ip_wput_local_options: not end of SR\n"));
			if (optval == IPOPT_SSRR) {
				return;
			}
			/* Hack: instead of dropping the packet truncate the
			 * source route to what has been used.
			 */
			bzero((char *)opt + off, optlen - off);
			opt[IPOPT_POS_LEN] = off;
			break;
		case IPOPT_RR:
			off = opt[IPOPT_POS_OFF];
			off--;
			if (optlen < IP_ADDR_LEN ||
			    off > optlen - IP_ADDR_LEN) {
				/* No more room - ignore */
				ip1dbg(("ip_wput_forward_options: end of RR\n"));
				break;
			}
			dst = INADDR_LOOPBACK;
			bcopy((char *)&dst, (char *)opt + off,
			      IP_ADDR_LEN);
			opt[IPOPT_POS_OFF] += IP_ADDR_LEN;
			break;
		case IPOPT_IT:
			/* Insert timestamp if there is romm */
			switch (opt[IPOPT_POS_OV_FLG] & 0x0F) {
			case IPOPT_IT_TIME:
				off = IPOPT_IT_TIMELEN;
				break;
			case IPOPT_IT_SPEC:
#ifdef IPOPT_IT_SPEC_BSD
			case IPOPT_IT_SPEC_BSD:
#endif
				/* Verify that the address matched */
				off = opt[IPOPT_POS_OFF] - 1;
				bcopy((char *)opt + off, (char *)&dst, 
				      IP_ADDR_LEN);
				if (!ire_lookup_myaddr(dst))
					/* Not for us */
					break;
				/* FALLTHRU */
			case IPOPT_IT_TIME_ADDR:
				off = IP_ADDR_LEN + IPOPT_IT_TIMELEN;
				break;
			}
			if (opt[IPOPT_POS_OFF] - 1 + off > optlen) {
				/* Increase overflow counter */
				off = (opt[IPOPT_POS_OV_FLG] >> 4) + 1;
				opt[IPOPT_POS_OV_FLG] = 
					(opt[IPOPT_POS_OV_FLG] & 0x0F) | (off << 4);
				break;
			}
			off = opt[IPOPT_POS_OFF] - 1;
			switch (opt[IPOPT_POS_OV_FLG] & 0x0F) {
			case IPOPT_IT_SPEC:
#ifdef IPOPT_IT_SPEC_BSD
			case IPOPT_IT_SPEC_BSD:
#endif
			case IPOPT_IT_TIME_ADDR:
				dst = INADDR_LOOPBACK;
				bcopy((char *)&dst, 
				      (char *)opt + off,
				      IP_ADDR_LEN);
				opt[IPOPT_POS_OFF] += IP_ADDR_LEN;
				/* FALLTHRU */
			case IPOPT_IT_TIME:
				off = opt[IPOPT_POS_OFF] - 1;
				/* Compute # of milliseconds since midnight */
				u1 = ((time_in_secs % (24 * 60 * 60)) * 1000) +
					(LBOLT_TO_MS(lbolt) % 1000);
				bcopy((char *)&u1, (char *)opt + off,
				      IP_ADDR_LEN);
				opt[IPOPT_POS_OFF] += IPOPT_IT_TIMELEN;
				break;
			}
			break;
		}
		totallen -= optlen;
		opt += optlen;
	}
	return;
}
	
/*
 * Send out a multicast packet on interface ipif 
 */
void
ip_wput_multicast (q, mp, ipif)
	queue_t	* q;
	mblk_t	* mp;
	ipif_t	* ipif;
{
reg	ipha_t	* ipha;
#define	rptr	((u_char *)ipha)
reg	ire_t	* ire;
reg	queue_t	* stq;
reg	u32	v_hlen_tos_len;
reg	u32	dst;
	u32	local_addr;

	ipha = (ipha_t *)ALIGN32(mp->b_rptr);

#ifdef	_BIG_ENDIAN
#define	V_HLEN	(v_hlen_tos_len >> 24)
#else
#define	V_HLEN	(v_hlen_tos_len & 0xFF)
#endif

#ifndef SPEED_BEFORE_SAFETY
	/*
	 * Make sure we have a full-word aligned message and that at least
	 * a simple IP header is accessible in the first message.  If not,
	 * try a pullup.
	 */
	if (!OK_32PTR(rptr)
	|| (mp->b_wptr - rptr) < IP_SIMPLE_HDR_LENGTH) {
		if (!pullupmsg(mp, IP_SIMPLE_HDR_LENGTH)) {
			BUMP_MIB(ip_mib.ipOutDiscards);
			freemsg(mp);
			return;
		}
		ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	}
#endif

	/* Most of the code below is written for speed, not readability */
	v_hlen_tos_len = ((u32 *)ipha)[0];

#ifndef SPEED_BEFORE_SAFETY
	/* If ip_newroute() fails, we're going to need a full
	 * header for the icmp wraparound.
	 */
	if (V_HLEN != IP_SIMPLE_HDR_VERSION) {
		u_int	v_hlen = V_HLEN;
		if ((v_hlen >> 4) != IP_VERSION) {
			BUMP_MIB(ip_mib.ipOutDiscards);
			freemsg(mp);
			return;
		}
		/*
		 * Are there enough bytes accessible in the header?  If
		 * not, try a pullup.
		 */
		v_hlen &= 0xF;
		v_hlen <<= 2;
		if (v_hlen > (mp->b_wptr - rptr)) {
			if (!pullupmsg(mp, v_hlen)) {
				BUMP_MIB(ip_mib.ipOutDiscards);
				freemsg(mp);
				return;
			}
			ipha = (ipha_t *)ALIGN32(mp->b_rptr);
		}
	}
#endif

	/* Find an IRE which matches the destination and the outgoing
	 * queue (i.e. the outgoing interface.)
	 */
	if (ipif->ipif_flags & IFF_POINTOPOINT)
		dst = ipif->ipif_pp_dst_addr;
	else
		dst = ipha->ipha_dst;
	stq = ipif->ipif_ill->ill_wq;
	local_addr = ipif->ipif_local_addr;

	ire = (ire_t *)&ire_hash_tbl[IRE_ADDR_HASH(dst)];
	do {
		ire = ire->ire_next;
		if (!ire) {
			/*
			 * Mark this packet to make it be delivered to
			 * ip_wput_ire after the new ire has been
			 * created.
			 */
			mp->b_prev = nilp(mblk_t);
			mp->b_next = nilp(mblk_t);
			ip_newroute_ipif(q, mp, ipif, dst);
			return;
		}
	} while (ire->ire_addr != dst || ire->ire_stq != stq ||
		 ire->ire_src_addr != local_addr);
	ip_wput_ire(q, mp, ire);
}

/* Called from ip_wput for all non data messages */
staticf void
ip_wput_nondata (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	mblk_t	* mp1;

	switch (mp->b_datap->db_type) {
	case M_IOCTL:
		/*
		 * IOCTL processing begins in ip_sioctl_copyin_setup which
		 * will arrange to copy in associated control structures.
		 */
		ip_sioctl_copyin_setup(q, mp);
		return;
	case M_IOCDATA:
		/* IOCTL continuation following copyin or copyout. */
		if ( mi_copy_state(q, mp, nilp(mblk_t *)) == -1 ) {
			/*
			 * The copy operation failed.  mi_copy_state already
			 * cleaned up, so we're out of here.
			 */
			return;
		}
		/*
		 * If we just completed a copy in, we become writer and
		 * continue processing in ip_sioctl_copyin_done.  If it
		 * was a copy out, we call mi_copyout again.  If there is
		 * nothing more to copy out, it will complete the IOCTL.
		 */
		if ( MI_COPY_DIRECTION(mp) == MI_COPY_IN ) {
			if (ip_sioctl_copyin_writer(mp))
				become_writer(q, mp,
					      (pfi_t)ip_sioctl_copyin_done);
			else
				ip_sioctl_copyin_done(q, mp);
		} else
			mi_copyout(q, mp);
		return;
	case M_IOCNAK:
		/*
		 * The only way we could get here is if a resolver didn't like
		 * an IOCTL we sent it.  This shouldn't happen.
		 */
		mi_strlog(q, 1, SL_ERROR|SL_TRACE,
			"ip_wput: unexpected M_IOCNAK, ioc_cmd 0x%x",
			((struct iocblk *)ALIGN32(mp->b_rptr))->ioc_cmd);
		freemsg(mp);
		return;
	case M_IOCACK:
		/* Finish socket ioctls passed through to ARP. */
		become_writer(q, mp, ip_sioctl_iocack);
		return;
	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW)
			flushq(q, FLUSHALL);
		if (q->q_next) {
			putnext(q, mp);
			return;
		}
		if (*mp->b_rptr & FLUSHR) {
			*mp->b_rptr &= ~FLUSHW;
			qreply(q, mp);
			return;
		}
		freemsg(mp);
		return;
	case IRE_DB_REQ_TYPE:
		/* An Upper Level Protocol wants a copy of an IRE. */
		ip_ire_req(q, mp);
		return;
	case M_CTL:
		/* M_CTL messages are used by ARP to tell us things. */
		if ( (mp->b_wptr - mp->b_rptr) < sizeof(arc_t) )
			break;
		switch ( ((arc_t *)ALIGN32(mp->b_rptr))->arc_cmd ) {
		case AR_ENTRY_SQUERY:
			ip_wput_ctl(q, mp);
			return;
		case AR_CLIENT_NOTIFY:
			become_writer(q, mp, (pfi_t)ip_arp_news);
			return;
		default:
			break;
		}
		break;
	case M_PROTO:
	case M_PCPROTO:
		/*
		 * The only PROTO messages we expect are ULP binds and
		 * copies of option negotiation acknowledgements.
		 */
		switch (((union T_primitives *)ALIGN32(mp->b_rptr))->type) {
		case T_BIND_REQ:
			become_writer(q, mp, (pfi_t)ip_bind);
			return;
		case T_OPTMGMT_REQ:
		case T_OPTMGMT_ACK:
			ip2dbg(("ip_wput: OPTMGMT_%s\n",
				((union T_primitives *)
				 ALIGN32(mp->b_rptr))->type == T_OPTMGMT_REQ 
				? "REQ" : "ACK"));
			/*
			 * Only IP_ADD/DROP_MEMBERSHIP and the multicast router
			 * socket options require the write lock.
			 */
			if (ip_optmgmt_writer(mp))
				become_writer(q, mp, (pfi_t)ip_optmgmt_req);
			else {
				/*
				 * Call ip_optmgmt_req so that it can generate
				 * the ack.
				 */
				ip_optmgmt_req(q, mp);
			}
			return;
		case T_UNBIND_REQ:
			become_writer(q, mp, (pfi_t)ip_unbind);
			return;
		default:
			/*
			 * Have to drop any DLPI messages comming down from
			 * arp (such as an info_req which would cause ip
			 * to receive an extra info_ack if it was passed
			 * through.
			 */
			ip1dbg(("ip_wput_nondata: dropping M_PROTO %d\n",
				(int)*(u_long *)ALIGN32(mp->b_rptr)));
			freemsg(mp);
			return;
		}
		/* NOTREACHED */
	case IRE_DB_TYPE:
		/*
		 * This is a response back from a resolver.  It
		 * consists of a message chain containing:
		 * 	IRE_MBLK-->LL_HDR_MBLK->pkt
		 * The IRE_MBLK is the one we allocated in ip_newroute.
		 * The LL_HDR_MBLK is the DLPI header to use to get
		 * the attached packet, and subsequent ones for the
		 * same destination, transmitted.
		 *
		 * Rearrange the message into:
		 * 	IRE_MBLK-->pkt
		 * and point the ire_ll_hdr_mp field of the IRE
		 * to LL_HDR_MBLK.  Then become writer and, in
		 * ire_add_then_put, the IRE will be added, and then
		 * the packet will be run back through ip_wput.
		 * This time it will make it to the wire.
		 */
		if ((mp->b_wptr - mp->b_rptr) != sizeof(ire_t))
			break;
		mp1 = mp->b_cont;
		((ire_t *)ALIGN32(mp->b_rptr))->ire_ll_hdr_mp = mp1;
		mp->b_cont = mp1->b_cont;
		mp1->b_cont = nilp(mblk_t);
		become_writer(q, mp, (pfi_t)ire_add_then_put);
		return;
	default:
		break;
	}
	if (q->q_next) {
		putnext(q, mp);
	} else
		freemsg(mp);
}

/*
 * Process IP options in an outbound packet.  Modify the destination if there
 * is a source route option.
 * Returns non-zero if something fails in which case an ICMP error has been
 * sent and mp freed.
 */
staticf int
ip_wput_options(q, mp, ipha)
	queue_t	* q;
	mblk_t	* mp;
	ipha_t	* ipha;
{
	u32	totallen;
	u_char	* opt;
	u32	optval;
	u32	optlen;
	u32	dst;
	int	code = 0;

	ip2dbg(("ip_wput_options\n"));
	dst = ipha->ipha_dst;
	totallen = ipha->ipha_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	opt = (u_char *)&ipha[1];
	totallen <<= 2;
	while (totallen != 0) {
		switch (optval = opt[IPOPT_POS_VAL]) {
		case IPOPT_EOL:
			return (0);
		case IPOPT_NOP:
			optlen = 1;
			break;
		default:
			optlen = opt[IPOPT_POS_LEN];
		}
		if (optlen == 0 || optlen > totallen) {
			ip1dbg(("ip_wput_options: bad option len %d, %d\n",
				optlen, totallen));
			code = (char *)&opt[IPOPT_POS_LEN] - (char *)ipha;
			goto param_prob;
		}
		ip2dbg(("ip_wput_options: opt %d, len %d\n", 
		    optval, optlen));

		switch (optval) {
			u32 off;
		case IPOPT_SSRR:
		case IPOPT_LSRR: 
			off = opt[IPOPT_POS_OFF];
			if (off < IPOPT_MINOFF_SR) {
				ip1dbg(("ip_wput_options: bad option offset %d\n",
					off));
				code = (char *)&opt[IPOPT_POS_OFF] - (char *)ipha;
				goto param_prob;
			}
			ip1dbg(("ip_wput_options: next hop 0x%x\n",
				(int)ntohl(dst)));
			/*
			 * For strict: verify that dst is directly
			 * reachable.
			 */
			if (optval == IPOPT_SSRR &&
			    !ire_lookup_interface(dst, IRE_INTERFACE)) {
				ip1dbg(("ip_wput_options: SSRR not directly reachable: 0x%x\n",
					(int)ntohl(dst)));
				goto bad_src_route;
			}
			break;
		case IPOPT_RR:
			off = opt[IPOPT_POS_OFF];
			if (off < IPOPT_MINOFF_SR) {
				ip1dbg(("ip_wput_options: bad option offset %d\n",
					off));
				code = (char *)&opt[IPOPT_POS_OFF] -
				    (char *)ipha;
				goto param_prob;
			}
			break;
		case IPOPT_IT:
			/*
			 * Verify that length >=5 and that there is either
			 * room for another timestamp or that the overflow
			 * counter is not maxed out.
			 */
			code = (char *)&opt[IPOPT_POS_LEN] - (char *)ipha;
			if (optlen < IPOPT_MINLEN_IT) {
				goto param_prob;
			}
			switch (opt[IPOPT_POS_OV_FLG] & 0x0F) {
			case IPOPT_IT_TIME:
				off = IPOPT_IT_TIMELEN;
				break;
			case IPOPT_IT_TIME_ADDR:
			case IPOPT_IT_SPEC:
#ifdef IPOPT_IT_SPEC_BSD
			case IPOPT_IT_SPEC_BSD:
#endif
				off = IP_ADDR_LEN + IPOPT_IT_TIMELEN;
				break;
			default:
				code = (char *)&opt[IPOPT_POS_OV_FLG] -
				    (char *)ipha;
				goto param_prob;
			}
			if (opt[IPOPT_POS_OFF] - 1 + off > optlen &&
			    (opt[IPOPT_POS_OV_FLG] & 0xF0) == 0xF0) {
				/*
				 * No room and the overflow counter is 15
				 * already.
				 */
				goto param_prob;
			}
			off = opt[IPOPT_POS_OFF];
			if (off < IPOPT_MINOFF_IT) {
				code = (char *)&opt[IPOPT_POS_OFF] -
				    (char *)ipha;
				goto param_prob;
			}
			break;
		}
		totallen -= optlen;
		opt += optlen;
	}
	return (0);

param_prob:
	/*
	 * Since ip_wput() isn't close to finished, we fill
	 * in enough of the header for credible error reporting.
	 */
	if (ip_hdr_complete((ipha_t *)ALIGN32(mp->b_rptr))) {
		/* Failed */
		freemsg(mp);
		return (-1);
	}
	icmp_param_problem(q, mp, code);
	return (-1);

bad_src_route:
	/*
	 * Since ip_wput() isn't close to finished, we fill
	 * in enough of the header for credible error reporting.
	 */
	if (ip_hdr_complete((ipha_t *)ALIGN32(mp->b_rptr))) {
		/* Failed */
		freemsg(mp);
		return (-1);
	}
	icmp_unreachable(q, mp, ICMP_SOURCE_ROUTE_FAILED);
	return (-1);
}

/*
 * Hash list insertion routine for IP client structures.  (Always called as
 * writer.)
 * Used when bound to a specified interface address.
 */
staticf void
ipc_hash_insert_first(ipcp, ipc)
reg	ipc_t	** ipcp;
reg	ipc_t	* ipc;
{
reg	ipc_t	* ipcnext;

	ipc_hash_remove(ipc);
	ipcnext = ipcp[0];
	if (ipcnext)
		ipcnext->ipc_ptphn = &ipc->ipc_hash_next;
	ipc->ipc_hash_next = ipcnext;
	ipc->ipc_ptphn = ipcp;
	ipcp[0] = ipc;
}

/*
 * Hash list insertion routine for IP client structures.  (Always called as
 * writer.)
 * Used when bound to INADDR_ANY to make streams bound to a specified interface
 * address take precedence.
 */
staticf void
ipc_hash_insert_last(ipcp, ipc)
reg	ipc_t	** ipcp;
reg	ipc_t	* ipc;
{
reg	ipc_t	* ipcnext;

	ipc_hash_remove(ipc);
	/* Skip to end of list */
	while (ipcp[0])
		ipcp = &(ipcp[0]->ipc_hash_next);
	ipcnext = ipcp[0];
	if (ipcnext)
		ipcnext->ipc_ptphn = &ipc->ipc_hash_next;
	ipc->ipc_hash_next = ipcnext;
	ipc->ipc_ptphn = ipcp;
	ipcp[0] = ipc;
}

/*
 * Write service routine.
 */
void
ip_wsrv(q)
	queue_t	* q;
{
	ipc_t	* ipc;
	mblk_t	* mp;

	if (q->q_next) {
		/*
		 * The device flow control has opened up.
		 * Walk through all upper (ipc) streams and qenable
		 * those that have queued data.
		 */
		ip1dbg(("ip_wsrv: walking\n"));
		ipc_walk_nontcp(ipc_qenable, NULL);
		return;
	}
	if (q->q_first == NULL)
		return;

	ipc = (ipc_t *)q->q_ptr;
	ip1dbg(("ip_wsrv: %X %X\n", (int)q, (int)ipc));
	/*
	 * Set ipc_draining flag to make ip_wput pass messages through and
	 * do a putbq instead of a putq if flow controlled from the driver.
	 * noenable the queue so that a putbq from ip_wsrv does not reenable
	 * (causing an infinite loop).
	 * Note: this assumes that ip is configured such that no
	 * other thread can execute in ip_wput while ip_wsrv is running.
	 */
	ipc->ipc_draining = 1;
	noenable(q);
	while (mp = getq(q)) {
		ip_wput(q, mp);
		if (ipc->ipc_did_putbq) {
			/* ip_wput did a putbq */
			ipc->ipc_did_putbq = 0;
			break;
		}
	}
	enableok(q);
	ipc->ipc_draining = 0;
}

/*
 * Hash list removal routine for IP client structures.  (Always called as
 * writer.)
 */
staticf void
ipc_hash_remove(ipc)
reg	ipc_t	* ipc;
{
reg	ipc_t	* ipcnext;

	if (ipc->ipc_ptphn) {
		ipcnext = ipc->ipc_hash_next;
		if (ipcnext) {
			ipcnext->ipc_ptphn = ipc->ipc_ptphn;
			ipc->ipc_hash_next = nilp(ipc_t);
		}
		*ipc->ipc_ptphn = ipcnext;
		ipc->ipc_ptphn = nilp(ipc_t *);
	}
}

/*
 * qenable the write side queue.
 * Used when flow control is opened up on the device stream.
 * Note: it is not possible to restrict the enabling to those
 * queues that have data queued since that would introduce a race
 * condition.
 */
/* ARGSUSED */
staticf void
ipc_qenable(ipc, arg)
	ipc_t	* ipc;
	caddr_t arg;
{
	qenable(ipc->ipc_wq);
}

/*
 * Walk the list of all IPC's calling the function provided with the
 * specified argument for each.  Note that this only walks IPC's that
 * have been bound.
 */
void
ipc_walk(func, arg)
	pfv_t	func;
	caddr_t	arg;
{
	int	i;
	ipc_t	* ipc, * ipc1;

#ifdef lint
	ipc1 = nilp(ipc_t);
#endif

	for (i = 0; i < A_CNT(ipc_udp_fanout); i++) {
		for (ipc = ipc_udp_fanout[i]; ipc; ipc = ipc1) {
			ipc1 = ipc->ipc_hash_next;
			(*func)(ipc, arg);
		}
	}
	for (i = 0; i < A_CNT(ipc_tcp_fanout); i++) {
		for (ipc = ipc_tcp_fanout[i]; ipc; ipc = ipc1) {
			ipc1 = ipc->ipc_hash_next;
			(*func)(ipc, arg);
		}
	}
	for (i = 0; i < A_CNT(ipc_proto_fanout); i++) {
		for (ipc = ipc_proto_fanout[i]; ipc; ipc = ipc1) {
			ipc1 = ipc->ipc_hash_next;
			(*func)(ipc, arg);
		}
	}
}

/*
 * Walk the list of all IPC's except TCP IPC's calling the function
 * provided with the specified argument for each.
 * Note that this only walks IPC's that have been bound.
 */
void
ipc_walk_nontcp(func, arg)
	pfv_t	func;
	caddr_t	arg;
{
	int	i;
	ipc_t	* ipc, * ipc1;

#ifdef lint
	ipc1 = nilp(ipc_t);
#endif

	for (i = 0; i < A_CNT(ipc_udp_fanout); i++) {
		for (ipc = ipc_udp_fanout[i]; ipc; ipc = ipc1) {
			ipc1 = ipc->ipc_hash_next;
			(*func)(ipc, arg);
		}
	}
	for (i = 0; i < A_CNT(ipc_proto_fanout); i++) {
		for (ipc = ipc_proto_fanout[i]; ipc; ipc = ipc1) {
			ipc1 = ipc->ipc_hash_next;
			(*func)(ipc, arg);
		}
	}
}

boolean_t
ipc_wantpacket(ipc, dst)
	ipc_t	* ipc;
	u32	dst;
{
	if (!CLASSD(dst) || ipc->ipc_multi_router)
		return (true);
	return (ilg_member(ipc, dst));
}
