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
#pragma ident	"@(#)analyze.c	1.31	95/07/28 SMI"

/*
 * If the environment flag LD_TRACE_LOADED_OBJECTS is set, we load
 * all objects, as above, print out the path name of each, and then exit.
 * If LD_WARN is also set, we also perform relocations, printing out a
 * diagnostic for any unresolved symbol.
 */
#include	<string.h>
#include	<stdio.h>
#include	<unistd.h>
#include	<sys/stat.h>
#include	<sys/mman.h>
#include	<dlfcn.h>
#include	<errno.h>
#include	"_rtld.h"
#include	"_elf.h"
#include	"profile.h"
#include	"debug.h"

extern char **	_envp;

static Fct *	vector[] = {
	&elf_fct,
#ifdef A_OUT
	&aout_fct,
#endif A_OUT
	0
};

/*
 * Search through the entire list of link maps looking for objects that can be
 * freed from the process' address space.  Any object added to the address space
 * is assigned a permission identifier:
 *
 *  o	the dynamic executable and its initial dependencies are assigned a
 *	unique permission from setup(), these will never be removed.
 *
 *  o	each unique dlopen() will also get a unique permission which will be
 *	masked into each of the dependencies of the dlopen().  The associated
 *	dlclose() will remove its unique mask.  Dependencies that are applicable
 *	to more than one dlopen will have multiple permission masks anded
 *	together.
 *
 * A mask of zero indicates that no references are being made to the object and
 * thus its .fini section can be called and the object can be deleted.
 */
void
remove_so(Lm_list * lml)
{
	Rt_map *	lmp, * tlmp;
	void (*		fptr)();
	void (*		rptr)() = (void (*)())(r_debug.r_brk);
	int		done = 1;

	PRF_MCOUNT(50, remove_so);

	/*
	 * Alert debuggers that link_map list is shrinking.
	 */
	r_debug.r_state = RT_DELETE;
	(* rptr)();

	/*
	 * Traverse the link map list looking for objects whoes permissions have
	 * dropped to zero.  i.e. they are presently unreferenced.
	 */
	while (done) {
		Rt_map *	nlmp;
		done = 0;
		/* LINTED */
		for (lmp = lml->lm_head; lmp; lmp = nlmp) {
			nlmp = (Rt_map *)NEXT(lmp);

			if (FLAGS(lmp) & FLG_RT_NODELETE)
				continue;
			if (PERMIT(lmp) || COUNT(lmp))
				continue;

			DBG_CALL(Dbg_file_delete(NAME(lmp)));

			/*
			 * Set the deleting flag and call any .fini section.
			 * The deleting flag insures that no .fini code can
			 * result in another object binding back to this object.
			 * As lookup_sym() always allows a caller to find
			 * symbols within itself, the .fini can still bind to
			 * functions in this object if necessary.
			 */
			FLAGS(lmp) |= FLG_RT_DELETING;
			if (FLAGS(lmp) & FLG_RT_INITDONE) {
				FLAGS(lmp) &= ~FLG_RT_INITDONE;
				if ((fptr = FINI(lmp)) != 0) {
					DBG_CALL(Dbg_util_call_fini(NAME(lmp)));
					(*fptr)();
				}
			}

			tlmp = REFLM(lmp);

#ifdef	PROF
			/*
			 * If a shared object was being profiled, disable
			 * profiling and clean up buffers.
			 */
			if (FLAGS(lmp) & FLG_RT_PROFILE)
				profile_close(lmp);
#endif
			/*
			 * Unlink link map from chain and unmap the object.
			 */
			NEXT((Rt_map *)PREV(lmp)) = (void *)nlmp;
			if (nlmp)
				PREV(nlmp) = PREV(lmp);
			else
				lml->lm_tail = (Rt_map *)PREV(lmp);

			LM_UNMAP_SO(lmp)(lmp);


			/*
			 * Now we can free up the Rt_map itself and all of it's
			 * structures.
			 */
			if (NAME(lmp))
				free(NAME(lmp));
			if (ELFPRV(lmp))
				free(ELFPRV(lmp));
			if ((FLAGS(lmp) & FLG_RT_FLTALLOC) && REFNAME(lmp)) {
				free(REFNAME(lmp));
			}
			free(lmp);

			/*
			 * If this link-map was acting as a filter reduce the
			 * dlp count and if zero remove the dlp.  As this may
			 * have caused more link maps to become unused, repeat
			 * the loop.
			 */
			if (tlmp) {
				Dl_obj *	dlp = DLP(tlmp);

				if (dlp && (--(dlp->dl_refcnt) == 0)) {
					DLP(lmp) = 0;
					dlp_delete(dlp);
					done = 1;
					break;
				}
			}
		}
	}

	/*
	 * Alert debuggers that link_map is consistent again.
	 */
	r_debug.r_state = RT_CONSISTENT;
	(* rptr)();
}

/*
 * Analyze a link map.  This routine is called at startup to continue the
 * processing of the main executable, or from a dlopen() to continue the
 * processing a newly opened shared object.
 *
 * If we've been called from startup we traverse the link map list adding to it
 * and new dependencies.  Thus the list grows as we traverse it.  Each object
 * is mapped and initialized with appropriate permission and flags information
 * for its relocation later.
 *
 * If we've been called from a dlopen() we simply want to analyze a single
 * link-map.  The dlopen() will generate a object list (`dlp') for each object
 * we analyze and thus we will recurse back to this routine until all the
 * dlopen() dependencies have been processed.
 */
int
analyze_so(Lm_list * lml, Rt_map * clmp, int mode, Dl_obj * dlp)
{
	Rt_map *	lmp;
	int		cont = 1;
	int		again = dlp ? 0 : 1;

	PRF_MCOUNT(51, analyze_so);

	/*
	 * Map in all shared objects needed by this link map.  Start with the
	 * needed section of the supplied link map, and then go through the
	 * needed sections of all objects just mapped, etc. this results in a
	 * breadth first ordering of all needed objects.  If this link map
	 * represents a relocatable object, then there is no need to search for
	 * any dependencies but the link editing of the object must be
	 * completed.
	 *
	 * If we have been called via dlopen() a `dlp' will be passed.  In this
	 * case we must always processed the needed list to insure that any
	 * permissions are updated accordingly and the associated object
	 * dependency list is created.  As the object list is created, so this
	 * routine will be called again to process each *individual* dependency.
	 */
	for (lmp = clmp; lmp && cont; lmp = (Rt_map *)NEXT(lmp), cont = again) {
		if ((FLAGS(lmp) & FLG_RT_ANALYZED) && again)
			continue;
		DBG_CALL(Dbg_file_analyze(NAME(lmp), mode));

		if (FLAGS(lmp) & FLG_RT_OBJECT) {
			if (!(elf_obj_fini(lml, lmp)))
				if (!tracing)
					return (0);
		} else {
			if (!(LM_LD_NEEDED(lmp)(lml, lmp, PERMIT(lmp), dlp)))
				if (!tracing)
					return (0);
		}
	}
	return (1);
}

/*
 * Relocate one or more objects that have just been analyzed.
 */
int
relocate_so(Rt_map * clmp, int mode)
{
	Rt_map *	lmp;

	PRF_MCOUNT(52, relocate_so);

	for (lmp = clmp; lmp; lmp = (Rt_map *)NEXT(lmp)) {
		if (FLAGS(lmp) & FLG_RT_ANALYZED)
			continue;
		if (!LM_RELOC(lmp)(lmp, (mode & ~RTLD_GLOBAL)))
			if (!tracing)
				return (0);
		FLAGS(lmp) |= FLG_RT_ANALYZED;
	}
	return (1);
}

/*
 * Determine the object type of a file.
 */
Fct *
are_u_this(const char * path)
{
	int	i;
	char *	maddr;

	PRF_MCOUNT(53, are_u_this);

	/*
	 * Map in the first page of the file.  Determine the memory size based
	 * on the larger of the filesize (obtained in load_so()) or the mapping
	 * size.  The mapping allows for execution as filter libraries may be
	 * able to use this initial mapping and require nothing else.
	 */
	if ((maddr = (char *)mmap(fmap->fm_maddr, fmap->fm_msize,
	    (PROT_READ | PROT_EXEC), fmap->fm_mflags, fmap->fm_fd, 0)) ==
	    (char *)-1) {
		eprintf(ERR_FATAL, Errmsg_cmfl, path, errno);
		return (0);
	}
	fmap->fm_maddr = maddr;

	/*
	 * From now on we will re-use fmap->fm_maddr as the mapping address
	 * so we augment the flags with MAP_FIXED.
	 */
	fmap->fm_mflags |= MAP_FIXED;

	/*
	 * Search through the object vectors to determine what kind of
	 * object we have.
	 */
	for (i = 0; vector[i]; i++) {
		if ((vector[i]->fct_are_u_this)())
			return (vector[i]);
	}

	/*
	 * Unknown file type - return error.
	 */
	eprintf(ERR_FATAL, Errmsg_unft, path);
	return (0);

}

static int
is_so_match(const char * old, const char * new, int slash)
{
	if (!slash) {
		const char *	_old = old;
		/*
		 * Find first character past last '/'.
		 */
		while (*_old)
			if (*_old++ == '/')
				old = _old;
	}
	return (strcmp(old, new));
}

/*
 * Function that determines whether a file name has already been loaded; if so,
 * returns a pointer to its link map structure; else returns a NULL pointer.
 */
Rt_map *
is_so_loaded(Lm_list * lml, const char * name)
{
	const char *	cp;
	int		slash = 0;
	Rt_map *	lmp;

	PRF_MCOUNT(54, is_so_loaded);

	/*
	 * Does the new name contain any '/'s ?
	 */
	for (cp = name; *cp; cp++) {
		if (*cp == '/') {
			slash++;
			break;
		}
	}

	/*
	 * If the name has any '/'s, compare it as is with the names stored
	 * in each link map; else compare it with the last component of each
	 * link map name.
	 */
	for (lmp = lml->lm_head; lmp; lmp = (Rt_map *)NEXT(lmp)) {
		if (FLAGS(lmp) &
		    (FLG_RT_ISMAIN | FLG_RT_OBJECT | FLG_RT_DELETING))
			continue;

		if (is_so_match(NAME(lmp), name, slash) == 0)
			return (lmp);
		if (ALIAS(lmp).head) {
			Listnode *	lnp;

			for (LIST_TRAVERSE(&ALIAS(lmp), lnp, cp)) {
				if (is_so_match(cp, name, slash) == 0)
					return (lmp);
			}
		}
	}
	return ((Rt_map *)0);
}

/*
 * This function loads the name files and returns a pointer to its link map.
 * It is assumed that the caller has already checked that the file is not
 * already loaded before calling this function (refer is_so_loaded()).
 * Find and open the file, map it into memory, add it to the end of the list
 * of link maps and return a pointer to the new link map.  Return 0 on error.
 */
Rt_map *
load_so(Lm_list * lml, const char * name, Rt_map * lmp)
{
	Fct *		ftp;		/* file class table ptr */
	char *		path;		/* full pathname of object */
	struct stat	status;
	int		done = 0;

	PRF_MCOUNT(55, load_so);

	/*
	 * If the file is the run time linker then it's already loaded.
	 */
	if (strcmp(name, NAME(lml_rtld.lm_head)) == 0)
		return (lml_rtld.lm_head);

	/*
	 * Find and open file.
	 */
	if ((fmap->fm_fd = so_find(name, lmp, (const char **)&path)) != -1) {
		(void) fstat(fmap->fm_fd, &status);
		fmap->fm_fsize = status.st_size;
		fmap->fm_stdev = status.st_dev;
		fmap->fm_stino = status.st_ino;

		/*
		 * Determine from the new files status information if this file
		 * is actually linked to one we already have mapped.
		 */
		for (lmp = lml->lm_head; lmp; lmp = (Rt_map *)NEXT(lmp)) {
			if ((lmp->rt_stdev != fmap->fm_stdev) ||
			    (lmp->rt_stino != fmap->fm_stino))
				continue;

			DBG_CALL(Dbg_file_skip(path, NAME(lmp)));
			if (list_append(&ALIAS(lmp), path) == 0)
				return (0);
			done++;
			break;
		}
	}

	/*
	 * Tracing is enabled by the LD_TRACE_LOADED_OPTIONS environment
	 * variable which is normally set from ldd(1).  For each link map we
	 * load, print the load name and the full pathname of the shared object.
	 * Loaded objects are skipped until tracing is 1 (ldd(1) uses higher
	 * values to skip preloaded shared libraries).
	 */
	if (tracing) {
		if (tracing != 1)
			tracing--;
		else if (!done) {
			/*
			 * An erroneous file descriptor indicates the file
			 * could not be found.
			 */
			if (fmap->fm_fd == -1) {
				if ((FLAGS(lmp) & FLG_RT_AUX) == 0) {
					(void) printf("\t%s =>\t (not found)\n",
							name);
				}
				return (0);
			}
			/*
			 * If the load name isn't a full pathname print its
			 * associated pathname.
			 */
			else if (*name == '/')
				(void) printf(Lddmsg_lflp, name);
			else
				(void) printf(Lddmsg_lequ, name, path);
		}
	}

	/*
	 * Having completed any tracing output, if we were not able to find
	 * and open the file return with an error.
	 */
	if (fmap->fm_fd == -1) {
		if ((FLAGS(lmp) & FLG_RT_AUX) == 0)
			eprintf(ERR_FATAL, Errmsg_cofl, name, errno);
		return (0);
	}

	/*
	 * Find out what type of object we have and map it in.  If the link-map
	 * is created save the dev/inode information for later comparisons.
	 */
	if (!done) {
		/*
		 * Initialize the lmp to 0 to insure the correct error return
		 * should are_u_this() fail.
		 */
		lmp = 0;

		if (((ftp = are_u_this(path)) != 0) &&
		    ((lmp = (ftp->fct_map_so)(lml, path, name)) != 0)) {
			lmp->rt_stdev = fmap->fm_stdev;
			lmp->rt_stino = fmap->fm_stino;
		}
	}
	(void) close(fmap->fm_fd);
	fmap->fm_fd = 0;

	return (lmp);
}

/*
 * Symbol lookup routine.  Takes an ELF symbol name, and a list of link maps to
 * search (if the flag indicates LKUP_FIRST only the first link map of the list
 * is searched ie. we've been called from dlsym()).
 * If successful, return a pointer to the symbol table entry and a pointer to
 * the link map of the enclosing object.  Else return a null pointer.
 *
 * To improve elf performance, we first compute the elf hash value and pass
 * it to each find_sym() routine.  The elf function will use this value to
 * locate the symbol, the a.out function will simply ignore it.
 */
Sym *
lookup_sym(const char * name, Permit * permit, Rt_map * clmp, Rt_map * ilmp,
	Rt_map ** dlmp, int flag)
{
	Sym *		sym;
	Rt_map *	lmp;
	unsigned long	hash;

	PRF_MCOUNT(56, lookup_sym);
	hash = elf_hash(name);

	/*
	 * Search the initial link map for the required symbol (this category is
	 * selected by dlsym(), where individual link maps are searched for a
	 * required symbol.  Therefore, we know we have permission to look at
	 * the link map).
	 */
	if (flag & LKUP_FIRST)
		return (SYMINTP(ilmp)(name, ilmp, dlmp, flag, hash));

	/*
	 * Examine the list of link maps, skipping any whose symbols are denied
	 * to this caller.  Note that we are always allowed to find symbols in
	 * the callers link map (this may be the case when we are in the process
	 * of deleting a link map but are executing its fini section).
	 */
	for (lmp = ilmp; lmp; lmp = (Rt_map *)NEXT(lmp)) {
		if ((lmp == clmp) || (!(FLAGS(lmp) & FLG_RT_DELETING) &&
		    ((FLAGS(lmp) & FLG_RT_PROMISC) ||
		    (perm_test(PERMIT(lmp), permit))))) {
			if (sym = SYMINTP(lmp)(name, lmp, dlmp, flag, hash))
				return (sym);
		}
	}
	return ((Sym *)0);
}

/*
 * Each link map maintains a list of other link maps it has bound to to resolve
 * symbols required for its own relocation.  Each of the link maps to which we
 * have bound maintain a reference count that indicates how many link maps are
 * making reference to symbols within it.  Only when this reference count is
 * zero can an object be deleted from the process address space.
 *
 * If the defining shared object's link map is not already in the callers link
 * map's `bound-to' list, then add it and bump the COUNT of the defining shared
 * object.
 */
static int
_bound_add(Rt_map * rlmp, Rt_map * dlmp)
{
	Rt_map *	tlmp;
	Listnode *	lnp;

	PRF_MCOUNT(57, bound_add);

	/*
	 * Determine if the object which holds the symbol definition is already
	 * on the `bound-to' list of the referencing object.
	 */
	for (LIST_TRAVERSE(&BOUNDTO(rlmp), lnp, tlmp))
		if (dlmp == tlmp)
			return (1);

	/*
	 * If the link map which defines the symbol is not already on the
	 * `bound-to' list bump it's COUNT.
	 */
	COUNT(dlmp)++;
	DBG_CALL(Dbg_file_bound(NAME(rlmp), NAME(dlmp), COUNT(dlmp)));

	/*
	 * Add this defining link map to the `bound-to' list of the referencing
	 * (caller's) link map.
	 */
	if (list_append(&BOUNDTO(rlmp), dlmp) == 0)
		return (0);
	else
		return (1);
}

int
bound_add(Rt_map * rlmp, Rt_map * dlmp)
{
	int bind = 0;
	int rc;

	if ((lc_version > 0) &&
	    (bind = bind_guard(THR_FLG_BOUND)))
		(void) rw_wrlock(&boundlock);

	rc = _bound_add(rlmp, dlmp);

	if (bind) {
		(void) rw_unlock(&boundlock);
		(void) bind_clear(THR_FLG_BOUND);
	}
	return (rc);
}


/*
 * Once a link maps permission identifier has returned to zero, no-one requires
 * to use it anymore.  Traverse its `bound-to' list decrementing any COUNT's of
 * any objects we have resolved symbols from.  Note that this objects COUNT may
 * be non-zero, as other users may remain bound to use.  Until our COUNT drops
 * to zero we will not be deleted.
 */
static void
_bound_delete(Rt_map * rlmp)
{
	Listnode *	lnp, * olnp = 0;
	Rt_map *	dlmp;

	PRF_MCOUNT(58, bound_delete);

	for (LIST_TRAVERSE(&BOUNDTO(rlmp), lnp, dlmp)) {
		COUNT(dlmp)--;
		DBG_CALL(Dbg_file_ref(NAME(dlmp), COUNT(dlmp), PERMIT(dlmp),
		    (FLAGS(dlmp) & FLG_RT_PROMISC)));

		if (olnp)
			(void) free(olnp);
		olnp = lnp;
	}
	if (olnp)
		(void) free(olnp);

	BOUNDTO(rlmp).head = (Listnode *)0;
	BOUNDTO(rlmp).tail = (Listnode *)0;

}

void
bound_delete(Rt_map * rlmp)
{
	int bind = 0;

	if ((lc_version > 0) &&
	    (bind = bind_guard(THR_FLG_BOUND)))
		(void) rw_wrlock(&boundlock);

	_bound_delete(rlmp);

	if (bind) {
		(void) rw_unlock(&boundlock);
		(void) bind_clear(THR_FLG_BOUND);
	}
}
