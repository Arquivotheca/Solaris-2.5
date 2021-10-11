/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)main.c	1.35	95/06/20 SMI"

/*
 * ld -- link/editor main program
 */
#include	<string.h>
#include	<unistd.h>
#include	<sys/mman.h>
#include	<errno.h>
#include	"paths.h"
#include	"_ld.h"
#include	"debug.h"

static const char
	* Errmsg_flgp = "Flags processing errors",
	* Errmsg_filp = "File processing errors.  No output written to %s",
	* Errmsg_liod = "libelf is out of date",
	* Errmsg_srec = "Symbol referencing errors",
	* Errmsg_sren = "Symbol referencing errors. No output written to %s";

static const char
	* Ldstabso = "libldstab.so.1";

static Ofl_desc Ofl;			/* Make sure ofl is zero */

/*
 * The main program
 */
void
main(int argc, char ** argv)
{
	char *		ld_options;	/* LD_OPTIONS environment string */
	char *		sgs_support;	/* SGS_SUPPORT environment string */
	Ofl_desc *	ofl = &Ofl;	/* Output file descriptor */
	Half		etype;
	int		suplib = 0;


	if (init(ofl) == S_ERROR)
		exit(EXIT_FAILURE);
	ofl->ofl_libver = EV_CURRENT;
	ofl->ofl_e_machine = M_MACH;
	ofl->ofl_e_flags = 0;
	if (elf_version(EV_CURRENT) == EV_NONE) {
		eprintf(ERR_ELF, Errmsg_liod);
		exit(EXIT_FAILURE);
	}

	/*
	 * Initialize the output file descriptor with some defaults, namely the
	 * symbol cache and its size.  Also assign a default segment alignment
	 * in preparation for building the segment descriptor list.
	 */
	if ((ofl->ofl_symbkt = (Sym_cache **)calloc(sizeof (Sym_cache *),
	    SYM_NBKTS)) == 0)
		exit(EXIT_FAILURE);
	ofl->ofl_symbktcnt = SYM_NBKTS;
	ofl->ofl_segalign = M_SEGM_ALIGN;

	/*
	 * Check the LD_OPTIONS environment variable, and if present prepend
	 * the arguments specified to the command line argument list.
	 */
	if ((ld_options = getenv("LD_OPTIONS")) != NULL)
		if (prepend_argv(ld_options, &argc, &argv) == S_ERROR)
			exit(EXIT_FAILURE);

	/*
	 * Argument pass one.  Get all the input flags (skip any files) and
	 * check for consistency.  After this point any map file processing
	 * would have been completed and the entrance criteria and segment
	 * descriptor lists will be complete.
	 */
	if (process_flags(ofl, argc, argv) == S_ERROR)
		exit(EXIT_FAILURE);
	if (ofl->ofl_flags & FLG_OF_FATAL) {
		eprintf(ERR_FATAL, Errmsg_flgp);
		exit(EXIT_FAILURE);
	}

	/*
	 * Determine whether any support libraries been loaded (either through
	 * the SGS_SUPPORT environment variable and/or through the -S option).
	 * By default the support library libldstab.so.1 is loaded provided the
	 * user hasn't specified their own -S libraries.
	 */
	if (((sgs_support = getenv("SGS_SUPPORT")) != NULL) &&
	    (*sgs_support != '\0')) {
		const char *	sep = (const char *)":";
		char *		lib;

		DBG_CALL(Dbg_support_req(sgs_support, DBG_SUP_ENVIRON));
		if ((lib = strtok(sgs_support, sep)) != NULL) {
			do {
				if (strcmp(lib, Ldstabso) == 0) {
					if (suplib++)
						continue;
				}
				if (ld_support_loadso(lib) == S_ERROR)
					ldexit();

			} while ((lib = strtok(NULL, sep)) != NULL);
		}
	}
	if (lib_support.head) {
		Listnode *	lnp;
		char *		lib;

		for (LIST_TRAVERSE(&lib_support, lnp, lib)) {
			DBG_CALL(Dbg_support_req(lib, DBG_SUP_CMDLINE));
			if (ld_support_loadso(lib) == S_ERROR)
				ldexit();
		}
	} else {
		if (suplib == 0) {
			DBG_CALL(Dbg_support_req(Ldstabso, DBG_SUP_DEFAULT));
			if (ld_support_loadso(Ldstabso) == S_ERROR)
				ldexit();
		}
	}

	DBG_CALL(Dbg_ent_print(&ofl->ofl_ents,
		(ofl->ofl_flags & FLG_OF_DYNAMIC)));
	DBG_CALL(Dbg_seg_list(&ofl->ofl_segs));

	/*
	 * Determine whether we can create the file before going any further.
	 */
	if (open_outfile(ofl) == S_ERROR)
		ldexit();

	/*
	 * If the user didn't supply a library path supply a default.  And, if
	 * no run-path has been specified (-R), see if the environment variable
	 * is in use (historic).  Also assign a default starting address.
	 */
	if (Plibpath == NULL)
		Plibpath = LIBPATH;
	if (ofl->ofl_rpath == NULL) {
		const char *	rpath;
		if (((rpath = getenv("LD_RUN_PATH")) != NULL) &&
		    (strcmp(rpath, "")))
			ofl->ofl_rpath = rpath;
	}
	if (ofl->ofl_flags & FLG_OF_EXEC)
		ofl->ofl_segorigin = M_SEGM_ORIGIN;


	/*
	 * Argument pass two.  Input all libraries and objects.
	 */
	if (lib_setup(ofl) == S_ERROR)
		ldexit();

	/*
	 * Call ld_start() with the etype of our output file and the
	 * output file name.
	 */
	if (ofl->ofl_flags & FLG_OF_SHAROBJ)
		etype = ET_DYN;
	else if (ofl->ofl_flags & FLG_OF_RELOBJ)
		etype = ET_REL;
	else
		etype = ET_EXEC;

	ld_start(ofl->ofl_name, etype, argv[0]);

	/*
	 * Process all input files.
	 */
	if (process_files(ofl, argc, argv) == S_ERROR)
		ldexit();
	if (ofl->ofl_flags & FLG_OF_FATAL) {
		eprintf(ERR_FATAL, Errmsg_filp, ofl->ofl_name);
		ldexit();
	}

	/*
	 * Before validating all symbols count the number of relocation entries.
	 * If copy relocations exist, COMMON symbols must be generated which are
	 * assigned to the executables .bss.  During sym_validate() the actual
	 * size and alignment of the .bss is calculated.  Doing things in this
	 * order reduces the number of symbol table traversals required (however
	 * it does take a little longer for the user to be told of any undefined
	 * symbol errors).
	 */
	if (reloc_init(ofl) == S_ERROR)
		ldexit();

	/*
	 * Now that all symbol processing is complete see if any undefined
	 * references still remain.  If we observed undefined symbols the
	 * FLG_OF_FATAL bit will be set:  If creating a static executable, or a
	 * dynamic executable or shared object with the -zdefs flag set, this
	 * condition is fatal.  If creating a shared object with the -Bsymbolic
	 * flag set, this condition is simply a warning.
	 */
	if (sym_validate(ofl) == S_ERROR)
		ldexit();
	if (ofl->ofl_flags & FLG_OF_FATAL) {
		eprintf(ERR_FATAL, Errmsg_sren, ofl->ofl_name);
		ldexit();
	} else if (ofl->ofl_flags & FLG_OF_WARN)
		eprintf(ERR_WARNING, Errmsg_srec);

	/*
	 * Generate any necessary sections.
	 */
	if (make_sections(ofl) == S_ERROR)
		ldexit();

	/*
	 * Now that all sections have been added to the output file, check to
	 * see if any section ordering was specified and if so give a warning
	 * if any ordering directives were not matched.
	 */
	map_segorder_check(ofl);

	/*
	 * Having collected all the input data create the initial output file
	 * image, assign virtual addresses to the image, and generate a load
	 * map if the user requested one.
	 */
	if (create_outfile(ofl) == S_ERROR)
		ldexit();

	if (update_outfile(ofl) == S_ERROR)
		ldexit();
	if (ofl->ofl_flags & FLG_OF_GENMAP)
		ldmap_out(ofl);


	/*
	 * Build relocation sections and perform any relocation updates.
	 */
	if (reloc_process(ofl) == S_ERROR)
		ldexit();

	/*
	 * We're done, so make sure the updates are flushed to the output file.
	 */
	if (ofl->ofl_flags & FLG_OF_OUTMMAP) {
		if (msync(ofl->ofl_image, ofl->ofl_size, MS_SYNC) != 0) {
			eprintf(ERR_FATAL, Errmsg_file, ofl->ofl_name,
				(const char *)"msync", errno);
			ldexit();
		}
	} else {
		if (lseek(ofl->ofl_fd, 0L, 0) != 0) {
			eprintf(ERR_FATAL, Errmsg_file, ofl->ofl_name,
				(const char *)"lseek", errno);
			ldexit();
		}
		if (write(ofl->ofl_fd, ofl->ofl_image, ofl->ofl_size) !=
		    ofl->ofl_size) {
			eprintf(ERR_FATAL, Errmsg_file, ofl->ofl_name,
				(const char *)"write", errno);
			ldexit();
		}
	}

	ld_atexit(EXIT_SUCCESS);
	exit(EXIT_SUCCESS);
}
