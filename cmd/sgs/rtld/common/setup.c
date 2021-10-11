/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)setup.c	1.21	95/07/28 SMI"

/*
 * Run time linker common setup.
 *
 * Called from _setup to get the process going at startup.
 */
#include	<string.h>
#include	<unistd.h>
#include	<dlfcn.h>
#include	"_rtld.h"
#include	"_elf.h"
#include	"profile.h"
#include	"debug.h"

extern char **	envp;

#if	defined(i386)
extern int	_elf_copy_gen(Rt_map *);
#endif

int
setup(Rt_map * lmp)
{
	Listnode *	lnp;
	Rt_map *	nlmp;
	char *		name;
	char **		addr;
	Sym *		sym;
	Permit *	permit;
	Rel_copy *	_rcp, * rcp;
	void (*		rptr)() = (void (*)())(r_debug.r_brk);

	PRF_MCOUNT(59, setup);

	/*
	 * Alert the debuggers that we are abount to mess with the link-map list
	 * (dbx doesn't care about this as it doesn't know we're around until
	 * the getpid() call later, prehaps adb needs this state information).
	 */
	r_debug.r_state = RT_ADD;
	(* rptr)();

	/*
	 * Obtain an initial permit structure and mark this object as
	 * promiscuous (the trick here is not to set the promiscuous bit for
	 * any interpretor maps that may have been loaded).  The promiscuous
	 * flag allows anyone to look for symbols within that module.
	 * These attributes will be applied to any preloaded objects and
	 * shared library dependencies.
	 */
	if ((permit = perm_get()) == 0)
		return (0);
	PERMIT(lmp) = permit;
	FLAGS(lmp) |= (FLG_RT_PROMISC | FLG_RT_NODELETE);
	DBG_CALL(Dbg_file_ref(NAME(lmp), COUNT(lmp), PERMIT(lmp),
	    FLG_RT_PROMISC));

	/*
	 * Map in any preloadable shared objects.  Note, it is valid to preload
	 * a 4.x shared object with a 5.0 executable (or visa-versa), as this
	 * functionality is required by ldd(1).
	 */
	if (preload.head) {
		DBG_CALL(Dbg_util_nl());
		for (LIST_TRAVERSE(&preload, lnp, name)) {
			if (is_so_loaded(&lml_main, name) == 0) {
				DBG_CALL(Dbg_file_preload(name));

				/*
				 * If this is a secure application only allow
				 * simple filenames to be preloaded. The lookup
				 * for these files will be restricted, but is
				 * allowed by placing preloaded objects in
				 * secure directories.
				 */
				if (rtld_flags & RT_FL_SECURE) {
					if (strchr(name, '/')) {
						DBG_CALL(Dbg_libs_ignore(name));
						continue;
					}
				}
				if ((nlmp = load_so(&lml_main, name, lmp)) == 0)
					if (tracing)
						continue;
					else
						return (0);

				PERMIT(nlmp) = perm_set(0, permit);
				FLAGS(nlmp) |=
				    (FLG_RT_PROMISC | FLG_RT_NODELETE);
				DBG_CALL(Dbg_file_ref(NAME(nlmp), COUNT(nlmp),
				    PERMIT(nlmp), FLG_RT_PROMISC));
			}
		}
	}

	/*
	 * Load all dependent (needed) objects.
	 */
	if (analyze_so(&lml_main, lmp, (bind_mode | RTLD_GLOBAL), 0) == 0)
		return (0);

	/*
	 * If we are tracing (with ldd(1)) but we don't want to do any further
	 * checks for relocation, exit.
	 */
	if (tracing && !(rtld_flags & RT_FL_WARN))
		exit(0);

	DBG_CALL(Dbg_file_nl());

	/*
	 * Relocate all the initial dependencies we've just added.
	 */
	if (relocate_so(lmp, bind_mode) == 0)
		return (0);

	/*
	 * Perform special copy type relocations.
	 */
#if	defined(i386)
	if (_elf_copy_gen(lmp) == 0)
		return (0);
#endif
	rcp = copies;
	while (rcp) {
		_rcp = rcp;
		(void) memcpy(rcp->r_to, rcp->r_from, rcp->r_size);
		rcp = rcp->r_next;
		(void) free(_rcp);
	}

	/*
	 * If we are tracing we're done.
	 */
	if (tracing)
		exit(0);

	/*
	 * Lookup up the _environ symbol for getenv() support in .init sections.
	 */
	if (sym = LM_LOOKUP_SYM(lmp)((const char *)"_environ", PERMIT(lmp),
	    lmp, lmp, &nlmp, LKUP_DEFT)) {
		addr = (char **)sym->st_value;
		if (!(FLAGS(nlmp) & FLG_RT_ISMAIN))
			addr = (char **)((int)addr + (int)ADDR(nlmp));
		*addr = (char *)envp;
	}

	/*
	 * Inform the debuggers we're here and stable.  Newer debuggers can
	 * indicate their presence by setting the DT_DEBUG entry in the dynamic
	 * executable (see elf_new_lm()).  In this case call getpid() so that
	 * the debugger can catch the system call.  This allows the debugger to
	 * initialize at this point and consequently allows the user to set
	 * break points in .init code.
	 */
	r_debug.r_state = RT_CONSISTENT;
	(* rptr)();
	if (FLAGS(lmp) & FLG_RT_DEBUGGER)
		(void) getpid();

	/*
	 * Call all dependencies .init sections and clean up any file
	 * descriptors we've opened for ourselves.
	 */
	rtld_flags |= RT_FL_APPLIC;
	call_init(lmp);
	dz_close();
	fm_cleanup(fmap);
	return (1);
}
