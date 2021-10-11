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
#pragma ident	"@(#)update.c	1.47	95/06/20 SMI"

/* LINTLIBRARY */

/*
 * Update the new output file image, perform virtual address, offset and
 * displacement calculations on the program headers and sections headers,
 * and generate any new output section information.
 */
#include	<string.h>
#include	"debug.h"
#include	"_libld.h"

static const char
	* Errmsg_nsfs = "No %s segments found.  Setting `%s' to 0",
	* Errmsg_sbto = "Setting `%s' and `%s' to 0",
	* Errmsg_sovr =	"%s: segment address overlap;\n"
			"\tprevious segment ending at address %#x overlaps\n"
			"\tuser defined segment `%s' starting at address %#x",
	* Errmsg_ssiz =	"%s: segment %s calculated size %#x\n"
			"\tis larger than user-defined size %#x",
	* Errmsg_loor = "%s: %s: sh_link out of range %d, not translated",
	* Errmsg_usnv = "undefined symbol `%s' with non-zero value encountered",
	* Errmsg_nbts = "NOBITS section found before end of initialized data";


/*
 * Define global pointers into the string table data areas.  Notice that these
 * are initialized in update_osym() and are referenced in update_odynamic() and
 * update_over[s](), this makes it important that the symbol tables are done
 * before these other sections can be completed.
 */
static char *	dynstr, * _dynstr;
static char *	strtab, * _strtab;


/*
 * Build and update any output symbol tables.  Here we work on all the symbol
 * tables at once to reduce the duplication of symbol and string manipulation.
 * Symbols and their associated strings are copied from the read-only input
 * file images to the output image and their values and index's updated in the
 * output image.
 */
static Addr
update_osym(Ofl_desc * ofl)
{
	Listnode *	lnp1, * lnp2;
	Sym_desc *	sdp;
	Sg_desc *	sgp, * tsgp = 0, * dsgp = 0;
	Os_desc *	osp;
	long		bkt;
	Ifl_desc *	ifl;
	Half		bssndx, etext_ndx, edata_ndx, end_ndx;
	Addr		bssaddr, etext = 0, edata = 0, end = 0;
	Sym		_sym = {0};
	Sym *		sym, * symtab = 0, * scopetab, * dynsym = 0;
	char *		shdrstr, * _shdrstr;
	Word		symndx;		/* Symbol index (for relocation use) */
	Word		scopendx;	/* Symbol index for scoped symbols */
	Word *		hashtab;	/* hash table pointer */
	Word *		hashbkt;	/* hash table bucket pointer */
	Word *		hashchain;	/* hash table chain pointer */
	Word		hashval;	/* value of hash function */
	Word		hashndx;	/* index of first free chain slot */
	Wk_desc *	wkp;
	List		weak = {NULL, NULL};
	Word		flags = ofl->ofl_flags;
	Versym *	versym;

	/*
	 * Initialize pointers to the symbol table entries and the symbol
	 * table strings.  Skip the first symbol entry and the first string
	 * table byte.  Note that if we are not generating any output symbol
	 * tables we must still generate and update an internal copies so
	 * that the relocation phase has the correct information.
	 */
	if (!(flags & FLG_OF_STRIP) || (flags & FLG_OF_RELOBJ)) {
		symtab = (Sym *)ofl->ofl_ossymtab->os_outdata->d_buf;
		*symtab = _sym;
		symtab++;
		_strtab = ofl->ofl_osstrtab->os_outdata->d_buf;
		strtab = (char *)_strtab;
		*strtab = '\0';
		strtab++;
	}
	if (flags & FLG_OF_DYNAMIC) {
		dynsym = (Sym *)ofl->ofl_osdynsym->os_outdata->d_buf;
		*dynsym = _sym;
		dynsym++;
		_dynstr = ofl->ofl_osdynstr->os_outdata->d_buf;
		dynstr = (char *)_dynstr;
		*dynstr = '\0';
		dynstr++;

		/*
		 * Initialize the hash table.
		 */
		hashtab = (Word *)(ofl->ofl_oshash->os_outdata->d_buf);
		hashbkt = &hashtab[2];
		hashchain = &hashtab[2 + ofl->ofl_hashbkts];
		hashtab[0] = ofl->ofl_hashbkts;
		hashtab[1] = ofl->ofl_shdrcnt + ofl->ofl_globcnt + 1;
		hashndx = ofl->ofl_shdrcnt + 1;
	}

	/*
	 * If we have version definitions initialize the version symbol index
	 * table.  There is one entry for each symbol which contains the symbols
	 * version index.
	 */
	if ((flags & (FLG_OF_VERDEF | FLG_OF_NOVERSEC)) == FLG_OF_VERDEF) {
		versym = (Versym *)ofl->ofl_osversym->os_outdata->d_buf;
		versym[0] = 0;
	} else
		versym = 0;

	/*
	 * Initialize pointers to the .shstrtab table.
	 */
	_shdrstr = (char *)ofl->ofl_osshstrtab->os_outdata->d_buf;
	shdrstr = (char *)_shdrstr;
	*shdrstr = '\0';
	shdrstr++;

	/*
	 * Provide each symbol an index for use by those that must reference
	 * the symbol (ie. relocations).  If we're building a dynamic executable
	 * or shared library, all references will be to the .dynsym table,
	 * otherwise references will be to the standard .symtab table.  The
	 * first symbol table entry is null, and the second entry of the .symtab
	 * will hold the file symbol.
	 */
	symndx = dynsym ? 1 : 2;

	DBG_CALL(Dbg_syms_sec_title());

	/*
	 * Add the output file name to the first .symtab symbol.
	 */
	if (symtab) {
		symtab->st_name = strtab - _strtab;
		symtab->st_value = 0;
		symtab->st_size = 0;
		symtab->st_info = ELF_ST_INFO(STB_LOCAL, STT_FILE);
		symtab->st_other = 0;
		symtab->st_shndx = SHN_ABS;

		(void) strcpy(strtab, ofl->ofl_name);
		strtab += strlen(ofl->ofl_name) + 1;
		symtab++;

		if (versym && !dynsym)
			versym[1] = 0;
	}

	/*
	 * Traverse the program headers.  Determine the last executable segment
	 * and the last data segment so that we can update etext and edata.
	 */
	for (LIST_TRAVERSE(&ofl->ofl_segs, lnp1, sgp)) {
		Phdr *		phd = &(sgp->sg_phdr);

		if (phd->p_type == PT_LOAD) {
			if (sgp->sg_osdescs.head != NULL) {
				Word	_flags = phd->p_flags & (PF_W | PF_R);
				if (_flags == PF_R)
					tsgp = sgp;
				else if (_flags == (PF_W | PF_R))
					dsgp = sgp;
			}
		}

		/*
		 * Generate a section symbol for each output section.
		 */
		for (LIST_TRAVERSE(&(sgp->sg_osdescs), lnp2, osp)) {
			sym = &_sym;

			sym->st_value = osp->os_shdr->sh_addr;
			sym->st_info = ELF_ST_INFO(STB_LOCAL, STT_SECTION);
			sym->st_shndx = elf_ndxscn(osp->os_scn);

			if (dynsym) {
				*dynsym = *sym;
				dynsym++;
			}
			if (symtab) {
				*symtab = *sym;
				symtab++;
			}
			if (versym)
				versym[symndx] = 0;

			DBG_CALL(Dbg_syms_sec_entry(symndx, sgp, osp));
			osp->os_scnsymndx = symndx;
			symndx++;

			/*
			 * Generate the .shstrtab for this section.
			 */
			osp->os_shdr->sh_name = shdrstr - _shdrstr;
			(void) strcpy(shdrstr, osp->os_name);
			shdrstr += strlen(osp->os_name) + 1;

			/*
			 * find the section index for our
			 * special symbols.
			 */
			if (sgp == tsgp)
				etext_ndx = osp->os_scnsymndx;
			else if (dsgp == sgp) {
				if (osp->os_shdr->sh_type != SHT_NOBITS)
					edata_ndx = osp->os_scnsymndx;
			}
		}
	}

	/*
	 * Having traversed all the output segments, warn the user if the
	 * traditional text or data segments don't exist.  Otherwise from these
	 * segments establish the values for `etext', `edata' and `end'.
	 */
	if (!(flags & FLG_OF_RELOBJ)) {
		Sg_desc *	sgp;
		const char *	ro = "read-only";
		const char *	rw = "read-write";

		if (tsgp)
			etext = tsgp->sg_phdr.p_vaddr + tsgp->sg_phdr.p_filesz;
		else {
			etext = (Addr)0;
			etext_ndx = SHN_ABS;
			eprintf(ERR_WARNING, Errmsg_nsfs, ro, Etext_usym);
		}
		if (dsgp) {
			edata = dsgp->sg_phdr.p_vaddr + dsgp->sg_phdr.p_filesz;
		} else {
			edata = (Addr)0;
			edata_ndx = SHN_ABS;
			eprintf(ERR_WARNING, Errmsg_nsfs, rw, Edata_usym);
		}
		if (dsgp == 0) {
			if (tsgp)
				sgp = tsgp;
			else
				sgp = 0;
		} else if (tsgp == 0)
			sgp = dsgp;
		else if (dsgp->sg_phdr.p_vaddr > tsgp->sg_phdr.p_vaddr)
			sgp = dsgp;
		else if (dsgp->sg_phdr.p_vaddr < tsgp->sg_phdr.p_vaddr)
			sgp = tsgp;
		else {
			/*
			 * One of the segments must be of zero size.
			 */
			if (tsgp->sg_phdr.p_memsz)
				sgp = tsgp;
			else
				sgp = dsgp;
		}

		if (sgp) {
			Os_desc *	tosp;
			end = sgp->sg_phdr.p_vaddr + sgp->sg_phdr.p_memsz;
			tosp = (Os_desc *)sgp->sg_osdescs.tail->data;
			end_ndx = tosp->os_scnsymndx;
		} else {
			end = (Addr) 0;
			end_ndx = SHN_ABS;
			eprintf(ERR_WARNING,  Errmsg_sbto, End_sym, End_usym);
		}
	}

	DBG_CALL(Dbg_syms_up_title());

	/*
	 * Initialize the scoped symbol table entry point.  This is for all
	 * the global symbols that have been scoped to locals and will be
	 * filled in during global symbol processing so that we don't have
	 * to traverse the globals symbol hash array more than once.
	 */
	if (symtab) {
		scopetab = symtab;
		symtab += ofl->ofl_scopecnt;
		if (!dynsym) {
			scopendx = symndx;
			symndx += ofl->ofl_scopecnt;
		}
	}

	/*
	 * If we are generating a .symtab collect all the local symbols,
	 * assigning a new virtual address or displacement (value).
	 */
	for (LIST_TRAVERSE(&ofl->ofl_objs, lnp1, ifl)) {
		Word		lndx;
		int		local;
		Is_desc *	isc;

		/*
		 * Check that we have local symbols to process.  If the user
		 * has indicated scoping then scan the global symbols also
		 * looking for entries from this file to reduce to locals.
		 */
		if ((local = ifl->ifl_locscnt) == 0)
			continue;

		for (lndx = 1; lndx < local; lndx++) {
			unsigned char	type;

			sdp = ifl->ifl_oldndx[lndx];
			sym = sdp->sd_sym;

			/*
			 * Assign a got offset if necessary.
			 */
#if defined(sparc) || defined(__ppc)
			if (sdp->sd_GOTndx) {
				if (assign_got(sdp) == S_ERROR)
					return (S_ERROR);
			}
#elif defined(i386)
/* nothing to do */
#else
#error Unknown architecture!
#endif

			if ((type = ELF_ST_TYPE(sym->st_info)) == STT_SECTION)
				continue;

			if (!dynsym)
				sdp->sd_symndx = symndx++;

			/*
			 * Generate an output symbol to represent this input
			 * symbol.  Even if the symbol table is to be stripped
			 * we still need to update any local symbols that are
			 * used during relocation.
			 */
			if (symtab) {
				*symtab = *sym;
				symtab->st_name = strtab - _strtab;
				(void) strcpy(strtab, sdp->sd_name);
				strtab += strlen(sdp->sd_name) + 1;

				sdp->sd_flags &= ~FLG_SY_CLEAN;
				sdp->sd_sym = sym = symtab++;

				if (sym->st_shndx == SHN_ABS)
					continue;
			} else {
				if (!(sdp->sd_flags & FLG_SY_UPREQD))
					continue;
				if (sym->st_shndx == SHN_ABS)
					continue;

				if (sym_copy(sdp) == S_ERROR)
					return ((Addr)S_ERROR);
				sym = sdp->sd_sym;
			}

			/*
			 * Update the symbols contents if necessary.
			 */
			if (type == STT_FILE) {
				sym->st_shndx = SHN_ABS;
				continue;
			}

			/*
			 * If this isn't an UNDEF symbol (ie. an input section
			 * is associated), update the symbols value and index.
			 */
			if ((isc = sdp->sd_isc) != 0) {
				osp = isc->is_osdesc;
				sym->st_value += isc->is_indata->d_off;
				if (!(flags & FLG_OF_RELOBJ))
					sym->st_value += osp->os_shdr->sh_addr;
				sym->st_shndx = elf_ndxscn(osp->os_scn);
			}
		}
	}

	/*
	 * Two special symbols are `_init' and `_fini'.  These are supplied
	 * by crti.o and are used to represent the total concatenation of the
	 * `.init' and `.fini' sections.  Determine the size of these sections
	 * and updated the symbols value accordingly.
	 */
	if (((sdp = sym_find(Init_sym, SYM_NOHASH, ofl)) != NULL) &&
	    (sdp->sd_ref == REF_REL_NEED) && sdp->sd_isc) {
		if (sym_copy(sdp) == S_ERROR)
			return ((Addr)S_ERROR);
		sdp->sd_sym->st_size =
			sdp->sd_isc->is_osdesc->os_shdr->sh_size;
	}
	if (((sdp = sym_find(Fini_sym, SYM_NOHASH, ofl)) != NULL) &&
	    (sdp->sd_ref == REF_REL_NEED) && sdp->sd_isc) {
		if (sym_copy(sdp) == S_ERROR)
			return ((Addr)S_ERROR);
		sdp->sd_sym->st_size =
			sdp->sd_isc->is_osdesc->os_shdr->sh_size;
	}

	/*
	 * Assign .bss information for use with updating COMMON symbols.
	 */
	if (ofl->ofl_isbss) {
		osp = ofl->ofl_isbss->is_osdesc;
		bssaddr = osp->os_shdr->sh_addr +
			ofl->ofl_isbss->is_indata->d_off;
		bssndx = elf_ndxscn(osp->os_scn);
	}

	/*
	 * Traverse the internal symbol table updating information and
	 * allocating common.
	 */
	for (bkt = 0; bkt < ofl->ofl_symbktcnt; bkt++) {
		Sym_cache *	scp;

		for (scp = ofl->ofl_symbkt[bkt]; scp; scp = scp->sc_next) {
			for (sdp = (Sym_desc *)(scp + 1);
			    sdp < scp->sc_free; sdp++) {
				const char *	name;
				Sym *		sym;
				Sym_aux *	sap;
				Half		spec;
				int		len;
				int		local = 0;

				/*
				 * Assign a got offset if necessary.
				 */
#if defined(sparc) || defined(__ppc)
				if (sdp->sd_GOTndx) {
					if (assign_got(sdp) == S_ERROR)
						return (S_ERROR);
				}
#elif defined(i386)
/* nothing to do */
#else
#error Unknown architecture!
#endif

				/*
				 * Only needed symbols will be copied to the
				 * output symbol table.
				 */
				if (sdp->sd_ref == REF_DYN_SEEN)
					continue;

				/*
				 * If this symbol has been marked as being
				 * reduced to local scope then it will have to
				 * be placed in the scoped portion of the
				 * .symtab.  Retain the appropriate index for
				 * use in version symbol indexing and
				 * relocation.
				 */
				if ((sdp->sd_flags & FLG_SY_LOCAL) &&
				    (flags & FLG_OF_PROCRED)) {
					local = 1;
					if (dynsym)
						sdp->sd_symndx = 0;
					else
						sdp->sd_symndx = scopendx++;
				} else
					sdp->sd_symndx = symndx++;

				/*
				 * Copy basic symbol and string information.
				 */
				name = sdp->sd_name;
				len = strlen(name) + 1;
				sap = sdp->sd_aux;

				/*
				 * If we require to record version symbol
				 * indexes, update the associated version symbol
				 * information for all defined symbols.  If
				 * a version definition is required any zero
				 * value symbol indexes would have been flagged
				 * as undefined symbol errors, however if we're
				 * just scoping these need to fall into the base
				 * of global symbols.
				 */
				if (sdp->sd_symndx && versym) {
					Half	vndx = 0;

					if (sdp->sd_flags & FLG_SY_MVTOCOMM)
						vndx = VER_NDX_GLOBAL;
					else if (sdp->sd_ref == REF_REL_NEED) {
						vndx = sap->sa_verndx;

						if ((vndx == 0) &&
						    (sdp->sd_sym->st_shndx !=
						    SHN_UNDEF) &&
						    (!(sdp->sd_flags &
						    FLG_SY_LOCAL)))
							vndx = VER_NDX_GLOBAL;
					}
					versym[sdp->sd_symndx] = vndx;
				}

				/*
				 * Note that the `sym' value is reset to be one
				 * of the new symbol table entries.  This symbol
				 * will be updated further depending on the type
				 * of the symbol.  Process the .symtab first,
				 * followed by the .dynsym, thus the `sym' value
				 * will remain as the .dynsym value when the
				 * .dynsym is present.  This insures that any
				 * versioning symbols st_name value will be
				 * appropriate for the string table used to
				 * by version entries.
				 */
				if (symtab) {
					int	_name = strtab - _strtab;

					if (local) {
						*scopetab = *sdp->sd_sym;
						scopetab->st_name = _name;
						sym = scopetab;
					} else {
						*symtab = *sdp->sd_sym;
						symtab->st_name = _name;
						sym = symtab;
					}
					sdp->sd_sym = sym;

					(void) strcpy(strtab, name);
					strtab += len;
				}
				if (dynsym && !local) {
					*dynsym = *sdp->sd_sym;

					dynsym->st_name = dynstr - _dynstr;
					(void) strcpy(dynstr, name);
					dynstr += len;

					hashval = sap->sa_hash %
						ofl->ofl_hashbkts;
					hashchain[hashndx] = hashbkt[hashval];
					hashbkt[hashval] = hashndx++;

					sdp->sd_sym = sym = dynsym;
				}
				if (!symtab && (!dynsym || local)) {
					if (!(sdp->sd_flags & FLG_SY_UPREQD))
						continue;
					sym = sdp->sd_sym;
				} else
					sdp->sd_flags &= ~FLG_SY_CLEAN;

				/*
				 * If we have a weak data symbol for which we
				 * need the real symbol also, save this
				 * processing until later.
				 *
				 * The exception to this is if the weak/strong
				 * have PLT's assigned to them.  In that case
				 * we don't do the post-weak processing because
				 * the PLT's must be maintained so that
				 * we can do 'interpositioning' on both of
				 * the symbols.
				 */
				if ((sap->sa_linkndx) &&
				    (ELF_ST_BIND(sym->st_info) == STB_WEAK) &&
				    (!sap->sa_PLTndx)) {
					Sym_desc *	_sdp =
					    sdp->sd_file->ifl_oldndx
						[sap->sa_linkndx];

					if (_sdp->sd_ref != REF_DYN_SEEN) {
						if ((wkp = (Wk_desc *)
						    calloc(sizeof (Wk_desc),
						    1)) == 0)
							return (S_ERROR);

						if (symtab)
							if (local)
								wkp->wk_symtab =
								    scopetab;
							else
								wkp->wk_symtab =
								    symtab;
						if (dynsym && !local)
							wkp->wk_dynsym = dynsym;
						wkp->wk_weak = sdp;
						wkp->wk_alias = _sdp;

						if (!(list_append(&weak, wkp)))
							return (S_ERROR);

						if (symtab)
							if (local)
								scopetab++;
							else
								symtab++;
						if (dynsym && !local)
							dynsym++;
						continue;
					}
				}

				DBG_CALL(Dbg_syms_old(sdp));

				spec = NULL;
				/*
				 * assign new symbol value.
				 */
				switch (sym->st_shndx) {
				case SHN_ABS:
					spec = sdp->sd_aux->sa_symspec;
					break;
				case SHN_UNDEF:
					if (sym->st_value != 0)
						eprintf(ERR_WARNING,
						    Errmsg_usnv, name);

					/*
					 * Undefined weak global, if we are
					 * generating a static executable,
					 * output as an absolute zero.
					 * Otherwise leave it as is, ld.so.1
					 * will skip symbols of this type (this
					 * technique allows applications and
					 * libraries to test for the existence
					 * of a symbol as an indication of the
					 * presence or absence of certain
					 * functionality).
					 */
					if (((flags &
					    (FLG_OF_STATIC | FLG_OF_EXEC)) ==
					    (FLG_OF_STATIC | FLG_OF_EXEC)) &&
					    (ELF_ST_BIND(sym->st_info) ==
					    STB_WEAK))
						sym->st_shndx = SHN_ABS;
					break;

				case SHN_COMMON:
					/*
					 * If allocating common assign it an
					 * address in the .bss section,
					 * otherwise leave it as is.
					 */
					if (local || !(flags & FLG_OF_RELOBJ)) {
						sym->st_shndx = bssndx;
						sym->st_value = S_ROUND(bssaddr,
						    sym->st_value);
						bssaddr = sym->st_value +
						    sym->st_size;
						sdp->sd_isc = ofl->ofl_isbss;
					}
					break;

				default:
					osp = sdp->sd_isc->is_osdesc;

					if (sdp->sd_ref == REF_DYN_NEED) {
						unsigned char	type, bind;

						sym->st_shndx = SHN_UNDEF;
						sym->st_value = 0;
						sym->st_size = 0;

						/*
						 * Make sure this undefined
						 * symbol is returned to the
						 * same binding as was defined
						 * in the original relocatable
						 * object reference.
						 */
						type = ELF_ST_TYPE(sym->
						    st_info);
						if (sdp->sd_flags &
						    FLG_SY_GLOBREF)
							bind = STB_GLOBAL;
						else
							bind = STB_WEAK;

						sym->st_info =
						    ELF_ST_INFO(bind, type);

						/*
						 * Indicate that the dependency
						 * is required to satisfy a
						 * symbol reference.
						 */
						sdp->sd_file->ifl_flags |=
						    FLG_IF_DEPREQD;

					} else if (sdp->sd_ref ==
					    REF_REL_NEED) {
						sym->st_shndx =
						    elf_ndxscn(osp->os_scn);

						/*
						 * In an executable, the new
						 * symbol value is the old value
						 * (offset into defining
						 * section) plus virtual address
						 * of defining section.  In a
						 * relocatable, the new value is
						 * the old value plus the
						 * displacement of the section
						 * within the file.
						 */
						sym->st_value += sdp->sd_isc->
							is_indata->d_off;
						if (!(flags & FLG_OF_RELOBJ))
							sym->st_value += osp->
							    os_shdr->sh_addr;
					}
					break;
				}

				if (spec) {
					switch (spec) {
					case SDAUX_ID_ETEXT:
						sym->st_value = etext;
						sym->st_shndx = etext_ndx;
						break;
					case SDAUX_ID_EDATA:
						sym->st_value = edata;
						sym->st_shndx = edata_ndx;
						break;
					case SDAUX_ID_END:
						sym->st_value = end;
						sym->st_shndx = end_ndx;
						break;
					case SDAUX_ID_DYN:
						if (ofl->ofl_flags &
						    FLG_OF_DYNAMIC) {
							sym->st_value = ofl->
							    ofl_osdynamic->
							    os_shdr->sh_addr;
							sym->st_shndx =
							    ofl->ofl_osdynamic->
							    os_scnsymndx;
						}
						break;
					case SDAUX_ID_PLT:
						if (ofl->ofl_osplt) {
							sym->st_value = ofl->
							    ofl_osplt->os_shdr->
							    sh_addr;
							sym->st_shndx = ofl->
							    ofl_osplt->
							    os_scnsymndx;
						}
						break;
					case SDAUX_ID_GOT:
						/*
						 * Symbol bias for negative
						 * growing tables is stored in
						 * symbol's value during
						 * allocate_got().
						 */
						sym->st_value += ofl->
						    ofl_osgot->os_shdr->sh_addr;
						sym->st_shndx = ofl->
						    ofl_osgot->os_scnsymndx;
						break;
					default:
						/* NOTHING */
						;
					}
				}

				/*
				 * If a plt index has been assigned to an
				 * undefined function, update the symbols value
				 * to the appropriate .plt address.
				 */
				if ((flags & FLG_OF_DYNAMIC) &&
				    (flags & FLG_OF_EXEC) &&
				    (sdp->sd_file) &&
				    (sdp->sd_file->ifl_ehdr->e_type ==
				    ET_DYN) &&
				    (ELF_ST_TYPE(sym->st_info) == STT_FUNC) &&
				    !(flags & FLG_OF_BFLAG)) {
					if (sap->sa_PLTndx)
						sym->st_value =
						    ofl->ofl_osplt->
						    os_shdr->sh_addr +
						    sap->sa_PLTndx *
						    M_PLT_ENTSIZE;
				}

				/*
				 * Finish updating the symbols.  If both the
				 * .symtab and .dynsym are present then we've
				 * actually updated the information in the
				 * .dynsym, therefore copy this same
				 * information to the .symtab entry.
				 */
				if (local)
					sym->st_info = ELF_ST_INFO(STB_LOCAL,
					    ELF_ST_TYPE(sym->st_info));

				if (symtab && dynsym && !local) {
					symtab->st_value = sym->st_value;
					symtab->st_size = sym->st_size;
					symtab->st_info = sym->st_info;
					symtab->st_other = sym->st_other;
					symtab->st_shndx = sym->st_shndx;
				}
				if (symtab)
					if (local)
						scopetab++;
					else
						symtab++;
				if (dynsym && !local)
					dynsym++;

				DBG_CALL(Dbg_syms_new(sym, sdp));
			}
		}
	}

	/*
	 * Now that all the symbols have been processed update any weak symbols
	 * information (ie. copy all information except `st_name').  As both
	 * symbols will be represented in the output, return the weak symbol to
	 * its correct type.
	 */
	for (LIST_TRAVERSE(&weak, lnp1, wkp)) {
		Sym_desc *	sdp, * _sdp;
		Sym *		sym, * _sym, * __sym;
		unsigned char	bind;

		sdp = wkp->wk_weak;
		_sdp = wkp->wk_alias;
		_sym = _sdp->sd_sym;

		sdp->sd_flags |= FLG_SY_WEAKDEF;

		/*
		 * If the symbol definition has been scoped then assign it to
		 * be local, otherwise if it's from a shared object then we need
		 * to maintain the binding of the original reference.
		 */
		if (sdp->sd_flags & FLG_SY_LOCAL) {
			if (flags & FLG_OF_PROCRED)
				bind = STB_LOCAL;
			else
				bind = STB_WEAK;
		} else if ((sdp->sd_ref == REF_DYN_NEED) &&
		    (sdp->sd_flags & FLG_SY_GLOBREF))
			bind = STB_GLOBAL;
		else
			bind = STB_WEAK;

		DBG_CALL(Dbg_syms_old(sdp));
		if ((sym = wkp->wk_symtab) != 0) {
			sym = wkp->wk_symtab;
			sym->st_value = _sym->st_value;
			sym->st_size = _sym->st_size;
			sym->st_other = _sym->st_other;
			sym->st_shndx = _sym->st_shndx;
			sym->st_info = ELF_ST_INFO(bind,
			    ELF_ST_TYPE(sym->st_info));
			__sym = sym;
		}
		if ((sym = wkp->wk_dynsym) != 0) {
			sym = wkp->wk_dynsym;
			sym->st_value = _sym->st_value;
			sym->st_size = _sym->st_size;
			sym->st_other = _sym->st_other;
			sym->st_shndx = _sym->st_shndx;
			sym->st_info = ELF_ST_INFO(bind,
			    ELF_ST_TYPE(sym->st_info));
			__sym = sym;
		}
		DBG_CALL(Dbg_syms_new(__sym, sdp));
	}

	/*
	 * Update the section headers information.
	 */
	if (symtab) {
		Shdr *	shdr = ofl->ofl_ossymtab->os_shdr;

		shdr->sh_info = ofl->ofl_shdrcnt + ofl->ofl_locscnt +
			ofl->ofl_scopecnt + 2;
		shdr->sh_link = elf_ndxscn(ofl->ofl_osstrtab->os_scn);
	}
	if (dynsym) {
		Shdr *	shdr = ofl->ofl_osdynsym->os_shdr;

		shdr->sh_info = ofl->ofl_shdrcnt + 1;
		shdr->sh_link = elf_ndxscn(ofl->ofl_osdynstr->os_scn);

		ofl->ofl_oshash->os_shdr->sh_link =
		    elf_ndxscn(ofl->ofl_osdynsym->os_scn);
	}

	/*
	 * Used by ld.so.1 only.
	 */
	return (etext);
}

/*
 * Build the dynamic section
 */
static int
update_odynamic(Ofl_desc * ofl)
{
	Listnode *	lnp;
	Ifl_desc *	ifl;
	Sym_desc *	sdp;
	Shdr *		shdr;
	Dyn *		dyn = (Dyn *)ofl->ofl_osdynamic->os_outdata->d_buf;
	char *		_dynstr = ofl->ofl_osdynstr->os_outdata->d_buf;
	Word		flags = ofl->ofl_flags;

	ofl->ofl_osdynamic->os_shdr->sh_link =
	    elf_ndxscn(ofl->ofl_osdynstr->os_scn);

	for (LIST_TRAVERSE(&ofl->ofl_sos, lnp, ifl)) {
		if (ifl->ifl_flags & FLG_IF_NEEDED)
			dyn->d_tag = DT_NEEDED;
		else if (ifl->ifl_flags & FLG_IF_USED)
			dyn->d_tag = DT_USED;
		else
			continue;
		dyn->d_un.d_val = dynstr - _dynstr;
		(void) strcpy(dynstr, ifl->ifl_soname);
		dynstr += strlen(ifl->ifl_soname) + 1;
		dyn++;

		/*
		 * If this dependency didn't satisfy any symbol references then
		 * generate a debugging diagnostic (ld(1) -Ddetail should be
		 * enough to display these).
		 */
		if (!(ifl->ifl_flags & FLG_IF_DEPREQD)) {
			DBG_CALL(Dbg_file_unused(ifl->ifl_soname));
		}
	}
	if (((sdp = sym_find(Init_sym, SYM_NOHASH, ofl)) != NULL) &&
		sdp->sd_ref == REF_REL_NEED) {
		dyn->d_tag = DT_INIT;
		dyn->d_un.d_ptr = sdp->sd_sym->st_value;
		dyn++;
	}
	if (((sdp = sym_find(Fini_sym, SYM_NOHASH, ofl)) != NULL) &&
		sdp->sd_ref == REF_REL_NEED) {
		dyn->d_tag = DT_FINI;
		dyn->d_un.d_ptr = sdp->sd_sym->st_value;
		dyn++;
	}
	if (ofl->ofl_soname) {
		dyn->d_tag = DT_SONAME;
		dyn->d_un.d_val = dynstr - _dynstr;
		(void) strcpy(dynstr, ofl->ofl_soname);
		dynstr += strlen(ofl->ofl_soname) + 1;
		dyn++;
	}
	if (ofl->ofl_filter) {
		if (ofl->ofl_flags & FLG_OF_AUX) {
			dyn->d_tag = DT_AUXILIARY;
		} else {
			dyn->d_tag = DT_FILTER;
		}
		dyn->d_un.d_val = dynstr - _dynstr;
		(void) strcpy(dynstr, ofl->ofl_filter);
		dynstr += strlen(ofl->ofl_filter) + 1;
		dyn++;
	}

	if (ofl->ofl_rpath) {
		dyn->d_tag = DT_RPATH;
		dyn->d_un.d_val = dynstr - _dynstr;
		(void) strcpy(dynstr, ofl->ofl_rpath);
		dynstr += strlen(ofl->ofl_rpath) + 1;
		dyn++;
	}
	if ((flags & (FLG_OF_VERDEF | FLG_OF_NOVERSEC)) == FLG_OF_VERDEF) {
		shdr = ofl->ofl_osverdef->os_shdr;
		dyn->d_tag = DT_VERDEF;
		dyn->d_un.d_ptr = shdr->sh_addr;
		dyn++;
		dyn->d_tag = DT_VERDEFNUM;
		dyn->d_un.d_ptr = shdr->sh_info;
		dyn++;
	}
	if ((flags & (FLG_OF_VERNEED | FLG_OF_NOVERSEC)) == FLG_OF_VERNEED) {
		shdr = ofl->ofl_osverneed->os_shdr;
		dyn->d_tag = DT_VERNEED;
		dyn->d_un.d_ptr = shdr->sh_addr;
		dyn++;
		dyn->d_tag = DT_VERNEEDNUM;
		dyn->d_un.d_ptr = shdr->sh_info;
		dyn++;
	}
	dyn->d_tag = DT_HASH;
	dyn->d_un.d_ptr = ofl->ofl_oshash->os_shdr->sh_addr;
	dyn++;

	shdr = ofl->ofl_osdynstr->os_shdr;
	dyn->d_tag = DT_STRTAB;
	dyn->d_un.d_ptr = shdr->sh_addr;
	dyn++;
	dyn->d_tag = DT_STRSZ;
	dyn->d_un.d_ptr = shdr->sh_size;
	dyn++;

	shdr = ofl->ofl_osdynsym->os_shdr;
	dyn->d_tag = DT_SYMTAB;
	dyn->d_un.d_ptr = shdr->sh_addr;
	dyn++;
	dyn->d_tag = DT_SYMENT;
	dyn->d_un.d_ptr = shdr->sh_entsize;
	dyn++;

	if (ofl->ofl_osinterp) {
		dyn->d_tag = DT_DEBUG;
		dyn->d_un.d_ptr = 0;
		dyn++;
	}

	if (flags & FLG_OF_TEXTREL) {
		/*
		 * Only the presence of this entry is used in this
		 * implementation, not the value stored.
		 */
		dyn->d_tag = DT_TEXTREL;
		dyn->d_un.d_val = 0;
		dyn++;
	}

	if (ofl->ofl_pltcnt != M_PLT_XNumber) {
		shdr =  ofl->ofl_osplt->os_relosdesc->os_shdr;

		dyn->d_tag = DT_PLTGOT;
		dyn->d_un.d_ptr = fillin_gotplt2(ofl);
		dyn++;
		dyn->d_tag = DT_PLTRELSZ;
		dyn->d_un.d_ptr = shdr->sh_size;
		dyn++;
		dyn->d_tag = DT_PLTREL;
		dyn->d_un.d_ptr = M_REL_DT_TYPE;
		dyn++;
		dyn->d_tag = DT_JMPREL;
		dyn->d_un.d_ptr = shdr->sh_addr;
		dyn++;
	}

	if (ofl->ofl_relocsz) {
		dyn->d_tag = M_REL_DT_TYPE;
		dyn->d_un.d_ptr = ofl->ofl_osreloc->os_shdr->sh_addr;
		dyn++;
		dyn->d_tag = M_REL_DT_SIZE;
		dyn->d_un.d_ptr = ofl->ofl_relocsz;
		dyn++;
		dyn->d_tag = M_REL_DT_ENT;
		dyn->d_un.d_ptr = sizeof (Rel);
		dyn++;
	}

	if (flags & (FLG_OF_SYMBOLIC | FLG_OF_BINDSYMB)) {
		dyn->d_tag = DT_SYMBOLIC;
		dyn++;
	}

	dyn->d_tag = DT_NULL;
	dyn->d_un.d_val = 0;

	return (1);
}

/*
 * Build the version definition section
 */
static int
update_overdef(Ofl_desc * ofl)
{
	Listnode *	lnp1, * lnp2;
	Ver_desc *	vdp, * _vdp;
	Verdef *	vdf, * _vdf;
	int		num = 0;
	Os_desc *	strosp, * symosp;

	/*
	 * Traverse the version descriptors and update the version structures
	 * to point to the dynstr name in preparation for building the version
	 * section structure.
	 */
	for (LIST_TRAVERSE(&ofl->ofl_verdesc, lnp1, vdp)) {
		Sym_desc *	sdp;

		if (vdp->vd_flags & VER_FLG_BASE) {
			const char *	name = vdp->vd_name;
			int		len = strlen(name) + 1;

			/*
			 * Create a new string table entry to represent the base
			 * version name (there is no corresponding symbol for
			 * this).
			 */
			if (ofl->ofl_flags & FLG_OF_RELOBJ) {
				vdp->vd_name = (const char *)(strtab - _strtab);
				(void) strcpy(strtab, name);
				strtab += len;
			} else {
				vdp->vd_name = (const char *)(dynstr - _dynstr);
				(void) strcpy(dynstr, name);
				dynstr += len;
			}
		} else {
			sdp = sym_find(vdp->vd_name, vdp->vd_hash, ofl);
			vdp->vd_name = (const char *)sdp->sd_sym->st_name;
		}
	}

	_vdf = vdf = (Verdef *)ofl->ofl_osverdef->os_outdata->d_buf;

	/*
	 * Traverse the version descriptors and update the version section to
	 * reflect each version and its associated dependencies.
	 */
	for (LIST_TRAVERSE(&ofl->ofl_verdesc, lnp1, vdp)) {
		Half		cnt = 1;
		Verdaux *	vdap, * _vdap;

		_vdap = vdap = (Verdaux *)(vdf + 1);

		vdf->vd_version = VER_DEF_CURRENT;
		vdf->vd_flags	= vdp->vd_flags;
		vdf->vd_ndx	= vdp->vd_ndx;
		vdf->vd_hash	= vdp->vd_hash;

		vdap->vda_name = (Addr)vdp->vd_name;
		vdap++;
		_vdap->vda_next = ((Word)vdap - (Word)_vdap);

		/*
		 * Traverse this versions dependency list generating the
		 * appropriate version dependency entries.
		 */
		for (LIST_TRAVERSE(&vdp->vd_deps, lnp2, _vdp)) {
			vdap->vda_name = (Addr)_vdp->vd_name;
			_vdap = vdap;
			vdap++, cnt++;
			_vdap->vda_next = ((Word)vdap - (Word)_vdap);
		}
		_vdap->vda_next = 0;

		/*
		 * Record the versions auxiliary array offset and the associated
		 * dependency count.
		 */
		vdf->vd_aux = ((Word)(vdf + 1) - (Word)vdf);
		vdf->vd_cnt = cnt;

		/*
		 * Record the next versions offset and update the version
		 * pointer.  Remember the previous version offset as the very
		 * last structures next pointer should be null.
		 */
		_vdf = vdf;
		vdf = (Verdef *)vdap, num++;
		_vdf->vd_next = ((Word)vdf - (Word)_vdf);
	}
	_vdf->vd_next = 0;

	/*
	 * Record the string table association with the version definition
	 * section, and the symbol table associated with the version symbol
	 * table (the actual contents of the version symbol table are filled
	 * in during symbol update).
	 */
	if (ofl->ofl_flags & FLG_OF_RELOBJ) {
		strosp = ofl->ofl_osstrtab;
		symosp = ofl->ofl_ossymtab;
	} else {
		strosp = ofl->ofl_osdynstr;
		symosp = ofl->ofl_osdynsym;
	}
	ofl->ofl_osverdef->os_shdr->sh_link = elf_ndxscn(strosp->os_scn);
	ofl->ofl_osversym->os_shdr->sh_link = elf_ndxscn(symosp->os_scn);

	/*
	 * The version definition sections `info' field is used to indicate the
	 * number of entries in this section.
	 */
	ofl->ofl_osverdef->os_shdr->sh_info = num;

	return (1);
}

/*
 * Build the version needed section
 */
static int
update_overneed(Ofl_desc * ofl)
{
	Listnode *	lnp;
	Ifl_desc *	ifl;
	Verneed *	vnd, * _vnd;
	char *		_dynstr = ofl->ofl_osdynstr->os_outdata->d_buf;
	int		num = 0;

	_vnd = vnd = (Verneed *)ofl->ofl_osverneed->os_outdata->d_buf;

	/*
	 * Traverse the shared object list looking for dependencies that have
	 * versions defined within them.
	 */
	for (LIST_TRAVERSE(&ofl->ofl_sos, lnp, ifl)) {
		Half		_cnt, cnt;
		Vernaux *	_vnap, * vnap;

		if (!(ifl->ifl_flags & FLG_IF_VERNEED))
			continue;

		vnd->vn_version = VER_NEED_CURRENT;

		(void) strcpy(dynstr, ifl->ifl_soname);
		vnd->vn_file = dynstr - _dynstr;
		dynstr += strlen(ifl->ifl_soname) + 1;

		_vnap = vnap = (Vernaux *)(vnd + 1);

		/*
		 * Traverse the version index list recording each version as a
		 * needed dependency.
		 */
		for (cnt = _cnt = 0; _cnt <= ifl->ifl_vercnt; _cnt++) {
			Ver_index *	vip = &ifl->ifl_verndx[_cnt];

			if (vip->vi_flags & FLG_VER_REFER) {
				(void) strcpy(dynstr, vip->vi_name);
				vnap->vna_name = dynstr - _dynstr;
				dynstr += strlen(vip->vi_name) + 1;
				if (vip->vi_desc) {
				    vnap->vna_hash = vip->vi_desc->vd_hash;
				    vnap->vna_flags = vip->vi_desc->vd_flags;
				} else {
				    vnap->vna_hash = 0;
				    vnap->vna_flags = 0;
				}
				vnap->vna_other = 0;

				_vnap = vnap;
				vnap++, cnt++;
				_vnap->vna_next = ((Word)vnap - (Word)_vnap);
			}
		}
		_vnap->vna_next = 0;

		/*
		 * Record the versions auxiliary array offset and the associated
		 * dependency count.
		 */
		vnd->vn_aux = ((Word)(vnd + 1) - (Word)vnd);
		vnd->vn_cnt = cnt;

		/*
		 * Record the next versions offset and update the version
		 * pointer.  Remember the previous version offset as the very
		 * last structures next pointer should be null.
		 */
		_vnd = vnd;
		vnd = (Verneed *)vnap, num++;
		_vnd->vn_next = ((Word)vnd - (Word)_vnd);
	}
	_vnd->vn_next = 0;

	/*
	 * Record association on string table section and use the `info' field
	 * to indicate the number of entries in this section.
	 */
	ofl->ofl_osverneed->os_shdr->sh_link =
	    elf_ndxscn(ofl->ofl_osdynstr->os_scn);
	ofl->ofl_osverneed->os_shdr->sh_info = num;

	return (1);
}

/*
 * Build the output elf header.
 */
static int
update_oehdr(Ofl_desc * ofl)
{
	Ehdr *		ehdr = ofl->ofl_ehdr;

	/*
	 * If an entry point symbol has already been established (refer
	 * sym_validate()) simply update the elf header entry point with the
	 * symbols value.  If no entry point is defined it will have been filled
	 * with the start address of the first section within the text segment
	 * (refer update_outfile()).
	 */
	if (ofl->ofl_entry)
		ehdr->e_entry =
			((Sym_desc *)(ofl->ofl_entry))->sd_sym->st_value;

	/*
	 * Note. it may be necessary to update the `e_flags' field in the
	 * machine dependent section.
	 */
	ehdr->e_ident[EI_DATA] = M_DATA;
	if (ofl->ofl_e_machine != M_MACH) {
		if (ofl->ofl_e_machine != M_MACHPLUS)
			return (S_ERROR);
		if ((ofl->ofl_e_flags & M_FLAGSPLUS) == 0)
			return (S_ERROR);
	}
	ehdr->e_machine = ofl->ofl_e_machine;
	ehdr->e_flags = ofl->ofl_e_flags;
	ehdr->e_version = ofl->ofl_libver;

	if (ofl->ofl_flags & FLG_OF_SHAROBJ)
		ehdr->e_type = ET_DYN;
	else if (ofl->ofl_flags & FLG_OF_RELOBJ)
		ehdr->e_type = ET_REL;
	else
		ehdr->e_type = ET_EXEC;

	return (1);
}


/*
 * Translate the shdr->sh_link from its input section value to that
 * of the corresponding shdr->sh_link output section value.
 */
static Word
translate_sh_link(Os_desc * osp)
{
	Is_desc *	isp;
	Ifl_desc *	ifl;
	Word		link;

	if ((link = osp->os_shdr->sh_link) == 0)
		return (link);

	/*
	 * Does this output section translate back to an input file.  If not
	 * then there is no translation to do.  In this case we will assume that
	 * if sh_link has a value, it's the right value.
	 */
	isp = (Is_desc *)osp->os_isdescs.head->data;
	if ((ifl = isp->is_file) == NULL)
		return (link);

	/*
	 * Sanity check to make sure that the sh_link value is within range for
	 * the input file.
	 */
	if (link > ifl->ifl_ehdr->e_shnum) {
		eprintf(ERR_WARNING, Errmsg_loor, ifl->ifl_name,
			isp->is_name, link);
		return (link);
	}

	/*
	 * Follow the link to the input section.
	 */
	if ((isp = ifl->ifl_isdesc[link]) == 0)
		return (0);
	if ((osp = isp->is_osdesc) == 0)
		return (0);

	return (elf_ndxscn(osp->os_scn));
}

/*
 * Having created all of the necessary sections, segments, and associated
 * headers, fill in the program headers and update any other data in the
 * output image.  Some general rules:
 *
 *  o	If an interpretor is required always generate a PT_PHDR entry as
 *	well.  It is this entry that triggers the kernel into passing the
 *	interpretor an aux vector instead of just a file descriptor.
 *
 *  o	When generating an image that will be interpreted (ie. a dynamic
 *	executable, a shared object, or a static executable that has been
 *	provided with an interpretor - weird, but possible), make the initial
 *	loadable segment include both the ehdr and phdr[].  Both of these
 *	tables are used by the interpretor therefore it seems more intuitive
 *	to explicitly defined them as part of the mapped image rather than
 *	relying on page rounding by the interpretor to allow their access.
 *
 *  o	When generating a static image that does not require an interpretor
 *	have the first loadable segment indicate the address of the first
 *	.section as the start address (things like /kernel/unix and ufsboot
 *	expect this behavior).
 */
Addr
update_outfile(Ofl_desc * ofl)
{
	Addr		vaddr = ofl->ofl_segorigin;
	Listnode *	lnp1, * lnp2;
	Sg_desc *	sgp;
	Os_desc *	osp;
	int		phdrndx = 0, segndx = -1, secndx;
	Ehdr *		ehdr = ofl->ofl_ehdr;
	Word		flags = ofl->ofl_flags;
	Addr		etext;
	List		osecs;
	Shdr *		hshdr;
	Addr		size;
	Phdr *		_phdr = 0;
	Word		phdrsz = ehdr->e_phnum * ehdr->e_phentsize;
	Word		ehdrsz = ehdr->e_ehsize;
	Boolean		nobits;
	Off		offset;

	/*
	 * Loop through the segment descriptors and pick out what we need.
	 */
	DBG_CALL(Dbg_seg_title());
	for (LIST_TRAVERSE(&ofl->ofl_segs, lnp1, sgp)) {
		Phdr *	phdr = &(sgp->sg_phdr);

		segndx++;

		/*
		 * If an interpreter is required generate a PT_INTERP and
		 * PT_PHDR program header entry.  The PT_PHDR entry describes
		 * the program header table itself.  This information will be
		 * passed via the aux vector to the interpreter (ld.so.1).
		 * The program header array is actually part of the first
		 * loadable segment (and the PT_PHDR entry is the first entry),
		 * therefore its virtual address isn't known until the first
		 * loadable segment is processed.
		 */
		if (phdr->p_type == PT_PHDR) {
			if (ofl->ofl_osinterp) {
				phdr->p_offset = ehdr->e_phoff;
				phdr->p_filesz = phdr->p_memsz = phdrsz;
				DBG_CALL(Dbg_seg_entry(segndx, sgp));
				ofl->ofl_phdr[phdrndx++] = *phdr;
			}
			continue;
		}
		if (phdr->p_type == PT_INTERP) {
			if (ofl->ofl_osinterp) {
				Shdr *	shdr = ofl->ofl_osinterp->os_shdr;

				phdr->p_vaddr = phdr->p_memsz = 0;
				phdr->p_offset = shdr->sh_offset;
				phdr->p_filesz = shdr->sh_size;
				DBG_CALL(Dbg_seg_entry(segndx, sgp));
				ofl->ofl_phdr[phdrndx++] = *phdr;
			}
			continue;
		}

		/*
		 * As the dynamic program header occurs after the loadable
		 * headers in the segment descriptor table, all the address
		 * information for the .dynamic output section will have been
		 * figured out by now.
		 */
		if (phdr->p_type == PT_DYNAMIC) {
			if (flags & FLG_OF_DYNAMIC) {
				Shdr *	shdr = ofl->ofl_osdynamic->os_shdr;

				phdr->p_vaddr = shdr->sh_addr;
				phdr->p_offset = shdr->sh_offset;
				phdr->p_filesz = shdr->sh_size;
				phdr->p_flags = (PF_R | PF_W | PF_X);
				DBG_CALL(Dbg_seg_entry(segndx, sgp));
				ofl->ofl_phdr[phdrndx++] = *phdr;
			}
			continue;
		}

		/*
		 * Having processed any of the special program headers any
		 * remaining headers will be built to express individual
		 * segments.  Segments are only built if they have output
		 * section descriptors associated with them (ie. some form of
		 * input section has been matched to this segment).
		 */
		osecs = sgp->sg_osdescs;
		if (osecs.head == NULL)
			continue;

		/*
		 * Determine the segments offset and size from the section
		 * information provided from elf_update().
		 * Allow for multiple NOBITS sections.
		 */
		hshdr = ((Os_desc *)osecs.head->data)->os_shdr;

		phdr->p_filesz = 0;
		phdr->p_memsz = 0;
		phdr->p_offset = offset = hshdr->sh_offset;
		nobits = (hshdr->sh_type == SHT_NOBITS);
		for (LIST_TRAVERSE(&osecs, lnp2, osp)) {
			Shdr *	shdr = osp->os_shdr;

			offset = S_ROUND(offset, shdr->sh_addralign);
			offset += shdr->sh_size;
			if (shdr->sh_type != SHT_NOBITS) {
				if (nobits) {
					eprintf(ERR_FATAL, Errmsg_nbts);
					return (S_ERROR);
				}
				phdr->p_filesz = offset - phdr->p_offset;
			} else
				nobits = TRUE;
		}
		phdr->p_memsz = offset - hshdr->sh_offset;

		/*
		 * If this is the first loadable segment of a dynamic object,
		 * or an interpretor has been specified (a static object built
		 * with an interpretor will still be given a PT_HDR entry), then
		 * compensate for the elf header and program header array.  Both
		 * of these are actually part of the loadable segment as they
		 * may be inspected by the interpretor.  Adjust the segments
		 * size and offset accordingly.
		 */
		if ((_phdr == 0) && (phdr->p_type == PT_LOAD) &&
		    ((ofl->ofl_osinterp) || (flags & FLG_OF_DYNAMIC)) &&
		    (!(sgp->sg_flags & FLG_SG_NOHDR))) {
			size = S_ROUND((phdrsz + ehdrsz), hshdr->sh_addralign);
			phdr->p_offset -= size;
			phdr->p_filesz += size;
			phdr->p_memsz += size;
		}

		/*
		 * If a segment size symbol is required (specified via a
		 * mapfile) update its value.
		 */
		if (sgp->sg_sizesym != NULL)
			sgp->sg_sizesym->sd_sym->st_value = phdr->p_memsz;


		/*
		 * If a virtual address has been specified for this segment
		 * (presumably from a map file) use it and make sure the
		 * previous segment does not run into this segment.
		 */
		if (phdr->p_type == PT_LOAD) {
			if ((sgp->sg_flags & FLG_SG_VADDR)) {
				if (_phdr && (vaddr > phdr->p_vaddr) &&
				    (phdr->p_type == PT_LOAD))
					eprintf(ERR_WARNING, Errmsg_sovr,
					    ofl->ofl_name, (unsigned long)vaddr,
					    sgp->sg_name,
					    (unsigned long)phdr->p_vaddr);
				vaddr = phdr->p_vaddr;
				phdr->p_align = 0;
			} else {
				vaddr = phdr->p_vaddr =
				    S_ROUND(vaddr, phdr->p_align) +
				    (phdr->p_offset % phdr->p_align);
			}
		}

		/*
		 * If an interpreter is required set the virtual address of the
		 * PT_PHDR program header now that we know the virtual address
		 * of the loadable segment that contains it.
		 */
		if ((_phdr == 0) && (phdr->p_type == PT_LOAD)) {
			_phdr = phdr;

			if (!(sgp->sg_flags & FLG_SG_NOHDR)) {
				if (ofl->ofl_osinterp)
					ofl->ofl_phdr[0].p_vaddr =
					    vaddr + ehdrsz;

				/*
				 * Finally, if we're creating a dynamic object
				 * (or a static object in which an interpretor
				 * is specified) update the vaddr to reflect
				 * the address of the first section within this
				 * segment.
				 */
				if ((ofl->ofl_osinterp) ||
				    (flags & FLG_OF_DYNAMIC))
					vaddr += size;
			} else {
				/*
				 * If the FLG_SG_NOHDR flag was set, PT_PHDR
				 * will not be part of any loadable segment.
				 */
				ofl->ofl_phdr[0].p_vaddr = 0;
				ofl->ofl_phdr[0].p_memsz = 0;
				ofl->ofl_phdr[0].p_flags = 0;
			}
		}

		/*
		 * Save the address of the first executable section for default
		 * use as the execution entry point.  This may get overridden in
		 * update_oehdr().
		 */
		if (!(flags & FLG_OF_RELOBJ) && !(ehdr->e_entry) &&
		    (phdr->p_flags & PF_X))
			ehdr->e_entry = vaddr;

		DBG_CALL(Dbg_seg_entry(segndx, sgp));

		/*
		 * Traverse the output section descriptors for this segment so
		 * that we can update the section headers addresses.  We've
		 * calculated the virtual address of the initial section within
		 * this segment, so each successive section can be calculated
		 * based on their offsets from each other.
		 */
		secndx = 0;
		hshdr = 0;
		for (LIST_TRAVERSE(&(sgp->sg_osdescs), lnp2, osp)) {
			Shdr *	shdr = osp->os_shdr;

			shdr->sh_link = translate_sh_link(osp);

			if (!(flags & FLG_OF_RELOBJ) &&
			    (phdr->p_type == PT_LOAD)) {
				if (hshdr)
					vaddr += (shdr->sh_offset -
					    hshdr->sh_offset);

				shdr->sh_addr = vaddr;
				hshdr = shdr;
			}

			DBG_CALL(Dbg_seg_os(osp, secndx));
			secndx++;
		}

		/*
		 * Establish the virtual address of the end of the last section
		 * in this segment so that the next segments offset can be
		 * calculated from this.
		 */
		if (hshdr)
			vaddr += hshdr->sh_size;

		/*
		 * Output sections for this segment complete.  Adjust the
		 * virtual offset for the last sections size, and make sure we
		 * haven't exceeded any maximum segment length specification.
		 */
		if ((sgp->sg_length != 0) && (sgp->sg_length < phdr->p_memsz)) {
			eprintf(ERR_FATAL, Errmsg_ssiz, ofl->ofl_name,
				sgp->sg_name, phdr->p_memsz, sgp->sg_length);
			return (S_ERROR);
		}

		if (phdr->p_type == PT_NOTE) {
			phdr->p_vaddr = 0;
			phdr->p_paddr = 0;
			phdr->p_align = 0;
			phdr->p_memsz = 0;
		}
		if ((phdr->p_type != PT_NULL) && !(flags & FLG_OF_RELOBJ))
			ofl->ofl_phdr[phdrndx++] = *phdr;
	}

	/*
	 * Update any new output sections.  When building the initial output
	 * image, a number of sections were created but left uninitialized (eg.
	 * .dynsym, .dynstr, .symtab, .symtab, etc.).  Here we update these
	 * sections with the appropriate data.  Other sections may still be
	 * modified via reloc_process().
	 *
	 * Copy the interpretor name into the .interp section.
	 */
	if (ofl->ofl_interp)
		(void) strcpy((char *)ofl->ofl_osinterp->os_outdata->d_buf,
		    ofl->ofl_interp);

	/*
	 * Build any output symbol tables, the symbols information is copied
	 * and updated into the new output image.
	 */
	if ((etext = update_osym(ofl)) == (Addr)S_ERROR)
		return ((Addr)S_ERROR);

	/*
	 * Build any output headers, version information and dynamic structure.
	 */
	if (update_oehdr(ofl) == S_ERROR)
		return ((Addr)S_ERROR);
	if ((flags & (FLG_OF_VERDEF | FLG_OF_NOVERSEC)) == FLG_OF_VERDEF)
		if (update_overdef(ofl) == S_ERROR)
			return (S_ERROR);
	if ((flags & (FLG_OF_VERNEED | FLG_OF_NOVERSEC)) == FLG_OF_VERNEED)
		if (update_overneed(ofl) == S_ERROR)
			return (S_ERROR);
	if (flags & FLG_OF_DYNAMIC)
		if (update_odynamic(ofl) == S_ERROR)
			return ((Addr)S_ERROR);

	/*
	 * Initialize the section headers string table index within the elf
	 * header.
	 */
	ofl->ofl_ehdr->e_shstrndx = elf_ndxscn(ofl->ofl_osshstrtab->os_scn);

	return (etext);
}
