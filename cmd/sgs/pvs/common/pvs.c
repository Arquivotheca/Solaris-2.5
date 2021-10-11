/*
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)pvs.c	1.12	94/11/03 SMI"

/*
 * Analyze the versioning information within a file.
 *
 *   -d		dump version definitions.
 *
 *   -n		normalize any version definitions.
 *
 *   -o		dump output in one-line fashion	(more suitable for grep'ing
 *		and diff'ing).
 *
 *   -r		dump the version requirements on library dependencies
 *
 *   -s		display the symbols associated with each version definition.
 *
 *   -v		verbose output.  With the -r and -d options any WEAK attribute
 *		is displayed.  With the -d option, any version inheritance,
 *		and the base version are displayed.  With the -s option the
 *		version symbol is displayed.
 *
 *   -N name	only print the specifed `name'.
 */
#include	<fcntl.h>
#include	<stdio.h>
#include	<libelf.h>
#include	<link.h>
#include	<stdlib.h>
#include	<stdarg.h>
#include	<string.h>
#include	<unistd.h>
#include	<errno.h>
#include	"sgs.h"
#include	"machdep.h"

#define		FLG_VER_AVAIL	0x10

static const char *	usage_msg = "usage: %s [-dnorsv] [-N name] file(s)\n";

typedef struct cache {
	Shdr *		c_shdr;
	Elf_Data *	c_data;
} Cache;

struct ver_desc {
	const char *	vd_name;
	unsigned long	vd_hash;
	Half		vd_ndx;
	Half		vd_flags;
	List		vd_deps;
};

extern unsigned long	elfhash();

static const char
	* Format_ofil = "%s -",
	* Format_tnco =	"\t%s:\n",
	* Format_tnse =	"\t%s;\n",
	* Format_bgnl = "\t%s (%s",
	* Format_next = ", %s",
	* Format_weak = " [WEAK]",
	* Format_endl = ");\n",

	* Errmsg_vrhe = "warning: file %s: version revision %d is "
			"higher than expected %d\n";

#define	DEF_DEFINED	1
#define	USR_DEFINED	2

/*
 * Print the files version needed sections.
 */
static int
vers_need(Cache * cache, Cache * need, const char * file, int oflag, int vflag,
	const char * name)
{
	unsigned int	num, _num;
	char *		strs;
	Verneed *	vnd = need->c_data->d_buf;
	Shdr *		shdr = need->c_shdr;
	int		error = 0;

	/*
	 * Verify the version revision.  We only check the first version
	 * structure as it is assumed all other version structures in this
	 * data section will be of the same revision.
	 */
	if (vnd->vn_version > VER_DEF_CURRENT)
		(void) fprintf(stderr, Errmsg_vrhe, file, vnd->vn_version,
		    VER_DEF_CURRENT);

	/*
	 * Get the data buffer for the associated string table.
	 */
	strs = (char *)cache[shdr->sh_link].c_data->d_buf;
	num = shdr->sh_info;

	for (_num = 1; _num <= num; _num++,
	    vnd = (Verneed *)((Word)vnd + vnd->vn_next)) {
		Vernaux *	vnap = (Vernaux *)((Word)vnd + vnd->vn_aux);
		Half		cnt = vnd->vn_cnt;
		const char *	_name, * dep;

		/*
		 * Obtain the version name and determine if we need to process
		 * it further.
		 */
		_name = (char *)(strs + vnd->vn_file);
		if (name && (strcmp(name, _name) == 0))
			continue;

		error = 1;

		/*
		 * If one-line ouput is called for display the filename being
		 * processed.
		 */
		if (oflag)
			(void) printf(Format_ofil, file);

		/*
		 * Determine the version name required from this file.
		 */
		if (cnt--)
			dep = (char *)(strs + vnap->vna_name);
		else
			dep = "";

		(void) printf(Format_bgnl, _name, dep);
		if (vflag && (vnap->vna_flags == VER_FLG_WEAK))
			(void) printf(Format_weak);

		/*
		 * Extract any other version dependencies for this file
		 */
		for (vnap = (Vernaux *)((Word)vnap + vnap->vna_next); cnt;
		    cnt--, vnap = (Vernaux *)((Word)vnap + vnap->vna_next)) {
			dep = (char *)(strs + vnap->vna_name);
			(void) printf(Format_next, dep);
			if (vflag && (vnap->vna_flags == VER_FLG_WEAK))
				(void) printf(Format_weak);
		}
		(void) printf(Format_endl);
	}
	return (error);
}

/*
 * Append an item to the specified list, and return a pointer to the list
 * node created.
 */
static Listnode *
list_append(List * lst, const void * item)
{
	Listnode *	_lnp;

	if ((_lnp = (Listnode *)malloc(sizeof (Listnode))) == 0) {
		(void) fprintf(stderr, "malloc: %s\n", strerror(errno));
		exit(1);
	}

	_lnp->data = (void *)item;
	_lnp->next = NULL;

	if (lst->head == NULL)
		lst->tail = lst->head = _lnp;
	else {
		lst->tail->next = _lnp;
		lst->tail = lst->tail->next;
	}
	return (_lnp);
}

static Ver_desc *
vers_find(const char * name, unsigned long hash, List * lst)
{
	Listnode *	lnp;
	Ver_desc *	vdp;

	for (LIST_TRAVERSE(lst, lnp, vdp)) {
		if (vdp->vd_hash != hash)
			continue;
		if (strcmp(vdp->vd_name, name) == 0)
			return (vdp);
	}
	return (0);
}

static Ver_desc *
vers_desc(const char * name, unsigned long hash, List * lst)
{
	Ver_desc *	vdp;

	if ((vdp = vers_find(name, hash, lst)) == 0) {
		if ((vdp = (Ver_desc *)calloc(sizeof (Ver_desc), 1)) == 0) {
			(void) fprintf(stderr, "calloc: %s\n", strerror(errno));
			exit(1);
		}

		vdp->vd_name = name;
		vdp->vd_hash = hash;

		if (list_append(lst, vdp) == 0)
			return (0);
	}
	return (vdp);
}

static Ver_desc *
vers_depend(const char * name, unsigned long hash, Ver_desc * vdp, List * lst)
{
	Ver_desc *	_vdp;

	if ((_vdp = vers_desc(name, hash, lst)) == 0)
		return (0);

	if (list_append(&vdp->vd_deps, _vdp) == 0)
		return (0);

	return (vdp);
}

static void
vers_syms(Versym * vsp, Sym * syms, int symn, char * strs, Ver_desc * vdp,
	const char * file, int oflag, int vflag)
{
	int	_symn;

	for (_symn = 0; _symn < symn; _symn++, syms++) {
		int	size =	0;
		char *	name;

		if (vsp[_symn] != vdp->vd_ndx)
			continue;

		/*
		 * For data symbols determine the size.
		 */
		if (ELF_ST_TYPE(syms->st_info) == STT_OBJECT)
			size = syms->st_size;

		name = (char *)(strs + syms->st_name);

		/*
		 * Only output the version symbol when the verbose flag is used.
		 */
		if (!vflag && (syms->st_shndx == SHN_ABS)) {
			if (strcmp(name, vdp->vd_name) == 0)
				continue;
		}

		if (oflag) {
			(void) printf(Format_ofil, file);
			(void) printf("\t%s: ", vdp->vd_name);
			if (size)
				(void) printf("%s (%d);\n", name, size);
			else
				(void) printf("%s;\n", name);
		} else {
			if (size)
				(void) printf("\t\t%s (%d);\n", name, size);
			else
				(void) printf("\t\t%s;\n", name);
		}
	}
}

static void
vers_derefer(Ver_desc * vdp, int weak)
{
	Listnode *	_lnp;
	Ver_desc *	_vdp;

	/*
	 * If the head of the list was a weak then we only clear out
	 * weak dependencies, but if the head of the list was 'strong'
	 * we clear the REFER bit on all dependencies.
	 */
	if ((weak && (vdp->vd_flags & VER_FLG_WEAK)) || (!weak))
		vdp->vd_flags &= ~FLG_VER_AVAIL;

	for (LIST_TRAVERSE(&vdp->vd_deps, _lnp, _vdp))
		vers_derefer(_vdp, weak);
}


static void
recurse_syms(Versym * vsp, Sym * syms, int symn, char * strs, Ver_desc * vdp,
	const char * file, int oflag, int vflag)
{
	Listnode *	_lnp;
	Ver_desc *	_vdp;

	for (LIST_TRAVERSE(&vdp->vd_deps, _lnp, _vdp)) {
		if (!oflag)
			(void) printf(Format_tnco, _vdp->vd_name);
		vers_syms(vsp, syms, symn, strs, _vdp, file, oflag, vflag);
		if (_vdp->vd_deps.head)
			recurse_syms(vsp, syms, symn, strs, _vdp, file, oflag,
			    vflag);
	}
}


/*
 * Print the files version definition sections.
 */
static int
vers_def(Cache * cache, Cache * def, Cache * sym, const char * file, int nflag,
	int oflag, int vflag, const char * name)
{
	unsigned int	num, _num;
	char *		strs;
	Versym *	vsp;
	Verdef *	vdf = def->c_data->d_buf;
	Shdr *		shdr = def->c_shdr;
	Sym *		syms;
	int		symn;
	Ver_desc *	vdp, * bvdp = 0;
	Listnode *	lnp;
	List		verdefs = {0, 0};
	int		error = 0;

	/*
	 * Verify the version revision.  We only check the first version
	 * structure as it is assumed all other version structures in this
	 * data section will be of the same revision.
	 */
	if (vdf->vd_version > VER_DEF_CURRENT) {
		(void) fprintf(stderr, Errmsg_vrhe, file, vdf->vd_version,
		    VER_DEF_CURRENT);
	}

	/*
	 * Get the data buffer for the associated string table.
	 */
	strs = (char *)cache[shdr->sh_link].c_data->d_buf;
	num = shdr->sh_info;

	/*
	 * Process the version definitions placing each on a version dependency
	 * list.
	 */
	for (_num = 1; _num <= num; _num++,
	    vdf = (Verdef *)((Word)vdf + vdf->vd_next)) {
		Half		cnt = vdf->vd_cnt;
		Half 		ndx = vdf->vd_ndx;
		Verdaux *	vdap = (Verdaux *)((Word)vdf + vdf->vd_aux);
		const char *	_name;

		/*
		 * Determine the version name and any dependencies.
		 */
		_name = (char *)(strs + vdap->vda_name);

		if ((vdp = vers_desc(_name, elf_hash(_name), &verdefs)) == 0)
			return (0);
		vdp->vd_ndx = ndx;
		vdp->vd_flags = vdf->vd_flags | FLG_VER_AVAIL;

		vdap = (Verdaux *)((Word)vdap + vdap->vda_next);
		for (cnt--; cnt; cnt--,
		    vdap = (Verdaux *)((Word)vdap + vdap->vda_next)) {
			_name = (char *)(strs + vdap->vda_name);
			if (vers_depend(_name, elf_hash(_name), vdp,
			    &verdefs) == 0)
				return (0);
		}

		/*
		 * Remember the base version for possible later use.
		 */
		if (ndx == VER_NDX_GLOBAL)
			bvdp = vdp;
	}

	/*
	 * Normalize the dependency list if required.
	 */
	if (nflag) {
		for (LIST_TRAVERSE(&verdefs, lnp, vdp)) {
			Listnode *	_lnp;
			Ver_desc *	_vdp;
			int		type = vdp->vd_flags & VER_FLG_WEAK;

			for (LIST_TRAVERSE(&vdp->vd_deps, _lnp, _vdp))
				vers_derefer(_vdp, type);
		}

		/*
		 * Always dereference the base version.
		 */
		if (bvdp)
			bvdp->vd_flags &= ~FLG_VER_AVAIL;
	}


	/*
	 * Traverse the dependency list and print out the appropriate
	 * information.
	 */
	for (LIST_TRAVERSE(&verdefs, lnp, vdp)) {
		Listnode *	_lnp;
		Ver_desc *	_vdp;
		int		count;

		if (name && (strcmp(name, vdp->vd_name) != 0))
			continue;

		if (!name && !(vdp->vd_flags & FLG_VER_AVAIL))
			continue;

		error = 1;

		if (vflag) {
			/*
			 * If the verbose flag is set determine if this version
			 * has a `weak' attribute, and print any version
			 * dependencies this version inherits.
			 */
			if (oflag)
				(void) printf(Format_ofil, file);
			(void) printf("\t%s", vdp->vd_name);
			if (vdp->vd_flags & VER_FLG_WEAK)
				(void) printf(Format_weak);

			count = 1;
			for (LIST_TRAVERSE(&vdp->vd_deps, _lnp, _vdp)) {
				const char *	_name = _vdp->vd_name;

				if (count++ == 1) {
					if (oflag)
						(void) printf(": {%s", _name);
					else if (vdp->vd_flags & VER_FLG_WEAK)
						(void) printf(":\t{%s", _name);
					else
						(void) printf(":       \t{%s",
						    _name);
				} else
					(void) printf(Format_next, _name);
			}

			if (count != 1)
				(void) printf("}");

			if (sym && !oflag)
				(void) printf(":\n");
			else
				(void) printf(";\n");
		} else {
			if (sym && !oflag)
				(void) printf(Format_tnco, vdp->vd_name);
			else if (!sym) {
				if (oflag)
					(void) printf(Format_ofil, file);
				(void) printf(Format_tnse, vdp->vd_name);
			}
		}

		/*
		 * If we need to print symbols get the associated symbol table.
		 */
		if (sym) {
			shdr = sym->c_shdr;
			vsp = (Versym *)sym->c_data->d_buf;
			syms = (Sym *)cache[shdr->sh_link].c_data->d_buf;
			shdr = cache[shdr->sh_link].c_shdr;
			symn = shdr->sh_size / shdr->sh_entsize;
		} else
			continue;

		/*
		 * If a specific version name has been specified then display
		 * any of its own symbols plus any inherited from other
		 * versions.  Otherwise simply print out the symbols for this
		 * version.
		 */
		vers_syms(vsp, syms, symn, strs, vdp, file, oflag, vflag);
		if (name) {
			recurse_syms(vsp, syms, symn, strs, vdp, file, oflag,
			    vflag);

			/*
			 * If the verbose flag is set add the base version as a
			 * dependency (unless it's the list we were asked to
			 * print in the first place).
			 */
			if (vflag && bvdp && strcmp(name, bvdp->vd_name)) {
				if (!oflag)
				    (void) printf(Format_tnco, bvdp->vd_name);
				vers_syms(vsp, syms, symn, strs, bvdp, file,
				    oflag, vflag);
			}
		}
	}
	return (error);
}

int
main(int argc, char ** argv)
{
	Shdr *		shdr;
	Elf *		elf;
	Elf_Scn *	scn;
	Ehdr *		ehdr;
	int		nfile, var;
	const char *	name;
	int		dflag, nflag, oflag, rflag, sflag, vflag;
	Cache *		cache, * _cache;
	Cache *		_cache_def, * _cache_need, * _cache_sym;
	int		error = 0;

	name = NULL;
	dflag = nflag = oflag = rflag = sflag = vflag = 0;

	opterr = 0;
	while ((var = getopt(argc, argv, "dnorsvN:")) != EOF) {
		switch (var) {
		case 'd':
			dflag = USR_DEFINED;
			break;
		case 'n':
			nflag = USR_DEFINED;
			break;
		case 'o':
			oflag = USR_DEFINED;
			break;
		case 'r':
			rflag = USR_DEFINED;
			break;
		case 's':
			sflag = USR_DEFINED;
			break;
		case 'v':
			vflag = USR_DEFINED;
			break;
		case 'N':
			name = optarg;
			break;
		case '?':
			(void) fprintf(stderr, usage_msg, argv[0]);
			exit(1);
		default:
			break;
		}
	}

	/*
	 * No files specified on the command line?
	 */
	if ((nfile = argc - optind) == 0) {
		(void) fprintf(stderr, usage_msg, argv[0]);
		exit(1);
	}

	/*
	 * By default print both version definitions and needed dependencies.
	 */
	if ((dflag == 0) && (rflag == 0))
		dflag = rflag = DEF_DEFINED;

	/*
	 * Open the input file and initialize the elf interface.
	 */
	for (; optind < argc; optind++) {
		int		derror = 0;
		int		nerror = 0;
		const char *	file = argv[optind];

		if ((var = open(file, O_RDONLY)) == -1) {
			(void) fprintf(stderr, "file %s: open: %s\n",
				file, strerror(errno));
			error = 1;
			continue;
		}
		(void) elf_version(EV_CURRENT);
		if ((elf = elf_begin(var, ELF_C_READ, NULL)) == NULL) {
			(void) fprintf(stderr, "file %s: elf_begin: %s\n",
			    file, elf_errmsg(elf_errno()));
			error = 1;
			(void) close(var);
			continue;
		}
		if (elf_kind(elf) != ELF_K_ELF) {
			(void) fprintf(stderr, "file %s: is not elf\n", file);
			error = 1;
			(void) close(var);
			(void) elf_end(elf);
			continue;
		}
		if ((ehdr = elf_getehdr(elf)) == NULL) {
			(void) fprintf(stderr, "file %s: elf_getehdr: %s\n",
			    file, elf_errmsg(elf_errno()));
			error = 1;
			(void) close(var);
			(void) elf_end(elf);
			continue;
		}

		/*
		 * Fill in the cache descriptor with information for each
		 * section we might need.   We probably only need to save
		 * read-only allocable sections as this is where the version
		 * structures and their associated symbols and strings live.
		 * However, God knows what someone can do with a mapfile, and
		 * as elf_begin has already gone through all the overhead we
		 * might as well set up the cache for every section.
		 */
		if ((cache = (Cache *)calloc(ehdr->e_shnum,
		    sizeof (Cache))) == 0) {
			(void) fprintf(stderr, "file %s: calloc: %s\n",
			    file, strerror(errno));
			exit(1);
		}

		_cache_def = _cache_need = _cache_sym = 0;
		_cache = cache;
		_cache++;
		for (scn = NULL; scn = elf_nextscn(elf, scn); _cache++) {
			if ((shdr = elf_getshdr(scn)) == NULL) {
				(void) fprintf(stderr, "file %s: elf_getshdr: "
				    "%s\n", file, elf_errmsg(elf_errno()));
				error = 1;
				continue;
			}
			if ((_cache->c_data = elf_getdata(scn, NULL)) ==
			    NULL) {
				(void) fprintf(stderr, "file %s: elf_getdata: "
				    "%s\n", file, elf_errmsg(elf_errno()));
				error = 1;
				continue;
			}
			_cache->c_shdr = shdr;

			/*
			 * Remember the version sections.
			 */
			if ((shdr->sh_type == SHT_SUNW_verdef) && dflag)
				_cache_def = _cache;
			else if ((shdr->sh_type == SHT_SUNW_verneed) && rflag)
				_cache_need = _cache;
			else if ((shdr->sh_type == SHT_SUNW_versym) && sflag)
				_cache_sym = _cache;
		}

		/*
		 * If there is more than one input file, and we're not printing
		 * one-line output, display the filename being processed.
		 */
		if ((nfile > 1) && !oflag)
			(void) printf("%s:\n", file);

		/*
		 * Print the files version needed sections.
		 */
		if (_cache_need)
			nerror = vers_need(cache, _cache_need, file, oflag,
			    vflag, name);

		/*
		 * Print the files version definition sections.
		 */
		if (_cache_def)
			derror = vers_def(cache, _cache_def, _cache_sym,
			    file, nflag, oflag, vflag, name);

		/*
		 * Determine the error return.  There are three conditions that
		 * may produce an error (a non-zero return):
		 *
		 *  o	if the user specified -d and no version definitions
		 *	were found.
		 *
		 *  o	if the user specified -r and no version requirements
		 *	were found.
		 *
		 *  o	if the user specified neither -d or -r, (thus both are
		 *	enabled by default), and no version definitions or
		 *	version dependencies were found.
		 */
		if (((dflag == USR_DEFINED) && (derror == 0)) ||
		    ((rflag == USR_DEFINED) && (nerror == 0)) ||
		    (rflag && dflag && (derror == 0) && (nerror == 0)))
			error = 1;

		(void) close(var);
		(void) elf_end(elf);
		free(cache);
	}
	return (error);
}
