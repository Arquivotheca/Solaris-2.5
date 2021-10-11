/*
 * Copyright (c) 1991, by Sun Microsystems,  Inc.
 */

#ifndef	_SYS_MACHCPUVAR_H
#define	_SYS_MACHCPUVAR_H

#pragma ident	"@(#)machcpuvar.h	1.22	95/07/11 SMI"

#include <sys/x_call.h>
#include <sys/intr.h>
#include <sys/clock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The mid is the same as the cpu id.
 * We might want to change this later
 */
#define	UPAID2CPU(upaid)	(upaid)

#ifndef	_ASM

#include <sys/prom_isa.h>

/*
 * Machine specific fields of the cpu struct
 * defined in common/sys/cpuvar.h.
 */
struct	machcpu {
	struct machpcb	*mpcb;
	int		mutex_ready;
	int		in_prom;
	char		*cpu_info;
	u_longlong_t	tmp1;		/* per-cpu tmps */
	u_longlong_t	tmp2;		/*  used in trap processing */
	struct intr_req intr_pool[INTR_PENDING_MAX];	/* intr pool */
	struct intr_req *intr_head[PIL_LEVELS];		/* intr que heads */
	struct intr_req *intr_tail[PIL_LEVELS];		/* intr que tails */
	long long	tickint_intrvl;
	u_longlong_t	tick_happened;
	struct tick_info tick_clnt[TICK_CLNTS]; /* tick int clients */

};

/*
 * The OpenBoot Standalone Interface supplies the kernel with
 * implementation dependent parameters through the devinfo/property mechanism
 */
#define	MAXSYSNAME	20

struct cpu_node {
	char	name[MAXSYSNAME];
	int	implementation;
	int	version;
	int	upaid;
	dnode_t	nodeid;
	u_int	clock_freq;
	union {
		int	dummy;
	}	u_info;
};

#define	CPUINFO_SZ	80	/* cpu_info_buf size for each cpu */

extern struct cpu_node cpunodes[];

#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MACHCPUVAR_H */
