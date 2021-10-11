/*
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	  All Rights Reserved
 */
#pragma ident	"@(#)a.out.c	1.26	95/07/31 SMI"

/*
 * Object file dependent support for a.out format objects.
 */
#include	<sys/mman.h>
#include	<unistd.h>
#include	<string.h>
#include	<limits.h>
#include	<stdio.h>
#include	<dlfcn.h>
#include	<errno.h>
#include	"_a.out.h"
#include	"cache.h"
#include	"paths.h"
#include	"_rtld.h"
#include	"profile.h"
#include	"debug.h"

/*
 * Directory search rules for a.out format objects.
 */
static int		aout_search_rules[] = {
	ENVDIRS, RUNDIRS, DEFAULT, 0
};

static Pnode		aout_dflt_dirs[] = {
	{ LIBDIR4X,	LIBDIR4XLEN,	&aout_dflt_dirs[1] },
	{ LIBDIR,	LIBDIRLEN,	&aout_dflt_dirs[2] },
	{ LIBDIRLCL,	LIBDIRLCLLEN,	(Pnode *)NULL }
};

static Pnode		aout_secure_dirs[] = {
	{ LIBDIR4X,	LIBDIR4XLEN,	&aout_secure_dirs[1] },
	{ LIBDIR,	LIBDIRLEN,	&aout_secure_dirs[2] },
	{ LIBDIRUCB,	LIBDIRUCBLEN,	&aout_secure_dirs[3] },
	{ LIBDIRLCL,	LIBDIRLCLLEN,	(Pnode *)NULL }
};

/*
 * Defines for local functions.
 */
static int		aout_are_u();
static unsigned long	aout_entry_pt();
static Rt_map *		aout_map_so();
static Rt_map *		aout_new_lm();
static int		aout_unmap_so();
static int		aout_needed();
extern Sym *		aout_lookup_sym();
static Sym *		aout_find_sym();
static char *		aout_get_so();
static const char *	aout_fix_name();
static void		aout_dladdr();
static Sym *		aout_dlsym();

/*
 * Functions and data accessed through indirect pointers.
 */
Fct aout_fct = {
	aout_are_u,
	aout_entry_pt,
	aout_map_so,
	aout_new_lm,
	aout_unmap_so,
	aout_needed,
	aout_lookup_sym,
	aout_find_sym,
	aout_reloc,
	aout_search_rules,
	aout_dflt_dirs,
	aout_secure_dirs,
	aout_fix_name,
	aout_get_so,
	aout_dladdr,
	aout_dlsym
};

static const char * Formsg_lbso = "lib%s.so.%d.%d";

/*
 * In 4.x, a needed file or a dlopened file that was a simple file name
 * implied that the file be found in the present working directory.  To
 * simulate this lookup within the elf rules it is necessary to add a
 * proceeding `./' to the filename.
 */
static const char *
aout_fix_name(const char * name)
{
	char *	_name;		/* temporary name pointer */

	/*
	 * If this is the main routine, just return.
	 */
	if (!name)
		return (0);

	/*
	 * Check for slash in name, if none, prepend "./",
	 * otherwise just return name given.
	 */
	for (_name = (char *)name; *_name; _name++) {
		if (*_name == '/')
			return (name);
	}

	if ((_name = (char *)malloc(strlen(name) + 3)) == 0)
		return (0);
	(void) sprintf(_name, "./%s", name);
	DBG_CALL(Dbg_file_fixname(name, _name));
	return ((const char *)_name);
}

/*
 * Determine if we have been given an A_OUT file.  Returns 1 if true.
 */
static int
aout_are_u()
{
	struct exec * exec;

	PRF_MCOUNT(6, aout_are_u);
	/* LINTED */
	exec = (struct exec *)fmap->fm_maddr;
	if (fmap->fm_fsize < sizeof (exec) || (exec->a_machtype != M_SPARC) ||
	    (N_BADMAG(*exec))) {
		return (0);
	}
	return (1);
}

/*
 * Return the entry point the A_OUT executable. This is always zero.
 */
static unsigned long
aout_entry_pt()
{
	PRF_MCOUNT(7, aout_entry_pt);
	return (0);
}

/*
 * Unmap a given A_OUT shared object from the address space.
 */
static int
aout_unmap_so(Rt_map * lm)
{
	caddr_t addr;

	PRF_MCOUNT(8, aout_unmap_so);
	addr = (caddr_t)ADDR(lm);
	/* LINTED */
	(void) munmap(addr, max(SIZE(*(struct exec *)addr),
	    N_SYMOFF((*(struct exec *)addr)) + sizeof (struct nlist)));
	return (1);
}

/*
 * Search through the dynamic section for DT_NEEDED entries and perform one
 * of two functions.  If only the first argument is specified then load the
 * defined shared object, otherwise add the link map representing the
 * defined link map the the dlopen list.
 */
static int
aout_needed(Lm_list * lml, Rt_map * lmp, Permit * permit, Dl_obj * dlp)
{
	Rt_map	*	nlmp;
	void *		need;
	const char *	name;

	PRF_MCOUNT(9, aout_needed);
	for (need = &TEXTBASE(lmp)[AOUTDYN(lmp)->v2->ld_need];
	    need != &TEXTBASE(lmp)[0];
	    need = &TEXTBASE(lmp)[((Lnk_obj *)(need))->lo_next]) {
		name = &TEXTBASE(lmp)[((Lnk_obj *)(need))->lo_name];

		if (((Lnk_obj *)(need))->lo_library) {
			/*
			 * If lo_library field is not NULL then this needed
			 * library was linked in using the "-l" option.
			 * Thus we need to rebuild the library name before
			 * trying to load it.
			 */
			Pnode *	dir, *dirlist = (Pnode *)0;
			char *	file;

			/*
			 * Allocate name length plus 20 for full library name.
			 * lib.so.. = 7 + (2 * short) + NULL = 7 + 12 + 1 = 20
			 */
			if ((file = (char *)malloc(strlen(name) + 20)) == 0)
				return (0);
			(void) sprintf(file, Formsg_lbso, name,
				((Lnk_obj *)(need))->lo_major,
				((Lnk_obj *)(need))->lo_minor);

			DBG_CALL(Dbg_libs_find(file));

			/*
			 * We need to determine what filename will match the
			 * the filename specified (ie, a libc.so.1.2 may match
			 * to a libc.so.1.3).  It's the real pathname that is
			 * recorded in the link maps.  If we are presently
			 * being traced, skip this pathname generation so
			 * that we fall through into _load_so() to print the
			 * appropriate diagnostics.  I don't like this at all.
			 */
			if (tracing)
				name = (const char *)file;
			else {
				const char *	path = (char *)0;
				for (dir = get_next_dir(&dirlist, lmp); dir;
				    dir = get_next_dir(&dirlist, lmp)) {
					if (dir->p_name == 0)
						continue;

					if (path = so_gen_path(dir, file, lmp))
						break;
				}
				if (!path) {
					eprintf(ERR_FATAL, Errmsg_cofl, file,
					    ENOENT);
					return (0);
				}
				name = path;
			}
		} else {
			/*
			 * If the library is specified as a pathname, see if
			 * it must be fixed to specify the current working
			 * directory (ie. libc.so.1.2 -> ./libc.so.1.2).
			 */
			name = aout_fix_name(name);
		}

		nlmp = is_so_loaded(lml, name);

		/*
		 * If the named link map does not already exist, load it.
		 */
		if (!nlmp) {
			DBG_CALL(Dbg_file_needed(name, NAME(lmp)));
			if ((nlmp = load_so(lml, name, lmp)) == 0)
				if (tracing)
					continue;
				else
					return (0);
		}
		PERMIT(nlmp) = perm_set(PERMIT(nlmp), permit);
		FLAGS(nlmp) |= (FLAGS(lmp) & FLG_RT_PROMISC);
		DBG_CALL(Dbg_file_ref(NAME(nlmp), COUNT(nlmp), PERMIT(nlmp),
		    (FLAGS(nlmp) & FLG_RT_PROMISC)));

		/*
		 * If this request comes from a dlopen, add the new link map to
		 * the dlp dependent link-map list.
		 */
		if (dlp)
			if (!dlp_listadd(nlmp, dlp))
				return (0);
	}
	return (1);
}

static Sym *
aout_symconvert(struct nlist * sp)
{
	static Sym	sym;

	PRF_MCOUNT(11, aout_symconvert);

	sym.st_shndx = 0;
	sym.st_value = sp->n_value;
	switch (sp->n_type) {
		case N_EXT + N_ABS:
			sym.st_shndx = SHN_ABS;
			break;
		case N_COMM:
			sym.st_shndx = SHN_COMMON;
			break;
		case N_EXT + N_UNDF:
			sym.st_shndx = SHN_UNDEF;
			break;
		default:
			break;
	}
	return (&sym);
}

/*
 * Process a.out format commons.
 */
static struct nlist *
aout_find_com(struct nlist * sp, const char * name)
{
	static struct rtc_symb *	rtcp = 0;
	struct rtc_symb *		rs, * trs;
	const char *			sl;
	char *				cp;

	PRF_MCOUNT(12, aout_find_com);
	/*
	 * See if common is already allocated.
	 */
	trs = rtcp;
	while (trs) {
		sl = name;
		cp = trs->rtc_sp->n_un.n_name;
		while (*sl == *cp++)
			if (*sl++ == '\0')
				return (trs->rtc_sp);
		trs = trs->rtc_next;
	}

	/*
	 * If we got here, common is not already allocated so allocate it.
	 */
	rs = (struct rtc_symb *)malloc(sizeof (struct rtc_symb));
	rs->rtc_sp = (struct nlist *)malloc(sizeof (struct nlist));
	trs = rtcp;
	rtcp = rs;
	rs->rtc_next = trs;
	*(rs->rtc_sp) = *sp;
	rs->rtc_sp->n_un.n_name = (char *)malloc(strlen(name) + 1);
	(void) strcpy(rs->rtc_sp->n_un.n_name, name);
	rs->rtc_sp->n_type = N_COMM;
	rs->rtc_sp->n_value = (long)calloc(rs->rtc_sp->n_value, 1);
	return (rs->rtc_sp);
}

/*
 * Find a.out format symbol in the specified link map.  Unlike the sister
 * elf routine we re-calculate the symbols hash value for each link map
 * we're looking at.
 */
static struct nlist *
aout_findsb(const char * aname, Rt_map * lmp, int flag)
{
	const char *	name = aname;
	char *		cp;
	struct fshash *	p;
	int		i;
	struct nlist *	sp;
	unsigned long	hval = 0;

	PRF_MCOUNT(13, aout_findsb);

#define	HASHMASK	0x7fffffff
#define	RTHS		126

	/*
	 * The name passed to us is in ELF format, thus it is necessary to
	 * map this back to the A_OUT format to compute the hash value (see
	 * mapping rules in aout_lookup_sym()).  Basically the symbols are
	 * mapped according to whether a leading `.' exists.
	 *
	 *	elf symbol		a.out symbol
	 * i.	   .bar		->	   .bar		(LKUP_LDOT)
	 * ii.	   .nuts	->	    nuts
	 * iii.	    foo		->	   _foo
	 */
	if (*name == '.') {
		if (!(flag & LKUP_LDOT))
			name++;
	} else
		hval = '_';

	while (*name)
		hval = (hval << 1) + *name++;
	hval = hval & HASHMASK;

	i = hval % (AOUTDYN(lmp)->v2->ld_buckets == 0 ? RTHS :
		AOUTDYN(lmp)->v2->ld_buckets);
	p = LM2LP(lmp)->lp_hash + i;

	if (p->fssymbno != -1)
		do {
			sp = &LM2LP(lmp)->lp_symtab[p->fssymbno];
			cp = &LM2LP(lmp)->lp_symstr[sp->n_un.n_strx];
			name = aname;
			if (*name == '.') {
				if (!(flag & LKUP_LDOT))
					name++;
			} else {
				cp++;
			}
			while (*name == *cp++) {
				if (*name++ == '\0')
					return (sp);	/* found */
			}
			if (p->next == 0)
				return (0);		/* not found */
			else
				continue;
		} while ((p = &LM2LP(lmp)->lp_hash[p->next]) != 0);
	return (0);
}

/*
 * The symbol name we have been asked to look up is in A_OUT format, this
 * symbol is mapped to the appropriate ELF format which is the standard by
 * which symbols are passed around ld.so.1.  The symbols are mapped
 * according to whether a leading `_' or `.' exists.
 *
 *	a.out symbol		elf symbol
 * i.	   _foo		->	    foo
 * ii.	   .bar		->	   .bar		(LKUP_LDOT)
 * iii.	    nuts	->	   .nuts
 */
Sym *
aout_lookup_sym(const char * aname, Permit * permit, Rt_map * clmp,
	Rt_map * ilmp, Rt_map ** dlmp, int flag)
{
	char	name[PATH_MAX];

	PRF_MCOUNT(14, aout_lookup_sym);
	DBG_CALL(Dbg_syms_lookup_aout(aname));

	if (*aname == '_')
		++aname;
	else if (*aname == '.')
		flag |= LKUP_LDOT;
	else {
		name[0] = '.';
		(void) strcpy(&name[1], aname);
		aname = name;
	}

	/*
	 * Call the generic lookup routine to cycle through the specified
	 * link maps.
	 */
	return (lookup_sym(aname, permit, clmp, ilmp, dlmp, flag));
}

/*
 * Symbol lookup for an a.out format module.
 */
/* ARGSUSED4 */
static Sym *
aout_find_sym(const char * aname, Rt_map * lmp, Rt_map ** dlmp, int flag,
	unsigned long hash)
{
	struct nlist *	sp;

	PRF_MCOUNT(15, aout_find_sym);
	DBG_CALL(Dbg_syms_lookup(aname, NAME(lmp), (const char *)"AOUT"));

	if (sp = aout_findsb(aname, lmp, flag)) {
		if (sp->n_value != 0) {
			/*
			 * is it a common?
			 */
			if (sp->n_type == (N_EXT + N_UNDF)) {
				sp = aout_find_com(sp, aname);
			}
			*dlmp = lmp;
			return (aout_symconvert(sp));
		}
	}
	return ((Sym *)0);
}

/*
 * Map in an a.out format object.
 * Takes an open file descriptor for the object to map and
 * its pathname; returns a pointer to a Rt_map structure
 * for this object, or 0 on error.
 */
/* ARGSUSED2 */
static Rt_map *
aout_map_so(Lm_list * lml, const char * pname, const char * profile)
{
	struct exec *	exec;		/* working area for object headers */
	caddr_t		addr;		/* mmap result temporary */
	struct link_dynamic *ld;	/* dynamic pointer of object mapped */
	size_t		size;		/* size of object */
	const char *	name;		/* actual name stored for pathname */
	Rt_map *	lmp;		/* link map created */
	int		dz_fd;		/* File descriptor for anon memory */

	PRF_MCOUNT(16, aout_map_so);
	/*
	 * Is object the executable?
	 */
	if (pname == (char *)0)
		name = pr_name;
	else
		name = pname;

	/*
	 * Map text and allocate enough address space to fit the whole
	 * library.  Note that we map enough to catch the first symbol
	 * in the symbol table and thereby avoid an "lseek" & "read"
	 * pair to pick it up.
	 */
	/* LINTED */
	exec = (struct exec *)fmap->fm_maddr;
	size = max(SIZE(*exec), N_SYMOFF(*exec) + sizeof (struct nlist));
	if ((addr = (caddr_t)mmap(0, size, PROT_READ | PROT_EXEC, MAP_PRIVATE,
	    fmap->fm_fd, 0)) == (caddr_t)-1) {
		eprintf(ERR_FATAL, Errmsg_cmfl, name, errno);
		return (0);
	}

	/*
	 * Grab the first symbol entry while we've got it mapped aligned
	 * to file addresses.  We assume that this symbol describes the
	 * object's link_dynamic.
	 */
	ld = (struct link_dynamic *)&addr[
		/* LINTED */
		((struct nlist *)&addr[N_SYMOFF(*exec)])->n_value];

	/*
	 * Map the initialized data portion of the file to the correct
	 * point in the range of allocated addresses.  This will leave
	 * some portion of the data segment "doubly mapped" on machines
	 * where the text/data relocation alignment is not on a page
	 * boundaries.  However, leaving the file mapped has the double
	 * advantage of both saving the munmap system call and of leaving
	 * us a contiguous chunk of address space devoted to the object --
	 * in case we need to unmap it all later.
	 */
	if ((caddr_t)mmap((caddr_t)(addr + M_SROUND(exec->a_text)),
	    (int)exec->a_data,
	    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE,
	    fmap->fm_fd, (off_t)exec->a_text) == (caddr_t)-1) {
		eprintf(ERR_FATAL, Errmsg_cmsg, name, errno);
		return (0);
	}

	/*
	 * Allocate pages for the object's bss, if necessary.
	 */
	if (exec->a_bss != 0) {
		if ((dz_fd = dz_open()) == DZ_UNAVAIL)
			goto error;
		if ((caddr_t)mmap(addr + M_SROUND(exec->a_text) + exec->a_data,
		    (int)exec->a_bss,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE,
		    dz_fd, 0) == (caddr_t)-1) {
			eprintf(ERR_FATAL, Errmsg_cmdz, errno);
			goto error;
		}
	}

	/*
	 * Create link map structure for newly mapped shared object.
	 */
	ld->v2 = (struct link_dynamic_2 *)((int)ld->v2 + (int)addr);
	if (!(lmp = aout_new_lm(lml, pname, ld, addr, size)))
		goto error;

	return (lmp);

	/*
	 * Error returns: close off file and free address space.
	 */
error:
	(void) munmap((caddr_t)addr, size);
	return (0);
}

/*
 * Create a new Rt_map structure for an a.out format object and
 * initializes all values.
 */
static Rt_map *
aout_new_lm(Lm_list * lml, const char * pname, struct link_dynamic * ld,
	caddr_t addr, size_t size)
{
	Rt_map *	lmp;
	caddr_t 	offset;

	PRF_MCOUNT(17, aout_new_lm);
	DBG_CALL(Dbg_file_aout((pname ? pname : pr_name), (unsigned long)ld,
	    (unsigned long)addr, (unsigned long)size));

	/*
	 * Allocate space.
	 */
	if ((lmp = (Rt_map *)calloc(sizeof (Rt_map), 1)) == 0)
		return (0);
	if ((AOUTPRV(lmp) = (Rt_aoutp *)calloc(sizeof (Rt_aoutp), 1)) == 0)
		return (0);
	if ((((Rt_aoutp *)AOUTPRV(lmp))->lm_lpd =
	    (struct ld_private *)calloc(sizeof (struct ld_private), 1)) == 0)
		return (0);

	/*
	 * All fields not filled in were set to 0 by calloc.
	 */
	NAME(lmp) = (char *)pname;
	ADDR(lmp) = (unsigned long)addr;
	MSIZE(lmp) = (unsigned long)size;
	SYMINTP(lmp) = aout_find_sym;
	FCT(lmp) = &aout_fct;
	LIST(lmp) = lml;

	/*
	 * Specific settings for a.out format.
	 */
	if (pname)
		offset = addr;
	else
		offset = (caddr_t)MAIN_BASE;

	ETEXT(lmp) = (unsigned long)&offset[ld->v2->ld_text];

	AOUTDYN(lmp) = ld;
	if ((RPATH(lmp) = (char *)&offset[ld->v2->ld_rules]) == offset)
		RPATH(lmp) = 0;
	LM2LP(lmp)->lp_symbol_base = addr;
	/* LINTED */
	LM2LP(lmp)->lp_plt = (struct jbind *)(&addr[JMPOFF(ld)]);
	LM2LP(lmp)->lp_rp =
	/* LINTED */
	    (struct relocation_info *)(&offset[RELOCOFF(ld)]);
	/* LINTED */
	LM2LP(lmp)->lp_hash = (struct fshash *)(&offset[HASHOFF(ld)]);
	/* LINTED */
	LM2LP(lmp)->lp_symtab = (struct nlist *)(&offset[SYMOFF(ld)]);
	LM2LP(lmp)->lp_symstr = &offset[STROFF(ld)];
	LM2LP(lmp)->lp_textbase = offset;
	LM2LP(lmp)->lp_refcnt++;
	LM2LP(lmp)->lp_dlp = NULL;

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
aout_set_prot(Rt_map * lm, int permission)
{
	int		prot;		/* protection setting */
	caddr_t		et;		/* cached _etext of object */
	size_t		size;		/* size of text segment */

	PRF_MCOUNT(18, aout_set_prot);
	DBG_CALL(Dbg_file_prot(NAME(lm), permission));

	et = (caddr_t)ETEXT(lm);
	size = M_PROUND((unsigned long)(et - TEXTBASE(lm)));
	prot = PROT_READ | PROT_EXEC | permission;
	if (mprotect((caddr_t)TEXTBASE(lm), size, prot) == -1) {
		eprintf(ERR_FATAL, Errmsg_csps, NAME(lm), errno);
		return (0);
	}
	return (1);
}

/*
 * Build full pathname of shared object from the given directory name and
 * filename.
 */
static char *
aout_get_so(char * dir, char * file)
{
	struct db *	dbp;
	char *		path = NULL;

	PRF_MCOUNT(19, aout_get_so);
	if (dbp = lo_cache(dir)) {
		path = ask_db(dbp, file);
	}
	return (path);
}

/*
 * Determine the symbol location of an address within a link-map.  Look for
 * the nearest symbol (whoes value is less than or equal to the required
 * address).  This is the object specific part of dladdr().
 */
static void
aout_dladdr(unsigned long addr, Rt_map * lmp, Dl_info * dlip)
{
	unsigned long	ndx, cnt, base, value, _value;
	struct nlist *	sym;
	const char *	_name;

	PRF_MCOUNT(85, aout_dladdr);

	cnt = ((int)LM2LP(lmp)->lp_symstr - (int)LM2LP(lmp)->lp_symtab) /
		sizeof (struct nlist);
	sym = LM2LP(lmp)->lp_symtab;

	if (FLAGS(lmp) & FLG_RT_ISMAIN)
		base = 0;
	else
		base = ADDR(lmp);

	for (_value = 0, ndx = 0; ndx < cnt; ndx++, sym++) {
		if (sym->n_type == (N_EXT + N_UNDF))
			continue;

		value = sym->n_value + base;
		if (value > addr)
			continue;
		if (value < _value)
			continue;

		_value = value;
		_name = &LM2LP(lmp)->lp_symstr[sym->n_un.n_strx];

		if (value == addr)
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
aout_dlsym(Dl_obj * dlp, Rt_map * clmp, const char * name, Rt_map ** _lmp)
{
	Rt_map *	tlmp;
	Listnode *	lnp;
	Sym *		sym;
	char		_name[PATH_MAX];

	for (LIST_TRAVERSE(&dlp->dl_lmps, lnp, tlmp)) {
		if (sym = LM_LOOKUP_SYM(clmp)(name, 0, 0, tlmp, _lmp,
		    (LKUP_DEFT | LKUP_FIRST)))
			return (sym);
	}

	/*
	 * Symbol not found as supplied.  However, most of our symbols will be
	 * in the "C" name space, where the implementation prepends a "_" to
	 * the symbol as it emits it.  Therefore, attempt to find the symbol
	 * with the "_" prepend.
	 */
	_name[0] = '_';
	(void) strcpy(&_name[1], name);
	for (LIST_TRAVERSE(&dlp->dl_lmps, lnp, tlmp)) {
		if (sym = LM_LOOKUP_SYM(clmp)(_name, 0, 0, tlmp, _lmp,
		    (LKUP_DEFT | LKUP_FIRST)))
			return (sym);
	}
	return (0);
}
