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
#pragma ident	"@(#)libs.c	1.29	95/05/12 SMI"

/* LINTLIBRARY */

/*
 * Library processing
 */
#include	<stdio.h>
#include	<string.h>
#include	"debug.h"
#include	"_libld.h"

static const char
	* Errmsg_eahd =	"file %s: elf_getarhdr",
	* Errmsg_eula = "file %s: unable to locate archive symbol table",
	* Errmsg_eulm = "file %s: unable to locate archive member;\n"
			"\toffset=%x, symbol[%d]=%s";

/*
 * Because a tentative symbol may cause the extraction of an archive member,
 * make sure that the potential member is really required.  If the archive
 * member has a defined symbol it will be extracted, if it simply contains
 * another tentative definition, or a defined function symbol, then it will
 * not be used.
 */
static int
process_member(Ar_mem * amp, const char * name, Ofl_desc * ofl)
{
	Sym *		syms;
	int		symn, cnt = 0;
	char *		strs;

	/*
	 * Find the first symbol table in the archive member, obtain its
	 * data buffer and determine the number of global symbols (Note,
	 * there must be a symbol table present otherwise the archive would
	 * never have been able to generate its own symbol entry for this
	 * member).
	 */
	if (amp->am_syms == 0) {
		Elf_Scn *	scn = NULL;
		Shdr *		shdr;
		Elf_Data *	data;

		while (scn = elf_nextscn(amp->am_elf, scn)) {
			if ((shdr = elf_getshdr(scn)) == NULL) {
				eprintf(ERR_ELF, Errmsg_egsh, amp->am_path);
				ofl->ofl_flags |= FLG_OF_FATAL;
				return (0);
			}
			if ((shdr->sh_type == SHT_SYMTAB) ||
			    (shdr->sh_type == SHT_DYNSYM))
				break;
		}
		if ((data = elf_getdata(scn, NULL)) == NULL) {
			eprintf(ERR_ELF, Errmsg_egdt, amp->am_path);
			ofl->ofl_flags |= FLG_OF_FATAL;
			return (0);
		}
		syms = (Sym *)data->d_buf;
		syms += shdr->sh_info;
		symn = shdr->sh_size / shdr->sh_entsize;
		symn -= shdr->sh_info;

		/*
		 * Get the data for the associated string table.
		 */
		if ((scn = elf_getscn(amp->am_elf, shdr->sh_link)) == NULL) {
			eprintf(ERR_ELF, Errmsg_egsc, amp->am_path);
			ofl->ofl_flags |= FLG_OF_FATAL;
			return (0);
		}
		if ((data = elf_getdata(scn, NULL)) == NULL) {
			eprintf(ERR_ELF, Errmsg_egdt, amp->am_path);
			ofl->ofl_flags |= FLG_OF_FATAL;
			return (0);
		}
		strs = data->d_buf;

		/*
		 * Initialize the archive member structure in case we have to
		 * come through here again.
		 */
		amp->am_syms = syms;
		amp->am_strs = strs;
		amp->am_symn = symn;
	} else {
		syms = amp->am_syms;
		strs = amp->am_strs;
		symn = amp->am_symn;
	}

	/*
	 * Loop through the symbol table entries looking for a match.
	 * If the symbol is defined then this member should be extracted.
	 */
	while (cnt < symn) {
		int		shndx = syms->st_shndx;
		unsigned char	type = ELF_ST_TYPE(syms->st_info);

		if ((shndx != SHN_ABS) && (shndx != SHN_COMMON) &&
		    (shndx != SHN_UNDEF) && (type != STT_FUNC) &&
		    (strcmp(strs + syms->st_name, name) == NULL))
			return (1);

		syms++;
		cnt++;
	}
	return (0);
}

/*
 * Create an archive descriptor.  By maintaining a list of archives any
 * duplicate occurrences of the same archive specified by the user enable us to
 * pick off where the last processing finished.
 */
Ar_desc *
ar_setup(const char * name, Elf * elf, int fd, Ofl_desc * ofl)
{
	Ar_desc *	adp;
	Elf *		arelf;
	size_t		number;
	Elf_Arsym *	start;

	/*
	 * Get the archive symbol table.  If this fails see if the file is
	 * really an elf archive so that we can provide a more meaningful error
	 * message.
	 */
	if ((start = elf_getarsym(elf, &number)) == 0) {
		(void) elf_rand(elf, 0);
		if (((arelf = elf_begin(fd, ELF_C_READ, elf)) != 0) &&
		    (elf_kind(arelf) != ELF_K_ELF))
			eprintf(ERR_FATAL, Errmsg_ufte, name);
		else
			eprintf(ERR_ELF, Errmsg_eula, name);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}

	/*
	 * As this is a new archive reference establish a new descriptor.
	 */
	if ((adp = (Ar_desc *)malloc(sizeof (Ar_desc))) == 0)
		return ((Ar_desc *)S_ERROR);
	adp->ad_name = name;
	adp->ad_elf = elf;
	adp->ad_start = start;
	adp->ad_cnt = 10000;
	if ((adp->ad_aux = (Ar_aux *)calloc(sizeof (Ar_aux), number)) == 0)
		return ((Ar_desc *)S_ERROR);

	/*
	 * Add this new descriptor to the liat of archives.
	 */
	if (list_append(&ofl->ofl_ars, adp) == 0)
		return ((Ar_desc *)S_ERROR);
	else
		return (adp);
}

/*
 * For each archive descriptor we maintain an `Ar_aux' table to parallel the
 * archive symbol table (returned from elf_getarsym(3e)).  We use this table to
 * hold the `Sym_desc' for each symbol (thus reducing the number of sym_find()'s
 * we have to do), and to hold the `Ar_mem' pointer.  The `Ar_mem' element can
 * have one of three values indicating the state of the archive member
 * associated with the offset for this symbol table entry:
 *
 *  0		indicates that the member has not been extracted.
 *
 *  AREXTRACTED	indicates that the member has been extracted.
 *
 *  addr	indicates that the member has been investigated to determine if
 *		it contained a symbol definition we need, but was found not to
 *		be a candidate for extraction.  In this case the members
 *		structure is maintained for possible later use.
 *
 * Each time we process an archive member we use its offset value to scan this
 * `Ar_aux' list.  If the member has been extracted, each entry with the same
 * offset has its `Ar_mem' pointer set to AREXTRACTED.  Thus if we cycle back
 * through the archive symbol table we will ignore these symbols as they will
 * have already been added to the output image.  If a member has been processed
 * but found not to contain a symbol we need, each entry with the same offset
 * has its `Ar_mem' pointer set to the member structures address.
 */
void
ar_member(Ar_desc * adp, Elf_Arsym * arsym, Ar_aux * aup, Ar_mem * amp)
{
	Elf_Arsym *	_arsym = arsym;
	Ar_aux *	_aup = aup;
	size_t		_off = arsym->as_off;

	if (_arsym != adp->ad_start) {
		do {
			_arsym--;
			_aup--;
			if (_arsym->as_off != _off)
				break;
			_aup->au_mem = amp;
		} while (_arsym != adp->ad_start);
	}

	_arsym = arsym;
	_aup = aup;

	do {
		if (_arsym->as_off != _off)
			break;
		_aup->au_mem = amp;
		_arsym++;
		_aup++;
	} while (_arsym->as_name);
}

/*
 * Read in the archive's symbol table; for each symbol in the table check
 * whether that symbol satisfies an unresolved, or tentative reference in
 * ld's internal symbol table; if so, the corresponding object from the
 * archive is processed.  The archive symbol table is searched until we go
 * through a complete pass without satisfying any unresolved symbols
 */
int
process_archive(const char * name, int fd, Ar_desc * adp, Ofl_desc * ofl)
{
	Elf_Arsym *	arsym;
	Elf_Arhdr *	arhdr;
	Elf *		arelf;
	Ar_aux *	aup;
	Sym_desc *	sdp;
	char *		arname, * arpath;
	int		ndx, err, found = 0;

	/*
	 * Loop through archive symbol table until we make a complete pass
	 * without satisfying an unresolved reference.  For each archive
	 * symbol, see if there is a symbol with the same name in ld's
	 * symbol table.  If so, and if that symbol is still unresolved or
	 * tentative, process the corresponding archive member.
	 */
	do {
		DBG_CALL(Dbg_file_archive(name, found));
		DBG_CALL(Dbg_syms_ar_title(name, found));

		ndx = found = 0;
		for (arsym = adp->ad_start, aup = adp->ad_aux; arsym->as_name;
		    ++arsym, ++aup, ndx++) {
			Ar_mem *	amp;
			Sym *		sym;
			Boolean		veravail;

			/*
			 * If the auxiliary members value indicates that this
			 * member has been extracted then this symbol will have
			 * been added to the output file image already.
			 */
			if (aup->au_mem == AREXTRACTED)
				continue;

			/*
			 * If the auxiliary symbol element is non-zero lookup
			 * the symbol from the internal symbol table.
			 */
			if ((sdp = aup->au_syms) == 0) {
				if ((sdp = sym_find(arsym->as_name,
				    arsym->as_hash, ofl)) == 0) {
					DBG_CALL(Dbg_syms_ar_entry(ndx, arsym));
					continue;
				}
				aup->au_syms = sdp;
			}

			/*
			 * If the symbol is undefined or tentative, and is
			 * not weak, this archive member is a candidate for
			 * extraction.  If this symbol has been bound to a
			 * versioned shared object make sure it is available
			 * for linking.
			 */
			veravail = TRUE;
			if ((sdp->sd_ref == REF_DYN_NEED) &&
			    (sdp->sd_file->ifl_vercnt)) {
				int		vndx;
				Ver_index *	vip;

				vndx = sdp->sd_aux->sa_verndx;
				vip = &sdp->sd_file->ifl_verndx[vndx];
				if (!(vip->vi_flags & FLG_VER_AVAIL))
					veravail = FALSE;
			}
			sym = sdp->sd_sym;
			if (((sym->st_shndx != SHN_UNDEF) &&
			    (sym->st_shndx != SHN_COMMON) && veravail) ||
			    (ELF_ST_BIND(sym->st_info) == STB_WEAK)) {
				DBG_CALL(Dbg_syms_ar_entry(ndx, arsym));
				continue;
			}

			/*
			 * Determine if we have already extracted this member,
			 * and if so reuse the Ar_mem information.
			 */
			if ((amp = aup->au_mem) != 0) {
				arelf = amp->am_elf;
				arname = amp->am_name;
				arpath = amp->am_path;
			} else {
				/*
				 * Set up a new elf descriptor for this member.
				 */
				if (elf_rand(adp->ad_elf, arsym->as_off) !=
				    arsym->as_off) {
					eprintf(ERR_ELF, Errmsg_eulm, name,
					    arsym->as_off, ndx, arsym->as_name);
					ofl->ofl_flags |= FLG_OF_FATAL;
					return (0);
				}
				if ((arelf = elf_begin(fd, ELF_C_READ,
				    adp->ad_elf)) == NULL) {
					eprintf(ERR_ELF, Errmsg_ebgn, name);
					ofl->ofl_flags |= FLG_OF_FATAL;
					return (0);
				}

				/*
				 * Construct the member filename.
				 */
				if ((arhdr = elf_getarhdr(arelf)) == NULL) {
					eprintf(ERR_ELF, Errmsg_eahd, name);
					ofl->ofl_flags |= FLG_OF_FATAL;
					return (0);
				}
				arname = arhdr->ar_name;

				/*
				 * Construct the members full pathname
				 */
				if ((arpath = (char *)malloc(strlen(name) +
				    strlen(arname) + 3)) == 0)
					return (S_ERROR);
				(void) sprintf(arpath, "%s(%s)", name, arname);
			}

			/*
			 * If the symbol for which this archive member is
			 * being processed is a tentative symbol, then this
			 * member must be verified to insure that it is
			 * going to provided a symbol definition that will
			 * override the tentative symbol.
			 */
			if (sym->st_shndx == SHN_COMMON) {
				/*
				 * If we don't already have a member structure
				 * allocate one.
				 */
				if (!amp) {
					if ((amp = (Ar_mem *)
					    calloc(sizeof (Ar_mem), 1)) == 0)
						return (S_ERROR);
					amp->am_elf = arelf;
					amp->am_name = arname;
					amp->am_path = arpath;
				}
				DBG_CALL(Dbg_syms_ar_checking(ndx, arsym,
				    arname));
				err = process_member(amp, arsym->as_name, ofl);
				if (err == S_ERROR)
					return (S_ERROR);

				/*
				 * If it turns out that we don't need this
				 * member simply initialize all other auxiliary
				 * entries that match this offset with this
				 * members address.  In this way we can resuse
				 * this information if we recurse back to this
				 * symbol.
				 */
				if (err == 0) {
					if (aup->au_mem == 0)
						ar_member(adp, arsym, aup, amp);
					continue;
				}
			}

			/*
			 * Process the archive member.  Retain any error for
			 * return to the caller.
			 */
			DBG_CALL(Dbg_syms_ar_resolve(ndx, arsym, arname));
			if ((err = (int)process_ifl(arpath, NULL, fd, arelf,
			    FLG_IF_EXTRACT | FLG_IF_NEEDED, ofl)) == S_ERROR)
				return (S_ERROR);
			if (err)
				found++;

			(void) free(aup->au_mem);
			ar_member(adp, arsym, aup, AREXTRACTED);
			DBG_CALL(Dbg_syms_nl());
		}
	} while (found);

	/*
	 * If this archives usage count has gone to zero free up any members
	 * that had been processed but were not really needed together with any
	 * other descriptors for this archive.
	 */
	if (--adp->ad_cnt == 0) {
		Ar_mem *	amp = 0;

		for (arsym = adp->ad_start, aup = adp->ad_aux; arsym->as_name;
		    ++arsym, ++aup) {

			if (aup->au_mem && (aup->au_mem != AREXTRACTED)) {
				if (aup->au_mem != amp) {
					amp = aup->au_mem;
					(void) elf_end(amp->am_elf);
					(void) free(amp->am_path);
					(void) free(amp);
				}
			} else
				amp = 0;
		}

		(void) elf_end(adp->ad_elf);
		(void) free(adp->ad_aux);

		/*
		 * We should now get rid of the archive descriptor.
		 */
	}
	return (1);
}
