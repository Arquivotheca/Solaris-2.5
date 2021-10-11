/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)dlfcns.c	1.39	95/07/31 SMI"

/*
 * Programmatic interface to the run_time linker.
 */
#include	<string.h>
#include	<dlfcn.h>
#include	<synch.h>
#include	"_rtld.h"
#include	"_elf.h"
#include	"profile.h"
#include	"debug.h"

/* LINTLIBRARY */

/*
 * Return pointer to string describing last occurring error.
 * The notion of the last occurring error is cleared.
 */
#pragma weak dlerror = _dlerror

char *
_dlerror()
{
	char *	_lasterr = lasterr;

	PRF_MCOUNT(20, _dlerror);

	lasterr = (char *)0;
	return (_lasterr);
}

/*
 * Create new dlp for this dlopened object.  Create a list of pointers to any
 * dependent (needed) link map structures, with the dlopened object's link map
 * at the head of the list.
 */
static Dl_obj *
dlp_create(Rt_map * lmp, Permit * permit, int mode)
{
	Dl_obj	*	dlp;

	PRF_MCOUNT(22, dlp_create);

	/*
	 * Create a new dlp.
	 */
	if ((dlp = (Dl_obj *)malloc(sizeof (Dl_obj))) == 0)
		return (0);
	DLP(lmp) = dlp;
	dlp->dl_refcnt = 1;
	dlp->dl_id = permit;
	dlp->dl_magic = DL_MAGIC;
	dlp->dl_cigam = DL_CIGAM;
	dlp->dl_lmps.head = 0;
	dlp->dl_lmps.tail = 0;

	/*
	 * Create head node for the dependent link map list.
	 * The head node holds link map of this dlopened object.
	 */
	if (list_append(&dlp->dl_lmps, lmp) == 0) {
		DLP(lmp) = 0;
		dlp_delete(dlp);
		remove_so(&lml_main);
		return (0);
	}

	/*
	 * A permit of zero indicates a dlopen(0). There is no need to create
	 * a link-map list in this case.
	 */
	if (!permit)
		return (dlp);

	/*
	 * Initialize the link map we've just openned with the new permit
	 * identifier and any promiscuous flag.  These attributes will be
	 * propagated to any dependencies of this object during analyze()
	 * and needed() processing.
	 */
	PERMIT(lmp) = perm_set(PERMIT(lmp), permit);
	if (mode & RTLD_GLOBAL)
		FLAGS(lmp) |= FLG_RT_PROMISC;
	DBG_CALL(Dbg_file_ref(NAME(lmp), COUNT(lmp), PERMIT(lmp),
	    (FLAGS(lmp) & FLG_RT_PROMISC)));

	return (dlp);
}

/*
 * Open a shared object.  Uses rtld to map the object into the process'
 * address space and maintains a list of known objects. On success, returns
 * a pointer to the structure containing information about the newly added
 * object. On failure, returns a null pointer.
 */
#pragma weak dlopen = _dlopen

static void *	__dlopen(const char *, int, unsigned int, Rt_map **);

void *
_dlopen(const char * path, int mode)
{
	void *		error;
	int		bind;
	Rt_map *	ilmp = 0;

	if ((bind = bind_guard(THR_FLG_BIND)) == 1)
		(void) rw_wrlock(&bindlock);
	error = __dlopen(path, mode, caller(), &ilmp);
	dz_close();
	fm_cleanup(fmap);
	if (bind) {
		(void) rw_unlock(&bindlock);
		(void) bind_clear(THR_FLG_BIND);
	}

	/*
	 * After releasing any locks call any .init sections if necessary.
	 * If the dlopen was successful error will be non-zero, and if any
	 * new objects have been added to the link map list the initial
	 * link map pointer will be non-zero.
	 */
	if (error && ilmp)
		call_init(ilmp);
	return (error);
}

static void *
__dlopen(const char * path, int mode, unsigned int cpc, Rt_map ** ilmp)
{
	Rt_map *	clmp, * tlmp, * lmp = NULL;
	Dl_obj *	dlp;
	const char *	name;
	Permit *	permit = 0;
	int		_mode;

	PRF_MCOUNT(21, __dlopen);

	_mode = mode & ~RTLD_GLOBAL;
	if ((_mode != RTLD_LAZY) && (_mode != RTLD_NOW)) {
		eprintf(ERR_FATAL, "dlopen: illegal mode %d", mode);
		return (0);
	}
	if (path && *path == '\0') {
		eprintf(ERR_FATAL, "dlopen: illegal pathname");
		return (0);
	}

	/*
	 * Determine who called us.  The caller address is determined from
	 * the caller() call in _dlopen().  The link map of the caller
	 * must be passed to load_so() so that the appropriate search rules
	 * (4.x or 5.0) are used to locate any dependencies.  Also, if we've
	 * been called from a 4.x module it may be necessary to fix the
	 * specified pathname so that it conforms with the 5.0 elf rules.
	 * If we can not figure out the calling link map assume it's "main".
	 */
	clmp = 0;
	for (tlmp = lml_main.lm_head; tlmp; tlmp = (Rt_map *)NEXT(tlmp)) {
		if ((cpc > ADDR(tlmp)) && (cpc < (ADDR(tlmp) + MSIZE(tlmp)))) {
			clmp = tlmp;
			break;
		}
	}
	if (clmp == 0)
		clmp = lml_main.lm_head;

	DBG_CALL(Dbg_file_dlopen((path ? path : (const char *)"0"),
	    NAME(clmp), mode));

	/*
	 * If the path specified is null then we're operating on promiscuous
	 * objects so there is no need to process a dlp list (dlsym will
	 * simply traverse all objects looking for promiscuous entries).
	 */
	if (!path)
		return (dlp_create(lml_main.lm_head, 0, mode));

	/*
	 * Determine whether we already have a link map for this object.
	 * If the object we are trying to dlopen() has not been mapped, or if
	 * it already exists but is not the object of a previous dlopen(),
	 * obtain a new (unique) permission identifier.
	 */
	lmp = is_so_loaded(&lml_main, path);
	if (!lmp || !DLP(lmp))
		if ((permit = perm_get()) == 0)
			return (0);

	/*
	 * If this object is already loaded simply increment the reference
	 * count.  Otherwise create new dlp struct for this object.
	 */
	if (lmp)
		return ((void *)dl_old_so(lmp, permit, mode));

	/*
	 * Fix the pathname if necessary and generate a new link map.
	 */
	if (LM_FIX_NAME(clmp))
		name = LM_FIX_NAME(clmp)(path);
	else
		name = path;

	if ((dlp = dl_new_so(name, clmp, ilmp, permit, mode)) == 0)
		return (0);

	return ((void *)dlp);
}


/*
 * Assign a dlp to an already loaded shared object and its dependencies,
 */
Dl_obj *
dl_old_so(Rt_map * lmp, Permit * permit, int mode)
{
	Dl_obj *	dlp = DLP(lmp);
	Listnode *	lnp;
	Rt_map *	tlmp;

	if (dlp) {
		dlp->dl_refcnt++;
		/*
		 * We won't need the permit structure that we
		 * allocated, free it now.
		 */
		if (permit)
			perm_free(permit);
	} else {
		if ((dlp = dlp_create(lmp, permit, mode)) == 0) {
			perm_free(permit);
			return (0);
		}

		/*
		 * Special case for ld.so.1.  We don't want to add any of
		 * ld.so.1's dependencies to the users list.
		 */
		if (lmp == lml_rtld.lm_head)
			return (dlp);
	}

	/*
	 * Continue the objects analysis to propagate the dlp list and insure
	 * that any permissions and promiscuous flags are propagated correctly.
	 */
	for (LIST_TRAVERSE(&dlp->dl_lmps, lnp, tlmp)) {
		if ((analyze_so(&lml_main, tlmp, mode, dlp)) == 0) {
			if (--(dlp->dl_refcnt) == 0) {
				DLP(lmp) = 0;
				dlp_delete(dlp);
			}
			return (0);
		}
	}
	return (dlp);
}


/*
 * Open a new shared object, assign a dlp and propagate any dependencies.
 */
Dl_obj *
dl_new_so(const char * name, Rt_map * clmp, Rt_map ** ilmp, Permit * permit,
	int mode)
{
	Rt_map *	lmp, * tlmp;
	Listnode *	lnp;
	Dl_obj *	dlp;
	void (*		rptr)() = (void (*)())(r_debug.r_brk);

	/*
	 * Alert the debuggers that we are about to mess with the link-map and
	 * re-initialize the state in preperation for the debugger consistancy
	 * call.
	 */
	r_debug.r_state = RT_ADD;
	(* rptr)();
	r_debug.r_state = RT_CONSISTENT;

	/*
	 * Load the new object.
	 */
	if ((lmp = load_so(&lml_main, name, clmp)) == 0) {
		perm_free(permit);
		(* rptr)();
		return (0);
	}

	/*
	 * Make sure this really is a newly loaded object.  We may have gotten
	 * here to load `libfoo.so' and found that it is was linked to another
	 * (already loaded) object, say `libfoo.so.1'.
	 */
	if (FLAGS(lmp) & FLG_RT_ANALYZED) {
		(* rptr)();
		return (dl_old_so(lmp, permit, mode));
	}

	/*
	 * Create a new dlp for this object.
	 */
	if ((dlp = dlp_create(lmp, permit, mode)) == 0) {
		perm_free(permit);
		remove_so(&lml_main);
		(* rptr)();
		return (0);
	}

	/*
	 * Save the newly obtained permission identifier in the new link map.
	 * Load any dependencies of this new link map and relocate everything.
	 */
	for (LIST_TRAVERSE(&dlp->dl_lmps, lnp, tlmp)) {
		if ((analyze_so(&lml_main, tlmp, mode, dlp)) == 0) {
			DLP(lmp) = 0;
			dlp_delete(dlp);
			remove_so(&lml_main);
			(* rptr)();
			return (0);
		}
	}
	if ((relocate_so(lmp, mode)) == 0) {
		DLP(lmp) = 0;
		dlp_delete(dlp);
		remove_so(&lml_main);
		(* rptr)();
		return (0);
	}

	/*
	 * Tell the debuggers we're ok again.
	 */
	(* rptr)();

	/*
	 * Indicate which link map to start calling .init sections and return
	 * the dlopen object.
	 */
	*ilmp = lmp;
	return (dlp);
}

/*
 * If nlmp is not already on the dependent link map list for this
 * dlopened object, then add it.
 */
/* ARGSUSED2 */
int
dlp_listadd(Rt_map * lmp, Dl_obj * dlp)
{
	Listnode *	lnp;
	Rt_map *	tlmp;

	PRF_MCOUNT(23, dlp_listadd);

	/*
	 * Check if already on the dependent link map list.
	 */
	for (LIST_TRAVERSE(&dlp->dl_lmps, lnp, tlmp))
		if (tlmp == lmp)
			return (1);

	/*
	 * Add this object to the dependent link map list.
	 */
	if (list_append(&dlp->dl_lmps, lmp) == 0)
		return (0);
	else
		return (1);
}

/*
 * Sanity check a program-provided dlp handle.
 */
static int
valid_handle(Dl_obj * dlp)
{
	/*
	 * Just incase we get some arbitrary, odd-byte aligned handle, round it
	 * (prevents bus errors and alike).
	 */
	dlp = (Dl_obj *)S_ROUND(dlp, sizeof (int));

	PRF_MCOUNT(27, valid_handle);
	if (dlp) {
		if ((dlp->dl_magic == DL_MAGIC) &&
		    (dlp->dl_cigam == DL_CIGAM) &&
		    (dlp->dl_refcnt != 0) &&
		    (dlp->dl_lmps.head != NULL) &&
		    (dlp->dl_lmps.tail != NULL))
			return (1);
	}
	return (0);
}

/*
 * Search for the specified symbol.  If the handle is RTLD_NEXT simply start
 * looking in the next link map from the callers.  Otherwise, verify that we've
 * been handed a valid handle and look in the shared object specified by that
 * handle and in all objects in the specified object's needed list.
 */
#pragma weak dlsym = _dlsym

static void *	__dlsym(void *, char *, unsigned int);

void *
_dlsym(void * handle, char * name)
{
	void *	error;
	int	bind;

	if ((bind = bind_guard(THR_FLG_BIND)) == 1)
		(void) rw_rdlock(&bindlock);
	error = __dlsym(handle, name, caller());
	dz_close();
	fm_cleanup(fmap);
	if (bind) {
		(void) rw_unlock(&bindlock);
		(void) bind_clear(THR_FLG_BIND);
	}
	return (error);
}

static void *
__dlsym(void * handle, char * name, unsigned int cpc)
{
	Rt_map *	lmp = 0, * _lmp;
	Sym *		sym;
	unsigned int	addr;

	PRF_MCOUNT(24, __dlsym);

	if (name == 0) {
		eprintf(ERR_FATAL, "dlsym: illegal symbol name");
		return (0);
	}

	/*
	 * Determine who called us.  If we can not figure out the calling link
	 * map assume it's "main".  This information is used by RTLD_NEXT and
	 * for the bindings diagnostic.
	 */
	for (_lmp = lml_main.lm_head; _lmp; _lmp = (Rt_map *)NEXT(_lmp)) {
		if ((cpc > ADDR(_lmp)) && (cpc < (ADDR(_lmp) + MSIZE(_lmp)))) {
			lmp = _lmp;
			break;
		}
	}
	if (lmp == 0)
		lmp = lml_main.lm_head;

	if (handle == RTLD_NEXT) {
		DBG_CALL(Dbg_syms_dlsym(NAME((Rt_map *)NEXT(lmp)), name, 1));

		/*
		 * Determine the permissions from the present link map, and
		 * start looking for symbols in the next link map.
		 */
		sym = LM_LOOKUP_SYM(lmp)(name, PERMIT(lmp), lmp,
		    (Rt_map *)NEXT(lmp), &_lmp, LKUP_DEFT);
	} else {
		Rt_map *	tlmp;
		Dl_obj *	dlp = (Dl_obj *)handle;
		Permit *	permit;

		/*
		 * Validate the handle provided.
		 */

		if (!valid_handle(dlp)) {
			eprintf(ERR_FATAL, "dlsym: invalid handle");
			return (0);
		}

		DBG_CALL(Dbg_syms_dlsym(NAME((Rt_map *)dlp->dl_lmps.head->data),
		    name, 0));

		/*
		 * If this symbol lookup is triggered from a dlopen(0) handle
		 * traverse the entire link-map list looking for promiscuous
		 * entries.  Otherwise traverse the dlp link map list.
		 */
		permit = dlp->dl_id;
		if (permit == 0) {
			for (tlmp = lml_main.lm_head; tlmp;
			    tlmp = (Rt_map *)NEXT(tlmp)) {
				if (!(FLAGS(tlmp) & FLG_RT_PROMISC))
					continue;
				if (sym = LM_LOOKUP_SYM(lmp)(name, permit, lmp,
				    tlmp, &_lmp, (LKUP_DEFT | LKUP_FIRST)))
					break;
			}
		} else
			sym = LM_DLSYM(lmp)(dlp, lmp, name, &_lmp);
	}

	if (!sym) {
		eprintf(ERR_FATAL, "dlsym: can't find symbol: %s", name);
		return (0);
	}
	addr = sym->st_value;
	if (!(FLAGS(_lmp) & FLG_RT_ISMAIN))
		addr += ADDR(_lmp);

	DBG_CALL(Dbg_bind_global(NAME(lmp), 0, 0, -1, NAME(_lmp),
	    (caddr_t)addr, (caddr_t)sym->st_value, name));

	return ((void *)addr);
}

/*
 * Close the shared object associated with handle
 * Returns 0 on success, 1 on failure.
 */
#pragma weak dlclose = _dlclose

static int 	__dlclose(void *);

int
_dlclose(void * handle)
{
	int 	error;
	int	bind;

	if ((bind = bind_guard(THR_FLG_BIND)) == 1)
		(void) rw_wrlock(&bindlock);
	error = __dlclose(handle);
	dz_close();
	if (bind) {
		(void) rw_unlock(&bindlock);
		(void) bind_clear(THR_FLG_BIND);
	}
	return (error);
}

static int
__dlclose(void * handle)
{
	Dl_obj *	dlp = (Dl_obj *)handle;
	Rt_map *	lmp;

	PRF_MCOUNT(25, __dlclose);

	if (!valid_handle(dlp)) {
		eprintf(ERR_FATAL, "dlclose: invalid handle");
		return (1);
	}

	/*
	 * Determine what link map we're going to be closing.
	 */
	lmp = (Rt_map *)dlp->dl_lmps.head->data;
	DBG_CALL(Dbg_file_dlclose(NAME(lmp)));

	/*
	 * Decrement reference count of this object.
	 */
	if (--(dlp->dl_refcnt) == 0) {
		DLP(lmp) = 0;
		dlp_delete(dlp);
		remove_so(&lml_main);
	}
	return (0);
}

/*
 * Loop through all objects on the dependent link map list and removed the
 * permission identifier associated with this dlopen().
 * their COUNT.  Free dlp, but save the permit id and indicate in the
 * effected link maps that we are deleting/changing their PERMIT field.
 * The _fini sections are called in cleanup() so we must leave the permit id
 * in the link maps until we return.  Called cleanup() to loop through
 * all the link maps and remove all the ones that are no longer in use.
 * Clear the permit id from effected link maps when cleanup() returns.
 */
void
dlp_delete(Dl_obj * dlp)
{
	Rt_map *	lmp;
	Listnode *	lnp, * olnp = 0;
	Permit *	permit = dlp->dl_id;

	PRF_MCOUNT(26, dlp_delete);

	/*
	 * Go through the dependency list and remove the permission identifier
	 * associated with this dlopen().  If the permit has gone to zero, then
	 * no-one is explicitly referencing this object, therefore decrement
	 * the count of any `bound-to' objects.
	 */
	for (LIST_TRAVERSE(&dlp->dl_lmps, lnp, lmp)) {
		PERMIT(lmp) = perm_unset(PERMIT(lmp), permit);
		DBG_CALL(Dbg_file_ref(NAME(lmp), COUNT(lmp), PERMIT(lmp),
		    (FLAGS(lmp) & FLG_RT_PROMISC)));
		if (!(PERMIT(lmp)))
			bound_delete(lmp);

		if (olnp)
			(void) free(olnp);
		olnp = lnp;
	}
	if (olnp)
		(void) free(olnp);

	(void) free(dlp);
	perm_free(permit);
}

/*
 * Return an information structure that reflects the symbol closest to the
 * address specified.
 */
#pragma weak dladdr = _dladdr

static int	__dladdr(void *, Dl_info *);

int
_dladdr(void * addr, Dl_info * dlip)
{
	int	error;
	int	bind;

	if ((bind = bind_guard(THR_FLG_BIND)) == 1)
		(void) rw_rdlock(&bindlock);
	error = __dladdr(addr, dlip);
	if (bind) {
		(void) rw_unlock(&bindlock);
		(void) bind_clear(THR_FLG_BIND);
	}
	return (error);
}

int
__dladdr(void * addr, Dl_info * dlip)
{
	Rt_map *	lmp, * tlmp;

	/*
	 * Scan the executables link map list to determine which image covers
	 * the required address.
	 */
	lmp = 0;
	for (tlmp = lml_main.lm_head; tlmp; tlmp = (Rt_map *)NEXT(tlmp)) {
		Rt_map *	rlmp;
		if (((unsigned long)addr >= ADDR(tlmp)) &&
		    ((unsigned long)addr <= (ADDR(tlmp) + MSIZE(tlmp)))) {
			lmp = tlmp;
			break;
		}

		/*
		 * If the object has a filter check there as well (this may
		 * be ld.so.1, which is not maintained on the executables link
		 * map list).
		 */
		if ((rlmp = REFLM(tlmp)) != 0) {
			if (((unsigned long)addr >= ADDR(rlmp)) &&
			    ((unsigned long)addr <=
			    (ADDR(rlmp) + MSIZE(rlmp)))) {
				lmp = rlmp;
				break;
			}
		}
	}
	if (lmp == 0) {
		eprintf(ERR_FATAL, "dladdr: address 0x%x does not fall "
		    "within any mapped object", addr);
		return (0);
	}

	/*
	 * Set up generic information and any defaults.
	 */
	dlip->dli_fname = NAME(lmp);
	dlip->dli_fbase = (void *)ADDR(lmp);
	dlip->dli_sname = 0;
	dlip->dli_saddr = 0;

	/*
	 * Determine the nearest symbol to this address.
	 */
	LM_DLADDR(lmp)((unsigned long)addr, lmp, dlip);
	return (1);
}
