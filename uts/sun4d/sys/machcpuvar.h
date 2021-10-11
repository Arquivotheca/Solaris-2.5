/*
 * Copyright (c) 1990, Sun Microsystems,  Inc.
 */

#ifndef	_SYS_MACHCPUVAR_H
#define	_SYS_MACHCPUVAR_H

#pragma ident	"@(#)machcpuvar.h	1.16	94/10/12 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Machine specific fields of the cpu struct
 * defined in common/sys/cpuvar.h.
 */
struct	machcpu {
	struct machpcb	*mpcb;
	u_int	syncflt_status;
	u_int	syncflt_addr;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MACHCPUVAR_H */
