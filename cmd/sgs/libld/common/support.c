/*
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)support.c	1.5	95/06/20 SMI"

#include	<stdio.h>
#include	<dlfcn.h>
#include	<libelf.h>
#include	<link.h>
#include	"debug.h"
#include	"_libld.h"

/* LINTLIBRARY */

/*
 * Table which defines the default functions to be called bythe library
 * SUPPORT (-S <libname>).  These functions can be redefined by the
 * ld_support_loadso() routine.
 */
static Support_list support[] = {
	"ld_start",	{ 0, 0 },		/* LD_START */
	"ld_atexit",	{ 0, 0 },		/* LD_ATEXIT */
	"ld_file",	{ 0, 0 },		/* LD_FILE */
	"ld_section",	{ 0, 0 },		/* LD_SECTION */
	NULL,		{ 0, 0 },
};

/*
 * Loads in a support shared object specified using the SGS_SUPPORT environment
 * variable or the -S ld option, and determines which interface functions are
 * provided by that object.
 *
 * return values for ld_support_loadso:
 *	0 -	unable to open shared object
 *	1 -	shared object loaded sucessfully
 *	S_ERROR - aww, damn!
 */
int
ld_support_loadso(const char * obj)
{
	void *		handle;
	void (*		fptr)();
	Func_list *	flp;
	int 		i;

	/*
	 * Load the required support library.  If we are unable to load it fail
	 * silently. (this might be changed in the future).
	 */
	if ((handle = dlopen(obj, RTLD_LAZY)) == NULL)
		return (0);

	for (i = 0; support[i].sup_name; i++) {
		if ((fptr = (void (*)())dlsym(handle, support[i].sup_name))) {

			flp = (Func_list *)malloc(sizeof (Func_list));
			if (flp == NULL)
				return (S_ERROR);

			flp->fl_obj = obj;
			flp->fl_fptr = fptr;
			if (list_append(&support[i].sup_funcs, flp) == 0)
				return (S_ERROR);

			DBG_CALL(Dbg_support_load(obj, support[i].sup_name));
		}
	}
	return (1);
}


/*
 * Wrapper routines for the ld support library calls.
 */
void
ld_start(const char * ofile, const Half etype, const char * caller)
{
	Func_list *	flp;
	Listnode *	lnp;

	for (LIST_TRAVERSE(&support[LD_START].sup_funcs, lnp, flp)) {
		DBG_CALL(Dbg_support_action(flp->fl_obj,
		    support[LD_START].sup_name, DBG_SUP_START, ofile));
		(*flp->fl_fptr)(ofile, etype, caller);
	}
}

void
ld_atexit(int exit_code)
{
	Func_list *	flp;
	Listnode *	lnp;

	for (LIST_TRAVERSE(&support[LD_ATEXIT].sup_funcs, lnp, flp)) {
		DBG_CALL(Dbg_support_action(flp->fl_obj,
		    support[LD_ATEXIT].sup_name, DBG_SUP_ATEXIT, 0));
		(*flp->fl_fptr)(exit_code);
	}
}

void
ld_file(const char * ifile, const Elf_Kind ekind, int flags, Elf * elf)
{
	Func_list *	flp;
	Listnode *	lnp;

	for (LIST_TRAVERSE(&support[LD_FILE].sup_funcs, lnp, flp)) {
		int	_flags = 0;

		if (!(flags & FLG_IF_CMDLINE))
			_flags |= LD_SUP_DERIVED;
		if (!(flags & FLG_IF_NEEDED))
			_flags |= LD_SUP_INHERITED;
		if (flags & FLG_IF_EXTRACT)
			_flags |= LD_SUP_EXTRACTED;

		DBG_CALL(Dbg_support_action(flp->fl_obj,
		    support[LD_FILE].sup_name, DBG_SUP_FILE, ifile));
		(*flp->fl_fptr)(ifile, ekind, _flags, elf);
	}
}

void
ld_section(const char * scn, Elf32_Shdr * shdr, Elf32_Word ndx,
    Elf_Data * data, Elf * elf)
{
	Func_list *	flp;
	Listnode *	lnp;

	for (LIST_TRAVERSE(&support[LD_SECTION].sup_funcs, lnp, flp)) {
		DBG_CALL(Dbg_support_action(flp->fl_obj,
		    support[LD_SECTION].sup_name, DBG_SUP_SECTION, scn));
		(*flp->fl_fptr)(scn, shdr, ndx, data, elf);
	}
}
