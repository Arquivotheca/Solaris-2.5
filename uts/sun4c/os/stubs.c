#ident	"@(#)stubs.c	1.4	94/10/24 SMI"

/*
 * Stubs for platform-specific routines so that the
 * platform-independent part of the kernel will compile.
 * Note: platform-independent kernel source should
 * dynamically test for platform-specific attributes
 * and *never* call these stubs.
 */
#include <sys/types.h>
#include <sys/cpuvar.h>
#include <sys/cmn_err.h>

/*
 * Stubs for MP support.
 */

/*ARGSUSED*/
void
poke_cpu(int cpun)
{
	cmn_err(CE_PANIC, "ERROR: stub for poke_cpu() called.");
}

/*ARGSUSED*/
void
mp_cpu_start(struct cpu *cp)
{
	cmn_err(CE_PANIC, "ERROR: stub for mp_cpu_start() called.");
}

/*ARGSUSED*/
void
mp_cpu_stop(struct cpu *cp)
{
	cmn_err(CE_PANIC, "ERROR: stub for mp_cpu_stop() called.");
}

/*ARGSUSED*/
int
cpu_disable_intr(struct cpu *cp)
{
	cmn_err(CE_PANIC, "ERROR: stub for cpu_disable_intr() called.");
	/*NOTREACHED*/
	return (0);
}

/*ARGSUSED*/
void
cpu_enable_intr(struct cpu *cp)
{
	cmn_err(CE_PANIC, "ERROR: stub for cpu_enable_intr() called.");
}

/*ARGSUSED*/
void
set_idle_cpu(int cpun)
{
	cmn_err(CE_PANIC, "ERROR: stub for set_idle_cpu() called.");
}

/*ARGSUSED*/
void
unset_idle_cpu(int cpun)
{
	cmn_err(CE_PANIC, "ERROR: stub for unset_idle_cpu() called.");
}
