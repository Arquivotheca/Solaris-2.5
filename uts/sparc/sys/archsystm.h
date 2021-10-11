/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_ARCHSYSTM_H
#define	_SYS_ARCHSYSTM_H

#pragma ident	"@(#)archsystm.h	1.11	95/02/28 SMI"

/*
 * A selection of ISA-dependent interfaces
 */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_KERNEL) && !defined(_ASM)

#include <sys/types.h>
#include <sys/regset.h>

extern greg_t getfp(void);
extern greg_t getpsr(void);
extern greg_t getpil(void);
extern greg_t gettbr(void);
extern greg_t getvbr(void);

/*
 * XXX	What's ISA-dependent about this?
 */
extern void realsigprof(int, int);

struct _klwp;
extern void xregrestore(struct _klwp *, int);

extern void vac_flushall(void);

extern void bind_hwcap(void);
extern void kern_use_hwinstr(int hwmul, int hwdiv);
extern int get_hwcap_flags(int inkernel);

extern int enable_mixed_bcp; /* patchable in /etc/system */

#endif /* _KERNEL && !_ASM */

/*
 * Flags used to hint at various performance enhancements available
 * on different SPARC processors.
 */
#define	AV_SPARC_HWMUL_32x32	1	/* 32x32-bit smul/umul is efficient */
#define	AV_SPARC_HWDIV_32x32	2	/* 32x32-bit sdiv/udiv is efficient */
#define	AV_SPARC_HWFSMULD	4	/* fsmuld is efficient */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_ARCHSYSTM_H */
