/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)bindings.c	1.8	94/10/21 SMI"

/* LINTLIBRARY */

#include	<sys/types.h>
#include	<string.h>
#include	"paths.h"
#include	"_debug.h"

/*
 * Normally we don't want to display any ld.so.1 bindings (i.e. the bindings
 * to these calls themselves). So, if a Dbg_bind_global() originates from
 * ld.so.1 don't print anything.  If we really want to see the ld.so.1 bindings,
 * simply give the run-time linker a different SONAME.
 */
void
Dbg_bind_global(const char * ffile, caddr_t fabs, caddr_t frel, int pltndx,
	const char * tfile, caddr_t tabs, caddr_t trel, const char * sym)
{
	if (DBG_NOTCLASS(DBG_BINDINGS))
		return;
	if (strcmp(ffile, LDSO_PATH) == 0)
		return;

	if (DBG_NOTDETAIL())
		dbg_print("binding file=%s to file=%s: symbol `%s'", ffile,
			tfile, sym);
	else {
		if (pltndx != -1) {
			/*
			 * Called from a plt offset.
			 */
			dbg_print("binding file=%s (%#x:%#x) at plt[%d] to "
			    "file=%s (%#x:%#x): symbol `%s'", ffile, fabs,
			    frel, pltndx, tfile, tabs, trel, sym);
		} else if ((fabs == 0) && (frel == 0)) {
			/*
			 * Called from a dlsym().  We're not really performing
			 * a relocation, but are handing the address of the
			 * symbol back to the user.
			 */
			dbg_print("binding file=%s (dlsym) to "
			    "file=%s (%#x:%#x): symbol `%s'", ffile, tfile,
			    tabs, trel, sym);
		} else {
			/*
			 * Standard relocation.
			 */
			dbg_print("binding file=%s (%#x:%#x) to "
			    "file=%s (%#x:%#x): symbol `%s'", ffile, fabs,
			    frel, tfile, tabs, trel, sym);
		}
	}
}

void
Dbg_bind_weak(const char * ffile, caddr_t fabs, caddr_t frel, const char * sym)
{
	if (DBG_NOTCLASS(DBG_BINDINGS))
		return;

	if (DBG_NOTDETAIL())
		dbg_print("binding file=%s to 0x0 (undefined weak): "
			"symbol `%s'", ffile, sym);
	else
		dbg_print("binding file=%s (%#x:%#x) to 0x0 (undefined weak): "
			"symbol `%s'", ffile, fabs, frel, sym);
}

void
Dbg_bind_profile(int ndx, int count)
{
	if (DBG_NOTCLASS(DBG_BINDINGS))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print("    profiling symbol[%d];  call count=%d", ndx, count);
}
