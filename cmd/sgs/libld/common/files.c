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
#pragma ident	"@(#)files.c	1.42	95/06/15 SMI"

/* LINTLIBRARY */

/*
 * Processing of relocatable objects and shared objects.
 */
#include	<stdio.h>
#include	<string.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<link.h>
#include	<limits.h>
#include	<sys/stat.h>
#include	"debug.h"
#include	"_libld.h"

/*
 * Local Variables
 */
static const char
	* Errmsg_amif = "file %s: attempted multiple inclusion of file",
	* Errmsg_amil = "file %s: linked to %s: attempted multiple inclusion "
			"of file",
	* Errmsg_elvh = "file %s: creator ELF library version is higher "
			"than the version known to ld",
	* Errmsg_estr =	"file %s: elf_strptr",
	* Errmsg_isos = "input of shared object `%s' in static mode",
	* Errmsg_ivst = "file %s: section %s has invalid type %s",
	* Errmsg_rfnf = "file %s: required by %s, not found",
	* Errmsg_mfst = "file %s: section %s: malformed string table, "
			"initial or final byte",
	* Errmsg_ptol = "`%s/%s' pathname too long",
	* Errmsg_rnmh = "file %s: recording name `%s' matches that supplied "
			"with -h option",
	* Errmsg_rnmf = "file %s: recording name `%s' matches that provided "
			"by file %s",
	* Errmsg_wmbo = "file %s: wrong machine byte order",
	* Errmsg_wmcl = "file %s: wrong machine class",
	* Errmsg_wmty = "file %s: wrong machine type";


/*
 * Check sanity of file header and allocate an infile descriptor
 * for the file being processed.
 */
static Ifl_desc *
ifl_setup(const char * name, Ehdr * ehdr, Half flags, Ofl_desc * ofl)
{
	Ifl_desc *	ifl;
	List *		list;

	/*
	 * check the validity of the elf header information for
	 * compatibility with this machine and our own internal elf library.
	 */
	if (ehdr->e_machine != M_MACH) {
		if ((ehdr->e_machine == M_MACHPLUS) &&
		    (ehdr->e_flags & M_FLAGSPLUS)) {
			ofl->ofl_e_machine = M_MACHPLUS;
		} else {
			eprintf(ERR_FATAL, Errmsg_wmty, name);
			ofl->ofl_flags |= FLG_OF_FATAL;
			return (0);
		}
	}

	ofl->ofl_e_flags |= ehdr->e_flags;

	if (ehdr->e_version > ofl->ofl_libver) {
		eprintf(ERR_FATAL, Errmsg_elvh, name);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}
	if (ehdr->e_ident[EI_CLASS] != M_CLASS) {
		eprintf(ERR_FATAL, Errmsg_wmcl, name);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}
	if (ehdr->e_ident[EI_DATA] != M_DATA) {
		eprintf(ERR_FATAL, Errmsg_wmbo, name);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}

	if ((ifl = (Ifl_desc *)calloc(sizeof (Ifl_desc), 1)) == 0)
		return ((Ifl_desc *)S_ERROR);
	ifl->ifl_name = name;
	ifl->ifl_ehdr = ehdr;
	ifl->ifl_flags = flags;
	if ((ifl->ifl_isdesc = (Is_desc **)calloc(sizeof (Is_desc *),
	    ehdr->e_shnum)) == 0)
		return ((Ifl_desc *)S_ERROR);

	/*
	 * Record this new input file on the shared object or relocatable
	 * object input file list.
	 */
	if (ifl->ifl_ehdr->e_type == ET_DYN)
		list = &ofl->ofl_sos;
	else
		list = &ofl->ofl_objs;

	if (list_append(list, ifl) == 0)
		return ((Ifl_desc *)S_ERROR);
	else
		return (ifl);
}

/*
 * Process a generic section.  The appropriate section information is added
 * to the files input descriptor list.
 */
static int
/* ARGSUSED6 */
process_section(const char * name, Ifl_desc * ifl, Shdr * shdr,
	Elf_Scn *scn, Word ndx, int ident, Ofl_desc * ofl)
{
	Is_desc *	isp;

	/*
	 * Creat a new input section descriptor.  If this is a NOBITS
	 * section elf_getdata() will still create a data buffer (the buffer
	 * will be null and the size will reflect the actual memory size).
	 */
	if ((isp = (Is_desc *)calloc(sizeof (Is_desc), 1)) == 0)
		return (S_ERROR);
	isp->is_shdr = shdr;
	isp->is_file = ifl;
	isp->is_name = name;
	if ((isp->is_indata = elf_getdata(scn, NULL)) == NULL) {
		eprintf(ERR_ELF, Errmsg_egdt, ifl->ifl_name);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}

	/*
	 * Add the new input section to the files input section list and
	 * to the output section list (some sections like .strtab and
	 * .shstrtab are not added to the output section list).
	 */
	ifl->ifl_isdesc[ndx] = isp;
	if (ident)
		return ((int)place_section(ofl, isp, ident));
	else
		return (1);
}

/*
 * Simply process the section so that we have pointers to the data for use
 * in later routines, however don't add the section to the output section
 * list as we will be creating our own replacement sections later (ie.
 * symtab and relocation).
 */
static int
/* ARGSUSED5 */
process_input(const char * name, Ifl_desc * ifl, Shdr * shdr, Elf_Scn *scn,
	Word ndx, int ident, Ofl_desc * ofl)
{
	return (process_section(name, ifl, shdr, scn, ndx, M_ID_NULL, ofl));
}


/*
 * Process a string table section.  A valid section contains an initial and
 * final null byte.
 */
static int
process_strtab(const char * name, Ifl_desc * ifl, Shdr * shdr, Elf_Scn *scn,
	Word ndx, int ident, Ofl_desc * ofl)
{
	char *		data;
	size_t		size;
	Is_desc *	isp;
	int		error;

	/*
	 * Never include .stab.excl sections in any output file.
	 * If the -s flag has been specified strip any .stab sections.
	 */
	if (((ofl->ofl_flags & FLG_OF_STRIP) && ident &&
	    (strncmp(name, ".stab", 5) == 0)) ||
	    (strcmp(name, ".stab.exclstr") == 0) && ident)
		return (1);

	/*
	 * If we got here to process a .shstrtab or .dynstr table, `ident' will
	 * be null.  Otherwise make sure we don't have a .strtab section as this
	 * should not be added to the output section list either.
	 */
	if ((ident != M_ID_NULL) && (strcmp(name, ".strtab") == 0))
		ident = M_ID_NULL;

	error = process_section(name, ifl, shdr, scn, ndx, ident, ofl);
	if ((error == 0) || (error == S_ERROR))
		return (error);

	/*
	 * String tables should start and end with a NULL byte.  Note, it
	 * has been known that empty string tables can be produced by the
	 * assembler, so check the size before attempting to verify the
	 * data itself.
	 */
	isp = ifl->ifl_isdesc[ndx];
	size = isp->is_indata->d_size;
	if (size) {
		data = isp->is_indata->d_buf;
		if (data[0] != '\0' || data[size - 1] != '\0')
			eprintf(ERR_WARNING, Errmsg_mfst, ifl->ifl_name, name);
	}
	return (1);
}

/*
 * Invalid sections produce a warning and are skipped.
 */
static int
/* ARGSUSED3 */
invalid_section(const char * name, Ifl_desc * ifl, Shdr * shdr,
	Elf_Scn *scn, Word ndx, int ident, Ofl_desc * ofl)
{
	eprintf(ERR_WARNING, Errmsg_ivst, ifl->ifl_name, name,
		conv_sectyp_str(shdr->sh_type));
	return (1);
}

/*
 * Process a progbits section.
 */
static int
process_progbits(const char * name, Ifl_desc * ifl, Shdr * shdr,
	Elf_Scn * scn, Word ndx, int ident, Ofl_desc * ofl)
{
	int stab_index = 0;

	/*
	 * Never include .stab.excl sections in any output file.
	 * If the -s flag has been specified strip any .stab sections.
	 */
	if (ident && (strncmp(name, ".stab", 5) == 0)) {
		if ((ofl->ofl_flags & FLG_OF_STRIP) ||
		    (strcmp((name + 5), ".excl") == 0))
			return (1);

		if (strcmp((name + 5), ".index") == 0)
			stab_index = 1;
	}

	if ((ofl->ofl_flags & FLG_OF_STRIP) && ident) {
		if ((strcmp(name, ".debug") == 0) ||
		    (strcmp(name, ".line") == 0))
			return (1);
	}

	/*
	 * Update the ident to reflect the type of section we've got.
	 *
	 * If there is any .plt or .got section to generate we'll be creating
	 * our own version, so don't allow any input sections of these types to
	 * be added to the output section list (why a relocatable object would
	 * have a .plt or .got is a mystery, but stranger things have occurred).
	 */
	if (ident) {
		if (shdr->sh_flags == (SHF_ALLOC | SHF_EXECINSTR))
			ident = M_ID_TEXT;
		else if (shdr->sh_flags & SHF_ALLOC) {
			if ((strcmp(name, ".plt") == 0) ||
			    (strcmp(name, ".got") == 0))
				ident = M_ID_NULL;
			else if (stab_index) {
				/*
				 * This is a work-around for the
				 * problem that x86 sets the SHF_ALLOC
				 * flag for the .stab.index section.
				 *
				 * Because of this we want to make
				 * sure that the .stab.index does not
				 * end up as the last section in the
				 * text segment.  This is because some
				 * older linkers will give segmentation
				 * violations when they do a strip
				 * link (ld -s) against a shared object
				 * whose last section in the text segment
				 * is a .stab.
				 */
				ident = M_ID_INTERP;
			} else
				ident = M_ID_DATA;
		} else
			ident = M_ID_NOTE;
	}
	return (process_section(name, ifl, shdr, scn, ndx, ident, ofl));
}

/*
 * Process a nobits section.
 */
static int
process_nobits(const char * name, Ifl_desc * ifl, Shdr * shdr,
	Elf_Scn *scn, Word ndx, int ident, Ofl_desc * ofl)
{
	if (ident)
		ident = M_ID_BSS;
	return (process_section(name, ifl, shdr, scn, ndx, ident, ofl));
}

/*
 * Process a dynamic section.  If we are processing an explicit shared object
 * then we need to determine if it has a recorded SONAME, if so, this name will
 * be recorded in the output file being generated as the NEEDED entry rather
 * than the shared objects filename itself.
 * If the mode of the link-edit indicates that no undefined symbols should
 * remain, then we also need to build up a list of any additional shared object
 * dependencies this object may have.  In this case save any NEEDED entries
 * together with any associated run-path specifications.  This information is
 * recorded on the `ofl_soneed' list and will be analyzed after all explicit
 * file processing has been completed (refer finish_libs()).
 */
static int
process_dynamic(Is_desc * isc, Ifl_desc * ifl, Ofl_desc * ofl)
{
	Dyn *		data, * dyn;
	char *		str, * rpath = NULL;
	const char *	soname;
	const char *	needed;
	Sdf_desc *	sdf;
	Listnode *	lnp;

	data = (Dyn *)isc->is_indata->d_buf;
	str = (char *)ifl->ifl_isdesc[isc->is_shdr->sh_link]->is_indata->d_buf;

	/*
	 * First loop through the dynamic section looking for a run path.
	 */
	if (ofl->ofl_flags & (FLG_OF_NOUNDEF | FLG_OF_SYMBOLIC)) {
		for (dyn = data; dyn->d_tag != DT_NULL; dyn++) {
			if (dyn->d_tag != DT_RPATH)
				continue;
			if ((rpath = str + (size_t)dyn->d_un.d_val) == NULL)
				continue;
			break;
		}
	}

	/*
	 * Now look for any needed dependencies (which may use the rpath)
	 * or a new SONAME.
	 */
	for (dyn = data; dyn->d_tag != DT_NULL; dyn++) {
		if (dyn->d_tag == DT_SONAME) {
			if ((soname = str + (size_t)dyn->d_un.d_val) == NULL)
				continue;

			/*
			 * Update the input file structure with this new
			 * name.
			 */
			ifl->ifl_soname = soname;

		} else if (dyn->d_tag == DT_NEEDED) {
			if (!(ofl->ofl_flags &
			    (FLG_OF_NOUNDEF | FLG_OF_SYMBOLIC)))
				continue;
			if ((needed = str + (size_t)dyn->d_un.d_val) == NULL)
				continue;

			/*
			 * Determine if this needed entry is already recorded on
			 * the shared object needed list, if not create a new
			 * definition for later processing (see finish_libs()).
			 */
			if ((sdf = sdf_find(needed, &ofl->ofl_soneed)) == 0) {
				if ((sdf = sdf_desc(needed,
				    &ofl->ofl_soneed)) == (Sdf_desc *)S_ERROR)
					return (S_ERROR);
				sdf->sdf_rfile = ifl->ifl_name;
			}

			/*
			 * Record the runpath (Note that we take the first
			 * runpath which is exactly what ld.so.1 would do during
			 * its dependency processing).
			 */
			if (rpath && (sdf->sdf_rpath == 0))
				sdf->sdf_rpath = rpath;
		}
	}

	/*
	 * Perform some SONAME sanity checks.
	 */
	if (ifl->ifl_flags & FLG_IF_NEEDED) {
		Ifl_desc *	sifl;

		/*
		 * Determine if anyone else will cause the same SONAME to be
		 * used (this is either caused by two different files having the
		 * same SONAME, or by one files SONAME actually matching another
		 * files basename (if no SONAME is specified within a shared
		 * library its basename will be used)). Probably rare, but some
		 * idiot will do it.
		 */
		for (LIST_TRAVERSE(&ofl->ofl_sos, lnp, sifl)) {
			if ((strcmp(ifl->ifl_soname, sifl->ifl_soname) == 0) &&
			    (ifl != sifl)) {
				eprintf(ERR_FATAL, Errmsg_rnmf, ifl->ifl_name,
				    ifl->ifl_soname, sifl->ifl_name);
				ofl->ofl_flags |= FLG_OF_FATAL;
				return (0);
			}
		}

		/*
		 * If the SONAME is the same as the name the user wishes to
		 * record when building a dynamic library (refer -h option),
		 * we also have a name clash.
		 */
		if (ofl->ofl_soname &&
		    (strcmp(ofl->ofl_soname, ifl->ifl_soname) == 0)) {
			eprintf(ERR_FATAL, Errmsg_rnmh, ifl->ifl_name,
			    ifl->ifl_soname);
			ofl->ofl_flags |= FLG_OF_FATAL;
			return (0);
		}
	}
	return (1);
}


/*
 * Process a relocation entry. At this point all input sections from this
 * input file have been assigned an input section descriptor which is saved
 * in the `ifl_isdesc' array.
 */
/* ARGSUSED2 */
static int
rel_process(Is_desc * isc, Ifl_desc * ifl, Ofl_desc * ofl)
{
	Word 		rndx;
	Is_desc *	risc;
	Os_desc *	osp;
	Shdr *		shdr = isc->is_shdr;

	/*
	 * Make sure this is a valid relocation we can handle.
	 */
	if (shdr->sh_type != M_REL_SHT_TYPE) {
		eprintf(ERR_FATAL, Errmsg_ivst, ifl->ifl_name, isc->is_name,
		    conv_sectyp_str(shdr->sh_type));
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}

	/*
	 * From the relocation section header information determine which
	 * section needs the actual relocation.  Determine which output section
	 * this input section has been assigned to and add to its relocation
	 * list.  Note that the relocation section may be null if it is not
	 * required (ie. .debug, .stabs, etc).
	 */
	rndx = shdr->sh_info;
	if ((risc = ifl->ifl_isdesc[rndx]) != 0) {
		osp = risc->is_osdesc;
		if (list_append(&osp->os_relisdescs, isc) == 0)
			return (S_ERROR);
	}
	return (1);
}


/*
 * Section processing state table.  `Initial' describes the required initial
 * procedure to be called (if any), `Final' describes the final processing
 * procedure (ie. things that can only be done when all required sections
 * have been collected).
 */
static int (*Initial[12][2])() = {

/*			ET_REL			ET_DYN			*/

/* SHT_NULL	*/	NULL,			NULL,
/* SHT_PROGBITS	*/	process_progbits,	process_progbits,
/* SHT_SYMTAB	*/	process_input,		process_input,
/* SHT_STRTAB	*/	process_strtab,		process_strtab,
/* SHT_RELA	*/	process_input,		NULL,
/* SHT_HASH	*/	invalid_section,	NULL,
/* SHT_DYNAMIC	*/	invalid_section,	process_section,
/* SHT_NOTE	*/	process_section,	NULL,
/* SHT_NOBITS	*/	process_nobits,		process_nobits,
/* SHT_REL	*/	process_input,		NULL,
/* SHT_SHLIB	*/	process_section,	invalid_section,
/* SHT_DYNSYM	*/	invalid_section,	process_input

};

static int (*Final[12][2])() = {

/* SHT_NULL	*/	NULL,			NULL,
/* SHT_PROGBITS	*/	NULL,			NULL,
/* SHT_SYMTAB	*/	sym_process,		sym_process,
/* SHT_STRTAB	*/	NULL,			NULL,
/* SHT_RELA	*/	rel_process,		NULL,
/* SHT_HASH	*/	NULL,			NULL,
/* SHT_DYNAMIC	*/	NULL,			process_dynamic,
/* SHT_NOTE	*/	NULL,			NULL,
/* SHT_NOBITS	*/	NULL,			NULL,
/* SHT_REL	*/	rel_process,		NULL,
/* SHT_SHLIB	*/	NULL,			NULL,
/* SHT_DYNSYM	*/	NULL,			sym_process

};

/*
 * Process an elf file.  Each section is compared against the section state
 * table to determine whether it should be processed (saved), ignored, or
 * is invalid for the type of input file being processed.
 */
static int
process_elf(Ifl_desc * ifl, Elf * elf, Ofl_desc * ofl)
{
	Elf_Scn *	scn;
	Shdr *		shdr;
	Word		ndx, sndx;
	char *		str, * name;
	Word		row, column;
	int		ident;
	int		error;
	Is_desc *	vdfisp, * vndisp, * vsyisp;
	Sdf_desc *	sdf;

	/*
	 * First process the .shstrtab section so that later sections can
	 * reference their name.
	 */
	ld_file(ifl->ifl_name, elf_kind(elf), ifl->ifl_flags, elf);
	sndx = ifl->ifl_ehdr->e_shstrndx;
	if ((scn = elf_getscn(elf, sndx)) == NULL) {
		eprintf(ERR_ELF, Errmsg_egsc, ifl->ifl_name);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}
	if ((shdr = elf_getshdr(scn)) == NULL) {
		eprintf(ERR_ELF, Errmsg_egsh, ifl->ifl_name);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}
	if ((name = elf_strptr(elf, sndx, shdr->sh_name)) == NULL) {
		eprintf(ERR_ELF, Errmsg_estr, ifl->ifl_name);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}

	error = process_strtab(name, ifl, shdr, scn, sndx, FALSE, ofl);
	if ((error == 0) || (error == S_ERROR))
		return (error);
	str = ifl->ifl_isdesc[sndx]->is_indata->d_buf;

	/*
	 * Determine the state table column from the input file type.  Note,
	 * shared library sections are not added to the output section list.
	 */
	if (ifl->ifl_ehdr->e_type == ET_DYN) {
		column = 1;
		ident = M_ID_NULL;
	} else {
		column = 0;
		ident = M_ID_UNKNOWN;
	}

	DBG_CALL(Dbg_file_generic(ifl));
	ndx = 0;
	vdfisp = vndisp = vsyisp = 0;
	scn = NULL;
	while (scn = elf_nextscn(elf, scn)) {
		ndx++;
		/*
		 * As we've already processed the .shstrtab don't do it again.
		 */
		if (ndx == sndx)
			continue;
		if ((shdr = elf_getshdr(scn)) == NULL) {
			eprintf(ERR_ELF, Errmsg_egsh, ifl->ifl_name);
			ofl->ofl_flags |= FLG_OF_FATAL;
			return (0);
		}
		name = str + (size_t)(shdr->sh_name);
		row = shdr->sh_type;

		/*
		 * If this is a standard section type process it via the
		 * appropriate action routine.
		 */
		if (row < SHT_NUM) {
			if (Initial[row][column] != NULL) {
				if (Initial[row][column](name, ifl, shdr, scn,
				    ndx, ident, ofl) == S_ERROR)
					return (S_ERROR);
			}
		} else {
			int	_ident;

			/*
			 * If this section is below SHT_LOSUNW then we don't
			 * really know what to do with it, issue a warning
			 * message but do the basic section processing anyway.
			 */
			if (row < (Word)SHT_LOSUNW)
				eprintf(ERR_WARNING, Errmsg_ivst, ifl->ifl_name,
				    name, conv_sectyp_str(shdr->sh_type));

			if (ident)
				_ident = M_ID_USER;
			if ((row == (Word)SHT_SUNW_verdef) ||
			    (row == (Word)SHT_SUNW_verneed) ||
			    (row == (Word)SHT_SUNW_versym))
				_ident = M_ID_NULL;

			if (process_section(name, ifl, shdr, scn, ndx, _ident,
			    ofl) == S_ERROR)
				return (S_ERROR);

			/*
			 * Remember any versioning sections.
			 */
			if (row == (Word)SHT_SUNW_verdef)
				vdfisp = ifl->ifl_isdesc[ndx];
			else if (row == (Word)SHT_SUNW_verneed)
				vndisp = ifl->ifl_isdesc[ndx];
			else if (row == (Word)SHT_SUNW_versym)
				vsyisp = ifl->ifl_isdesc[ndx];
		}

	}

	/*
	 * If this is an explict shared object determine if the user has
	 * specified a control definition.  This descriptor may specify which
	 * version definitions can be used from this object (it may also update
	 * the dependency to USED and supply an alternative SONAME).
	 */
	sdf = 0;
	if (column && (ifl->ifl_flags & FLG_IF_NEEDED)) {
		const char * 	base;

		/*
		 * Use the basename of the input file (typically this is the
		 * compilation environment name, ie. libfoo.so).
		 */
		if ((base = strrchr(ifl->ifl_name, '/')) == NULL)
			base = ifl->ifl_name;
		else
			base++;

		if ((sdf = sdf_find(base, &ofl->ofl_socntl)) != 0) {
			sdf->sdf_file = ifl;
			ifl->ifl_sdfdesc = sdf;
		}
	}

	/*
	 * Process any version dependencies.  These will establish shared object
	 * `needed' entries in the same manner as will be generated from the
	 * .dynamic's NEEDED entries.
	 */
	if (vndisp && (ofl->ofl_flags & (FLG_OF_NOUNDEF | FLG_OF_SYMBOLIC)))
		if (vers_need_process(vndisp, ifl, ofl) == S_ERROR)
			return (S_ERROR);

	/*
	 * Before processing any symbol resolution or relocations process any
	 * version sections.
	 */
	if (vdfisp || (sdf && (sdf->sdf_flags & FLG_SDF_SELECT)))
		if (vers_def_process(vdfisp, ifl, ofl) == S_ERROR)
			return (S_ERROR);
	if (vsyisp)
		if (vers_sym_process(vsyisp, ifl) == S_ERROR)
			return (S_ERROR);

	/*
	 * Having collected the appropriate sections carry out any additional
	 * processing if necessary.
	 */
	for (ndx = 0; ndx < ifl->ifl_ehdr->e_shnum; ndx++) {
		Is_desc *	isp;

		if ((isp = ifl->ifl_isdesc[ndx]) == 0)
			continue;
		row = isp->is_shdr->sh_type;

		ld_section(isp->is_name, isp->is_shdr, ndx,
		    isp->is_indata, elf);

		/*
		 * If this is a standard section type process it via the
		 * appropriate action routine.
		 */
		if (row < SHT_NUM) {
			if (Final[row][column] != NULL)
				if (Final[row][column](isp, ifl, ofl) ==
				    S_ERROR)
					return (S_ERROR);
		}
	}
	return (1);
}

/*
 * Process the current input file.  There are basically three types of files
 * that come through here:
 *
 *  o	files explicitly defined on the command line (ie. foo.o or bar.so),
 *	in this case only the `name' field is valid.
 *
 *  o	libraries determined from the -l command line option (ie. -lbar),
 *	in this case the `soname' field contains the basename of the located
 *	file.
 *
 * Any shared object specified via the above two conventions must be recorded
 * as a needed dependency.
 *
 *  o	libraries specified as dependencies of those libraries already obtained
 *	via the command line (ie. bar.so has a DT_NEEDED entry of fred.so.1),
 *	in this case the `soname' field contains either a full pathname (if the
 *	needed entry contained a `/'), or the basename of the located file.
 *	These libraries are processed to verify symbol binding but are not
 *	recorded as dependencies of the output file being generated.
 */
Ifl_desc *
process_ifl(const char * name, const char * soname, int fd, Elf * elf,
	Half flags, Ofl_desc * ofl)
{
	Ifl_desc *	ifl;
	Ehdr *		ehdr;
	Listnode *	lnp;
	int		error = 0;
	struct stat	status;
	const char *	errmsg, * _name;
	Ar_desc *	adp;


	/*
	 * If this file was not extracted from an archive obtain its device
	 * information.  This will be used to determine if the file has already
	 * been processed (rather than simply comparing filenames, the device
	 * information provides a quicker comparison and detects linked files).
	 */
	if (!(flags & FLG_IF_EXTRACT))
		(void) fstat(fd, &status);
	else {
		status.st_dev = 0;
		status.st_ino = 0;
	}

	switch (elf_kind(elf)) {
	case ELF_K_AR:
		/*
		 * Determine if we've already come across this archive file.
		 */
		if (!(flags & FLG_IF_EXTRACT)) {
			for (LIST_TRAVERSE(&ofl->ofl_ars, lnp, adp)) {
				if ((adp->ad_stdev != status.st_dev) ||
				    (adp->ad_stino != status.st_ino))
					continue;

				/*
				 * We've seen this file before so reuse the
				 * original archive descriptor and discard the
				 * new elf descriptor.
				 */
				DBG_CALL(Dbg_file_reuse(name, adp->ad_name));
				(void) elf_end(elf);
				return ((Ifl_desc *)process_archive(name, fd,
				    adp, ofl));
			}
		}

		/*
		 * As we haven't processed this file before establish a new
		 * archive descriptor.
		 */
		adp = ar_setup(name, elf, fd, ofl);
		if ((adp == 0) || (adp == (Ar_desc *)S_ERROR))
			return ((Ifl_desc *)adp);
		adp->ad_stdev = status.st_dev;
		adp->ad_stino = status.st_ino;

		ld_file(name, ELF_K_AR, flags, elf);

		return ((Ifl_desc *)process_archive(name, fd, adp, ofl));

	case ELF_K_ELF:
		/*
		 * Obtain the elf header so that we can determine what type of
		 * elf ELF_K_ELF file this is.
		 */
		if ((ehdr = elf_getehdr(elf)) == NULL) {
			eprintf(ERR_ELF, Errmsg_egeh, name);
			ofl->ofl_flags |= FLG_OF_FATAL;
			return (0);
		}

		/*
		 * Determine if we've already come across this file.
		 */
		if (!(flags & FLG_IF_EXTRACT)) {
			List *	lst;

			if (ehdr->e_type == ET_REL)
				lst = &ofl->ofl_objs;
			else
				lst = &ofl->ofl_sos;

			/*
			 * Traverse the appropriate file list and determine if
			 * a dev/inode match is found.
			 */
			for (LIST_TRAVERSE(lst, lnp, ifl)) {
				if ((ifl->ifl_stino != status.st_ino) ||
				    (ifl->ifl_stdev != status.st_dev))
					continue;

				/*
				 * If we've processed this file before determine
				 * whether it's the same file name or not so as
				 * to provide the most descriptive diagnostic.
				 */
				_name = ifl->ifl_name;
				if (strcmp(name, _name) == 0) {
					errmsg = Errmsg_amif;
					_name = 0;
				} else
					errmsg = Errmsg_amil;

				/*
				 * If the file was explicitly defined on the
				 * command line (this is always the case for
				 * relocatable objects, and is true for shared
				 * objects when they weren't specified via -l or
				 * were dragged in as an implicit dependency),
				 * then warn the user.
				 */
				DBG_CALL(Dbg_file_skip(name, _name));
				if ((flags & FLG_IF_CMDLINE) ||
				    (ifl->ifl_flags & FLG_IF_CMDLINE))
					eprintf(ERR_WARNING, errmsg, name,
					    _name);

				/*
				 * Disregard (skip) this image.
				 */
				(void) elf_end(elf);
				return (ifl);
			}
		}

		/*
		 * At this point, we know we need the file.  Establish an input
		 * file descriptor and continue processing.
		 */
		ifl = ifl_setup(name, ehdr, flags, ofl);
		if ((ifl == 0) || (ifl == (Ifl_desc *)S_ERROR))
			return (ifl);
		ifl->ifl_stdev = status.st_dev;
		ifl->ifl_stino = status.st_ino;

		switch (ehdr->e_type) {
		case ET_REL:
			error = process_elf(ifl, elf, ofl);
			break;
		case ET_DYN:
			if ((ofl->ofl_flags & FLG_OF_STATIC) ||
			    !(ofl->ofl_flags & FLG_OF_DYNLIBS)) {
				eprintf(ERR_FATAL, Errmsg_isos, name);
				ofl->ofl_flags |= FLG_OF_FATAL;
				return (0);
			}

			/*
			 * Record any additional shared object information.
			 * If no soname is specified (eg. this file was
			 * derived from a explicit filename declaration on the
			 * command line, ie. bar.so) use the pathname.
			 * This entry may be overridden if the files dynamic
			 * section specifies an DT_SONAME value.
			 */
			if (soname == NULL)
				ifl->ifl_soname = ifl->ifl_name;
			else
				ifl->ifl_soname = soname;
			error = process_elf(ifl, elf, ofl);
			break;
		default:
			eprintf(ERR_FATAL, Errmsg_ufte, name);
			ofl->ofl_flags |= FLG_OF_FATAL;
			return (0);
		}
		break;
	default:
		eprintf(ERR_FATAL, Errmsg_ufte, name);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}
	if ((error == 0) || (error == S_ERROR))
		return ((Ifl_desc *)error);
	else
		return (ifl);
}

/*
 * Having successfully opened a file, set up the necessary elf structures to
 * process it further.  This small section of processing is slightly different
 * from the elf initialization required to process a relocatable object from an
 * archive (see libs.c: process_archive()).
 */
Ifl_desc *
process_open(const char * path, int dlen, int fd, Ofl_desc * ofl, Half flags)
{
	Elf *		elf;
	Ifl_desc *	ifl;

	if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		eprintf(ERR_ELF, Errmsg_ebgn, path);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}
	ifl = process_ifl(&path[0], &path[dlen], fd, elf, flags, ofl);
	if (ifl != (Ifl_desc *)S_ERROR)
		(void) close(fd);
	return (ifl);
}

/*
 * Process a required library (i.e. the dependency of a shared object).
 * Combine the directory and filename, check the resultant path size, and try
 * opening the pathname.
 */
static Ifl_desc *
process_req_lib(Sdf_desc * sdf, const char * dir, const char * file,
	Ofl_desc * ofl)
{
	int		dlen, plen, fd;
	char		path[PATH_MAX];
	const char *	_dir = dir;

	/*
	 * Determine the sizes of the directory and filename to insure we don't
	 * exceed our buffer.
	 */
	if ((dlen = strlen(dir)) == 0) {
		_dir = ".";
		dlen = 1;
	}
	dlen++;
	plen = dlen + strlen(file) + 1;
	if (plen > PATH_MAX) {
		eprintf(ERR_FATAL, Errmsg_ptol, _dir, file);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}

	/*
	 * Build the entire pathname and try and open the file.
	 */
	(void) strcpy(path, _dir);
	(void) strcat(path, "/");
	(void) strcat(path, file);
	DBG_CALL(Dbg_libs_req(sdf, path));
	if ((fd = open(path, O_RDONLY)) == -1)
		return (0);
	else {
		char * _path;

		if ((_path = (char *)malloc(strlen(path) + 1)) == 0)
			return ((Ifl_desc *)S_ERROR);
		(void) strcpy(_path, path);
		return (process_open(_path, dlen, fd, ofl, NULL));
	}
}

/*
 * Finish any library processing.  Walk the list of so's that have been listed
 * as "included" by shared objects we have previously processed.  Examine them,
 * without adding them as explicit dependents of this program, in order to
 * complete our symbol definition process.  The search path rules are:
 *
 *  o	use any user supplied paths, i.e. LD_LIBRARY_PATH and -L, then
 *
 *  o	use any RPATH defined within the parent shared object, then
 *
 *  o	use the default directories, i.e. LIBPATH or -YP.
 */
int
finish_libs(Ofl_desc * ofl)
{
	Listnode *	lnp1;
	Sdf_desc *	sdf;

	/*
	 * Make sure we are back in dynamic mode.
	 */
	ofl->ofl_flags |= FLG_OF_DYNLIBS;

	for (LIST_TRAVERSE(&ofl->ofl_soneed, lnp1, sdf)) {
		Listnode *	lnp2;
		const char *	path;
		int		fd;
		Ifl_desc *	ifl;
		const char *	file = sdf->sdf_name;

		/*
		 * See if this file has already been processed.  At the time
		 * this implicit dependency was determined there may still have
		 * been more explict dependencies to process.  (Note, if we ever
		 * do parse the command line three times we would be able to
		 * do all this checking when processing the dynamic section).
		 */
		if (sdf->sdf_file)
			continue;

		for (LIST_TRAVERSE(&ofl->ofl_sos, lnp2, ifl)) {
			if (strcmp(file, ifl->ifl_soname) == 0) {
				sdf->sdf_file = ifl;
				break;
			}
		}
		if (sdf->sdf_file)
			continue;

		/*
		 * If the current element embeds a "/", then it's to be taken
		 * "as is", with no searching involved.
		 */
		for (path = file; *path; path++)
			if (*path == '/')
				break;
		if (*path) {
			DBG_CALL(Dbg_libs_req(sdf, file));
			if ((fd = open(file, O_RDONLY)) == -1) {
				eprintf(ERR_WARNING, Errmsg_rfnf, file,
					sdf->sdf_rfile);
			} else {
				if ((ifl = process_open(file, sizeof (file) + 1,
				    fd, ofl, NULL)) == (Ifl_desc *)S_ERROR)
					return (S_ERROR);
				sdf->sdf_file = ifl;
			}
			continue;
		}

		/*
		 * Now search for this file in any user defined directories.
		 */
		for (LIST_TRAVERSE(&ofl->ofl_ulibdirs, lnp2, path)) {
			ifl = process_req_lib(sdf, path, file, ofl);
			if (ifl == (Ifl_desc *)S_ERROR)
				return (S_ERROR);
			if (ifl) {
				sdf->sdf_file = ifl;
				break;
			}
		}
		if (sdf->sdf_file)
			continue;

		/*
		 * Next use the local rules defined within the parent shared
		 * object.
		 */
		if (sdf->sdf_rpath != NULL) {
			char *	rpath;

			if ((rpath =
			    (char *)malloc(strlen(sdf->sdf_rpath) + 1)) == 0)
				return (S_ERROR);
			(void) strcpy(rpath, sdf->sdf_rpath);
			DBG_CALL(Dbg_libs_rpath(sdf->sdf_rfile, rpath));
			if ((path = strtok(rpath, ":")) != NULL) {
				do {
					ifl = process_req_lib(sdf, path,
						file, ofl);
					if (ifl == (Ifl_desc *)S_ERROR)
						return (S_ERROR);
					if (ifl) {
						sdf->sdf_file = ifl;
						break;
					}
				} while ((path = strtok(NULL, ":")) != NULL);
			}
			(void) free(rpath);
		}
		if (sdf->sdf_file)
			continue;

		/*
		 * Finally try the default library search directories.
		 */
		for (LIST_TRAVERSE(&ofl->ofl_dlibdirs, lnp2, path)) {
			ifl = process_req_lib(sdf, path, file, ofl);
			if (ifl == (Ifl_desc *)S_ERROR)
				return (S_ERROR);
			if (ifl) {
				sdf->sdf_file = ifl;
				break;
			}
		}
		if (sdf->sdf_file)
			continue;

		/*
		 * If we've got this far we haven't found the shared object.
		 */
		eprintf(ERR_WARNING, Errmsg_rfnf, file, sdf->sdf_rfile);
	}

	/*
	 * Finally, now that all objects have been input, make sure any version
	 * requirements have been met.
	 */
	return (vers_verify(ofl));
}
