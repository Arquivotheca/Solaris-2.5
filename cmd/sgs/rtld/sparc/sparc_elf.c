/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)sparc_elf.c	1.32	95/03/05 SMI"

/*
 * SPARC machine dependent and ELF file class dependent functions.
 * Contains routines for performing function binding and symbol relocations.
 */
#include	<stdio.h>
#include	<sys/elf.h>
#include	<sys/elf_SPARC.h>
#include	<sys/mman.h>
#include	<signal.h>
#include	<dlfcn.h>
#include	<unistd.h>
#include	<synch.h>
#include	"_rtld.h"
#include	"_elf.h"
#include	"profile.h"
#include	"debug.h"
#include	"libconv.h"


extern void	iflush_range(caddr_t, int);

#ifdef	PROF

/*
 * After the first call to a plt, elf_bndr() will have determined the true
 * address of the function being bound.  If the library to which the function
 * belongs is being profiled then the plt is written to call the 'dynamic plt'
 * which calls elf_plt_cg (which counts the fact that the library was called),
 * and then the real function is called.
 *
 * Note: it is possible that the 'dynamic plt' for the profiled function
 * has already been created.  If this is the case we will just point
 * the plt to this 'dynamic plt'.  By doing this we can re-use the same
 * dynamic plt for multiple 'plt's' which point to the same function.
 *
 * the dynamic plt entry is:
 *
 *	mov	%o7, %g1
 *	call	elf_plt_cg
 *	sethi	<symbol index>, %g0
 */
static void
elf_plt_cg_write(caddr_t pc, Rt_map * lmp, int symndx)
{
	extern void	elf_plt_cg(int ndx, caddr_t from);
	caddr_t		dyn_plt;

	dyn_plt = ((caddr_t)DYNPLT(lmp) + (symndx * M_DYN_PLT_ENT));

	/*
	 * Have we initialized this dynamic plt entry yet?  If we haven't do it
	 * now.  Otherwise this function has been called before, but from a
	 * different plt (ie. from another shared object).  In that case
	 * we just set the plt to point to the new dyn_plt.
	 */
	if (*dyn_plt != M_MOV07TOG1) {
		/* LINTED */
		*(unsigned long *)dyn_plt = (unsigned long)M_MOV07TOG1;
		/* LINTED */
		*(unsigned long *)(dyn_plt + 4) = (M_CALL |
		    (((unsigned long)&elf_plt_cg -
		    (unsigned long)(dyn_plt + 4)) >> 2));
		/* LINTED */
		*(unsigned long *)(dyn_plt + 8) = (M_NOP |
		    (symndx & S_MASK(22)));
	}

	iflush_range(dyn_plt, M_DYN_PLT_ENT);
	/*
	 * Now point the current PLT to our new PLT.
	 */
	/* LINTED */
	elf_plt_write((unsigned long *)pc, (unsigned long *)dyn_plt);
}

#endif

/*
 * Function binding routine - invoked on the first call to a function through
 * the procedure linkage table;
 * passes first through an assembly language interface.
 *
 * Takes the address of the PLT entry where the call originated,
 * the offset into the relocation table of the associated
 * relocation entry and the address of the link map (rt_private_map struct)
 * for the entry.
 *
 * Returns the address of the function referenced after re-writing the PLT
 * entry to invoke the function directly.
 *
 * On error, causes process to terminate with a signal.
 */

unsigned long
elf_bndr(caddr_t pc, unsigned long reloc, Rt_map * lmp, caddr_t from)
{
	Rt_map *	nlmp;
	unsigned long	pltadd, symval;
	char *		name;
	Rel *		rptr;
	Sym *		sym, * nsym;
	int		pltndx;
	int		bind;

	PRF_MCOUNT(1, elf_bndr);
	if ((bind = bind_guard(THR_FLG_BIND)) == 1)
		(void) rw_rdlock(&bindlock);

	/*
	 * Must calculate true plt relocation address from reloc.
	 * Take offset, subtract number of reserved PLT entries, and divide
	 * by PLT entry size, which should give the index of the plt
	 * entry (and relocation entry since they have been defined to be
	 * in the same order).  Then we must multiply by the size of
	 * a relocation entry, which will give us the offset of the
	 * plt relocation entry from the start of them given by JMPREL(lm).
	 */
	pltadd = reloc - M_PLT_XNumber * M_PLT_ENTSIZE;
	pltndx = pltadd / M_PLT_ENTSIZE;


	/*
	 * Perform some basic sanity checks.  If we didn't get a load map
	 * or the plt offset is invalid then its possible someone has walked
	 * over the plt entries.
	 */
	if (!lmp) {
		eprintf(ERR_FATAL, Errmsg_rupp, from, reloc, Errmsg_rupr);
		exit(1);
	}

	if ((pltadd % M_PLT_ENTSIZE) != 0) {
		eprintf(ERR_FATAL, Errmsg_rupp, from, reloc, Errmsg_rbeo);
		exit(1);
	}

	reloc = pltndx * sizeof (Rel);

	/*
	 * Use relocation entry to get symbol table entry and symbol name.
	 */
	pltadd = (unsigned long)JMPREL(lmp);
	rptr = (Rel *)(pltadd + reloc);
	sym = (Sym *)((unsigned long)SYMTAB(lmp) +
		(ELF_R_SYM(rptr->r_info) * SYMENT(lmp)));
	name = (char *)(STRTAB(lmp) + sym->st_name);

	/*
	 * Find definition for symbol.
	 */
	if ((nsym = lookup_sym(name, PERMIT(lmp), lmp, LIST(lmp)->lm_head,
	    &nlmp, LKUP_DEFT)) != 0) {
		symval = nsym->st_value;
		if (!(FLAGS(nlmp) & FLG_RT_ISMAIN) &&
		    (nsym->st_shndx != SHN_ABS))
			symval += ADDR(nlmp);
		if (lmp != nlmp)
			/*
			 * Record that this new link map is now bound to the
			 * caller.
			 */
			if (!(bound_add(lmp, nlmp)))
				exit(1);
	} else {
		eprintf(ERR_FATAL, Errmsg_rsnf, name, NAME(lmp));
		exit(1);
	}

	/*
	 * Print binding information and rebuild PLT entry.
	 */
	DBG_CALL(Dbg_bind_global(NAME(lmp), from, from - ADDR(lmp), pltndx,
		NAME(nlmp), (caddr_t)symval, (caddr_t)nsym->st_value, name));

	if (!(rtld_flags & RT_FL_NOBIND)) {
#ifdef	PROF
		if (FLAGS(nlmp) & FLG_RT_PROFILE) {
			int	symndx = (((int)nsym - (int)SYMTAB(nlmp)) /
						SYMENT(nlmp));
			/*
			 * create the dynamic PLT entry which
			 * will call elf_plt_cg.
			 */
			elf_plt_cg_write(pc, nlmp, symndx);

			/*
			 * Call the call graph interpretor to initialize this
			 * first call.
			 */
			(void) plt_cg_interp(symndx, (caddr_t)from,
				(caddr_t)symval);
		} else {
#endif
			/*
			 * Write standard PLT entry to jump directly
			 * to newly bound function.
			 */
			/* LINTED */
			elf_plt_write((unsigned long *)pc,
			    (unsigned long *)symval);
#ifdef	PROF
		}
#endif
	}
	if (bind) {
		(void) rw_unlock(&bindlock);
		(void) bind_clear(THR_FLG_BIND);
	}
	return (symval);
}

/*
 * Read and process the relocations for one link object, we assume all
 * relocation sections for loadable segments are stored contiguously in
 * the file.
 *
 * Accept a pointer to the rt_private_map structure for a given object and
 * the binding mode: if RTLD_LAZY, then don't relocate procedure
 * linkage table entries; if RTLD_NOW, do.
 */
int
elf_reloc(Rt_map * lmp, int mode)
{
	unsigned long	relbgn, relend, relsiz, basebgn;
	unsigned long	pltbgn, pltend;
	unsigned long	roffset, rsymndx, psymndx = 0, etext;
	unsigned int	rtype;
	long		reladd;
	long		value, pvalue;
	Sym *		symref, * psymref, * symdef, * psymdef;
	char *		name, * pname;
	Rt_map *	_lmp, * plmp;
	int		textrel = 0;
	int		error = 0, noplt = 0;
	Rel *		rel;

	PRF_MCOUNT(2, elf_reloc);
	DBG_CALL(Dbg_reloc_run(NAME(lmp)));

	/*
	 * Although only necessary for lazy binding, initialize the first
	 * procedure linkage table entry to go to elf_rtbndr().  dbx(1) seems
	 * to find this useful.
	 */
	if (PLTGOT(lmp))
		elf_plt_init((unsigned long *)PLTGOT(lmp), lmp);

	/*
	 * Initialize the plt start and end addresses.
	 */
	if ((pltbgn = (unsigned long)JMPREL(lmp)) != 0)
		pltend = pltbgn + (unsigned long)(PLTRELSZ(lmp));

	/*
	 * The relocation sections appear to the run-time linker as a single
	 * table.  Determine the address of the beginning and end of this table.
	 * Determine from our knowledege of the relocation range and .plt
	 * range, the range of the total relocation table.  Note that one other
	 * ABI assumption seems to be that the .plt relocations always follow
	 * any other relocations, this range checking drops that assumption.
	 */
	relbgn = (unsigned long)(REL(lmp));
	relend = relbgn + (unsigned long)(RELSZ(lmp));
	if (pltbgn) {
		if (!relbgn || (relbgn > pltbgn))
			relbgn = pltbgn;
		if (!relbgn || (relend < pltend))
			relend = pltend;
	}
	if (!relbgn || (relbgn == relend))
		return (1);
	relsiz = (unsigned long)(RELENT(lmp));
	basebgn = ADDR(lmp);
	etext = ETEXT(lmp);

	/*
	 * If we're processing in lazy mode there is no need to scan the
	 * .rela.plt table.
	 */
	if (pltbgn && (mode == RTLD_LAZY))
		noplt = 1;

	/*
	 * Loop through relocations.
	 */
	while (relbgn < relend) {
		rsymndx = ELF_R_SYM(((Rel *)relbgn)->r_info);
		rtype = ELF_R_TYPE(((Rel *)relbgn)->r_info);
		reladd = (long)(((Rel *)relbgn)->r_addend);
		roffset = ((Rel *)relbgn)->r_offset;
		rel = (Rel *)relbgn;
		relbgn += relsiz;

		/*
		 * Optimizations.
		 */
		if (rtype == R_SPARC_NONE)
			continue;
		if (noplt && ((unsigned long)rel >= pltbgn) &&
		    ((unsigned long)rel < pltend))
			continue;

		/*
		 * If this is a shared object, add the base address to offset.
		 */
		if (!(FLAGS(lmp) & FLG_RT_ISMAIN))
			roffset += basebgn;

		/*
		 * If this relocation is not against part of the image
		 * mapped into memory we skip it.
		 */
		if ((roffset < ADDR(lmp)) || (roffset > (ADDR(lmp) +
		    MSIZE(lmp))))
			continue;

		/*
		 * If this object has relocations in the text segment, turn
		 * off the write protect.
		 */
		if (roffset < etext) {
			if (!textrel) {
				if (elf_set_prot(lmp, PROT_WRITE) == 0)
					return (0);
				textrel = 1;
			}
		}

		/*
		 * If a symbol index is specified then get the symbol table
		 * entry, locate the symbol definition, and determine its
		 * address.
		 */
		if (rsymndx) {
			/*
			 * Get the local symbol table entry.
			 */
			symref = (Sym *)((unsigned long)SYMTAB(lmp) +
					(rsymndx * SYMENT(lmp)));

			/*
			 * If this is a local symbol, just use the base address.
			 * (we should have no local relocations in the
			 * executable).
			 */
			if (ELF_ST_BIND(symref->st_info) == STB_LOCAL) {
				value = basebgn;
				name = (char *)0;
			} else {
				/*
				 * If the symbol index is equal to the previous
				 * symbol index relocation we processed then
				 * reuse the previous values. (Note that there
				 * have been cases where a relocation exists
				 * against a copy relocation symbol, our ld(1)
				 * should optimize this away, but make sure we
				 * don't use the same symbol information should
				 * this case exist).
				 */
				if ((rsymndx == psymndx) &&
				    (rtype != R_SPARC_COPY)) {
					/* LINTED */
					value = pvalue;
					/* LINTED */
					name = pname;
					/* LINTED */
					symdef = psymdef;
					/* LINTED */
					symref = psymref;
					/* LINTED */
					_lmp = plmp;
				} else {
					int	flags;

					/*
					 * Lookup the symbol definition.  If
					 * this is for a copy relocation don't
					 * bother looking in the executable.
					 */
					name = (char *)(STRTAB(lmp) +
					    symref->st_name);

					_lmp = LIST(lmp)->lm_head;
					if (rtype == R_SPARC_COPY)
						_lmp = (Rt_map *)NEXT(_lmp);

					if (rtype == R_SPARC_JMP_SLOT)
						flags = LKUP_DEFT;
					else
						flags = LKUP_SPEC;

					symdef = lookup_sym(name, PERMIT(lmp),
					    lmp, _lmp, &_lmp, flags);

					/*
					 * If the symbol is not found and the
					 * reference was not to a weak symbol,
					 * report an error.  Weak references may
					 * be unresolved.
					 */
					if (symdef == 0) {
					    if (ELF_ST_BIND(symref->st_info) !=
						STB_WEAK) {
						if (rtld_flags & RT_FL_WARN) {
						    (void) printf(Lddmsg_rsnf,
							name, NAME(lmp));
						    continue;
						} else {
						    eprintf(ERR_FATAL,
							Errmsg_rsnf, name,
							NAME(lmp));
						    return (0);
						}
					    } else {
						DBG_CALL(Dbg_bind_weak(
						    NAME(lmp), (caddr_t)roffset,
						    (caddr_t)
						    (roffset - basebgn), name));
						continue;
					    }
					}

					/*
					 * If symbol was found in an object
					 * other than the referencing object
					 * then set the boundto bit in the
					 * defining object.
					 */
					if (lmp != _lmp)
						FLAGS(_lmp) |= FLG_RT_BOUND;

					/*
					 * Calculate the location of definition;
					 * symbol value plus base address of
					 * containing shared object.
					 */
					value = symdef->st_value;
					if (!(FLAGS(_lmp) & FLG_RT_ISMAIN) &&
					    (symdef->st_shndx != SHN_ABS))
						value += ADDR(_lmp);

					/*
					 * Retain this symbol index and the
					 * value in case it can be used for the
					 * subsequent relocations.
					 */
					if (rtype != R_SPARC_COPY) {
						psymndx = rsymndx;
						pvalue = value;
						pname = name;
						psymdef = symdef;
						psymref = symref;
						plmp = _lmp;
					}
				}

				/*
				 * If relocation is PC-relative, subtract
				 * offset address.
				 */
				if (IS_PC_RELATIVE(rtype))
					value -= roffset;

				DBG_CALL(Dbg_bind_global(NAME(lmp),
				    (caddr_t)roffset,
				    (caddr_t)(roffset - basebgn), -1,
				    NAME(_lmp), (caddr_t)value,
				    (caddr_t)symdef->st_value, name));
			}
		} else {
			value = basebgn;
			name = (char *)0;
		}

		/*
		 * Call relocation routine to perform required relocation.
		 */
		DBG_CALL(Dbg_reloc_in(rel, name, 0));

		switch (rtype) {
		case R_SPARC_COPY:
			if (elf_copy_reloc(name, symref, lmp, (void *)roffset,
			    symdef, _lmp, (const void *)value) == 0)
				return (0);
			break;
		case R_SPARC_JMP_SLOT:
			value += reladd;
#ifdef	PROF
			if (FLAGS(_lmp) & FLG_RT_PROFILE) {
				int	symndx = (((int)symdef -
					(int)SYMTAB(_lmp)) / SYMENT(_lmp));

				/*
				 * Write PLT entry to jump to profile counter
				 */
				elf_plt_cg_write((caddr_t)roffset,
					    _lmp, symndx);

				/*
				 * Call the call graph interpretor to
				 * initialize this first call element.
				 */
				(void) plt_cg_interp(symndx,
					(caddr_t)PRF_UNKNOWN, (caddr_t)value);
			} else {
#endif
				/*
				 * Write standard PLT entry to jump directly
				 * to newly bound function.
				 */
				DBG_CALL(Dbg_reloc_apply(roffset,
				    (unsigned long)value, 0));
				elf_plt_write((unsigned long *)roffset,
				    (unsigned long *)value);
#ifdef	PROF
			}
#endif
			break;
		default:
			value += reladd;
			/*
			 * write the relocation out
			 */
			if (do_reloc(rtype, (unsigned char *)roffset,
			    (Word *)&value,
			    (name ? name : (const char *)"<unknown>"),
			    NAME(lmp)) == 0)
				error = 1;
			/*
			 * value now contains the 'bit-shifted' value
			 * that was or'ed into memory (this was set
			 * by do_reloc()).
			 */
			DBG_CALL(Dbg_reloc_apply((unsigned long)roffset,
			    value, 0));

			/*
			 * If this relocation is against a text segment
			 * we must make sure that the instruction
			 * cache is flushed.
			 */
			if (textrel)
				iflush_range((caddr_t) roffset, 0x4);
		}
	}

	if (error)
		return (0);

	/*
	 * All objects with BOUND flag set hold definitions for the object
	 * we just relocated.  Call bound_add() to save those references.
	 */
	for (_lmp = LIST(lmp)->lm_head; _lmp; _lmp = (Rt_map *)NEXT(_lmp)) {
		if (FLAGS(_lmp) & FLG_RT_BOUND) {
			if (!error && !(bound_add(lmp, _lmp)))
				error = 1;
			FLAGS(_lmp) &= ~FLG_RT_BOUND;
		}
	}
	if (error)
		return (0);

	/*
	 * If we write enabled the text segment to perform these relocations
	 * re-protect by disabling writes.
	 */
	if (textrel) {
		(void) elf_set_prot(lmp, 0);
		textrel = 0;
	}

	return (1);
}
