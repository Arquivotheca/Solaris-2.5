/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_INET_IP_IF_H
#define	_INET_IP_IF_H

#pragma ident	"@(#)ip_if.h	1.9	93/11/01 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef __STDC__
extern	mblk_t	*ill_arp_alloc(ill_t * ill, u_char * template, u32 addr);

extern	void	ill_delete(ill_t * ill);

extern	mblk_t	*ill_dlur_gen(u_char * addr, u_int addr_length, u_long sap,
				int sap_length);

extern	void	ill_down(ill_t * ill);

extern	void	ill_fastpath_ack(ill_t * ill, mblk_t * mp);

extern	void	ill_fastpath_probe(ill_t * ill, mblk_t *dlur_mp);

extern	boolean_t	ill_frag_timeout(ill_t * ill, u_long dead_interval);

extern	int	ill_init(queue_t * q, ill_t * ill);

extern	ill_t	*ill_lookup_on_name(char * name, u_int namelen);

extern	int	ip_ill_report(queue_t * q, mblk_t * mp, caddr_t arg);

extern	int	ip_ipif_report(queue_t * q, mblk_t * mp, caddr_t arg);

extern	void	ip_ll_subnet_defaults(ill_t * ill, mblk_t * mp);

extern	void	ip_sioctl_copyin_done(queue_t * q, mblk_t * mp);

extern	void	ip_sioctl_copyin_setup(queue_t * q, mblk_t * mp);

extern	int	ip_sioctl_copyin_writer(mblk_t * mp);

extern	void	ip_sioctl_iocack(queue_t * q, mblk_t * mp);

extern	boolean_t	ipif_arp_up(ipif_t * ipif, u32 addr);

extern	void	ipif_down(ipif_t * ipif);

extern	char	*ipif_get_name(ipif_t * ipif, char * buf, int len);

extern	ipif_t	*ipif_lookup(ill_t * ill, u32 addr);

extern	boolean_t	ipif_loopback_init(void);

extern	void	ipif_mask_reply(ipif_t * ipif);

extern	ire_t	*ipif_to_ire(ipif_t * ipif);
#else /* __STDC__ */
extern	mblk_t	*ill_arp_alloc();

extern	void	ill_delete();

extern	mblk_t	*ill_dlur_gen();

extern	void	ill_down();

extern	void	ill_fastpath_ack();

extern	void	ill_fastpath_probe();

extern	boolean_t	ill_frag_timeout();

extern	int	ill_init();

extern	ill_t	*ill_lookup_on_name();

extern	int	ip_ill_report();

extern	int	ip_ipif_report();

extern	void	ip_ll_subnet_defaults();

extern	void	ip_sioctl_copyin_done();

extern	void	ip_sioctl_copyin_setup();

extern	void	ip_sioctl_iocack();

extern	boolean_t	ipif_arp_up();

extern	void	ipif_down();

extern	char	*ipif_get_name();

extern	ipif_t	*ipif_lookup();

extern	boolean_t	ipif_loopback_init();

extern	void	ipif_mask_reply();

extern	ire_t	*ipif_to_ire();
#endif /* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_IP_IF_H */
