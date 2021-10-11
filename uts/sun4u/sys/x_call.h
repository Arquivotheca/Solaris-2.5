/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_X_CALL_H
#define	_SYS_X_CALL_H

#pragma ident	"@(#)x_call.h	1.10	95/04/25 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	MP

#define	XCALL_PIL 13	/* prom uses 14, and error handling uses 15 */

#ifndef _ASM

typedef	unsigned long cpuset_t;	/* a set of CPUs */

#if defined(_KERNEL)

/*
 * Cross-call routines.
 */
extern void	xt_one(int, u_int, u_int, u_int, u_int, u_int);
extern void	xt_some(cpuset_t, u_int, u_int, u_int, u_int, u_int);
extern void	xt_all(u_int, u_int, u_int, u_int, u_int);
extern void	xc_attention(cpuset_t);
extern void	xc_dismissed(cpuset_t);
extern void	xc_one(int, u_int (*func)(), u_int, u_int);
extern void	xc_some(cpuset_t, u_int (*func)(), u_int, u_int);
extern void	xc_all(u_int (*func)(), u_int, u_int);
extern void	xc_init(void);

#endif	/* _KERNEL */

#endif	/* !_ASM */

#endif	/* MP */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_X_CALL_H */
