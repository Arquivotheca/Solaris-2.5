/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)ip_ire.c	1.23	95/02/21 SMI"

/*
 * This file contains routines that manipulate Internet Routing Entries (IREs).
 */

#ifndef	MI_HDRS
#include <sys/types.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strlog.h>
#include <sys/tihdr.h>
#include <sys/tiuser.h>
#include <sys/dlpi.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>

#include <sys/systm.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>

#include <inet/common.h>
#include <inet/mi.h>
#include <inet/ip.h>
#include <inet/ip_if.h>

#else

#include <types.h>
#include <stream.h>
#include <stropts.h>
#include <strlog.h>
#include <tihdr.h>
#include <tiuser.h>
#include <dlpi.h>

#include <socket.h>
#include <if.h>
#include <in.h>

#include <common.h>
#include <mi.h>
#include <ip.h>

#endif

#ifndef	STRMSGSZ
#define	STRMSGSZ	4096
#endif

#ifndef	DB_REF_REDZONE
#define	DB_REF_REDZONE	120
#endif

	int	ip_ire_advise(   queue_t * q, mblk_t * mp   );
	int	ip_ire_delete(   queue_t * q, mblk_t * mp   );
	int	ip_ire_report(   queue_t * q, mblk_t * mp, caddr_t arg   );
staticf	void	ip_ire_report_ire(   ire_t * ire, char * mp   );
	void	ip_ire_req(   queue_t * q, mblk_t * mp   );
	ire_t	* ire_add(   ire_t * ire   );
	void	ire_add_then_put(   queue_t * q, mblk_t * mp   );
	ire_t	* ire_create(   u_char * addr, u_char * mask,
			u_char * src_addr, u_char * gateway, u_int max_frag,
			mblk_t * ll_hdr_mp, queue_t * rfq, queue_t * stq,
			u_int type, u_long rtt, u_int ll_hdr_len   );
	ire_t	** ire_create_bcast(   ipif_t * ipif, u32 addr,
			ire_t ** irep   );
	void	ire_delete(   ire_t * ire   );
	void	ire_delete_routes(   ire_t * ire   );
staticf void	ire_delete_route_gw(   ire_t * ire, char * cp   );
staticf void	ire_delete_route_match(   ire_t * ire, char * cp   );
	void	ire_expire(   ire_t * ire, char * arg   );
staticf void	ire_fastpath(   ire_t * ire   );
	void	ire_fastpath_update(   ire_t * ire, char * arg   );
	ire_t	* ire_lookup(   u32 addr   );
	ire_t	* ire_lookup_broadcast(   u32 addr, ipif_t * ipif   );
	ire_t	* ire_lookup_exact(   u32 addr, u_int type, u32 gw_addr   );
	ire_t	* ire_lookup_local(   void   );
	ire_t	* ire_lookup_loop(   u32 dst, u32 * gw   );
	ire_t	* ire_lookup_myaddr(   u32 addr   );
	ire_t	* ire_lookup_noroute(   u32 addr   );
	ire_t	* ire_lookup_interface(   u32 addr, u_int ire_type_mask   );
	ire_t	* ire_lookup_ipif(ipif_t * ipif, u_int type, u32 addr, u32 src_addr);
staticf	ire_t	** ire_net_irep(   u32 addr   );
	void	ire_pkt_count(   ire_t * ire, char * ippc   );
	ipif_t	* ire_interface_to_ipif(   ire_t * ire   );
	ill_t	* ire_to_ill(   ire_t * ire   );
	ipif_t	* ire_to_ipif(   ire_t * ire   );
	void	ire_walk(   pfv_t func, char * arg   );
staticf void	ire_walk1(   ire_t * ire, pfv_t func, char * arg   );
	void	ire_walk_wq(   queue_t * wq, pfv_t func, char * arg   );
staticf void	ire_walk_wq1(   queue_t * wq, ire_t * ire, pfv_t func,
				char * arg   );

/*
 * This function is associated with the IP_IOC_IRE_ADVISE_NO_REPLY
 * IOCTL.  It is used by TCP (or other ULPs) to supply revised information
 * for an existing route IRE.
 */
/* ARGSUSED */
int
ip_ire_advise (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	u32	addr;
	u_char	* addr_ucp;
	ipic_t	* ipic;
	ire_t	* ire;

	ipic = (ipic_t *)ALIGN32(mp->b_rptr);
	if (ipic->ipic_addr_length != IP_ADDR_LEN
	|| !(addr_ucp = mi_offset_param(mp, ipic->ipic_addr_offset,
		ipic->ipic_addr_length)))
		return EINVAL;
	/* Extract the destination address. */
	addr = *(u32 *)ALIGN32(addr_ucp);
	/* Find the corresponding IRE. */
	ire = ire_lookup(addr);
	if (!ire)
		return ENOENT;
	/* Update the round trip time estimate and/or the max frag size. */
	if (ipic->ipic_rtt)
		ire->ire_rtt = ipic->ipic_rtt;
	if (ipic->ipic_max_frag)
		ire->ire_max_frag = ipic->ipic_max_frag;
	return 0;
}

/*
 * This function is associated with the IP_IOC_IRE_DELETE[_NO_REPLY]
 * IOCTL[s].  The NO_REPLY form is used by TCP to delete a route IRE
 * for a host that is not responding.  This will force an attempt to
 * establish a new route, if available.  Management processes may want
 * to use the version that generates a reply.
 */
/* ARGSUSED */
int
ip_ire_delete (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
reg	u_char	* addr_ucp;
	u32	addr;
reg	ire_t	* ire;
reg	ipid_t	* ipid;

	ipid = (ipid_t *)ALIGN32(mp->b_rptr);
	
	/* Only actions on IRE_ROUTEs are acceptable at present. */
	if ( ipid->ipid_ire_type != IRE_ROUTE )
		return EINVAL;
	
	addr_ucp = mi_offset_param(mp, ipid->ipid_addr_offset,
		ipid->ipid_addr_length);
	if (!addr_ucp)
		return EINVAL;
	switch (ipid->ipid_addr_length) {
	case IP_ADDR_LEN:
		/* addr_ucp points at IP addr */
		break;
	case sizeof(ipa_t): {
		ipa_t	* ipa;
		/* 
		 * got complete (sockaddr) address - increment addr_ucp to point
		 * at the ip_addr field.
		 */
		ipa = (ipa_t *)ALIGN32(addr_ucp);
		addr_ucp = (u_char *)&ipa->ip_addr;
		break;
		}
	default:
		return EINVAL;
	}
	/* Extract the destination address. */
	bcopy((char *)addr_ucp, (char *)&addr, IP_ADDR_LEN);

	/* Try to find the IRE. */
	ire = ire_lookup_exact(addr, ipid->ipid_ire_type, 0);
	
	/* Nail it. */
	if (ire) {
		/*
		 * Verify that the IRE has been around for a while.
		 * This is to protect against transport protocols
		 * that are too eager in sending delete messages.
		 */
		if (time_in_secs <
		    ire->ire_create_time + ip_ignore_delete_time)
			return EINVAL;
		if (ire->ire_gateway_addr != 0 && ip_def_gateway != NULL) {
			/*
			 * Make sure that we pick a different IRE_GATEWAY
			 * next time.
			 */
			ire_t	*gw_ire = ip_def_gateway;
			u_int u1 = ip_def_gateway_index % ip_def_gateway_count;
			while (u1--)
				gw_ire = gw_ire->ire_next;
			if (ire->ire_gateway_addr == gw_ire->ire_gateway_addr)
				/* Skip past the potentially bad gateway */
				ip_def_gateway_index++;				
		}
		ire_delete(ire);
	}
	/* Also look for an IRE_ROUTE_REDIRECT and remove it if present */
	ire = ire_lookup_exact(addr, IRE_ROUTE_REDIRECT, 0);
	
	/* Nail it. */
	if (ire)
		ire_delete(ire);
	return 0;
}

/*
 * Named Dispatch routine to produce a formatted report on all IREs.
 * This report is accessed by using the ndd utility to "get" ND variable
 * "ip_ire_status".
 */
/* ARGSUSED */
int
ip_ire_report (q, mp, arg)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	arg;
{
	mi_mpprintf(mp,
	  "IRE      rfq      stq      addr            mask            src             gateway         mxfrg rtt   ref in/out/forward type");
	/* 01234567 01234567 01234567 123.123.123.123 123.123.123.123 123.123.123.123 123.123.123.123 12345 12345 123 in/out/forward xxxxxxxxxx */
	ire_walk(ip_ire_report_ire, (char *)mp);
	return 0;
}

/* ire_walk routine invoked for ip_ire_report for each IRE. */
void
ip_ire_report_ire (ire, mp)
	ire_t	* ire;
	char	* mp;
{
	char	buf1[16];
	char	buf2[16];
	char	buf3[16];
	char	buf4[16];
	u_long	fo_pkt_count;
	u_long	ib_pkt_count;
	mblk_t	* ll_hdr_mp;
	int	ref = 0;

	ll_hdr_mp = ire->ire_ll_hdr_mp;
	if ( ll_hdr_mp )
		ref = ll_hdr_mp->b_datap->db_ref;
	/* "inbound" to a non local address is a forward */
	ib_pkt_count = ire->ire_ib_pkt_count;
	fo_pkt_count = 0;
	if (!(ire->ire_type & (IRE_LOCAL|IRE_BROADCAST))) {
		fo_pkt_count = ib_pkt_count;
		ib_pkt_count = 0;
	}
	mi_mpprintf((mblk_t *)ALIGN32(mp),"%08x %08x %08x %s %s %s %s %05d %05D %03d %d/%d/%d %s",
		ire, ire->ire_rfq, ire->ire_stq,
		ip_dot_addr(ire->ire_addr, buf1),
		ip_dot_addr(ire->ire_mask, buf2),
		ip_dot_addr(ire->ire_src_addr, buf3),
		ip_dot_addr(ire->ire_gateway_addr, buf4),
		ire->ire_max_frag, ire->ire_rtt, ref,
		ib_pkt_count,
		ire->ire_ob_pkt_count,
		fo_pkt_count,
		ip_nv_lookup(ire_nv_tbl, (int)ire->ire_type));
}

/*
 * ip_ire_req is called by ip_wput when an IRE_DB_REQ_TYPE message is handed
 * down from the Upper Level Protocol.  TCP sends down a message of this
 * type with a connection request packet chained on.  TCP wants a copy of
 * the IRE associated with the source address in the connection request
 * to establish starting parameters, such as round-trip time estimate and
 * max frag size, based on cached values in the IRE.
 */
void
ip_ire_req (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	ipha_t	* ipha;
	ire_t	* ire;
	mblk_t	* mp1;

	/* Look for the IP header. */
	mp1 = mp->b_cont;
	if (!mp1
	||  ((mp1->b_wptr - mp1->b_rptr) < sizeof(ipha_t))) {
		freemsg(mp);
		return;
	}
	/*
	 * Got it, now take our best shot at an IRE.  Note that we happen
	 * to know that we are handling a connection request packet, and
	 * so the address of interest is the source address.
	 */
	ipha = (ipha_t *)ALIGN32(mp1->b_rptr);
	ire = ire_lookup_loop(ipha->ipha_src, nilp(u32));
	if (!IRE_IS_TARGET(ire)) {
		/*
		 * If we don't have a clue how to get back to the source,
		 * we can just drop it on the floor right here.  The ULP
		 * doesn't have any state hanging on a return.
		 */
		freemsg(mp);
		return;
	}
	bcopy((char *)ire, (char *)mp->b_rptr, sizeof(ire_t));
	mp->b_wptr = &mp->b_rptr[sizeof(ire_t)];
	mp->b_datap->db_type = IRE_DB_TYPE;
	mp1 = mp->b_cont;
	mp->b_cont = nilp(mblk_t);
	linkb(mp1, mp);
	qreply(q, mp1);
}

/*
 * Add a fully initialized IRE to an appropriate list based on ire_type.
 * (Always called as writer.)
 */
ire_t *
ire_add (ire)
reg	ire_t	* ire;
{
reg	ire_t	** irep;
reg	ire_t	* ire1;

	/*
	 * If the ire is in a mblk copy it to a kmem_alloc'ed area
	 * and perform the deferred mutex_init of ire_ident_lock.
	 */
	if (ire->ire_mp) {
		ire1 = (ire_t *)ALIGN32(mi_alloc(sizeof(ire_t), BPRI_MED));
		if (!ire1) {
			ip1dbg(("ire_add: alloc failed\n"));
			ire_delete(ire);
			return nilp(ire_t);
		}
		*ire1 = *ire;
		ire1->ire_mp = nilp(mblk_t);
		freeb(ire->ire_mp);
		ire = ire1;
		mutex_init(&ire->ire_ident_lock, "ip ire lock",
			   MUTEX_DEFAULT, NULL);
	}
	/* Find the appropriate list head. */
	switch (ire->ire_type) {
	case IRE_ROUTE_ASSOC:
	case IRE_ROUTE_REDIRECT:
		ire->ire_mask = (u32)~0;
		irep = IREP_ASSOC_LOOKUP(ire->ire_addr);
		ire->ire_src_addr = 0;
		break;
	case IRE_ROUTE:
	case IRE_BROADCAST:
		ire_fastpath(ire);
		/* FALLTHRU */
	case IRE_LOCAL:
	case IRE_LOOPBACK:
		ire->ire_mask = (u32)~0;
		irep = IREP_LOOKUP(ire->ire_addr);
		break;
	case IRE_RESOLVER:
	case IRE_SUBNET:
		irep = &ire_subnet_head;
		break;
	case IRE_NET:
		irep = ire_net_irep(ire->ire_addr);
		ire->ire_src_addr = 0;
		break;
	case IRE_GATEWAY:
		/*
		 * We keep a count of default gateways which is used when
		 * assigning them as routes.
		 */
		ip_def_gateway_count++;
		irep = &ip_def_gateway;
		ire->ire_src_addr = 0;
		break;
	default:
		printf("ire_add: ire 0x%x has unrecognized IRE type (%d)\n",
		       (int)ire, ire->ire_type);
		ire_delete(ire);
		return nilp(ire_t);
	}
	
	/* Make sure the address is properly masked. */
	ire->ire_addr &= ire->ire_mask;
	
	/*
	 * Insert entries with the longest mask first. (Assume that
	 * masks are contigious.)
	 * Make it easy for ip_wput() to hit multiple targets by grouping
	 * identical addresses together on the hash chain.
	 */
	while ((ire1 = *irep) != 0) {
		if (ire1->ire_mask > ire->ire_mask ||
		    (ire1->ire_mask == ire->ire_mask &&
		     ire->ire_addr != ire1->ire_addr)) {
			irep = &ire1->ire_next;
			continue;
		}
		while (ire->ire_addr == ire1->ire_addr) {
			/* 
			 * Ignore new entry if there is an old duplicate.
			 * Note that we have to keep the old one and throw away
			 * the one to be added to avoid generating multiple 
			 * packets with the same IP id. Please note that
			 * this duplicate detection should not apply to
			 * IRE_LOCAL due to adding multiple IRE_LOCALs in
			 * case of unnumbered.
			 */
			if ((ire->ire_type != IRE_LOCAL)
			&& (ire1->ire_type == ire->ire_type)
			&& (ire1->ire_gateway_addr == ire->ire_gateway_addr)
			&& (ire1->ire_src_addr == ire->ire_src_addr)
			&& (ire1->ire_stq == ire->ire_stq)) {
				ire_delete(ire);
				return(ire1);
			} else {
				irep = &ire1->ire_next;
			}
			ire1 = *irep;
			if (!ire1)
				break;
		}
		break;		
	}
	if (ire1)
		ire1->ire_ptpn = &ire->ire_next;
	ire->ire_next = ire1;
	/* Link the new one in. */
	ire->ire_ptpn = irep;
	*irep = ire;
	return ire;
}

/*
 * ire_add_then_put is called when a new IRE has been created in order to
 * route an outgoing packet.  Typically, it is called from ip_wput when
 * a response comes back down from a resolver.  We add the IRE, and then
 * run the packet through ip_wput or ip_rput, as appropriate.  (Always called
 * as writer.)
 */
void
ire_add_then_put (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	mblk_t	* pkt;
	ire_t	* ire = (ire_t *)ALIGN32(mp->b_rptr);

	/*
	 * We are handed a message chain of the form:
	 *	IRE_MBLK-->packet
	 * Unhook the packet from the IRE.
	 */
	pkt = mp->b_cont;
	mp->b_cont = nilp(mblk_t);
	/* Add the IRE. */
	ire = ire_add(ire);
	if (!ire) {
		pkt->b_prev = nilp(mblk_t);
		pkt->b_next = nilp(mblk_t);
		freemsg(pkt);
		return;
	}
	/*
	 * Now we can feed the packet back in and this time it will probably
	 * fly.  If the packet was originally given to ip_wput, we cleared
	 * b_prev.  If it came in to ip_rput, we stored a pointer to
	 * the queue it came in on in b_prev.
	 */
	if (!pkt)
		return;
	/* If the packet originated externally then */
	if (pkt->b_prev) {
		q = (queue_t *)pkt->b_prev;
		pkt->b_prev = nilp(mblk_t);
		mp = allocb(0, BPRI_HI);
		if (!mp) {
			/* TODO source quench */
			pkt->b_next = nilp(mblk_t);
			freemsg(pkt);
			return;
		}
		mp->b_datap->db_type = M_BREAK;
		mp->b_cont = pkt;
		put(q, mp);
	} else if (pkt->b_next) {
		/* Packets from multicast router */
		pkt->b_next = nilp(mblk_t);
		ip_rput_forward(ire, (ipha_t *)ALIGN32(pkt->b_rptr), pkt);
	} else {
		/* Locally originated packets */
		ipha_t *ipha = (ipha_t *)ALIGN32(pkt->b_rptr);

		/*
		 * If we were resolving a router we can not use the
		 * routers IRE for sending the packet (since it would
		 * violate the uniqness of the IP idents) thus we
		 * make another pass through ip_wput to create the IRE_ROUTE
		 * for the destination.
		 */
		if (ipha->ipha_dst != ire->ire_addr)
			ip_wput(q, pkt);
		else
			ip_wput_ire(q, pkt, ire);
	}
}

/*
 * ire_create is called to allocate and initialize a new IRE.  (May be called
 * as writer.)
 */
ire_t *
ire_create (addr, mask, src_addr, gateway, max_frag, ll_hdr_mp, rfq, stq,
		type, rtt, ll_hdr_len)
	u_char	* addr;
	u_char	* mask;
	u_char	* src_addr;
	u_char	* gateway;
	u_int	max_frag;
	mblk_t	* ll_hdr_mp;
	queue_t	* rfq;
	queue_t	* stq;
	u_int	type;
	u_long	rtt;
	u_int	ll_hdr_len;
{
static	ire_t	ire_nil;
reg	ire_t	* ire;
	mblk_t	* mp;

	if (ll_hdr_mp) {
		if (ll_hdr_mp->b_datap->db_ref < DB_REF_REDZONE) {
			ll_hdr_mp = dupb(ll_hdr_mp);
		} else {
			ll_hdr_mp = copyb(ll_hdr_mp);
		}
		if (!ll_hdr_mp)
			return nilp(ire_t);
	}

	/* Check that IRE_RESOLVER and IRE_SUBNET have a ll_hdr_mp */
	if ((type & IRE_INTERFACE) &&
	    ll_hdr_mp == nilp(mblk_t)) {
		ip0dbg(("ire_create: no ll_hdr_mp\n"));
		return nilp(ire_t);
	}

	/* Allocate the new IRE. */
	mp = allocb(sizeof(ire_t), BPRI_MED);
	if (!mp) {
		if (ll_hdr_mp)
			freeb(ll_hdr_mp);
		return nilp(ire_t);
	}
	
	ire = (ire_t *)ALIGN32(mp->b_rptr);
	mp->b_wptr = (u_char *)&ire[1];

	/* Start clean. */
	*ire = ire_nil;
	
	/*
	 * Initialize the atomic ident field, using a possibly environment-
	 * specific macro.
	 */
	ATOMIC_32_INIT(&ire->ire_atomic_ident);
	ire->ire_mp = mp;
	mp->b_datap->db_type = IRE_DB_TYPE;
	
	bcopy((char *)addr, (char *)&ire->ire_addr, IP_ADDR_LEN);
	if (src_addr)
		bcopy((char *)src_addr,(char *)&ire->ire_src_addr,IP_ADDR_LEN);
	if (mask)
		bcopy((char *)mask, (char *)&ire->ire_mask, IP_ADDR_LEN);
	if (gateway) {
		bcopy((char *)gateway, (char *)&ire->ire_gateway_addr,
			IP_ADDR_LEN);
	}
	ire->ire_max_frag = max_frag;
	ire->ire_frag_flag = (ip_path_mtu_discovery) ? IPH_DF : 0;
	ire->ire_ll_hdr_mp = ll_hdr_mp;
	ire->ire_stq = stq;
	ire->ire_rfq = rfq;
	ire->ire_type = type;
	ire->ire_rtt = rtt;
	ire->ire_ll_hdr_length = ll_hdr_len;
	ATOMIC_32_ASSIGN(&ire->ire_atomic_ident, (u32)LBOLT_TO_MS(lbolt));
	ire->ire_tire_mark = ire->ire_ob_pkt_count + ire->ire_ib_pkt_count;
	ire->ire_create_time = (u32)time_in_secs;
	/*
	 * Wait to initialize ire_ident_lock until ire_add is run. Before
	 * ire_add this IRE will be sent around in streams messages
	 * and might be freed by a freeb() thus we can't make sure that
	 * the mutex_destroy gets executed until after ire_add has
	 * taken the ire out of the ire_mp mblk.
	 */
	return ire;
}

/*
 * This routine is called repeatedly by ipif_up to create broadcast IREs.
 * It is passed a pointer to a slot in an IRE pointer array into which to
 * place the pointer to the new IRE, if indeed we create one.  If the
 * IRE corresponding to the address passed in would be a duplicate of an
 * existing one, we don't create the new one.  irep is incremented before
 * return only if we do create a new IRE.  (Always called as writer.)
 */
ire_t **
ire_create_bcast (ipif, addr, irep)
	ipif_t	* ipif;
	u32	addr;
	ire_t	** irep;
{
	/*
	 * No broadcast IREs for the LOOPBACK interface
	 * or others such as point to point.
	 */
	if (!(ipif->ipif_flags & IFF_BROADCAST))
		return irep;

	/* If this would be a duplicate, don't bother. */
	if (ire_lookup_broadcast(addr, ipif))
		return irep;
	
	*irep++ = ire_create(
		(u_char *)&addr,			/* dest addr */
		(u_char *)&ip_g_all_ones,		/* mask */
		(u_char *)&ipif->ipif_local_addr,	/* source addr */
		nilp(u_char),				/* no gateway */
		ipif->ipif_mtu,				/* max frag */
		ipif->ipif_bcast_mp,			/* xmit header */
		ipif->ipif_rq,				/* recv-from queue */
		ipif->ipif_wq,				/* send-to queue */
		IRE_BROADCAST,
		(u_long)512,				/* rtt */
		0
	);
	/*
	 * Create a loopback IRE for the broadcast address.
	 * Note: ire_add() will blow away duplicates thus there is no need
	 * to check for existing entries.
	 */
	*irep++ = ire_create(
	     	(u_char *)&addr,		/* dest address */
		(u_char *)&ip_g_all_ones,	/* mask */
		(u_char *)&ipif->ipif_local_addr,/* source address */
		nilp(u_char),			/* no gateway */
		STRMSGSZ,			/* max frag size */
		nilp(mblk_t),			/* no xmit header */
		ipif->ipif_rq,			/* recv-from queue */
		nilp(queue_t),			/* no send-to queue */
		IRE_BROADCAST,			/* Needed for fanout in wput */
		512,				/* rtt */
		0
	);
	return irep;
}

/* Delete the specified IRE.  (Always called as writer.) */
void
ire_delete (ire)
reg	ire_t	* ire;
{
reg	ire_t	* ire1;
reg	ire_t	** ptpn = ire->ire_ptpn;
reg	ipif_t	* ipif;


	/* Remove IRE from whatever list it is on. */
	ire1 = ire->ire_next;
	if ( ire1 )
		ire1->ire_ptpn = ptpn;
	if ( ptpn ) {
		*ptpn = ire1;
		/* If it is a gateway, decrement the count. */
		if (ire->ire_type == IRE_GATEWAY)
			ip_def_gateway_count--;
	}
	
	/* Remember the global statistics from the dieing */
	if (ipif = ire_to_ipif(ire)) {
		/* "inbound" to a non local address is a forward */
		if (ire->ire_type & (IRE_LOCAL|IRE_BROADCAST))
			ipif->ipif_ib_pkt_count += ire->ire_ib_pkt_count;
		else
			ipif->ipif_fo_pkt_count += ire->ire_ib_pkt_count;
		ipif->ipif_ob_pkt_count += ire->ire_ob_pkt_count;
	}
	/* Free the xmit header, and the IRE itself. */
	if (ire->ire_ll_hdr_saved_mp)
		freeb(ire->ire_ll_hdr_saved_mp);
	if (ire->ire_ll_hdr_mp)
		freeb(ire->ire_ll_hdr_mp);
	if (ire->ire_mp)
		freeb(ire->ire_mp);
	else {
		mutex_destroy(&ire->ire_ident_lock);
		mi_free((char *)ire);
	}
}

/*
 * Remove all IRE_ROUTE entries that match the ire specified.  (Always called
 * as writer.)
 */
void
ire_delete_routes (ire)
	ire_t	* ire;
{
	switch (ire->ire_type) {
	case IRE_ROUTE:
		break;
	case IRE_GATEWAY:
		ire_walk(ire_delete_route_gw, (char *)&ire->ire_gateway_addr);
		break;
	default:
		ire_walk(ire_delete_route_match, (char *)ire);
		break;
	}
}

/*
 * ire_walk routine to delete all IRE_ROUTE/IRE_ROUTE_REIDRECT entries 
 * that have a given gateway address.  (Always called as writer.)
 */
staticf void
ire_delete_route_gw (ire, cp)
	ire_t	* ire;
	char	* cp;
{
	u32	gw_addr;

	if (!(ire->ire_type & (IRE_ROUTE|IRE_ROUTE_REDIRECT)))
		return;

	bcopy(cp, (char *)&gw_addr, sizeof(gw_addr));
	if (ire->ire_gateway_addr == gw_addr) {
		ip1dbg(("ire_delete_route_gw: deleted 0x%x type %d to 0x%x\n",
			(int)ntohl(ire->ire_addr), ire->ire_type,
			(int)ntohl(ire->ire_gateway_addr)));
		ire_delete(ire);
	}
}

/*
 * ire_walk routine to delete all IRE_ROUTE entries that match the address
 * and netmask of an ire.  (Always called as writer.)
 */
staticf void
ire_delete_route_match (ire, cp)
	ire_t	* ire;
	char	* cp;
{
	ire_t	* ire2 = (ire_t *)ALIGN32(cp);

	if (ire->ire_type != IRE_ROUTE)
		return;

	if ((ire->ire_addr & ire2->ire_mask) == ire2->ire_addr) {
		ip1dbg(("ire_delete_route_match: deleted 0x%x type %d to 0x%x\n",
			(int)ntohl(ire->ire_addr), ire->ire_type,
			(int)ntohl(ire->ire_gateway_addr)));
		ire_delete(ire);
	}
}

/*
 * ire_walk routine to delete any ROUTE IRE's that are stale.
 * We check the current value of the IRE ident field.  If it is unchanged
 * since we last checked, we delete the IRE.  Otherwise, we re-mark the
 * tire and check it next time.  (Always called as writer.)
 */
void
ire_expire (ire, arg)
	ire_t	* ire;
	char	* arg;
{
	int flush_flags = (int)arg;

	if ( (flush_flags & 2) && ire->ire_type == IRE_ROUTE_REDIRECT ) {
		/* Make sure we delete the corresponding IRE_ROUTE */
		ire_t	*ire2;

		ip1dbg(("ire_expire: all redirects\n"));
		ire2 = ire_lookup_exact(ire->ire_addr, IRE_ROUTE, 0);
		if (ire2)
			ire_delete(ire2);
		ire_delete(ire);
		return;
	}
	if ( ire->ire_type != IRE_ROUTE )
		return;

	if (flush_flags & 1) {
		/*
		 * Remove all IRE_ROUTE.
		 * Verify that create time is more than
		 * ip_ire_flush_interval milliseconds ago.
		 */
		if (((u32)time_in_secs - ire->ire_create_time) * 1000 >
		    ip_ire_flush_interval) {
			ip1dbg(("ire_expire: all IRE_ROUTE\n"));
			ire_delete(ire);
			return;
		}
	}
	/*
	 * Garbage collect it if has not been used since the
	 * last time and if it is not on the local network.
	 * Avoid agressive garbage collection if path MTU discovery
	 * has decremented the MTU.
	 */
	if ( ire->ire_ob_pkt_count + ire->ire_ib_pkt_count ==
	    ire->ire_tire_mark && ire->ire_gateway_addr != 0) {
		ipif_t	*ipif;

		ipif = ire_to_ipif(ire);
		if (ipif && ire->ire_max_frag == ipif->ipif_mtu) {
			ip1dbg(("ire_expire: old IRE_ROUTE\n"));
			ire_delete(ire);
			return;
		}
	}
	ire->ire_tire_mark = ire->ire_ob_pkt_count + ire->ire_ib_pkt_count;
	if ( ip_path_mtu_discovery && (flush_flags & 4) ) {
		/* Increase pmtu if it is less than the interface mtu */
		ipif_t	*ipif;

		ipif = ire_to_ipif(ire);
		if (ipif) {
			ire->ire_max_frag = ipif->ipif_mtu;
			ire->ire_frag_flag = IPH_DF;
		}
	}
}

/*
 * If the device driver supports it, we change the ire_ll_hdr_mp from a
 * dl_unitdata_req to an M_DATA prepend.  (May be called as writer.)
 */
staticf void
ire_fastpath (ire)
	ire_t	* ire;
{
	ill_t	* ill;
	u_int	len;

	if (ire->ire_ll_hdr_length  ||  !ire->ire_ll_hdr_mp)
		return;
	ill = ire_to_ill(ire);
	if (!ill)
		return;
	len = ill->ill_hdr_length;
	if (len == 0)
		return;
	ill_fastpath_probe(ill, ire->ire_ll_hdr_mp);
}

/*
 * Update all IRE's that are not in fastpath mode and 
 * have an ll_hdr_mp that matches mp. mp->b_cont contains
 * the fastpath header.
 */
void	
ire_fastpath_update(ire, arg)
	ire_t	*ire;
	char 	*arg;
{
	mblk_t 	* mp, * ll_hdr_mp;
	u_char 	* up, * up2;
	int	cmplen;

	if (!(ire->ire_type & (IRE_ROUTE|IRE_BROADCAST)))
		return;
	if (ire->ire_ll_hdr_length != 0 || !ire->ire_ll_hdr_mp)
		return;

	ip2dbg(("ip_fastpath_update: trying\n"));
	mp = (mblk_t *)ALIGN32(arg);
	up = mp->b_rptr;
	cmplen = mp->b_wptr - up;
	up2 = ire->ire_ll_hdr_mp->b_rptr;
	if (ire->ire_ll_hdr_mp->b_wptr - up2 != cmplen ||
	    bcmp((char *)up, (char *)up2, cmplen) != 0)
		return;
	/* Matched - install mp as the ire_ll_hdr_mp */
	ip1dbg(("ip_fastpath_update: match\n"));
	ll_hdr_mp = dupb(mp->b_cont);
	if (ll_hdr_mp) {
		if (ire->ire_ll_hdr_length == 0) {
			/* Save the ll_hdr for mib and SIOC*ARP ioctls */
			if (ire->ire_ll_hdr_saved_mp)
				freeb(ire->ire_ll_hdr_saved_mp);
			ire->ire_ll_hdr_saved_mp = ire->ire_ll_hdr_mp;
		} else
			freeb(ire->ire_ll_hdr_mp);
		ire->ire_ll_hdr_mp = ll_hdr_mp;
		ire->ire_ll_hdr_length = ll_hdr_mp->b_wptr - ll_hdr_mp->b_rptr;
	}
}
/*
 * Return the IRE that best applies to the destination address given.  (May be
 * called as writer.)
 */
ire_t *
ire_lookup (addr)
	u32	addr;
{
reg	ire_t	* ire;

	/* First check the host route list. */
	ire = ire_hash_tbl[IRE_ADDR_HASH(addr)];
	while ( ire ) {
		if (ire->ire_addr == addr)
			return ire;
		ire = ire->ire_next;
	}
	/*
	 * Next check for a host route association (that is, an
	 * assignment of a gateway for a particular host address).
	 */
	ire = ire_assoc_hash_tbl[IRE_ADDR_HASH(addr)];
	while ( ire ) {
		if (ire->ire_addr == addr)
			return ire;
		ire = ire->ire_next;
	}
	/*
	 * Next look for a directly connected subnet which would include
	 * the destination host.
	 */
	ire = ire_subnet_head;
	while ( ire ) {
		if (ire->ire_addr == (addr & ire->ire_mask))
			return ire;
		ire = ire->ire_next;
	}
	/*
	 * Try for a net route (that is, an assignment of a gateway for
	 * the network that includes the destination host).
	 */
	ire = *ire_net_irep(addr);
	while ( ire ) {
		if (ire->ire_addr == (addr & ire->ire_mask))
			return ire;
		ire = ire->ire_next;
	}
	/* If all else fails, hand back a default gateway to try. */
	ire = ip_def_gateway;
	if ( ire ) {
		u_int u1 = ip_def_gateway_index % ip_def_gateway_count;
		while (u1--)
			ire = ire->ire_next;
	}
	return ire;
}

/*
 * Look up a broadcast IRE associated with a specific IPIF.  (May be called
 * as writer.)
 */
ire_t *
ire_lookup_broadcast (addr, ipif)
	u32	addr;
	ipif_t	* ipif;
{
	ire_t	* ire;
	
	ire = ire_hash_tbl[IRE_ADDR_HASH(addr)];
	for ( ; ire ; ire = ire->ire_next ) {
		if ( ire->ire_type == IRE_BROADCAST
		&&  ire->ire_addr == addr
		&&  ire->ire_src_addr == ipif->ipif_local_addr
		&&  ire->ire_stq == ipif->ipif_ill->ill_wq )
			break;
	}
	return ire;
}

/*
 * Look up an IRE which matches both type and address, and, if type is
 * IRE_GATEWAY, the gateway address.  (May be called as writer.)
 */
ire_t *
ire_lookup_exact (addr, type, gw_addr)
	u32	addr;
	u_int	type;
	u32	gw_addr;
{
reg	ire_t	* ire;
reg	ire_t	** irep;

	/* Establish the appropriate list head. */
	switch (type) {
	case IRE_BROADCAST:
	case IRE_LOCAL:
	case IRE_LOOPBACK:
	case IRE_ROUTE:
		irep = &ire_hash_tbl[IRE_ADDR_HASH(addr)];
		break;
	case IRE_GATEWAY:
		irep = &ip_def_gateway;
		break;
	case IRE_NET:
		irep = ire_net_irep(addr);
		break;
	case IRE_SUBNET:
	case IRE_RESOLVER:
		irep = &ire_subnet_head;
		break;
	case IRE_ROUTE_ASSOC:
	case IRE_ROUTE_REDIRECT:
		irep = IREP_ASSOC_LOOKUP(addr);
		break;
	default:
		return nilp(ire_t);
	}
	
	for (ire = *irep; ire; ire = ire->ire_next) {
		if ( ire->ire_type == type
		&&   ire->ire_addr == addr 
		&&  (gw_addr == 0  ||  gw_addr == ire->ire_gateway_addr))
		         	break;
        }
	return ire;
}

/*
 * Return any local address.  We use this to target ourselves
 * when the src address was specified as 'default'.
 * Preference for IRE_LOCAL entries.
 */
ire_t *
ire_lookup_local ()
{
	ire_t	** irep;
	ire_t	* ire;
	ire_t	* maybe = nilp(ire_t);

	for (irep = ire_hash_tbl; irep < &ire_hash_tbl[IRE_HASH_TBL_COUNT]
	; irep++) {
		for (ire = *irep; ire; ire = ire->ire_next) {
			switch (ire->ire_type) {
			case IRE_LOOPBACK:
				if (maybe == nilp(ire_t))
					maybe = ire;
				break;
			case IRE_LOCAL:
				return ire;
			}
		}
	}
	return maybe;
}

/*
 * Find the most specific IRE available for a destination address.  If
 * ire_lookup gives us a gateway, follow the gateway address.  (May be called
 * as writer.)
 */
ire_t *
ire_lookup_loop (dst, gw)
	u32	dst;
	u32	* gw;
{
	int	count = 8;
	ire_t	* ire;

	ire = ire_lookup(dst);
	while (ire) {
		switch (ire->ire_type) {
		case IRE_ROUTE_ASSOC:
		case IRE_ROUTE_REDIRECT:
		case IRE_NET:
		case IRE_GATEWAY:
			/*
			 * Follow the gateway.  (Any of the above types
			 * without an ire_stq must have a gateway address.)
			 * If the gateway is the same as the destination
			 * we instead just look for the interface IRE.
			 */
			ire->ire_ob_pkt_count++;
			if (dst == ire->ire_gateway_addr) {
				ire = ire_lookup_interface(dst, IRE_INTERFACE);
				break;
			}
			dst = ire->ire_gateway_addr;
			ire = ire_lookup(dst);
			if ( gw  &&  dst )
				*gw = dst;
			break;
		case IRE_SUBNET:
		case IRE_RESOLVER:
		case IRE_LOCAL:
		case IRE_LOOPBACK:
			ire->ire_ob_pkt_count++;
			/* FALLTHRU */
		default:
			return ire;
		}
		/* Shouldn't need this firewall, but... */
		if (--count < 0)
			break;
	}
	return nilp(ire_t);
}

/* (May be called as writer.) */
ire_t *
ire_lookup_myaddr (addr)
	u32	addr;
{
	return ire_lookup_exact(addr, IRE_LOCAL, 0);
}

/*
 * Return the IRE that best applies to the destination address given. 
 * Ignore IRE_ROUTE entries
 */
ire_t *
ire_lookup_noroute (addr)
	u32	addr;
{
reg	ire_t	* ire;

	/*
	 * Next check for a host route association (that is, an
	 * assignment of a gateway for a particular host address).
	 */
	ire = ire_assoc_hash_tbl[IRE_ADDR_HASH(addr)];
	while ( ire ) {
		if (ire->ire_addr == addr)
			return ire;
		ire = ire->ire_next;
	}
	/*
	 * Next look for a directly connected subnet which would include
	 * the destination host.
	 */
	ire = ire_subnet_head;
	while ( ire ) {
		if (ire->ire_addr == (addr & ire->ire_mask))
			return ire;
		ire = ire->ire_next;
	}
	/*
	 * Try for a net route (that is, an assignment of a gateway for
	 * the network that includes the destination host).
	 */
	ire = *ire_net_irep(addr);
	while ( ire ) {
		if (ire->ire_addr == (addr & ire->ire_mask))
			return ire;
		ire = ire->ire_next;
	}
	/* If all else fails, hand back a default gateway to try. */
	ire = ip_def_gateway;
	if ( ire ) {
		u_int u1 = ip_def_gateway_index % ip_def_gateway_count;
		while (u1--)
			ire = ire->ire_next;
	}
	return ire;
}
/*
 * Find an interface IRE that matches any type in the type mask for the
 * address specified.
 */
ire_t *
ire_lookup_interface (addr, ire_type_mask)
	u32	addr;
	u_int	ire_type_mask;
{
reg	ire_t	* ire;

	ire = ire_subnet_head;
	while ( ire ) {
		if ( (ire->ire_type & ire_type_mask) 
		&&  ire->ire_addr == (addr & ire->ire_mask))
			return ire;
		ire = ire->ire_next;
	}
	return ire;
}
/*
 * Find the ire, whose associated ipif matches a given ipif (Called by
 * ip_siocdelrt() 
 */
ire_t *
ire_lookup_ipif(ipif, type, addr, src_addr)
	ipif_t	* ipif;
	u_int	type;
	u32	addr;
	u32	src_addr;
{
	ire_t   ** irep;
	ire_t   * ire;
	ipif_t  * ipif_of_ire;

	
	irep = &ire_subnet_head;
	for(ire = *irep; ire; ire = ire->ire_next) {
		if ( (ire->ire_type == type) &&
		(ire->ire_addr == addr) && 
		(ire->ire_src_addr == src_addr) ) {
			ipif_of_ire  = ire_to_ipif(ire);
			if(ipif_of_ire &&
  			ipif_of_ire == ipif) {
				return(ire);
			}
		}
	}

	return(nilp(ire_t));
}
/*
 * Find an interface IRE that matches 
 * the given address exactly. No mask is
 * used here.
 */
ire_t *
ire_lookup_interface_exact (addr, ire_type, src_addr)
	u32	addr;
	u_int	ire_type;
	u32	src_addr;
{
	ire_t	* ire;

	ire = ire_subnet_head;
	while ( ire ) {
		if ( (ire->ire_type & ire_type) 
                      && (ire->ire_addr == addr)
                      && (ire->ire_src_addr == src_addr) )
			return ire;
		ire = ire->ire_next;
	}
	return ire;
}

/*
 * Return the hash list head for the network number associated with the
 * address specified.
 */
staticf ire_t **
ire_net_irep (addr)
	u32	addr;
{
	addr &= ip_net_mask(addr);
	return &ire_net_hash_tbl[INE_ADDR_HASH(addr)];
}

/* ire_walk routine to sum all the packets for IREs that match */
void
ire_pkt_count (ire, ippc_arg)
reg	ire_t	* ire;
	char	* ippc_arg;
{
	ippc_t	* ippc = (ippc_t *)ALIGN32(ippc_arg);

	if (ire->ire_src_addr == ippc->ippc_addr) {
		/* "inbound" to a non local address is a forward */
		if (ire->ire_type & (IRE_LOCAL|IRE_BROADCAST))
			ippc->ippc_ib_pkt_count += ire->ire_ib_pkt_count;
		else
			ippc->ippc_fo_pkt_count += ire->ire_ib_pkt_count;
		ippc->ippc_ob_pkt_count += ire->ire_ob_pkt_count;
	}
}

/* Return the ipif associated with the specified ire. */
ipif_t *
ire_interface_to_ipif (ire)
	ire_t	* ire;
{
	u32	our_local_addr = ire->ire_src_addr;
	ill_t	*ill;
	ipif_t	*ipif;

	for (ill = ill_g_head; ill; ill = ill->ill_next) {
		if (ire->ire_stq != ill->ill_wq &&
		ire->ire_stq != ill->ill_rq &&
		ire->ire_rfq != ill->ill_wq &&
		ire->ire_rfq != ill->ill_rq) {
			/* Wrong ill hence wrong ipif */
			continue;
		}
		for (ipif = ill->ill_ipif; ipif; ipif = ipif->ipif_next) {
			/* Allow the ipif to be down */
			if (ipif->ipif_local_addr != our_local_addr)
				continue;
			if (ire->ire_mask == (u32)~0
			&&  ipif->ipif_flags & IFF_POINTOPOINT) {
				/* Verify that we get the correct unnumbered
				 * pt-pt link.
				 */
				if (ipif->ipif_pp_dst_addr != ire->ire_addr)
					continue;
			} else if (ire->ire_mask == (u32)~0
			||  ipif->ipif_flags & IFF_POINTOPOINT)
				continue;
			return ipif;
		}
	}
	return nilp(ipif_t);
}

/*
 * If the specified IRE is associated with a particular ILL, return
 * that ILL pointer.  (May be called as writer.)
 */
ill_t *
ire_to_ill (ire)
	ire_t	* ire;
{
reg	queue_t	* stq;

	if (ire  &&  (stq = ire->ire_stq))
		return (ill_t *)stq->q_ptr;
	return nilp(ill_t);
}

/*
 * Return the ipif associated with the specified ire.  (May be called as
 * writer.)
 */
ipif_t *
ire_to_ipif (ire)
	ire_t	* ire;
{
	u32	our_local_addr = ire->ire_src_addr;
	ill_t	* ill;
	ipif_t	* ipif;

	if (our_local_addr == 0)
		return nilp(ipif_t);

	for (ill = ill_g_head; ill; ill = ill->ill_next) {
		if (ire->ire_stq != ill->ill_wq &&
		ire->ire_stq != ill->ill_rq &&
		ire->ire_rfq != ill->ill_wq &&
		ire->ire_rfq != ill->ill_rq) {
			/* Wrong ill hence wrong ipif */
			continue;
		}

		for (ipif = ill->ill_ipif; ipif; ipif = ipif->ipif_next) {
			/* Allow the ipif to be down */
			if (ipif->ipif_local_addr == our_local_addr)
				return ipif;
		}
	}
	return nilp(ipif_t);
}

/* Arrange to call the specified function for every IRE in the world. */
void
ire_walk (func, arg)
	pfv_t	func;
	char	* arg;
{
	ire_t	** irep;

	for (irep = ire_hash_tbl; irep < &ire_hash_tbl[IRE_HASH_TBL_COUNT]
	; irep++)
		ire_walk1(*irep, func, arg);
	irep = ire_assoc_hash_tbl;
	for ( ; irep < &ire_assoc_hash_tbl[IRE_HASH_TBL_COUNT]; irep++)
		ire_walk1(*irep, func, arg);
	for (irep = ire_net_hash_tbl
	; irep < &ire_net_hash_tbl[INE_HASH_TBL_COUNT]; irep++)
		ire_walk1(*irep, func, arg);
	ire_walk1(ire_subnet_head, func, arg);
	ire_walk1(ip_def_gateway, func, arg);
}

/*
 * Walk the supplied IRE chain calling 'func' with each IRE and 'arg'
 * as parameters.  Note that we walk the chain in a way that permits
 * removal of the IRE by the called function.
 */
staticf void
ire_walk1 (ire, func, arg)
	ire_t	* ire;
	pfv_t	func;
	char	* arg;
{
	ire_t	* ire1;

#ifdef lint
	ire1 = nilp(ire_t);
#endif
	for ( ; ire; ire = ire1) {
		ire1 = ire->ire_next;
		(*func)(ire, arg);
	}
}

/* Arrange to call the specified function for every IRE that matches the wq. */
void
ire_walk_wq (wq, func, arg)
	queue_t	* wq;
	pfv_t	func;
	char	* arg;
{
	ire_t	** irep;

	for (irep = ire_hash_tbl; irep < &ire_hash_tbl[IRE_HASH_TBL_COUNT]
	; irep++)
		ire_walk_wq1(wq, *irep, func, arg);
	irep = ire_assoc_hash_tbl;
	for ( ; irep < &ire_assoc_hash_tbl[IRE_HASH_TBL_COUNT]; irep++)
		ire_walk_wq1(wq, *irep, func, arg);
	for (irep = ire_net_hash_tbl
	; irep < &ire_net_hash_tbl[INE_HASH_TBL_COUNT]; irep++)
		ire_walk_wq1(wq, *irep, func, arg);
	ire_walk_wq1(wq, ire_subnet_head, func, arg);
	ire_walk_wq1(wq, ip_def_gateway, func, arg);
}

/*
 * Walk the supplied IRE chain calling 'func' with each IRE and 'arg'
 * as parameters.  Note that we walk the chain in a way that permits
 * removal of the IRE by the called function.
 */
staticf void
ire_walk_wq1 (wq, ire, func, arg)
	queue_t	* wq;
	ire_t	* ire;
	pfv_t	func;
	char	* arg;
{
	ire_t	* ire1;

#ifdef lint
	ire1 = nilp(ire_t);
#endif
	for ( ; ire; ire = ire1) {
		ire1 = ire->ire_next;
		if (ire->ire_stq == wq)
			(*func)(ire, arg);
	}
}

