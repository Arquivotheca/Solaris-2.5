/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_INET_IP_MULTI_H
#define	_INET_IP_MULTI_H

#pragma ident	"@(#)ip_multi.h	1.9	95/02/21 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef __STDC__
extern	int	ip_addmulti(u32  group, ipif_t * ipif);

extern	int	ip_delmulti(u32  group, ipif_t *ipif);

extern	void	ip_multicast_loopback(queue_t * rq, mblk_t * mp_orig);

extern	void	ip_wput_ctl(queue_t * q, mblk_t * mp_orig);

extern	void	ill_add_multicast(ill_t * ill);

extern	void	ill_delete_multicast(ill_t * ill);

extern	ilm_t	*ilm_lookup(ill_t *ill, u32 group);

extern	ilm_t	*ilm_lookup_exact(ipif_t *ipif, u32 group);

extern	void	ilm_free(ipif_t *ipif);

extern	ipif_t	*ipif_lookup_group(u32 group);

extern	ipif_t	*ipif_lookup_addr(u32 addr);

extern	ipif_t	*ipif_lookup_interface(u32 if_addr, u32 dst);

extern	ipif_t	*ipif_lookup_remote(ill_t * ill, u32 addr);

extern	int 	ip_opt_add_group(ipc_t *ipc, u32 group, u32 ifaddr);

extern	int	ip_opt_delete_group(ipc_t *ipc, u32 group, u32 ifaddr);

extern	boolean_t	ilg_member(ipc_t *ipc, u32 group);

extern	void	ilg_delete_all(ipc_t *ipc);

extern	void	reset_ilg_lower(ipif_t *ipif);

extern	void	reset_ipc_multicast_ipif(ipif_t *ipif);

#else	/* __STDC__ */

extern	int	ip_addmulti();

extern	int	ip_delmulti();

extern	void	ip_multicast_loopback();

extern	void	ip_wput_ctl();

extern	void	ill_add_multicast();

extern	void	ill_delete_multicast();

extern	ilm_t	*ilm_lookup();

extern	ilm_t 	*ilm_lookup_exact();

extern	void	ilm_free();

extern	ipif_t	*ipif_lookup_group();

extern	ipif_t	*ipif_lookup_addr();

extern	ipif_t	*ipif_lookup_interface();

extern	ipif_t	*ipif_lookup_remote();

extern	int 	ip_opt_add_group();

extern	int	ip_opt_delete_group();

extern	boolean_t	ilg_member();

extern	void	ilg_delete_all();

extern	void	reset_ilg_lower();

extern	void	reset_ipc_multicast_ipif();
#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_IP_MULTI_H */
