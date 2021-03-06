/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All Rights Reserved
 */
#pragma ident	"@(#)_setup.c	1.35	95/07/27 SMI"

/*
 * SPARC specific setup routine  -  relocate ld.so's symbols, setup its
 * environment, map in loadable sections of the executable.
 *
 * Takes base address ld.so was loaded at, address of ld.so's dynamic
 * structure, address of process environment pointers, address of auxiliary
 * vector and * argv[0] (process name).
 * If errors occur, send process signal - otherwise
 * return executable's entry point to the bootstrap routine.
 */
#include	<signal.h>
#include	<stdlib.h>
#include	<fcntl.h>
#include	<sys/auxv.h>
#include	<sys/elf_SPARC.h>
#include	<sys/mman.h>
#include	<sys/types.h>
#include	<link.h>
#include	<unistd.h>
#include	<string.h>
#include	<dlfcn.h>
#include	"paths.h"
#include	"_rtld.h"
#include	"_elf.h"
#ifdef A_OUT
#include	"_a.out.h"
#endif A_OUT
#include	"profile.h"
#include	"debug.h"

extern int	_end;
extern int	_etext;
extern void	_init(void);
char **		envp;
int		environ = 0;

/*
 * Define for the executable's interpreter.
 * Usually it is ld.so.1, but for the first release of ICL binaries
 * it is libc.so.1.  We keep this information so that we don't end
 * up mapping libc twice if it is the interpreter.
 */
static Interp _interp;


/* VARARGS */
unsigned long
_setup(Boot * ebp, Dyn * ld_dyn)
{
	unsigned long	reladdr, relsize, etext, ld_base = 0;
	unsigned long	strtab, soname, entry, interp_base = 0;
	char *		c, * _rt_name, ** _envp, ** _argv, * _pr_name = 0;
	int		_syspagsz, phsize, phnum, i;
	int		_flags = 0, aoutflag = 0;
	Dyn *		dyn_ptr;
	Phdr *		phdr;
	Ehdr *		ehdr;
	int		fd = -1;
	int		dz_fd = DZ_UNAVAIL;
	Fct *		ftp;
	Rt_map *	lmp;
	auxv_t *	auxv, * _auxv;
	size_t		eaddr, esize;
	uid_t		uid = -1, euid = -1;
	gid_t		gid = -1, egid = -1;
	char *		_platform = 0;
#ifdef	A_OUT
	struct link_dynamic *aoutdyn;
#endif	A_OUT
#ifdef	PRF_RTLD
	unsigned long *	hashaddr;
#endif

	/*
	 * Scan the bootstrap structure to pick up the basics.
	 */
	for (; ebp->eb_tag != EB_NULL; ebp++)
		switch (ebp->eb_tag) {
		case EB_DYNAMIC:
			aoutflag = 1;
#ifdef A_OUT
			aoutdyn = (struct link_dynamic *)ebp->eb_un.eb_val;
#endif A_OUT
			break;
		case EB_LDSO_BASE:
			ld_base = (long)ebp->eb_un.eb_val;
			break;
		case EB_ARGV:
			_argv = (char **)ebp->eb_un.eb_ptr;
			_pr_name = *_argv;
			break;
		case EB_ENVP:
			_envp = (char **)ebp->eb_un.eb_ptr;
			break;
		case EB_AUXV:
			_auxv = (auxv_t *)ebp->eb_un.eb_ptr;
			break;
		case EB_DEVZERO:
			dz_fd = (int)ebp->eb_un.eb_val;
			break;
		case EB_PAGESIZE:
			_syspagsz = (int)ebp->eb_un.eb_val;
			break;
		}

	/*
	 * Search the aux. vector for the information passed by exec.
	 */
	for (auxv = _auxv; auxv->a_type != AT_NULL; auxv++) {
		switch (auxv->a_type) {
		case AT_EXECFD:
			/* this is the old exec that passes a file descriptor */
			fd = auxv->a_un.a_val;
			break;
		case AT_FLAGS:
			/* processor flags (MAU available, etc) */
			_flags = auxv->a_un.a_val;
			break;
		case AT_PAGESZ:
			/* system page size */
			_syspagsz = auxv->a_un.a_val;
			break;
		case AT_PHDR:
			/* address of the segment table */
			phdr = (Phdr *) auxv->a_un.a_ptr;
			break;
		case AT_PHENT:
			/* size of each segment header */
			phsize = auxv->a_un.a_val;
			break;
		case AT_PHNUM:
			/* number of program headers */
			phnum = auxv->a_un.a_val;
			break;
		case AT_BASE:
			/* interpreter base address */
			if (ld_base == 0)
				ld_base = auxv->a_un.a_val;
			interp_base = auxv->a_un.a_val;
			break;
		case AT_ENTRY:
			/* entry point for the executable */
			entry = auxv->a_un.a_val;
			break;
		case AT_SUN_UID:
			/* effective user id for the executable */
			euid = auxv->a_un.a_val;
			break;
		case AT_SUN_RUID:
			/* real user id for the executable */
			uid = auxv->a_un.a_val;
			break;
		case AT_SUN_GID:
			/* effective group id for the executable */
			egid = auxv->a_un.a_val;
			break;
		case AT_SUN_RGID:
			/* real group id for the executable */
			gid = auxv->a_un.a_val;
			break;
		case AT_SUN_PLATFORM:
			/* platform name */
			_platform = auxv->a_un.a_ptr;
			break;
		}
	}

	/*
	 * Get needed info from ld.so's dynamic structure.
	 */
	/* LINTED */
	dyn_ptr = (Dyn *)((char *)ld_dyn + ld_base);
	for (ld_dyn = dyn_ptr; ld_dyn->d_tag != DT_NULL; ld_dyn++) {
		switch (ld_dyn->d_tag) {
		case DT_RELA:
			reladdr = ld_dyn->d_un.d_ptr + ld_base;
			break;
		case DT_RELASZ:
			relsize = ld_dyn->d_un.d_val;
			break;
		case DT_STRTAB:
			strtab = ld_dyn->d_un.d_ptr + ld_base;
			break;
		case DT_SONAME:
			soname = ld_dyn->d_un.d_val;
			break;
#ifdef	PRF_RTLD
		case DT_HASH:
			hashaddr = (unsigned long *)(ld_dyn->d_un.d_ptr +
				ld_base);
			break;
#endif
		}
	}
	_rt_name = (char *)strtab + soname;


	/*
	 * Relocate all symbols in ld.so.
	 */
	{
		unsigned long	raddend, relend, roff;
		unsigned char	rtype;
		int		error = 0;

		relend = relsize + reladdr;
		while (reladdr < relend) {
			roff = (unsigned long)((Rel *)reladdr)->r_offset +
				ld_base;
			raddend = (long)(((Rel *)reladdr)->r_addend);
			rtype = ELF_R_TYPE(((Rel *)reladdr)->r_info);

			/*
			 * Normally there's only have one kind of relocation
			 * because ld.so was built with -Bsymbolic
			 * (R_SPARC_RELATIVE).  For these relocations we
			 * simply need to add the base address of ld.so.  We
			 * may also have plt entries if we intend to dlopen
			 * any libraries later for our own use.  These can
			 * simply be ignored as they will be processed
			 * during the binding process.
			 *
			 * Note that should an invalid relocation be
			 * discovered we try and hold the error condition.
			 * This (hopefully) will allow the error message
			 * string itself to be relocated.
			 */
			if (rtype == R_SPARC_RELATIVE) {
				/*
				 * Because we no longer store values
				 * in the got (besides GOT[0]) their
				 * is no reason for us to add in the
				 * location from memory.
				 */
				((long *)roff)[0] = ld_base + raddend;
			} else if (rtype != R_SPARC_JMP_SLOT)
				error++;

			reladdr += sizeof (Rel);
		}
		if (error)
			(void) write(2, Intmsg_relo, strlen(Intmsg_relo));
	}

	/*
	 * Now that ld.so has relocated itself, initialize any global variables.
	 * Initialize our own 'environ' so as to establish an address suitable
	 * for libc's hardware mul/div magic (libc/sparc/crt/hwmuldiv.o).
	 */
	environ = (int)_auxv - sizeof (char *);
	_init();

	if (_pr_name)
		pr_name = (const char *)_pr_name;
	flags = _flags;
	if (dz_fd != DZ_UNAVAIL)
		dz_init(dz_fd);
	envp = _envp;
	platform = _platform;

	/*
	 * If pagesize is unspecified find its value.
	 */
	if ((syspagsz = _syspagsz) == 0)
		syspagsz = sysconf(_SC_PAGESIZE);

	/*
	 * Initialize the fmap structure.
	 */
	if ((fmap = fm_init()) == NULL)
		exit(1);

	/*
	 * Determine whether we have a secure executable.
	 */
	security(uid, euid, gid, egid);

	/*
	 * Add the unused portion of the last data page to the free space list.
	 * The page size must be set before doing this.  Here, _end refers to
	 * the end of the run-time linkers bss.  Note that we do not use the
	 * unused data pages from any included .so's to supplement this free
	 * space as badly behaved .os's may corrupt this data space, and in so
	 * doing ruin our data.
	 */
	eaddr = S_DROUND((size_t)&_end);
	esize = eaddr % syspagsz;
	if (esize) {
		esize = syspagsz - esize;
		zero((caddr_t)eaddr, esize);
		addfree((void *)eaddr, esize);
	}


	/*
	 * We copy rtld's name here rather than just setting a pointer
	 * to it so that it will appear in the data segment and
	 * thus in any core file.
	 */
	if ((c = malloc(strlen(_rt_name) + 1)) == 0) {
		exit(1);
	}
	(void) strcpy(c, _rt_name);
	{
		rt_name = _rt_name;
		/*
		 * Get the filename of the rtld for use in any diagnostics (but
		 * save the full name in the link map for future comparisons)
		 */
		while (*c) {
			if (*c++ == '/')
				rt_name = c;
		}
	}


	/*
	 * Create a link map structure for ld.so.
	 */
	ftp = &elf_fct;
	if ((lmp = ftp->fct_new_lm(&lml_rtld, _rt_name, dyn_ptr, ld_base,
	    (unsigned long)&_etext, (unsigned long)(eaddr - ld_base),
	    0, 0, 0, 0)) == 0) {
		exit(1);
	}
	FLAGS(lmp) |= (FLG_RT_NODELETE | FLG_RT_ANALYZED);

	/*
	 * There is no need to analyze ld.so because we don't map in any of
	 * its dependencies.  However we may map these dependencies in later
	 * (as if ld.so had dlopened them), so initialize the plt and the
	 * permission information.
	 */
	if (PLTGOT(lmp))
		elf_plt_init((unsigned long *)(PLTGOT(lmp)), lmp);
	PERMIT(lmp) = perm_get();

	/*
	 * Look for environment strings (allows debugging to get switched on).
	 */
	if (!readenv((const char **)envp, aoutflag))
		exit(1);

	DBG_CALL(Dbg_file_ldso(rt_name, (unsigned long)ld_dyn, ld_base,
	    (unsigned long)envp, (unsigned long)_auxv));

#ifdef	PRF_RTLD
	/*
	 * Have we been requested to profile ourselves.
	 */
	if (profile_name && (strcmp(profile_name, ldso_name) == 0))
		profile_rtld = profile(ldso_name, lmp);
	PRF_MCOUNT(5, _setup);
#endif

	/*
	 * Using the executables phdr address determine the base address of
	 * the input file.  If this is an ELF file determine from the elf header
	 * if we're been called from a shared object or dynamic executable.  If
	 * the latter (which is the normal case) then the object will be marked
	 * as FLG_RT_ISMAIN in the objects link-map.  This flag indicates that
	 * any addresses within the object are to be used as is.  The addresses
	 * within shared objects must be added to the process's base address.
	 */
	_flags = 0;
	if (aoutflag == 0) {
		ehdr = (Ehdr *)((int)phdr - (int)phdr->p_offset);
		if (ehdr->e_type == ET_DYN)
			_flags = 1;
	}

	/*
	 * Map in the file, if exec has not already done so.  If it has,
	 * simply create a new link map structure for the executable.
	 */
	if (fd != -1) {
		/*
		 * Find out what type of object we have.
		 */
		fmap->fm_fd = fd;
		if ((ftp = are_u_this(pr_name)) == 0)
			exit(1);

		/*
		 * Map in object.
		 */
		if ((lmp = (ftp->fct_map_so)(&lml_main, pr_name,
		    pr_name)) == 0)
			exit(1);

	} else {
		/*
		 * Set up function ptr and arguments according to the type
		 * of file class the executable is. (Currently only supported
		 * types are ELF and a.out format.)  Then create a link map
		 * for the executable.
		 */
		if (aoutflag) {
#ifdef A_OUT
			ftp = &aout_fct;
			if (!(lmp = (ftp->fct_new_lm)(&lml_main, 0, aoutdyn,
			    0, 0))) {
				exit(1);
			}
			/*
			 * Set the memory size.  Note, we only know the end of
			 * text, and although we could find the _end by looking
			 * up the symbol, this may not be present.  We should
			 * set ADDR to MAIN_BASE, but presently all the a.out
			 * relocation code assumes ADDR is 0 for the dynamic
			 * executable. (these data items are only used for
			 * dladdr(3x), and there aren't many a.out dladdr(3x)
			 * users to warrent spending much time on this :-).
			 */
			MSIZE(lmp) = MAIN_BASE + ETEXT(lmp);
#else
			eprintf(ERR_FATAL, Errmsg_unft, pr_name);
			exit(1);
#endif A_OUT
		} else {
			Phdr *	pptr;
			Phdr *	firstptr = 0;
			Phdr *	lastptr;
			Dyn *	dyn;
			Off	i_offset;
			Addr	base = 0;
			char *	name = 0;

			if (_flags) {
				base = (Addr)ehdr;
				name = (char *)pr_name;
			}

			/*
			 * Extract the needed information from the segment
			 * headers.
			 */
			for (i = 0, pptr = phdr; i < phnum; i++) {
				if (pptr->p_type == PT_INTERP) {
					interp = &_interp;
					i_offset = pptr->p_offset;
					interp->i_faddr =
					    (caddr_t)interp_base;
				}
				if (pptr->p_type == PT_LOAD) {
					if (!firstptr)
						firstptr = pptr;
					lastptr = pptr;
					if ((pptr->p_offset <= i_offset) &&
					    (i_offset <= (pptr->p_memsz +
					    pptr->p_offset))) {
						interp->i_name = (char *)
						    pptr->p_vaddr + i_offset -
						    pptr->p_offset + base;
					}
					if (!(pptr->p_flags & PF_W))
						etext = pptr->p_vaddr +
							pptr->p_memsz + base;
				} else if (pptr->p_type == PT_DYNAMIC)
					dyn = (Dyn *)(pptr->p_vaddr + base);
				pptr = (Phdr *)((unsigned long)pptr + phsize);
			}
			ftp = &elf_fct;
			if (!(lmp = (ftp->fct_new_lm)(&lml_main, name,
			    dyn, (Addr)ehdr, etext,
			    (lastptr->p_vaddr + lastptr->p_memsz) -
			    S_ALIGN(firstptr->p_vaddr, syspagsz), entry,
			    phdr, phnum, phsize))) {
				exit(1);
			}
		}
	}
	/*
	 * Having mapped the executable in and created its link map, initialize
	 * the name and flags entries as necessary.
	 */
	if (_flags == 0) {
		NAME(lmp) = (char *)pr_name;
		FLAGS(lmp) |= (FLG_RT_ISMAIN | FLG_RT_NODELETE);
	} else
		FLAGS(lmp) |= FLG_RT_NODELETE;

	/*
	 * Initialize debugger information structure some parts of
	 * this structure were initialized statically.
	 */
	r_debug.r_map = (Link_map *)lmp;
	r_debug.r_ldbase = ld_base;

	/*
	 * Continue with generic startup processing.
	 */
	if (setup(lmp) == 0) {
		exit(1);
	}

	DBG_CALL(Dbg_util_call_main(pr_name));
	return (LM_ENTRY_PT(lmp)());
}
