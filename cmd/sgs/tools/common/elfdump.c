/*
 *	Copyright (c) 1991 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)elfdump.c	1.15	94/10/21 SMI"

/*
 * Dump an elf file.
 */
#include	<sys/param.h>
#include	<fcntl.h>
#include	<stdio.h>
#include	<libelf.h>
#include	<link.h>
#include	<stdlib.h>
#include	<stdarg.h>
#include	<string.h>
#include	<unistd.h>
#include	"machdep.h"
#include	"debug.h"

#define	FLG_DYNAMIC	0x0001
#define	FLG_EHDR	0x0002
#define	FLG_INTERP	0x0004
#define	FLG_SHDR	0x0008
#define	FLG_NOTE	0x0010
#define	FLG_PHDR	0x0020
#define	FLG_RELOC	0x0040
#define	FLG_SYMBOLS	0x0080
#define	FLG_VERSIONS	0x0100
#define	FLG_HASH	0x0200

#define	FLG_EVERYTHING	0xffff

typedef struct cache {
	Shdr *		c_shdr;
	Elf_Data *	c_data;
	char *		c_name;
} Cache;

static const char
	* Errmsg_ivfn = "file %s: invalid file type\n";

extern int		errno;


/*
 * Define our own printing routine.  All Elf routines referenced call upon
 * this routine to carry out the actual printing.
 */
void
dbg_print(const char * format, ...)
{
	va_list		ap;

	va_start(ap, format);
	(void) vprintf(format, ap);
	(void) printf("\n");
}

/*
 * Define our own standard error routine.
 */
static void
failure(const char * file, const char * func)
{
	(void) fprintf(stderr, "%s: %s failed: %s\n", file, func,
	    elf_errmsg(elf_errno()));
}

/*
 * Print section headers.
 */
static void
sections(Cache * cache, Ehdr * ehdr, const char * name)
{
	unsigned int	cnt;
	Cache *		_cache;

	for (cnt = 1; cnt < ehdr->e_shnum; cnt++) {
		_cache = &cache[cnt];

		if (name && strcmp(name, _cache->c_name))
			continue;

		dbg_print("");
		dbg_print("Section Header[%d]:  sh_name: %s", cnt,
			_cache->c_name);
		Elf_shdr_entry(_cache->c_shdr);
	}
}

/*
 * Print the interpretor section.
 */
static void
interp(Cache * cache, Ehdr * ehdr)
{
	unsigned int	cnt;
	Cache *		_cache;

	for (cnt = 1; cnt < ehdr->e_shnum; cnt++) {
		_cache = &cache[cnt];

		if (strcmp(".interp", _cache->c_name))
			continue;

		dbg_print("");
		dbg_print("Interpreter Section:  .interp");
		dbg_print("	%s", (char *)_cache->c_data->d_buf);
		break;
	}
}

/*
 * Search for and process any version sections.
 */
static Versym *
versions(Cache * cache, Ehdr * ehdr)
{
	unsigned int	cnt;
	Shdr *		shdr;
	Cache *		_cache;
	char *		strs;
	void *		ver;
	Versym *	versym = 0;

	for (cnt = 1; cnt < ehdr->e_shnum; cnt++) {
		unsigned int	num;

		_cache = &cache[cnt];
		shdr = _cache->c_shdr;

		if ((shdr->sh_type < SHT_LOSUNW) ||
		    (shdr->sh_type > SHT_HISUNW))
			continue;

		ver = (void *)_cache->c_data->d_buf;

		/*
		 * If this is the version symbol table simply record its
		 * data address for possible use in later symbol processing.
		 */
		if (shdr->sh_type == SHT_SUNW_versym) {
			versym = (Versym *)ver;
			continue;
		}

		/*
		 * Get the data buffer for the associated string table.
		 */
		strs = (char *)cache[shdr->sh_link].c_data->d_buf;
		num = shdr->sh_info;

		dbg_print("");
		if (shdr->sh_type == SHT_SUNW_verdef) {
			dbg_print("Version Definition Section:  %s",
			    _cache->c_name);
			Elf_ver_def_print((Verdef *)ver, num, strs);
		} else if (shdr->sh_type == SHT_SUNW_verneed) {
			dbg_print("Version Needed Section:  %s",
			    _cache->c_name);
			Elf_ver_need_print((Verneed *)ver, num, strs);
		}
	}
	return (versym);
}

/*
 * Search for and process any symbol tables.
 */
static void
symbols(Cache * cache, Ehdr * ehdr, const char * name, Versym * versym)
{
	unsigned int	cnt;
	Shdr *		shdr;
	Cache *		_cache;
	char *		strs;
	Sym *		syms;
	int		symn, _cnt;

	for (cnt = 1; cnt < ehdr->e_shnum; cnt++) {
		_cache = &cache[cnt];
		shdr = _cache->c_shdr;

		if ((shdr->sh_type != SHT_SYMTAB) &&
		    (shdr->sh_type != SHT_DYNSYM))
			continue;
		if (name && strcmp(name, _cache->c_name))
			continue;

		/*
		 * Determine the symbol data and number.
		 */
		syms = (Sym *)_cache->c_data->d_buf;
		symn = shdr->sh_size / shdr->sh_entsize;

		/*
		 * Get the data buffer for the associated string table.
		 */
		strs = (char *)cache[shdr->sh_link].c_data->d_buf;

		/*
		 * loop through the symbol tables entries.
		 */
		dbg_print("");
		dbg_print("Symbol Table:  %s", _cache->c_name);
		Elf_sym_table_title("index", "name");

		for (_cnt = 0; _cnt < symn; _cnt++, syms++) {
			char	index[10];
			char *	sec;
			int	verndx;

			if (syms->st_shndx < SHN_LORESERVE)
				sec = cache[syms->st_shndx].c_name;
			else
				sec = NULL;

			/*
			 * If versioning is available display the version index
			 * for any dynsym entries.
			 */
			if ((shdr->sh_type == SHT_DYNSYM) && versym)
				verndx = versym[_cnt];
			else
				verndx = 0;

			(void) sprintf(index, " [%d]", _cnt);
			Elf_sym_table_entry(index, syms, verndx, sec,
				strs + syms->st_name);
		}
	}
}

/*
 * Search for and process any relocation sections.
 */
static void
reloc(Cache * cache, Ehdr * ehdr, const char * name)
{
	unsigned int	cnt;
	Shdr *		shdr;
	Cache *		_cache;
	char *		strs;
	Rel *		rsta, * rend;
	Sym *		syms;

	for (cnt = 1; cnt < ehdr->e_shnum; cnt++) {
		_cache = &cache[cnt];
		shdr = _cache->c_shdr;

		if ((shdr->sh_type != SHT_RELA) &&
		    (shdr->sh_type != SHT_REL))
			continue;
		if (name && strcmp(name, _cache->c_name))
			continue;

		/*
		 * Get a pointer to the relocation data buffer and determine
		 * the number of relocations available.
		 */
		rsta = (Rel *)_cache->c_data->d_buf;
		rend = rsta + (shdr->sh_size / shdr->sh_entsize);

		/*
		 * Get the data buffer for the associated symbol table.
		 */
		syms = (Sym *)cache[shdr->sh_link].c_data->d_buf;

		/*
		 * Get the data buffer for the associated string table.
		 */
		shdr = cache[shdr->sh_link].c_shdr;
		strs = (char *)cache[shdr->sh_link].c_data->d_buf;

		/*
		 * loop through the relocation entries.
		 */
		dbg_print("");
		dbg_print("Relocation: %s", _cache->c_name);
		dbg_print("\ttype\t\t   offset     addend  "
			"section        with respect to");

		while (rsta < rend) {
			char	section[24];
			char *	_name;
			Sym *	_sym;

			/*
			 * Determine the symbol with which this relocation is
			 * associated.  If the symbol represents a section
			 * offset construct an appropriate string.
			 */
			_sym = syms + ELF_R_SYM(rsta->r_info);
			if (ELF_ST_TYPE(_sym->st_info) == STT_SECTION) {
				(void) sprintf(section, "%.12s (section)",
					cache[_sym->st_shndx].c_name);
				_name = section;
			} else
				_name = strs + _sym->st_name;

			Elf_reloc_entry("", rsta, _cache->c_name, _name);
			rsta++;
		}
	}
}

/*
 * Search for and process a .dynamic section.
 */
static void
dynamic(Cache * cache, Ehdr * ehdr)
{
	unsigned int	cnt;
	Shdr *		shdr;
	Cache *		_cache;
	Dyn *		dyn;
	char *		strs;

	for (cnt = 1; cnt < ehdr->e_shnum; cnt++) {
		_cache = &cache[cnt];
		shdr = _cache->c_shdr;

		if (shdr->sh_type != SHT_DYNAMIC)
			continue;

		dyn = (Dyn *)_cache->c_data->d_buf;

		/*
		 * Get the data buffer for the associated string
		 * table.
		 */
		strs = (char *)cache[shdr->sh_link].c_data->d_buf;

		dbg_print("");
		dbg_print("Dynamic Section:  %s", _cache->c_name);
		Elf_dyn_print(dyn, strs);
	}
}

/*
 * Search for and process a .note section.
 */
static void
note(Cache * cache, Ehdr * ehdr, const char * name)
{
	unsigned int	cnt;
	Shdr *		shdr;
	Cache *		_cache;

	for (cnt = 1; cnt < ehdr->e_shnum; cnt++) {
		_cache = &cache[cnt];
		shdr = _cache->c_shdr;

		if (shdr->sh_type != SHT_NOTE)
			continue;
		if (name && strcmp(name, _cache->c_name))
			continue;

		dbg_print("");
		dbg_print("Note Section:  %s", _cache->c_name);
		Elf_note_entry((long *)_cache->c_data->d_buf);
	}
}


#define	MAXCOUNT	50

static void
hash(Cache * cache, Ehdr * ehdr, const char * name)
{
	static long	count[MAXCOUNT];
	unsigned long	cnt;
	unsigned long *	hash, * chain;
	Shdr *		shdr;
	char *		strs;
	Sym *		syms;
	unsigned long	ndx, bkts;
	char		number[10];
	Cache *		_cache;

	for (cnt = 1; cnt < ehdr->e_shnum; cnt++) {
		_cache = &cache[cnt];
		shdr = _cache->c_shdr;

		if (shdr->sh_type != SHT_HASH)
			continue;
		if (name && strcmp(name, _cache->c_name))
			continue;

		hash = (unsigned long *)_cache->c_data->d_buf;

		/*
		 * Determine the associated symbol table.
		 */
		syms = (Sym *)cache[shdr->sh_link].c_data->d_buf;

		/*
		 * Determine the symbol tables associated string table.
		 */
		shdr = cache[shdr->sh_link].c_shdr;
		strs = (char *)cache[shdr->sh_link].c_data->d_buf;


		bkts = *hash;
		chain = hash + 2 + bkts;
		hash += 2;

		dbg_print("");
		dbg_print("Hash Section:  %s  ", _cache->c_name);
		dbg_print("    bucket    symndx    name");

		/*
		 * Loop through the hash buckets, printing the appropriate
		 * symbols.
		 */
		for (ndx = 0; ndx < bkts; ndx++, hash++) {
			Sym *		_syms;
			char *		_strs;
			unsigned long	_ndx, _cnt;
			char		_number[10];
			const char *	format = "%10.10s  %-10s  %s";

			if (*hash == 0) {
				count[0]++;
				continue;
			}
			_syms = syms + *hash;
			_strs = strs + _syms->st_name;
			(void) sprintf(number, " %ld", ndx);
			(void) sprintf(_number, "[%ld]", *hash);
			dbg_print(format, number, _number, _strs);

			/*
			 * Determine if any other symbols are chained to this
			 * bucket.
			 */
			_ndx = chain[*hash];
			_cnt = 1;
			while (_ndx) {
				_syms = syms + _ndx;
				_strs = strs + _syms->st_name;
				(void) sprintf(_number, "[%ld]", _ndx);
				dbg_print(format, "", _number, _strs);
				_ndx = chain[_ndx];
				_cnt++;
			}
			if (_cnt >= MAXCOUNT)
				(void) fprintf(stderr, "warning: too many "
					"symbols to count, bucket=%ld "
					"count=%ld", ndx, _cnt);
			else
				count[_cnt]++;
		}
		break;
	}

	/*
	 * Print out the count information.
	 */
	bkts = cnt = 0;
	dbg_print("");
	for (ndx = 0; ndx < MAXCOUNT; ndx++) {
		long	_cnt;

		if ((_cnt = count[ndx]) == 0)
			continue;

		(void) sprintf(number, " %ld", _cnt);
		dbg_print("%10.10s  buckets contain %8d symbols", number, ndx);
		bkts += _cnt;
		cnt += (ndx * _cnt);
	}
	(void) sprintf(number, " %ld", bkts);
	dbg_print("%10.10s  buckets         %8d symbols (globals)",
	    number, cnt);
}


static void
regular(const char * file, Elf * elf, unsigned long flags,
	char * Nname, int wfd)
{
	Elf_Scn *	scn;
	Ehdr *		ehdr;
	Elf_Data *	data;
	unsigned int	cnt;
	char *		names;
	Cache *		cache, * _cache;
	Versym *	versym = 0;

	if ((ehdr = elf_getehdr(elf)) == NULL) {
		failure(file, "elf_getehdr");
		return;
	}

	/*
	 * Print the elf header.
	 */
	if (flags & FLG_EHDR)
		Elf_elf_header(ehdr);

	/*
	 * Print the program headers.
	 */
	if ((flags & FLG_PHDR) && ehdr->e_phnum) {
		Phdr *	phdr;

		if ((phdr = elf_getphdr(elf)) == NULL) {
			failure(file, "elf_getphdr");
			return;
		}

		for (cnt = 0; cnt < ehdr->e_phnum; cnt++, phdr++) {
			dbg_print("");
			dbg_print("Program Header[%d]:", cnt);
			Elf_phdr_entry(phdr);
		}
	}


	/*
	 * If there are no sections (core files), or if we don't want
	 * any section information we might as well return now.
	 */
	if ((ehdr->e_shnum == 0) || (flags & ~(FLG_EHDR | FLG_PHDR)) == 0)
		return;

	/*
	 * Obtain the .shstrtab data buffer to provide the required
	 * section name strings.
	 */
	if ((scn = elf_getscn(elf, ehdr->e_shstrndx)) == NULL) {
		failure(file, "elf_getscn");
		return;
	}

	if ((data = elf_getdata(scn, NULL)) == NULL) {
		failure(file, "elf_getdata");
		return;
	}

	names = data->d_buf;

	/*
	 * Fill in the cache descriptor with information for each section.
	 */
	if ((cache = (Cache *)malloc(ehdr->e_shnum * sizeof (Cache))) == 0) {
		(void) fprintf(stderr, "file %s: malloc: %s\n",
		    file, strerror(errno));
		return;
	}

	_cache = cache;
	_cache++;
	for (scn = NULL; scn = elf_nextscn(elf, scn); _cache++) {
		if ((_cache->c_shdr = elf_getshdr(scn)) == NULL) {
			failure(file, "elf_getshdr");
			free(cache);
			return;
		}

		if ((_cache->c_data = elf_getdata(scn, NULL)) == NULL) {
			failure(file, "elf_getdata");
			free(cache);
			return;
		}

		_cache->c_name = names + _cache->c_shdr->sh_name;

		/*
		 * Do we wish to write the section out?
		 */
		if (wfd && Nname && (strcmp(Nname, _cache->c_name) == 0)) {
			(void) write(wfd, _cache->c_data->d_buf,
			    _cache->c_data->d_size);
		}
	}

	if (flags & FLG_SHDR)
		sections(cache, ehdr, Nname);

	if (flags & FLG_INTERP)
		interp(cache, ehdr);

	if (flags & FLG_VERSIONS)
		versym = versions(cache, ehdr);

	if (flags & FLG_SYMBOLS)
		symbols(cache, ehdr, Nname, versym);

	if (flags & FLG_HASH)
		hash(cache, ehdr, Nname);

	if (flags & FLG_RELOC)
		reloc(cache, ehdr, Nname);

	if (flags & FLG_DYNAMIC)
		dynamic(cache, ehdr);

	if (flags & FLG_NOTE)
		note(cache, ehdr, Nname);

	free(cache);
}

static void
archive(const char * file, int fd, Elf * elf, unsigned long flags,
	char * Nname, int wfd)
{
	Elf_Cmd	cmd = ELF_C_READ;
	Elf *	_elf;

	/*
	 * Determine if the archive sysmbol table itself is required.
	 */
	if ((flags & FLG_SYMBOLS) &&
	    ((Nname == NULL) || (strcmp(Nname, "ARSYM") == 0))) {
		Elf_Arsym *	arsym;
		unsigned int	cnt, ptr;
		char		index[10];

		/*
		 * Get the archive symbol table.
		 */
		if ((arsym = elf_getarsym(elf, &ptr)) == 0) {
			(void) fprintf(stderr, "%s: can't locate archive "
			    "symbol table\n", file);
			return;
		}

		/*
		 * Print out all the symbol entries.
		 */
		dbg_print("\nSymbol Table: (archive)");
		dbg_print("     index    offset    name");
		for (cnt = 0; cnt < ptr; cnt++, arsym++) {
			(void) sprintf(index, " [%d]", cnt);
			dbg_print("%10.10s  0x%8.8x  %s", index, arsym->as_off,
			    (arsym->as_name ? arsym->as_name : "(null)"));
		}
	}

	/*
	 * Process each object within the archive.
	 */
	while ((_elf = elf_begin(fd, cmd, elf)) != NULL) {
		Elf_Arhdr *	arhdr = elf_getarhdr(_elf);
		char		name[MAXPATHLEN];

		if (*arhdr->ar_name != '/') {
			(void) sprintf(name, "%s(%s)", file, arhdr->ar_name);
			dbg_print("\n%s:", name);

			switch (elf_kind(_elf)) {
			case ELF_K_AR:
				archive(name, fd, _elf, flags, Nname, wfd);
				break;
			case ELF_K_ELF:
				regular(name, _elf, flags, Nname, wfd);
				break;
			default:
				(void) fprintf(stderr, Errmsg_ivfn, name);
				break;
			}
		}

		cmd = elf_next(_elf);
		(void) elf_end(_elf);
	}
}

main(int argc, char ** argv)
{
	Elf *		elf;
	int		var, fd, wfd = 0;
	char *		Nname = NULL, * wname = NULL;
	unsigned long	flags = 0;

	const char *	usage = "usage: %s "
				"[-cdeihnprsv] [-w file] [-N name] file(s)\n";
	const char *	detail_usage =
			"\t[-c Dump section header information]\n"
			"\t[-d Dump the contents fo the .dynamic section]\n"
			"\t[-e Dump the elf header.]\n"
			"\t[-i Dump the contents of the .interp section]\n"
			"\t[-h Dump the contents of the .hash section]\n"
			"\t[-n Dump the contents of the .note section]\n"
			"\t[-p Dump the program headers]\n"
			"\t[-r Dump the contents of the relocation "
			"sections]\n"
			"\t[-s Dump the contents of the symbol table "
			"sections]\n"
			"\t[-v Dump the contents of the version sections]\n"
			"\t[-w Write the contents of specified "
			"section to file]\n"
			"\t[-N qualify an option with a Name]\n";

	opterr = 0;
	while ((var = getopt(argc, argv, "cdeihnprsw:vN:")) != EOF) {
		switch (var) {
		case 'c':
			flags |= FLG_SHDR;
			break;
		case 'd':
			flags |= FLG_DYNAMIC;
			break;
		case 'e':
			flags |= FLG_EHDR;
			break;
		case 'h':
			flags |= FLG_HASH;
			break;
		case 'i':
			flags |= FLG_INTERP;
			break;
		case 'n':
			flags |= FLG_NOTE;
			break;
		case 'p':
			flags |= FLG_PHDR;
			break;
		case 'r':
			flags |= FLG_RELOC;
			break;
		case 's':
			flags |= FLG_SYMBOLS;
			break;
		case 'w':
			wname = optarg;
			break;
		case 'v':
			flags |= FLG_VERSIONS;
			break;
		case 'N':
			Nname = optarg;
			break;
		case '?':
			(void) fprintf(stderr, usage, argv[0]);
			(void) fprintf(stderr, detail_usage);
			exit(1);
		default:
			break;
		}
	}

	/*
	 * Validate any arguments.
	 */
	if (flags == 0) {
		if (wname || Nname) {
			(void) fprintf(stderr, "options -w or -N must augment "
			    "other options\n");
			exit(1);
		}
		flags = FLG_EVERYTHING;
	}
	if ((var = argc - optind) == 0) {
		(void) fprintf(stderr, usage, argv[0]);
		exit(1);
	}

	/*
	 * If the -w option has indicated an output file open it.  It's
	 * arguable whether this option has much use when multiple files are
	 * being processed.
	 */
	if (wname) {
		if ((wfd = open(wname, (O_RDWR | O_CREAT | O_TRUNC),
		    0666)) < 0) {
			(void) fprintf(stderr, "file %s: open: %s\n", wname,
			    strerror(errno));
			wfd = 0;
		}
	}

	/*
	 * Open the input file and initialize the elf interface.
	 */
	for (; optind < argc; optind++) {
		const char *	file = argv[optind];

		if ((fd = open(argv[optind], O_RDONLY)) == -1) {
			(void) fprintf(stderr, "file %s: open: %s\n",
			    file, strerror(errno));
			continue;
		}
		(void) elf_version(EV_CURRENT);
		if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
			failure(file, "elf_begin");
			(void) close(fd);
			continue;
		}

		if (var > 1)
			dbg_print("\n%s:\n", file);

		switch (elf_kind(elf)) {
		case ELF_K_AR:
			archive(file, fd, elf, flags, Nname, wfd);
			break;
		case ELF_K_ELF:
			regular(file, elf, flags, Nname, wfd);
			break;
		default:
			(void) fprintf(stderr, Errmsg_ivfn, file);
			break;
		}

		(void) close(fd);
		(void) elf_end(elf);
	}

	if (wfd)
		(void) close(wfd);
	return (0);
}
