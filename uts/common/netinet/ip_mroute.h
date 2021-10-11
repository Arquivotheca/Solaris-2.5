/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef	_NETINET_IP_MROUTE_H
#define	_NETINET_IP_MROUTE_H

#pragma ident	"@(#)ip_mroute.h	1.9	93/12/21 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Definitions for the kernel part of DVMRP,
 * a Distance-Vector Multicast Routing Protocol.
 * (See RFC-1075.)
 *
 * Written by David Waitzman, BBN Labs, August 1988.
 * Modified by Steve Deering, Stanford, February 1989.
 *
 * MROUTING 1.0
 */

/*
 * DVMRP-specific setsockopt commands.
 */
#define	DVMRP_INIT	100
#define	DVMRP_DONE	101
#define	DVMRP_ADD_VIF	102
#define	DVMRP_DEL_VIF	103
#define	DVMRP_ADD_LGRP	104
#define	DVMRP_DEL_LGRP	105
#define	DVMRP_ADD_MRT	106
#define	DVMRP_DEL_MRT	107


/*
 * Types and macros for handling bitmaps with one bit per virtual interface.
 */
#define	MAXVIFS 32
typedef u_long vifbitmap_t;
typedef u_short vifi_t;		/* type of a vif index */

#define	VIFM_SET(n, m)		((m) |=  (1 << (n)))
#define	VIFM_CLR(n, m)		((m) &= ~(1 << (n)))
#define	VIFM_ISSET(n, m)	((m) &   (1 << (n)))
#define	VIFM_CLRALL(m)		((m) = 0x00000000)
#define	VIFM_COPY(mfrom, mto)	((mto) = (mfrom))
#define	VIFM_SAME(m1, m2)	((m1) == (m2))


/*
 * Agument structure for DVMRP_ADD_VIF.
 * (DVMRP_DEL_VIF takes a single vifi_t argument.)
 */
struct vifctl {
	vifi_t	vifc_vifi;		/* the index of the vif to be added */
	u_char	vifc_flags;		/* VIFF_ flags defined below */
	u_char	vifc_threshold;		/* min ttl required to forward on vif */
	struct in_addr vifc_lcl_addr;	/* local interface address */
	struct in_addr vifc_rmt_addr;	/* remote address (tunnels only) */
};

#define	VIFF_TUNNEL	0x1		/* vif represents a tunnel end-point */
#define	VIFF_SRCRT	0x2		/* tunnel uses IP src routing */


/*
 * For netstat. The different groups are reported by having multiple entries
 * which only differ in the group address field.
 */
struct vifinfo {
	vifi_t		vifi_vifi;	/* the index of the vif to be added */
	u_char		vifi_flags;	/* VIFF_ flags defined above */
	u_char		vifi_threshold;	/* min ttl required to forward on vif */
	struct in_addr	vifi_lcl_addr;	/* local interface address */
	struct in_addr	vifi_rmt_addr;	/* remote address (tunnels only) */
	struct in_addr	vifi_grp_addr;	/* group member (physint only) */
};


/*
 * Argument structure for DVMRP_ADD_LGRP and DVMRP_DEL_LGRP.
 */
struct lgrplctl {
	vifi_t	lgc_vifi;
	struct in_addr lgc_gaddr;
};


/*
 * Argument structure for DVMRP_ADD_MRT.
 * (DVMRP_DEL_MRT takes a single struct in_addr argument, containing origin.)
 * Also used by netstat.
 */
struct mrtctl {
	struct in_addr mrtc_origin;	/* subnet origin of multicasts */
	struct in_addr mrtc_originmask;	/* subnet mask for origin */
	vifi_t	mrtc_parent;    	/* incoming vif */
	vifbitmap_t mrtc_children;	/* outgoing children vifs */
	vifbitmap_t mrtc_leaves;	/* subset of outgoing children vifs */
};

/*
 * The kernel's multicast routing statistics.
 */
struct mrtstat {
	u_long	mrts_mrt_lookups;	/* # multicast route lookups */
	u_long	mrts_mrt_misses;	/* # multicast route cache misses */
	u_long	mrts_grp_lookups;	/* # group address lookups */
	u_long	mrts_grp_misses;	/* # group address cache misses */
	u_long	mrts_no_route;		/* no route for packet's origin */
	u_long	mrts_bad_tunnel;	/* malformed tunnel options */
	u_long	mrts_cant_tunnel;	/* no room for tunnel options */
	u_long	mrts_fwd_in;		/* # packets forwarded */
	u_long	mrts_fwd_out;		/* # resulting outgoing packets */
	u_long	mrts_fwd_drop;		/* # dropped for lack of resources */
	u_long	mrts_wrong_if;		/* arrived on wrong interface	   */
};


#ifdef _KERNEL

/*
 * The kernel's virtual-interface structure.
 */
struct vif {
	u_char	v_flags;	/* VIFF_ flags defined above */
	u_char	v_threshold;	/* min ttl required to forward on vif */
	u_long	v_lcl_addr;	/* local interface address */
	u_long	v_rmt_addr;	/* remote address (tunnels only) */
	struct ipif_s *v_ipif;	/* pointer to logical interface  */
	struct grplst *v_lcl_groups;  /* list of local groups (phyints only) */
	kmutex_t v_cache_lock;	/* protects the next two entries */
	u_long	v_cached_group; /* last group looked-up (phyints only) */
	int	v_cached_result; /* last look-up result (phyints only) */
};

#define	GRPBLKSIZE	100
#define	GRPLSTLEN (GRPBLKSIZE/4)	/* number of groups in a grplst */

struct grplst {
	struct grplst	*gl_next;
	int		gl_numentries;
	u_long		gl_gaddr[GRPLSTLEN];
};


/*
 * The kernel's multicast route structure.
 */
struct mrt {
	struct mrt	*mrt_next;
	u_long		mrt_origin;	/* subnet origin of multicasts */
	u_long		mrt_originmask;	/* subnet mask for origin */
	vifi_t		mrt_parent;    	/* incoming vif */
	vifbitmap_t 	mrt_children;	/* outgoing children vifs */
	vifbitmap_t 	mrt_leaves;	/* subset of outgoing children vifs */
};


#define	MRTHASHSIZ	64
#if (MRTHASHSIZ & (MRTHASHSIZ - 1)) == 0	  /* from sys:route.h */
#define	MRTHASHMOD(h)	((h) & (MRTHASHSIZ - 1))
#else
#define	MRTHASHMOD(h)	((h) % MRTHASHSIZ)
#endif

#if defined(_KERNEL) && defined(__STDC__)

extern	int	ip_mrouter_cmd(int cmd, queue_t *q, char *data,
			int datalen);
extern	int	ip_mrouter_done(void);
extern	int	ip_mforward(ill_t *ill, ipha_t *ipha, mblk_t *mp);
extern	void	reset_mrt_vif_ipif(ipif_t *ipif);
extern	int	ip_mroute_stats(struct opthdr * optp, mblk_t *mp);
extern	int	ip_mroute_vif(struct opthdr * optp, mblk_t *mp);
extern	int	ip_mroute_mrt(struct opthdr * optp, mblk_t *mp);
extern	void	ip_mroute_decap(queue_t * q, mblk_t * mp);

extern	queue_t	*ip_g_mrouter;

#endif	/* defined(_KERNEL) && defined(__STDC__) */

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _NETINET_IP_MROUTE_H */
