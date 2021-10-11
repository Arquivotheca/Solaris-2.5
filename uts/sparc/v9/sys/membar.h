/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_MEMBAR_H
#define	_SYS_MEMBAR_H

#pragma ident	"@(#)membar.h	1.2	94/11/11 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(_KERNEL)
extern void membar_ldld();
extern void membar_stld();
extern void membar_ldst();
extern void membar_stst();

extern void membar_ldld_ldst();
extern void membar_ldld_stld();
extern void membar_ldld_stst();

extern void membar_stld_ldld();
extern void membar_stld_ldst();
extern void membar_stld_stst();

extern void membar_ldst_ldld();
extern void membar_ldst_stld();
extern void membar_ldst_stst();

extern void membar_stst_ldld();
extern void membar_stst_stld();
extern void membar_stst_ldst();

extern void membar_lookaside();
extern void membar_memissue();
extern void membar_sync();
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MEMBAR_H */
