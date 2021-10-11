/*
 * Copyright (c) 1991, 1993 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_GPROF_H
#define	_SYS_GPROF_H

#pragma ident	"@(#)gprof.h	1.13	93/05/30 SMI"

#if defined(_KERNEL)
#include <sys/regset.h>
#endif /* _KERNEL */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 *	kernel profiling structs and defines
 *
 */
#define	PROFILE_NCPUS	0
#define	PROFILE_STATUS	1
#define	PROFILE_INIT 	2
#define	PROFILE_ON	3
#define	PROFILE_OFF	4
#define	PROFILE_DUMP	5
#define	PROFILE_SNAP	6
#define	PROFILE_RESET	7
#define	PROFILE_DEALLOC	8
#define	PROFILE_QUERY	9

#define	NCPUS_PROFILING    	_IOR('p', 0, int)
#define	INIT_PROFILING    	_IOR('p', 1, int)
#define	DEALLOC_PROFILING    	_IOR('p', 2, int)
#define	MCOUNT_TRACING    	_IOR('p', 3, int)

#define	EPROFILING	 -2
#define	ENOTPROFILING	 -3
#define	ENOTINIT	 -4

#ifndef _ASM

#if defined(_KERNEL)

#define	VALID_CPU(i)	((cpu[i] != NULL) && \
		((cpu[i]->cpu_flags & CPU_OFFLINE) == 0))

extern 	void 	enable_profiling(int cpuid);
extern 	void	disable_profiling(int cpuid);
extern 	void 	clear_profiling_intr(int cpuid);
extern 	void 	fast_profile_intr(void);
extern	u_int	get_profile_pc(void *);
extern 	int 	have_fast_profile_intr;
extern	char	Sysbase[];
extern	char	etext[];
extern	void	_start();

#endif /* _KERNEL */
/*
 * Profiling interrupt pc sample structure.
 *
 * Formed into chains connected to a hash array.
 */
typedef struct kp_sample {
	struct kp_sample	*link;
	char			*pc;
	long			count;
} kp_sample_t;

/*
 * Mcount procedure call structure.
 *
 * Formed into chains connected to a hash array.
 */
typedef struct kp_call {
	struct kp_call	*link;
	char		*frompc;
	char		*topc;
	long		count;
} kp_call_t;

/*
 * Per cpu profiling information.
 */
typedef struct kern_profiling {
	int		profiling;
	unsigned char	profiling_lock;
	void		*rp;
	kp_sample_t	**s_hash;
	kp_sample_t	*samples;
	kp_call_t	**froms;
	kp_call_t	*tos;
	int		s_hashsize;
	int		samplessize;
	int		fromssize;
	int		tossize;
	kp_sample_t	*samplesnext;
	kp_call_t	*tosnext;
	int		kernel_textsize;
	char 		*kernel_lowpc;
	char 		*kernel_highpc;
	int		module_textsize;
	char  		*module_lowpc;
	char		*module_highpc;
} kernp_t;

/*
 * Bytes of code per sample structure.  Note: bytes, not instructions.
 *
 * For a 2M image this would permit 15000 distinct sample address, which at
 * 100 samples per second would provide a minimum of 2.5 minutes system time.
 * In reality the same addresses will occur frequently, and so we will have
 * much more time.
 */
#define	SAMPLE_RATIO	140

/*
 * Maximum tolerable bytes of code per sample hash chain.
 *
 *	Since sampling occurs infrequently we can tolerate a significant
 *	average hash chain length.
 */
#define	SAMPLE_HASH_RATIO	(20 * SAMPLE_RATIO)

/*
 * Bytes of code per call structure.  Note: bytes, not instructions.
 *
 * A quick experiment discovered that an unoptimized SPARC kernel contains
 * roughly one call site for every 70 bytes of code.  But not every call is
 * likely to be executed, thus we can go significantly higher.
 */
#define	CALL_RATIO	200

/*
 * Maximum tolerable bytes of code per call hash chain.
 *
 * Since calls occur frequently we can only tolerate a very small average hash
 * chain length.
 */
#define	CALL_HASH_RATIO	(CALL_RATIO)

/*
 * Gprof style profiling header.
 *
 * Followed by an array of histogram counters, and then the call data.
 */
struct phdr {
		char	*lpc;
		char	*hpc;
		int	ncnt;
};

/*
 * Histogram counters (according to gprof).
 */
#define	HISTCOUNTER	unsigned short

/*
 * Bytes of code mapped by a histogram counter (according to gprof).
 */
#define	HIST_GRANULARITY	4

/*
 * Call data (according to gprof).
 */
struct rawarc {
	unsigned long	raw_frompc;
	unsigned long	raw_topc;
	long		raw_count;
};

/*
 * General rounding functions.
 */
#define	PROFILE_ROUNDDOWN(x, y)	(((x)/(y))*(y))
#define	PROFILE_ROUNDUP(x, y)	((((x)+(y)-1)/(y))*(y))

#endif /* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_GPROF_H */
