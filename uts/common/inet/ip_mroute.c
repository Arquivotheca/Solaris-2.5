/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)ip_mroute.c	1.14	94/11/07 SMI"

/*
 * Procedures for the kernel part of DVMRP,
 * a Distance-Vector Multicast Routing Protocol.
 * (See RFC-1075.)
 *
 * Written by David Waitzman, BBN Labs, August 1988.
 * Modified by Steve Deering, Stanford, February 1989.
 * Modified by Mark J. Steiglitz, Stanford, May, 1991
 * Modified by Van Jacobson, LBL, January 1993
 *
 * MROUTING 1.4
 */

/* TODO
 * - TODO strlog
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

#include <inet/common.h>
#include <inet/mi.h>
#include <inet/nd.h>
#include <inet/arp.h>
#include <inet/mib2.h>
#include <inet/ip.h>
#include <inet/snmpcom.h>

#include <netinet/igmp.h>
#include <netinet/igmp_var.h>
#include <netinet/ip_mroute.h>
#include <inet/ip_multi.h>
#include <inet/ip_ire.h>

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

#include <common.h>
#include <mi.h>
#include <nd.h>
#include <arp.h>
#include <mib2.h>
#include <ip.h>

#include <igmp.h>
#include <igmp_var.h>
#include <ip_mroute.h>
#include <ip_multi.h>
#include <ip_ire.h>

#endif

extern mib2_ip_t	ip_mib;

#define	same(a1, a2) \
	(bcmp((caddr_t)(a1), (caddr_t)(a2), IP_ADDR_LEN) == 0)

static int	ip_mrouter_init(queue_t *q);
       int	ip_mrouter_done(   void   );
static int	add_vif(struct vifctl *vifcp);
static int	del_vif(vifi_t *vifip);
static int	add_lgrp(struct lgrplctl *gcp);
static int	del_lgrp(struct lgrplctl *gcp);
static int	grplst_member(struct vif *vifp, u_long gaddr);
static u_long	nethash(u_long addr);
static int	add_mrt(struct mrtctl *mrtcp);
static int	del_mrt(struct in_addr *origin);
static struct mrt *mrtfind(u_long origin);
static void	phyint_send(ipha_t *ipha, mblk_t *mp, struct vif *vifp, u32 dst);
static void	srcrt_send(ipha_t *ipha, mblk_t *mp, struct vif *vifp, u32 dst);
static void	encap_send(ipha_t *ipha, mblk_t *mp, struct vif *vifp, u32 dst);

#define LOG

#ifndef LOG_DEBUG
#define USE_MY_LOG
#define LOG_DEBUG 0
#endif

/*
 * Globals.  All but ip_mrouter and ip_mrtproto could be static,
 * except for netstat or debugging purposes.
 */
queue_t		*ip_g_mrouter  = nilp(queue_t);



int		ip_mrtproto = IGMP_DVMRP;    /* for netstat only */

static struct mrt	*mrttable[MRTHASHSIZ];
static struct vif	viftable[MAXVIFS];
struct mrtstat	mrtstat;

#define ENCAP_TTL 64

/* prototype IP hdr for encapsulated packets */
static ipha_t multicast_encap_iphdr = {
	IP_SIMPLE_HDR_VERSION,
	0,				/* tos */
	sizeof(ipha_t),			/* total length */
	0,				/* id */
	0,				/* frag offset */
	ENCAP_TTL, IPPROTO_ENCAP,	
	0,				/* checksum */
};

/*
 * Private variables.
 */
static vifi_t	   numvifs = 0;

/*
 * one-back cache used by multiencap_decap to locate a tunnel's vif
 * given a datagram's src ip address.
 */
static u_long last_encap_src;
static struct vif *last_encap_vif;
static kmutex_t last_encap_lock;	/* Protects the above */

static kmutex_t	   cache_lock;		/* Protects the next three variables */
static struct mrt *cached_mrt = nilp(struct mrt);
static u_long	   cached_origin;
static u_long	   cached_originmask;

/*
 * MT design:
 *	All access through ip_mrouter_cmd() are single-threaded by holding the
 *	write access on the ip module wide readers-writers lock. Since the
 *	the data path (ip_mforward) is holding the read side of the same
 *	lock the only concern is modifications to data in the data path.
 *	The data that gets modified is:
 *		cached_{mrt,origin,originmask}
 *		vipf->v_cached_{group,result}
 *		global statistics 
 * 	For the two first categories there are mutexes. One global for the
 * 	global route cache and one per vif structure for the per-vif
 *	group cache.
 *
 *	The statistics are currently not protected (They could
 *	go into the ill structure so that the queue synchronization would
 *	give of exclusive access for free.)
 */


#ifdef IP_DEBUG
	void show_packet (char *msg, mblk_t *mp);
	void dump_hdr_cksum(ipha_t *ipha);
	void dump_cksum(mblk_t *mp, int off);
	void dump_icmp_echo_pattern(mblk_t *mp, int off);
#endif /*IP_DEBUG*/
staticf void log();

/*
 * Handle DVMRP setsockopt commands to modify the multicast routing tables.
 */
int
ip_mrouter_cmd(cmd, q, data, datalen)
	int cmd;
	queue_t	*q;
	char *data;
	int datalen;
{
#ifdef lint
    datalen = datalen;
#endif

    if (cmd != DVMRP_INIT && q != ip_g_mrouter) return EACCES;

    switch (cmd) {
	case DVMRP_INIT:     return ip_mrouter_init(q);
	case DVMRP_DONE:     return ip_mrouter_done();
	case DVMRP_ADD_VIF:  return add_vif ((struct vifctl *)ALIGN32(data));
	case DVMRP_DEL_VIF:  return del_vif ((vifi_t *)ALIGN32(data));
	case DVMRP_ADD_LGRP: return add_lgrp((struct lgrplctl *)ALIGN32(data));
	case DVMRP_DEL_LGRP: return del_lgrp((struct lgrplctl *)ALIGN32(data));
	case DVMRP_ADD_MRT:  return add_mrt ((struct mrtctl *)ALIGN32(data));
	case DVMRP_DEL_MRT:  return del_mrt ((struct in_addr *)ALIGN32(data));
	default:             return EOPNOTSUPP;
    }
}

static int saved_ip_g_forward = -1;

/*
 * Enable multicast routing
 */
static int
ip_mrouter_init(q)
	queue_t	*q;
{
    ipc_t	*ipc = (ipc_t *)q->q_ptr;

    if (ip_g_mrouter != nilp(queue_t)) return EADDRINUSE;

    ip_g_mrouter = q;
    ipc->ipc_multi_router = 1;

    mutex_init(&cache_lock, "IP multicast forwarding cache", MUTEX_DEFAULT, 0);
    mutex_init(&last_encap_lock, "IP multicast decap lock", MUTEX_DEFAULT, 0);

    /* In order for tunnels to work we have to turn ip_g_forward on */
    if (!WE_ARE_FORWARDING) {
#ifdef LOG
	    if (ip_mrtdebug)
		    log(LOG_DEBUG, "ip_mrouter_init: turning on forwarding");
#endif 
	    saved_ip_g_forward = ip_g_forward;
	    ip_g_forward = IP_FORWARD_ALWAYS;
    }

#ifdef LOG
    if (ip_mrtdebug)
	log(LOG_DEBUG, "ip_mrouter_init");
#endif 
    return 0;
}

/*
 * Disable multicast routing
 */
int
ip_mrouter_done()
{
    vifi_t vifi;
    int i;
    ipc_t	*ipc = (ipc_t *)ip_g_mrouter->q_ptr;

    if (saved_ip_g_forward != -1) {
#ifdef LOG
	    if (ip_mrtdebug)
		    log(LOG_DEBUG, "ip_mrouter_done: turning off forwarding");
#endif 
	    ip_g_forward = saved_ip_g_forward;
	    saved_ip_g_forward = -1;
    }

    ipc->ipc_multi_router = 0;

    /*
     * Always clear cache when vifs change.
     * No need to get last_encap_lock since we are running as a writer.
     */
    last_encap_vif = 0;
    last_encap_src = 0;

    /*
     * For each phyint in use, free its local group list and
     * disable promiscuous reception of all IP multicasts.
     */
    for (vifi = 0; vifi < numvifs; vifi++) {
	if (viftable[vifi].v_lcl_addr != 0 &&
	    !(viftable[vifi].v_flags & VIFF_TUNNEL)) {
		ipif_t *ipif;
		struct grplst *grp, *next_grp;
		
		/* LINTED lint confusion: next_grp used before set */
		for (grp = viftable[vifi].v_lcl_groups; grp; grp = next_grp) {
			next_grp = grp->gl_next;
			mi_free((char *)grp);
		}
		ipif = viftable[vifi].v_ipif; 
		if (ipif) {
			(void)ip_delmulti(INADDR_ANY, ipif);
		} 
	}
    }
    bzero((caddr_t)viftable, sizeof(viftable));
    numvifs = 0;

    /*
     * Free any multicast route entries.
     */
    for (i = 0; i < MRTHASHSIZ; i++) {
	    struct mrt *mrt, *next_mrt;

	    /* LINTED lint confusion: next_mrt used before set */
	    for (mrt = mrttable[i]; mrt; mrt = next_mrt) {
		    next_mrt = mrt->mrt_next;
		    mi_free((char *)mrt);
	    }
    }
    bzero((caddr_t)mrttable, sizeof(mrttable));
    cached_mrt = nilp(struct mrt);

    mutex_destroy(&cache_lock);
    mutex_destroy(&last_encap_lock);

    ip_g_mrouter = nilp(queue_t);

#ifdef LOG
    if (ip_mrtdebug)
	log(LOG_DEBUG, "ip_mrouter_done");
#endif 

    return 0;
}

/*
 * Add a vif to the vif table
 */
static int
add_vif(vifcp)
    register struct vifctl *vifcp;
{
    register struct vif *vifp = viftable + vifcp->vifc_vifi;
    ipif_t	*ipif;
    int 	error;

    if (vifcp->vifc_vifi >= MAXVIFS)  return EINVAL;
    if (vifp->v_lcl_addr != 0) return EADDRINUSE;

    /* Find the interface with the local address */
    if (!(ipif = ipif_lookup_addr((u32)vifcp->vifc_lcl_addr.s_addr)))
	    return EADDRNOTAVAIL;

    if (vifcp->vifc_flags & VIFF_TUNNEL) {
	vifp->v_rmt_addr  = vifcp->vifc_rmt_addr.s_addr;
    }
    else {
	/* Make sure the interface supports multicast */
	if ((ipif->ipif_flags & IFF_MULTICAST) == 0) {
	    return EOPNOTSUPP;
	}
	/* Enable promiscuous reception of all IP multicasts from the if */
	error = ip_addmulti(INADDR_ANY, ipif);
	if (error) {
	    return error;
	}
    }

    vifp->v_flags     = vifcp->vifc_flags;
    vifp->v_threshold = vifcp->vifc_threshold;
    vifp->v_lcl_addr  = vifcp->vifc_lcl_addr.s_addr;
    vifp->v_ipif      = ipif;
    mutex_init(&vifp->v_cache_lock, "IP Multicast group cache", 
	       MUTEX_DEFAULT, 0);

    /* Adjust numvifs up if the vifi is higher than numvifs */
    if (numvifs <= vifcp->vifc_vifi) numvifs = vifcp->vifc_vifi + 1;

    /*
     * Always clear cache when vifs change.
     * No need to get last_encap_lock since we are running as a writer.
     */
    last_encap_vif = 0;
    last_encap_src = 0;

#ifdef LOG
    if (ip_mrtdebug)
	log(LOG_DEBUG, "add_vif #%d, lcladdr %x, %s %x, thresh %x",
	    vifcp->vifc_vifi, 
	    (int)ntohl(vifcp->vifc_lcl_addr.s_addr),
	    (vifcp->vifc_flags & VIFF_TUNNEL) ? "rmtaddr" : "mask",
	    (int)ntohl(vifcp->vifc_rmt_addr.s_addr),
	    vifcp->vifc_threshold);
#endif 
    
    return 0;
}

/*
 * Delete a vif from the vif table
 */
static int
del_vif(vifip)
    vifi_t *vifip;
{
    struct vif 	*vifp = viftable + *vifip;
    vifi_t 	vifi;

    if (*vifip >= numvifs) return EINVAL;
    if (vifp->v_lcl_addr == 0) return EADDRNOTAVAIL;

    if (!(vifp->v_flags & VIFF_TUNNEL)) {
	    ipif_t *ipif;
	    struct grplst *grp, *next_grp;
		
	    /* LINTED lint confusion: next_grp used before set */
	    for (grp = vifp->v_lcl_groups; grp; grp = next_grp) {
		    next_grp = grp->gl_next;
		    mi_free((char *)grp);
	    }

	    ipif = vifp->v_ipif;
	    if (ipif) {
		    (void)ip_delmulti(INADDR_ANY, ipif);
	    }
    }

    /*
     * Always clear cache when vifs change.
     * No need to get last_encap_lock since we are running as a writer.
     */
    last_encap_vif = 0;
    last_encap_src = 0;

    mutex_destroy(&vifp->v_cache_lock);
    bzero((caddr_t)vifp, sizeof (*vifp));

    /* Adjust numvifs down */
    for (vifi = numvifs; vifi != 0; vifi--) /* vifi is unsigned */
      if (viftable[vifi - 1].v_lcl_addr != 0) break;
    numvifs = vifi;

#ifdef LOG
    if (ip_mrtdebug)
      log(LOG_DEBUG, "del_vif %d, numvifs %d", *vifip, numvifs);
#endif 

    return 0;
}

/*
 * Add the multicast group in the lgrpctl to the list of local multicast
 * group memberships associated with the vif indexed by gcp->lgc_vifi.
 */
static int
add_lgrp(gcp)
    register struct lgrplctl *gcp;
{
    register struct vif *vifp = viftable + gcp->lgc_vifi;
    register struct grplst *grp, *prev_grp;

#ifdef LOG
    if (ip_mrtdebug)
      log(LOG_DEBUG,"add_lgrp %x on %d",
	  (int)ntohl(gcp->lgc_gaddr.s_addr), gcp->lgc_vifi);
#endif 

    if (gcp->lgc_vifi >= numvifs) return EINVAL;
    if (vifp->v_lcl_addr == 0 ||
       (vifp->v_flags & VIFF_TUNNEL)) return EADDRNOTAVAIL;

    /* try to find a group list block that has free space left */
    for (grp = vifp->v_lcl_groups, prev_grp = nilp(struct grplst)
	 ; grp && grp->gl_numentries == GRPLSTLEN
         ; prev_grp = grp, grp = grp->gl_next)
      ;

    if (grp == nilp(struct grplst)) {
	/* no group list block with free space was found */
	grp = (struct grplst *)ALIGN32(mi_zalloc(GRPBLKSIZE));
	if (grp == nilp(struct grplst)) {
	  return ENOBUFS;
	}
	if (prev_grp == nilp(struct grplst))
	  vifp->v_lcl_groups = grp;
	else
	  prev_grp->gl_next = grp;
    }

    grp->gl_gaddr[grp->gl_numentries++] = gcp->lgc_gaddr.s_addr;

    if (gcp->lgc_gaddr.s_addr == vifp->v_cached_group)
	vifp->v_cached_result = 1;

    return 0;
}

/*
 * Delete the the local multicast group associated with the vif
 * indexed by gcp->lgc_vifi.
 * Does not pullup the values from other blocks in the list unless the
 * current block is totally empty.  This is a ~bug.
 */

static int
del_lgrp(gcp)
    register struct lgrplctl *gcp;
{
    register struct vif *vifp = viftable + gcp->lgc_vifi;
    register u_long i;
    register struct grplst *grp, *prev_grp = nilp(struct grplst);
    int cnt;

#ifdef LOG
    if (ip_mrtdebug)
      log(LOG_DEBUG,"del_lgrp %x on %d",
	  (int)ntohl(gcp->lgc_gaddr.s_addr), gcp->lgc_vifi);
#endif 

    if (gcp->lgc_vifi >= numvifs) return EINVAL;
    if (vifp->v_lcl_addr == 0 ||
       (vifp->v_flags & VIFF_TUNNEL)) return EADDRNOTAVAIL;

    if (gcp->lgc_gaddr.s_addr == vifp->v_cached_group) 
	vifp->v_cached_result = 0;

    /* for all group list blocks */
    for (grp = vifp->v_lcl_groups; grp; prev_grp = grp, grp = grp->gl_next) {
	/* for all group addrs in an block */
	for (cnt = grp->gl_numentries, i = 0; i < cnt; i++)
	  /* if this is the addr to delete */
	  if (same(&gcp->lgc_gaddr, &grp->gl_gaddr[i])) {
	      grp->gl_numentries--;
	      cnt--;       
	      if (grp->gl_numentries == 0)  {
		  /* the block is now empty */
		  if (prev_grp) {
		      prev_grp->gl_next = grp->gl_next;
		  } else {
		      vifp->v_lcl_groups = grp->gl_next;
		  }
		  mi_free((char *)grp);
	      } else
		/* move all other group addresses down one address */
		for (; i < cnt; i++)
		  grp->gl_gaddr[i] = grp->gl_gaddr[i + 1];
	      return 0;
	  }
    }
    return EADDRNOTAVAIL;		/* not found */
}

/*
 * Return 1 if gaddr is a member of the local group list for vifp.
 */
static int
grplst_member(vifp, gaddr)
    struct vif *vifp;
    u_long	gaddr;
{
    register int i;
    register u_long *gl;
    register struct grplst *grp;

    mrtstat.mrts_grp_lookups++;

    mutex_enter(&vifp->v_cache_lock);
    if (gaddr == vifp->v_cached_group) {
	i = vifp->v_cached_result;
	mutex_exit(&vifp->v_cache_lock);
	return (i);
    }
    mutex_exit(&vifp->v_cache_lock);
    mrtstat.mrts_grp_misses++;

    for (grp = vifp->v_lcl_groups; grp; grp = grp->gl_next) {
      for (gl = &grp->gl_gaddr[0], i = grp->gl_numentries;
	   i; gl++, i--) {
	if (gaddr == *gl) {
	  mutex_enter(&vifp->v_cache_lock);
	  vifp->v_cached_group  = gaddr;
	  vifp->v_cached_result = 1;
	  mutex_exit(&vifp->v_cache_lock);
	  return 1;
	}
      }
    }
    mutex_enter(&vifp->v_cache_lock);
    vifp->v_cached_group  = gaddr;
    vifp->v_cached_result = 0;
    mutex_exit(&vifp->v_cache_lock);
    return 0;
}

/*
 * A simple hash function: returns MRTHASHMOD of the low-order octet of
 * the argument's network or subnet number.
 */ 
static u_long
nethash(addr)
    u_long	addr;
{
    addr &= ip_net_mask((u32)addr);
    return (MRTHASHMOD(addr));
}

/*
 * Add an mrt entry
 */
static int
add_mrt(mrtcp)
    struct mrtctl *mrtcp;
{
    struct mrt *rt;
    u_long hash;

    if (rt = mrtfind(mrtcp->mrtc_origin.s_addr)) {
#ifdef LOG
	if (ip_mrtdebug)
	  log(LOG_DEBUG,"add_mrt update o %x m %x p %x c %x l %x",
	      (int)ntohl(mrtcp->mrtc_origin.s_addr),
	      (int)ntohl(mrtcp->mrtc_originmask.s_addr),
	      mrtcp->mrtc_parent, mrtcp->mrtc_children, mrtcp->mrtc_leaves);
#endif 

	/* Just update the route */
	rt->mrt_parent = mrtcp->mrtc_parent;
	VIFM_COPY(mrtcp->mrtc_children, rt->mrt_children);
	VIFM_COPY(mrtcp->mrtc_leaves,   rt->mrt_leaves);
	return 0;
    }

#ifdef LOG
    if (ip_mrtdebug)
      log(LOG_DEBUG,"add_mrt o %x m %x p %x c %x l %x",
	  (int)ntohl(mrtcp->mrtc_origin.s_addr),
	  (int)ntohl(mrtcp->mrtc_originmask.s_addr),
	  mrtcp->mrtc_parent, mrtcp->mrtc_children, mrtcp->mrtc_leaves);
#endif 

    rt = (struct mrt *)ALIGN32(mi_alloc(sizeof(struct mrt), BPRI_LO));
    if (rt == 0) {
	return ENOBUFS;
    }

    /*
     * insert new entry at head of hash chain
     */
    rt->mrt_origin     = mrtcp->mrtc_origin.s_addr;
    rt->mrt_originmask = mrtcp->mrtc_originmask.s_addr;
    rt->mrt_parent     = mrtcp->mrtc_parent;
    VIFM_COPY(mrtcp->mrtc_children, rt->mrt_children); 
    VIFM_COPY(mrtcp->mrtc_leaves,   rt->mrt_leaves);     
    /* link into table */
    hash = nethash(mrtcp->mrtc_origin.s_addr);
    rt->mrt_next = mrttable[hash];
    mrttable[hash] = rt;

    return 0;
}

/*
 * Delete an mrt entry
 */
static int
del_mrt(origin)
    struct in_addr *origin;
{
    register struct mrt *rt, *prev_rt;
    register u_long hash = nethash(origin->s_addr);

#ifdef LOG
    if (ip_mrtdebug)
      log(LOG_DEBUG,"del_mrt orig %x",
	  (int)ntohl(origin->s_addr));
#endif 

    for (prev_rt = rt = mrttable[hash]
	 ; rt
	 ; prev_rt = rt, rt = rt->mrt_next) {
	if (origin->s_addr == rt->mrt_origin)
	    break;
    }
    if (!rt) {
	return ESRCH;
    }

    if (rt == cached_mrt)
        cached_mrt = nilp(struct mrt);

    if (prev_rt != rt) {	/* if moved past head of list */
	    prev_rt->mrt_next = rt->mrt_next;
    } else			/* delete head of list, it is in the table */
        mrttable[hash] = rt->mrt_next;
    mi_free((char *)rt);
    return 0;
}

/*
 * Find a route for a given origin IP address.
 */
static struct mrt *
mrtfind(origin)
    u_long origin;
{
    register struct mrt *rt;
    register u_int hash;

    mrtstat.mrts_mrt_lookups++;

    mutex_enter(&cache_lock);
    if (cached_mrt != nilp(struct mrt) &&
	(origin & cached_originmask) == cached_origin) {
	mutex_exit(&cache_lock);
	return (cached_mrt);
    }
    mutex_exit(&cache_lock);
    mrtstat.mrts_mrt_misses++;

    hash = nethash(origin);
    for (rt = mrttable[hash]; rt; rt = rt->mrt_next) {
	if ((origin & rt->mrt_originmask) == rt->mrt_origin) {
	    mutex_enter(&cache_lock);
	    cached_mrt        = rt;
	    cached_origin     = rt->mrt_origin;
	    cached_originmask = rt->mrt_originmask;
	    mutex_exit(&cache_lock);
	    return (rt);
	}
    }
    return nilp(struct mrt);
}

/*
 * IP multicast forwarding function. This function assumes that the packet
 * pointed to by "ip" has arrived on (or is about to be sent to) the interface
 * pointed to by "ill", and the packet is to be relayed to other networks
 * that have members of the packet's destination IP multicast group.
 *
 * The packet is returned unscathed to the caller, unless it is tunneled
 * or erroneous, in which case a non-zero return value tells the caller to
 * discard it.
 */

#define IP_HDR_LEN  20	/* # bytes of fixed IP header (excluding options) */
#define TUNNEL_LEN  12  /* # bytes of IP option for tunnel encapsulation  */

int
ip_mforward(ill, ipha, mp)
    ill_t	*ill;	/* Incomming physical interface */
    ipha_t	*ipha;
    mblk_t	*mp;
{
    register struct mrt *rt;
    register struct vif *vifp;
    register vifi_t vifi;
    register u_char *ipoptions;
    u_long *ipopts;	
    u_long tunnel_src;
    u_long dst;

#ifdef LOG
    if (ip_mrtdebug > 1)
      log(LOG_DEBUG, "ip_mforward: src %x, dst %x, ill %s",
	  (int)ntohl(ipha->ipha_src), (int)ntohl(ipha->ipha_dst), 
	  ill->ill_name);
#endif 

    dst = ipha->ipha_dst;

    /*
     * Don't forward a packet with time-to-live of zero or one,
     * or a packet destined to a local-only group. This will not
     * drop packets arriving over a tunnel since we don't know 
     * the destination address yet. We do it here for performace
     * and redo it in the tunnel case.
     */
    if (CLASSD(dst) && 
	(ipha->ipha_ttl <= 1 ||
	 ntohl(dst) <= (u_long)INADDR_MAX_LOCAL_GROUP)) {
#ifdef LOG
	    if (ip_mrtdebug > 1)
		    log(LOG_DEBUG, "ip_mforward: dropped ttl %d, dst 0x%x",
			ipha->ipha_ttl, (int)ntohl(dst));
#endif 

	mp->b_prev = NULL;
	return 0;
    }
    if ((tunnel_src = (u32)mp->b_prev) != 0) {
	/*
	 * Packet arrived over encapsulation tunnel.
	 */
	mp->b_prev = NULL;
    } else if ((ipha->ipha_version_and_hdr_length & 0xf) < 
	(u_long)(IP_HDR_LEN + TUNNEL_LEN) >> 2 ||
	(ipoptions = (u_char *)(ipha + 1))[1] != IPOPT_LSRR ) {
	/*
	 * Packet arrived via a physical interface.
	 */
	tunnel_src = 0;
    }
    else {
	/*
	 * Packet arrived through a tunnel.
	 *
	 * A tunneled packet has a single NOP option and a two-element
	 * loose-source-and-record-route (LSRR) option immediately following
	 * the fixed-size part of the IP header.  At this point in processing,
	 * the IP header should contain the following IP addresses:
	 *
	 *	original source          - in the source address field
	 *	destination group        - in the destination address field
	 *	remote tunnel end-point  - in the first  element of LSRR
	 *	one of this host's addrs - in the second element of LSRR
	 *
	 * NOTE: RFC-1075 would have the original source and remote tunnel
	 *	 end-point addresses swapped.  However, that could cause
	 *	 delivery of ICMP error messages to innocent applications
	 *	 on intermediate routing hosts!  Therefore, we hereby
	 *	 change the spec.
	 */
	u_long ip_len;
	u32	sum;
	
	/* 
	 * Verify the checksum since ip_rput does not do this
	 */
	sum = ip_csum_hdr(ipha);
	if (sum) {
		BUMP_MIB(ip_mib.ipInCksumErrs);
		return 1;
	}
	/*
	 * Verify that the tunnel options are well-formed.
	 */
	ipopts = (u_long *)ALIGN32(ipoptions);
	if (ipoptions[0] != IPOPT_NOP ||
	    ipoptions[2] != 11 ||	/* LSRR option length   */
	    ipoptions[3] != 8 ||	/* LSRR address pointer before being
					 * modified by ip_rput_options */
	    (tunnel_src = ipopts[1]) == 0) {
	    mrtstat.mrts_bad_tunnel++;
#ifdef LOG
	    if (ip_mrtdebug)
		log(LOG_DEBUG,
		"ip_mforward: bad tunnel from %x (%x %x %x %x %x %x)",
		(int)ntohl(ipha->ipha_src),
		ipoptions[0], ipoptions[1], ipoptions[2], ipoptions[3],
		(int)ntohl(ipopts[1]), (int)ntohl(ipopts[2]));
#endif 
	    return 1;
	}

	/*
	 * NOTE copy the current next hop from the LSRR to the dstination field
	 * This is needed since ip_rput does not modify the packet before
	 * passing it here.
	 */
	dst = ipha->ipha_dst = ipopts[2];

	/*
	 * Delete the tunnel options from the packet.
	 */
  	ovbcopy((caddr_t)(ipoptions + TUNNEL_LEN), (caddr_t)ipoptions,
	      (unsigned)(mp->b_wptr - mp->b_rptr - (IP_HDR_LEN + TUNNEL_LEN)));
	mp->b_wptr	-= TUNNEL_LEN;
	ip_len		=  ntohs(ipha->ipha_length);
	ip_len      	-= TUNNEL_LEN;
	ipha->ipha_length = htons(ip_len);
	ipha->ipha_version_and_hdr_length -= TUNNEL_LEN >> 2;
	/* Update the checksum */
	ipha->ipha_hdr_checksum = 0;
	ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);

	/*
	 * Don't forward a packet with time-to-live of zero or one,
	 * or a packet destined to a local-only group.
	 */
	if (ipha->ipha_ttl <= 1 ||
	    ntohl(dst) <= (u_long)INADDR_MAX_LOCAL_GROUP) {
#ifdef LOG
		if (ip_mrtdebug > 1)
			log(LOG_DEBUG, "ip_mforward: dropped ttl %d, dst 0x%x",
			    ipha->ipha_ttl, (int)ntohl(dst));
#endif 
		return (int)tunnel_src;
	}
    }

    mrtstat.mrts_fwd_in++;
#ifdef LOG
	if (ip_mrtdebug > 1)
		log(LOG_DEBUG, "ip_mforward: arrived via a %s",
		    tunnel_src ? "tunnel" : "physical if");
#endif 
    /*
     * Don't forward if we don't have a route for the packet's origin.
     */
    if (!(rt = mrtfind(ipha->ipha_src))) {
	mrtstat.mrts_no_route++;
#ifdef LOG
	if (ip_mrtdebug)
	    log(LOG_DEBUG, "ip_mforward: no route for %x",
		(int)ntohl(ipha->ipha_src));
#endif 
	return (int)tunnel_src;
    }

    /*
     * Don't forward if it didn't arrive from the parent vif for its origin.
     * Notes: v_ipif is the physical interface for both tunnels and
     * physical vifs, so the first part of the if catches wrong physical 
     * interface; v_rmt_addr is zero for non-tunneled packets so
     * the 2nd part catches both packets that arrive via a tunnel
     * that shouldn't and packets that arrive via the wrong tunnel.
     */
    vifi = rt->mrt_parent;
    if (viftable[vifi].v_ipif->ipif_ill != ill ||
	viftable[vifi].v_rmt_addr != tunnel_src) {
#ifdef LOG
	    if (ip_mrtdebug > 1)
		    log(LOG_DEBUG, "ip_mforward: arrived on wrong if,addr (%s, 0x%x) - should be (%s, 0x%x)",
			ill->ill_name,
			(int)ntohl(tunnel_src),
			viftable[vifi].v_ipif->ipif_ill->ill_name,
			(int)ntohl(viftable[vifi].v_rmt_addr));
#endif 
	    mrtstat.mrts_wrong_if++;
	    return (int)tunnel_src;
    }

    /*
     * For each vif, decide if a copy of the packet should be forwarded.
     * Forward if:
     *		- the ttl exceeds the vif's threshold AND
     *		- the vif is a child in the origin's route AND
     *		- ( the vif is not a leaf in the origin's route OR
     *		    the destination group has members on the vif )
     *
     * (This might be speeded up with some sort of cache -- someday.)
     */
    for (vifp = viftable, vifi = 0; vifi < numvifs; vifp++, vifi++) {
	if (ipha->ipha_ttl > vifp->v_threshold &&
	    VIFM_ISSET(vifi, rt->mrt_children) &&
	    (!VIFM_ISSET(vifi, rt->mrt_leaves) ||
	     grplst_member(vifp, dst))) {
		mrtstat.mrts_fwd_out++;
		if (vifp->v_flags & VIFF_SRCRT)
			srcrt_send(ipha, mp, vifp, dst);
		else if (vifp->v_flags & VIFF_TUNNEL) 
			encap_send(ipha, mp, vifp, dst);
		else	
			phyint_send(ipha, mp, vifp, dst);
	}
    }

    return (int)tunnel_src;
}

static void
phyint_send(ipha, mp, vifp, dst)
	ipha_t	*ipha;
	mblk_t	*mp;
	struct vif *vifp;
	u32	dst;
{
    register mblk_t *mp_copy;
    ipif_t	*ipif;

#ifdef lint
    ipha = ipha;
#endif

    ipif = vifp->v_ipif;
    mp_copy = copymsg(mp);	/* TODO could copy header and dup rest */
    if (mp_copy == nilp(mblk_t)) {
	    mrtstat.mrts_fwd_drop++;    
	    return;
    }
    /* Need to loop back to members on the outgoing interface */
    if (ilm_lookup_exact(ipif, dst)) { 	
	    /* The packet is not yet reassembled thus we need to pass it
	     * to ip_rput_local for checksum verification and reassembly
	     * (and fanout the user stream.)
	     */
	    mblk_t *mp_loop;
	    ire_t	*ire;

#ifdef LOG
	    if (ip_mrtdebug > 1)
		    log(LOG_DEBUG, "phyint_send loopback");
#endif 
	    mp_loop = copymsg(mp_copy);
	    ire = ire_lookup_exact(~0, IRE_BROADCAST, 0);
	    if (mp_loop && ire)
		    ip_rput_local(ipif->ipif_ill->ill_rq, mp_loop,
				  (ipha_t *)ALIGN32(mp_loop->b_rptr),
				  ire);
#ifdef LOG
	    else
		    log(LOG_DEBUG, "phyint_send: mp_loop 0x%x, ire 0x%x\n",
			   mp_loop, ire);
#endif
    } 
    ip_rput_forward_multicast(dst, mp_copy, ipif);
#ifdef LOG
    if (ip_mrtdebug > 1)
	log(LOG_DEBUG, "phyint_send on vif %d", vifp-viftable);
#endif 
}

/* ARGSUSED */
static void
srcrt_send(ipha, mp, vifp, dst)
	ipha_t 	*ipha;
	mblk_t	*mp;
	struct vif *vifp;
	u32	dst;
{
    mblk_t 	*mp_copy, *mp_opts;
    ipha_t 	*ipha_copy;
    u_char 	*cp;
    u_long	ip_len;
    u32		orig_optlen;	/* In bytes */
    u32		sum;

    /*
     * Make sure that adding the tunnel options won't exceed the
     * maximum allowed number of option bytes.
     */
    if ((ipha->ipha_version_and_hdr_length & 0xf) > 
	(u_long)(60 - TUNNEL_LEN) >> 2) {
	mrtstat.mrts_cant_tunnel++;
#ifdef LOG
	if (ip_mrtdebug)
	    log(LOG_DEBUG, "tunnel_send: no room for tunnel options, from %x",
					(int)ntohl(ipha->ipha_src));
#endif 
	return;
    }

    /* Verify the checksum since this might not have been done yet */
    sum = ip_csum_hdr(ipha);	
    if (sum) {
	    BUMP_MIB(ip_mib.ipInCksumErrs);
	    return;
    }

    mp_copy = copymsg(mp);	/* TODO could copy header and dup rest */
    if (mp_copy == nilp(mblk_t)) {
	    mrtstat.mrts_fwd_drop++;    
	    return;
    }
    ipha_copy = (ipha_t *)ALIGN32(mp_copy->b_rptr);

    ipha_copy->ipha_dst = vifp->v_rmt_addr;
    /*
     * Adjust the ip header length to account for the tunnel options.
     */
    ip_len		=  ntohs(ipha_copy->ipha_length);
    ip_len      	+= TUNNEL_LEN;
    ipha_copy->ipha_length = htons(ip_len);
    ipha_copy->ipha_version_and_hdr_length += TUNNEL_LEN >> 2;
    
    /* Prepare for checksum computation */
    orig_optlen = (ipha->ipha_version_and_hdr_length -
	    (u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS)) << 2;
    ipha_copy->ipha_hdr_checksum = 0;
    
    /* We have to put all of the ip header in one mblk since ip_forward 
     * assumes this for checksum calculation.
     */
    mp_opts = allocb(IP_HDR_LEN + TUNNEL_LEN + orig_optlen, BPRI_HI);
    if (mp_opts == nilp(mblk_t)) {
	    freemsg(mp_copy);
	    mrtstat.mrts_fwd_drop++;    
	    return;
    }
    mp_opts->b_datap->db_type = M_DATA;
    /*
     * 'Delete' the base ip header from the mp_copy chain
     */
    mp_copy->b_rptr += IP_HDR_LEN;
    /*
     * Make mp_opts be the new head of the packet chain.
     * Any options of the packet will be copied in after the tunnel options.
     */
    mp_opts->b_cont = mp_copy;
    mp_opts->b_wptr += IP_HDR_LEN + TUNNEL_LEN + orig_optlen;
    /*
     * Copy the base ip header from the mp_copy chain to the new head mblk
     */
    bcopy((caddr_t)ipha_copy, (char *)mp_opts->b_rptr, IP_HDR_LEN);
    /*
     * Add the NOP and LSRR after the base ip header
     */
    cp = mp_opts->b_rptr + IP_HDR_LEN;
    *cp++ = IPOPT_NOP;
    *cp++ = IPOPT_LSRR;
    *cp++ = 11; /* LSRR option length */
    *cp++ = 8;  /* LSSR pointer to second element */
    /* add local tunnel end-point */
    *(u32 *)ALIGN32(cp) = vifp->v_lcl_addr;
    cp += 4;
    *(u32 *)ALIGN32(cp) = ipha->ipha_dst;	/* destination group */
    cp += 4;
    /*
     * Copy the original ip options from the mp_copy chain to the new head mblk
     */
    bcopy((caddr_t)mp_copy->b_rptr, (char *)cp, orig_optlen);
    /*
     * Remove the original options from the mp_copy chain
     */
    mp_copy->b_rptr += orig_optlen;

    /* Update the checksum by recalculating it
     */
    ipha_copy = (ipha_t *)ALIGN32(mp_opts->b_rptr);
    ipha_copy->ipha_hdr_checksum = ip_csum_hdr(ipha_copy);

    /* TODO should we handle machines with only tunnels?
     * (We can handle machines with pt-pt links since these support 
     * IFF_MULTICAST thus for uniformity ... but hopefully multicast tunneling
     * will go away.)
     * If so we might want to loop back to members on the outgoing vif...
     */

    /* Make it look like the packet just arrived by passing it to ip_rput 
     * which can handle M_BREAK messages provided that b_next contains the
     * destination.
     * Note: this requires that ip_g_forward is turned on. See ip_mrouter_init
     */
    /* Allocate an empty M_BREAK to avoid problems with the unclean b_next */
    mp = allocb(0, BPRI_HI);
    if (!mp) {
	    BUMP_MIB(ip_mib.ipOutDiscards);
	    freemsg(mp_opts);
	    return;
    }
    mp->b_datap->db_type = M_BREAK;
    mp->b_cont = mp_opts;
    mp_opts->b_next = (mblk_t *)vifp->v_rmt_addr; /* ipha_copy->ipha_dst */
    put(vifp->v_ipif->ipif_ill->ill_rq, mp);

#ifdef LOG
    if (ip_mrtdebug > 1)
	log(LOG_DEBUG, "tunnel_send on vif %d", vifp-viftable);
#endif 
}

/* ARGSUSED */
static void
encap_send(ipha, mp, vifp, dst)
	ipha_t 	*ipha;
	mblk_t	*mp;
	struct vif *vifp;
	u32	dst;
{
	mblk_t 	*mp_copy;
	ipha_t 	*ipha_copy;
	u_long	len;

	len = ntohs(ipha->ipha_length);
	/*
	 * copy the old packet & pullup it's IP header into the
	 * new mbuf so we can modify it.  Try to fill the new
	 * mbuf since if we don't the ethernet driver will.
	 */
	mp_copy = allocb(32 + sizeof(multicast_encap_iphdr), BPRI_MED);
	if (mp_copy == NULL)
		return;
	mp_copy->b_rptr += 32;
	mp_copy->b_wptr = mp_copy->b_rptr + sizeof(multicast_encap_iphdr);
	if ((mp_copy->b_cont = copymsg(mp)) == NULL) {
		freeb(mp_copy);
		return;
	}
	/*
	 * fill in the encapsulating IP header.
	 */
	ipha_copy = (ipha_t *)ALIGN32(mp_copy->b_rptr);
	*ipha_copy = multicast_encap_iphdr;
	ipha_copy->ipha_length = htons(len + sizeof (ipha_t));
	ipha_copy->ipha_src = vifp->v_lcl_addr;
	ipha_copy->ipha_dst = vifp->v_rmt_addr;

	/*
	 * turn the encapsulated IP header back into a valid one.
	 */
	ipha = (ipha_t *)ALIGN32(mp_copy->b_cont->b_rptr);
	ipha->ipha_ttl--;
	ipha->ipha_hdr_checksum = 0;
	ipha->ipha_hdr_checksum = ip_csum_hdr(ipha);

	/*
	 * Feed into ip_wput which will set the ident field and 
	 * checksum the encapsulating header.
	 */
	put(vifp->v_ipif->ipif_ill->ill_wq, mp_copy);
}

/*
 * De-encapsulate a packet and feed it back through ip input (this
 * routine is called whenever IP gets a packet with proto type
 * IPPROTO_ENCAP and a local destination address).
 */
void
ip_mroute_decap(q, mp)
	queue_t	*q;
	mblk_t	*mp;
{
	ipha_t	*ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	ipha_t	*ipha_encap;
	register int hlen = IPH_HDR_LENGTH(ipha);
	register struct vif *vifp;
	u32 src;

	/*
	 * dump the packet if it's not to a multicast destination or if
	 * we don't have an encapsulating tunnel with the source.
	 * Note:  This code assumes that the remote site IP address
	 * uniquely identifies the tunnel (i.e., that this site has
	 * at most one tunnel with the remote site).
	 */
	ipha_encap = (ipha_t *)ALIGN32((char *)ipha + hlen);
	if (!CLASSD(ipha_encap->ipha_dst)) {
		++mrtstat.mrts_bad_tunnel;
		freemsg(mp);
		return;
	}
	src = ipha->ipha_src;
	mutex_enter(&last_encap_lock);
	if (src != last_encap_src) {
		register struct vif *vife;

		vifp = viftable;
		vife = vifp + numvifs;
		last_encap_src = src;
		last_encap_vif = 0;
		for ( ; vifp < vife; ++vifp)
			if (vifp->v_rmt_addr == src) {
				if ((vifp->v_flags & (VIFF_TUNNEL|VIFF_SRCRT))
				    == VIFF_TUNNEL)
					last_encap_vif = vifp;
				break;
			}
	}
	if ((vifp = last_encap_vif) == 0) {
		mutex_exit(&last_encap_lock);
		mrtstat.mrts_bad_tunnel++;
		freemsg(mp);
#ifdef LOG
		if (ip_mrtdebug)
			log(LOG_DEBUG, "ip_mforward: no tunnel with %u",
			    (int)ntohl(src));
#endif
		return;
	}
	mutex_exit(&last_encap_lock);
	/*
	 * Need to pass in the tunnel source to ip_mforward (so that it can
	 * verify that the packet arrived over the correct vif.)
	 * We use b_prev to pass this information. This is safe
	 * since the ip_rput either free's the packet or passes it
	 * to ip_mforward.
	 */
	mp->b_prev = (mblk_t *)src;
	mp->b_rptr += hlen;
	/*
	 * Feed back into ip_rput as an M_DATA.
	 */
	ip_rput(q, mp);
}

/* 
 * Remove all records with v_ipif == ipif
 * Called when an interface goes away (stream closed)
 */
void
reset_mrt_vif_ipif(ipif)
	ipif_t	*ipif;
{
	vifi_t	vifi, tmp_vifi;

	/* Can't check vifi >= 0 since vifi_t is unsigned! */
	for (vifi = numvifs; vifi != 0; vifi--) {
		tmp_vifi = vifi - 1 ;
		if (viftable[tmp_vifi].v_ipif == ipif) {
			(void)del_vif(&tmp_vifi);
		}
	}
}

#ifdef USE_MY_LOG
/* TODO log() not avail in SVr4 */
/*VARARGS2*/
staticf void
log(level, fmt, a,b,c,d,f,g,h,i,j,k,l)
	int level;
	char *fmt;
{
#ifdef lint
	level = level;
#endif
	printf(fmt, a,b,c,d,f,g,h,i,j,k,l);
	printf("\n");
}
#endif

ip_mroute_stats(optp, mp)
	struct opthdr 	*optp;
	mblk_t		*mp;
{
	optp->level = EXPER_DVMRP;
	optp->name = 0;
	if (!snmp_append_data(mp, (char *)&mrtstat, sizeof(mrtstat))){
		ip0dbg(("ip_mroute_stats: failed %d bytes\n", 
			  sizeof(mrtstat)));
		return 0;
	}
	return 1;
}

ip_mroute_vif(optp, mp)
	struct opthdr 	*optp;
	mblk_t		*mp;
{
	struct vifinfo 	vi;
	vifi_t		vifi;
	struct grplst	*grp;
	int		i;
	int		had_members;

	optp->level = EXPER_DVMRP;
	optp->name = EXPER_DVMRP_VIF;

	for (vifi = 0; vifi < numvifs; vifi++) {
		if (viftable[vifi].v_lcl_addr == 0)
			continue;
		vi.vifi_vifi = vifi;
		vi.vifi_flags = viftable[vifi].v_flags;
		vi.vifi_threshold = viftable[vifi].v_threshold;
		vi.vifi_lcl_addr.s_addr = viftable[vifi].v_lcl_addr;
		vi.vifi_rmt_addr.s_addr = viftable[vifi].v_rmt_addr;
		if (viftable[vifi].v_flags & VIFF_TUNNEL) {
			vi.vifi_grp_addr.s_addr = 0;
			if (!snmp_append_data(mp, (char *)&vi, sizeof(vi))) {
				ip0dbg(("ip_mroute_vif: failed %d bytes\n", 
					  sizeof(vi)));
				return 0;
			} 
			continue;
		}
		had_members = 0;
		/* for all group list mblk */
		for (grp = viftable[vifi].v_lcl_groups; grp; 
		     grp = grp->gl_next) {
			/* for all group addrs in an mblk */
			for (i = 0; i < grp->gl_numentries; i++) {
				vi.vifi_grp_addr.s_addr = grp->gl_gaddr[i];
				had_members++;
				if (!snmp_append_data(mp, (char *)&vi, 
						      sizeof(vi))) {
					ip0dbg(("ip_mroute_vif: failed %d bytes\n", 
						  sizeof(vi)));
					return 0;
				}
			}
		}
		if (!had_members) {
			vi.vifi_grp_addr.s_addr = 0;
			if (!snmp_append_data(mp, (char *)&vi, sizeof(vi))) {
				ip0dbg(("ip_mroute_vif: failed %d bytes\n", 
					  sizeof(vi)));
				return 0;
			} 
		}
	}
	return 1;
}

ip_mroute_mrt(optp, mp)
	struct opthdr 	*optp;
	mblk_t		*mp;
{
	int		i;
	struct mrt	*rt;
	struct mrtctl	mrtc;

	optp->level = EXPER_DVMRP;
	optp->name = EXPER_DVMRP_MRT;

	/* Loop over all has buckets and their chains */
	for (i = 0; i < MRTHASHSIZ; i++) {
		for (rt = mrttable[i]; rt; rt = rt->mrt_next) {
			mrtc.mrtc_origin.s_addr = rt->mrt_origin;
			mrtc.mrtc_originmask.s_addr = rt->mrt_originmask;
			mrtc.mrtc_parent = rt->mrt_parent;
			VIFM_COPY(rt->mrt_children, mrtc.mrtc_children); 
			VIFM_COPY(rt->mrt_leaves, mrtc.mrtc_leaves);     

			if (!snmp_append_data(mp, (char *)&mrtc, 
					      sizeof(mrtc))) {
				ip0dbg(("ip_mroute_mrt: failed %d bytes\n", 
					  sizeof(mrtc)));
				return 0;
			} 
		}
	}
	return 1;
}

#ifdef IP_DEBUG
void
dump_hdr_cksum(ipha)
	ipha_t	*ipha;
{
	u32	sum;
	u16	* uph = ((u16 *)ipha);
	u32	u1;
	int	i;

	u1 = ipha->ipha_version_and_hdr_length - (u8)((IP_VERSION << 4));
	u1 *= 2;
	sum = 0;
	for (i = 0; i < u1; i++) {
		printf("	%d: %x + %x = %x\n",
		       i, (int)sum, (int)uph[i], (int)(sum + uph[i]));
		sum += uph[i];
	}
	sum = (sum & 0xFFFF) + (sum >> 16);
	printf("fold: %x, ", (int)sum);
	sum = ~(sum + (sum >> 16)) & 0xFFFF;
	printf("%x\n", (int)sum);
}

void
dump_cksum(mp, off)
	mblk_t *mp;
	int off;
{
	u32	sum;
	u16	* uph;
	int	i;

	if (off & 1) {
		printf("dump_cksum: bad offset %d\n", off);
		return;
	}
	sum = 0; i = 0;
	for (; mp; mp = mp->b_cont) {
		uph = (u16 *)ALIGN16(mp->b_rptr + off);
		off = 0;
		if ((mp->b_wptr - (u8 *)uph) & 0x1)
			printf("dump_cksum: bad mblk length\n");
		printf(" mblk len %d\n", mp->b_wptr - (u8 *)uph);
		while (uph < (u16 *)ALIGN16(mp->b_wptr)) {
			printf("	%d: %x + %x = %x\n",
			       i, (int)sum, (int)*uph, (int)(sum + *uph));
			sum += *uph;
			uph++;
			i++;
		}
	}
	sum = (sum & 0xFFFF) + (sum >> 16);
	printf("fold: %x, ", (int)sum);
	sum = ~(sum + (sum >> 16)) & 0xFFFF;
	printf("%x\n", (int)sum);
}

void
dump_icmp_echo_pattern(mp, off)
	mblk_t *mp;
	int off;
{
	u_char	* uph;
	u_long	i;

	i = 0;
	i += 8; off += 8;	/* Skip timestamp portion */
	for (; mp; mp = mp->b_cont) {
		u_char *mblkstart;
		u_char *start = nilp(u_char);	/* Start of block of wrong bytes */
		int	wrong;	/* Amount wrong in case there it is a constant
				   offset */
		int	diff;	/* 1 if there is a constant offset */

		mblkstart = uph = mp->b_rptr + off;
		if (uph > mp->b_wptr) {
			off -= (mp->b_wptr - mp->b_rptr);
			continue;
		}
		off = 0;
		printf("dump_icmp_echo_pattern: mblk len %d\n", 
		       mp->b_wptr - uph);
		while (uph < mp->b_wptr) {
			if (*uph != (i & 0xff)) {
				if (!start) {
					printf("	%d (%d): %x should be %x\n",
					       (int)i, (int)(uph - mblkstart),
					       (int)uph[0], (int)i&0xff);
					start = uph;
					wrong = uph[0] - (i & 0xff);
					diff = 1;
				} else if (*uph != ((wrong + i) & 0xff)) {
					diff = 0;
				}
			} else {
				printf(" %d bytes wrong:", uph - start);
				if (diff)
					printf(" by %d\n", wrong);
				else
					printf(" not a constant difference\n");
				if (uph < start)
					return;
				start = nilp(u_char);
			}
					
			uph++;
			i++;
		}
		if (start) {
			printf(" %d bytes wrong:", uph - start);
			if (diff)
				printf(" by %d\n", wrong);
			else
				printf(" not a constant difference\n");
			start = nilp(u_char);
		}
	}
}

void
show_packet (msg, mp)
	char	* msg;
	mblk_t	* mp;
{
reg	ipha_t	* ipha;
	u32	optlen;

	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	if (msg  &&  *msg)
		printf("%s", msg);
	if ((mp->b_wptr - mp->b_rptr) < sizeof (ipha_t)) {
		printf("show_packet: runt packet\n");
		return;
	}
	optlen = ipha->ipha_version_and_hdr_length -
		(u8)((IP_VERSION << 4) + IP_SIMPLE_HDR_LENGTH_IN_WORDS);
	if (optlen*4 + sizeof (ipha_t) > mp->b_wptr - mp->b_rptr) {
		printf("show_packet: runt packet %d - %d\n",
		       optlen, mp->b_wptr - mp->b_rptr);
		return;
	}
	printf("version %d hdr_length %d tos %d\n",
	       ipha->ipha_version_and_hdr_length >> 4,
	       ipha->ipha_version_and_hdr_length & 0xF,
	       ipha->ipha_type_of_service);

	printf("length %d ident %d offset 0x%x\n",
	       ntohs(ipha->ipha_length), ntohs(ipha->ipha_ident), 
	       (int)ntohs(ipha->ipha_fragment_offset_and_flags));

	printf("ttl %d protocol %d checksum 0x%x\n",
	       ipha->ipha_ttl, ipha->ipha_protocol,
	       (int)ntohs(ipha->ipha_hdr_checksum));

	printf("src 0x%x dst 0x%x\n", (int)ntohl(ipha->ipha_src),
	       (int)ntohl(ipha->ipha_dst));
	if (optlen) {
		u32 *up = (u32 *)&ipha[1];

		printf("options ");
		while (optlen != 0) {
			printf("0x%x ", (int)*up++);
			optlen--;
		}
	}
}
#endif /*IP_DEBUG*/
