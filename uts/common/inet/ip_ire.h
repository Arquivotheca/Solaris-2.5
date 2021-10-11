/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_INET_IP_IRE_H
#define	_INET_IP_IRE_H

#pragma ident	"@(#)ip_ire.h	1.10	95/02/21 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef __STDC__
extern	int	ip_ire_advise(queue_t * q, mblk_t * mp);

extern	int	ip_ire_delete(queue_t * q, mblk_t * mp);

extern	int	ip_ire_report(queue_t * q, mblk_t * mp, caddr_t arg);

extern	void	ip_ire_report_ire(ire_t * ire, char * mp);

extern	void	ip_ire_req(queue_t * q, mblk_t * mp);

extern	ire_t	*ire_add(ire_t * ire);

extern	void	ire_add_then_put(queue_t * q, mblk_t * mp);

extern	ire_t	*ire_create(u_char * addr, u_char * mask, u_char * src_addr,
				u_char * gateway, u_int max_frag,
				mblk_t * ll_hdr_mp, queue_t * rfq,
				queue_t * stq, u_int type, u_long rtt,
				u_int ll_hdr_len);

extern	ire_t	**ire_create_bcast(ipif_t * ipif, u32 addr, ire_t ** irep);

extern	void	ire_delete(ire_t * ire);

extern	void	ire_delete_routes(ire_t * ire);

extern	void	ire_expire(ire_t * ire, char * arg);

extern	ire_t *	ire_lookup(u32 addr);

extern	ire_t *	ire_lookup_broadcast(u32 addr, ipif_t * ipif);

extern	ire_t *	ire_lookup_exact(u32 addr, u_int type, u32 gw_addr);

extern	ire_t *	ire_lookup_local(void);

extern	ire_t *	ire_lookup_loop(u32 dst, u32 * gw);

extern	ire_t *	ire_lookup_myaddr(u32 addr);

extern	ire_t *	ire_lookup_noroute(u32 addr);

extern	ire_t *	ire_lookup_interface(u32 addr, u_int ire_type_mask);

extern	ire_t *	ire_lookup_ipif(ipif_t * ipif, u_int type, u32 addr,
				u32 src_addr);

extern	ire_t *	ire_lookup_interface_exact(u32 addr, u_int ire_type,
						u32 src_addr);

extern	void	ire_pkt_count(ire_t * ire, char * ippc_arg);

extern	ipif_t	*ire_interface_to_ipif(ire_t * ire);

extern	ill_t *	ire_to_ill(ire_t * ire);

extern	ipif_t	*ire_to_ipif(ire_t * ire);

extern	void	ire_walk(pfv_t func, char * arg);

extern	void	ire_walk_wq(queue_t *wq, pfv_t func, char * arg);

#else /* __STDC__ */

extern	int	ip_ire_advise();

extern	int	ip_ire_delete();

extern	int	ip_ire_report();

extern	void	ip_ire_report_ire();

extern	void	ip_ire_req();

extern	ire_t	* ire_add();

extern	void	ire_add_then_put();

extern	ire_t *	ire_create();

extern	ire_t **ire_create_bcast();

extern	void	ire_delete();

extern	void	ire_delete_routes();

extern	void	ire_expire();

extern	ire_t *	ire_lookup();

extern	ire_t *	ire_lookup_broadcast();

extern	ire_t *	ire_lookup_exact();

extern	ire_t *	ire_lookup_local();

extern	ire_t *	ire_lookup_loop();

extern	ire_t *	ire_lookup_myaddr();

extern	ire_t *	ire_lookup_noroute();

extern	ire_t *	ire_lookup_interface();

extern	ire_t *	ire_lookup_interface_exact();

extern	void	ire_pkt_count();

extern	ipif_t	*ire_interface_to_ipif();

extern	ill_t *	ire_to_ill();

extern	ipif_t	*ire_to_ipif();

extern	void	ire_walk();

extern	void	ire_walk_wq();
#endif /* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_IP_IRE_H */
