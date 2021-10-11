/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef _SYS_PPP_EXTERN_H
#define	_SYS_PPP_EXTERN_H

#pragma ident	"@(#)ppp_extern.h	1.14	94/01/04 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef __STDC__
void start_restart_timer(pppMachine_t *);
void ppp_cross_fsm(pppLink_t *, u_int, pppProtocol_t, u_int);
void ppp_reneg(pppLink_t *);
void lcp_external_event(pppLink_t *, u_int);
void ncp_external_event(pppLink_t *, u_int);
void pap_external_event(pppLink_t *, u_int);
void chap_external_event(pppLink_t *, u_int);
void pap_external_event(pppLink_t *, u_int);
void lqm_external_event(pppLink_t *, u_int);
void ppp_notify_lm(pppLink_t *, u_int, pppProtocol_t, caddr_t, int);
void ppp_notify_config_change(pppMachine_t *);
void ppp_internal_event(pppLink_t *, u_int, pppProtocol_t);
int  is_open(pppMachine_t *);
void send_protocol_reject(pppLink_t *, queue_t *, mblk_t *);
void str_print(queue_t *, mblk_t *);
pppMachine_t *alloc_lcp_machine(queue_t *, pppLink_t *);
pppMachine_t *alloc_ipncp_machine(queue_t *, pppLink_t *);
chapMachine_t *alloc_chap_machine(queue_t *, pppLink_t *);
papMachine_t *alloc_pap_machine(queue_t *, pppLink_t *);
lqmMachine_t *alloc_lqm_machine(queue_t *, pppLink_t *);
void free_machine(pppMachine_t *);
void free_chap_machine(chapMachine_t *);
void free_pap_machine(papMachine_t *);
void free_lqm_machine(lqmMachine_t *);
mblk_t *ppp_alloc_frame(pppProtocol_t, u_int, u_int);
mblk_t *ppp_alloc_raw_frame(pppProtocol_t);
int add_opt(pppOption_t **, u_int, u_int, caddr_t, int (*)());
void ppp_ioctl(queue_t *, mblk_t *);
int  pap_ioctl(papMachine_t *, int, mblk_t *);
int  chap_ioctl(chapMachine_t *, int, mblk_t *);
int  ppp_get_conf_ipncp(pppMachine_t *, pppLinkControlEntry_t *);
int  ppp_get_conf_lcp(pppMachine_t *, pppLinkControlEntry_t *);
int  ppp_set_conf_ipncp(pppMachine_t *, pppLinkControlEntry_t *);
int  ppp_set_conf_lcp(pppMachine_t *, pppLinkControlEntry_t *);
int  ppp_set_auth(pppMachine_t *, pppAuthControlEntry_t *);
void do_incoming_cp(pppMachine_t *, mblk_t *);
void do_incoming_lqm(lqmMachine_t *, mblk_t *);
void do_incoming_chap(chapMachine_t *, mblk_t *);
void do_incoming_pap(papMachine_t *, mblk_t *);
void ppp_fsm(pppMachine_t *, pppEvent_t);
int  ppp_ipncp_initialize(void);
int  ppp_lcp_initialize(void);
void ppp_start_restart_timer(pppMachine_t *machp);
void tx_lqr(lqmMachine_t *, mblk_t *);
void rx_lqr(lqmMachine_t *, mblk_t *);
void ppp_error_ind(pppLink_t *, enum ppp_errors, u_char *, u_int);
void frame_dump(char *, mblk_t *);
void ip_pkt_in(pppMachine_t *, mblk_t *, u_int);
void ip_pkt_out(pppMachine_t *, mblk_t *, u_int);
void queue_memory_retry(queue_t *);
void ppp_putnext(queue_t *, mblk_t *);
void ppp_randomize(void);
u_long ppp_rand(void);
void ppp_apply_ipncp_option(pppMachine_t *, pppOption_t *);
void ppp_apply_lcp_option(pppMachine_t *, pppOption_t *);


#else	/* __STDC__ */

void start_restart_timer(/* pppMachine_t * */);
void ppp_cross_fsm(/* pppLink_t *, u_int, pppProtocol_t, u_int */);
void ppp_reneg(/* pppLink_t * */);
void lcp_external_event(/* pppLink_t *, u_int */);
void ncp_external_event(/* pppLink_t *, u_int */);
void pap_external_event(/* pppLink_t *, u_int */);
void chap_external_event(/* pppLink_t *, u_int */);
void pap_external_event(/* pppLink_t *, u_int */);
void lqm_external_event(/* pppLink_t *, u_int */);
void ppp_notify_lm(/* pppLink_t *, u_int, pppProtocol_t, caddr_t, int */);
void ppp_notify_config_change(/* pppMachine_t * */);
void ppp_internal_event(/* pppLink_t *, u_int, pppProtocol_t */);
int  is_open(/* pppMachine_t * */);
void send_protocol_reject(/* pppLink_t *, queue_t *, mblk_t * */);
void str_print(/* queue_t *, mblk_t * */);
pppMachine_t *alloc_lcp_machine(/* queue_t *, pppLink_t * */);
pppMachine_t *alloc_ipncp_machine(/* queue_t *, pppLink_t * */);
chapMachine_t *alloc_chap_machine(/* queue_t *, pppLink_t * */);
papMachine_t *alloc_pap_machine(/* queue_t *, pppLink_t * */);
lqmMachine_t *alloc_lqm_machine(/* queue_t *, pppLink_t * */);
void free_machine(/* pppMachine_t * */);
void free_chap_machine(/* chapMachine_t * */);
void free_pap_machine(/* papMachine_t * */);
void free_lqm_machine(/* lqmMachine_t * */);
mblk_t *ppp_alloc_frame(/* pppProtocol_t, u_int, u_int */);
mblk_t *ppp_alloc_raw_frame(/* pppProtocol_t */);
int add_opt(/* pppOption_t **, u_int, u_int, caddr_t, int (*)() */);
void ppp_ioctl(/* queue_t *, mblk_t * */);
int  pap_ioctl(/* papMachine_t *, int, mblk_t * */);
int  chap_ioctl(/* chapMachine_t *, int, mblk_t * */);
int  ppp_get_conf_ipncp(/* pppMachine_t *, pppLinkControlEntry_t * */);
int  ppp_get_conf_lcp(/* pppMachine_t *, pppLinkControlEntry_t * */);
int  ppp_set_conf_ipncp(/* pppMachine_t *, pppLinkControlEntry_t * */);
int  ppp_set_conf_lcp(/* pppMachine_t *, pppLinkControlEntry_t * */);
void do_incoming_cp(/* pppMachine_t *, mblk_t * */);
void do_incoming_lqm(/* lqmMachine_t *, mblk_t * */);
void do_incoming_chap(/* chapMachine_t *, mblk_t * */);
void do_incoming_pap(/* papMachine_t *, mblk_t * */);
void ppp_fsm(/* pppMachine_t *, pppEvent_t */);
int  ppp_ipncp_initialize(/* void */);
int  ppp_lcp_initialize(/* void */);
void ppp_start_restart_timer(/* pppMachine_t *machp */);
void tx_lqr(/* lqmMachine_t *, mblk_t * */);
void rx_lqr(/* lqmMachine_t *, mblk_t * */);
void ppp_error_ind(/* pppMachine_t *, enum ppp_errors, mblk_t * */);
void frame_dump(/* char *, mblk_t * */);
void ip_pkt_in(/* pppMachine_t *, mblk_t *, u_int */);
void ip_pkt_out(/* pppMachine_t *, mblk_t *, u_int */);
void queue_memory_retry(/* queue_t * */);
void ppp_putnext(/* queue_t *, mblk_t * */);
void ppp_randomize(/* void */);
u_long ppp_rand(/* void */);
void ppp_apply_ipncp_option(/* pppMachine_t *, pppOption_t * */);
void ppp_apply_lcp_option(/* pppMachine_t *, pppOption_t * */);

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PPP_EXTERN_H */
