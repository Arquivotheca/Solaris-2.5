/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)ip_if.c	1.38	95/03/01 SMI"

/*
 * This file contains the interface control functions for IP.
 */

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
#include <sys/kstat.h>
#include <sys/debug.h>

#include <sys/systm.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/isa_defs.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/route.h>
#include <sys/sockio.h>
#include <netinet/in.h>

#include <inet/common.h>
#include <inet/mi.h>
#include <inet/nd.h>
#include <inet/arp.h>
#include <inet/mib2.h>
#include <inet/ip.h>
#include <inet/ip_multi.h>
#include <inet/ip_ire.h>

#include <netinet/ip_mroute.h>

#else

#include <types.h>
#include <stream.h>
#include <dlpi.h>
#include <stropts.h>
#include <strlog.h>
#include <tihdr.h>
#include <tiuser.h>
#include <kstat.h>

#include <socket.h>
#include <isa_defs.h>
#include <if.h>
#include <if_arp.h>
#include <route.h>
#include <sockio.h>
#include <in.h>

#include <common.h>
#include <mi.h>
#include <nd.h>
#include <arp.h>
#include <mib2.h>
#include <ip.h>
#include <ip_multi.h>
#include <ip_ire.h>

#include <ip_mroute.h>

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

extern mib2_ip_t	ip_mib;

/* The character which tells where the ill_name ends */
#define IPIF_SEPARATOR_CHAR	':'

#ifndef	EBADADDR
#define	EBADADDR	EFAULT
#endif

#ifndef	STRMSGSZ
#define	STRMSGSZ	(8192+40)
#endif

#define	IP_MULTI_EXTRACT_MASK		0x007fffff
#define	IP_ARP_HW_MAPPING_START		2
#define	IP_MAX_ADDR_LENGTH		40

/* DLPI SAPs are in host byte order for all systems */
#define	IP_DL_SAP	0x0800

/* IP ioctl function table entry */
typedef struct ipft_s {
	int	ipft_cmd;
	pfi_t	ipft_pfi;
	int	ipft_min_size;
	int	ipft_flags;
} ipft_t;
#define	IPFT_F_NO_REPLY		0x1

typedef struct ip_sock_ar_s {
	union {
		area_t	ip_sock_area;
		ared_t	ip_sock_ared;
		areq_t	ip_sock_areq;
	} ip_sock_ar_u;
	queue_t	* ip_sock_ar_q;
} ip_sock_ar_t;

typedef	struct iocblk	* IOCP;

	mblk_t	* ill_arp_alloc(   ill_t * ill, u_char * template, u32 addr   );
	void	ill_delete(   ill_t * ill   );
	mblk_t	* ill_dlur_gen(   u_char * addr, u_int addr_length,
				 u_long sap, int sap_length);
	void	ill_down(   ill_t * ill   );
staticf void	ill_downi(   ire_t * ire, char * ill   );
	void	ill_fastpath_ack(   ill_t * ill, mblk_t * mp   );
	void	ill_fastpath_probe(   ill_t * ill, mblk_t *dlur_mp   );
	boolean_t	ill_frag_timeout(   ill_t * ill, u_long dead_interval   );
	int	ill_init(   queue_t * q, ill_t * ill   );
staticf ill_t	* ill_lookup_on_name(   char * name, u_int namelen   );

extern	u16	ip_csum(   mblk_t * mp, int offset, u32 prev_sum   );
	int	ip_ill_report(   queue_t * q, mblk_t * mp, caddr_t arg   );
	int	ip_ipif_report(   queue_t * q, mblk_t * mp, caddr_t arg   );
	void	ip_ll_subnet_defaults(   ill_t * ill, mblk_t * mp   );
staticf	boolean_t	ip_local_addr_ok(   u32 addr, u32 subnet_mask   );
staticf ip_m_t	* ip_m_lookup(   ill_t * ill, u_long mac_type   );
staticf	int	ip_siocaddrt(   struct rtentry * rt   );
staticf	int	ip_siocdelrt(   struct rtentry * rt   );
	void	ip_sioctl_copyin_done(   queue_t * q, mblk_t * mp   );
	void	ip_sioctl_copyin_setup(   queue_t * q, mblk_t * mp   );
	void	ip_sioctl_iocack(   queue_t * q, mblk_t * mp   );
staticf int	ip_sioctl_newmask(   ipif_t * ipif, u32 mask   );
staticf	u32	ip_subnet_mask(   u32 addr   );

staticf void	ip_wput_ioctl(   queue_t * q, mblk_t * mp   );

staticf ipif_t	* ipif_allocate(   ill_t * ill, long id, u_int ire_type   );
staticf	void	ipif_arp_down(   ipif_t * ipif   );
	boolean_t	ipif_arp_up(   ipif_t * ipif, u32 addr   );
	void	ipif_down(   ipif_t * ipif   );
staticf void	ipif_downi(   ire_t * ire, char * ipif   );
staticf	void	ipif_free(   ipif_t * ipif   );
	char	* ipif_get_name(   ipif_t * ipif, char * buf, int len   );
staticf	ipif_t	* ipif_lookup_on_name(   char * name, int namelen   );
	boolean_t	ipif_loopback_init(   void   );
	void	ipif_mask_reply(   ipif_t * ipif   );
staticf void	ipif_mtu_change(   ire_t * ire, char * ipif_arg   );
staticf void	ipif_multicast_up(   ipif_t * ipif   );
#ifdef notdef
staticf void	ipif_multicast_down(   ipif_t * ipif   );
#endif
staticf	void	ipif_recover_ire(   ipif_t * ipif   );
staticf void	ipif_set_default(   ipif_t * ipif   );
	ire_t	* ipif_to_ire(   ipif_t * ipif   );
staticf int	ipif_up(   ipif_t * ipif, queue_t * q, mblk_t * mp   );
staticf boolean_t ip_addr_availability_check(	ipif_t * new_ipif);

/* 
 * Multicast address mappings used over Ethernet/802.X.
 * This address is used as base for mappings.
 *
 * TODO Make these configurable e.g. by putting them in (the user extensible?)
 * ip_m_t.
 */
static	u8	ip_g_phys_multi_addr[] =
		{ 0x01, 0x00, 0x5e, 0x00, 0x00, 0x00 };
#define	IP_PHYS_MULTI_ADDR_LENGTH 	sizeof(ip_g_phys_multi_addr)

	u_long	xxx_pad;

/* 
 * The field values are larger than strictly necessary for simple
 * AR_ENTRY_ADDs but the padding lets us accomodate the socket ioctls.
 */
static	area_t	ip_area_template = {
	AR_ENTRY_ADD,			/* area_cmd */
	sizeof(ip_sock_ar_t) + (IP_ADDR_LEN*2) + sizeof(struct sockaddr),
					/* area_name_offset */
	/* area_name_length temporarily holds this structure length */
	sizeof(area_t),			/* area_name_length */
	IP_ARP_PROTO_TYPE,		/* area_proto */
	sizeof(ip_sock_ar_t),		/* area_proto_addr_offset */
	IP_ADDR_LEN,			/* area_proto_addr_length */
	sizeof(ip_sock_ar_t) + IP_ADDR_LEN,
					/* area_proto_mask_offset */
	0,				/* area_flags */
	sizeof(ip_sock_ar_t) + IP_ADDR_LEN + IP_ADDR_LEN,
					/* area_hw_addr_offset */
	/* Zero length hw_addr_length means 'use your idea of the address' */
	0				/* area_hw_addr_length */
};

static	ared_t	ip_ared_template = {
	AR_ENTRY_DELETE,
	sizeof(ared_t) + IP_ADDR_LEN,
	sizeof(ared_t),
	IP_ARP_PROTO_TYPE,
	sizeof(ared_t),
	IP_ADDR_LEN
};

static	areq_t	ip_areq_template = {
	AR_ENTRY_QUERY,			/* cmd */
	sizeof(areq_t)+(2*IP_ADDR_LEN),	/* name offset */
	sizeof(areq_t),			/* name len (filled by ill_arp_alloc) */
	IP_ARP_PROTO_TYPE,		/* protocol, from arps perspective */
	sizeof(areq_t),			/* target addr offset */
	IP_ADDR_LEN,			/* target addr_length */
	0,				/* flags */
	sizeof(areq_t) + IP_ADDR_LEN,	/* sender addr offset */
	IP_ADDR_LEN,			/* sender addr length */
	6,				/* xmit_count */
	1000,				/* (re)xmit_interval in milliseconds */
	4				/* max # of requests to buffer */
	/* anything else filled in by the code */
};

static	arc_t	ip_aru_template = {
	AR_INTERFACE_UP,
	sizeof(arc_t),		/* Name offset */
	sizeof(arc_t)		/* Name length (set by ill_arp_alloc) */
};

static	arc_t	ip_ard_template = {
	AR_INTERFACE_DOWN,
	sizeof(arc_t),		/* Name offset */
	sizeof(arc_t)		/* Name length (set by ill_arp_alloc) */
};

static	arma_t	ip_arma_multi_bcast_template = {
	AR_MAPPING_ADD,
	sizeof(arma_t) + 3*IP_ADDR_LEN + IP_MAX_ADDR_LENGTH,
				/* Name offset */
	sizeof(arma_t),		/* Name length (set by ill_arp_alloc) */
	IP_ARP_PROTO_TYPE,	
	sizeof(arma_t),				/* proto_addr_offset */
	IP_ADDR_LEN,				/* proto_addr_length */
	sizeof(arma_t) + IP_ADDR_LEN,		/* proto_mask_offset */
	sizeof(arma_t) + 2*IP_ADDR_LEN,		/* proto_extract_mask_offset */
	ACE_F_PERMANENT | ACE_F_MAPPING,	/* flags */
	sizeof(arma_t) + 3*IP_ADDR_LEN,		/* hw_addr_offset */	
	IP_MAX_ADDR_LENGTH,			/* hw_addr_length */
	0,					/* hw_mapping_start */
};

static	arma_t	ip_arma_multi_template = {
	AR_MAPPING_ADD,
	sizeof(arma_t) + 3*IP_ADDR_LEN + IP_PHYS_MULTI_ADDR_LENGTH,
				/* Name offset */
	sizeof(arma_t),		/* Name length (set by ill_arp_alloc) */
	IP_ARP_PROTO_TYPE,	
	sizeof(arma_t),				/* proto_addr_offset */
	IP_ADDR_LEN,				/* proto_addr_length */
	sizeof(arma_t) + IP_ADDR_LEN,		/* proto_mask_offset */
	sizeof(arma_t) + 2*IP_ADDR_LEN,		/* proto_extract_mask_offset */
	ACE_F_PERMANENT | ACE_F_MAPPING,	/* flags */
	sizeof(arma_t) + 3*IP_ADDR_LEN,		/* hw_addr_offset */	
	IP_PHYS_MULTI_ADDR_LENGTH,		/* hw_addr_length */
	IP_ARP_HW_MAPPING_START,		/* hw_mapping_start */
};

static	ipft_t	ip_ioctl_ftbl[] = {
	{ IP_IOC_IRE_DELETE, ip_ire_delete, sizeof(ipid_t), 0 },
	{ IP_IOC_IRE_DELETE_NO_REPLY, ip_ire_delete, sizeof(ipid_t),
		IPFT_F_NO_REPLY },
	{ IP_IOC_IRE_ADVISE_NO_REPLY, ip_ire_advise, sizeof(ipic_t),
		IPFT_F_NO_REPLY },
	{ 0 }
};

/* Simple ICMP IP Header Template */
static	ipha_t icmp_ipha = {
	IP_SIMPLE_HDR_VERSION, 0, 0, 0, 0, 0, IPPROTO_ICMP
};

/* Flag descriptors for ip_ipif_report */
static	nv_t	ipif_nv_tbl[] = {
	{ IFF_UP,		"UP" },
	{ IFF_RUNNING,		"RUNNING" },
	{ IFF_LOOPBACK,		"LOOPBACK" },
	{ IFF_NOARP,		"NOARP" },
	{ IFF_NOTRAILERS,	"NOTRAILERS" },
	{ IFF_DEBUG,		"DEBUG" },
	{ IFF_BROADCAST,	"BROADCAST" },
#ifdef	  IFF_PRIVATE
	{ IFF_PRIVATE,		"PRIVATE" },
#endif
#ifdef	  IFF_PROMISC
	{ IFF_PROMISC,		"PROMISC" },
#endif
#ifdef	  IFF_ALLMULTI
	{ IFF_ALLMULTI,		"ALLMULTI" },
#endif
#ifdef	  IFF_INTELLIGENT
	{ IFF_INTELLIGENT,	"INTELLIGENT" },
#endif
#ifdef	  IFF_POINTOPOINT
	{ IFF_POINTOPOINT,	"POINTOPOINT" },
#endif
	{ IFF_MULTICAST,	"MULTICAST" },
	{ IFF_MULTI_BCAST,	"MULTI_BCAST" },
	{ IFF_UNNUMBERED,	"UNNUMBERED" },
};

static	u_char	ip_six_byte_all_ones[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static	ip_m_t	ip_m_tbl[] = {
	{ DL_ETHER,	IP_DL_SAP,	-2, 6,	&ip_six_byte_all_ones[0] },
	{ DL_CSMACD,	IP_DL_SAP,	-2, 6,	&ip_six_byte_all_ones[0] },
	{ DL_TPB,	IP_DL_SAP,	-2, 6,	&ip_six_byte_all_ones[0] },
	{ DL_TPR,	IP_DL_SAP,	-2, 6,	&ip_six_byte_all_ones[0] },
	{ DL_FDDI,	IP_DL_SAP,	-2, 6,	&ip_six_byte_all_ones[0] }
};

static	ill_t	ill_nil;		/* Empty ILL for init. */
static	char	ipif_loopback_name[] = "lo0";
extern	ulong_t	loopback_packets;

/*
 * Common code for preparation of ARP commands.  Two points to remember:
 * 	1) The ill_name is tacked on at the end of the allocated space so
 *	   the templates name_offset field must contain the total space
 *	   to allocate less the name length.
 *
 *	2) The templates name_length field should contain the *template*
 *	   length.  We use it as a parameter to bcopy() and then write
 *	   the real ill_name_length into the name_length field of the copy.
 * (Always called as writer.)
 */
mblk_t *
ill_arp_alloc (ill, template, addr)
	ill_t	* ill;
	u_char	* template;
	u32	addr;
{
	arc_t	* arc = (arc_t *)ALIGN32(template);
	char	* cp;
	int	len;
	mblk_t	* mp;
	u_int	name_length = ill->ill_name_length;
	u_int	template_len = arc->arc_name_length;

	len = arc->arc_name_offset + name_length;
	mp = allocb(len, BPRI_HI);
	if (!mp)
		return nilp(mblk_t);
	cp = (char *)mp->b_rptr;
	mp->b_wptr = (u_char *)&cp[len];
	if (template_len)
		bcopy((char *)template, cp, template_len);
	if (len > template_len)
		bzero(&cp[template_len], len - template_len);
	mp->b_datap->db_type = M_PROTO;

	arc = (arc_t *)ALIGN32(cp);
	arc->arc_name_length = name_length;
	cp = (char *)arc + arc->arc_name_offset;
	bcopy((char *)ill->ill_name, cp, name_length);

	if (addr) {
		area_t	* area = (area_t *)ALIGN32(mp->b_rptr);

		cp = (char *)area + area->area_proto_addr_offset;
		bcopy((char *)&addr, cp, area->area_proto_addr_length);
		if ( area->area_cmd == AR_ENTRY_ADD ) {
			cp = (char *)area;
			len = area->area_proto_addr_length;
			if (area->area_proto_mask_offset)
				cp += area->area_proto_mask_offset;
			else
				cp += area->area_proto_addr_offset + len;
			while (len-- > 0)
				*cp++ = (char)~0;
		}
	}
	return mp;
}

/*
 * Completely vaporize a lower level tap and all associated interfaces.
 * ill_delete is called only out of ip_close when the device control
 * stream is being closed.  The ill structure itself is freed when
 * ip_close calls mi_close_comm.  (Always called as writer.)
 */
void
ill_delete (ill)
reg	ill_t	* ill;
{
reg	ill_t	** illp;
	mblk_t	** mpp;

	/*
	 * Make sure no upper stream is flow controlled due to this interface
	 * stream being flow controlled. Note that ipif_down is handled
	 * automatically since the DL_UNBIND_REQ will cause the driver to
	 * send M_FLUSH(FLUSHRW) which will backenable ip_wsrv if canput()
	 * had failed on the driver stream.
	 */
	ip_wsrv(ill->ill_wq);

	/*
	 * Nuke all interfaces.  ipif_free will take down the interface,
	 * remove it from the list, and free the data structure.
	 */
	while (ill->ill_ipif)
		ipif_free(ill->ill_ipif);
	/*
	 * ill_down will arrange to blow off any IRE's dependent on this
	 * ILL, and shut down fragmentation reassembly.
	 */
	ill_down(ill);
	/* Take us out of the list of ILLs. */
	for (illp = &ill_g_head; illp[0]; illp = &illp[0]->ill_next) {
		if (illp[0] == ill) {
			illp[0] = ill->ill_next;
			break;
		}
	}
	if (ill->ill_frag_timer_mp) {
		/* Free the frag timer. */
		mi_timer_free(ill->ill_frag_timer_mp);
		ill->ill_frag_timer_mp = nilp(mblk_t);
		ill->ill_name = nilp(char);
		ill->ill_name_length = 0;
	}
	/* Free all retained control messages. */
	mpp = &ill->ill_first_mp_to_free;
	do {
		if (mpp[0]) {
			freemsg(mpp[0]);
			mpp[0] = nilp(mblk_t);
		}
	} while (mpp++ != &ill->ill_last_mp_to_free);
	
	if ( ip_timer_ill == ill ) {
		ill_t	*ill2;
		/* The IRE expiration timer is running on this ill. */
		for ( ill2 = ill_g_head; ill2; ill2 = ill2->ill_next )
			if ( ill2 != ip_timer_ill && ill2->ill_rq != NULL)
				break;
		if ( ill2 ) {
			/* Ask mi_timer to switch queues. */
			ip_timer_ill = ill2;
			mi_timer(ip_timer_ill->ill_rq, ip_timer_mp, -2);
		} else {
			mi_timer_free(ip_timer_mp);
			ip_timer_mp = nilp(mblk_t);
			ip_timer_ill = nilp(ill_t);
		}
	}
	
	if ( igmp_timer_ill == ill ) {
		ill_t	*ill2;
		/* The IGMP timer is running on this ill. */
		for ( ill2 = ill_g_head; ill2; ill2 = ill2->ill_next )
			if ( ill2 != igmp_timer_ill && ill2->ill_rq != NULL)
				break;
		if ( ill2 ) {
			/* Ask mi_timer to switch queues. */
			igmp_timer_ill = ill2;
			mi_timer(igmp_timer_ill->ill_rq, igmp_timer_mp, -2);
		} else {
			mi_timer_free(igmp_timer_mp);
			igmp_timer_mp = nilp(mblk_t);
			igmp_timer_ill = nilp(ill_t);
		}
	}
	
	/* That's it.  mi_close_comm will free the ill itself. */
}

/*
 * Concatenate together a physical address and a sap.
 *
 * Sap_lengths are interpreted as follows:
 *   sap_length == 0	==>	no sap
 *   sap_length > 0	==>	sap is at the head of the dlpi address
 *   sap_length < 0	==>	sap is at the tail of the dlpi address
 */
staticf void
ill_dlur_copy_address(phys_src, phys_length, sap_src, sap_length, dst)
	u_char	* phys_src;
	u_int	phys_length;
	u_long	sap_src;
	int	sap_length;	/* With sign */
	u_char	* dst;
{
	if ( sap_length == 0 ) {
		bcopy((char *)phys_src, (char *)dst, phys_length);
	} else if ( sap_length < 0 ) {
		sap_length = - sap_length;
		bcopy((char *)phys_src, (char *)dst, phys_length);
		bcopy((char *)&sap_src + sizeof(sap_src) - sap_length, 
		      (char *)dst + phys_length, 
		      sap_length);
	} else {
		bcopy((char *)&sap_src + sizeof(sap_src) - sap_length, 
		      (char *)dst, 
		      sap_length);
		bcopy((char *)phys_src, (char *)dst + sap_length, 
		      phys_length);
	}		
}

/*
 * Generate a dl_unitdata_req mblk for the device and address given.
 * addr_length is the length of the physical portion of the address.
 * TRUE? In any case, addr_length is taken to be the entire length of the
 * dlpi address, including the absolute value of sap_length.
 */
mblk_t *
ill_dlur_gen (addr, addr_length, sap, sap_length)
	u_char	* addr;
	u_int	addr_length;
	u_long	sap;
	int	sap_length;
{
	dl_unitdata_req_t * dlur;
	mblk_t	* mp;
	int	abs_sap_length;		/* absolute value */
	
	if (!addr)
		return nilp(mblk_t);

	abs_sap_length = (sap_length < 0) ? -sap_length : sap_length;
	mp = ip_dlpi_alloc((int)(sizeof(*dlur) + addr_length + abs_sap_length),
		DL_UNITDATA_REQ);
	if ( !mp )
		return nilp(mblk_t);
	dlur = (dl_unitdata_req_t *)ALIGN32(mp->b_rptr);
	/* HACK: accomodate incompatible DLPI drivers */
	if (addr_length == 8)
		addr_length = 6;
	dlur->dl_dest_addr_length = addr_length + abs_sap_length;
	dlur->dl_dest_addr_offset = sizeof(*dlur);
	dlur->dl_priority.dl_min = 0;
	dlur->dl_priority.dl_max = 0;
	ill_dlur_copy_address(addr, addr_length, sap, sap_length, 
			      (u_char *)&dlur[1]);
	return mp;
}

/*
 * ill_down is called either out of ill_delete when the device control stream
 * is closing, or if an M_ERROR or M_HANGUP is passed up from the device.  We
 * shut down all associated interfaces, but do not tear down any plumbing or
 * ditch any information.  (Always called as writer.)
 */
void
ill_down (ill)
reg	ill_t	* ill;
{
	ipif_t	* ipif;

	/* Down the interfaces, without destroying them. */
	for (ipif = ill->ill_ipif; ipif; ipif = ipif->ipif_next)
		ipif_down(ipif);
	/* Blow off any IREs dependent on this ILL. */
	ire_walk(ill_downi, (char *)ill);
	/* Shut down fragmentation reassembly. */
	if (ill->ill_frag_timer_mp)
		mi_timer(ill->ill_rq, ill->ill_frag_timer_mp, -1L);
	ill->ill_frag_timer_running = false;
	/* Ditch any incomplete packets. */
	(void)ill_frag_timeout(ill, (u_long)0);
}

/*
 * ire_walk routine used to delete every IRE that depends on queues
 * associated with 'ill'.  (Always called as writer.)
 */
staticf void
ill_downi (ire, ill)
reg	ire_t	* ire;
	char	* ill;
{
	queue_t	* rq = ((ill_t *)ALIGN32(ill))->ill_rq;
	queue_t	* wq = ((ill_t *)ALIGN32(ill))->ill_wq;
	queue_t * stq = ire->ire_stq;
	queue_t * rfq = ire->ire_rfq;

	if (stq == rq || stq == wq  || rfq == rq || rfq == wq)
		ire_delete(ire);
}

void	ire_fastpath_update(   ire_t * ire, char * arg   );

/* Consume an M_IOCACK of the fastpath probe. */
void
ill_fastpath_ack (ill, mp)
	ill_t	* ill;
	mblk_t	* mp;
{
	mblk_t	* mp1 = mp;

	/* Free the M_IOCACK mblk, hold on to the data */
	mp = mp->b_cont;
	freeb(mp1);
	if (!mp)
		return;
	if (mp->b_cont) {
		/*
		 * Update all IRE's that are not in fastpath mode and 
		 * have an ll_hdr_mp that matches mp->b_cont that have
		 * an ire_wq matching this ill.
		 */
		ire_walk_wq(ill->ill_wq, ire_fastpath_update, (char *)mp);
		mp1 = mp->b_cont;
		freeb(mp);
		mp = mp1;
	} else {
		ip0dbg(("ip_fastpath_ack:  no b_cont\n"));
		freeb(mp);
		return;
	}
	if (ill->ill_hdr_mp)
		freemsg(ill->ill_hdr_mp);
	ill->ill_hdr_mp = mp;
	ill->ill_hdr_length = (mp->b_wptr - mp->b_rptr);
}

/*
 * Throw an M_IOCTL message downstream asking "do you know fastpath?"
 * The data portion of the request is a dl_unitdata_req_t template for
 * what we would send downstream in the absence of a fastpath confirmation.
 */
void
ill_fastpath_probe (ill, dlur_mp)
	ill_t	* ill;
	mblk_t	* dlur_mp;
{
	struct iocblk	* ioc;
	mblk_t	* mp;

	if (!dlur_mp)
		return;
 	mp = allocb(sizeof(struct iocblk), BPRI_HI);
	if (!mp)
		return;
	mp->b_cont = copyb(dlur_mp);
	if (!mp->b_cont) {
		freeb(mp);
		return;
	}
	mp->b_datap->db_type = M_IOCTL;
	bzero((char *)mp->b_rptr, sizeof(*ioc));
	ioc = (struct iocblk *)ALIGN32(mp->b_rptr);
	mp->b_wptr = (u_char *)&ioc[1];
	ioc->ioc_cmd = DL_IOC_HDR_INFO;
	ioc->ioc_count = msgdsize(mp->b_cont);
	putnext(ill->ill_wq, mp);
}

/*
 * This routine is called to scan the fragmentation reassembly table for the
 * This routine is called to scan the fragmentation reassembly table for the
 * specified ILL for any packets that are starting to smell.  dead_interval is
 * the maximum time in seconds that will be tolerated.  It will either be
 * the value specified in ip_g_frag_timeout, or zero if the ILL is shutting
 * down and it is time to blow everything off.  (May be called as writer.)
 */
boolean_t
ill_frag_timeout (ill, dead_interval)
	ill_t	* ill;
	u_long	dead_interval;
{
	ipfb_t	* ipfb;
	ipfb_t	* endp;
reg	ipf_t	* ipf;
	ipf_t	* ipfnext;
reg	mblk_t	* mp;
reg	u_long	current_time = time_in_secs;
	boolean_t	some_outstanding = false;

	ipfb = ill->ill_frag_hash_tbl;
	if (!ipfb)
		return false;
	endp = &ipfb[ILL_FRAG_HASH_TBL_COUNT];
	/* Walk the frag hash table. */
	for ( ; ipfb < endp; ipfb++) {
		mutex_enter(&ipfb->ipfb_lock);
		while ((ipf = ipfb->ipfb_ipf) != 0) {
			if ((current_time - ipf->ipf_timestamp)<dead_interval){
				/* Note that there are more outstanding. */
				some_outstanding = true;
				break;
			}
			/* Time's up.  Get it out of here. */
			ipfnext = ipf->ipf_hash_next;
			if ( ipfnext )
				ipfnext->ipf_ptphn = ipf->ipf_ptphn;
			*ipf->ipf_ptphn = ipfnext;
			mp = ipf->ipf_mp->b_cont;
			for ( ; mp; mp = mp->b_cont) {
				/* Extra points for neatness. */
				IP_REASS_SET_START(mp, 0);
				IP_REASS_SET_END(mp, 0);
			}
			mp = ipf->ipf_mp->b_cont;
			mp->b_rptr -= ipf->ipf_stripped_hdr_len;
			ill->ill_frag_count -= ipf->ipf_count;
			ASSERT(ipfb->ipfb_count >= ipfb->ipfb_count);
			ipfb->ipfb_count -= ipf->ipf_count;
			freeb(ipf->ipf_mp);
			BUMP_MIB(ip_mib.ipReasmFails);
			icmp_time_exceeded(ill->ill_wq, mp,
				ICMP_REASSEMBLY_TIME_EXCEEDED);
		}
		mutex_exit(&ipfb->ipfb_lock);
	}
	/*
	 * A non-dieing ILL will use the return value to decide whether to
	 * restart the frag timer.
	 */
	return some_outstanding;
}

/*
 * This routine is called when the approximate count of mblk memory used
 * for the specified ILL has exceeded max_count.
 *
 * The fragmentation reassembly table is scaned for the oldest fragment
 * queue, its resources freed, while the ILL's count is to high.
 */
void
ill_frag_prune (ill, max_count)
	ill_t	* ill;
	u_long	max_count;
{
	ipfb_t	* ipfb;
	ipf_t	* ipf;
	ipf_t	** ipfp;
	mblk_t	* mp;
	mblk_t	* tmp;
	u_long	count;

	/*
	 * While the reassembly list for this ILL is to big
	 * prune a fragment queue by age, oldest first.
	 * Note that the per ILL count is approximate, while
	 * the per frag hash bucket counts are accurate.
	 */
	while (ill->ill_frag_count > max_count) {
		int	ix;
		ipfb_t	* oipfb = nilp(ipfb_t);
		u_long	oldest = MAX_ULONG;

		count = 0;
		for (ix = 0; ix < ILL_FRAG_HASH_TBL_COUNT; ix++) {
			ipfb = &ill->ill_frag_hash_tbl[ix];
			ipfp = &ipfb->ipfb_ipf;
			ipf = ipfp[0];
			if (ipf && ipf->ipf_gen < oldest) {
				oldest = ipf->ipf_gen;
				oipfb = ipfb;
			}
			count += ipfb->ipfb_count;
		}
		/* Refresh the per ILL count */
		ill->ill_frag_count = count;
		if (oipfb == nilp(ipfb_t)) {
			ill->ill_frag_count = 0;
			break;
		}
		if (count <= max_count)
			/* Somebody beat us to it, nothing to do */
			return;
		mutex_enter(&oipfb->ipfb_lock);
		ipfp = &oipfb->ipfb_ipf;
		ipf = ipfp[0];
		if (ipf == nilp(ipf_t)) {
			/* Somebody beat us to it, try again */
			mutex_exit(&oipfb->ipfb_lock);
			continue;
		}
		count = ipf->ipf_count;
		mp = ipf->ipf_mp;
		ipf = ipf->ipf_hash_next;
		if (ipf)
			ipf->ipf_ptphn = ipfp;
		ipfp[0] = ipf;
		for (tmp = mp; tmp; tmp = tmp->b_cont) {
			IP_REASS_SET_START(tmp, 0);
			IP_REASS_SET_END(tmp, 0);
		}
		ill->ill_frag_count -= count;
		ASSERT(oipfb->ipfb_count >= count);
		oipfb->ipfb_count -= count;
		mutex_exit(&oipfb->ipfb_lock);
		freemsg(mp);
	}
}

/*
 * ill_init is called by ip_open when a device control stream is opened.
 * It does a few initializations, and shoots a DL_INFO_REQ message down
 * to the driver.  The response is later picked up in ip_rput_dlpi and
 * used to set up default mechanisms for talking to the driver.  (Always
 * called as writer.)
 */
int
ill_init (q, ill)
	queue_t	* q;
reg	ill_t	* ill;
{
	mblk_t	* areq_mp;
	int	count;
	dl_info_req_t	* dlir;
	mblk_t	* info_mp;
	mblk_t	* mp;
	char	* cp;

	/* Start clean. */
	*ill = ill_nil;
	/*
	 * Assume that there will be a RESOLVER upstream until we get an
	 * info ack with no broadcast address.
	 */
	ill->ill_subnet_type = IRE_RESOLVER;
	ill->ill_rq = q;
	q = WR(q);
	ill->ill_wq = q;
	/*
	 * Don't look!  We walk downstream to pick up the name of the
	 * driver so we can construct the ILL name.
	 */
	do {
		q = q->q_next;
	} while (q->q_next);
	cp = q->q_qinfo->qi_minfo->mi_idname;
	if (!cp  ||  !*cp)
		return ENXIO;

	info_mp = allocb(sizeof(dl_info_req_t), BPRI_HI);
	if (!info_mp)
		return EAGAIN;

	/*
	 * Allocate a timer control block with sufficient space to contain
	 * our fragment hash table and the device name.  We later use the
	 * timer to age the fragments in the hash table.
	 */
	mp = mi_timer_alloc(ILL_FRAG_HASH_TBL_SIZE + mi_strlen(cp) + 4);
	if (!mp) {
		freemsg(info_mp);
		return EAGAIN;
	}
	ill->ill_frag_timer_mp = mp;
	ill->ill_frag_hash_tbl = (ipfb_t *)ALIGN32(mp->b_rptr);
	ill->ill_name = (char *)&mp->b_rptr[ILL_FRAG_HASH_TBL_SIZE];
	for (count = 0; count < ILL_FRAG_HASH_TBL_COUNT; count++) {
		ill->ill_frag_hash_tbl[count].ipfb_ipf = nilp(ipf_t);
		/* XXX should mutex_destory this when doing the mi_timer_free */
		mutex_init(&ill->ill_frag_hash_tbl[count].ipfb_lock,
			   "ip hash list lock", MUTEX_DEFAULT, (void *)NULL);
		ill->ill_frag_hash_tbl[count].ipfb_count = 0;
	}

	/*
	 * Construct a name from our devices 'q_qinfo->qi_minfo->mi_idname'
	 * and the lowest 'unit number' that is unused.  We assume here that
	 * devices are opened in ascending order.
	 */
	count = -1;
	do {
		if (++count >= 1000) {
			mi_timer_free(mp);
			freemsg(info_mp);
			return ENXIO;
		}
		mi_sprintf(ill->ill_name, "%s%d", cp, count);
		ill->ill_name_length = mi_strlen(ill->ill_name) + 1;
	} while (ill_lookup_on_name(ill->ill_name, ill->ill_name_length));

	/* Remember the unit number for subsequent DLPI attach requests. */
	ill->ill_ppa = count;

	/* Construct a resolver template. */
	areq_mp = ill_arp_alloc(ill, (u_char *)&ip_areq_template, 0);
	if ( !areq_mp ) {
		mi_timer_free(mp);
		freemsg(info_mp);
		return EAGAIN;
	}

	ill->ill_resolver_mp = areq_mp;

	/* Chain us in at the end of the ill list. */
	ill->ill_next = nilp(ill_t);
	if (ill_g_head) {
		ill_t	* ill1;

		for (ill1 = ill_g_head; ill1->ill_next; ill1 = ill1->ill_next)
			;
		ill1->ill_next = ill;
	} else
		ill_g_head = ill;

	/* Send down the Info Request to the driver. */
	info_mp->b_datap->db_type = M_PROTO;
	dlir = (dl_info_req_t *)ALIGN32(info_mp->b_rptr);
	info_mp->b_wptr = (u_char *)&dlir[1];
	dlir->dl_primitive = DL_INFO_REQ;
	putnext(ill->ill_wq, info_mp);
	
	/* If there is no IRE expiration timer running, get one started. */
	if ( !ip_timer_mp ) {
		ip_timer_mp = mi_timer_alloc(0);
		if ( ip_timer_mp ) {
			ip_timer_ill = ill;
			ip_ire_time_elapsed += ip_timer_interval;
			mi_timer(ill->ill_rq, ip_timer_mp, ip_timer_interval);
		}
	}
	/* If there is no IGMP timer running, get one started. */
	if ( !igmp_timer_mp ) {
		igmp_timer_mp = mi_timer_alloc(0);
		if ( igmp_timer_mp ) {
			igmp_timer_ill = ill;
			mi_timer(ill->ill_rq, igmp_timer_mp, 
				 igmp_timer_interval);
		}
	}
	/* Wait for the DL_INFO_ACK */
	while (ill->ill_ipif == NULL)
		if (!qwait_sig(ill->ill_wq)) {
			ill_delete(ill);
			return (EINTR);
		}
	/* Frag queue limit stuff */
	ill->ill_frag_count = 0;
	ill->ill_ipf_gen = 0;
	
	return 0;
}

static int
loopback_kstat_update(kstat_t *ksp, int rw)
{
	kstat_named_t *kn = KSTAT_NAMED_PTR(ksp);

	if (rw == KSTAT_WRITE)
		return (EACCES);
	kn[0].value.ul = loopback_packets;
	kn[1].value.ul = loopback_packets;
	return (0);
}

/*
 * Return a pointer to the ill which matches the supplied name.  Note that
 * the ill name length includes the null termination character.  (May be
 * called as writer.)
 */
ill_t *
ill_lookup_on_name (name, namelen)
	char	* name;
	u_int	namelen;
{
reg	ill_t	* ill;
reg	ipif_t	* ipif;
	kstat_t	*ksp;
	kstat_named_t	*kn;

	if (namelen == 0)
		return nilp(ill_t);
	for (ill = ill_g_head; ill; ill = ill->ill_next) {
		if (ill->ill_name_length == namelen) {
		reg	int	i1 = namelen;

			do {
				if (--i1 < 0)
					return ill;
			} while (ill->ill_name[i1] == name[i1]);
		}
	}
	/*
	 * Couldn't find it.  Does this happen to be a lookup for the
	 * loopback device?
	 */
	if (namelen != sizeof(ipif_loopback_name)
	||  mi_strcmp(name, ipif_loopback_name) != 0) {
		/* Nope, some bogon. */
		return nilp(ill_t);
	}

	/* Create the loopback device on demand */
	ill = (ill_t *)ALIGN32(mi_alloc(sizeof(ill_t) + 
				       sizeof(ipif_loopback_name),
				       BPRI_MED));
	if (!ill)
		return nilp(ill_t);

	*ill = ill_nil;
	ill->ill_max_frag = STRMSGSZ;
	ill->ill_name = ipif_loopback_name;
	ill->ill_name_length = sizeof(ipif_loopback_name);
	/* No resolver here. */
	ill->ill_subnet_type = IRE_LOOPBACK;

	ipif = ipif_allocate(ill, 0L, IRE_LOOPBACK);
	if (!ipif) {
		mi_free((char *)ill);
		return nilp(ill_t);
	}

	/* Set up default loopback address and mask. */
#ifdef	_BIG_ENDIAN
	ipif->ipif_local_addr = 0x7F000001;
	ipif->ipif_net_mask = (u32)0xFF000000;
#else
	ipif->ipif_local_addr = 0x0100007F,
	ipif->ipif_net_mask = 0x000000FF;
#endif
	ipif->ipif_flags = IFF_RUNNING | IFF_LOOPBACK | IFF_MULTICAST;

	ill->ill_next = ill_g_head;
	ill_g_head = ill;

	/* Export loopback interface statistics */
	if ((ksp = kstat_create("lo", 0, ipif_loopback_name, "net",
	    KSTAT_TYPE_NAMED, 2, 0)) == nilp(kstat_t))
		return ill;
	ksp->ks_update = loopback_kstat_update;
	kn = KSTAT_NAMED_PTR(ksp);
	kstat_named_init(&kn[0], "ipackets", KSTAT_DATA_ULONG);
	kstat_named_init(&kn[1], "opackets", KSTAT_DATA_ULONG);
	kstat_install(ksp);

	return ill;
}

/*
 * Named Dispatch routine to produce a formatted report on all ILLs.
 * This report is accessed by using the ndd utility to "get" ND variable
 * "ip_ill_status".
 */
/* ARGSUSED */
int
ip_ill_report (q, mp, arg)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	arg;
{
	ill_t	* ill;
	
	mi_mpprintf(mp, "ILL      rq       wq       upcnt mxfrg err name");
                      /* 01234567 01234567 01234567 12345 12345 123 xxxxxxxx  */
	for ( ill = ill_g_head; ill; ill = ill->ill_next ) {
		mi_mpprintf(mp, "%08x %08x %08x %05d %05d %03d %s",
			ill, ill->ill_rq, ill->ill_wq, ill->ill_ipif_up_count,
			ill->ill_max_frag, ill->ill_error, ill->ill_name);
	}
	return 0;
}

/*
 * Named Dispatch routine to produce a formatted report on all IPIFs.
 * This report is accessed by using the ndd utility to "get" ND variable
 * "ip_ipif_status".
 */
/* ARGSUSED */
int
ip_ipif_report (q, mp, arg)
	queue_t	* q;
	mblk_t	* mp;
	caddr_t	arg;
{
	char	buf1[16];
	char	buf2[16];
	char	buf3[16];
	char	buf4[16];
	char	buf[32];
	ill_t	* ill;
	ipif_t	* ipif;
	nv_t	* nvp;
	u_int	flags;
	ippc_t ippc1;
	
	mi_mpprintf(mp,
	  "IPIF     addr            mask            broadcast       p-p-dst         metr mtu   in/out/forward name");
        /* 01234567 123.123.123.123 123.123.123.123 123.123.123.123 123.123.123.123 0123 12345 in/out/forward sle0/1 */
	for ( ill = ill_g_head; ill; ill = ill->ill_next ) {
		for ( ipif = ill->ill_ipif; ipif; ipif = ipif->ipif_next ) {
			ippc1.ippc_addr = ipif->ipif_local_addr;
			ippc1.ippc_ib_pkt_count = ipif->ipif_ib_pkt_count;
			ippc1.ippc_ob_pkt_count = ipif->ipif_ob_pkt_count;
			ippc1.ippc_fo_pkt_count = ipif->ipif_fo_pkt_count;
			ire_walk(ire_pkt_count, (char *)&ippc1);
			mi_mpprintf(mp, "%08x %s %s %s %s %04d %05d %D/%D/%D %s",
				ipif,
				ip_dot_addr(ipif->ipif_local_addr, buf1),
				ip_dot_addr(ipif->ipif_net_mask, buf2),
				ip_dot_addr(ipif->ipif_broadcast_addr, buf3),
				ip_dot_addr(ipif->ipif_pp_dst_addr, buf4),
				ipif->ipif_metric, ipif->ipif_mtu,
				ippc1.ippc_ib_pkt_count,
				ippc1.ippc_ob_pkt_count, 
				ippc1.ippc_fo_pkt_count,
				ipif_get_name(ipif, buf, sizeof(buf)));
			flags = ipif->ipif_flags;
			/* Tack on text strings for any flags. */
			nvp = ipif_nv_tbl;
			for ( ; nvp < A_END(ipif_nv_tbl); nvp++) {
				if (nvp->nv_value & flags)
					mi_mpprintf_nr(mp, " %s", nvp->nv_name);
			}
		}
	}
	return 0;
}

/*
 * ip_ll_subnet_defaults is called when we get the DL_INFO_ACK back from the
 * driver.  We construct best guess defaults for lower level information that
 * we need.  If an interface is brought up without injection of any overriding
 * information from outside, we have to be ready to go with these defaults.
 */
void
ip_ll_subnet_defaults (ill, mp)
	ill_t	* ill;
	mblk_t	* mp;
{
	u_char	* brdcst_addr;
	u_int	brdcst_addr_length, phys_addr_length;
	int	sap_length;
	dl_info_ack_t * dlia;
	ip_m_t	* ipm;
	
	dlia = (dl_info_ack_t *)ALIGN32(mp->b_rptr);
	if ( dlia->dl_provider_style == DL_STYLE2 )
		ill->ill_needs_attach = 1;

	ipm = ip_m_lookup(ill, dlia->dl_mac_type);
	if (!ipm) {
		mi_strlog(ill->ill_rq, 1, SL_ERROR|SL_TRACE,
		"ip_ll_subnet_defaults: couldn't identify mac type %d on %s",
			dlia->dl_mac_type, ill->ill_name);
		ipm = ip_m_lookup(ill, DL_ETHER);
	}
	/* When the new DLPI stuff is ready we'll pull lengths from dlia */
	if (dlia->dl_version == DL_VERSION_2) {
		/* TODO: we should wait until the stream is attached and
		 * bound before using these values.
		 */
		brdcst_addr_length = dlia->dl_brdcst_addr_length;
		brdcst_addr = mi_offset_param(mp, dlia->dl_brdcst_addr_offset,
					      brdcst_addr_length);
		if (brdcst_addr == nilp(u_char)) {
			mi_strlog(ill->ill_rq, 1, SL_ERROR|SL_TRACE,
			  "ip_ll_subnet_defaults: malformed dl_brdcst_addr in dl_info_ack on %s",
			ill->ill_name);
			brdcst_addr_length = 0;
		}
		sap_length = dlia->dl_sap_length;
		phys_addr_length = dlia->dl_addr_length +
			(sap_length > 0 ? -sap_length : sap_length);
		ip1dbg(("ip: bcast_len %d, sap_len %d, phys_len %d\n", 
		       brdcst_addr_length, sap_length, phys_addr_length));
	} else {
		brdcst_addr_length = ipm->ip_m_brdcst_addr_length;
		brdcst_addr = ipm->ip_m_brdcst_addr;
		sap_length = ipm->ip_m_sap_length;
		phys_addr_length = brdcst_addr_length;
	}
	ill->ill_sap = ipm->ip_m_sap;
	if ( brdcst_addr_length == 0 ) {
		u_char zero_addr[128];

		freeb(ill->ill_resolver_mp);
		ill->ill_resolver_mp = nilp(mblk_t);
		ill->ill_subnet_type = IRE_SUBNET;
		bzero((char *)zero_addr, sizeof(zero_addr));
		ill->ill_resolver_mp = ill_dlur_gen(zero_addr, 
						    phys_addr_length,
						    ill->ill_sap,
						    sap_length);
	} else {
		areq_t	* areq;
		int	abs_sap_length;		/* absolute value */
	
		abs_sap_length = (sap_length < 0) ? -sap_length : sap_length;
		if ( !ill->ill_bcast_mp )
			ill->ill_bcast_mp = ill_dlur_gen(brdcst_addr,
							 brdcst_addr_length, 
							 ill->ill_sap, 
							 sap_length);
		areq = (areq_t *)ALIGN32(ill->ill_resolver_mp->b_rptr);
		bcopy((char *)&ill->ill_sap + sizeof(ill->ill_sap) - abs_sap_length, 
		      (char *)areq->areq_sap, abs_sap_length);
	}
	ill->ill_max_frag = dlia->dl_max_sdu;
	if (ill->ill_max_frag > ip_max_mtu)
		ip_max_mtu = ill->ill_max_frag;
	ill->ill_sap_length = sap_length;
	ill->ill_phys_addr_length = phys_addr_length;
	/* Clear any previous error indication. */
	ill->ill_error = 0;

	/* Allocate the first ipif on this ill - we do not want to
	 * delay its creation until the first ioctl references it since
	 * we want to report it in SIOCGIFCONF ioctls.
	 * For some reason we get 2 DL_INFO_ACK messages here thus 
	 * we need to check for duplicates by calling ipif_lookup_on_name
	 * instead of just ipif_allocate.
	 */
	(void)ipif_lookup_on_name(ill->ill_name, ill->ill_name_length);

	freemsg(mp);
}

/*
 * Perform various checks to verify that an address would make sense as a local
 * interface address.  This is currently only called when an attempt is made
 * to set a local address.
 */
staticf	boolean_t
ip_local_addr_ok (addr, subnet_mask)
	u32	addr;
	u32	subnet_mask;
{
	u32	net_mask;

	net_mask = ip_net_mask(addr);
	if (net_mask == 0
	||  addr == (u32)0
	||  addr == ~(u32)0
	||  addr == (addr & net_mask)
	||  addr == (addr | ~net_mask)
	||  addr == (addr & subnet_mask)
	||  addr == (addr | ~subnet_mask))
		return false;
	return true;
}

/*
 * TODO: make this table extendible at run time
 * Return a pointer to the mac type info for 'mac_type'
 */
/* ARGSUSED */
staticf ip_m_t *
ip_m_lookup (ill, mac_type)
	ill_t	* ill;
	u_long	mac_type;
{
	ip_m_t	* ipm;

	for (ipm = ip_m_tbl; ipm < A_END(ip_m_tbl); ipm++) {
		/* TODO: Match on ill name as well as mac type */
		if (ipm->ip_m_mac_type == mac_type)
			return ipm;
	}
	return nilp(ip_m_t);
}

/*
 * ip_siocaddrt is called (as writer) by ip_sioctl_copyin_done to complete
 * processing of an SIOCADDRT IOCTL.
 */
staticf int
ip_siocaddrt (rt)
	struct rtentry	* rt;
{
	ire_t	* ire;
	u32	dst_addr;
	ipa_t	* dst_ipaddr;
	u32	gw_addr;
	ipa_t	* gw_ipaddr;
	ire_t	* gw_ire;
	u32	net_mask;
	u_int	type;
	
	dst_ipaddr = (ipa_t *)&rt->rt_dst;
	dst_addr = *(u32 *)ALIGN32(dst_ipaddr->ip_addr);
	gw_ipaddr = (ipa_t *)&rt->rt_gateway;
	gw_addr = *(u32 *)ALIGN32(gw_ipaddr->ip_addr);
	
	/* Determine netmask */
	if (rt->rt_flags & RTF_HOST)
		net_mask = (u_long)~0;
	else
		net_mask = ip_subnet_mask(dst_addr);

	/* RTF_GATEWAY not set */
	if (!(rt->rt_flags & RTF_GATEWAY)) {
		ipif_t	*ipif;
		queue_t	*stq;
		mblk_t	*save_mp;

		/* Check for existing entry */
		if (ire_lookup_interface_exact(dst_addr, IRE_INTERFACE, gw_addr))
			return EEXIST;
		/*
		 * Get the ipif corresponding to the gw_addr
		 */
		if (!(ipif = ipif_lookup_interface(gw_addr, dst_addr)))
			return ENETUNREACH;

		stq = (ipif->ipif_subnet_type == IRE_RESOLVER)
			? ipif->ipif_rq : ipif->ipif_wq;
		/*
		 * Create a copy of the IRE_SUBNET/RESOLVER with modified 
		 * address and netmask.
		 */
		ire = ire_create(
			dst_ipaddr->ip_addr,
			(u8 *)&net_mask, 
			(u8 *)&ipif->ipif_local_addr,
			nilp(u8),
			ipif->ipif_mtu,
			ipif->ipif_resolver_mp,
			nilp(queue_t),
			stq,
			ipif->ipif_subnet_type,
			512,
			0
		);


		if (!ire)
			return ENOMEM;
		ire = ire_add(ire);
		if (ire)
			ire_delete_routes(ire);
		/* Save enough information so that we can recreate the IRE
		 * if the interface goes down and up
		 */
		save_mp = allocb(2*IP_ADDR_LEN, BPRI_MED);
		if (!save_mp)
			return 0;
		bcopy((char *)dst_ipaddr->ip_addr, (char *)save_mp->b_wptr, 
		      IP_ADDR_LEN);
		save_mp->b_wptr += IP_ADDR_LEN;
		bcopy((char *)&net_mask, (char *)save_mp->b_wptr, 
		      IP_ADDR_LEN);
		save_mp->b_wptr += IP_ADDR_LEN;
		save_mp->b_cont = ipif->ipif_saved_ire_mp;
		ipif->ipif_saved_ire_mp = save_mp;
		return 0;
	}

	/*
	 * Get an interface IRE for the specified gateway.
	 * If we don't have a SUBNET or RESOLVER IRE for the gateway,
	 * it is currently unreachable and we fail the IOCTL accordingly.
	 */
	gw_ire = ire_lookup_interface(gw_addr, IRE_INTERFACE);
	if (!gw_ire)
		return ENETUNREACH;
	
	/*
	 * We create one of three types of IREs as a result of this request.
	 * If the RTF_HOST flag is on, this is a request to assign a gateway
	 * to a particular host address.  In this case, we create an
	 * IRE_ROUTE_ASSOC for the particular destination address.  Otherwise,
	 * we check the destination address.  If it is 0.0.0.0, this is a
	 * request to add a default gateway, specified by the gateway address.
	 * In this case, we create an IRE_GATEWAY.  Otherwise, this is an
	 * association of a gateway with a particular remote network address,
	 * and we create an IRE_NET.
	 */
	if (rt->rt_flags & RTF_HOST) {
		type = IRE_ROUTE_ASSOC;
	} else if (dst_addr == 0) {
		type = IRE_GATEWAY;
	} else {
		type = IRE_NET;
	}
	
	ire = ire_lookup_exact(dst_addr, type, gw_addr);
	if ( ire )
		return EEXIST;

	/* Create the IRE. */
	ire = ire_create(
		dst_ipaddr->ip_addr,		/* dest address */
		(u_char *)&net_mask,		/* mask */
		(u_char *)&gw_ire->ire_src_addr,/* source address */
		gw_ipaddr->ip_addr,		/* gateway address */
		gw_ire->ire_max_frag,
		nilp(mblk_t),			/* no xmit header */
		nilp(queue_t),			/* no recv-from queue */
		nilp(queue_t),			/* no send-to queue */
		type,				/* IRE type */
		gw_ire->ire_rtt,
		0
	);
	if (!ire)
		return ENOMEM;

	/*
	 * POLICY: should we allow an RTF_HOST with address INADDR_ANY? 
	 * SUN/OS socket stuff does but do we really want to allow 0.0.0.0?
	 */
	
	/* Add the new IRE. */
	ire = ire_add(ire);
	if (ire)
		ire_delete_routes(ire);
	return 0;
}

/*
 * ip_siocdelrt is called (as writer) by ip_sioctl_copyin_done to complete
 * processing of an SIOCDELRT IOCTL.
 */
staticf int
ip_siocdelrt (rt)
	struct rtentry	* rt;
{
	u32	addr;
	u32	gw_addr;
	ipa_t	* ipaddr;
	ire_t	* ire;
	ipif_t	* ipif;
	u_int	type;
	
	ipaddr = (ipa_t *)&rt->rt_dst;
	addr = *(u32 *)ALIGN32(ipaddr->ip_addr);
	ipaddr = (ipa_t *)&rt->rt_gateway;
	gw_addr = *(u32 *)ALIGN32(ipaddr->ip_addr);
	
	/*
	 * Note that RTF_GATEWAY is never set on SIOCDELRT therefore
	 * we check if the gateway address is one of our interfaces first,
	 * and fall back on RTF_GATEWAY routes.
	 */
	ipif = ipif_lookup_interface(gw_addr, addr);
	/*
	 * This makes it possible to delete an original
	 * IRE_SUBNET/RESOLVER - consistent with SunOS 4.1.
	 */
	if (ipif &&
	   (ipif->ipif_ire_type == IRE_LOOPBACK) &&  
	    (ire = ipif_to_ire(ipif)) != NULL &&
	    ire_lookup_exact(addr, ire->ire_type, 0) != NULL) {
		mblk_t	** mpp;
		mblk_t	* mp;
		type = ire->ire_type;
                gw_addr = 0; 
		/* Remove from ipif_saved_ire_mp list if it is there */
		for (mpp = &ipif->ipif_saved_ire_mp; *mpp; 
		     mpp = &(*mpp)->b_cont) {
			mp = *mpp;
			if (bcmp((char *)mp->b_rptr, 
				 (char *)&ipaddr, IP_ADDR_LEN) == 0) {
				*mpp = mp->b_cont;
				freeb(mp);
				break;
			}
		}
	} else if (ipif &&
	    (ire = ipif_to_ire(ipif)) != NULL &&
	    ire_lookup_exact(addr, ire->ire_type, 0) != NULL) {
		mblk_t	** mpp;
		mblk_t	* mp;

		type = ire->ire_type;
		ire = ire_lookup_ipif(ipif, type, addr, gw_addr);
		/* Remove from ipif_saved_ire_mp list if it is there */
		for (mpp = &ipif->ipif_saved_ire_mp; *mpp; 
		     mpp = &(*mpp)->b_cont) {
			mp = *mpp;
			if (bcmp((char *)mp->b_rptr, 
				 (char *)&ipaddr, IP_ADDR_LEN) == 0) {
				*mpp = mp->b_cont;
				freeb(mp);
				break;
			}
		}
	        goto remove_ire;
	} else if (gw_addr == 0) {
		/*
		 * This makes it possible to delete IRE_ROUTE entries
		 * until the garbage collection has been implemented.
		 * TODO: Should this go away now, or is it something
		 * we still want to be able to force?
		 */
		type = IRE_ROUTE;
	} else {
		if (rt->rt_flags & RTF_HOST) {
			type = IRE_ROUTE_ASSOC;
		} else if (addr == 0) {
			type = IRE_GATEWAY;
		} else
			type = IRE_NET;
	}

	ire = ire_lookup_exact(addr, type, gw_addr);
	if ( !ire  &&  type == IRE_ROUTE_ASSOC ) {
		ire = ire_lookup_exact(addr, IRE_ROUTE_REDIRECT, gw_addr);
		if ( !ire )
			ire = ire_lookup_exact(addr, IRE_ROUTE, gw_addr);
	}
remove_ire:
	if ( !ire )
		return ESRCH;
	ire_delete_routes(ire);
	ire_delete(ire);
	return 0;
}

/*
 * Continue SIOC ioctls following copyin completion.  (Called
 * as writer when ip_sioctl_copyin_writer returns 1.)
 */
void
ip_sioctl_copyin_done (q, mp)
	queue_t	* q;
reg	mblk_t	* mp;
{
	char		* addr;
	struct arpreq	* ar;
	area_t		* area;
	int		err = 0;
	struct ifreq	* ifr;
	struct ifconf	* ifc;
	ill_t		* ill;
	struct iocblk	* iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
	ipa_t		* ipaddr;
	ipif_t		* ipif;
	ire_t		* ire;
	mblk_t		* mp1;
	mblk_t		* mp2;
	u32		u1;

	if ( !(mp1 = mp->b_cont)  ||  !(mp1 = mp1->b_cont) ) {
		err = EPROTO;
		goto done;
	}
	addr = (char *)mp1->b_rptr;
	switch ( iocp->ioc_cmd ) {
	case SIOCADDRT:
		err = ip_siocaddrt((struct rtentry *)ALIGN32(addr));
		goto done;
	case SIOCDELRT:
		err = ip_siocdelrt((struct rtentry *)ALIGN32(addr));
		goto done;
	case SIOCSIFADDR:
	case SIOCGIFADDR:
	case SIOCSIFDSTADDR:
	case SIOCGIFDSTADDR:
	case SIOCSIFFLAGS:
	case SIOCGIFFLAGS:
	case SIOCSIFMTU:
	case SIOCGIFMTU:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
	case SIOCGIFMETRIC:
	case SIOCSIFMETRIC:
	case SIOCSIFMUXID:
	case SIOCGIFMUXID:
		/* Pull out common information before we switch again. */
		ifr = (struct ifreq *)ALIGN32(addr);
		ipaddr = (ipa_t *)&ifr->ifr_addr;
		ipif = ipif_lookup_on_name(ifr->ifr_name,
			mi_strlen(ifr->ifr_name) + 1);
		if ( !ipif ) {
			err = ENXIO;
			goto done;
		}
		switch ( iocp->ioc_cmd ) {
		case SIOCSIFADDR:
			/* Set the interface address. */
			if (ipaddr->ip_family != AF_INET) {
				err = EBADADDR;
				goto done;
			}
			u1 = *(u32 *)ALIGN32(ipaddr->ip_addr);
			/* Allow address 0 in down state */
			if (!ip_local_addr_ok(u1, ipif->ipif_net_mask) 
			&&  !(u1 == 0 && (ipif->ipif_flags & IFF_UP) == 0)) {
				err = EBADADDR;
				goto done;
			}
			if (ipif->ipif_flags & IFF_UP) {
				/*
				 * If the interface is already marked up,
				 * we call ipif_down which will take care
				 * of ditching any IREs that have been set
				 * up based on the old interface address.
				 */
				ipif_down(ipif);
				/* Set the new address. */
				ipif->ipif_local_addr = u1;
				ipif_set_default(ipif);
				/*
				 * Now bring the interface back up.  If this
				 * is the only IPIF for the ILL, ipif_up
				 * will have to re-bind to the device, so
				 * we may get back EINPROGRESS, in which
				 * case, this IOCTL will get completed in
				 * ip_rput_dlpi when we see the DL_INFO_ACK.
				 */
				err = ipif_up(ipif, q, mp);
				if ( err == EINPROGRESS )
					return;
			} else {
				ipif->ipif_local_addr = u1;
				ipif_set_default(ipif);
				if (ipif->ipif_local_addr == 0
				&&  ipif->ipif_id != 0)
					ipif_free(ipif);
			}
			goto done;
		case SIOCGIFADDR:
			/* Get the interface address. */
			ipaddr->ip_family = AF_INET;
			*(u32 *)ALIGN32(ipaddr->ip_addr) =
				ipif->ipif_local_addr;
			*(u16 *)ALIGN16(ipaddr->ip_port) = 0;
			/* Break to copyout. */
			break;
		case SIOCSIFDSTADDR:
			/* Set the interface address. */
			if (ipaddr->ip_family != AF_INET) {
				err = EBADADDR;
				goto done;
			}
			/* Set point to point destination address. */
			if ((ipif->ipif_flags & IFF_POINTOPOINT) == 0) {
				printf("SIOCSIFDSTADDR: IFF_POINTOPOINT not set\n");
				/* Allow this as a means of creating logical
				 * pt-pt interfaces on top of e.g. an Ethernet.
				 * Useful for testing.
				 */
				ipif->ipif_flags |= IFF_POINTOPOINT;
			}
			if (ipif->ipif_flags & IFF_UP) {
				ipif_down(ipif);
				ipif->ipif_pp_dst_addr =
					*(u32 *)ALIGN32(ipaddr->ip_addr);
				ipif->ipif_flags |= IFF_POINTOPOINT;
				ipif->ipif_flags &= ~IFF_BROADCAST;
				err = ipif_up(ipif, q, mp);
				if (err == EINPROGRESS)
					return;
			} else {
				ipif->ipif_pp_dst_addr =
					*(u32 *)ALIGN32(ipaddr->ip_addr);
				ipif->ipif_flags |= IFF_POINTOPOINT;
				ipif->ipif_flags &= ~IFF_BROADCAST;
			}
			goto done;
		case SIOCGIFDSTADDR:
			/* Get point to point destination address. */
			if ((ipif->ipif_flags & IFF_POINTOPOINT) == 0) {
				err = EBADADDR;
				goto done;
			}
			ipaddr->ip_family = AF_INET;
			*(u32 *)ALIGN32(ipaddr->ip_addr) =
				ipif->ipif_pp_dst_addr;
			*(u16 *)ALIGN16(ipaddr->ip_port) = 0;
			break;
		case SIOCSIFFLAGS:
			/* Set interface flags. */
			{
			u_int	turn_on;
			u_int	turn_off;
			int	went_off = 0;

			/*
			 * Compare the new flags to the old, and partition
			 * into those coming on and those going off.
			 */
			turn_on = (ifr->ifr_flags ^ ipif->ipif_flags)
			& ~IFF_CANTCHANGE;
			if (!turn_on)
				goto done;
			turn_off = ipif->ipif_flags & turn_on;
			turn_on ^= turn_off;
			err = 0;
			
			/*
			 * The only flag change that we currently take action
			 * on is IFF_UP.
			 */
			if (turn_off & IFF_UP) {
				went_off = 1;
				turn_off &= ~IFF_UP;
				/*
				 * Check that nobody tries to bring down
				 * the ipif with id 0 if there are other
				 * ipif's for this ill.
				 */
				if ( ipif->ipif_id == 0
				&&   ipif->ipif_ill->ill_ipif_up_count > 1 ) {
					err = EBUSY;
					goto done;
				}
				ipif_down(ipif);
			} else if (turn_on & IFF_UP) {
				turn_on &= ~IFF_UP;
				/*
				 * Check that nobody tries to bring up the
				 * ipif with non-zero id if the ipif with
				 * zero id is not there yet.
				 */
				if ( ipif->ipif_id != 0
				&&   ipif->ipif_ill->ill_ipif_up_count == 0 ) {
					err = EBUSY;
					goto done;
				}
				err = ipif_up(ipif, q, mp);
				if (err  &&  err != EINPROGRESS)
					goto done;
			}
			
			/* Track current value of other flags. */
			ipif->ipif_flags |= turn_on;
			ipif->ipif_flags &= ~turn_off;
			if ( ipif->ipif_local_addr == 0
			&&  (ipif->ipif_flags & IFF_UP) == 0
			&&  went_off
			&&  ipif->ipif_id != 0 )
				ipif_free(ipif);

			if (err == EINPROGRESS)
				return;
			}
			goto done;
		case SIOCGIFFLAGS:
			/* Get interface flags. */
			ifr->ifr_flags = ipif->ipif_flags;
			/* Break to copyout. */
			break;
		case SIOCSIFMTU:
			/* Set interface MTU. */
#define	IP_MIN_MTU	(IP_MAX_HDR_LENGTH+8)
			if (ifr->ifr_metric > ipif->ipif_ill->ill_max_frag ||
			    ifr->ifr_metric < IP_MIN_MTU) {
				err = EINVAL;
				goto done;
			}
			ipif->ipif_mtu = ifr->ifr_metric;
			if ((ipif->ipif_flags & IFF_UP) &&
			    ipif->ipif_local_addr != 0)
				ire_walk(ipif_mtu_change, (char *)ipif);
			goto done;
		case SIOCGIFMTU:
			/* Get interface MTU. */
			ifr->ifr_metric = ipif->ipif_mtu;
			/* Break to copyout. */
			break;
		case SIOCSIFBRDADDR:
			/* Set the interface address. */
			if (ipaddr->ip_family != AF_INET) {
				err = EBADADDR;
				goto done;
			}
			if (!(ipif->ipif_flags & IFF_BROADCAST)) {
				err = EBADADDR;
				goto done;
			}
			/* Set interface broadcast address. */
			u1 = *(u32 *)ALIGN32(ipaddr->ip_addr);
			if (ipif->ipif_flags & IFF_UP) {
				/*
				 * If we are already up, make sure the new
				 * broadcast address makes sense.  If it does,
				 * there should be an IRE for it already.
				 */
				ire_t	* ire;
				ire = ire_lookup_broadcast(u1, ipif);
				if ( !ire ) {
					err = EBADADDR;
					goto done;
				}
			}
			ipif->ipif_broadcast_addr = u1;
			goto done;
		case SIOCGIFBRDADDR:
			/* Get interface broadcast address. */
			if ( !(ipif->ipif_flags & IFF_BROADCAST) ) {
				err = EBADADDR;
				goto done;
			}
			ipaddr->ip_family = AF_INET;
			*(u32 *)ALIGN32(ipaddr->ip_addr) = ipif->ipif_broadcast_addr;
			*(u16 *)ALIGN16(ipaddr->ip_port) = 0;
			/* Break to copyout. */
			break;
		case SIOCSIFNETMASK:
			/* Set interface net mask. */
			if (ipaddr->ip_family != AF_INET) {
				err = EBADADDR;
				goto done;
			}
			err = ip_sioctl_newmask(ipif, *(u32 *)
						ALIGN32(ipaddr->ip_addr));
			goto done;
		case SIOCGIFNETMASK:
			/* Get interface net mask. */
			ipaddr->ip_family = AF_INET;
			*(u32 *)ALIGN32(ipaddr->ip_addr) = ipif->ipif_net_mask;
			*(u16 *)ALIGN16(ipaddr->ip_port) = 0;
			/* Break to copyout. */
			break;
		case SIOCSIFMETRIC:
			/*
			 * Set interface metric.  We don't use this for
			 * anything but we keep track of it in case it is
			 * important to routing applications or such.
			 */
			ipif->ipif_metric = ifr->ifr_metric;
			goto done;
		case SIOCGIFMETRIC:
			/* Get interface metric. */
			ifr->ifr_metric = ipif->ipif_metric;
			/* Break to copyout. */
			break;
		case SIOCSIFMUXID:
			/*
			 * Set the muxid returned from I_PLINK.
			 */
			ipif->ipif_ill->ill_ip_muxid = ifr->ifr_ip_muxid;
			ipif->ipif_ill->ill_arp_muxid = ifr->ifr_arp_muxid;
			goto done;
		case SIOCGIFMUXID:
			/*
			 * Get the muxid saved in ill for I_PUNLINK.
			 */
			ifr->ifr_ip_muxid = ipif->ipif_ill->ill_ip_muxid;
			ifr->ifr_arp_muxid = ipif->ipif_ill->ill_arp_muxid;

			/* Break to copyout. */
			break;
		}
		/* Break to copyout. */
		break;
	case SIOCGIFCONF:
	/*
	 * The original SIOCGIFCONF passed in a struct ifconf which specified
	 * the user buffer address and length into which the list of struct
	 * ifreqs was to be copied.  Since AT&T Streams does not seem to
	 * allow M_COPYOUT to be used in conjunction with I_STR IOCTLS,
	 * the SIOCGIFCONF operation was redefined to simply provide
	 * a large output buffer into which we are supposed to jam the ifreq
	 * array.  The same ioctl command code was used, despite the fact that
	 * both the applications and the kernel code had to change, thus making
	 * it impossible to support both interfaces.
	 *
	 * For reasons not good enough to try to explain, the following
	 * algorithm is used for deciding what to do with one of these:
	 * If the IOCTL comes in as an I_STR, it is assumed to be of the new
	 * form with the output buffer coming down as the continuation message.
	 * If it arrives as a TRANSPARENT IOCTL, it is assumed to be old style,
	 * and we have to copy in the ifconf structure to find out how big the
	 * output buffer is and where to copy out to.  Sure no problem...
	 *
	 */
		ifc = nilp(struct ifconf);
		if ( (mp1->b_wptr - mp1->b_rptr) == sizeof(struct ifconf) ) {
			/*
			 * Must be (better be!) continuation of a TRANSPARENT
			 * IOCTL.  We just copied in the ifconf structure.
			 */
			ifc = (struct ifconf *)ALIGN32(addr);
			/* 
			 * Allocate a buffer to hold the requested
			 * information.
			 */
			mp1 = mi_copyout_alloc(q, mp, ifc->ifc_buf, 
					       ifc->ifc_len);
			if (!mp1)
				return;
			mp1->b_wptr = mp1->b_rptr + ifc->ifc_len;
		}
		bzero((char *)mp1->b_rptr, mp1->b_wptr - mp1->b_rptr);
		ifr = (struct ifreq *)ALIGN32(mp1->b_rptr);
		for ( ill = ill_g_head;  ill; ill = ill->ill_next ) {
			for (ipif = ill->ill_ipif; ipif; ipif=ipif->ipif_next) {
				ipa_t	* ipa;
				if ((u_char *)&ifr[1] > mp1->b_wptr) {
					err = EINVAL;
					goto done;
				}
				(void)ipif_get_name(ipif, ifr->ifr_name,
					sizeof(ifr->ifr_name));
				ipa = (ipa_t *)&ifr->ifr_addr;
				ipa->ip_family = AF_INET;
				bcopy((char *)&ipif->ipif_local_addr,
				    (char *)ipa->ip_addr, IP_ADDR_LEN);
				ifr++;
			}
		}
		mp1->b_wptr = (u_char *)ifr;
		if ( ifc )
			ifc->ifc_len = (u_char *)ifr - mp1->b_rptr;
		mi_copyout(q, mp);
		return;

#ifdef SIOCGIFNUM
	case SIOCGIFNUM: {
		/* Get number of interfaces. */
		int num = 0;
		int *nump = (int *)ALIGN32(addr);

		for ( ill = ill_g_head;  ill; ill = ill->ill_next )
			for (ipif = ill->ill_ipif; ipif; ipif=ipif->ipif_next)
				num++;
		
		*nump = num;
		/* Break to copyout. */
		break;
	}
#endif
	case IF_UNITSEL: {
		int ppa = *(int *)ALIGN32(addr);
		ill_t	*ill = (ill_t *)q->q_ptr;
		queue_t	*q1 = q;
		char *cp;
		u_int name_length;
		ip1dbg(("ip_sioctl_copyin_done: IF_UNITSEL\n"));
		if (!q->q_next) {
			ip1dbg(("ip_sioctl_copyin_done: IF_UNITSEL: no q_next\n"));
			err = EINVAL;
			goto done;
		}
		do {
			q1 = q1->q_next;
		} while (q1->q_next);
		cp = q1->q_qinfo->qi_minfo->mi_idname;
			
		mi_sprintf(ill->ill_name, "%s%d", cp, ppa);
		name_length = mi_strlen(ill->ill_name) + 1;
		/* Avoid finding ourself in the lookup by setting len to 0 */
		ill->ill_name_length = 0;
		if (ill_lookup_on_name(ill->ill_name, name_length)) {
			ip1dbg(("ip_sioctl_copyin_done: IF_UNITSEL %s busy\n",
			       ill->ill_name));
			mi_sprintf(ill->ill_name, "%s%d", cp, 
				   ill->ill_ppa);
			ill->ill_name_length = mi_strlen(ill->ill_name) + 1;
			err = EBUSY;
			goto done;
		}
		/* Defer setting the new ppa until all mblks are allocated */
		ill->ill_name_length = name_length;
		/* Update ill_resolver_mp if it is an AR_ENTRY_QUERY msg */
		/* TODO till_resolver_mp should be allocated in ipif_up */
		if (ill->ill_subnet_type == IRE_RESOLVER) {
			mblk_t	*areq_mp;
			areq_t	*areq;
			int	abs_sap_length;		/* absolute value */
	
			abs_sap_length = (ill->ill_sap_length < 0) ? 
				-ill->ill_sap_length : ill->ill_sap_length;
			areq_mp = ill_arp_alloc(ill, 
					(u_char *)&ip_areq_template, 0);
			if ( !areq_mp ) {
				ip1dbg(("ip_sioctl_copyin_done: IF_UNITSEL %s no memory\n",
					ill->ill_name));
				mi_sprintf(ill->ill_name, "%s%d", cp, 
					   ill->ill_ppa);
				ill->ill_name_length = mi_strlen(ill->ill_name) + 1;
				err = ENOMEM;
				goto done;
			}
			freemsg(ill->ill_resolver_mp);
			ill->ill_resolver_mp = areq_mp;
			areq = (areq_t *)ALIGN32(ill->ill_resolver_mp->b_rptr);
			bcopy((char *)&ill->ill_sap + sizeof(ill->ill_sap) - 
			      abs_sap_length, 
			      (char *)areq->areq_sap, abs_sap_length);
		}
		ip1dbg(("ip_sioctl_copyin_done: IF_UNITSEL: %d\n", ppa));
		ill->ill_ppa = ppa;
		goto done;
	}
		
/*
 * ARP IOCTLs.
 * How does IP get in the business of fronting ARP configuration/queries?
 * Well its like this, the Berkeley ARP IOCTLs (SIOCGARP, SIOCDARP, SIOCSARP)
 * are by tradition passed in through a datagram socket.  That lands in IP.
 * As it happens, this is just as well since the interface is quite crude in
 * that it passes in no information about protocol or hardware types, or
 * interface association.  After making the protocol assumption, IP is in
 * the position to look up the name of the ILL, which ARP will need, and
 * format a request that can be handled by ARP.  The request is passed up
 * stream to ARP, and the original IOCTL is completed by IP when ARP passes
 * back a response.  ARP supports its own set of more general IOCTLs, in
 * case anyone is interested.
 */
	case SIOCGARP:
	case SIOCSARP:
	case SIOCDARP:
		/*
		 * Convert the SIOC{G|S|D}ARP calls into our AR_ENTRY_xxx
		 * calls.
		 */
		ar = (struct arpreq *)ALIGN32(addr);
		ipaddr = (ipa_t *)&ar->arp_pa;
		ire = ire_lookup_interface(*(u32 *)ALIGN32(ipaddr->ip_addr),
					   IRE_RESOLVER);
		if (!ire  ||  !(ill = ire_to_ill(ire))) {
			if ( iocp->ioc_cmd == SIOCDARP) {
				bcopy((char *)ipaddr->ip_addr, (char *)&u1, 
				      sizeof(u1));
				ire = ire_lookup_exact(u1, IRE_ROUTE, 0);
				if (ire) { 
					ire_delete(ire);
					goto done;
				}
			}
			err = ENXIO;
			goto done;
		}
	/*
	 * We are going to pass up to ARP a packet chain that looks
	 * like:
	 *
	 * M_IOCTL-->ARP_op_MBLK-->ORIG_M_IOCTL-->MI_COPY_MBLK-->ARPREQ_MBLK
	 */
		
		/* Get a copy of the original IOCTL mblk to head the chain. */
		mp1 = copyb(mp);
		if (!mp1) {
			err = ENOMEM;
			goto done;
		}

		bcopy((char *)ipaddr->ip_addr, (char *)&u1, sizeof(u1));
		mp2 = ill_arp_alloc(ill, (u_char *)&ip_area_template, u1);
		if (!mp2) {
			freeb(mp1);
			err = ENOMEM;
			goto done;
		}
		/* Put together the chain. */
		mp1->b_cont = mp2;
		mp1->b_datap->db_type = M_IOCTL;
		mp2->b_cont = mp;
		mp2->b_datap->db_type = M_DATA;
		
		/* Set the proper command in the ARP message. */
		area = (area_t *)ALIGN32(mp2->b_rptr);
		iocp = (struct iocblk *)ALIGN32(mp1->b_rptr);
		switch (iocp->ioc_cmd) {
		case SIOCDARP:
			/* We defer deleting the corresponding IRE until
			 * we return from arp.
			 */
			area->area_cmd = AR_ENTRY_DELETE;
			area->area_proto_mask_offset = 0;
			break;
		case SIOCGARP:
			area->area_cmd = AR_ENTRY_SQUERY;
			area->area_proto_mask_offset = 0;
			break;
		case SIOCSARP: {
			ire_t	*ire1;
			/* Delete the corresponding ire to make sure IP will
			 * pick up any change from arp.
			 */
			ire1 = ire_lookup_exact(u1, IRE_ROUTE, 0);
			if (ire1) 
				ire_delete(ire1);
			break;
		}
		}
		
		iocp->ioc_cmd = area->area_cmd;

		/*
		 * Remember the originating queue pointer so we know where
		 * to complete the request when the resolver reply comes back.
		 */
		((ip_sock_ar_t *)area)->ip_sock_ar_q = q;

		/* Fill in the rest of the ARP operation fields. */
		
		/*
		 * Theoretically, the sa_family could tell us what link layer
		 * type this operation is trying to deal with.  By common
		 * usage AF_UNSPEC means ethernet.  We'll assume any attempt
		 * to use the SIOC?ARP ioctls is for ethernet, for now.  Our
		 * new ARP ioctls can be used more generally.
		 */
		switch ( ar->arp_ha.sa_family ) {
		case AF_UNSPEC:
		default:
			area->area_hw_addr_length = 6;
			break;
		}
		bcopy(ar->arp_ha.sa_data,
			(char *)area + area->area_hw_addr_offset,
			area->area_hw_addr_length);
	
		/* Translate the flags. */
		if ( ar->arp_flags & ATF_PERM )
			area->area_flags |= ACE_F_PERMANENT;
		if ( ar->arp_flags & ATF_PUBL )
			area->area_flags |= ACE_F_PUBLISH;
	
		/*
		 * Up to ARP it goes.  The response will come back in
		 * ip_wput as an M_IOCACK message, and will be handed to
		 * ip_sioctl_iocack for completion.
		 */
		putnext(ire->ire_stq, mp1);
		return;
	}

	/*
	 * We should only get here when we are simply copying out the single
	 * control structure that we copied in.  Where nothing is copied out,
	 * we should have branched to "done" below.  Where we are doing a
	 * multi-part copyout, it is handled in line.
	 */
	mi_copyout(q, mp);
	return;

done:;
	mi_copy_done(q, mp, err);
	return;
}

/*
 * ip_sioctl_copyin_setup is called by ip_wput with any M_IOCTL message
 * that arrives.  Most of the IOCTLs are "socket" IOCTLs which we handle
 * in either I_STR or TRANSPARENT form, using the mi_copy facility.
 * We establish here the size of the block to be copied in.  mi_copyin
 * arranges for this to happen, an processing continues in ip_wput with
 * an M_IOCDATA message.
 */
void
ip_sioctl_copyin_setup (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	int	copyin_size;
	struct iocblk * iocp = (struct iocblk *)ALIGN32(mp->b_rptr);

	if ( !mp->b_cont ) {
		iocp->ioc_error = EINVAL;
		mp->b_datap->db_type = M_IOCNAK;
		iocp->ioc_count = 0;
		qreply(q, mp);
		return;
	}
	if ( iocp->ioc_cr  &&  drv_priv(iocp->ioc_cr) != 0 ) {
		/* Only privileged users can do the following operations. */
		switch ( iocp->ioc_cmd ) {
		case SIOCADDRT:
		case SIOCDELRT:
		case SIOCSIFADDR:
		case SIOCSIFDSTADDR:
		case SIOCSIFFLAGS:
		case SIOCSIFMTU:
		case SIOCSIFBRDADDR:
		case SIOCSIFNETMASK:
		case SIOCSIFMETRIC:
		case SIOCSARP:
		case SIOCDARP:
		case SIOCSIFMUXID:
		case SIOCGIFMUXID:
		case I_LINK:
		case I_UNLINK:
		case I_PLINK:
		case I_PUNLINK:
		case ND_SET:
		case IP_IOCTL:
		case IF_UNITSEL:
			mp->b_datap->db_type = M_IOCACK;
			iocp->ioc_error = EPERM;
			iocp->ioc_count = 0;
			qreply(q, mp);
			return;
		}
	}
	switch ( iocp->ioc_cmd ) {
	case SIOCADDRT:
	case SIOCDELRT:
		copyin_size = sizeof(struct rtentry);
		break;
	case SIOCSIFADDR:
	case SIOCGIFADDR:
	case SIOCSIFDSTADDR:
	case SIOCGIFDSTADDR:
	case SIOCSIFFLAGS:
	case SIOCGIFFLAGS:
	case SIOCSIFMTU:
	case SIOCGIFMTU:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
	case SIOCGIFMETRIC:
	case SIOCSIFMETRIC:
	case SIOCSIFMUXID:
	case SIOCGIFMUXID:
		copyin_size = sizeof(struct ifreq);
		break;
	case SIOCGIFCONF:
		/*
		 * This IOCTL is hilarious.  See comments in
		 * ip_sioctl_copyin_done for the story.
		 */
		if ( iocp->ioc_count == TRANSPARENT )
			copyin_size = sizeof(struct ifconf);
		else
			copyin_size = iocp->ioc_count;
		break;
#ifdef SIOCGIFNUM
	case SIOCGIFNUM:
		copyin_size = sizeof(int);
		break;
#endif
	case IF_UNITSEL:
		copyin_size = sizeof(int);
		break;
	case SIOCSARP:
	case SIOCGARP:
	case SIOCDARP:
		copyin_size = sizeof(struct arpreq);
		break;
	case I_LINK:
	case I_UNLINK:
	case I_PLINK:
	case I_PUNLINK:
		if (q->q_next) {
			/* Not for us! */
			putnext(q, mp);
			return;
		}
		/*
		 * We handle linked streams as a convenience only.  Simply
		 * complete the operation successfully, and forget about it.
		 * (Configuration utilities may want to permanently link
		 * plumbing streams under IP for lack of any better place
		 * to put them.)
		 */
		iocp->ioc_count = 0;
done:;
		iocp->ioc_error = 0;
		mp->b_datap->db_type = M_IOCACK;
		qreply(q, mp);
		return;
	case ND_GET:
		if ( nd_getset(q, ip_g_nd, mp) )
			goto done;
		/* Didn't like it.  Maybe someone downstream wants it. */
		/* FALLTHRU */
	default:
		/*
		 * The only other IOCTLs that require writer status are
		 * ND_SET and IP_IOCTL.  If it isn't one of those, don't
		 * bother becoming writer.
		 */
		if ( iocp->ioc_cmd == ND_SET  ||  iocp->ioc_cmd == IP_IOCTL ) {
			become_writer(q, mp, (pfi_t)ip_wput_ioctl);
			return;
		}
		if ( q->q_next ) {
			putnext(q, mp);
			return;
		}
		mp->b_datap->db_type = M_IOCNAK;
		iocp->ioc_error = ENOENT;
		iocp->ioc_count = 0;
		qreply(q, mp);
		return;
	}
	mi_copyin(q, mp, nilp(char), copyin_size);
}

/*
 * Return 1 if ip_sioctl_copyin_done has to be called as a writer
 * for this ioctl.
 */
int
ip_sioctl_copyin_writer (mp)
	mblk_t	* mp;
{
	struct iocblk	* iocp = (struct iocblk *)ALIGN32(mp->b_rptr);

	switch ( iocp->ioc_cmd ) {
	case SIOCGIFADDR:
	case SIOCGIFDSTADDR:
	case SIOCGIFFLAGS:
	case SIOCGIFMTU:
	case SIOCGIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCGIFMETRIC:
	case SIOCGIFNUM:
	case SIOCGIFCONF:
	case SIOCGARP:
		return (0);
	default:
		return (1);
	}
}

/* ip_wput hands off ARP IOCTL responses to us */
void
ip_sioctl_iocack (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
	struct arpreq * ar;
	area_t	* area;
	mblk_t	* area_mp;
	struct iocblk * iocp;
	mblk_t	* orig_ioc_mp;
	queue_t	* q1;
	
	/*
	 * We should get back from ARP a packet chain that looks like:
	 * M_IOCACK-->ARP_op_MBLK-->ORIG_M_IOCTL-->MI_COPY_MBLK-->ARPREQ_MBLK
	 */
	if (!(area_mp = mp->b_cont)
	||  (area_mp->b_wptr - area_mp->b_rptr) < sizeof(ip_sock_ar_t)
	||  !(orig_ioc_mp = area_mp->b_cont)
	||  !orig_ioc_mp->b_cont
	||  !orig_ioc_mp->b_cont->b_cont) {
		freemsg(mp);
		return;
	}
	
	/*
	 * Pick out the originating queue. This should be replaced by a safer
	 * key with lookup.
	 */
	area = (area_t *)ALIGN32(area_mp->b_rptr);
	q1 = ((ip_sock_ar_t *)area)->ip_sock_ar_q;
	if (q1 != q) {
		put(q1, mp);
		return;
	}
	
	/* Uncouple the internally generated IOCTL from the original one */
	area_mp->b_cont = nilp(mblk_t);
	
	/* We're done if there was an error or if this is not an SIOCGARP
	 * Catch the case where there is an IRE_ROUTE by no entry in the
	 * arp table.
	 */
	iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
	if (iocp->ioc_error  &&  iocp->ioc_cmd == AR_ENTRY_SQUERY) {
		ire_t			* ire;
		dl_unitdata_req_t	* dlup;
		u32			addr;
		mblk_t			* llmp;
		int			addr_len;
		ill_t			* ill;
		ipa_t			* ipaddr;

		ar = (struct arpreq *)ALIGN32(orig_ioc_mp->b_cont->b_cont->b_rptr);
		ipaddr = (ipa_t *)&ar->arp_pa;
		bcopy((char *)ipaddr->ip_addr, (char *)&addr, sizeof(addr));
		ire = ire_lookup_exact(addr, IRE_ROUTE, 0);
		if (ire) {
			ar->arp_flags = ATF_INUSE;
			if (ire->ire_ll_hdr_length)
				llmp = ire->ire_ll_hdr_saved_mp;
			else
				llmp = ire->ire_ll_hdr_mp;
			if (llmp != 0 &&
			    (ill = ire_to_ill(ire)) != 0) {
				u_char * addr;

				ar->arp_flags |= ATF_COM;
				addr_len = ill->ill_phys_addr_length;
				dlup = (dl_unitdata_req_t *)ALIGN32(llmp->b_rptr);
				if (ill->ill_sap_length < 0)  
					addr = llmp->b_rptr +
						dlup->dl_dest_addr_offset;
				else
					addr = llmp->b_rptr +
						dlup->dl_dest_addr_offset +
							ill->ill_sap_length;
				bcopy((char *)addr, ar->arp_ha.sa_data, 
				      addr_len);
			}

			/* Ditch the internal IOCTL. */
			freemsg(mp);
			/* Complete the original. */
			mi_copyout(q, orig_ioc_mp);
			return;
		}
	}
	/* 
	 * Delete the coresponding IRE_ROUTE if any. 
	 * Reset the error if there was one (in case there was no entry
	 * in arp.)
	 */
	if (iocp->ioc_cmd == AR_ENTRY_DELETE) {
		ire_t			* ire;
		u32			addr;
		ipa_t			* ipaddr;

		ar = (struct arpreq *)ALIGN32(orig_ioc_mp->b_cont->b_cont->b_rptr);
		ipaddr = (ipa_t *)&ar->arp_pa;
		bcopy((char *)ipaddr->ip_addr, (char *)&addr, sizeof(addr));
		ire = ire_lookup_exact(addr, IRE_ROUTE, 0);
		if (ire) {
			ire_delete(ire);
			iocp->ioc_error = 0;
		}
	}
	if (iocp->ioc_error  ||  iocp->ioc_cmd != AR_ENTRY_SQUERY) {
		int error = iocp->ioc_error;

		freemsg(mp);
		mi_copy_done(q, orig_ioc_mp, error);
		return;
	}

	/*
	 * Completion of an SIOCGARP.  Translate the information from the
	 * area_t into the struct arpreq.
	 */
	ar = (struct arpreq *)ALIGN32(orig_ioc_mp->b_cont->b_cont->b_rptr);
	ar->arp_flags = ATF_INUSE;
	if ( area->area_flags & ACE_F_PERMANENT )
		ar->arp_flags |= ATF_PERM;
	if ( area->area_flags & ACE_F_PUBLISH )
		ar->arp_flags |= ATF_PUBL;
	if ( area->area_hw_addr_length ) {
		ar->arp_flags |= ATF_COM;
		bcopy((char *)area + area->area_hw_addr_offset,
			ar->arp_ha.sa_data, area->area_hw_addr_length);
	}

	/* Ditch the internal IOCTL. */
	freemsg(mp);
	/* Complete the original. */
	mi_copyout(q, orig_ioc_mp);
}

/*
 * This routine is called by ip_sioctl_copyin_done to handle the
 * SIOCSIFNETMASK IOCTL.  It isn't called anywhere else.  It is off
 * by itself only because it is a bit ugly.
 */
staticf int
ip_sioctl_newmask (ipif, mask)
	ipif_t	* ipif;
	u32	mask;
{
	ire_t	* ire;
	ire_t	* new_ire;
	queue_t	* stq;
	u32	subnet_addr;

	/*
	 * No big deal if the interface isn't already up, or the mask
	 * isn't really changing.
	 */
	if ( !(ipif->ipif_flags & IFF_UP)
	||  mask == ipif->ipif_net_mask
	||  (ipif->ipif_flags & IFF_POINTOPOINT) ) {
		ipif->ipif_net_mask = mask;
		return 0;
	}

	/*
	 * We have to delete the old IRE_RESOLVER or IRE_SUBNET and
	 * replace it with a new one based on the new net mask.
	 */
	new_ire = nilp(ire_t);
	stq = (ipif->ipif_subnet_type == IRE_RESOLVER)
	? ipif->ipif_rq : ipif->ipif_wq;
	if (stq && ipif->ipif_local_addr) {
		subnet_addr = ipif->ipif_local_addr & mask;
		if (subnet_addr) {
			new_ire = ire_create(
				(u_char *)&subnet_addr,
				(u_char *)&mask,
				(u_char *)&ipif->ipif_local_addr,
				nilp(u_char),
				ipif->ipif_mtu,
				ipif->ipif_resolver_mp,
				nilp(queue_t),
				stq,
				ipif->ipif_subnet_type,
				512,
				0
			);
			if (!new_ire)
				return EAGAIN;
		}
		subnet_addr = ipif->ipif_local_addr & ipif->ipif_net_mask;
		if ( ((ire = ire_lookup_interface(subnet_addr, IRE_SUBNET)) != 0
		     &&  ire->ire_stq == ipif->ipif_wq)
		||   ((ire = ire_lookup_interface(subnet_addr, IRE_RESOLVER)) != 0
		     &&  ire->ire_stq == ipif->ipif_rq) ) {
			ire_delete_routes(ire);
			ire_delete(ire);
		}
		if (new_ire) {
			new_ire = ire_add(new_ire);
			if (new_ire)
				ire_delete_routes(new_ire);
		}
	}
	ipif->ipif_net_mask = mask;

	/* Broadcast an address mask reply. */
	ipif_mask_reply(ipif);

	return 0;
}

/* Return best guess as to the subnet mask for the specified address. */
staticf u32
ip_subnet_mask (addr)
	u32	addr;
{
	u32	net_mask, subnet_mask = 0;
	ill_t	*ill;
	ipif_t	*ipif;

	net_mask = ip_net_mask(addr);
	if (net_mask == 0)
		return 0;
	/* Let's check to see if this is maybe a local subnet route. */
	for (ill = ill_g_head; ill; ill = ill->ill_next) 
		for (ipif = ill->ill_ipif; ipif; ipif = ipif->ipif_next) {
			if ((ipif->ipif_flags & IFF_UP) == 0)
				continue;
			if ( (ipif->ipif_local_addr & net_mask) == 
			     (addr & net_mask) ) {
				/* 
				 * Don't trust pt-pt interfaces if there are
				 * other interfaces.
				 */
				if (ipif->ipif_flags & IFF_POINTOPOINT) {
					subnet_mask = ipif->ipif_net_mask;
					continue;
				}
				/*
				 * Fine.  Just assume the same net mask as the
				 * directly attached subnet interface is using.
				 */
				return ipif->ipif_net_mask;
			}
		}
	if (subnet_mask != 0)
		return subnet_mask;
	return net_mask;
}

/*
 * ip_sioctl_copyin_setup calls ip_wput_ioctl (as writer) to process any
 * IOCTLs it doesn't recognize.
 */
staticf void
ip_wput_ioctl (q, mp)
	queue_t	* q;
	mblk_t	* mp;
{
reg	IOCP	iocp;
	ipft_t	* ipft;
	ipllc_t	* ipllc;
	mblk_t	* mp1;

	iocp = (IOCP)ALIGN32(mp->b_rptr);
	mp1 = mp->b_cont;
	if (!mp1) {
		iocp->ioc_error = EINVAL;
		mp->b_datap->db_type = M_IOCNAK;
		iocp->ioc_count = 0;
		qreply(q, mp);
		return;
	}
	iocp->ioc_error = 0;
	switch (iocp->ioc_cmd) {
	case IP_IOCTL:
		/*
		 * These IOCTLs provide various control capabilities to
		 * upstream agents such as ULPs and processes.  There
		 * are currently two such IOCTLs implemented.  They
		 * are used by TCP to provide update information for
		 * existing IREs and to forcibly delete an IRE for a
		 * host that is not responding, thereby forcing an
		 * attempt at a new route.
		 */
		iocp->ioc_error = EINVAL;
		if (!pullupmsg(mp1, sizeof(ipllc->ipllc_cmd)))
			break;
		ipllc = (ipllc_t *)ALIGN32(mp1->b_rptr);
		for (ipft = ip_ioctl_ftbl; ipft->ipft_pfi; ipft++) {
			if (ipllc->ipllc_cmd == ipft->ipft_cmd)
				break;
		}
		if (ipft->ipft_pfi
		&& ((mp1->b_wptr - mp1->b_rptr) >= ipft->ipft_min_size
		 ||  pullupmsg(mp1, ipft->ipft_min_size)))
			iocp->ioc_error = (*ipft->ipft_pfi)(q, mp1);
		if (ipft->ipft_flags & IPFT_F_NO_REPLY) {
			freemsg(mp);
			return;
		}
		break;
	case ND_SET:
		if (nd_getset(q, ip_g_nd, mp))
			break;
		/* FALLTHRU */
	default:
		/*
		 * Nothing unexpected should get through.
		 * ip_sioctl_copyin_setup terminates anything else.
		 * We can leave the "default" line in anyway.
		 * Note the "fallthru" above, however.  The code below
		 * is not extraneous.
		 */
		if (q->q_next) {
			putnext(q, mp);
			return;
		}
		iocp->ioc_error = ENOENT;
		mp->b_datap->db_type = M_IOCNAK;
		iocp->ioc_count = 0;
		qreply(q, mp);
		return;
	}
	mp->b_datap->db_type = M_IOCACK;
	if (iocp->ioc_error)
		iocp->ioc_count = 0;
	qreply(q, mp);
}

/*
 * Allocate and initialize a new interface control structure.  (Always
 * called as writer.)
 */
staticf ipif_t *
ipif_allocate (ill, id, ire_type)
	ill_t	* ill;
	long	id;
	u_int	ire_type;
{
static	ipif_t	ipif_zero;
	ipif_t	* ipif;

	/*
	 * Crowbar dynamic allocations at 255 as a
	 * simple guard against runaway configuration.
	 */
	if ((unsigned long)id > 255)
		return nilp(ipif_t);

	ipif = (ipif_t *)ALIGN32(mi_alloc(sizeof(ipif_t), BPRI_MED));
	if ( ipif ) {
		/* Start clean. */
		*ipif = ipif_zero;
		ipif->ipif_id = id;
		ipif->ipif_flags = IFF_RUNNING;
		ipif->ipif_ire_type = ire_type;
		ipif->ipif_ill = ill;
		if ( ill ) {
			ipif_t	*ipif1;

			ipif->ipif_mtu = ill->ill_max_frag;
			/* Insert at tail */
			ipif->ipif_next = nilp(ipif_t);
			if (ill->ill_ipif) {
				for (ipif1 = ill->ill_ipif; ipif1->ipif_next;
				     ipif1 = ipif1->ipif_next)
					;
				ipif1->ipif_next = ipif;
			} else
				ill->ill_ipif = ipif;
			if (ill->ill_bcast_mp != nilp(mblk_t)) {
				/*
				 * Detect lack of multicast capability by
				 * catching DL_ENABMULTI errors.
				 */
				ipif->ipif_flags |=
					IFF_BROADCAST | IFF_MULTICAST;
			} else {
				ipif->ipif_flags |= IFF_NOARP;
				if (ill->ill_phys_addr_length == 0) {
					/* pt-pt supports multicast. */
					ipif->ipif_flags |=
					    IFF_POINTOPOINT | IFF_MULTICAST;
				}
			}
		}
	}
	return ipif;
}

/*
 * If appropriate, send a message up to the resolver delete the entry
 * for the address of this interface which is going out of business.
 * Assumes that each message is only one message block.  (Always called
 * as writer.)
 */
staticf void
ipif_arp_down (ipif)
	ipif_t	* ipif;
{
	mblk_t	* mp;

	while ((mp = ipif->ipif_arp_down_mp) != 0) {
		ipif->ipif_arp_down_mp = mp->b_cont;
		mp->b_cont = nilp(mblk_t);
		putnext(ipif->ipif_ill->ill_rq, mp);
	}
}

/*
 * Get the resolver set up for a new interface address.  (Always called
 * as writer.)
 */
boolean_t
ipif_arp_up (ipif, addr)
	ipif_t	* ipif;
	u32	addr;
{
	mblk_t	* arp_up_mp = nilp(mblk_t);
	mblk_t	* arp_down_mp = nilp(mblk_t);
	mblk_t	* arp_add_mp = nilp(mblk_t);
	mblk_t	* arp_del_mp = nilp(mblk_t);
	mblk_t	* arp_add_mapping_mp = nilp(mblk_t);
	mblk_t	* arp_del_mapping_mp = nilp(mblk_t);
	ill_t	* ill = ipif->ipif_ill;

	if (ill->ill_subnet_type != IRE_RESOLVER  ||  !addr)
		return true;
	

	if ((ipif->ipif_flags & IFF_UNNUMBERED) == 0) {
		/*
		 * Allocate an ARP deletion message so we know we can tell ARP
		 * when the interface goes down.
		 */
		arp_del_mp = ill_arp_alloc(ill, (u_char *)&ip_ared_template, 
					   addr);
		if (!arp_del_mp)
			goto failed;

		/* Now ask ARP to publish our address. */
		arp_add_mp = ill_arp_alloc(ill, (u_char *)&ip_area_template, 
					   addr);
		if (!arp_add_mp) 
			goto failed;

		((area_t *)ALIGN32(arp_add_mp->b_rptr))->area_flags =
			ACE_F_PERMANENT | ACE_F_PUBLISH;

	}
	/* 
	 * If there are multiple logical interfaces for the same stream
	 * (e.g. sle0/1) we only add a multicast mapping for the primary one.
	 * Note: there will be no multicast info in arp if sle0 is down
	 * but sle0/1 is up.
	 * We also only bring arp up/down on the primary interface.
	 */

	if (ipif->ipif_id != 0)
		goto done;

	/*
	 * Allocate an ARP down message (to be saved) and an ARP up
	 * message.
	 */
	arp_down_mp = ill_arp_alloc(ill, (u_char *)&ip_ard_template, 0);
	if (!arp_down_mp)
		goto failed;
	
	arp_up_mp = ill_arp_alloc(ill, (u_char *)&ip_aru_template, 0);
	if (!arp_up_mp)
		goto failed;
	
	if (ipif->ipif_flags & IFF_POINTOPOINT)
		goto done;

	/* Check IFF_MULTI_BCAST and possible length of physical 
	 * address != 6(?) to determine if we use the mapping or the
	 * broadcast address.
	 */
	if (ipif->ipif_flags & IFF_MULTI_BCAST) {
		u32	addr;
		arma_t	* arma;
		u32	extract_mask;
		u32	mask;
		dl_unitdata_req_t * dlur;

		/* Remove 224.0.0.0 mapping */

		/* Check that the address is not to long for the constant
		 * length reserved in the template arma_t.
		 */
		if (ill->ill_phys_addr_length > IP_MAX_ADDR_LENGTH)
			goto failed;

		addr = htonl(INADDR_ALLHOSTS_GROUP);
		/* Make sure this will not match the "exact" entry. */

		arp_del_mapping_mp = ill_arp_alloc(ill,
			(u_char *)&ip_ared_template, addr);
		if (!arp_del_mapping_mp)
			goto failed;
		
		/* Add mapping mblk */
		addr = (u_long)htonl(INADDR_UNSPEC_GROUP);
		mask = (u_long)htonl(IN_CLASSD_NET);
		extract_mask = 0;

		arp_add_mapping_mp = ill_arp_alloc(ill,
			(u_char *)&ip_arma_multi_bcast_template, addr);
		if (!arp_add_mapping_mp) 	
			goto failed;
		arma = (arma_t *)ALIGN32(arp_add_mapping_mp->b_rptr);
		bcopy((char *)&mask,
			((char *)arma) + arma->arma_proto_mask_offset,
			IP_ADDR_LEN);
		bcopy((char *)&extract_mask,
			((char *)arma) + arma->arma_proto_extract_mask_offset,
			IP_ADDR_LEN);
		/* Use the broadcast addess for MULTI_BCAST */
		dlur = (dl_unitdata_req_t *)ALIGN32(ill->ill_bcast_mp->b_rptr);
		arma->arma_hw_addr_length = ill->ill_phys_addr_length;
		if (ill->ill_sap_length < 0)
			bcopy((char *)dlur + dlur->dl_dest_addr_offset,
			      (char *)arma + arma->arma_hw_addr_offset,
			      ill->ill_phys_addr_length);
		else
			bcopy((char *)dlur + dlur->dl_dest_addr_offset 
			      + ill->ill_sap_length,
			      (char *)arma + arma->arma_hw_addr_offset,
			      ill->ill_phys_addr_length);
		ip2dbg(("ipif_arp_up: adding MULTI_BCAST ARP setup for %s\n",
			ill->ill_name));
	} else if (ipif->ipif_flags & IFF_MULTICAST) {
		u32	addr;
		arma_t	* arma;
		u32	extract_mask;
		u32	mask;

		/* Remove mapping mblk */
		addr = (u_long)htonl(INADDR_ALLHOSTS_GROUP);
			/* Make sure this will not match the "exact" entry. */

		arp_del_mapping_mp = ill_arp_alloc(ill,
			(u_char *)&ip_ared_template, addr);
		if (!arp_del_mapping_mp)
			goto failed;
		
		/* Add mapping mblk */
		addr = (u_long)htonl(INADDR_UNSPEC_GROUP);
		mask = (u_long)htonl(IN_CLASSD_NET);			/* 0xf0000000 */
		extract_mask = (u_long)htonl(IP_MULTI_EXTRACT_MASK);	/* 0x007fffff */

		arp_add_mapping_mp = ill_arp_alloc(ill,
			(u_char *)&ip_arma_multi_template, addr);
		if (!arp_add_mapping_mp)
			goto failed;
		arma = (arma_t *)ALIGN32(arp_add_mapping_mp->b_rptr);
		bcopy((char *)&mask,
			((char *)arma) + arma->arma_proto_mask_offset,
			IP_ADDR_LEN);
		bcopy((char *)&extract_mask,
			((char *)arma) + arma->arma_proto_extract_mask_offset,
			IP_ADDR_LEN);
		bcopy((char *)ip_g_phys_multi_addr,
		      (char *)arma + arma->arma_hw_addr_offset,
		      ill->ill_phys_addr_length);
		ip2dbg(("ipif_arp_up: adding multicast ARP setup for %s\n",
			ill->ill_name));
	}
 done:;

	ipif_arp_down(ipif);
	ipif->ipif_arp_down_mp = arp_down_mp;
	if (arp_del_mp) {
		arp_del_mp->b_cont = ipif->ipif_arp_down_mp;
		ipif->ipif_arp_down_mp = arp_del_mp;
	}
	if (arp_del_mapping_mp) {
		arp_del_mapping_mp->b_cont = ipif->ipif_arp_down_mp;
		ipif->ipif_arp_down_mp = arp_del_mapping_mp;
	}

	if (arp_up_mp)
		putnext(ill->ill_rq, arp_up_mp);
	if (arp_add_mp)
		putnext(ill->ill_rq, arp_add_mp);
	if (arp_add_mapping_mp)
		putnext(ill->ill_rq, arp_add_mapping_mp);
	return true;

failed:;
	freemsg(arp_add_mp);
	freemsg(arp_del_mp);
	freemsg(arp_add_mapping_mp);
	freemsg(arp_del_mapping_mp);
	return false;
}

/*
 * Take down a specific interface, but don't lose any information about it.
 * (Always called as writer.)
 */
void
ipif_down (ipif)
	ipif_t	* ipif;
{
	ill_t	* ill = ipif->ipif_ill;

#ifdef notdef
	/* Assume that the driver will drop all memberships
	 * (including allmulti) when we detach. This code has
	 * problems since the DL_DELMULTI_REQs may be delayed
	 * in arp causing them to be delivered to the driver after
	 * the unbind/detach.
	 */
	ipif_multicast_down(ipif);
#endif
	if (ipif->ipif_local_addr)
		ire_walk(ipif_downi, (char *)ipif);
	if (ipif->ipif_flags & IFF_UP) {
		ipif->ipif_flags &= ~IFF_UP;
#if 0
		/* Assume that the driver will drop all memberships
		 * (including allmulti) when we detach. This code has
		 * problems since the DL_DELMULTI_REQs may be delayed
		 * in arp causing them to be delivered to the driver after
		 * the unbind/detach.
		 */
		/* Will we unbind and detach now? */
		if (ill->ill_ipif_up_count == 1)
			ill_delete_multicast(ill);
#endif
		--ill->ill_ipif_up_count;
		if ( !(ipif->ipif_flags & IFF_LOOPBACK) ) {
			ipif_g_count--;
			if (ill->ill_ipif_up_count == 0) {
				/*
				 * The ill is completely out of business.
				 * Get it to unbind, and detach.
				 */
				mblk_t	* mp;
				queue_t	* wq = ill->ill_wq;
				
				if (!ill->ill_unbind_pending) {
					mp = ill->ill_unbind_mp;
					if ( mp ) {
						ip1dbg(("ipif_down: unbind\n"));
						ill->ill_unbind_mp = mp->b_next;
						mp->b_next = nilp(mblk_t);
						putnext(wq, mp);
						ill->ill_unbind_pending = 1;
					}
				}
				if (!ill->ill_unbind_pending) {
					mp = ill->ill_detach_mp;
					if ( mp ) {
						ip1dbg(("ipif_down: detach\n"));
						ill->ill_detach_mp = mp->b_next;
						mp->b_next = nilp(mblk_t);
						putnext(wq, mp);
					}
				}
			}
		}
	}
	/* Have to be after removing the routes in ipif_downi and the
	 * multicast addresses in ill_delete_multicast
	 */
	ipif_arp_down(ipif);
}

/*
 * ire_walk routine to delete every IRE dependent on the the interface
 * address that is going down.  (Always called as writer.)
 */
staticf void
ipif_downi (ire, ipif_arg)
reg	ire_t	* ire;
	char	* ipif_arg;
{
	ipif_t	* ipif = (ipif_t *)ALIGN32(ipif_arg);

	if (ire->ire_stq != ipif->ipif_wq && ire->ire_stq != ipif->ipif_rq &&
	    ire->ire_rfq != ipif->ipif_wq && ire->ire_rfq != ipif->ipif_rq)
		return;

	if (ire->ire_src_addr == ipif->ipif_local_addr) {
	    if (ire->ire_type != IRE_ROUTE)
		ire_delete_routes(ire);
	    ire_delete(ire);
	}
}

/* Deallocate an IPIF.  (Always called as writer.) */
staticf void
ipif_free (ipif)
	ipif_t	* ipif;
{
	ipif_t	** ipifp;

	/* Free state for addition IRE_RESOLVER/SUBNET ire's */
	freemsg(ipif->ipif_saved_ire_mp);
	ipif->ipif_saved_ire_mp = nilp(mblk_t);

	/* Remove all multicast memberships on the interface */
	ilm_free(ipif);

	/* Remove multicast bindings to this interface */
	reset_ipc_multicast_ipif(ipif);

	/* Remove pointers to this ill in the multicast routing tables */
	reset_mrt_vif_ipif(ipif);

	/* Take down the interface. */
	ipif_down(ipif);
	
	/* Remove all groups on this interface */
	reset_ilg_lower(ipif);		

	/* Get it out of the ILL interface list. */
	ipifp = &ipif->ipif_ill->ill_ipif;
	for ( ; *ipifp ; ipifp = &ipifp[0]->ipif_next) {
		if (*ipifp == ipif) {
			*ipifp = ipif->ipif_next;
			break;
		}
	}
	
	/* Free the memory. */
	mi_free((char *)ipif);
}

/*
 * Returns an ipif name in the form "ill_name/unit" if ipif_id is not zero,
 * "ill_name" otherwise.
 */
char *
ipif_get_name (ipif, buf, len)
	ipif_t	* ipif;
	char	* buf;
	int	len;
{
	char	lbuf[32];
	char	* name;
	int	name_len;

	buf[0] = '\0';
	if ( !ipif )
		return buf;
	name = ipif->ipif_ill->ill_name;
	name_len = ipif->ipif_ill->ill_name_length;
	if ( ipif->ipif_id != 0 ) {
		mi_sprintf(lbuf, "%s%c%d", name, IPIF_SEPARATOR_CHAR,
			   ipif->ipif_id);
		name = lbuf;
		name_len = mi_strlen(name) + 1;
	}
	len -= 1;
	buf[len] = '\0';
	len = MIN(len, name_len);
	bcopy(name, buf, len);
	return buf;
}

/*
 * Find an IPIF based on the name passed in.  Names can be of the
 * form <dev><#> (e.g., sle0), or <dev><#>/<#> (e.g., sle0/1).
 * When there is no slash, the implied unit id is zero.  "dev"
 * must match a DLPI device module name, and <dev><#> must
 * correspond to the name of an ILL.  (May be called as writer.)
 */
staticf ipif_t * 
ipif_lookup_on_name (name, namelen)
	char	* name;
	int	namelen;
{
	char	* cp;
	char	* endp;
	long	id;
	ill_t	* ill;
	ipif_t	* ipif;

	/* Look for a slash in the name. */
	endp = &name[namelen - 1];
	for (cp = endp; --cp > name; ) {
		if (*cp == IPIF_SEPARATOR_CHAR)
			break;
	}
	if (cp <= name) {
		cp = endp;
	} else {
		*cp = '\0';
	}
	
	/*
	 * Look up the ILL, based on the portion of the name
	 * before the slash.
	 */
	ill = ill_lookup_on_name(name, (cp + 1) - name);
	if (cp != endp)
		*cp = IPIF_SEPARATOR_CHAR;
	if (!ill)
		return nilp(ipif_t);

	/* Establish the unit number in the name. */
	id = 0L;
	if (cp < endp  &&  *endp == '\0') {
		/* If there was a slash, the unit number follows. */
		cp++;
		id = mi_strtol(cp, &endp, 0);
		if (endp == cp)
			return nilp(ipif_t);
	}
	
	/* Now see if there is an IPIF with this unit number. */
	for (ipif = ill->ill_ipif; ipif; ipif = ipif->ipif_next) {
		if (ipif->ipif_id == id)
			return ipif;
	}
	
	/* If none found, allocate and return a new one. */
	return ipif_allocate(ill, id, IRE_LOCAL);
}

/* Initialize the loopback device.  (Always called as writer.) */
boolean_t
ipif_loopback_init ()
{
static	boolean_t	initialized;
	ipif_t	* ipif;

	if (initialized)
		return true;
	/*
	 * ipif_lookup_on_name will allocate the device if it isn't
	 * there already.
	 */
	ipif = ipif_lookup_on_name(ipif_loopback_name,
		sizeof(ipif_loopback_name));
	if (!ipif)
		return false;
	/* Bring it up. */
	initialized = ipif_up(ipif, nilp(queue_t), nilp(mblk_t)) == 0;
	return initialized;
}

/*
 * This routine is called whenever a new address comes up on an ipif.  If
 * we are configured to respond to address mask requests, then we are supposed
 * to broadcast an address mask reply at this time.  This routine is also
 * called if we are already up, but a netmask change is made.  This is legal
 * but might not make the system manager very popular.  (May be called
 * as writer.)
 */
void
ipif_mask_reply (ipif)
	ipif_t	* ipif;
{
	icmph_t	* icmph;
	ipha_t	* ipha;
	mblk_t	* mp;

#define	REPLY_LEN	(sizeof(icmp_ipha) + sizeof(icmph_t) + IP_ADDR_LEN)
	
	if ( !ip_respond_to_address_mask_broadcast )
		return;
	mp = allocb(REPLY_LEN, BPRI_HI);
	if ( !mp )
		return;
	mp->b_wptr = mp->b_rptr + REPLY_LEN;
	
	ipha = (ipha_t *)ALIGN32(mp->b_rptr);
	bzero((char *)ipha, REPLY_LEN);
	*ipha = icmp_ipha;
	ipha->ipha_ttl = ip_broadcast_ttl;
	ipha->ipha_src = ipif->ipif_local_addr;
	ipha->ipha_dst = ipif->ipif_broadcast_addr;
	ipha->ipha_length = htons(REPLY_LEN);
	
	icmph = (icmph_t *)&ipha[1];
	icmph->icmph_type = ICMP_ADDRESS_MASK_REPLY;
	bcopy((char *)&ipif->ipif_net_mask, (char *)&icmph[1], IP_ADDR_LEN);
	icmph->icmph_checksum = IP_CSUM(mp, sizeof(ipha_t), 0);
	
	put(ipif->ipif_wq, mp);

#undef	REPLY_LEN
}

/*
 * When the mtu in the ipif changes, we call this routine through ire_walk
 * to update all the relevant IREs.
 */
staticf void
ipif_mtu_change (ire, ipif_arg)
	ire_t	* ire;
	char	* ipif_arg;
{
	ipif_t	* ipif = (ipif_t *)ALIGN32(ipif_arg);
	ill_t   * ill = ipif->ipif_ill;

	if (ire->ire_src_addr != ipif->ipif_local_addr ||
	    ire->ire_type == IRE_LOCAL)
        	 return;

        if (ire->ire_stq != ill->ill_wq &&
            ire->ire_stq != ill->ill_rq &&
            ire->ire_rfq != ill->ill_wq &&
            ire->ire_rfq != ill->ill_rq) {
                /* Wrong ill hence wrong ipif */
                return;
        }
	ire->ire_max_frag = ipif->ipif_mtu;
}

/*
 * Must be called after a mapping has been set up in the resolver.  (Always
 * called as writer.)
 */
staticf void
ipif_multicast_up (ipif)
	ipif_t	* ipif;
{
	int err;

	ip1dbg(("ip_multicast_up\n"));
	if ( (ipif->ipif_flags & IFF_MULTICAST)
	&&  !ipif->ipif_multicast_up
	&&  ipif->ipif_local_addr ) {	
		/* Join the all hosts multicast address */
		ip1dbg(("ip_multicast_up - addmulti\n"));
		err = ip_addmulti(htonl(INADDR_ALLHOSTS_GROUP), ipif);
		if (err)
			printf("ipif_multicast_up: failed %d\n", err);
		else
			ipif->ipif_multicast_up = 1;
	}
}

#ifdef notdef
staticf void
ipif_multicast_down (ipif)
	ipif_t	* ipif;
{
	ip1dbg(("ip_multicast_down\n"));
	if ( ipif->ipif_multicast_up ) {
		/* Leave the all hosts multicast address */
		ip1dbg(("ip_multicast_down - delmulti\n"));
		(void)ip_delmulti(htonl(INADDR_ALLHOSTS_GROUP), ipif);
		ipif->ipif_multicast_up = 0;
	}					
}
#endif

/*
 * Used when an interface comes up to recreate any extra IRE_SUBNET/RESOLVER
 * on this interface.
 */
staticf void
ipif_recover_ire(ipif)
	ipif_t	* ipif;
{
	mblk_t	* mp;

	for (mp = ipif->ipif_saved_ire_mp; mp; mp = mp->b_cont) {
		ire_t	* ire;
		queue_t	* stq;
		u32	net_mask, ipaddr;

		bcopy((char *)mp->b_rptr, (char *)&ipaddr, IP_ADDR_LEN);
		bcopy((char *)mp->b_rptr + IP_ADDR_LEN, (char *)&net_mask, 
		      IP_ADDR_LEN);

		stq = (ipif->ipif_subnet_type == IRE_RESOLVER)
			? ipif->ipif_rq : ipif->ipif_wq;
		/*
		 * Create a copy of the IRE_SUBNET/RESOLVER with modified 
		 * address and netmask.
		 */
		ire = ire_create(
			(u8 *)&ipaddr,
			(u8 *)&net_mask, 
			(u8 *)&ipif->ipif_local_addr,
			nilp(u8),
			ipif->ipif_mtu,
			ipif->ipif_resolver_mp,
			nilp(queue_t),
			stq,
			ipif->ipif_subnet_type,
			512,
			0
		);

		if (!ire)
			return;
		ire = ire_add(ire);
		if (ire)
			ire_delete_routes(ire);
	}
}

/*
 * Used to set the netmask and broadcast address to default values when the
 * interface is brought up.  (Always called as writer.)
 */
staticf void
ipif_set_default (ipif)
	ipif_t	* ipif;
{
	if (!ipif->ipif_net_mask)
		ipif->ipif_net_mask = ip_net_mask(ipif->ipif_local_addr);
	/*
	 * NOTE: SunOS 4.X does this even if the broadcast address has been
	 * already set thus we do the same here.
	 */
	if (ipif->ipif_flags & IFF_BROADCAST)
		ipif->ipif_broadcast_addr =
			(ipif->ipif_local_addr & ipif->ipif_net_mask)
			| ~ipif->ipif_net_mask;
}

/* 
 * Return true if this address can
 * be used as local address.
 */
staticf boolean_t
ip_addr_availability_check (new_ipif)
	ipif_t	* new_ipif;
{
	u32	our_local_addr = new_ipif->ipif_local_addr;
	ill_t *ill;
	ipif_t *ipif;

	new_ipif->ipif_flags &= ~IFF_UNNUMBERED;
	for (ill = ill_g_head; ill; ill = ill->ill_next) 
	    for (ipif = ill->ill_ipif; ipif; ipif = ipif->ipif_next) {
		if ((ipif == new_ipif) 
		    || ((ipif->ipif_flags & IFF_UP) == 0)
		    || (ipif->ipif_flags & IFF_UNNUMBERED))
		    continue;
		if (ipif->ipif_local_addr == our_local_addr) {
			if (new_ipif->ipif_flags & IFF_POINTOPOINT)
                            new_ipif->ipif_flags |= IFF_UNNUMBERED;
			else if (ipif->ipif_flags & IFF_POINTOPOINT)
                            ipif->ipif_flags |= IFF_UNNUMBERED;
			else
			    return (false);
                }
            }    
        return (true);

}

/* 
 * Return the IRE_RESOLVER or IRE_SUBNET ire associated with the specified
 * ipif.  (May be called as writer.)
 */
ire_t *
ipif_to_ire (ipif)
	ipif_t	* ipif;
{
	u32	our_local_addr = ipif->ipif_local_addr;
	ill_t   * ill = ipif->ipif_ill;
	ire_t	* ire;

        if(ipif->ipif_ire_type == IRE_LOOPBACK) {
 		ire_t   ** irep;
 
 		for (irep = ire_hash_tbl; irep < 
 		&ire_hash_tbl[IRE_HASH_TBL_COUNT]; irep++) {
 			for (ire = *irep; ire; ire = ire->ire_next) {
 				if (ire->ire_src_addr == our_local_addr)
 					return(ire);
 			}
 		}
 	}

	for ( ire = ire_subnet_head; ire; ire = ire->ire_next ) {
		if (ire->ire_src_addr != our_local_addr)
			continue;
		if (ire->ire_stq != ill->ill_wq &&
		ire->ire_stq != ill->ill_rq &&
		ire->ire_rfq != ill->ill_wq &&
		ire->ire_rfq != ill->ill_rq) {
			/* Wrong ill hence wrong ipif */
			continue;
		}
		if (ipif->ipif_flags & IFF_POINTOPOINT) {
			/* Verify that we get the correct ire for unnumbered
			 * pt-pt link.
			 */
			if (ire->ire_mask != (u32)~0 ||
			    ipif->ipif_pp_dst_addr != ire->ire_addr)
					continue;
			}
			return ire;
		}
	return nilp(ire_t);
}

/*
 * Add IREs for the specified interface: IRE_LOCAL, IRE_BROADCASTs, IRE_SUBNET
 * (or IRE_RESOLVER).  (Always called as writer.)
 */
staticf int
ipif_up (ipif, q, mp)
	ipif_t	* ipif;
	queue_t	* q;
	mblk_t	* mp;
{
	mblk_t	* bind = nilp(mblk_t);
	mblk_t	* unbind = nilp(mblk_t);
	mblk_t	* attach = nilp(mblk_t);
	mblk_t	* detach = nilp(mblk_t);
	ire_t	* ire_array[20];
	ire_t	** irep = &ire_array[0];
	ire_t	** irep1;
	u32	net_mask = 0;
	u32	subnet_mask, route_mask;
	queue_t	* stq;
	u32	subnet_addr;
	ill_t	* ill = ipif->ipif_ill;

	/* Shouldn't get here if it is already up. */
	if (ipif->ipif_flags & IFF_UP)
		return EALREADY;
	
	/*
	 * Figure out which way the send-to queue should go.  Only
	 * IRE_RESOLVER or IRE_SUBNET should show up here.
	 */
	switch (ill->ill_subnet_type) {
	case IRE_RESOLVER:
		stq = ill->ill_rq;
		break;
	case IRE_SUBNET:
	case IRE_LOOPBACK:
		stq = ill->ill_wq;
		break;
	default:
		return EINVAL;
	}
	
	/* Check if this address can be used on this interface  */
	if (!ip_addr_availability_check(ipif))
	    return EADDRNOTAVAIL;
	/* Create all the IREs associated with this interface */
	if (ipif->ipif_local_addr) {
		/* If the interface address is set, create the local IRE. */
		ip1dbg(("ipif_up: creating IRE %d for 0x%x\n",
			ipif->ipif_ire_type,
			(int)ntohl(ipif->ipif_local_addr)));
		net_mask = ip_net_mask(ipif->ipif_local_addr);
		*irep++ = ire_create(
			(u_char *)&ipif->ipif_local_addr,/* dest address */
			(u_char *)&ip_g_all_ones,	/* mask */
			(u_char *)&ipif->ipif_local_addr,/* source address */
			nilp(u_char),			/* no gateway */
			STRMSGSZ,			/* max frag size */
			nilp(mblk_t),			/* no xmit header */
			ipif->ipif_rq,			/* recv-from queue */
			nilp(queue_t),			/* no send-to queue */
			ipif->ipif_ire_type,		/* LOCAL or LOOPBACK */
			512,				/* rtt */
			0
		);
	} else {
		ip1dbg(("ipif_up: not creating IRE %d for 0x%x: flags 0x%x\n",
			ipif->ipif_ire_type,
			(int)ntohl(ipif->ipif_local_addr),
			ipif->ipif_flags));
	}

	subnet_mask = ipif->ipif_net_mask;
	/* If no net mask set, assume the default based on net class. */
	if (subnet_mask == 0)
		subnet_mask = net_mask;

	/* Set up the IRE_RESOLVER or IRE_SUBNET, as appropriate. */
	if (ipif->ipif_flags & IFF_POINTOPOINT) {
		route_mask = (u32)~0;
		subnet_addr = ipif->ipif_pp_dst_addr;
	} else {
		subnet_addr = ipif->ipif_local_addr & subnet_mask;
		route_mask = subnet_mask;
	}
	if (subnet_addr && ipif->ipif_local_addr) {
		ipif->ipif_net_mask = subnet_mask;
		if (stq) {
			*irep++ = ire_create(
				(u_char *)&subnet_addr,	/* dest address */
				(u_char *)&route_mask,	/* mask */
				(u_char *)&ipif->ipif_local_addr, /* src addr */
				nilp(u_char),		/* no gateway */
				ipif->ipif_mtu,		/* max frag */
				ill->ill_resolver_mp,	/* xmit header */
				nilp(queue_t),		/* no recv from queue */
				stq,			/* send-to queue */
				ill->ill_subnet_type,	/* RESOLVER / SUBNET */
				512,			/* rtt */
				0
			);
		}
	}
	
	/* If the interface address is set, create the broadcast IREs. */
	if (ipif->ipif_local_addr && ipif->ipif_flags & IFF_BROADCAST) {
		u32	addr;
		irep = ire_create_bcast(ipif, 0, irep);
		irep = ire_create_bcast(ipif, ~0, irep);

		addr = net_mask & ipif->ipif_local_addr;
		irep = ire_create_bcast(ipif, addr, irep);
		irep = ire_create_bcast(ipif, ~net_mask | addr, irep);

		addr = subnet_mask & ipif->ipif_local_addr;
		irep = ire_create_bcast(ipif, addr, irep);
		irep = ire_create_bcast(ipif, ~subnet_mask | addr, irep);

	}
	
	/* If an earlier ire_create failed, get out now */
	for (irep1 = irep; irep1-- > ire_array; ) {
		if (!*irep1)
			goto bad;
	}

	/* Crank up ARP on the new address. */
	if (!ipif_arp_up(ipif, ipif->ipif_local_addr))
		goto bad;

	/* Add in all newly created IREs */
	while (irep-- > ire_array) {
		if ( (*irep)->ire_type == IRE_BROADCAST ) {
			/* Avoid duplicates. */
			if ( ire_lookup_broadcast((*irep)->ire_addr, ipif) ) {
				ire_delete(*irep);
				continue;
			}
		}
		*irep = ire_add(*irep);
		if (*irep)
			ire_delete_routes(*irep);
	}
	
	/* Join the allhosts multicast address */
	ipif_multicast_up(ipif);

	/* If this is the loopback interface, we are done. */
	if (ipif->ipif_ire_type == IRE_LOOPBACK) {
		ipif->ipif_flags |= IFF_UP;
		ipif->ipif_ipif_up_count++;
		/* This one doesn't count towards ipif_g_count. */
		return 0;
	}

	/*
	 * If the broadcast address has been set, make sure it makes sense
	 * based on the interface address.
	 */
	if ( ipif->ipif_broadcast_addr && (ipif->ipif_flags & IFF_BROADCAST)) {
		ire_t	* ire;
		ire = ire_lookup_broadcast(ipif->ipif_broadcast_addr, ipif);
		if ( !ire ) {
			/*
			 * If there isn't a matching broadcast IRE, 
			 * revert to the default for this netmask.
			 */
			ipif->ipif_broadcast_addr = 0;
			ipif_set_default(ipif);
		}
	}
	
	/* Recover any addition IRE_SUBNET/RESOLVER entries for this ipif */
	ipif_recover_ire(ipif);

	if (ipif->ipif_ipif_up_count > 0  ||  ipif->ipif_ipif_pending) {
		/* Mark it up, and increment counters. */
		ill->ill_ipif_up_count++;
		ipif->ipif_flags |= IFF_UP;
		ipif_g_count++;

		/* Broadcast an address mask reply. */
		ipif_mask_reply(ipif);
 
		return 0;
	}
	/*
	 * If this is the first interface to come up on this ILL, we need to
	 * attach and bind to the device.
	 */

	bind = ip_dlpi_alloc(sizeof(dl_bind_req_t)+sizeof(long), DL_BIND_REQ);
	if (!bind)
		goto bad;
	((dl_bind_req_t *)ALIGN32(bind->b_rptr))->dl_sap = 
		ill->ill_sap;
	((dl_bind_req_t *)ALIGN32(bind->b_rptr))->dl_service_mode = DL_CLDLS;

	unbind = ip_dlpi_alloc(sizeof(dl_unbind_req_t), DL_UNBIND_REQ);
	if (!unbind)
		goto bad;
	
	/* If we need to attach/detach, pre-alloc and initialize the mblks */
	if (ill->ill_needs_attach) {
		attach = ip_dlpi_alloc(sizeof(dl_attach_req_t), DL_ATTACH_REQ);
		if (!attach)
			goto bad;
		((dl_attach_req_t *)ALIGN32(attach->b_rptr))->dl_ppa = 
			ill->ill_ppa;

		detach = ip_dlpi_alloc(sizeof(dl_detach_req_t), DL_DETACH_REQ);
		if (!detach)
			goto bad;
	}
	/*
	 * Record state needed to complete this operation when the
	 * DL_BIND_ACK shows up.  Also remember the pre-allocated mblks.
	 */
	ill->ill_ipif_pending = ipif;
	ill->ill_bind_pending = mp;
	ill->ill_bind_pending_q = q;
	/*
	 * Since all detach and unbind messages are indentical we can 
	 * safely queue them in reverse order.
	 */
	if (detach) {
		detach->b_next = ill->ill_detach_mp;
		ill->ill_detach_mp = detach;
	}
	unbind->b_next = ill->ill_unbind_mp;
	ill->ill_unbind_mp = unbind;

	if (ill->ill_unbind_pending) {
		ip1dbg(("ipif_up: unbind_pending\n"));
		ill->ill_attach_mp = attach;
		ill->ill_bind_mp = bind;
	} else {
		if ( attach ) {
			ip1dbg(("ipif_up: attach\n"));
			putnext(ill->ill_wq, attach);
			/*
			 * The attach is on its way.  We don't wait for
			 * it to come back.  If it fails, the bind will too,
			 * and we will note that in ip_rput_dlpi.
			 */
		}
		ip1dbg(("ipif_up: bind\n"));
		putnext(ill->ill_wq, bind);
	}
	/*
	 * This operation will complete in ip_rput_dlpi with either
	 * a DL_BIND_ACK or DL_ERROR_ACK.
	 */
	return EINPROGRESS;
bad:
	while (irep-- > ire_array) {
		if (*irep)
			ire_delete(*irep);
	}
	if (bind)
		freemsg(bind);
	if (unbind)
		freemsg(unbind);
	if (attach)
		freemsg(attach);
	if (detach)
		freemsg(detach);
	return ENOMEM;
}
