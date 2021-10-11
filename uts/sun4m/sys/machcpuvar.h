/*
 * Copyright (c) 1991, by Sun Microsystems,  Inc.
 */

#ifndef	_SYS_MACHCPUVAR_H
#define	_SYS_MACHCPUVAR_H

#pragma ident	"@(#)machcpuvar.h	1.21	94/09/29 SMI"

#include <sys/xc_levels.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * A level 1 mbus module imples that the machine
 * is running in a uni-processor configuration and
 * sends out "F" as its module id on the M-bus.
 */
#define	MID2CPU(mid)	((mid == 15) ? 0 : (mid ^ 8))

#ifndef	_ASM
/*
 * Machine specific fields of the cpu struct
 * defined in common/sys/cpuvar.h.
 */
struct	machcpu {
	struct machpcb	*mpcb;
	volatile int	xc_pend[X_CALL_LEVELS];
	volatile int	xc_wait[X_CALL_LEVELS];
	volatile int	xc_ack[X_CALL_LEVELS];
	volatile int	xc_state[X_CALL_LEVELS];
	volatile int	xc_retval[X_CALL_LEVELS];
	volatile int	syncflt_status;
	int	syncflt_addr;
	int	in_prom;
	int	bcopy_res;
	char	*cpu_info;
};
#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MACHCPUVAR_H */
