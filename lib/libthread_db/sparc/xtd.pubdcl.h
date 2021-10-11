/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _XTD_PUBDCL_H
#define	_XTD_PUBDCL_H

#pragma ident	"@(#)xtd.pubdcl.h	1.8	94/12/31 SMI"

extern td_err_e td_ta_map_lwp2thr(const td_thragent_t *ta_p,
	lwpid_t lwpid, td_thrhandle_t *th_p);


extern td_err_e td_thr_getgregs(const td_thrhandle_t *th_p,
	prgregset_t regset);
extern td_err_e td_thr_setgregs(const td_thrhandle_t *th_p,
	const prgregset_t regset);

#endif /* _XTD_PUBDCL_H */
