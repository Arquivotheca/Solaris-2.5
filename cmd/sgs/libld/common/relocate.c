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
#pragma ident	"@(#)relocate.c	1.33	95/08/01 SMI"

/* LINTLIBRARY */

/*
 * set-up for relocations
 */
#include	<string.h>
#include	<stdio.h>
#include	"debug.h"
#include	"_libld.h"

static const char
	* Errmsg_rirg = "internal error: unexpected relocation found, "
			"relocation performed by reloc_generic().",
	* Errmsg_nrr  = "file=%s: section %s: skipping null relocation record",
	* Errmsg_raus = "file=%s: section %s: attempt to "
			"relocate with respect to unknown symbol\n"
			"\t(offset=%#x, symndx=%d, type=%s)",
	* Errmsg_ustf = "file=%s: section %s: unexpected symbol type found\n"
			"\t(sym=%s, symndx=%d, type=%d)",
	* Errmsg_rrnw =	"relocations remain against allocatable but "
			"non-writable sections";

extern	Word	orels;


static int
reloc_GOT_relative(Boolean local, Rel_desc * rsp, Rel * reloc, Ofl_desc * ofl)
{
	Sym_desc * 	sdp;
	Word		flags = ofl->ofl_flags;

	/*
	 * If this is a locally bound symbol then we will only need
	 * to generate one GOT index for both a WEAK and a STRONG
	 * symbol.  But - if this symbol isn't bound locally then we
	 * will need a GOT entry for each symbol so that each could
	 * potentially be re-bound at runtime.
	 */
	if (local)
		sdp = rsp->rel_usym;
	else
		sdp = rsp->rel_sym;
	/*
	 * If this is the first time we've seen this symbol in a GOT
	 * relocation we need to assign it a GOT token.  Once we've got
	 * all of the GOT's assigned we can assign the actual indexes.
	 */
	if (sdp->sd_GOTndx == 0) {
		Word	rtype = rsp->rel_rtype;
		sdp->sd_GOTndx = assign_got_ndx(rtype, 0, ofl);

		/*
		 * Now we initialize the GOT table entry.
		 *
		 * Pseudo code to describe the the decisions below:
		 *
		 * If (local)
		 * then
		 *	enter symbol value in GOT table entry
		 *	if (Shared Object)
		 *	then
		 *		create Relative relocation against symbol
		 *	fi
		 * else
		 *	clear GOT table entry
		 *	create a GLOB_DAT relocation against symbol
		 * fi
		 */
		if (local == TRUE) {
			rsp->rel_sym->sd_GOTndx = sdp->sd_GOTndx;

			if (flags & FLG_OF_SHAROBJ) {
				if (add_actrel(FLG_REL_GOT | FLG_REL_GOTCL,
				    rsp, reloc, ofl) == S_ERROR)
					return (S_ERROR);

				rsp->rel_rtype = M_R_RELATIVE;
				if (add_outrel(FLG_REL_GOT | FLG_REL_ADVAL,
				    rsp, reloc, ofl) == S_ERROR)
					return (S_ERROR);
				rsp->rel_rtype = rtype;
			} else {
				if (add_actrel(FLG_REL_GOT, rsp,
				    reloc, ofl) == S_ERROR)
					return (S_ERROR);
			}
		} else {
			if (add_actrel(FLG_REL_GOT | FLG_REL_CLVAL, rsp,
			    reloc, ofl) == S_ERROR)
				return (S_ERROR);

			rsp->rel_rtype = M_R_GLOB_DAT;
			if (add_outrel(FLG_REL_GOT, rsp, reloc, ofl) ==
			    S_ERROR)
				return (S_ERROR);
			rsp->rel_rtype = rtype;
		}
	} else
		sdp->sd_GOTndx = assign_got_ndx(rsp->rel_rtype,
			sdp->sd_GOTndx, ofl);

	/*
	 * perform relocation to GOT table entry.
	 */
	return (add_actrel(NULL, rsp, reloc, ofl));
}


/*
 * Perform relocations for PLT's
 */
static int
reloc_plt(Rel_desc * rsp, Rel * reloc, Ofl_desc * ofl)
{
	Sym_desc *	sdp = rsp->rel_sym;

	/*
	 * if (not PLT yet assigned)
	 * then
	 *	assign PLT index to symbol
	 *	build output JMP_SLOT relocation
	 * fi
	 */
	if (sdp->sd_aux->sa_PLTndx == 0) {
		Word	ortype = rsp->rel_rtype;

		assign_plt_ndx(sdp, ofl);
		rsp->rel_rtype = M_R_JMP_SLOT;
		if (add_outrel(FLG_REL_PLT, rsp, reloc, ofl) ==
		    S_ERROR)
			return (S_ERROR);
		rsp->rel_rtype = ortype;
	}
	/*
	 * perform relocation to PLT table entry
	 */
	return (add_actrel(NULL, rsp, reloc, ofl));
}


/*
 * process GLOBAL undefined and ref_dyn_need symbols.
 */
static int
reloc_exec(Rel_desc * rsp, Rel * reloc, Ofl_desc * ofl)
{
	Sym_desc *	_sdp, * sdp = rsp->rel_sym;
	Sym_aux *	sap = sdp->sd_aux;
	Sym *		sym = sdp->sd_sym;

	/*
	 * Reference is to a function so simply create a plt entry for it.
	 */
	if (ELF_ST_TYPE(sym->st_info) == STT_FUNC)
		return (reloc_plt(rsp, reloc, ofl));

	/*
	 * If the relocation is against a writable section simply compute the
	 * necessary output relocation.  As an optimization, if the symbol has
	 * already been transformed into a copy relocation then we can perform
	 * the relocation directly (copy relocations should only be generated
	 * for references from the text segment and these relocations are
	 * normally carried out before we get to the data segment relocations).
	 */
	if ((ELF_ST_TYPE(sym->st_info) == STT_OBJECT) &&
	    (rsp->rel_osdesc->os_shdr->sh_flags & SHF_WRITE)) {
		if (sdp->sd_flags & FLG_SY_MVTOCOMM)
			return (add_actrel(NULL, rsp, reloc, ofl));
		else
			return (add_outrel(NULL, rsp, reloc, ofl));
	}

	/*
	 * If the reference isn't to an object (normally because some idiot
	 * hasn't defined a .type directive in some assembler source), then
	 * simply apply a generic relocation (this has a tendency to result in
	 * text relocations).
	 */
	if ((ELF_ST_TYPE(sym->st_info) != STT_OBJECT) ||
	    (sym->st_shndx == SHN_ABS)) {
		eprintf(ERR_WARNING, Errmsg_ustf, rsp->rel_fname,
		    rsp->rel_isdesc->is_name, rsp->rel_sname, sdp->sd_symndx,
		    ELF_ST_TYPE(sym->st_info));
		return (add_outrel(NULL, rsp, reloc, ofl));
	}

	/*
	 * Prepare for generating a copy relocation.
	 *
	 * If this symbol is one of an alias pair, we need to insure both
	 * symbols become part of the output (the strong symbol will be used to
	 * maintain the symbols state).  And, if we did raise the precedence of
	 * a symbol we need to check and see if this is a weak symbol.  If it is
	 * we want to use it's strong counter part.
	 *
	 * The results of this logic should be:
	 *	rel_usym: assigned to strong
	 *	 rel_sym: assigned to symbol to perform
	 *		  copy_reloc against (weak or strong).
	 */
	if (sap->sa_linkndx) {
		_sdp = sdp->sd_file->ifl_oldndx[sap->sa_linkndx];

		if (_sdp->sd_ref < sdp->sd_ref) {
			_sdp->sd_ref = sdp->sd_ref;
			_sdp->sd_flags |= FLG_SY_REFRSD;
		} else if (_sdp->sd_ref > sdp->sd_ref) {
			sdp->sd_ref = _sdp->sd_ref;
			sdp->sd_flags |= FLG_SY_REFRSD;
		}

		/*
		 * If this is a weak symbol then we want to move the strong
		 * symbol into local .bss.  If their is a copy_reloc to be
		 * performed that should still occur against the WEAK symbol.
		 */
		if ((ELF_ST_BIND(sdp->sd_sym->st_info) == STB_WEAK) ||
		    (sdp->sd_flags & FLG_SY_WEAKDEF))
			rsp->rel_usym = _sdp;
	} else
		_sdp = 0;

	/*
	 * If the reference is to an object then allocate space for the object
	 * within the executables .bss.  Relocations will now be performed from
	 * this new location.  If the original shared objects data is
	 * initialized the generate a copy relocation that will copy the data to
	 * the executables .bss at runtime.
	 */
	if (!(rsp->rel_usym->sd_flags & FLG_SY_MVTOCOMM)) {
		/*
		 * Indicate that the symbol(s) against which we're relocating
		 * have been moved to the executables common.  Also, insure that
		 * the symbol(s) remain marked as global, as the shared object
		 * from which they are copied must be able to relocate to the
		 * new common location within the executable.
		 *
		 * Note that even though a new symbol has been generated in the
		 * output files' .bss, the symbol must remain REF_DYN_NEED and
		 * not be promoted to REF_REL_NEED.  sym_validate() still needs
		 * to carry out a number of checks against the symbols binding
		 * that are triggered by the REF_DYN_NEED state.
		 */
		sdp->sd_flags |= (FLG_SY_MVTOCOMM | FLG_SY_GLOBAL);
		sdp->sd_flags &= ~FLG_SY_LOCAL;
		if (_sdp) {
			_sdp->sd_flags |= (FLG_SY_MVTOCOMM | FLG_SY_GLOBAL);
			_sdp->sd_flags &= ~FLG_SY_LOCAL;

			/*
			 * Make sure the symbol has a reference in case of any
			 * error diagnostics against it (perhaps this belongs
			 * to a version that isn't allowable for this build).
			 * The resulting diagnostic (see sym_undef_entry())
			 * might seem a little bogus, as the symbol hasn't
			 * really been referenced by this file, but has been
			 * promoted as a consequence of its alias reference.
			 */
			if (!(_sdp->sd_aux->sa_rfile))
				_sdp->sd_aux->sa_rfile = sdp->sd_aux->sa_rfile;
		}

		/*
		 * Assign the symbol to the bss and insure sufficient alignment
		 * (we don't know the real alignment so we have to make the
		 * worst case guess).
		 */
		_sdp = rsp->rel_usym;
		if (sym_copy(_sdp) == S_ERROR)
			return (S_ERROR);
		_sdp->sd_sym->st_shndx = SHN_COMMON;
		_sdp->sd_sym->st_value =
		    (_sdp->sd_sym->st_size < (M_WORD_ALIGN * 2)) ?
		    M_WORD_ALIGN : M_WORD_ALIGN * 2;

		/*
		 * If the symbol references initialized data indicate that we
		 * need copy relocations at startup.  This is the method by
		 * which global, initialized data is exported from a .so.
		 */
		if (_sdp->sd_isc->is_shdr->sh_type != SHT_NOBITS) {
			Word	rtype = rsp->rel_rtype;

			rsp->rel_rtype = M_R_COPY;
			if (add_outrel(FLG_REL_BSS, rsp, reloc, ofl) == S_ERROR)
				return (S_ERROR);
			rsp->rel_rtype = rtype;
			DBG_CALL(Dbg_syms_reloc(sdp, TRUE));
		} else
			DBG_CALL(Dbg_syms_reloc(sdp, FALSE));
	}
	return (add_actrel(NULL, rsp, reloc, ofl));
}

/*
 * All relocations should have been handled by the other routines.  This
 * routine is hear as a catch all, if we do enter it we've goofed - but
 * we'll try and to the best we can.
 */
static int
reloc_generic(Rel_desc * rsp, Rel * reloc, Ofl_desc * ofl)
{
	Word		flags = ofl->ofl_flags;

	eprintf(ERR_WARNING, Errmsg_rirg);

	/*
	 * If building a shared object then put the relocation off
	 * until runtime.
	 */
	if (flags & FLG_OF_SHAROBJ)
		return (add_outrel(NULL, rsp, reloc, ofl));

	/*
	 * else process relocation now.
	 */
	return (add_actrel(NULL, rsp, reloc, ofl));
}



static int
process_reloc(Ofl_desc * ofl, Is_desc * isect, Is_desc * rsect, Os_desc * osect)
{
	Rel *		rend;	/* Just past end of relocation section data. */
	Rel *		reloc;	/* Current entry in relocation section data. */
	Word		rsize;	/* Size of relocation section data. */
	Word		rstndx;	/* Relocation index of current entry. */
	Word		rtype;	/* Relocation type of current entry. */
	Word		roffset; /* Relocation offset of current entry. */
	Rel_desc	reld;	/* holder for add_{out|in}rel() processing */
	Word		flags = ofl->ofl_flags;

	rsize = rsect->is_shdr->sh_size;
	reloc = (Rel *)rsect->is_indata->d_buf;

	/*
	 * Build up the basic information in for the Rel_desc structure.
	 */
	reld.rel_osdesc = osect;
	reld.rel_isdesc = isect;
	reld.rel_fname = rsect->is_file->ifl_name;
	if ((!(ofl->ofl_flags & FLG_OF_RELOBJ)) &&
	    (osect->os_sgdesc->sg_phdr.p_type == PT_LOAD))
		reld.rel_flags = FLG_REL_LOAD;
	else
		reld.rel_flags = 0;

	DBG_CALL(Dbg_reloc_proc(osect, isect));
	for (rend = reloc + (rsize / sizeof (Rel)); reloc < rend; reloc++) {
		Sym_desc *	sdp;
		Sym_aux *	sap;
		Boolean		local = FALSE;

		rtype = ELF_R_TYPE(reloc->r_info);
		rstndx = ELF_R_SYM(reloc->r_info);
		roffset = reloc->r_offset;

		/*
		 * Determine whether we're dealing with a global symbol.
		 */
		if (((sdp = rsect->is_file->ifl_oldndx[rstndx]) != 0) &&
		    sdp->sd_name && *sdp->sd_name) {
			reld.rel_sname = sdp->sd_name;

			/*
			 * It's possible that this symbol is a candidate for
			 * reduction to local. (This test is also carried out
			 * sym_validate()).
			 */
			if ((ofl->ofl_flags & FLG_OF_AUTOLCL) &&
			    ((!(sdp->sd_flags & MSK_SY_DEFINED)) &&
			    (sdp->sd_ref == REF_REL_NEED) &&
			    (sdp->sd_sym->st_shndx != SHN_UNDEF)))
				sdp->sd_flags |=
				    (FLG_SY_LOCAL | FLG_SY_REDUCED);
		} else
			reld.rel_sname = (const char *)"<unknown>";

		DBG_CALL(Dbg_reloc_in(reloc, reld.rel_sname, rsect));

		/*
		 * If for some reason we have a null relocation record issue a
		 * warning and continue (the compiler folks can get into this
		 * state some time).  Normal users should never see this error.
		 */
		if (rtype == M_R_NONE) {
			eprintf(ERR_WARNING, Errmsg_nrr,
				rsect->is_file->ifl_name, rsect->is_name);
			continue;
		}

		/*
		 * If we have a relocation against a section that will
		 * not be in the output file (has been stripped) then
		 * ignore the relocation.
		 */
		if ((sdp->sd_isc == 0) &&
		    (ELF_ST_TYPE(sdp->sd_sym->st_info) == STT_SECTION))
			continue;

		/*
		 * Validate the symbol reference.
		 */
		if ((rstndx == 0) || (rstndx >= rsect->is_file->ifl_symscnt)) {
			eprintf(ERR_FATAL, Errmsg_raus,
				rsect->is_file->ifl_name, rsect->is_name,
				roffset, rstndx, conv_reloc_type_str(rtype));
			return (S_ERROR);
		}

		/*
		 * Indicate that this symbol is being used for relocation and
		 * therefore it must have its output address updated
		 * accordingly (refer to update_osym()).
		 */
		sdp->sd_flags |= FLG_SY_UPREQD;

		reld.rel_sym = reld.rel_usym = sdp;
		reld.rel_rtype = rtype;

		/*
		 * For all symbols check if this symbol is actually an alias to
		 * another symbol.  If so, and the symbol we are aliased to is
		 * not REF_DYN_SEEN then we will set rel_usym to point to the
		 * weaks strong counter-part.
		 * The one exception is if the FLG_SY_MVTOCOMM flag is set on
		 * the WEAK symbol.  If this is the case then the strong is only
		 * here because of its promotion and we should still use the
		 * WEAK for the relocation reference (see reloc_exec()).
		 */
		sap = sdp->sd_aux;
		if (sap && sap->sa_linkndx &&
		    ((ELF_ST_BIND(sdp->sd_sym->st_info) == STB_WEAK) ||
		    (sdp->sd_flags & FLG_SY_WEAKDEF)) &&
		    (!(sdp->sd_flags & FLG_SY_MVTOCOMM))) {
			Sym_desc *	_sdp;

			_sdp = sdp->sd_file->ifl_oldndx[sap->sa_linkndx];
			if (_sdp->sd_ref != REF_DYN_SEEN)
				reld.rel_usym = _sdp;
		}

		/*
		 * Determine whether this symbol should be bound locally or not.
		 * Symbols will be bound locally if one of the following is
		 * true:
		 *
		 *  o	the symbol is of type STB_LOCAL
		 *
		 *  o	the output image is not a relocatable object and the
		 *	relocation is relative to the .got
		 *
		 *  o	the symbol has been reduced (scoped to a local or
		 *	symbolic) and reductions are being processed
		 *
		 *  o	the -Bsymbolic flag is in use when building a shared
		 *	object or an executable (fixed address) is being created
		 *
		 *  o	the relocation is against a segment which will not
		 *	be loaded into memory.  If that is the case we need
		 *	to resolve the relocation now because ld.so.1 won't
		 *	be able to.
		 */
		if (ELF_ST_BIND(sdp->sd_sym->st_info) == STB_LOCAL)
			local = TRUE;
		else if (!(reld.rel_flags & FLG_REL_LOAD))
			local = TRUE;
		else if (sdp->sd_sym->st_shndx != SHN_UNDEF) {
			if (!(flags & FLG_OF_RELOBJ) && IS_GOT_PC(rtype))
				local = TRUE;
			else if (sdp->sd_ref == REF_REL_NEED) {
				if ((flags & FLG_OF_PROCRED) && (sdp->sd_flags &
				    (FLG_SY_LOCAL | FLG_SY_SYMBOLIC)))
					local = TRUE;
				else if (flags &
				    (FLG_OF_SYMBOLIC | FLG_OF_EXEC))
					local = TRUE;
			}
		}

		/*
		 * Select the relocation to perform.
		 */
		if (flags & FLG_OF_RELOBJ) {
			if (reloc_relobj(local, &reld, reloc, ofl) == S_ERROR)
				return (S_ERROR);
			continue;
		} else if (IS_GOT_RELATIVE(rtype)) {
			if (reloc_GOT_relative(local, &reld, reloc, ofl) ==
			    S_ERROR)
				return (S_ERROR);
			continue;
		} else if (local) {
			if (reloc_local(&reld, reloc, ofl) == S_ERROR)
				return (S_ERROR);
			continue;
		} else if (IS_PLT(rtype)) {
			if (reloc_plt(&reld, reloc, ofl) == S_ERROR)
				return (S_ERROR);
			continue;
		} else if ((sdp->sd_ref == REF_REL_NEED) ||
		    (flags & FLG_OF_BFLAG) || (flags & FLG_OF_SHAROBJ) ||
		    (ELF_ST_TYPE(sdp->sd_sym->st_info) == STT_NOTYPE)) {
			if (add_outrel(NULL, &reld, reloc, ofl) ==
			    S_ERROR)
				return (S_ERROR);
			continue;
		} else if (sdp->sd_ref == REF_DYN_NEED) {
			if (reloc_exec(&reld, reloc, ofl) == S_ERROR)
				return (S_ERROR);
			continue;
		} else {
			/*
			 * IS_NOT_REL(rtype)
			 */
			if (reloc_generic(&reld, reloc, ofl) == S_ERROR)
				return (S_ERROR);
			continue;
		}
	}
	return (1);
}


static int
reloc_segments(int wr_flag, Ofl_desc * ofl)
{
	Listnode *	lnp1;
	Sg_desc	*	sgp;

	for (LIST_TRAVERSE(&ofl->ofl_segs, lnp1, sgp)) {
		Os_desc *	osp;
		Listnode *	lnp2;

		if ((sgp->sg_phdr.p_flags & PF_W) != wr_flag)
			continue;

		for (LIST_TRAVERSE(&(sgp->sg_osdescs), lnp2, osp)) {
			Is_desc *	risp;
			Listnode *	lnp3;

			osp->os_szoutrels = 0;
			for (LIST_TRAVERSE(&(osp->os_relisdescs), lnp3, risp)) {
				Word		indx;
				Is_desc *	isp;

				/*
				 * Determine the input section that this
				 * relocation information refers to.
				 */
				indx = risp->is_shdr->sh_info;
				isp = risp->is_file->ifl_isdesc[indx];

				if (process_reloc(ofl, isp, risp, osp) ==
				    S_ERROR)
					return (S_ERROR);
			}
			ofl->ofl_relocsz += osp->os_szoutrels;


			/*
			 * Create output relocation section if necessary.
			 */
			if (osp->os_szoutrels) {
				if (make_reloc(ofl, osp) == S_ERROR)
					return (S_ERROR);

				/*
				 * Check for relocations against non-writable
				 * allocatable sections.
				 */
				if ((osp->os_shdr->sh_flags &
				    (SHF_ALLOC | SHF_WRITE)) == SHF_ALLOC)
					ofl->ofl_flags |= FLG_OF_TEXTREL;
			}
		}
	}
	return (1);
}


/*
 * Count the number of output relocation entries and global offset table or
 * procedure linkage table entries.  This function searches the segment and
 * outsect lists and passes each input reloc section to process_reloc().
 * It allocates space for any output relocations needed.  And builds up
 * the relocation structures for latter processing.
 */
int
reloc_init(Ofl_desc * ofl)
{
	/*
	 * At this point we have finished processing all input symbols.  Make
	 * sure we add any absolute (internal) symbols before continuing with
	 * any relocation processing.
	 */
	if (sym_spec(ofl) == S_ERROR)
		return (S_ERROR);

	ofl->ofl_gotcnt = M_GOT_XNumber;
	ofl->ofl_pltcnt = M_PLT_XNumber;

	/*
	 * We first process all of the relocations against NON-Writeable
	 * segments and then we do them against the writeable segments.
	 *
	 * This is so that when we process the writeable segments we
	 * know whether or not a COPYRELOC will be produced for any
	 * symbols.  If we don't do relocations in this order we
	 * might produce both a COPYRELOC and a regular relocation
	 * against the same symbol, the regular relocation is redundant.
	 */
	if (reloc_segments(0, ofl) == S_ERROR)
		return (S_ERROR);

	if (reloc_segments(PF_W, ofl) == S_ERROR)
		return (S_ERROR);

	/*
	 * Calculate the total relocation size (to be stored in .dynamic).
	 */
	ofl->ofl_relocsz += ofl->ofl_relocgotsz + ofl->ofl_relocpltsz +
		ofl->ofl_relocbsssz;

	/*
	 * Make the .got section.
	 */
	if ((ofl->ofl_flags & FLG_OF_BLDGOT) ||
	    (ofl->ofl_gotcnt != M_GOT_XNumber) ||
	    (sym_find(Got_sym, SYM_NOHASH, ofl) != 0) ||
	    (sym_find(Got_usym, SYM_NOHASH, ofl) != 0)) {
		if (make_got(ofl) == S_ERROR)
			return (S_ERROR);

#if defined(sparc)
		if (allocate_got(ofl) == S_ERROR)
			return (S_ERROR);
#endif
	}
	return (1);
}

/*
 * Simple comparison routine to be used by qsort() for
 * the sorting of the output relocation list.
 */
static int
reloc_compare(Reloc_list * i, Reloc_list * j)
{
	return ((unsigned int)i->rl_key - (unsigned int)j->rl_key);
}


static int
do_sorted_outrelocs(Ofl_desc * ofl)
{
	Rel_desc *	orsp;
	Rel_cache *	rcp;
	Listnode *	lnp;
	Reloc_list *	sorted_list;
	int		index = 0;

	if ((sorted_list = malloc(sizeof (Reloc_list) *
	    ofl->ofl_reloccnt)) == NULL)
		return (S_ERROR);

	DBG_CALL(Dbg_reloc_dooutrel());

	/*
	 * All but the PLT output relocations are sorted in the
	 * output file based upon their sym_desc.  By
	 * doing this when an executable is later relocated
	 * by ld.so.1 it will take advanage of the symbol
	 * cache that ld.so.1 has.  This will decrease the
	 * loadtime of an executable do to its relocation
	 * processing of it and it's dependent shared objects.
	 *
	 * PLT relocations are not sorted because the order
	 * of the PLT relocations is used by ld.so.1 to determine
	 * what symbol a PLT relocation is against.
	 */
	for (LIST_TRAVERSE(&ofl->ofl_outrels, lnp, rcp)) {
		for (orsp = (Rel_desc *)(rcp + 1);
		    orsp < rcp->rc_free; orsp++) {
			/*
			 * If it's a PLT relocation we output it
			 * now in the order that it was originally
			 * processed in.
			 */
			if (orsp->rel_flags & FLG_REL_PLT) {
				perform_outreloc(orsp, ofl);
			} else {
				sorted_list[index].rl_key = orsp->rel_sym;
				sorted_list[index++].rl_rsp = orsp;
			}
		}
	}

	qsort(sorted_list, ofl->ofl_reloccnt, sizeof (Reloc_list),
		(int (*)(const void *, const void *))reloc_compare);

	/*
	 * All output relocations have now been sorted, go through
	 * and process each relocation.
	 */
	for (index = 0; index < ofl->ofl_reloccnt; index++)
		perform_outreloc(sorted_list[index].rl_rsp, ofl);

	return (1);
}

/*
 * Process relocations.  Finds every input relocation section for each output
 * section and invokes reloc_sec() to relocate that section.
 */
int
reloc_process(Ofl_desc * ofl)
{
	Listnode *	lnp1;
	Sg_desc *	sgp;
	unsigned	ndx = 0;
	Word		flags = ofl->ofl_flags;

	/*
	 * Determine the index of the symbol table that will be referenced by
	 * the relocation entries.
	 */
	if (flags & FLG_OF_DYNAMIC)
		ndx = elf_ndxscn(ofl->ofl_osdynsym->os_scn);
	else if (!(flags & FLG_OF_STRIP) || (flags & FLG_OF_RELOBJ))
		ndx = elf_ndxscn(ofl->ofl_ossymtab->os_scn);

	/*
	 * Re-initialize counters. These are used to provide relocation
	 * offsets within the output buffers.
	 */
	ofl->ofl_relocpltsz = 0;
	ofl->ofl_relocgotsz = 0;
	ofl->ofl_relocbsssz = 0;

	/*
	 * Now that we have created out output file and updated all of
	 * the symbols in the symbol table we can now process the
	 * relocations that were built up in process_reloc()
	 */
	if (do_sorted_outrelocs(ofl) == S_ERROR)
		return (S_ERROR);
	if (do_activerelocs(ofl) == S_ERROR)
		return (S_ERROR);

	/*
	 * Process the relocation sections:
	 *
	 *  o	for each relocation section generated for the output image
	 *	update its shdr information to reflect the symbol table it
	 *	needs (sh_link) and the section to which the relocation must
	 *	be applied (sh_info).
	 */
	for (LIST_TRAVERSE(&ofl->ofl_segs, lnp1, sgp)) {
		Listnode *	lnp2;
		Os_desc *	osp;

		for (LIST_TRAVERSE(&(sgp->sg_osdescs), lnp2, osp)) {
			if (osp->os_relosdesc) {
				Shdr *	shdr = osp->os_relosdesc->os_shdr;

				shdr->sh_link = ndx;
				shdr->sh_info = elf_ndxscn(osp->os_scn);

			}
		}
	}

	/*
	 * If the -z text option was given, and we have output relocations
	 * against a non-writable, allocatable section, issue a diagnostic and
	 * return (the actual entries that caused this error would have been
	 * output during the relocating section phase).
	 */
	if ((flags & (FLG_OF_PURETXT | FLG_OF_TEXTREL)) ==
	    (FLG_OF_PURETXT | FLG_OF_TEXTREL)) {
		eprintf(ERR_FATAL, Errmsg_rrnw);
		return (S_ERROR);
	}

	/*
	 * Finally, initialize the first got entry with the address of the
	 * .dynamic section (_DYNAMIC).
	 */
	if (flags & FLG_OF_DYNAMIC)
		fillin_gotplt1(ofl);

	return (1);
}

/*
 * If the -z text option was given, and we have output relocations against a
 * non-writable, allocatable section, issue a diagnostic. Print offending
 * symbols in tabular form similar to the way undefined symbols are presented.
 * Called from reloc_count().  The actual fatal error condition is triggered on
 * in reloc_process() above.
 *
 * Note.  For historic reasons -ztext is not a default option (however all OS
 * shared object builds use this option).  It can be argued that this option
 * should also be default when generating an a.out (see 1163979).  However, if
 * an a.out contains text relocations it is either because the user is creating
 * something pretty weird (they've used the -b or -znodefs options), or because
 * the library against which they're building wasn't constructed correctly (ie.
 * a function has a NOTYPE type, in which case the a.out won't generate an
 * associated plt).  In the latter case the builder of the a.out can't do
 * anything to fix the error - thus we've chosen not to give the user an error,
 * or warning, for this case.
 */
static Boolean	reloc_title = TRUE;

static void
reloc_remain_title()
{
	eprintf(ERR_NONE, "Text relocation remains\t\t\t      referenced");
	eprintf(ERR_NONE, "    against symbol\t\t    offset\tin file");

	reloc_title = FALSE;
}

void
reloc_remain_entry(Rel_desc * orsp, Os_desc * osp, Ofl_desc * ofl)
{
	/*
	 * Determine if this relocation is against a allocable, read-only
	 * section.
	 */
	if ((ofl->ofl_flags & FLG_OF_PURETXT) &&
	    ((osp->os_shdr->sh_flags & (SHF_ALLOC | SHF_WRITE)) == SHF_ALLOC)) {

		if (reloc_title)
			reloc_remain_title();

		eprintf(ERR_NONE, "%-35s 0x%-8x\t%s", orsp->rel_sname,
		    (int)orsp->rel_roffset, orsp->rel_fname);
	}
}
