#ident	"@(#)stubs.c	1.3	94/10/24 SMI"

/*
 * Stubs for platform-specific routines so that the
 * platform-independent part of the kernel will compile.
 * Note: platform-independent kernel source should
 * dynamically test for platform-specific attributes
 * and *never* call these stubs.
 */
#include <sys/types.h>
#include <sys/cmn_err.h>

/*
 * XXX Stubs for VAC support,
 * required by crash dump routines.
 */
void
vac_flushall(void)
{
	cmn_err(CE_PANIC, "ERROR: stub for vac_flushall() called.");
}
