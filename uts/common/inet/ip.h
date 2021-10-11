/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_INET_IP_H
#define	_INET_IP_H

#pragma ident	"@(#)ip.h	1.32	95/02/25 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/isa_defs.h>

#define	IP_DEBUG

#define	IP_CSUM(mp, off, sum)		(~ip_cksum(mp, off, sum) & 0xFFFF)
#define	IP_CSUM_PARTIAL(mp, off, sum)	ip_cksum(mp, off, sum)

#define	ILL_FRAG_HASH_TBL_COUNT	((unsigned int)64)
#define	ILL_FRAG_HASH_TBL_SIZE	(ILL_FRAG_HASH_TBL_COUNT * sizeof (ipfb_t))

#define	IP_DEV_NAME			"/dev/ip"
#define	IP_MOD_NAME			"ip"
#define	IP_ADDR_LEN			4
#define	IP_ARP_PROTO_TYPE		0x0800

#define	IP_VERSION			4
#define	IP_SIMPLE_HDR_LENGTH_IN_WORDS	5
#define	IP_SIMPLE_HDR_LENGTH		20
#define	IP_MAX_HDR_LENGTH		64
#define	IP_SIMPLE_HDR_VERSION \
	((IP_VERSION << 4) | IP_SIMPLE_HDR_LENGTH_IN_WORDS)

#define	IP_IOCTL			(('i'<<8)|'p')
#define	IP_IOC_IRE_DELETE		4
#define	IP_IOC_IRE_DELETE_NO_REPLY	5
#define	IP_IOC_IRE_ADVISE_NO_REPLY	6

/* IP Options */
#define	IPOPT_EOL		0x00
#define	IPOPT_NOP		0x01
#define	IPOPT_SEC		0x82
#define	IPOPT_LSRR		0x83
#define	IPOPT_IT		0x44
#define	IPOPT_RR		0x07
#define	IPOPT_SID		0x88
#define	IPOPT_SSRR		0x89

/* Bits in the option value */
#define	IPOPT_COPY		0x80

/* IP option header indexes */
#define	IPOPT_POS_VAL		0
#define	IPOPT_POS_LEN		1
#define	IPOPT_POS_OFF		2
#define	IPOPT_POS_OV_FLG	3

/* Minimum for src and record route options */
#define	IPOPT_MINOFF_SR	4

/* Minimum for timestamp option */
#define	IPOPT_MINOFF_IT	5
#define	IPOPT_MINLEN_IT	5

/* Timestamp option flag bits */
#define	IPOPT_IT_TIME		0	/* Only timestamp */
#define	IPOPT_IT_TIME_ADDR	1	/* Timestamp + IP address */
#define	IPOPT_IT_SPEC		3	/* Only listed routers */
#define	IPOPT_IT_SPEC_BSD	2	/* Defined fopr BSD compat */

#define	IPOPT_IT_TIMELEN	4	/* Timestamp size */

/* Controls forwarding of IP packets, set via ndd */
#define	IP_FORWARD_NEVER	0
#define	IP_FORWARD_ALWAYS	1
#define	IP_FORWARD_AUTOMATIC	2

#define	WE_ARE_FORWARDING				\
	(ip_g_forward == IP_FORWARD_ALWAYS ||		\
	    (ip_g_forward == IP_FORWARD_AUTOMATIC &&	\
		ipif_g_count > 1))

#define	IPH_HDR_LENGTH(ipha)						\
	((int)(((iph_t *)ipha)->iph_version_and_hdr_length & 0xF) << 2)

/*
 * IP reassembly macros.  We hide starting and ending offsets in b_next and
 * b_prev of messages on the reassembly queue.	The messages are chained using
 * b_cont.  These macros are used in ip_reassemble() so we don't have to see
 * the ugly casts and assignments.
 */
#define	IP_REASS_START(mp)		((u_long)((mp)->b_next))
#define	IP_REASS_SET_START(mp, u)	((mp)->b_next = (mblk_t *)(u))
#define	IP_REASS_END(mp)		((u_long)((mp)->b_prev))
#define	IP_REASS_SET_END(mp, u)		((mp)->b_prev = (mblk_t *)(u))

#define	ATOMIC_32_INC(u1, p)		(u1 = ++(*(p)))
#define	ATOMIC_32_INIT(au32p)
#define	ATOMIC_32_ASSIGN(au32p, val)	((au32p)[0] = (val))
#define	ATOMIC_32_VAL(au32p)		((au32p)[0])

#ifndef	A_U32
#define	A_U32	unsigned long
#endif

#define	IRE_HASH_TBL_COUNT	256
#define	INE_HASH_TBL_COUNT	32
#define	HASH_BIT_COUNT		8

#ifdef	_BIG_ENDIAN
#define	IRE_ADDR_HASH(l1)						\
		((u32)((l1 >> HASH_BIT_COUNT) ^ (l1))			\
		& ~(~0 << HASH_BIT_COUNT))
#else
#define	IRE_ADDR_HASH(l1)						\
		((u32)((l1 << HASH_BIT_COUNT) ^ (l1)) >>		\
		(u32)(32 - HASH_BIT_COUNT))
#endif

#define	INE_ADDR_HASH(l1)						\
		((((l1 >> 16) ^ (l1)) ^ (((l1 >> 16) ^ (l1)) >> 8))	\
		% INE_HASH_TBL_COUNT)

#ifndef	IRE_DB_TYPE
#define	IRE_DB_TYPE	M_SIG
#endif

#ifndef	IRE_DB_REQ_TYPE
#define	IRE_DB_REQ_TYPE	M_PCSIG
#endif

#define	IRE_IS_TARGET(ire)	((ire)&& ((ire)->ire_type !=		\
				IRE_BROADCAST))

#define	IREP_LOOKUP(addr)	&ire_hash_tbl[IRE_ADDR_HASH(addr)]
#define	IREP_ASSOC_LOOKUP(addr)	&ire_assoc_hash_tbl[IRE_ADDR_HASH(addr)]

/* IP Fragmentation Reassembly Header */
typedef struct ipf_s {
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
	int	ipf_stripped_hdr_len;	/* What we've stripped from the */
					/* lead mblk. */
	u16	ipf_checksum;	/* Partial checksum of fragment data */
	u16	ipf_checksum_valid;
	u_long	ipf_gen;	/* Frag queue generation */
	u_long	ipf_count;	/* Count of bytes used by frag */
} ipf_t;

/* IP packet count structure */
typedef struct ippc_s {
	u32	ippc_addr;
	u_long	ippc_ib_pkt_count;
	u_long	ippc_ob_pkt_count;
	u_long	ippc_fo_pkt_count;
} ippc_t;

/* ICMP types */
#define	ICMP_ECHO_REPLY			0
#define	ICMP_DEST_UNREACHABLE		3
#define	ICMP_SOURCE_QUENCH		4
#define	ICMP_REDIRECT			5
#define	ICMP_ECHO_REQUEST		8
#define	ICMP_ROUTER_ADVERTISEMENT	9
#define	ICMP_ROUTER_SOLICITATION	10
#define	ICMP_TIME_EXCEEDED		11
#define	ICMP_PARAM_PROBLEM		12
#define	ICMP_TIME_STAMP_REQUEST		13
#define	ICMP_TIME_STAMP_REPLY		14
#define	ICMP_INFO_REQUEST		15
#define	ICMP_INFO_REPLY			16
#define	ICMP_ADDRESS_MASK_REQUEST	17
#define	ICMP_ADDRESS_MASK_REPLY		18

/* ICMP_TIME_EXCEEDED codes */
#define	ICMP_TTL_EXCEEDED		0
#define	ICMP_REASSEMBLY_TIME_EXCEEDED	1

/* ICMP_DEST_UNREACHABLE codes */
#define	ICMP_NET_UNREACHABLE		0
#define	ICMP_HOST_UNREACHABLE		1
#define	ICMP_PROTOCOL_UNREACHABLE	2
#define	ICMP_PORT_UNREACHABLE		3
#define	ICMP_FRAGMENTATION_NEEDED	4
#define	ICMP_SOURCE_ROUTE_FAILED	5

/* ICMP Header Structure */
typedef struct icmph_s {
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
#define	icmph_echo_ident	icmph_u.u_echo.u_echo_ident
#define	icmph_echo_seqnum	icmph_u.u_echo.u_echo_seqnum
#define	icmph_du_zero		icmph_u.u_du.u_du_zero
#define	icmph_du_mtu		icmph_u.u_du.u_du_mtu
#define	icmph_pp_ptr		icmph_u.u_pp.u_pp_ptr
#define	icmph_rd_gateway	icmph_u.u_rd.u_rd_gateway
#define	ICMPH_SIZE	8

/* Unaligned IP header */
typedef struct iph_s {
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

/* Aligned IP header */
typedef struct ipha_s {
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

#define	IPH_DF		0x4000	/* Don't fragment */
#define	IPH_MF		0x2000	/* More fragments to come */
#define	IPH_OFFSET	0x1FFF	/* Where the offset lives */

/* IP Mac info structure */
typedef struct ip_m_s {
	u_long	ip_m_mac_type;
	u_long	ip_m_sap;
	int	ip_m_sap_length;
	u_long	ip_m_brdcst_addr_length;
	u_char	* ip_m_brdcst_addr;
} ip_m_t;

/* Internet Routing Entry */
#ifdef _KERNEL
typedef struct ire_s {
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
#endif

/* Router entry types */
#define	IRE_BROADCAST		0x0001
#define	IRE_GATEWAY		0x0002
#define	IRE_LOCAL		0x0004
#define	IRE_LOOPBACK		0x0008
#define	IRE_NET			0x0010
#define	IRE_ROUTE		0x0020
#define	IRE_SUBNET		0x0040
#define	IRE_RESOLVER		0x0080
#define	IRE_ROUTE_ASSOC		0x0100
#define	IRE_ROUTE_REDIRECT	0x0200
#define	IRE_INTERFACE		(IRE_SUBNET|IRE_RESOLVER)

/* Group membership list per upper ill */
typedef struct ilg_s {
	u32		ilg_group;
	struct ipif_s	* ilg_lower;	/* Interface we are member on */
} ilg_t;

/* Multicast address list entry for lower ill */
typedef struct ilm_s {
	u32		ilm_addr;
	int		ilm_refcnt;
	u_int		ilm_timer;	/* IGMP */
	struct ipif_s	* ilm_ipif;	/* Back pointer to ipif */
	struct ilm_s	* ilm_next;	/* Linked list for each ill */
} ilm_t;

/* IP client structure, allocated when ip_open indicates a STREAM device */
#ifdef	_KERNEL
typedef struct ipc_s {
	struct	ipc_s	*ipc_hash_next;	/* Hash chain must be first */
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

typedef struct ipif_s {
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
	mblk_t	* ipif_arp_down_mp;	/* Allocated at time arp comes up to */
					/* prevent awkward out of mem */
					/* condition later */
	mblk_t	* ipif_saved_ire_mp;	/* Allocated for each extra */
					/* IRE_SUBNET/RESOLVER on this */
					/* interface so that they can survive */
					/* ifconfig down. */
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
#define	ipif_wq			ipif_ill->ill_wq
#define	ipif_rq			ipif_ill->ill_rq
#define	ipif_subnet_type	ipif_ill->ill_subnet_type
#define	ipif_resolver_mp	ipif_ill->ill_resolver_mp
#define	ipif_ipif_up_count	ipif_ill->ill_ipif_up_count
#define	ipif_ipif_pending	ipif_ill->ill_ipif_pending
#define	ipif_bcast_mp		ipif_ill->ill_bcast_mp

/*
 * Fragmentation hash bucket
 */
typedef struct ipfb_s {
	struct ipf_s	* ipfb_ipf;	/* List of ... */
	u_long		ipfb_count;	/* Count of bytes used by frag(s) */
	kmutex_t	ipfb_lock;	/* Protect all ipf in list */
} ipfb_t;

typedef struct ill_s {
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
	u_int	ill_phys_addr_length;	/* Excluding the sap. Only set when */
					/* the DL provider supports */
					/* broadcast. */

	mblk_t	* ill_frag_timer_mp;	/* Reassembly timer state. */
	ipfb_t	* ill_frag_hash_tbl;	/* Fragment hash list head. */

	queue_t	* ill_bind_pending_q;	/* Queue waiting for DL_BIND_ACK. */
	ipif_t	* ill_ipif_pending;	/* IPIF waiting for DL_BIND_ACK. */

	/*
	 * ill_hdr_length and ill_hdr_mp will be non zero if
	 * the underlying device supports the M_DATA fastpath
	 */
	int	ill_hdr_length;

	ilm_t	* ill_ilm;		/* Multicast mebership for lower ill */

	/*
	 * All non-nil cells between 'ill_first_mp_to_free' and
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

typedef struct ipparam_s {
	u_long	ip_param_min;
	u_long	ip_param_max;
	u_long	ip_param_value;
	char	* ip_param_name;
} ipparam_t;
#endif	/* _KERNEL */

/* Common fields for IP IOCTLs. */
typedef struct ipllcmd_s {
	u_long	ipllc_cmd;
	u_long	ipllc_name_offset;
	u_long	ipllc_name_length;
} ipllc_t;

/* IP IRE Change Command Structure. */
typedef struct ipic_s {
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
#define	ipic_cmd		ipic_ipllc.ipllc_cmd
#define	ipic_ll_name_length	ipic_ipllc.ipllc_name_length
#define	ipic_ll_name_offset	ipic_ipllc.ipllc_name_offset

/* IP IRE Delete Command Structure. */
typedef struct ipid_s {
	ipllc_t	ipid_ipllc;
	u_long	ipid_ire_type;
	u_long	ipid_addr_offset;
	u_long	ipid_addr_length;
	u_long	ipid_mask_offset;
	u_long	ipid_mask_length;
} ipid_t;
#define	ipid_cmd		ipid_ipllc.ipllc_cmd

/* IP Address Structure. */
typedef struct ipaddr_s {
	u16	ip_family;		/* Presumably AF_INET. */
	u_char	ip_port[2];		/* Port in net-byte order. */
	u_char	ip_addr[4];		/* Internet addr. in net-byte order. */
	u_char	ip_pad[8];
} ipa_t;

#ifdef	_KERNEL
/* Name/Value Descriptor. */
typedef struct nv_s {
	int	nv_value;
	char	* nv_name;
} nv_t;

/* IP Forwarding Ticket */
typedef	struct ipftk_s {
	queue_t	* ipftk_queue;
	u32	ipftk_dst;
} ipftk_t;

extern struct ill_s * ill_g_head;	/* ILL List Head */

extern ill_t	* ip_timer_ill;		/* ILL for IRE expiration timer. */
extern mblk_t	* ip_timer_mp;		/* IRE expiration timer. */
extern u_long	ip_ire_time_elapsed;	/* Time since IRE cache last flushed */

extern ill_t	* igmp_timer_ill;	/* ILL for IGMP timer. */
extern mblk_t	* igmp_timer_mp;	/* IGMP timer */
extern int	igmp_timer_interval;

extern ire_t	* ip_def_gateway;	/* Default gateway list head. */
extern u_int	ip_def_gateway_count;	/* Number of gateways. */
extern u_int	ip_def_gateway_index;	/* Walking index used to mod in */
extern u32	ip_g_all_ones;

extern caddr_t	ip_g_nd;		/* Named Dispatch List Head */

extern int	ip_max_mtu;			/* Used by udp/icmp */

extern ipparam_t	* ip_param_arr;

#define	ip_g_forward			ip_param_arr[0].ip_param_value

#define	ip_respond_to_address_mask_broadcast ip_param_arr[1].ip_param_value
#define	ip_g_resp_to_address_mask	ip_param_arr[1].ip_param_value
#define	ip_debug			ip_param_arr[7].ip_param_value
#define	ip_mrtdebug			ip_param_arr[8].ip_param_value
#define	ip_timer_interval		ip_param_arr[9].ip_param_value
#define	ip_ire_flush_interval		ip_param_arr[10].ip_param_value
#define	ip_path_mtu_discovery		ip_param_arr[18].ip_param_value
#define	ip_ignore_delete_time		ip_param_arr[19].ip_param_value
#define	ip_broadcast_ttl		ip_param_arr[22].ip_param_value

/*
 * Network byte order macros
 */
#ifdef	_BIG_ENDIAN
#define	N_IN_CLASSD_NET		IN_CLASSD_NET
#define	N_INADDR_UNSPEC_GROUP	INADDR_UNSPEC_GROUP
#else /* _BIG_ENDIAN */
#define	N_IN_CLASSD_NET		0x000000f0
#define	N_INADDR_UNSPEC_GROUP	0x000000e0
#endif /* _BIG_ENDIAN */
#define	CLASSD(addr)	(((addr) & N_IN_CLASSD_NET) == N_INADDR_UNSPEC_GROUP)

#ifdef IP_DEBUG
#define	ip0dbg(a)	printf a
#define	ip1dbg(a)	if (ip_debug) printf a
#define	ip2dbg(a)	if (ip_debug > 1) printf a
#define	ip3dbg(a)	if (ip_debug > 2) printf a
#else
#define	ip0dbg(a)	/* */
#define	ip1dbg(a)	/* */
#define	ip2dbg(a)	/* */
#define	ip3dbg(a)	/* */
#endif	/* IP_DEBUG */

extern u_int	ipif_g_count;			/* Count of IPIFs "up". */

extern ire_t	* ire_hash_tbl[];

extern ire_t	* ire_assoc_hash_tbl[];

extern ire_t	* ire_net_hash_tbl[];

extern ire_t	* ire_subnet_head;

extern nv_t	* ire_nv_tbl;

extern int	ipdevflag;

#ifdef __STDC__
extern	void	icmp_time_exceeded(queue_t * q, mblk_t * mp, int code);

extern	u_int	ip_cksum(mblk_t * mp, int offset, u32 prev_sum);
extern	u_int	ip_cksum_partial(mblk_t * mp, int offset, u32 prev_sum);
extern	u_int	ip_csum_hdr(ipha_t * ipha);
extern	u16	ip_csum(mblk_t * mp, int offset, u32 prev_sum);
extern	u16	ip_csum_partial(mblk_t * mp, int offset, u32 prev_sum);

extern	mblk_t	*ip_dlpi_alloc(int len, long prim);

extern	char	*ip_dot_addr(u32 addr, char * buf);

extern	mblk_t	*ip_forwarding_ticket(queue_t * q, u32 dst);

extern	u32	ip_massage_options(ipha_t * ipha);

extern	u32	ip_net_mask(u32 addr);

extern	char 	*ip_nv_lookup(nv_t * nv, int value);

extern	void	ip_rput(queue_t * q, mblk_t * mp);

extern	void	ip_rput_forward(ire_t * ire, ipha_t * ipha, mblk_t * mp);

extern	void	ip_rput_forward_multicast(u32  dst, mblk_t * mp,
						ipif_t * ipif);

extern	void	ip_rput_local(queue_t * q, mblk_t * mp, ipha_t * ipha,
				ire_t * ire);

extern	void	ip_wput(queue_t * q, mblk_t * mp);

extern	void	ip_wput_ire(queue_t * q, mblk_t * mp, ire_t * ire);

extern	void	ip_wput_local(queue_t * q, ipha_t * ipha, mblk_t * mp,
				int ire_type, queue_t * rq);

extern	void	ip_wput_multicast(queue_t * q, mblk_t * mp, ipif_t * ipif);

extern	void	ipc_walk(pfv_t func, caddr_t arg);

extern	boolean_t	ipc_wantpacket(ipc_t * ipc, u32 dst);

extern	void	ip_wsrv(queue_t * q);
#else /* __STDC__ */

extern	void	icmp_time_exceeded();

extern	u_int	ip_cksum();
extern	u_int	ip_cksum_partial();
extern	u_int	ip_csum_hdr();
extern	u16	ip_csum();
extern	u16	ip_csum_partial();

extern	mblk_t	*ip_dlpi_alloc();

extern	char	*ip_dot_addr();

extern	mblk_t	*ip_forwarding_ticket();

extern	u32	ip_massage_options();

extern	u32	ip_net_mask();

extern	char	*ip_nv_lookup();

extern	void	ip_rput();

extern	void	ip_rput_forward();

extern	void	ip_rput_forward_multicast();

extern	void	ip_rput_local();

extern	void	ip_wput();

extern	void	ip_wput_ire();

extern	void	ip_wput_local();

extern	void	ip_wput_multicast();

extern	void	ipc_walk();

extern	boolean_t	ipc_wantpacket();

extern	void	ip_wsrv();
#endif /* __STDC__ */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_IP_H */
