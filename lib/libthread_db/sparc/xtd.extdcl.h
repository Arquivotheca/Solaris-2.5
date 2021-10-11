/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _XTD_EXTDCL_H
#define	_XTD_EXTDCL_H

#pragma ident	"@(#)xtd.extdcl.h	1.10	95/01/12 SMI"

#include "xtd_to.h"
extern td_ierr_e __map_fp_reg2pr(prfpregset_t * sink, fpregset_t * source);
extern td_ierr_e __map_fp_pr2reg(fpregset_t * sink, prfpregset_t * source);
extern	td_terr_e
__td_write_thr_struct(td_thragent_t *ta_p,
	paddr_t thr_addr, uthread_t * thr_struct_p);

#endif /* _XTD_EXTDCL_H */
