#ident	"@(#)stubs.c	1.3	94/07/01 SMI"

/*
 * Stubs for platform-specific routines so that the
 * platform-independent part of the kernel will compile.
 * Note: platform-independent kernel source should
 * dynamically test for platform-specific attributes
 * and *never* call these stubs.
 */
#include <sys/types.h>
#include <sys/cmn_err.h>

#if defined(__STDC__)
#define	STUB(name) \
	void \
	/* CSTYLED */ \
	name/**/() \
	{ \
		cmn_err(CE_PANIC, "ERROR: stub for " #name "() called.\n"); \
	}
#else
#define	STUB(name) \
	void \
	/* CSTYLED */ \
	name/**/() \
	{ \
		cmn_err(CE_PANIC, "ERROR: stub for " "name" "() called.\n"); \
	}


#endif /* defined(__STDC__) */

/*
 * Stubs for MP support.
 */
STUB(poke_cpu)
STUB(mp_cpu_start)
STUB(mp_cpu_stop)
STUB(cpu_disable_intr)
STUB(cpu_enable_intr)
STUB(set_idle_cpu)
STUB(unset_idle_cpu)
