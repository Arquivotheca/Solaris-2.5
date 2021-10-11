/*
 *	Copyright (c) 1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)object.c	1.15	94/10/06 SMI"

/*
 * Object file dependent suport for ELF objects.
 */
#include	<stdio.h>
#include	<unistd.h>
#include	<libelf.h>
#include	<string.h>
#include	"libld.h"
#include	"_rtld.h"
#include	"_elf.h"
#include	"paths.h"
#include	"debug.h"

static Rt_map *		olmp = 0;

#define	SYM_NBKTS	211

/*
 * Process a relocatable object.  The static object link map pointer is used as
 * a flag to determine whether a concatenation is already in progress (ie. an
 * LD_PRELOAD may specify a list of objects).  The link map returned simply
 * specifies an `object' flag which the caller can interpret and thus call
 * elf_obj_fini() to complete the concatenation.
 */
static Rt_map *
elf_obj_init(Lm_list * lml, const char * name)
{
	Rt_map *	lmp, * rlmp = lml_rtld.lm_head;
	Ofl_desc *	ofl;
	Dyn *		used;
	const char *	libld = 0, * libc = 0;

	/*
	 * Determine from our own dynamic section what version of the
	 * link-editor library, and libc, we need to use.
	 */
	for (used = (Dyn *)DYN(rlmp); used->d_tag != DT_NULL; used++) {
		const char *	str;

		if (used->d_tag != DT_USED)
			continue;
		str = (const char *)STRTAB(rlmp) + used->d_un.d_val;
		if (strncmp(str, "libld.so", 8) == 0)
			libld = str;
		else if (strncmp(str, "libc.so", 7) == 0)
			libc = str;
	}
	if ((libld == 0) || (libc == 0)) {
		eprintf(ERR_FATAL, "unable to determine USED libraries "
		    "(libld/libc)");
		return (0);
	}

	/*
	 * Load both the link-editor library and libc.  Note we switch off
	 * tracing during this loading so that an ldd(1) of an object preload
	 * only shows the user the objects that they care about.
	 */
	if ((lmp = is_so_loaded(&lml_rtld, libld)) == 0) {
		Rt_map *	_lmp;
		int		_tracing = tracing;

		tracing = 0;
		DBG_CALL(Dbg_file_needed(libld, NAME(rlmp)));
		if ((lmp = load_so(&lml_rtld, libld, rlmp)) == 0) {
			tracing = _tracing;
			return (0);
		}
		PERMIT(lmp) = perm_set(PERMIT(lmp), PERMIT(rlmp));

		DBG_CALL(Dbg_file_needed(libc, NAME(rlmp)));
		if ((_lmp = load_so(&lml_rtld, libc, rlmp)) == 0) {
			tracing = _tracing;
			return (0);
		}
		PERMIT(_lmp) = perm_set(PERMIT(_lmp), PERMIT(rlmp));

		if (analyze_so(&lml_rtld, lmp, bind_mode, 0) == 0) {
			tracing = _tracing;
			return (0);
		}
		tracing = _tracing;
		if (relocate_so(lmp, bind_mode) == 0)
			return (0);

		/*
		 * Initialize the elf library version.
		 */
		if (elf_version(EV_CURRENT) == EV_NONE) {
			eprintf(ERR_FATAL, "libelf is out of date");
			return (0);
		}
	}

	/*
	 * Initialize an output file descriptor and the entrance criteria.
	 */
	if ((ofl = (Ofl_desc *)calloc(sizeof (Ofl_desc), 1)) == 0)
		return (0);
	ofl->ofl_e_machine = M_MACH;
	ofl->ofl_e_flags = 0;
	ofl->ofl_libver = EV_CURRENT;
	ofl->ofl_segalign = syspagsz;
	ofl->ofl_flags = (FLG_OF_DYNAMIC | FLG_OF_SHAROBJ | FLG_OF_STRIP |
		FLG_OF_MEMORY);
	if ((ofl->ofl_symbkt =
	    (Sym_cache **)calloc(sizeof (Sym_cache), SYM_NBKTS)) == 0)
		return (0);
	ofl->ofl_symbktcnt = SYM_NBKTS;

	/*
	 * Obtain a generic set of entrance criteria.
	 */
	if (ent_setup(ofl) == S_ERROR)
		return (0);

	/*
	 * Generate a link map place holder and use the `rt_priv' element to
	 * to maintain the output file descriptor.
	 */
	if ((olmp = (Rt_map *)calloc(sizeof (Rt_map), 1)) == 0)
		return (0);
	FLAGS(olmp) |= FLG_RT_OBJECT;
	olmp->rt_priv = (void *)ofl;

	/*
	 * Assign the output file name to be the initial object that got us
	 * here.  This name is being used for diagnostic purposes only as
	 * we don't actually generate an output file unless debugging is
	 * enabled.
	 */
	ofl->ofl_name = name;
	NAME(olmp) = (char *)name;

	lm_append(lml, olmp);
	return (olmp);
}


/*
 * Initial processing of a relocatable object.  If this is the first object
 * encountered we need to initialize some structures, then simply call the
 * link-edit functionality to provide the initial processing of the file (ie.
 * reads in sections and symbols, performs symbol resolution if more that one
 * object file have been specified, and assigns input sections to output
 * sections).
 */
Rt_map *
elf_obj_file(Lm_list * lml, const char * name)
{
	/*
	 * If this is the first relocatable object (LD_PRELOAD could provide a
	 * list of objects), initialize an input file descriptor and a link map.
	 * Note, because elf_obj_init() will reuse the global fmap structure we
	 * need to retain the original open file descriptor, both for the
	 * process_open() call and for the correct closure of the file on return
	 * to load_so().
	 */
	if (!olmp) {
		int	fd = fmap->fm_fd;

		olmp = elf_obj_init(lml, name);
		fmap->fm_fd = fd;
		if (olmp == 0)
			return (0);
	}

	/*
	 * Proceed to process the input file.
	 */
	DBG_CALL(Dbg_util_nl());
	if (process_open(name, 0, fmap->fm_fd,
	    (Ofl_desc *)olmp->rt_priv, NULL) == (Ifl_desc *)S_ERROR)
		return (0);

	return (olmp);
}

/*
 * Finish relocatable object processing.  Having already initially processed one
 * or more objects, complete the generation of a shared object image by calling
 * the appropriate link-edit functionality (refer to sgs/ld/common/main.c).
 */
Rt_map *
elf_obj_fini(Lm_list * lml, Rt_map * lmp)
{
	Ofl_desc *	ofl = (Ofl_desc *)lmp->rt_priv;
	Rt_map *	nlmp;
	Addr		etext;

	DBG_CALL(Dbg_util_nl());

	if (reloc_init(ofl) == S_ERROR)
		return (0);
	if (sym_validate(ofl) == S_ERROR)
		return (0);
	if (make_sections(ofl) == S_ERROR)
		return (0);
	if (create_outfile(ofl) == S_ERROR)
		return (0);
	if ((etext = update_outfile(ofl)) == (Addr)S_ERROR)
		return (0);
	if (reloc_process(ofl) == S_ERROR)
		return (0);

	/*
	 * At this point we have a memory image of the shared object.  The link
	 * editor would normally simply write this to the required output file.
	 * If we're debugging generate a standard temporary output file.
	 */
	DBG_CALL(Dbg_file_output(ofl));

	/*
	 * Generate a new link map representing the memory image created.
	 */
	if ((nlmp = LM_NEW_LM(lml_main.lm_head)(&lml_main,
	    ofl->ofl_name, ofl->ofl_osdynamic->os_outdata->d_buf,
	    (unsigned long)ofl->ofl_ehdr, (unsigned long)ofl->ofl_ehdr + etext,
	    (unsigned long)ofl->ofl_size, 0, ofl->ofl_phdr,
	    ofl->ofl_ehdr->e_phnum, ofl->ofl_ehdr->e_phentsize)) == 0)
		return (0);

	/*
	 * Remove this link map from the end of the link map list and copy its
	 * contents into the link map originally created for this file (we copy
	 * the contents rather than manipulate the link map pointers as parts
	 * of the dlopen code have remembered the original link map address).
	 */
	NEXT((Rt_map *)PREV(nlmp)) = 0;
	lml->lm_tail = (Rt_map *)PREV(nlmp);

	PREV(nlmp) = PREV(olmp);
	NEXT(nlmp) = NEXT(olmp);
	PERMIT(nlmp) = PERMIT(olmp);
	REFLM(nlmp) = olmp;
	FLAGS(nlmp) |= (FLAGS(olmp) & ~FLG_RT_OBJECT) | FLG_RT_ALLOC;

	(void) free(olmp->rt_priv);
	(void) memcpy(olmp, nlmp, sizeof (Rt_map));
	(void) free(nlmp);
	nlmp = olmp;
	olmp = 0;
	return (nlmp);
}

/*
 * Interposed routines from libelf (see libelf/common/output.c).  A standard
 * elf_update(elf, ELF_C_WRITE) operation results in:
 *
 *  o	a call to elf_outmap() to generate the initial image for the output
 *	file.  This image is normally created by an ftruncate() of the output
 *	file, followed by an mmap().
 *
 *  o	the elf_update() routine then copies all the data buffers associated
 *	with the elf descriptor into this new image.
 *
 *  o	finally, a call to elf_outsync() results in the image being written
 *	back to the file, normally with msync().
 *
 * Because we wish to continue modifying this initial output image, and we know
 * that the images size will not be changed by this modification, all we want
 * of elf_update() is for it to perform the necessary magic to translate the
 * data buffers into the output image.  Thus we interpose on the output
 * controlling elf routine to simply malloc() the required image.
 */
char *
/* ARGSUSED */
_elf_outmap(int fd, size_t size, unsigned int * flag)
{
	return ((char *)malloc(size));
}
