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
#pragma ident	"@(#)elf.c	1.49	95/07/31 SMI"

/*
 * Object file dependent support for ELF objects.
 */
#include	<stdio.h>
#include	<sys/mman.h>
#include	<unistd.h>
#include	<string.h>
#include	<limits.h>
#include	<dlfcn.h>
#include	<errno.h>
#include	"paths.h"
#include	"_rtld.h"
#include	"_elf.h"
#include	"debug.h"
#include	"profile.h"

/*
 * Directory search rules for ELF objects.
 */
static int		elf_search_rules[] = {
	ENVDIRS,	RUNDIRS,	DEFAULT,	0
};

static Pnode		elf_dflt_dirs[] = {
	{ LIBDIR,	LIBDIRLEN,	(Pnode *)NULL }
};

/*
 * Defines for local functions.
 */
static int		elf_are_u();
static unsigned long	elf_entry_pt();
static Rt_map *		elf_map_so();
static Rt_map *		elf_new_lm();
static int		elf_unmap_so();
static int		elf_needed();
static Sym *		elf_find_sym();
static char *		elf_get_so();
static void		elf_dladdr();
static Sym *		elf_dlsym();

/*
 * Functions and data accessed through indirect pointers.
 */
Fct elf_fct = {
	elf_are_u,
	elf_entry_pt,
	elf_map_so,
	elf_new_lm,
	elf_unmap_so,
	elf_needed,
	lookup_sym,
	elf_find_sym,
	elf_reloc,
	elf_search_rules,
	elf_dflt_dirs,
	elf_dflt_dirs,
	NULL,
	elf_get_so,
	elf_dladdr,
	elf_dlsym
};


/*
 * For ABI compliance, if we are asked for ld.so.1, then really give them
 * libsys.so.1 (libsys.so.1 SONAME is ld.so.1).
 */
static const char *
elf_fix_name(const char * name)
{
	const char * _name = "/usr/lib/libsys.so.1";

	PRF_MCOUNT(40, elf_fix_name);

	if ((strcmp(name, ldso_path) == 0) ||
	    (strcmp(name, ldso_name) == 0)) {
		DBG_CALL(Dbg_file_fixname(name, _name));
		return (_name);
	} else
		return (name);
}

/*
 * Determine if we have been given an ELF file.  Returns 1 if true.
 */
static int
elf_are_u()
{
	PRF_MCOUNT(30, elf_are_u);
	if (fmap->fm_fsize < sizeof (Ehdr) ||
	    fmap->fm_maddr[EI_MAG0] != ELFMAG0 ||
	    fmap->fm_maddr[EI_MAG1] != ELFMAG1 ||
	    fmap->fm_maddr[EI_MAG2] != ELFMAG2 ||
	    fmap->fm_maddr[EI_MAG3] != ELFMAG3) {
		return (0);
	}
	return (1);
}

/*
 * Return the entry point of the ELF executable.
 */
static unsigned long
elf_entry_pt()
{
	PRF_MCOUNT(31, elf_entry_pt);
	return (ENTRY(lml_main.lm_head));
}

/*
 * Unmap a given ELF shared object from the address space.
 */
static int
elf_unmap_so(Rt_map * lmp)
{
	Phdr *	phdr;
	int	addr, msize, cnt, first;

	PRF_MCOUNT(32, elf_unmap_so);

	/*
	 * If this link map represents a relocatable object concatenation then
	 * the image was simply generated in allocated memory.  Free the memory.
	 */
	if (FLAGS(lmp) & FLG_RT_ALLOC) {
		(void) free((void *)ADDR(lmp));
		return (1);
	}

	/*
	 * Otherwise this object is an mmap()'ed image. Unmap each segment.
	 * Determine the first loadable program header.  Once we've unmapped
	 * this segment we're done.
	 */
	phdr = PHDR(lmp);
	for (first = 0; first < (int)(PHNUM(lmp));
	    phdr = (Phdr *)((unsigned long)phdr + PHSZ(lmp)), first++)
		if (phdr->p_type == PT_LOAD)
			break;

	/*
	 * Segments are unmapped in reverse order as the program headers are
	 * part of the first segment (page).
	 */
	phdr = (Phdr *)((unsigned long)PHDR(lmp) +
		((PHNUM(lmp) - 1) * (PHSZ(lmp))));
	for (cnt = (int)PHNUM(lmp); cnt > first; cnt--) {
		if (phdr->p_type == PT_LOAD) {
			addr = phdr->p_vaddr + ADDR(lmp);
			msize = phdr->p_memsz + (addr - M_PTRUNC(addr));
			(void) munmap((caddr_t)M_PTRUNC(addr), msize);
		}
		phdr = (Phdr *)((unsigned long)phdr - PHSZ(lmp));
	}
	return (1);
}

/*
 * Determine if a dependency requires a particular version and if so verify
 * that the version exists in the dependency.
 */
static int
elf_vers(const char * name, Rt_map * clmp, Rt_map * nlmp)
{
	Verneed *	vnd;
	int		num, _num;
	char *		cstrs = (char *)STRTAB(clmp);
	int		found = 0;

	vnd = VERNEED(clmp);
	num = VERNEEDNUM(clmp);

	/*
	 * Traverse the callers version needed information and determine if any
	 * specific versions are required from the dependency.
	 */
	for (_num = 1; _num <= num; _num++,
	    vnd = (Verneed *)((Word)vnd + vnd->vn_next)) {
		unsigned char	cnt = vnd->vn_cnt;
		Vernaux *	vnap;
		char *		nstrs, * need;

		/*
		 * Determine if a needed entry matches this dependency.
		 */
		need = (char *)(cstrs + vnd->vn_file);
		if (strcmp(name, need) != 0)
			continue;

		if (found++ == 0) {
			DBG_CALL(Dbg_ver_need_title(NAME(clmp)));
			if (rtld_flags & RT_FL_VERBOSE)
				(void) printf("   find version=%s;\n", name);
		}

		/*
		 * Validate that each version required actually exists in the
		 * dependency.
		 */
		nstrs = (char *)STRTAB(nlmp);

		for (vnap = (Vernaux *)((Word)vnd + vnd->vn_aux); cnt;
		    cnt--, vnap = (Vernaux *)((Word)vnap + vnap->vna_next)) {
			char *		version, * define;
			Verdef *	vdf;
			unsigned long	num, _num;
			int		found;

			version = (char *)(cstrs + vnap->vna_name);
			DBG_CALL(Dbg_ver_need_entry(0, need, version));

			vdf = VERDEF(nlmp);
			num = VERDEFNUM(nlmp);
			found = 0;

			for (_num = 1; _num <= num; _num++,
			    vdf = (Verdef *)((Word)vdf + vdf->vd_next)) {
				Verdaux *	vdap;

				if (vnap->vna_hash != vdf->vd_hash)
					continue;

				vdap = (Verdaux *)((Word)vdf + vdf->vd_aux);
				define = (char *)(nstrs + vdap->vda_name);
				if (strcmp(version, define) != 0)
					continue;

				found++;
				break;
			}

			/*
			 * If we're being traced print out any matched version
			 * when the verbose (-v) option is in effect.  Always
			 * print any unmatched versions.
			 */
			if (tracing) {
				if (found) {
					if (rtld_flags & RT_FL_VERBOSE)
					    (void) printf("\t%s (%s) =>\t %s\n",
						need, version, NAME(nlmp));
				} else {
					(void) printf("\t%s (%s) =>\t (version "
					    "not found)\n", need, version);
				}
				continue;
			}

			/*
			 * If the version hasn't been found then this is a
			 * candidiate for a fatal error condition.  Weak
			 * version definition requirements are silently
			 * ignored.  Also, if the image inspected for a version
			 * definition has no versioning recorded at all then
			 * silently ignore this (this provides better backward
			 * compatibility to old images created prior to
			 * versioning being available).  Both of these skipped
			 * diagnostics are available under tracing (see above).
			 */
			if ((found == 0) && (num != 0) &&
			    (!(vnap->vna_flags & VER_FLG_WEAK))) {
				eprintf(ERR_FATAL, "%s: version `%s' not found "
				    "(required by file %s)", need, version,
				    NAME(clmp));
				return (0);
			}
		}
	}
	return (1);
}

/*
 * Search through the dynamic section for DT_NEEDED entries and perform one
 * of two functions.  If only the first argument is specified then load the
 * defined shared object, otherwise add the link map representing the defined
 * link map the the dlopen list.
 */
static int
elf_needed(Lm_list * lml, Rt_map * lmp, Permit * permit, Dl_obj * dlp)
{
	Rt_map	*	nlmp;
	Dyn *		need;
	const char *	name;

	PRF_MCOUNT(33, elf_needed);
	/*
	 * Process each shared object on needed list.
	 */
	if (DYN(lmp) == 0)
		return (1);

	for (need = (Dyn *)DYN(lmp); need->d_tag != DT_NULL; need++) {
		if (!((need->d_tag == DT_NEEDED) ||
		    ((need->d_tag == DT_USED) && (bind_mode == RTLD_NOW)))) {
			continue;
		}
		name = elf_fix_name((char *)STRTAB(lmp) + need->d_un.d_val);
		nlmp = is_so_loaded(lml, name);

		/*
		 * If the named link map does not already exist, load it.
		 */
		if (!nlmp) {
			DBG_CALL(Dbg_file_needed(name, NAME(lmp)));
			if (rtld_flags & (RT_FL_VERBOSE | RT_FL_SEARCH))
				(void) printf(Lddmsg_fndl, name, NAME(lmp));
			if ((nlmp = load_so(lml, name, lmp)) == 0)
				if (tracing)
					continue;
				else
					return (0);
		} else {
			if (rtld_flags & RT_FL_VERBOSE) {
				(void) printf(Lddmsg_fndl, name, NAME(lmp));
				if (*name == '/')
					(void) printf(Lddmsg_lflp, name);
				else
					(void) printf(Lddmsg_lequ, name,
						NAME(nlmp));
			}
		}

		PERMIT(nlmp) = perm_set(PERMIT(nlmp), permit);
		FLAGS(nlmp) |= (FLAGS(lmp) &
		    (FLG_RT_PROMISC | FLG_RT_NODELETE));
		DBG_CALL(Dbg_file_ref(NAME(nlmp), COUNT(nlmp), PERMIT(nlmp),
		    (FLAGS(nlmp) & FLG_RT_PROMISC)));

		/*
		 * If this dependency is associated with a required version
		 * insure that the version is present in the loaded file.
		 */
		if (!(rtld_flags & RT_FL_NOVERSION) && VERNEED(lmp)) {
			if (elf_vers(name, lmp, nlmp) == 0)
				return (0);
		}

		/*
		 * If this request comes from a dlopen, add the new link map to
		 * the dlp dependent link map list.
		 */
		if (dlp)
			if (!dlp_listadd(nlmp, dlp))
				return (0);
	}
	return (1);
}

/*
 * Compute the elf hash value (as defined in the ELF access library).
 * The form of the hash table is:
 *
 *	|--------------|
 *	| # of buckets |
 *	|--------------|
 *	| # of chains  |
 *	|--------------|
 *	|   bucket[]   |
 *	|--------------|
 *	|   chain[]    |
 *	|--------------|
 */
unsigned long
elf_hash(const char * ename)
{
	unsigned long	hval = 0;

	PRF_MCOUNT(34, elf_hash);

	while (*ename) {
		unsigned long	g;
		hval = (hval << 4) + *ename++;
		if ((g = (hval & 0xf0000000)) != 0)
			hval ^= g >> 24;
		hval &= ~g;
	}
	return (hval);
}

/*
 * If flag argument has LKUP_SPEC set, we treat undefined symbols of type
 * function specially in the executable - if they have a value, even though
 * undefined, we use that value.  This allows us to associate all references
 * to a function's address to a single place in the process: the plt entry
 * for that function in the executable.  Calls to lookup from plt binding
 * routines do NOT set LKUP_SPEC in the flag.
 */
static Sym *
elf_find_sym(const char * ename, Rt_map * lmp, Rt_map ** dlmp,
	int flag, unsigned long hash)
{
	unsigned long	ndx, htmp, buckets;
	Sym *		sym;
	Sym *		symtabptr;
	char *		strtabptr, * name;
	unsigned long *	chainptr;

	PRF_MCOUNT(35, elf_find_sym);
	DBG_CALL(Dbg_syms_lookup(ename, NAME(lmp), (const char *)"ELF"));

	if (HASH(lmp) == 0)
		return ((Sym *)0);

	buckets = HASH(lmp)[0];
	htmp = hash % buckets;

	/*
	 * Get the first symbol on hash chain and initialize the string
	 * and symbol table pointers.
	 */
	ndx = HASH(lmp)[htmp + 2];
	chainptr = HASH(lmp) + 2 + buckets;
	strtabptr = STRTAB(lmp);
	symtabptr = SYMTAB(lmp);

	while (ndx) {
		sym = symtabptr + ndx;
		name = strtabptr + sym->st_name;

		/*
		 * Compare the symbol found with the name required.  If the
		 * names don't match continue with the next hash entry.
		 */
		if ((*name++ != *ename) || strcmp(name, &ename[1])) {
			ndx = chainptr[ndx];
			continue;

		/*
		 * If we find a match and the symbol is defined, return the
		 * symbol pointer and the link map in which it was found.
		 */
		} else if (sym->st_shndx != SHN_UNDEF) {
			*dlmp = lmp;
			return (sym);

		/*
		 * If we find a match and the symbol is undefined, the
		 * symbol type is a function, and the value of the symbol
		 * is non zero, then this is a special case.  This allows
		 * the resolution of a function address to the plt[] entry.
		 * See SPARC ABI, Dynamic Linking, Function Addresses for
		 * more details.
		 */
		} else if (((flag & LKUP_SPEC) == LKUP_SPEC) &&
		    (FLAGS(lmp) & FLG_RT_ISMAIN) && (sym->st_value != 0) &&
		    (ELF_ST_TYPE(sym->st_info) == STT_FUNC)) {
			*dlmp = lmp;
			return (sym);
		}

		/*
		 * Local or undefined symbol.
		 */
		break;
	}

	/*
	 * If here, then no match was found.
	 */
	return ((Sym *)0);
}

static caddr_t
elf_map_it(
	const char *	name,		/* actual name stored for pathname */
	Off		mlen,		/* total mapping claim */
	Ehdr *		ehdr,		/* ELF header of file */
	Phdr **		phdr,		/* first Phdr in file */
	Phdr *		fph,		/* first loadable Phdr */
	Phdr *		lph,		/* last loadable Phdr */
	int		main)
{
	int		j, r;		/* general temporary */
	caddr_t		addr;		/* working mapping address */
	caddr_t		faddr;		/* first program mapping address */
	caddr_t		maddr;		/* pointer to mapping claim */
	caddr_t		zaddr;		/* /dev/zero working mapping addr */
	Off		foff;		/* file offset for segment mapping */
	Off		_flen;		/* file length for segment mapping */
	Off		_mlen;		/* memory length for segment mapping */
	Phdr *		pptr;		/* working Phdr */
	int		dz_fd;		/* File descriptor for anon memory */
	int		init_perm = 0;	/* initial mmap perms */

	PRF_MCOUNT(36, elf_map_it);

	/*
	 * Determine whether or not to let system reserve address space
	 * based on whether or not we have a "dynamic" executable.  Determine
	 * amount of address space to be used.
	 */
	maddr = ehdr->e_type == ET_DYN ? 0 :
	    (caddr_t)S_ALIGN(fph->p_vaddr, syspagsz);

	/*
	 * determine the intial permisions we will use to map in
	 * the first segment.
	 */
	if (fph->p_flags & PF_R)
		init_perm |= PROT_READ;
	if (fph->p_flags & PF_X)
		init_perm |= PROT_EXEC;
	if (fph->p_flags & PF_W)
		init_perm |= PROT_WRITE;

	foff = S_ALIGN(fph->p_offset, syspagsz);
	_mlen = fph->p_memsz + (fph->p_offset - foff);

	/*
	 * Map enough address space to hold the program (as opposed to the
	 * file) represented by ld.so.  The amount to be assigned is the
	 * range between the end of the last loadable segment and the
	 * beginning of the first PLUS the alignment of the first segment.
	 * mmap() can assign us any page-aligned address, but the relocations
	 * assume the alignments included in the program header.  As an
	 * optimization, however, let's assume that mmap() will actually
	 * give us an aligned address -- since if it does, we can save
	 * an munmap() later on.  If it doesn't -- then go try it again.
	 *
	 * N.B.: change this to PROT_NONE when 2001568 is fixed.
	 */
	if ((maddr = (caddr_t)mmap(maddr, mlen, init_perm,
	    MAP_PRIVATE, fmap->fm_fd, 0)) == (caddr_t)-1) {
		eprintf(ERR_FATAL, Errmsg_cmfl, name, errno);
		return (0);
	}
	faddr = (caddr_t)S_ROUND(maddr, fph->p_align);

	if (!(fph->p_flags & PF_W))
		fmap->fm_etext = fph->p_vaddr + fph->p_memsz +
		    (unsigned long)(main ? 0 : faddr);

	/*
	 * Check to see whether alignment skew was really needed.
	 */
	if (faddr != maddr) {
		(void) munmap(maddr, mlen);
		maddr = ehdr->e_type == ET_DYN ? 0 :
		    (caddr_t)S_ALIGN(fph->p_vaddr, fph->p_align);
		if ((maddr = (caddr_t)mmap(maddr, mlen + fph->p_align,
		    init_perm, MAP_PRIVATE, fmap->fm_fd, 0)) == (caddr_t)-1) {
			eprintf(ERR_FATAL, Errmsg_cmfl, name, errno);
			return (0);
		}
		faddr = (caddr_t)S_ROUND(maddr, fph->p_align);
		(void) munmap(maddr, faddr - maddr);
		if ((maddr = (caddr_t)mmap(faddr, mlen, init_perm,
		    MAP_FIXED | MAP_PRIVATE, fmap->fm_fd, 0)) == (caddr_t)-1) {
			eprintf(ERR_FATAL, Errmsg_cmfl, name, errno);
			return (0);
		}
	}

	/*
	 * The first loadable segment now pointed to by maddr, and since
	 * the first loadable segment contains the elf header and program
	 * headers.  Reset the program header (which will be saved in
	 * the objects link map, and may be used for later unmapping
	 * operations) to this mapping so that we can unmap the original
	 * first page on return.
	 */
	/* LINTED */
	*phdr = (Phdr *)((char *)maddr + ((Ehdr *)maddr)->e_ehsize);

	foff = S_ALIGN(fph->p_offset, syspagsz);
	_mlen = fph->p_memsz + (fph->p_offset - foff);
	maddr += M_PROUND(_mlen);
	mlen -= M_PROUND(_mlen);


	/*
	 * We have the address space reserved, so map each loadable segment.
	 */
	for (pptr = (Phdr *)((Off)fph + ehdr->e_phentsize);
	    pptr <= lph;
	    pptr = (Phdr *)((Off)pptr + ehdr->e_phentsize)) {

		/*
		 * Skip non-loadable segments or segments that don't occupy
		 * any memory.
		 */
		if ((pptr->p_type != PT_LOAD) || (pptr->p_memsz == 0))
			continue;

		/*
		 * Determine the file offset to which the mapping will be
		 * directed (must be aligned) and how much to map (might
		 * be more than the file in the case of .bss).
		 */
		foff = S_ALIGN(pptr->p_offset, syspagsz);
		_flen = pptr->p_filesz + (pptr->p_offset - foff);
		_mlen = pptr->p_memsz + (pptr->p_offset - foff);

		/*
		 * Set address of this segment relative to our base.
		 */
		addr = (caddr_t)S_ALIGN(pptr->p_vaddr + (main ? 0 : faddr),
		    syspagsz);

		/*
		 * Unmap anything from the last mapping address to this one.
		 */
		if (addr - maddr) {
			(void) munmap(maddr, addr - maddr);
			mlen -= addr - maddr;
		}

		/*
		 * Determine the mapping protection from the section
		 * attributes.  Also determine the etext address from the
		 * last loadable segment which has no write access.
		 */
		j = 0;
		if (pptr->p_flags & PF_R)
			j |= PROT_READ;
		if (pptr->p_flags & PF_X)
			j |= PROT_EXEC;
		if (pptr->p_flags & PF_W)
			j |= PROT_WRITE;
		else
			fmap->fm_etext = pptr->p_vaddr + pptr->p_memsz +
			    (unsigned long)(main ? 0 : faddr);
		if ((caddr_t)mmap((caddr_t)addr, _flen, j,
		    MAP_FIXED | MAP_PRIVATE, fmap->fm_fd, foff) ==
		    (caddr_t)-1) {
			eprintf(ERR_FATAL, Errmsg_cmsg, name, errno);
			return (0);
		}

		/*
		 * If the memory occupancy of the segment overflows the
		 * definition in the file, we need to "zero out" the
		 * end of the mapping we've established, and if necessary,
		 * map some more space from /dev/zero.
		 */
		if (pptr->p_memsz > pptr->p_filesz) {
			foff = (Off) (faddr + pptr->p_vaddr +
			    pptr->p_filesz);
			zaddr = (caddr_t)M_PROUND(foff);
			zero((caddr_t)foff, (int)(zaddr - foff));
			r = (faddr + pptr->p_vaddr + pptr->p_memsz) - zaddr;
			if (r > 0) {
				if ((dz_fd = dz_open()) == DZ_UNAVAIL)
					return (0);
				if ((caddr_t)mmap((caddr_t)zaddr, r, j,
				    MAP_FIXED|MAP_PRIVATE, dz_fd, 0) ==
				    (caddr_t)-1) {
					eprintf(ERR_FATAL, Errmsg_cmdz, errno);
					return (0);
				}
			}
		}

		/*
		 * Update the mapping claim pointer.
		 */
		maddr = addr + M_PROUND(_mlen);
		mlen -= maddr - addr;
	}

	/*
	 * Unmap any final reservation.
	 */
	if (mlen != 0)
		(void) munmap(maddr, mlen);

	return (faddr);
}

/*
 * Map in an ELF object.
 * Takes an open file descriptor for the object to map and its pathname; returns
 * a pointer to a Rt_map structure for this object, or 0 on error.
 */
/* ARGSUSED2 */
static Rt_map *
elf_map_so(Lm_list * lml, const char * pname, const char * profile_n)
{
	int		i; 		/* general temporary */
	Off		memsize = 0;	/* total memory size of pathname */
	Off		mentry;		/* entry point */
	Ehdr *		ehdr;		/* ELF header of ld.so */
	Phdr *		phdr;		/* first Phdr in file */
	Phdr *		pptr;		/* working Phdr */
	Phdr *		lph;		/* last loadable Phdr */
	Phdr *		fph = 0;	/* first loadable Phdr */
	Dyn *		mld;		/* DYNAMIC structure for pathname */
	size_t		size;		/* size of elf and program headers */
	caddr_t		faddr;		/* mapping address of pathname */
	Rt_map *	lmp;		/* link map created */
	const char *	name;
	int		main = 0;

	PRF_MCOUNT(38, elf_map_so);
	/*
	 * Is object the executable?
	 */
	if (pname == (char *)0) {
		main++;
		name = pr_name;
	} else
		name = pname;

	/*
	 * Check class and encoding.
	 */
	/* LINTED */
	ehdr = (Ehdr *)fmap->fm_maddr;
	if (ehdr->e_ident[EI_CLASS] != M_CLASS ||
		ehdr->e_ident[EI_DATA] != M_DATA) {
		eprintf(ERR_ELF, "%s: wrong class or data encoding", name);
		return (0);
	}

	/*
	 * Check machine type and flags.
	 */
	if (ehdr->e_machine != M_MACH) {
		if (ehdr->e_machine != M_MACHPLUS) {
			eprintf(ERR_ELF, "%s: bad machine type", name);
			return (0);
		}
		if ((ehdr->e_flags & M_FLAGSPLUS) == 0) {
			eprintf(ERR_ELF, "%s: bad machine type", name);
			return (0);
		}
		if ((ehdr->e_flags & ~flags) & M_FLAGSPLUS_MASK) {
			eprintf(ERR_ELF, "%s: bad flags value %x", name,
			    ehdr->e_flags);
			return (0);
		}
	} else if (ehdr->e_flags != 0) {
		eprintf(ERR_ELF, "%s: bad flags value %x", name,
		    ehdr->e_flags);
		return (0);
	}

	/*
	 * Verify ELF version.  ??? is this too restrictive ???
	 */
	if (ehdr->e_version > EV_CURRENT) {
		eprintf(ERR_ELF, "%s: bad file version", name);
		return (0);
	}

	/*
	 * Check magic number.
	 */
	if (main) {
		if (ehdr->e_type != ET_EXEC) {
			eprintf(ERR_ELF, "%s: not an executable file", name);
			return (0);
		}
	} else {
		/*
		 * Shared or relocatable object.
		 */
		if (!((ehdr->e_type == ET_DYN) || (ehdr->e_type == ET_REL))) {
			eprintf(ERR_ELF, "%s: not a shared or relocatable "
			    "object", name);
			return (0);
		}
		if (ehdr->e_type == ET_REL)
			return (elf_obj_file(lml, name));
	}

	/*
	 * If our original mapped page was not large enough to hold all
	 * the program headers remap them.
	 */
	size = (size_t)((char *)ehdr->e_phoff +
		(ehdr->e_phnum * ehdr->e_phentsize));
	if (fmap->fm_fsize < size) {
		eprintf(ERR_FATAL, Errmsg_cotf, name);
		return (0);
	}
	if (size > fmap->fm_msize) {
		(void) munmap((caddr_t)fmap->fm_maddr, fmap->fm_msize);
		if ((fmap->fm_maddr = (char *)mmap(0, size, PROT_READ,
			MAP_SHARED, fmap->fm_fd, 0)) == (char *)-1) {
			eprintf(ERR_FATAL, Errmsg_cmfl, name, errno);
			return (0);
		}
		fmap->fm_msize = size;
		/* LINTED */
		ehdr = (Ehdr *)fmap->fm_maddr;
	}
	/* LINTED */
	phdr = (Phdr *)((char *)ehdr + ehdr->e_ehsize);

	/*
	 * Get entry point.
	 */
	mentry = ehdr->e_entry;

	/*
	 * Point at program headers and perform some basic validation.
	 */
	for (i = 0, pptr = phdr; i < (int)ehdr->e_phnum; i++,
	    pptr = (Phdr *)((Off)pptr + ehdr->e_phentsize)) {
		if (pptr->p_type == PT_LOAD) {
			if (fph == 0) {
				fph = pptr;
			/* LINTED argument lph is initialized in first pass */
			} else if (pptr->p_vaddr <= lph->p_vaddr) {
				eprintf(ERR_ELF, "%s: invalid program "
				    "header, segments out of order", name);
				return (0);
			}
			lph = pptr;
		} else if (pptr->p_type == PT_DYNAMIC)
			mld = (Dyn *)(pptr->p_vaddr);
	}

	/*
	 * We'd better have at least one loadable segment.
	 */
	if (fph == 0) {
		eprintf(ERR_ELF, "%s: no loadable segments found", name);
		return (0);
	}

	/*
	 * Check that the files size accounts for the loadable sections
	 * we're going to map in (failure to do this may cause spurious
	 * bus errors if we're given a truncated file).
	 */
	if (fmap->fm_fsize < ((size_t)lph->p_offset + lph->p_filesz)) {
		eprintf(ERR_FATAL, Errmsg_cotf, name);
		return (0);
	}

	memsize = M_PROUND((lph->p_vaddr + lph->p_memsz) -
	    S_ALIGN(fph->p_vaddr, syspagsz));

	/*
	 * Map the complete file if necessary.
	 */
	if (interp && !(strcmp(name, interp->i_name))) {
		/*
		 * If this is the interpreter then it has already been mapped
		 * and we have the address so don't map it again.
		 */
		faddr = interp->i_faddr;
		/* LINTED */
		phdr = (Elf32_Phdr *)((char *)faddr + ehdr->e_ehsize);

	} else if ((memsize <= fmap->fm_msize) &&
	    ((fph->p_flags & PF_W) == 0)) {
		/*
		 * If the mapping required has already been established from
		 * the initial page we don't need to do anything more.  Reset
		 * the fmap address so then any later files start a new fmap.
		 * This is really an optimization for filters, such as libdl.so,
		 * which should only require one page.
		 */
		faddr = fmap->fm_maddr;
		fmap->fm_mflags &= ~MAP_FIXED;
		fmap->fm_maddr = 0;
		/* LINTED */
		phdr = (Elf32_Phdr *)((char *)faddr + ehdr->e_ehsize);

	} else {
		/*
		 * Map the file.
		 */
		if (!(faddr = elf_map_it(name, memsize, ehdr, &phdr,
		    fph, lph, main)))
			return (0);
	}

	/*
	 * Calculate absolute base addresses and entry points.
	 */
	if (!main) {
		/* LINTED */
		mld = (Dyn *)((Off)mld + faddr);
		mentry += (Off)faddr;
	}

	/*
	 * Create new link map structure for newly mapped shared object.
	 */
	if (!(lmp = elf_new_lm(lml, pname, mld, (unsigned long)faddr,
	    fmap->fm_etext, memsize, mentry, phdr, ehdr->e_phnum,
	    ehdr->e_phentsize))) {
		(void) munmap((caddr_t)faddr, memsize);
		return (0);
	}

#ifdef	PROF
	/*
	 * If the filename matches the profile name, set up for profil(2).
	 */
	if (profile_name && (FLAGS(lml->lm_head) & FLG_RT_ISMAIN) &&
	    (strcmp(profile_name, profile_n) == 0))
		FLAGS(lmp) |= profile(profile_n, (void*) lmp);
#endif
	return (lmp);
}

/*
 * Find symbol interpreter.
 * This function is called when the symbols from a shared object should
 * be resolved from the shared objects reference link map instead of
 * from within ifself.
 */
static Sym *
elf_intp_find_sym(const char * ename, Rt_map * lmp, Rt_map ** dlmp,
	int flag, unsigned long hash)
{
	Rt_map *	rlmp, * tlmp;
	Listnode *	lnp;
	Sym *		sym;
	Sym *		fsym;
	Dl_obj *	dlp;

	PRF_MCOUNT(39, elf_intp_find_sym);
	/*
	 * First check that the symbol is actually defined in the filter.
	 *
	 * If the filter is a weak filter and the symbol cannot be resolved
	 * in the filtee, then use the filter's symbol.
	 */
	if ((fsym = LM_FIND_SYM(lmp)(ename, lmp, dlmp, flag, hash)) == 0)
		return ((Sym *)0);

	/*
	 * If this is the first instance of calling through the filter then
	 * effectively dlopen the reference object(s) and set up a DLP to
	 * provide for symbol lookup.
	 */
	if (!REFLM(lmp)) {
		const char *	filter = REFNAME(lmp);
		char *		nfilter = (char *)NULL;
		Permit *	permit = 0;
		int		ret;

		DBG_CALL(Dbg_file_filter(filter, NAME(lmp)));

		/*
		 * handle $PLATFORM token
		 */
		ret = do_platform_token((char *)filter, &nfilter);
		if (ret == 1) {
			filter = REFNAME(lmp) = nfilter;
			FLAGS(lmp) |= FLG_RT_FLTALLOC;
		} else if (ret == -1) {
			if (FLAGS(lmp) & FLG_RT_AUX) {
				SYMINTP(lmp) = elf_find_sym;
				REFNAME(lmp) = (char *)0;
				FLAGS(lmp) &= ~FLG_RT_AUX;
				return (fsym);
			}
			return ((Sym *)0);
		}

		/*
		 * Determine if the reference link map is already loaded.
		 */
		rlmp = lml_rtld.lm_head;
		if (strcmp(filter, NAME(rlmp)))
			rlmp = is_so_loaded(&lml_main, filter);

		if (!rlmp || !DLP(rlmp))
			if ((permit = perm_get()) == 0)
				return ((Sym *)0);

		if (rlmp) {
			if ((dlp = dl_old_so(rlmp, permit, RTLD_LAZY)) ==
			    (Dl_obj *)0)
				return ((Sym *)0);
		} else {
			/*
			 * If this object isn't loaded go ahead and get it.
			 */
			if ((dlp = dl_new_so(filter, lmp, &rlmp, permit,
			    RTLD_LAZY)) == (Dl_obj *)0) {
				/*
				 * if the filter is a weak filter and
				 * the filtee does not exist, then resolve
				 * symbols from within the filter itself
				 */
				if (FLAGS(lmp) & FLG_RT_AUX) {
					SYMINTP(lmp) = elf_find_sym;
					REFNAME(lmp) = (char *)0;
					FLAGS(lmp) &= ~FLG_RT_AUX;
					return (fsym);
				}
				return ((Sym *)0);
			}

			if (rtld_flags & RT_FL_APPLIC)
				call_init(rlmp);
		}
		REFLM(lmp) = rlmp;
	}


	/*
	 * Lookup the symbol using the referenced link maps dlp information.
	 */
	dlp = DLP((Rt_map *)REFLM(lmp));
	for (LIST_TRAVERSE(&dlp->dl_lmps, lnp, tlmp))
		if (sym = SYMINTP(tlmp)(ename, tlmp, dlmp, flag, hash)) {
			return (sym);
		}
	/*
	 * if this is a weak filter and the symbol cannot be resolved
	 * in the filtee, use the symbol from within the filter
	 */
	if (FLAGS(lmp) & FLG_RT_AUX) {
		return (fsym);
	}

	return ((Sym *)0);
}

/*
 * Create a new Rt_map structure for an ELF object and initialize
 * all values.
 */
static Rt_map *
elf_new_lm(Lm_list * lml, const char * pname, Dyn * ld, unsigned long addr,
	unsigned long etext, unsigned long msize, unsigned long entry,
	Phdr * phdr, unsigned int phnum, unsigned int phsize)
{
	Rt_map *	lmp;
	unsigned long	offset,	fltr = 0;
	int		rpath = 0;

	PRF_MCOUNT(41, elf_new_lm);
	DBG_CALL(Dbg_file_elf((pname ? pname : pr_name), (unsigned long)ld,
	    addr, msize, entry, (unsigned long)phdr, phnum));

	/*
	 * Allocate space.
	 */
	if ((lmp = (Rt_map *)calloc(sizeof (Rt_map), 1)) == 0)
		return (0);
	if ((ELFPRV(lmp) = (Rt_elfp *)calloc(sizeof (Rt_elfp), 1)) == 0)
		return (0);

	/*
	 * All fields not filled in were set to 0 by calloc.
	 */
	NAME(lmp) = (char *)pname;
	DYN(lmp) = ld;
	ADDR(lmp) = addr;
	MSIZE(lmp) = msize;
	ENTRY(lmp) = entry;
	PHDR(lmp) = (void *)phdr;
	PHNUM(lmp) = (unsigned short)phnum;
	PHSZ(lmp) = (unsigned short)phsize;
	SYMINTP(lmp) = elf_find_sym;
	ETEXT(lmp) = etext;
	FCT(lmp) = &elf_fct;
	LIST(lmp) = lml;

	/*
	 * Fill in rest of the link map entries with info the from the file's
	 * dynamic structure.  If shared object, add base address to each
	 * address; if executable, use address as is.
	 */
	if (pname)
		offset = addr;
	else
		offset = 0;

	/*
	 * Read dynamic structure into an array of ptrs to Dyn unions;
	 * array[i] is pointer to Dyn with tag == i.
	 */
	if (ld) {
		/* CSTYLED */
		for ( ; ld->d_tag != DT_NULL; ++ld) {
			switch (ld->d_tag) {
			case DT_SYMTAB:
				SYMTAB(lmp) = (char *)ld->d_un.d_ptr + offset;
				break;
			case DT_STRTAB:
				STRTAB(lmp) = (char *)ld->d_un.d_ptr + offset;
				break;
			case DT_SYMENT:
				SYMENT(lmp) = ld->d_un.d_val;
				break;
			case DT_REL:
			case DT_RELA:
				/*
				 * At this time we can only handle 1 type of
				 * relocation per object.
				 */
				REL(lmp) = (char *)ld->d_un.d_ptr + offset;
				break;
			case DT_RELSZ:
			case DT_RELASZ:
				RELSZ(lmp) = ld->d_un.d_val;
				break;
			case DT_RELENT:
			case DT_RELAENT:
				RELENT(lmp) = ld->d_un.d_val;
				break;
			case DT_HASH:
				HASH(lmp) = (unsigned long *)(ld->d_un.d_ptr +
					offset);
				break;
			case DT_PLTGOT:
				PLTGOT(lmp) = (unsigned long *)(ld->d_un.d_ptr +
					offset);
				break;
			case DT_PLTRELSZ:
				PLTRELSZ(lmp) = ld->d_un.d_val;
				break;
			case DT_JMPREL:
				JMPREL(lmp) = (char *)(ld->d_un.d_ptr) + offset;
				break;
			case DT_INIT:
				INIT(lmp) = (void (*)())((unsigned long)
					ld->d_un.d_ptr + offset);
				break;
			case DT_FINI:
				FINI(lmp) = (void (*)())((unsigned long)
					ld->d_un.d_ptr + offset);
				break;
			case DT_SYMBOLIC:
				SYMBOLIC(lmp) = 1;
				break;
			case DT_RPATH:
				rpath = ld->d_un.d_val;
				break;
			case DT_FILTER:
				fltr = ld->d_un.d_val;
				break;
			case DT_AUXILIARY:
				fltr = ld->d_un.d_val;
				FLAGS(lmp) |= FLG_RT_AUX;
				break;
			case DT_DEBUG:
				/*
				 * DT_DEBUG entries are only created in
				 * executables, and provide for a hand-shake
				 * with debuggers.  This entry is initialized
				 * to zero by the link-editor.  If a debugger
				 * has invoked us and updated this entry simply
				 * set the debugger flag, and finish
				 * initializing the debugging structure (see
				 * setup() also).
				 */
				if (ld->d_un.d_ptr)
					FLAGS(lmp) |= FLG_RT_DEBUGGER;
				ld->d_un.d_ptr = (Addr)&r_debug;
				break;
			case DT_VERNEED:
				VERNEED(lmp) = (Verneed *)((unsigned long)
				    ld->d_un.d_ptr + offset);
				break;
			case DT_VERNEEDNUM:
				VERNEEDNUM(lmp) = ld->d_un.d_val;
				break;
			case DT_VERDEF:
				VERDEF(lmp) = (Verdef *)((unsigned long)
				    ld->d_un.d_ptr + offset);
				break;
			case DT_VERDEFNUM:
				VERDEFNUM(lmp) = ld->d_un.d_val;
				break;
			}
		}
	}

	if (rpath)
		RPATH(lmp) = (char *)(rpath + (char *)STRTAB(lmp));
	if (fltr) {
		REFNAME(lmp) = (char *)(fltr + (char *)STRTAB(lmp));
		SYMINTP(lmp) = elf_intp_find_sym;
	}

	/*
	 * For Intel ABI compatibility.  It's possible that a JMPREL can be
	 * specified without any other relocations (e.g. a dynamic executable
	 * normally only contains .plt relocations).  If this is the case then
	 * no REL, RELSZ or RELENT will have been created.  For us to be able
	 * to traverse the .plt relocations under LD_BIND_NOW we need to know
	 * the RELENT for these relocations.  Refer to elf_reloc() for more
	 * details.
	 */
	if (!RELENT(lmp) && JMPREL(lmp))
		RELENT(lmp) = sizeof (Rel);

	/*
	 * Add the mapped object to the end of the link map list.
	 */
	lm_append(lml, lmp);

	return (lmp);
}

/*
 * Function to correct protection settings.
 * Segments are all mapped initially with permissions as given in
 * the segment header, but we need to turn on write permissions
 * on a text segment if there are any relocations against that segment,
 * and them turn write permission back off again before returning control
 * to the program.  This function turns the permission on or off depending
 * on the value of the argument.
 */
int
elf_set_prot(Rt_map * lmp, int permission)
{
	int		i, prot;
	Phdr *		phdr;
	size_t		size;
	unsigned long	addr;

	PRF_MCOUNT(42, elf_set_prot);

	/*
	 * If this is an allocated image (ie. a relocatable object) we can't
	 * mprotect() anything.
	 */
	if (FLAGS(lmp) & FLG_RT_ALLOC)
		return (1);

	DBG_CALL(Dbg_file_prot(NAME(lmp), permission));

	phdr = (Phdr *)PHDR(lmp);

	/*
	 * Process all loadable segments.
	 */
	for (i = 0; i < (int)PHNUM(lmp); i++) {
		if ((phdr->p_type == PT_LOAD) &&
		    ((phdr->p_flags & PF_W) == 0)) {
			prot = PROT_READ | permission;
			if (phdr->p_flags & PF_X)
				prot |=  PROT_EXEC;
			addr = (unsigned long)phdr->p_vaddr +
			    ((FLAGS(lmp) & FLG_RT_ISMAIN) ? 0 : ADDR(lmp));
			size = phdr->p_memsz + (addr - M_PTRUNC(addr));
			if (mprotect((caddr_t)M_PTRUNC(addr), size, prot) ==
			    -1) {
				eprintf(ERR_FATAL, Errmsg_csps, NAME(lmp),
				    errno);
				return (0);
			}
		}
		phdr = (Phdr *)((unsigned long)phdr + PHSZ(lmp));
	}
	return (1);
}

/*
 * Build full pathname of shared object from given directory name and filename.
 */
static char *
elf_get_so(char * dir, const char * file)
{
	static char	pname[PATH_MAX];

	PRF_MCOUNT(43, elf_get_so);
	(void) sprintf(pname, "%s/%s", dir, file);
	return (pname);
}

/*
 * Perform copy relocations.  If the size of the .bss area available for the
 * copy information is not the same as the source of the data inform the user
 * if we're under ldd(1) control (this checking was only established in 5.3,
 * so by only issuing an error via ldd(1) we maintain the standard set by
 * previous releases).
 * The copy relocation is recorded in a copy structure which will be applied
 * after all other relocations are carried out.  This provides for copying data
 * that has not yet been relocated itself (ie. pointers in shared objects).
 */
int
elf_copy_reloc(char * name, Sym * rsym, Rt_map * rlmp, void * ref, Sym * dsym,
	Rt_map * dlmp, const void * def)
{
	Rel_copy *	rcp;

	PRF_MCOUNT(83, elf_copy_reloc);
	/*
	 * Allocate a copy entry structure to hold the copy data information.
	 * These structures will be called in setup().
	 */
	if ((rcp = (Rel_copy *)malloc(sizeof (Rel_copy))) == 0) {
		if (!(rtld_flags & RT_FL_WARN))
			return (0);
		else
			return (1);
	}

	rcp->r_to = (char *)ref;
	if (rsym->st_size > dsym->st_size)
		rcp->r_size = (size_t)dsym->st_size;
	else
		rcp->r_size = (size_t)rsym->st_size;
	rcp->r_from = (char *)def;
	if (!copies)
		rcp->r_next = (Rel_copy *)0;
	else
		rcp->r_next = copies;
	copies = rcp;

	/*
	 * We can only copy as much data as the reference (dynamic executables)
	 * entry allows.  Determine the size from the reference symbol, and if
	 * we are tracing (ldd) warn the user if it differs from the copy
	 * definition.
	 */
	if ((rtld_flags & RT_FL_WARN) && (rsym->st_size != dsym->st_size)) {
		(void) printf("\tcopy relocation sizes differ: %s\n", name);
		(void) printf("\t\t(file %s size=%x; file %s size=%x);\n",
		    NAME(rlmp), rsym->st_size, NAME(dlmp), dsym->st_size);
		if (rsym->st_size > dsym->st_size)
			(void) printf("\t\t%s size used; possible insufficient "
			    "data copied\n", NAME(dlmp));
		else
			(void) printf("\t\t%s size used; possible data "
			    "truncation\n", NAME(rlmp));
	}

	DBG_CALL(Dbg_reloc_apply((Word)ref, (Word)rcp->r_size, 0));
	return (1);
}

/*
 * Determine the symbol location of an address within a link-map.  Look for
 * the nearest symbol (whoes value is less than or equal to the required
 * address).  This is the object specific part of dladdr().
 */
static void
elf_dladdr(unsigned long addr, Rt_map * lmp, Dl_info * dlip)
{
	unsigned long	ndx, cnt, base, value, _value;
	Sym *		sym;
	const char *	str, * _name;

	PRF_MCOUNT(84, elf_dladdr);

	/*
	 * If we don't have a .hash table there are no symbols to look at.
	 */
	if (HASH(lmp) == 0)
		return;

	cnt = HASH(lmp)[1];
	str = STRTAB(lmp);
	sym = SYMTAB(lmp);

	if (FLAGS(lmp) & FLG_RT_ISMAIN)
		base = 0;
	else
		base = ADDR(lmp);

	for (_value = 0, sym++, ndx = 1; ndx < cnt; ndx++, sym++) {
		if (sym->st_shndx == SHN_UNDEF)
			continue;

		value = sym->st_value + base;
		if (value > addr)
			continue;
		if (value < _value)
			continue;

		_value = value;
		_name =  str + sym->st_name;

		/*
		 * Note, because we accept local and global symbols we could
		 * find a section symbol that matches the associated address,
		 * which means that the symbol name will be null.  In this
		 * case continue the search in case we can find a global
		 * symmol of the same value.
		 */
		if ((value == addr) &&
		    (ELF_ST_TYPE(sym->st_info) != STT_SECTION))
			break;
	}

	if (_value) {
		dlip->dli_sname = _name;
		dlip->dli_saddr = (void *)_value;
	}
}

/*
 * Continue processing a dlsym request.  Lookup the required symbol in each
 * link-map specified by the dlp.  Note, that because this lookup is against
 * individual link-maps we don't need to cupply a permit or starting link-map
 * to the loopup routine (see lookup_sym():analyze.c).
 */
Sym *
elf_dlsym(Dl_obj * dlp, Rt_map * clmp, const char * name, Rt_map ** _lmp)
{
	Rt_map *	tlmp;
	Listnode *	lnp;
	Sym *		sym;

	for (LIST_TRAVERSE(&dlp->dl_lmps, lnp, tlmp)) {
		if (sym = LM_LOOKUP_SYM(clmp)(name, 0, 0, tlmp, _lmp,
		    (LKUP_DEFT | LKUP_FIRST)))
			return (sym);
	}
	return (0);
}
