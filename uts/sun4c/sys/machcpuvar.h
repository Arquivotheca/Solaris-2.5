/*
 * Copyright 1990, Sun Microsystems,  Inc.
 */

#ifndef	_SYS_MACHCPUVAR_H
#define	_SYS_MACHCPUVAR_H

#pragma ident	"@(#)machcpuvar.h	1.7	94/10/12 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM
/*
 * Machine specific fields of the cpu struct
 * defined in common/sys/cpuvar.h.
 *
 * Note:  This is kinda kludgy but seems to be the best
 * of our alternatives.
 */
struct	machcpu {
	struct machpcb *mpcb;
	int	dummy;
};
#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MACHCPUVAR_H */
