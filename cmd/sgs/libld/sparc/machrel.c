/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991,1992,1993,1994,1995 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)machrel.c	1.41	95/08/01 SMI"

/* LINTLIBRARY */

#include	<string.h>
#include	<sys/elf_SPARC.h>
#include	"debug.h"
#include	"_libld.h"

/*
 *	Local Variable Definitions
 */
static Word negative_got_offset = 0;
				/* offset of GOT table from GOT symbol */
static Word countSmallGOT = M_GOT_XNumber;
				/* number of small GOT symbols */
Word	orels;			/* counter for output relocations */


/*
 *	Build a single P.L.T. entry - code is:
 *
 *	sethi	(. - .L0), %g1
 *	ba,a	.L0
 *	sethi	0, %g0		(nop)
 *
 *	.L0 is the address of the first entry in the P.L.T.
 *	This one is filled in by the run-time link editor. We just
 *	have to leave space for it.
 */
static void
plt_entry(Ofl_desc * ofl, Word pltndx)
{
	unsigned char *	pltent;	/* PLT entry being created. */
	Sword		pltoff;	/* Offset of this entry from PLT top */

	pltoff = pltndx * M_PLT_ENTSIZE;
	pltent = (unsigned char *)ofl->ofl_osplt->os_outdata->d_buf + pltoff;

	/*
	 * PLT[0]: sethi %hi(. - .L0), %g0
	 */
	/* LINTED */
	*(Word *)pltent = M_SETHIG1 | pltoff;

	/*
	 * PLT[1]: ba,a .L0 (.L0 accessed as a PC-relative index of longwords)
	 */
	pltent += M_PLT_INSSIZE;
	pltoff += M_PLT_INSSIZE;
	pltoff = -pltoff;
	/* LINTED */
	*(Word *)pltent = M_BA_A | ((pltoff >> 2) & S_MASK(22));

	/*
	 * PLT[2]: sethi 0, %g0 (NOP for delay slot of eventual CTI).
	 */
	pltent += M_PLT_INSSIZE;
	/* LINTED */
	*(Word *)pltent = M_SETHIG0;

	/*
	 * PLT[3]: sethi 0, %g0 (NOP for PLT padding).
	 */
	pltent += M_PLT_INSSIZE;
	/* LINTED */
	*(Word *)pltent = M_SETHIG0;
}


void
perform_outreloc(Rel_desc * orsp, Ofl_desc * ofl)
{
	Os_desc *	osp;	/* output section */
	Word		ndx;	/* sym & scn index */
	Word *		rloc;	/* points to relocsz for */
				/* output section */
	Word		roffset; /* roffset for output rel */
	Word		value;
	Sword		raddend; /* raddend for output rel */
	Rel		rea;	/* SHT_RELA entry. */
	char *		relbits;
	Sym_desc *	sdp;	/* current relocation sym */

	raddend = orsp->rel_raddend;
	sdp = orsp->rel_sym;
	/*
	 * If this is a relocation against a section then we
	 * need to adjust the raddend field to compensate
	 * for the new position of the input section within
	 * the new output section.
	 */
	if (ELF32_ST_TYPE(sdp->sd_sym->st_info) == STT_SECTION) {
		raddend += sdp->sd_isc->is_indata->d_off;
		if (sdp->sd_isc->is_shdr->sh_flags & SHF_ALLOC)
			raddend += sdp->sd_isc->is_osdesc->os_shdr->sh_addr;
	}

	value = sdp->sd_sym->st_value;

	if (orsp->rel_flags & FLG_REL_GOT) {
		osp = ofl->ofl_osgot;
		roffset = (Word) (osp->os_shdr->sh_addr) +
		    (-negative_got_offset * M_GOT_ENTSIZE) + (sdp->sd_GOTndx *
		    M_GOT_ENTSIZE);
		rloc = &ofl->ofl_relocgotsz;
	} else if (orsp->rel_flags & FLG_REL_PLT) {
		osp = ofl->ofl_osplt;
		roffset = (Word) (osp->os_shdr->sh_addr) +
		    (sdp->sd_aux->sa_PLTndx * M_PLT_ENTSIZE);
		plt_entry(ofl, sdp->sd_aux->sa_PLTndx);
		rloc = &ofl->ofl_relocpltsz;
	} else if (orsp->rel_flags & FLG_REL_BSS) {
		/*
		 * this must be a R_SPARC_COPY - for those
		 * we also set the roffset to point to the
		 * new symbols location.
		 *
		 */
		osp = ofl->ofl_isbss->is_osdesc;
		roffset = (Word)value;
		rloc = &ofl->ofl_relocbsssz;
		/*
		 * the raddend doesn't mean anything in an
		 * R_SPARC_COPY relocation.  We will null
		 * it out because it can be confusing to
		 * people.
		 */
		raddend = 0;
	} else {
		osp = orsp->rel_osdesc;
		/*
		 * Calculate virtual offset of reference point;
		 * equals offset into section + vaddr of section
		 * for loadable sections, or offset plus
		 * section displacement for nonloadable
		 * sections.
		 */
		roffset = orsp->rel_roffset +
		    orsp->rel_isdesc->is_indata->d_off;
		if (!(ofl->ofl_flags & FLG_OF_RELOBJ))
			roffset += orsp->rel_isdesc->is_osdesc->
			    os_shdr->sh_addr;
		rloc = &osp->os_szoutrels;
	}


	/*
	 * assign the symbols index for the output
	 * relocation.  If the relocation refers to a
	 * SECTION symbol then it's index is based upon
	 * the output sections symbols index.  Otherwise
	 * the index can be derived from the symbols index
	 * itself.
	 */
	if ((orsp->rel_flags & FLG_REL_SCNNDX) ||
	    (ELF_ST_TYPE(sdp->sd_sym->st_info) == STT_SECTION)) {
		ndx = sdp->sd_isc->is_osdesc->os_scnsymndx;
	} else if (orsp->rel_rtype == R_SPARC_RELATIVE)
		ndx = STN_UNDEF;
	else
		ndx = sdp->sd_symndx;

	/*
	 * Add the symbols 'value' to the addend field.
	 */
	if (orsp->rel_flags & FLG_REL_ADVAL)
		raddend += value;

	relbits = (char *)osp->os_relosdesc->os_outdata->d_buf;

	rea.r_info = ELF32_R_INFO(ndx, orsp->rel_rtype);
	rea.r_offset = roffset;
	rea.r_addend = raddend;
	DBG_CALL(Dbg_reloc_out(&rea, orsp->rel_sname, osp));

	(void) memcpy((relbits + *rloc), (char *)&rea, sizeof (Rel));
	*rloc += sizeof (Rel);

	/*
	 * Determine whether this relocation is against a
	 * non-writeable, allocatable section.  If so we may
	 * need to provide a text relocation diagnostic.
	 */
	reloc_remain_entry(orsp, osp, ofl);
}


int
do_activerelocs(Ofl_desc *ofl)
{
	Rel_desc *	arsp;
	Rel_cache *	rcp;
	Listnode *	lnp;
	int		return_code = 1;


	DBG_CALL(Dbg_reloc_doactiverel());
	/*
	 * process active relocs
	 */
	for (LIST_TRAVERSE(&ofl->ofl_actrels, lnp, rcp)) {
		for (arsp = (Rel_desc *)(rcp + 1);
		    arsp < rcp->rc_free; arsp++) {
			unsigned char *	addr;
			Word		value;
			Sym_desc *	sdp;
			Word		refaddr = arsp->rel_roffset +
					    arsp->rel_isdesc->is_indata->d_off;
			sdp = arsp->rel_sym;

			if ((arsp->rel_flags & FLG_REL_CLVAL) ||
			    (arsp->rel_flags & FLG_REL_GOTCL))
				value = 0;
			else if (ELF_ST_TYPE(sdp->sd_sym->st_info) ==
			    STT_SECTION) {
				/*
				 * The value for a symbol pointing to a SECTION
				 * is based off of that sections position.
				 */
				value = sdp->sd_isc->is_indata->d_off;
				if (sdp->sd_isc->is_shdr->sh_flags & SHF_ALLOC)
					value += sdp->sd_isc->is_osdesc->
					    os_shdr->sh_addr;
			} else
				/*
				 * else the value is the symbols value
				 */
				value = sdp->sd_sym->st_value;

			/*
			 * relocation against the GLOBAL_OFFSET_TABLE
			 */
			if (arsp->rel_flags & FLG_REL_GOT)
				arsp->rel_osdesc = ofl->ofl_osgot;

			/*
			 * If loadable and not producing a relocatable object
			 * add the sections virtual address to the reference
			 * address.
			 */
			if ((arsp->rel_flags & FLG_REL_LOAD) &&
			    !(ofl->ofl_flags & FLG_OF_RELOBJ))
				refaddr += arsp->rel_isdesc->is_osdesc->
				    os_shdr->sh_addr;

			/*
			 * If this entry has a PLT assigned to it, it's
			 * value is actually the address of the PLT (and
			 * not the address of the function).
			 */
			if (IS_PLT(arsp->rel_rtype)) {
				if (sdp->sd_aux && sdp->sd_aux->sa_PLTndx)
					value = (Word)(ofl->ofl_osplt->os_shdr->
					    sh_addr) + (sdp->sd_aux->sa_PLTndx *
					    M_PLT_ENTSIZE);
			}

			if (arsp->rel_flags & FLG_REL_GOT) {
				Word	R1addr;
				Word	R2addr;
				/*
				 * Clear the GOT table entry, on SPARC
				 * we clear the entry and the 'value' if
				 * needed is stored in an output relocations
				 * addend.
				 */

				/*
				 * calculate offset into GOT at which to apply
				 * the relocation.
				 */
				R1addr = (Word)((char *)(-negative_got_offset *
				    M_GOT_ENTSIZE) + (sdp->sd_GOTndx
				    * M_GOT_ENTSIZE));
				/*
				 * add the GOTs data's offset
				 */
				R2addr = R1addr + (Word)
				    arsp->rel_osdesc->os_outdata->d_buf;

				DBG_CALL(Dbg_reloc_doact(arsp->rel_rtype,
				    R1addr, value, arsp->rel_sname,
				    arsp->rel_osdesc));

				/*
				 * and do it.
				 */
				*(Word *)R2addr = value;
				continue;
			} else if (IS_PC_RELATIVE(arsp->rel_rtype)) {
				value -= refaddr;
			} else if (IS_GOT_RELATIVE(arsp->rel_rtype))
				value = sdp->sd_GOTndx * M_GOT_ENTSIZE;

			/*
			 * add relocations addend to value.
			 */
			value += arsp->rel_raddend;

			/*
			 * Make sure we have data to relocate. Our compiler
			 * and assembler developers have been known
			 * to generate relocations against invaild sections
			 * (normally .bss), so for their benefit give
			 * them sufficient information to help analyze
			 * the problem.  End users should probably
			 * never see this.
			 */
			if (arsp->rel_isdesc->is_indata->d_buf == 0) {
				eprintf(ERR_FATAL, Errmsg_ares,
					arsp->rel_isdesc->is_file->ifl_name,
					conv_reloc_type_str(arsp->rel_rtype),
					arsp->rel_isdesc->is_name,
					arsp->rel_sname);
				return (S_ERROR);
			}

			/*
			 * Get the address of the data item we need to modify.
			 */
			addr = (unsigned char *)arsp->rel_isdesc->
				is_indata->d_off + arsp->rel_roffset;
			DBG_CALL(Dbg_reloc_doact(arsp->rel_rtype, (Word)addr,
				value, arsp->rel_sname, arsp->rel_osdesc));
			addr += (int)arsp->rel_osdesc->os_outdata->d_buf;
			if (do_reloc(arsp->rel_rtype, addr, &value,
			    arsp->rel_sname,
			    arsp->rel_isdesc->is_file->ifl_name) == 0)
				return_code = S_ERROR;
		}
	}
	return (return_code);
}


int
add_outrel(Half flags, Rel_desc * rsp, Rel * rloc, Ofl_desc * ofl)
{
	Rel_desc *	orsp;
	Rel_cache *	rcp;

	/*
	 * If no relocation cache structures are available allocate
	 * a new one and link it into the cache list.
	 */
	if ((ofl->ofl_outrels.tail == 0) ||
	    ((rcp = (Rel_cache *)ofl->ofl_outrels.tail->data) == 0) ||
	    ((orsp = rcp->rc_free) == rcp->rc_end)) {
		if ((rcp = (Rel_cache *)malloc(sizeof (Rel_cache) +
		    (sizeof (Rel_desc) * REL_OIDESCNO))) == 0)
			return (S_ERROR);
		rcp->rc_free = orsp = (Rel_desc *)(rcp + 1);
		rcp->rc_end = (Rel_desc *)((int)rcp->rc_free +
				(sizeof (Rel_desc) * REL_OIDESCNO));
		if (list_append(&ofl->ofl_outrels, rcp) ==
		    (Listnode *)S_ERROR)
			return (S_ERROR);
	}

	*orsp = *rsp;
	orsp->rel_flags |= flags;
	orsp->rel_raddend = rloc->r_addend;
	orsp->rel_roffset = rloc->r_offset;
	rcp->rc_free++;

	if (flags & FLG_REL_GOT)
		ofl->ofl_relocgotsz += sizeof (Rel);
	else if (flags & FLG_REL_PLT)
		ofl->ofl_relocpltsz += sizeof (Rel);
	else if (flags & FLG_REL_BSS)
		ofl->ofl_relocbsssz += sizeof (Rel);
	else
		rsp->rel_osdesc->os_szoutrels += sizeof (Rel);

	/*
	 * We don't perform sorting on PLT relocations because
	 * they have already been assigned a PLT index and if we
	 * were to sort them we would have to re-assign the plt indexes.
	 */
	if (!(flags & FLG_REL_PLT))
		ofl->ofl_reloccnt++;

	DBG_CALL(Dbg_reloc_ors_entry(orsp));
	return (1);
}



int
add_actrel(Half flags, Rel_desc * rsp, Rel * rloc, Ofl_desc * ofl)
{
	Rel_desc * 	arsp;
	Rel_cache *	rcp;

	/*
	 * If no relocation cache structures are available allocate a
	 * new one and link it into the bucket list.
	 */
	if ((ofl->ofl_actrels.tail == 0) ||
	    ((rcp = (Rel_cache *)ofl->ofl_actrels.tail->data) == 0) ||
	    ((arsp = rcp->rc_free) == rcp->rc_end)) {
		if ((rcp = (Rel_cache *)malloc(sizeof (Rel_cache) +
			(sizeof (Rel_desc) * REL_AIDESCNO))) == 0)
				return (S_ERROR);
		rcp->rc_free = arsp = (Rel_desc *)(rcp + 1);
		rcp->rc_end = (Rel_desc *)((int)rcp->rc_free +
				(sizeof (Rel_desc) * REL_AIDESCNO));
		if (list_append(&ofl->ofl_actrels, rcp) ==
		    (Listnode *)S_ERROR)
			return (S_ERROR);
	}

	*arsp = *rsp;
	arsp->rel_flags |= flags;
	arsp->rel_raddend = rloc->r_addend;
	arsp->rel_roffset = rloc->r_offset;
	rcp->rc_free++;

	DBG_CALL(Dbg_reloc_ars_entry(arsp));
	return (1);
}



/*
 * process relocation for a LOCAL symbol
 */
int
reloc_local(Rel_desc * rsp, Rel * reloc, Ofl_desc * ofl)
{
	Word	flags = ofl->ofl_flags;

	/*
	 * If ((shaed object) and (not pc relative relocation))
	 * then
	 *	if (rtype != R_SPARC_32)
	 *	then
	 *		build relocation against section
	 *	else
	 *		build R_SPARC_RELATIVE
	 *	fi
	 * fi
	 */
	if ((flags & FLG_OF_SHAROBJ) && (rsp->rel_flags & FLG_REL_LOAD) &&
	    !(IS_PC_RELATIVE(rsp->rel_rtype))) {
		Word	ortype = rsp->rel_rtype;
		if (rsp->rel_rtype != R_SPARC_32)
			return (add_outrel(FLG_REL_SCNNDX |
			    FLG_REL_ADVAL, rsp, reloc, ofl));

		rsp->rel_rtype = R_SPARC_RELATIVE;
		if (add_outrel(FLG_REL_ADVAL, rsp, reloc, ofl) ==
		    S_ERROR)
			return (S_ERROR);
		rsp->rel_rtype = ortype;
		return (1);
	}

	if (!(rsp->rel_flags & FLG_REL_LOAD) &&
	    (rsp->rel_sym->sd_sym->st_shndx == SHN_UNDEF)) {
		(void) eprintf(ERR_WARNING, Errmsg_rnas, rsp->rel_fname,
		    rsp->rel_sname, rsp->rel_osdesc->os_name);
		return (1);
	}
	/*
	 * just do it.
	 */
	return (add_actrel(NULL, rsp, reloc, ofl));
}



int
reloc_relobj(Boolean local, Rel_desc * rsp, Rel * reloc, Ofl_desc * ofl)
{
	Word		rtype = rsp->rel_rtype;
	Sym_desc *	sdp = rsp->rel_sym;
	Is_desc *	isp = rsp->rel_isdesc;

	/*
	 * Try to determine if we can do any relocations at
	 * this point.  We can if:
	 *
	 * (local_symbol) and (non_GOT_relocation) and
	 * (IS_PC_RELATIVE()) and
	 * (relocation to symbol in same section)
	 */
	if (local && !IS_GOT_RELATIVE(rtype) && IS_PC_RELATIVE(rtype) &&
	    ((sdp->sd_isc) && (sdp->sd_isc->is_osdesc == isp->is_osdesc)))
		return (add_actrel(NULL, rsp, reloc, ofl));
	else
		return (add_outrel(NULL, rsp, reloc, ofl));
}


/*
 * allocate_got: if a GOT is to be made, after the section is built this
 * function is called to allocate all the GOT slots.  The allocation is
 * deferred until after all GOTs have been counted and sorted according
 * to their size, for only then will we know how to allocate them on
 * a processor like SPARC which has different models for addressing the
 * GOT.  SPARC has two: small and large, small uses a signed 13-bit offset
 * into the GOT, whereas large uses an unsigned 32-bit offset.
 */
static	Word small_index;	/* starting index for small GOT entries */
static	Word large_index;	/* starting index for large GOT entries */

int
assign_got(Sym_desc * sdp)
{
	switch (sdp->sd_GOTndx) {
	case M_GOT_SMALL:
		sdp->sd_GOTndx = small_index++;
		if (small_index == 0)
			small_index = M_GOT_XNumber;
		break;
	case M_GOT_LARGE:
		sdp->sd_GOTndx = large_index++;
		break;
	default:
		eprintf(ERR_FATAL, "internal error: assign_got() invalid "
		    "got index 0x%x for symbol `%s'",
		    sdp->sd_GOTndx, sdp->sd_name);
		return (S_ERROR);
	}
	return (1);
}



int
assign_got_ndx(Word rtype, int prevndx, Ofl_desc *ofl)
{
	/*
	 * If we'd previously assigned a M_GOT_LARGE index
	 * to a got index it is possible that we will need
	 * to update it's index to an M_GOT_SMALL if we
	 * find a *small* GOT relocation against the same
	 * symbol.
	 *
	 * If we've already assigned an M_GOT_SMALL to the symbol
	 * no further action needs to be taken.
	 */
	if (prevndx == 0)
		ofl->ofl_gotcnt++;
	else if (prevndx == M_GOT_SMALL)
		return (M_GOT_SMALL);

	/*
	 * because of the PIC vs. pic issue we can't assign the
	 * actual GOT index yet - instead we assign a token and
	 * track how many of each kind we have encountered.
	 *
	 * The actual index will be assinged durring update_osym().
	 */
	if (rtype == R_SPARC_GOT13) {
		countSmallGOT++;
		return (M_GOT_SMALL);
	} else
		return (M_GOT_LARGE);
}


void
assign_plt_ndx(Sym_desc * sdp, Ofl_desc *ofl)
{
	sdp->sd_aux->sa_PLTndx = ofl->ofl_pltcnt++;
}

int
allocate_got(Ofl_desc * ofl)
{
	Sym_desc *	sdp;
	Addr		addr;

	/*
	 * Sanity check -- is this going to fit at all?
	 */
	if (countSmallGOT >= M_GOT_MAXSMALL) {
		eprintf(ERR_FATAL,
		    "too many symbols require \"small\" PIC references:\n"
		    "\thave %d, maximum %d -- recompile some modules -K PIC.",
		    countSmallGOT, M_GOT_MAXSMALL);
		return (S_ERROR);
	}

	/*
	 * Set starting offset to be either 0, or a negative index into
	 * the GOT based on the number of small symbols we've got.
	 */
	negative_got_offset = countSmallGOT > (M_GOT_MAXSMALL / 2) ?
	    -(countSmallGOT - (M_GOT_MAXSMALL / 2)) : 0;

	/*
	 * Initialize the large and small got offsets (used in assign_got()).
	 */
	small_index = negative_got_offset == 0 ?
	    M_GOT_XNumber : negative_got_offset;
	large_index = negative_got_offset + countSmallGOT;

	/*
	 * Assign bias to GOT symbols.
	 */
	addr = -negative_got_offset * M_GOT_ENTSIZE;
	if (sdp = sym_find(Got_sym, SYM_NOHASH, ofl))
		sdp->sd_sym->st_value = addr;
	if (sdp = sym_find(Got_usym, SYM_NOHASH, ofl))
		sdp->sd_sym->st_value = addr;
	return (1);
}

/*
 * Initializes .got[0] with the _DYNAMIC symbol value.
 */
void
fillin_gotplt1(Ofl_desc * ofl)
{
	Sym_desc *	sdp;

	if (ofl->ofl_osgot) {
		if ((sdp = sym_find(Dyn_usym, SYM_NOHASH, ofl)) != NULL) {
			unsigned char *	genptr = ((unsigned char *)
			    ofl->ofl_osgot->os_outdata->d_buf +
			    (-negative_got_offset * M_GOT_ENTSIZE) +
			    (M_GOT_XDYNAMIC * M_GOT_ENTSIZE));
			/* LINTED */
			*((Word *)genptr) = sdp->sd_sym->st_value;
		}
	}
}

/*
 * Return plt[0].
 */
Addr
fillin_gotplt2(Ofl_desc * ofl)
{
	if (ofl->ofl_osplt)
		return (ofl->ofl_osplt->os_shdr->sh_addr);
	else
		return (0);
}
