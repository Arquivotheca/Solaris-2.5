/*	Copyright (c) 1988 AT&T	*/
/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)nm.c	6.25	95/03/16 SMI"

#include <stdio.h>
#include <locale.h>
#include <libelf.h>

#ifdef __STDC__
#include <stdlib.h>
#define	VOID_P void *
#else
#define	VOID_P char *
#endif

/* exit return codes */
#define	NOARGS	1
#define	BADELF	2
#define	NOALLOC 3

#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include "sgs.h"

typedef struct {		/* structure to translate symbol table data */
	int  indx;
	char * name;
	Elf32_Addr value;
	Elf32_Word size;
	int type;
	int bind;
	unsigned char other;
	Elf32_Half shndx;
} SYM;

#define	UNDEFINED "U"
#define	BSS_GLOB  "B"
#define	BSS_WEAK  "B*"
#define	BSS_LOCL  "b"
#define	BSS_SECN  ".bss"

#define	OPTSTR	":APoxhvnursplCVefgRTt:" /* option string for getopt() */

#define	DATESIZE 60

#define	TYPE 5
#define	BIND 3

#define	DEF_MAX_SYM_SIZE 256

static char *key[TYPE][BIND];

static  int	/* flags: ?_flag corresponds to ? option */
	o_flag = 0,	/* print value and size in octal */
	x_flag = 0,	/* print value and size in hex */
	d_flag = 0,	/* print value and size in decimal */
	h_flag = 0,	/* suppress printing of headings */
	v_flag = 0,	/* sort external symbols by value */
	n_flag = 0,	/* sort external symbols by name */
	u_flag = 0,	/* print only undefined symbols */
	r_flag = 0,	/* prepend object file or archive name */
			/* to each symbol name */
	R_flag = 0,	/* if "-R" issued then prepend archive name, */
			/* object file name to each symbol */
	s_flag = 0,	/* print section name instead of section index */
	p_flag = 0,	/* produce terse output */
	P_flag = 0,	/* Portable format output */
	l_flag = 0,	/* produce long listing of output */
	C_flag = 0,	/* print decoded C++ names */
	A_flag = 0,	/* FIle name */
	e_flag = 0,	/* -e flag */
	g_flag = 0,	/* -g flag */
	f_flag = 0,	/* -f flag */
	t_flag = 0,	/* -t flag */
	V_flag = 0;	/* print version information */
static char A_header[DEF_MAX_SYM_SIZE+1] = {0};
extern int close();

#ifdef SPARC
extern char *demangle(); /* changed from elf_demangle() (from libelf.a) */
			/* to demangle() (from C++ mangle.a) */
#endif
#ifdef I386
extern char *elf_demangle();
#endif

static char *prog_name;
static char *archive_name = (char *)0;
static int errflag = 0;
static void usage();
static void each_file();
static void process();
static Elf_Scn *get_scnfd();
static VOID_P get_scndata();
static void get_symtab();
static SYM *readsyms();
static int compare();
static char *lookup();
static int  is_bss_section();
static void print_ar_files();
static void print_symtab();
static void parsename();
static void parse_fn_and_print();
static char d_buf[512];
static char p_buf[512];
static void set_A_header(char *);

/*
 * Parses the command line options and then
 * calls each_file() to process each file.
 */
main(argc, argv)
	int argc;
	char *argv[];
{
	char	*optstr = OPTSTR; /* option string used by getopt() */
	extern  int optind;		/* arg list index */
	extern char *optarg;
	extern int optopt;
	int    optchar;

	/* table of keyletters for use with -p and -P options */
	key[STT_NOTYPE][STB_LOCAL] = "n";
	key[STT_NOTYPE][STB_GLOBAL] = "N";
	key[STT_NOTYPE][STB_WEAK] = "N*";
	key[STT_OBJECT][STB_LOCAL] = "d";
	key[STT_OBJECT][STB_GLOBAL] = "D";
	key[STT_OBJECT][STB_WEAK] = "D*";
	key[STT_FUNC][STB_LOCAL] = "t";
	key[STT_FUNC][STB_GLOBAL] = "T";
	key[STT_FUNC][STB_WEAK] = "T*";
	key[STT_SECTION][STB_LOCAL] = "s";
	key[STT_SECTION][STB_GLOBAL] = "S";
	key[STT_SECTION][STB_WEAK] = "S*";
	key[STT_FILE][STB_LOCAL] = "f";
	key[STT_FILE][STB_GLOBAL] = "F";
	key[STT_FILE][STB_WEAK] = "F*";

	prog_name = argv[0];

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	while ((optchar = getopt(argc, argv, optstr)) != -1) {
		switch (optchar) {
		case 'o':	if (!x_flag && !d_flag)
					o_flag = 1;
				else
					(void) fprintf(stderr, gettext(
					"%s: -x set, -o ignored\n"),
					prog_name);
				break;
		case 'x':	if (!o_flag && !d_flag)
					x_flag = 1;
				else
					(void) fprintf(stderr, gettext(
					"%s: -o set, -x ignored\n"),
					prog_name);
				break;
		case 'h':	h_flag = 1;
				break;
		case 'v':	if (!n_flag)
					v_flag = 1;
				else
					(void) fprintf(stderr, gettext(
					"%s: -n set, -v ignored\n"),
					prog_name);
				break;
		case 'n':	if (!v_flag)
					n_flag = 1;
				else
					(void) fprintf(stderr, gettext(
					"%s: -v set, -n ignored\n"),
					prog_name);
				break;
		case 'u':	if (!e_flag && !g_flag)
					u_flag = 1;
				else
					(void) fprintf(stderr, gettext(
					"%s: -e or -g set, -u ignored\n"),
					prog_name);
				break;
		case 'e':	if (!u_flag && !g_flag)
					e_flag = 1;
				else
					(void) fprintf(stderr, gettext(
					"%s: -u or -g set, -e ignored\n"),
					prog_name);
				break;
		case 'g':	if (!u_flag && !e_flag)
					g_flag = 1;
				else
					(void) fprintf(stderr, gettext(
					"%s: -u or -e set, -g ignored\n"),
					prog_name);
				break;
		case 'r': 	if (R_flag) {
					R_flag = 0;
					(void) fprintf(stderr, gettext(
						"%s: -r set, -R ignored\n"),
						prog_name);
				}
				r_flag = 1;
				break;
		case 's':	s_flag = 1;
				break;
		case 'p':	if (P_flag == 1) {
					fprintf(stderr, gettext(
					"nm: -P set. -p ignored\n"));
				} else
					p_flag = 1;
				break;
		case 'P':	if (p_flag == 1) {
					fprintf(stderr, gettext(
					"nm: -p set. -P ignored\n"));
				} else
					P_flag = 1;
				break;
		case 'l':	l_flag = 1;
				break;
		case 'C':	C_flag = 1;
				break;
		case 'A':	A_flag = 1;
				break;
		case 'V':	V_flag = 1;
				(void) fprintf(stderr,
					"nm: %s %s\n",
					(const char *)SGU_PKG,
					(const char *)SGU_REL);
				break;
		case 'f':	f_flag = 1;
				break;
		case 'R':	if (!r_flag)
					R_flag = 1;
				else
					(void) fprintf(stderr, gettext(
						"%s: -r set, -R ignored\n"),
						prog_name);
				break;
		case 'T':
				break;
		case 't':	if (t_flag || o_flag || x_flag) {
					fprintf(stderr, gettext(
				"nm: -t or -o or -x set. -t ignored.\n"));
				} else if (strcmp(optarg, "o") == 0) {
					t_flag = 1;
					o_flag = 1;
				} else if (strcmp(optarg, "d") == 0) {
					t_flag = 1;
					d_flag = 1;
				} else if (strcmp(optarg, "x") == 0) {
					t_flag = 1;
					x_flag = 1;
				} else {
					fprintf(stderr, gettext(
"nm: illegal format '%s' for -t is specified. -t ignored.\n"), optarg);
				}
				break;
		case ':':	errflag += 1;
				fprintf(stderr, gettext(
					"nm: %c requires operand\n"),
					optopt);
				break;
		case '?':	errflag += 1;
				break;
		default:	break;
		}
	}

	if (errflag || (optind >= argc))
	{
		if (!(V_flag && (argc == 2)))
		{
			usage();
			exit(NOARGS);
		}
	}

	while (optind < argc)
	{
		each_file(argv[optind]);
		optind++;
	}
	return (errflag);
}

/*
 * Print out a usage message in short form when program is invoked
 * with insufficient or no arguements, and in long form when given
 * either a ? or an invalid option.
 */
static void
usage()
{
	(void) fprintf(stderr, gettext(
"Usage: nm [-APvChlnV] [-efox] [-r | -R]  [-g | -u] [-t format] file ...\n"));
}

/*
 * Takes a filename as input.  Test first for a valid version
 * of libelf.a and exit on error.  Process each valid file
 * or archive given as input on the command line.  Check
 * for file type.  If it is an archive, call print_ar_files
 * to process each member of the archive in the same manner
 * as object files on the command line.  The same tests for
 * valid object file type apply to regular archive members.
 * If it is an ELF or COFF object file, process it; otherwise
 * warn that it is an invalid file type and return from
 * processing the file.
 */

static void
each_file(filename)
char * filename;
{
	Elf	*elf_file;
	int	fd;
	Elf_Kind   file_type;

	struct stat buf;

	Elf_Cmd cmd;
	errno = 0;
	if (stat(filename, &buf) == -1)
	{
		(void) fprintf(stderr,
			"%s: ", prog_name);
		perror(filename);
		errflag++;
		return;
	}
	if (elf_version(EV_CURRENT) == EV_NONE)
	{
		(void) fprintf(stderr, gettext(
			"%s: %s: Libelf is out of date\n"),
			prog_name, filename);
		exit(BADELF);
	}

	if ((fd = open((filename), O_RDONLY)) == -1)
	{
		(void) fprintf(stderr, gettext(
		"%s: %s: cannot read file\n"),
		prog_name, filename);
		errflag++;
		return;
	}
	cmd = ELF_C_READ;
	if ((elf_file = elf_begin(fd, cmd, (Elf *) 0)) == NULL)
	{
		(void) fprintf(stderr,
		"%s: %s: %s\n", prog_name, filename, elf_errmsg(-1));
		errflag++;
		return;
	}
	file_type = elf_kind(elf_file);
	if (file_type == ELF_K_AR)
	{
		print_ar_files(fd, elf_file, filename);
	}
	else
	{
		if ((file_type == ELF_K_ELF) || (file_type == ELF_K_COFF))
		{
						if (file_type == ELF_K_COFF)
							(void) fprintf(stderr,
"%s: %s: warning - internal conversion of COFF file to ELF\n",
							prog_name, filename);
#ifndef XPG4
			if (u_flag && !h_flag) {
				/*
				 * u_flag is specified.
				 */
				if (p_flag)
					(void) printf(
						"\n\n%s:\n\n",
						filename);
				else
					(void) printf(gettext(
				"\n\nUndefined symbols from %s:\n\n"),
				filename);
			} else if (!h_flag & !P_flag)
#else
			if (!h_flag & !P_flag)
#endif
			{
				if (p_flag)
					(void) printf(
						"\n\n%s:\n",
						filename);
				else {
					if (A_flag != 0)
						(void) printf(
						"\n\n%s%s:\n",
						A_header,
						filename);
					else
						(void) printf(
						"\n\n%s:\n",
						filename);
				}
			}
			archive_name = (char *)0;
			process(elf_file, filename);
		}
		else
		{
			(void) fprintf(stderr, gettext(
				"%s: %s: invalid file type\n"),
				prog_name, filename);
			errflag++;
			elf_end(elf_file);
			(void) close(fd);
		}
	}
	elf_end(elf_file);
	(void) close(fd);
}

/*
 * Get the ELF header and, if it exists, call get_symtab()
 * to begin processing of the file; otherwise, return from
 * processing the file with a warning.
 */
static void
process(elf_file, filename)
Elf * elf_file;
char * filename;
{
	Elf32_Ehdr *p_ehdr;

	p_ehdr = elf32_getehdr(elf_file);
	if (p_ehdr == (Elf32_Ehdr *)0)
	{
		(void) fprintf(stderr,
			"%s: %s: %s\n", prog_name, filename, elf_errmsg(-1));
		return;
	}
	set_A_header(filename);
	get_symtab(elf_file, p_ehdr, filename);
}

/*
 * Get section descriptor for the associated string table
 * and verify that the type of the section pointed to is
 * indeed of type STRTAB.  Returns a valid section descriptor
 * or NULL on error.
 */
static Elf_Scn *
get_scnfd(e_file, shstrtab, SCN_TYPE)
Elf	*e_file;
Elf32_Half  shstrtab;
Elf32_Half  SCN_TYPE;
{
	Elf_Scn    *fd_scn;
	Elf32_Shdr *p_scnhdr;

	if ((fd_scn = elf_getscn(e_file, shstrtab)) == NULL)
	{
		return (NULL);
	}

	if ((p_scnhdr = elf32_getshdr(fd_scn)) == NULL)
	{
		return (NULL);
	}

	if (p_scnhdr->sh_type != SCN_TYPE)
	{
		return (NULL);
	}
	return (fd_scn);
}

/*
 * Get the section descriptor and set the size of the
 * data returned.  Data is byte-order converted by
 * the access library.  Section size must be calculated
 * on the return from elf_getdata() in order to correctly
 * print out COFF file information.
 */
static VOID_P
get_scndata(fd_scn, scn_size)
Elf_Scn *fd_scn;
size_t  *scn_size;
{
	Elf_Data *p_data;

	p_data = 0;
	if ((p_data = elf_getdata(fd_scn, p_data)) == 0 ||
		p_data->d_size == 0)
	{
		return (NULL);
	}

	*scn_size = p_data->d_size;
	return (p_data->d_buf);
}

/*
 * Print the symbol table.  This function does not print the contents
 * of the symbol table but sets up the parameters and then calls
 * print_symtab to print the symbols.  This function does not assume
 * that there is only one section of type SYMTAB.  Input is an opened
 * ELF file, a pointer to the ELF header, and the filename.
 */
static void
get_symtab(elf_file, elf_head_p, filename)
Elf	*elf_file;
Elf32_Ehdr *elf_head_p;
char	*filename;
{
	Elf32_Shdr	*p_shdr;
	Elf_Scn	*scn, *scnfd;
	char	*offset = NULL;
	char	*s_name;
	size_t	str_size;

/* get section header string table */
	scnfd = get_scnfd(elf_file, elf_head_p->e_shstrndx, SHT_STRTAB);
	if (scnfd == NULL)
	{
		(void) fprintf(stderr, gettext(
			"%s: %s: could not get string table\n"),
			prog_name, filename);
		return;
	}
	offset = (char *)get_scndata(scnfd, &str_size);
	if (offset == (char *)0)
	{
		(void) fprintf(stderr, gettext(
			"%s: %s: no data in string table\n"),
			prog_name, filename);
		return;
	}

	scn = 0;
	while ((scn = elf_nextscn(elf_file, scn)) != 0)
	{
		s_name = NULL;
		p_shdr = NULL;
		if ((p_shdr = elf32_getshdr(scn)) == 0)
		{
			(void) fprintf(stderr,
				"%s: %s: %s:\n",
				prog_name,
				filename, elf_errmsg(-1));
			return;
		}
		s_name = offset + p_shdr->sh_name;

		if (p_shdr->sh_type == SHT_SYMTAB)
		{
			print_symtab(elf_file, s_name, p_shdr,
				scn, filename, elf_head_p);
		}
	} /* end while */
}

/*
 * Process member files of an archive.  This function provides
 * a loop through an archive equivalent the processing of
 * each_file for individual object files.
 */
static void
print_ar_files(fd, elf_file, filename)
int fd;
Elf *elf_file;
char *filename;
{
	Elf_Arhdr  *p_ar;
	Elf	*arf;
	Elf_Cmd    cmd;
	Elf_Kind   file_type;


	cmd = ELF_C_READ;
	archive_name = filename;
	while ((arf = elf_begin(fd, cmd, elf_file)) != 0)
	{
		p_ar = elf_getarhdr(arf);
		if (p_ar == NULL)
		{
			(void) fprintf(stderr,
				"%s: %s: %s\n",
				prog_name,
				filename,
				elf_errmsg(-1));
			return;
		}
		if ((int)strncmp(p_ar->ar_name, "/", 1) == 0)
		{
			cmd = elf_next(arf);
			elf_end(arf);
			continue;
		}

		if (!h_flag & !P_flag)
		{
			if (p_flag)
				(void) printf("\n\n%s[%s]:\n",
				filename, p_ar->ar_name);
			else {
				if (A_flag != 0)
					(void) printf(
					"\n\n%s%s[%s]:\n",
					A_header,
					filename, p_ar->ar_name);
				else
					(void) printf(
					"\n\n%s[%s]:\n",
					filename, p_ar->ar_name);
			}
		}
		file_type = elf_kind(arf);
		if ((file_type == ELF_K_ELF) || (file_type == ELF_K_COFF))
		{
			if (file_type == ELF_K_COFF)
				(void) fprintf(stderr, gettext(
"%s: %s[%s]: warning - internal conversion of COFF file to ELF\n"),
				prog_name, filename, p_ar->ar_name);
			process(arf, p_ar->ar_name);
		}
		else
		{
			(void) fprintf(stderr, gettext(
				"%s: %s: invalid file type\n"),
				prog_name, p_ar->ar_name);
			cmd = elf_next(arf);
			elf_end(arf);
			errflag++;
			continue;
		}

		cmd = elf_next(arf);
		elf_end(arf);
	} /* end while */
}

/*
 * Print the symbol table according to the flags that were
 * set, if any.  Input is an opened ELF file, the section name,
 * the section header, the section descriptor, and the filename.
 * First get the symbol table with a call to get_scndata.
 * Then translate the symbol table data in memory by calling
 * readsyms().  This avoids duplication of function calls
 * and improves sorting efficiency.  qsort is used when sorting
 * is requested.
 */
static void
print_symtab(elf_file, s_name, p_shdr, p_sd, filename, elf_head_p)
Elf	*elf_file;
Elf32_Ehdr *elf_head_p;
char    *s_name;
Elf32_Shdr *p_shdr;
Elf_Scn *p_sd;
char *filename;
{

	Elf32_Sym  *sym;
	SYM	*sym_data;
	size_t	count = 0;
	size_t	sym_size = 0;
	static void print_header();
#ifndef XPG4
	static void print_with_uflag(Elf *, SYM *, char *, Elf32_Ehdr *);
#endif
	static void print_with_pflag(Elf *, SYM *, char *, Elf32_Ehdr *);
	static void print_with_Pflag(Elf *, SYM *, char *, Elf32_Ehdr *);
	static void print_with_otherflags(Elf *, SYM *, char *, Elf32_Ehdr *);

	/*
	 * print header
	 */
#ifndef XPG4
	if (!u_flag)
		print_header();
#else
	print_header();
#endif

	/*
	 * get symbol table data
	 */
	sym = NULL;
	if ((sym = (Elf32_Sym *)get_scndata(p_sd, &sym_size)) == NULL)
	{
		(void) printf(gettext(
			"%s: %s - No symbol table data\n"),
			prog_name, filename);
		return;
	}
	count = sym_size/sizeof (Elf32_Sym);
	sym++;	/* first member holds the number of symbols */

	/*
	 * translate symbol table data
	 */
	sym_data = readsyms(sym, count, elf_file, p_shdr->sh_link);
	if (sym_data == NULL)
	{
		(void) fprintf(stderr, gettext(
			"%s: %s: problem reading symbol data\n"),
			prog_name, filename);
		return;
	}
	qsort((char *)sym_data, count-1, sizeof (*sym_data), compare);
	while (count > 1)
	{
#ifndef XPG4
		if (u_flag) {
			/*
			 * U_flag specified
			 */
			print_with_uflag(elf_file, sym_data,
				filename, elf_head_p);
		} else if (p_flag)
#else
		if (p_flag)
#endif
			print_with_pflag(elf_file, sym_data,
				filename, elf_head_p);
		else if (P_flag)
			print_with_Pflag(elf_file, sym_data,
				filename, elf_head_p);
		else
			print_with_otherflags(elf_file, sym_data,
				filename, elf_head_p);
		sym_data++;
		count--;
	}
}

/*
 * Return appropriate keyletter(s) for -p option.
 * Returns an index into the key[][] table or NULL if
 * the value of the keyletter is unknown.
 */
static char *
lookup(a, b)
int a;
int b;
{
	return (((a < TYPE) && (b < BIND)) ? key[a][b] : NULL);
}

/*
 * Return TRUE(1) if the given section is ".bss" for "-p" option.
 * Return FALSE(0) if not ".bss" section.
 */
static int
is_bss_section(shndx, elf_file, elf_head_p)
Elf32_Half shndx;
Elf	*elf_file;
Elf32_Ehdr *elf_head_p;
{
	Elf32_Shdr 	*tmp_shdr;
	char		*sym_name;

	tmp_shdr = elf32_getshdr(elf_getscn(elf_file, shndx));
	if (tmp_shdr && tmp_shdr->sh_name)
	{
		sym_name = elf_strptr(elf_file, elf_head_p->e_shstrndx,
			tmp_shdr->sh_name);
		if (strcmp(BSS_SECN, sym_name) == 0)
			return (1);
	}
	return (0);
}

/*
 * Translate symbol table data particularly for sorting.
 * Input is the symbol table data structure, number of symbols,
 * opened ELF file, and the string table link offset.
 */
static SYM *
readsyms(data, num, elf, link)
Elf32_Sym *data;
size_t	num;
Elf	*elf;
Elf32_Word  link;
{
	extern char *DemangleAndFormat(char *, char *);
	static char *FormatName(char *, char *);
	static char *format1 = "%s\n             [%s]";
	static char *format2 = "%s\n\t\t\t\t\t\t       [%s]";
	SYM *s, *buf;
	int i;

	if ((buf = (SYM *)calloc(num, sizeof (SYM))) == NULL)
	{
		(void) fprintf(stderr,
			"%s: cannot calloc space\n", prog_name);
		return (NULL);
	}

	s = buf;	/* save pointer to head of array */

	for (i = 1; i < num; i++, data++, buf++)
	{
		buf->indx = i;
		/* allow to work on machines where NULL-derefs dump core */
		if (data->st_name == 0)
			buf->name = "";
		else if (C_flag)
		{
			char *name = (char *)elf_strptr(elf, link,
					data->st_name);
#ifdef I386
			char *dn = elf_demangle(name);
#endif

				/*
				 * Following changes made within #ifdef SPARC so
				 * that it does not affect nm on 386. --MK
				 */
#ifdef SPARC
					name = DemangleAndFormat(name,
						p_flag ? format1 : format2);

#else  /* not SPARC */
			if (dn == name)  /* name not demangled */
			{
				if (exotic(name))
				{
					name = FormatName(name, d_buf);
				}
			} else  /* name demangled */
			{
				name = FormatName(name, dn);
			}
#endif  /* not SPARC */

			buf->name = name;
		}
		else
			buf->name = (char *)elf_strptr(elf,
					link,
					data->st_name);
		buf->value = data->st_value;
		buf->size = data->st_size;
		buf->type = ELF32_ST_TYPE(data->st_info);
		buf->bind = ELF32_ST_BIND(data->st_info);
		buf->other = data->st_other;
		buf->shndx = data->st_shndx;
	}	/* end for loop */
	return (s);
}

/*
 * compare either by name or by value for sorting.
 * This is the comparison function called by qsort to
 * sort the symbols either by name or value when requested.
 */
static int
compare(a, b)
SYM *a, *b;
{
	if (v_flag)
	{
		if (a->value > b->value)
			return (1);
		else
			return ((a->value == b->value) -1);
	}
	else
		return ((int) strcoll(a->name, b->name));
}

/*
 * Set up a header line for -A option.
 */
static void
set_A_header(char *fname)
{
	if (A_flag == 0)
		return;

	if (archive_name == (char *)0)
		sprintf(A_header, "%s: ", fname);
	else
		sprintf(A_header, "%s[%s]: ",
			archive_name,
			fname);
}

/*
 * output functions
 *	The following functions are called from
 *	print_symtab().
 */
static void
print_header()
{
	/*
	 * Print header line if needed.
	 */
	if (h_flag == 0 && p_flag == 0 && P_flag == 0) {
		(void) printf("\n");
		if (A_flag != 0)
			printf("%s", A_header);
		if (o_flag) {
			if (!s_flag) {
				(void) printf(
			"%-9s%-13s%-13s%-6s%-6s%-6s%-8s%s\n\n",
				"[Index]", " Value", " Size",
				"Type", "Bind", "Other",
				"Shndx", "Name");
			}
			else
			{
				(void) printf(
			"%-9s%-13s%-13s%-6s%-6s%-6s%-15s%s\n\n",
				"[Index]", " Value", " Size",
				"Type", "Bind", "Other",
				"Shname", "Name");
			}
		} else if (x_flag) {
			if (!s_flag) {
				(void) printf(
			"%-9s%-11s%-11s%-6s%-6s%-6s%-8s%s\n\n",
				"[Index]", " Value", " Size",
				"Type", "Bind", "Other",
				"Shndx", "Name");
			}
			else
			{
				(void) printf(
			"%-9s%-11s%-11s%-6s%-6s%-6s%-15s%s\n\n",
				"[Index]", " Value", " Size",
				"Type", "Bind", "Other",
				"Shname", "Name");
			}
		}
		else
		{
			if (!s_flag) {
				(void) printf(
			"%-9s%-11s%-9s%-6s%-6s%-6s%-8s%s\n\n",
				"[Index]", " Value", " Size",
				"Type", "Bind", "Other",
				"Shndx", "Name");
			}
			else
			{
				(void) printf(
			"%-9s%-11s%-9s%-6s%-6s%-6s%-15s%s\n\n",
				"[Index]", " Value", " Size",
				"Type", "Bind", "Other",
				"Shname", "Name");
			}
		}
	}
}

/*
 * If the symbol can be printed, then return 1.
 * If the symbol can not be printed, then return 0.
 */
static int
is_sym_print(SYM *sym_data)
{
	/*
	 * If -u flag is specified,
	 *	the symbol has to be undefined.
	 */
	if (u_flag != 0) {
		if ((sym_data->shndx == SHN_UNDEF) &&
			(strlen(sym_data->name) != 0))
			return (1);
		else
			return (0);
	}

	/*
	 * If -e flag is specified,
	 *	the symbol has to be global or static.
	 */
	if (e_flag != 0) {
		switch (sym_data->type) {
		case STT_NOTYPE:
		case STT_OBJECT:
		case STT_FUNC:
			switch (sym_data->bind) {
			case STB_LOCAL:
			case STB_GLOBAL:
			case STB_WEAK:
				return (1);
			default:
				return (0);
			}
		default:
			return (0);
		}
	}

	/*
	 * If -g is specified,
	 *	the symbol has to be global.
	 */
	if (g_flag != 0) {
		switch (sym_data->type) {
		case STT_NOTYPE:
		case STT_OBJECT:
		case STT_FUNC:
			switch (sym_data->bind) {
			case STB_GLOBAL:
			case STB_WEAK:
				return (1);
			default:
				return (0);
			}
		default:
			return (0);
		}
	}

	/*
	 * If it comes here, any symbol can be printed.
	 *	(So basically, -f is no-op.)
	 */
	return (1);
}

#ifndef XPG4
/*
 * -u flag specified
 */
static void
print_with_uflag(Elf *elf_file,
	SYM *sym_data,
	char *filename,
	Elf32_Ehdr *elf_head_p)
{
	if ((sym_data->shndx == SHN_UNDEF) &&
		(strlen(sym_data->name))) {
		if (!r_flag) {
			if (R_flag) {
				if (archive_name != (char *)0)
					(void) printf(
					"   %s:%s:%s\n",
					archive_name,
					filename,
					sym_data->name);
				else
					(void) printf(
					"    %s:%s\n",
					filename,
					sym_data->name);
			}
			else
				(void) printf(
					"    %s\n",
					sym_data->name);
		}
		else
			(void) printf("    %s:%s\n",
				filename,
				sym_data->name);
	}
}
#endif
/*
 * -p flag specified
 */
static void
print_with_pflag(Elf *elf_file,
	SYM *sym_data,
	char *filename,
	Elf32_Ehdr *elf_head_p)
{
	char *sym_key = NULL;

	if (is_sym_print(sym_data) != 1)
		return;
	/*
	 * -A header
	 */
	if (A_flag != 0)
		printf("%s", A_header);

	/*
	 * Symbol Value.
	 *	(hex/octal/decimal)
	 */
	if (x_flag)
		(void) printf("0x%.8lx ", sym_data->value);
	else if (o_flag)
		(void) printf("0%.11lo ", sym_data->value);
	else
		(void) printf("%.10lu ", sym_data->value);

	/*
	 * Symbol Type.
	 */
	if ((sym_data->shndx == SHN_UNDEF) &&
		(strlen(sym_data->name)))
		sym_key = UNDEFINED;
	else if (is_bss_section(sym_data->shndx,
		elf_file,
		elf_head_p)) {
		switch (sym_data->bind) {
			case STB_LOCAL  : sym_key = BSS_LOCL;
					break;
			case STB_GLOBAL : sym_key = BSS_GLOB;
					break;
			case STB_WEAK   : sym_key = BSS_WEAK;
					break;
			default	: sym_key = BSS_GLOB;
					break;
		}

	}
	else
		sym_key = lookup(sym_data->type,
			sym_data->bind);

	if (sym_key != NULL) {
		if (!l_flag)
			(void) printf("%c ", sym_key[0]);
		else
			(void) printf("%-3s", sym_key);
	} else {
		if (!l_flag)
			(void) printf("%-2d", sym_data->type);
		else
			(void) printf("%-3d", sym_data->type);
	}
	if (!r_flag) {
		if (R_flag) {
			if (archive_name != (char *)0)
				(void) printf(
				"%s:%s:%s\n",
				archive_name,
				filename,
				sym_data->name);
			else
				(void) printf(
				"%s:%s\n",
				filename,
				sym_data->name);
		}
		else
			(void) printf("%s\n", sym_data->name);
	}
	else
		(void) printf("%s:%s\n",
			filename,
			sym_data->name);
}

/*
 * -P flag specified
 */
static void
print_with_Pflag(Elf *elf_file,
	SYM *sym_data,
	char *filename,
	Elf32_Ehdr *elf_head_p)
{
	char *sym_key = NULL;
#define	SYM_LEN 10
	char sym_name[SYM_LEN+1];
	int len;

	if (is_sym_print(sym_data) != 1)
		return;
	/*
	 * -A header
	 */
	if (A_flag != 0)
		printf("%s", A_header);

	/*
	 * Symbol name
	 */
	len = strlen(sym_data->name);
	if (len >= SYM_LEN)
		printf("%s ", sym_data->name);
	else {
		sprintf(sym_name, "%-10s", sym_data->name);
		printf("%s ", sym_name);
	}

	/*
	 * Symbol Type.
	 */
	if ((sym_data->shndx == SHN_UNDEF) &&
		(strlen(sym_data->name)))
		sym_key = UNDEFINED;
	else if (is_bss_section(sym_data->shndx,
		elf_file,
		elf_head_p)) {
		switch (sym_data->bind) {
			case STB_LOCAL  : sym_key = BSS_LOCL;
					break;
			case STB_GLOBAL : sym_key = BSS_GLOB;
					break;
			case STB_WEAK   : sym_key = BSS_WEAK;
					break;
			default	: sym_key = BSS_GLOB;
					break;
		}

	} else
		sym_key = lookup(sym_data->type,
			sym_data->bind);

	if (sym_key != NULL) {
		if (!l_flag)
			(void) printf("%c ", sym_key[0]);
		else
			(void) printf("%-3s", sym_key);
	} else {
		if (!l_flag)
			(void) printf("%-2d", sym_data->type);
		else
			(void) printf("%-3d", sym_data->type);
	}

	/*
	 * Symbol Value & size
	 *	(hex/octal/decimal)
	 */
	if (d_flag)
		(void) printf("%.10lu %.10lu ",
			sym_data->value, sym_data->size);
	else if (o_flag)
		(void) printf("%.11lo %.11lo ",
			sym_data->value, sym_data->size);
	else	/* Hex and it is the default */
		(void) printf("%.8lx %.8lx ",
			sym_data->value, sym_data->size);
	putchar('\n');
}

/*
 * other flags specified
 */
static void
print_with_otherflags(Elf *elf_file,
	SYM *sym_data,
	char *filename,
	Elf32_Ehdr *elf_head_p)
{
	Elf32_Shdr *tmp_shdr;

	if (is_sym_print(sym_data) != 1)
		return;
	printf("%s", A_header);
	(void) printf("[%d]\t|", sym_data->indx);
	if (o_flag)
		(void) printf("0%.11lo|0%.11lo|",
			sym_data->value,
			sym_data->size);
	else if (x_flag)
		(void) printf("0x%.8lx|0x%.8lx|",
			sym_data->value,
			sym_data->size);
	else
		(void) printf("%10lu|%8ld|",
			sym_data->value,
			sym_data->size);

	switch (sym_data->type) {
	case STT_NOTYPE:(void) printf("%-5s", "NOTY"); break;
	case STT_OBJECT:(void) printf("%-5s", "OBJT"); break;
	case STT_FUNC:	(void) printf("%-5s", "FUNC"); break;
	case STT_SECTION:(void) printf("%-5s", "SECT"); break;
	case STT_FILE:	(void) printf("%-5s", "FILE"); break;
	default:
		if (o_flag)
			(void) printf("%#-5o", sym_data->type);
		else if (x_flag)
			(void) printf("%#-5x", sym_data->type);
		else
			(void) printf("%-5d", sym_data->type);
	}
	(void) printf("|");
	switch (sym_data->bind) {
	case STB_LOCAL:	(void) printf("%-5s", "LOCL"); break;
	case STB_GLOBAL:(void) printf("%-5s", "GLOB"); break;
	case STB_WEAK:	(void) printf("%-5s", "WEAK"); break;
	default:
		(void) printf("%-5d", sym_data->bind);
		if (o_flag)
			(void) printf("%#-5o", sym_data->bind);
		else if (x_flag)
			(void) printf("%#-5x", sym_data->bind);
		else
			(void) printf("%-5d", sym_data->bind);
	}
	(void) printf("|");
	if (o_flag)
		(void) printf("%#-5o", sym_data->other);
	else if (x_flag)
		(void) printf("%#-5x", sym_data->other);
	else
		(void) printf("%-5d", sym_data->other);
	(void)  printf("|");

	switch (sym_data->shndx) {
		case SHN_UNDEF: if (!s_flag)
					(void) printf("%-7s",
						"UNDEF");
				else
					(void) printf("%-14s",
						"UNDEF");
				break;
		case SHN_ABS:	if (!s_flag)
					(void) printf("%-7s",
						"ABS");
				else
					(void) printf("%-14s",
						"ABS");
				break;
		case SHN_COMMON:if (!s_flag)
					(void) printf("%-7s",
						"COMMON");
				else
					(void) printf("%-14s",
						"COMMON");
				break;
		default:
			if (o_flag && !s_flag)
				(void) printf("%-7d",
					sym_data->shndx);
			else if (x_flag && !s_flag)
				(void) printf("%-7d",
					sym_data->shndx);
			else if (s_flag) {
				tmp_shdr = elf32_getshdr(
					elf_getscn(elf_file,
					sym_data->shndx));
				if (tmp_shdr &&
					tmp_shdr->sh_name)
					(void) printf("%-14s",
				(char *) elf_strptr(elf_file,
				elf_head_p->e_shstrndx,
				tmp_shdr->sh_name));
				else
					(void) printf("%-14d",
					sym_data->shndx);

			}
			else
				(void) printf("%-7d",
					sym_data->shndx);
	}
	(void) printf("|");
	if (!r_flag) {
		if (R_flag) {
			if (archive_name != (char *)0)
				(void) printf("%s:%s:%s\n",
					archive_name,
					filename,
					sym_data->name);
			else
				(void) printf("%s:%s\n",
					filename,
					sym_data->name);
		}
		else
			(void) printf("%s\n", sym_data->name);
	}
	else
		(void) printf("%s:%s\n",
			filename, sym_data->name);
}

/*
 * C++ name demangling supporting routines
 */
static char *ctor_str = "static constructor function for %s";
static char *dtor_str = "static destructor function for %s";
static char *ptbl_str = "pointer to the virtual table vector for %s";
static char *vtbl_str = "virtual table for %s";

/*
 * alloc memory and create name in necessary format.
 * Return name string
 */
static char *
FormatName(OldName, NewName)
char  *OldName;
char  *NewName;
{
	char *s = p_flag ?
		"%s\n             [%s]" :
		"%s\n\t\t\t\t\t\t       [%s]";
	int length = strlen(s)+strlen(NewName)+strlen(OldName)-3;
	char *hold = OldName;
	OldName = (char *)malloc(length);
	sprintf(OldName, s, NewName, hold);
	return (OldName);
}


/*
 * Return 1 when s is an exotic name, 0 otherwise.  s remains unchanged,
 * the exotic name, if exists, is saved in d_buf.
 */
int
exotic(s)
char *s;
{
	int tag = 0;
	if (strncmp(s, "__sti__", 7) == 0)
	{
		s += 7; tag = 1;
		parse_fn_and_print(ctor_str, s);
	} else if (strncmp(s, "__std__", 7) == 0)
	{
		s += 7; tag = 1;
		parse_fn_and_print(dtor_str, s);
	} else if (strncmp(s, "__vtbl__", 8) == 0)
	{
		char *printname;
		s += 8; tag = 1;
		parsename(s);
		sprintf(d_buf, vtbl_str, p_buf);
	} else if (strncmp(s, "__ptbl_vec__", 12) == 0)
	{
		s += 12; tag = 1;
		parse_fn_and_print(ptbl_str, s);
	}
	return (tag);
}

void
parsename(s)
char *s;
{
	register int len;
	char c, *orig = s;
	*p_buf = '\0';
	strcat(p_buf, "class ");
	while (isdigit(*s)) s++;
	c = *s;
	*s = '\0';
	len = atoi(orig);
	*s = c;
	if (*(s+len) == '\0') { /* only one class name */
		strcat(p_buf, s);
		return;
	} else
	{ /* two classname  %drootname__%dchildname */
		char *root, *child, *child_len_p;
		int child_len;
		root = s;
		child = s + len + 2;
		child_len_p = child;
		if (!isdigit(*child))
		{	/* ptbl file name */
			/*  %drootname__%filename */
			/* kludge for getting rid of '_' in file name */
			char *p;
			c = *(root + len);
			*(root + len) = '\0';
			strcat(p_buf, root);
			*(root + len) = c;
			strcat(p_buf, " in ");
			for (p = child; *p != '_'; ++p);
			c = *p;
			*p = '.';
			strcat(p_buf, child);
			*p = c;
			return;
		}

		while (isdigit(*child))
			child++;
		c = *child;
		*child = '\0';
		child_len = atoi(child_len_p);
		*child = c;
		if (*(child + child_len) == '\0')
		{
			strcat(p_buf, child);
			strcat(p_buf, " derived from ");
			c = *(root + len);
			*(root + len) = '\0';
			strcat(p_buf, root);
			*(root + len) = c;
			return;
		} else
		{
			/* %drootname__%dchildname__filename */
			/* kludge for getting rid of '_' in file name */
			char *p;
			c = *(child + child_len);
			*(child + child_len) = '\0';
			strcat(p_buf, child);
			*(child+child_len) = c;
			strcat(p_buf, " derived from ");
			c = *(root + len);
			*(root + len) = '\0';
			strcat(p_buf, root);
			*(root + len) = c;
			strcat(p_buf, " in ");
			for (p = child + child_len + 2; *p != '_'; ++p);
			c = *p;
			*p = '.';
			strcat(p_buf, child + child_len + 2);
			*p = c;
			return;
		}
	}
}

void
parse_fn_and_print(str, s)
char *str, *s;
{
	char		c, *p1, *p2;
	int			sym_len = strlen(s);
	int			yes = 1;
	static char *	buff = 0;
	static int		buf_size;

	/*
	 * We will need to modify the symbol (s) as we are analyzing it,
	 * so copy it into a buffer so that we can play around with it.
	 */
	if (buff == NULL) {
	buff = (char *)malloc(DEF_MAX_SYM_SIZE);
	buf_size = DEF_MAX_SYM_SIZE;
	}

	if (++sym_len > buf_size) {
	if (buff)
		free(buff);
	buff = (char *)malloc(sym_len);
	buf_size = sym_len;
	}

	if (buff == NULL) {
		(void) fprintf(stderr, gettext(
			"%s: cannot malloc space\n"), prog_name);
		exit(NOALLOC);
	}
	s = strcpy(buff, s);

	if ((p1 = p2 =  strstr(s, "_c_")) == NULL)
		if ((p1 = p2 =  strstr(s, "_C_")) == NULL)
			if ((p1 = p2 =  strstr(s, "_cc_")) == NULL)
				if ((p1 = p2 =  strstr(s, "_cxx_")) == NULL)
					if (
					(p1 = p2 = strstr(s, "_h_")) == NULL)
			yes = 0;
			else
						p2 += 2;
				else
					p2 += 4;
			else
				p2 += 3;
		else
			p2 += 2;
	else
		p2 += 2;

	if (yes)
	{
	*p1 = '.';
		c = *p2;
		*p2 = '\0';
	}

	for (s = p1;  *s != '_';  --s);
	++s;

	sprintf(d_buf, str, s);

	if (yes)
	{
	*p1 = '_';
		*p2 = c;
	}
}
