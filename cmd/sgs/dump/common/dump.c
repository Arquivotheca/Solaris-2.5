/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)dump.c	6.21	95/02/13 SMI"

#include <stdio.h>

#include <stdlib.h>
#include <locale.h>
#include <unistd.h>
#include <libelf.h>
#include <link.h>
#include <sys/elf_M32.h>
#include <sys/elf_386.h>
#include <sys/elf_SPARC.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "sgs.h"
#include "dump.h"


#define OPTSTR	"agcd:fhln:oprstvCDLT:V?"	/* option string for getopt() */

static SCNTAB *	p_symtab, * p_head_scns, * p_dynsym;
SCNTAB *	p_debug, * p_line;

static int
	x_flag=0,	/* option requires section header table */
	z_flag=0,	/* process files within an archive */
	rn_flag=0;	/* dump named relocation information */



static int
	/* flags: ?_flag corresponds to ? option */
	a_flag=0,	/* dump archive header of each member of archive */
	g_flag=0,	/* dump archive symbol table */
	c_flag=0,	/* dump the string table */
	d_flag=0,	/* dump range of sections */
	f_flag=0,	/* dump each file header */
	h_flag=0,	/* dump section headers */
	l_flag=0,	/* dump the .line section */
	n_flag=0,	/* dump named section */
	o_flag=0,	/* dump each program execution header */
	r_flag=0,	/* dump relocation information */
	s_flag=0,	/* dump section contents */
	t_flag=0,	/* dump symbol table entries */
	C_flag=0,	/* dump decoded C++ symbol names */
	D_flag=0,	/* dump debugging information */
	L_flag=0,	/* dump dynamic linking information */
	T_flag=0,	/* dump symbol table range */
	V_flag=0;	/* dump version information */

int	p_flag=0,	/* suppress printing of headings */
	v_flag=0;	/* print information in verbose form */

static int
	d_low=0,	/* range for use with -d */
	d_hi=0,
	d_num=0;

static int
	T_low=0,	/* range for use with -T */
	T_hi=0,
	T_num=0;

static	const char *	Fmtrel = "%-18s";
static	const char *	Fmtreld = "%-18d";

static char *	name = NULL;     /* for use with -n option */
char	*prog_name;
static int	errflag=0;

static struct stab_list_s
{
	struct stab_list_s	*next;
	char			*strings;
	int			 size;
} *StringTableList = (void *) 0;

static void usage();
static void each_file();
static Elf32_Ehdr * dump_elf_header();
extern void *	get_scndata();
static void *	 get_rawscn();
static void dump_ar_hdr();
extern void ar_sym_read();
extern void dump_line();
extern void dump_exec_header();
static void dump_section_table();
static void dump_shdr();
static void print_shdr();
static void print_static();
static void dump_section();
static void print_section();
static void print_rawdata();
static void set_range();
static int check_range();
static void dump_ar_files();
static void dump_string_table();
static struct stab_list_s *load_arstring_table();
static void dump_symbol_table();
static void print_symtab();
static void dump_reloc_table();
static void dump_dynamic();
static void print_rela();
static void print_rel();
extern void dump_debug();

static char *demangled_name();


/*
 * Sets up flags for command line options given and then
 * calls each_file() to process each file.
 */

main(argc, argv)
	int argc;
	char *argv[];
{
	char        *optstr = OPTSTR; /* option string used by getopt() */
	extern  int optind;		/* arg list index */
#if 0
 	            opterr;		/* turn off error message */
#endif
	int	    optchar;
	extern char *optarg;        /* current option argument */

	prog_name = argv[0];

	while ( (optchar = getopt(argc, argv, optstr)) != -1)
	{
		switch (optchar) {
		case 'a':	a_flag=1; x_flag=1; break;
		case 'g':	g_flag=1; x_flag=1; break;
		case 'v':	v_flag=1; break;
		case 'p':	p_flag=1; break;
		case 'f':	f_flag=1;z_flag=1; break;
		case 'o':	o_flag=1;z_flag=1; break;
		case 'h':	h_flag=1; x_flag=1;z_flag=1; break;
		case 'l':	l_flag=1; x_flag=1;z_flag=1; break;
		case 's':	s_flag=1; x_flag=1;z_flag=1; break;
		case 'd':	d_flag=1; x_flag=1; z_flag=1;
				set_range(optarg, &d_low, &d_hi);
				break;
		case 'n':	n_flag++; x_flag=1; z_flag=1;
				name=optarg;
				break;
		case 'r':	r_flag=1; x_flag=1;z_flag=1; break;
		case 't':	t_flag=1; x_flag=1;z_flag=1; break;
		case 'C':	C_flag=1;t_flag=1; x_flag=1;z_flag=1; break;
		case 'T':	T_flag=1; x_flag=1;z_flag=1;
				set_range(optarg, &T_low, &T_hi);
				break;
		case 'c':	c_flag=1; x_flag=1;z_flag=1; break;
		case 'L':	L_flag=1; x_flag=1;z_flag=1; break;
		case 'D':	D_flag=1; x_flag=1;z_flag=1; break;
		case 'V':	V_flag=1;
				(void) fprintf(stderr, "dump: %s %s\n",
				    (const char *)SGU_PKG,
				    (const char *)SGU_REL);
				break;
		case '?':	errflag +=1;break;
		default:		  break;
		}
	}

	if ( errflag || (optind >= argc) || (!z_flag && !x_flag) )
	{
		if(! (V_flag && (argc==2) ) )
		{
			usage();
			exit(269);
		}
	}

	while (optind < argc) {
		each_file(argv[optind]);
		optind++;
	}
	return 0;
}



/*
 * Print out a usage message in short form when program is invoked
 * with insufficient or no arguements, and in long form when given
 * either a ? or an invalid option.
 */

static void
usage()
{
	(void) fprintf(stderr,
	"Usage: %s [-%s] file(s) ...\n", prog_name, OPTSTR);
	if (errflag)
	{
		(void) fprintf(stderr,
		"\t\t[-a dump archive header of each member of archive]\n\
		[-g dump archive global symbol table]\n\
		[-c dump the string table]\n\
		[-d dump range of sections]\n\
		[-f dump each file header]\n\
		[-h dump section headers]\n\
		[-l dump line number information]\n\
		[-n dump named section]\n\
		[-o dump each program execution header]\n\
		[-p suppress printing of headings]\n\
		[-r dump relocation information]\n\
		[-s dump section contents]\n\
		[-t dump symbol table entries]\n\
		[-v print information in verbose form]\n\
		[-C dump decoded C++ symbol names]\n\
		[-D dump debugging information]\n\
		[-L dump the .dynamic structure]\n\
		[-T dump symbol table range]\n\
		[-V dump version information]\n");
	}
}



/*
 * Takes a filename as input.  Test first for a valid version
 * of libelf.a and exit on error.  Process each valid file
 * or archive given as input on the command line.  Check
 * for file type.  If it is an archive, process the archive-
 * specific options first, then files within the archive.
 * If it is an ELF or COFF object file, process it; otherwise
 * warn that it is an invalid file type.
 * All options except the archive-specific and program
 * execution header are processed in the function, dump_section_table.
 */

static void
each_file(char * filename)
{
	Elf        *elf_file;
	Elf32_Ehdr *elf_head_p;
	int        fd;
	Elf_Kind   file_type;

	struct stat buf;

	Elf_Cmd cmd;
	errno = 0;
	if (stat(filename,&buf) == -1) {
		fprintf(stderr,"%s: ",prog_name);
		perror(filename);
		return;
	}
	if(elf_version(EV_CURRENT) == EV_NONE)
	{
		(void)fprintf(stderr,"%s: Libelf is out of date\n", prog_name);
		exit(101);
	}

	if ((fd = open((filename), O_RDONLY)) == -1)
	{
		(void)fprintf(stderr, "%s: %s: cannot read\n", 
			prog_name, filename);
		return;
	}
	cmd = ELF_C_READ;
	if ( (elf_file = elf_begin(fd, cmd, (Elf *) 0)) == NULL )
	{
	/*	(void)fprintf(stderr,"%s: %s: problem opening file\n",
			prog_name, filename);*/
                (void)fprintf(stderr, "%s: %s: %s\n", prog_name, filename, elf_errmsg(-1));
		return;
	}
	file_type = elf_kind(elf_file);
	if(file_type == ELF_K_AR)
	{
		if( a_flag || g_flag)
			{
			dump_ar_hdr(fd, elf_file, filename);
			(elf_file = elf_begin(fd, cmd, (Elf *) 0)); 
			}
		if(z_flag)
			dump_ar_files(fd, elf_file, filename);	
	}
	else
	{
		if ( (file_type == ELF_K_ELF) || (file_type == ELF_K_COFF) )
		{
			(void)printf("\n%s:\n", filename);
			if (file_type == ELF_K_COFF)
                        {
                                fprintf(stderr, "%s: %s: warning - internal conversion of COFF file to ELF\n",prog_name,filename);

                        }

			elf_head_p = dump_elf_header(elf_file, filename);
			if (elf_head_p == (Elf32_Ehdr *)0)
				{
					elf_end(elf_file);
					close(fd);
					return;
				}
			if (o_flag)
				dump_exec_header(elf_file,elf_head_p->e_phnum, filename);
			if(x_flag)
				dump_section_table(elf_file, elf_head_p, filename);
		}
		else
		{
			(void)fprintf(stderr, "%s: %s: invalid file type\n",
				prog_name, filename);
			elf_end(elf_file);
			close(fd);
			return;
		}
	}
	elf_end(elf_file);
	close(fd);
}



/*
 * Print the ELF header.  Input is an ELF file descriptor
 * and the filename.  If f_flag is set, the ELF header is
 * printed to stdout, otherwise the function returns after
 * setting the pointer to the ELF header.  Any values which
 * are not known are printed in decimal.  Fields must be updated
 * as new values are added.
 */

static Elf32_Ehdr *
dump_elf_header(elf_file, filename)
Elf *elf_file;
char     *filename;
{
	Elf32_Ehdr  *elf_head_p;
	int         class, data;

	if ( (elf_head_p = elf32_getehdr(elf_file)) == NULL) {
		/*(void)fprintf(stderr,"%s: %s: problem opening ELF header\n",
			prog_name, filename);*/
                (void)fprintf(stderr, "%s: %s: %s\n", prog_name, filename, elf_errmsg(-1));
		return NULL;
	}

	if (!f_flag)
		return elf_head_p;

	if (!p_flag)
	{
	(void)printf("\n                    **** ELF HEADER ****\n");
	(void)printf("Class        Data       Type         Machine     Version\n");
	(void)printf("Entry        Phoff      Shoff        Flags       Ehsize\n");
	(void)printf("Phentsize    Phnum      Shentsz      Shnum       Shstrndx\n\n");
	}
	if (!v_flag)
	{
		(void)printf("%-13d%-11d%-13ld%-12d%d\n",
			elf_head_p->e_ident[4],
			elf_head_p->e_ident[5],
			elf_head_p->e_type,
			elf_head_p->e_machine,
		 	elf_head_p->e_version);
	}
	else
	{
		class = elf_head_p->e_ident[4];
		data = elf_head_p->e_ident[5];

		switch (class)
		{
			case ELFCLASSNONE: (void)printf("%-13s", "None"); break;
			case ELFCLASS32: (void)printf("%-13s", "32-bit"); break;
			case ELFCLASS64: (void)printf("%-13s", "64-bit"); break;
			default:
				(void)printf("%-13d", class); break;
		}
		switch (data)
		{
			case ELFDATANONE: (void)printf("%-11s", "None   "); break;
			case ELFDATA2LSB: (void)printf("%-11s", "2LSB   "); break;
			case ELFDATA2MSB: (void)printf("%-11s", "2MSB   "); break;
			default:
				(void)printf("%-11d", data); break;
		}

		switch (elf_head_p->e_type)
		{
			case ET_NONE: (void)printf("%-13s", "None");break;
			case ET_REL: (void)printf("%-13s", "Reloc");break;
			case ET_EXEC: (void)printf("%-13s", "Exec");break;
			case ET_DYN: (void)printf("%-13s", "Dyn");break;
			case ET_CORE: (void)printf("%-13s", "Core");break;
			default:
				(void)printf("%-13d", elf_head_p->e_type);break;
		}
		switch (elf_head_p->e_machine)
		{
			case EM_NONE: (void)printf("%-12s", "No mach");break;
			/* case EM_ALL: (void)printf("%-12s", "All");break; */
			case EM_M32: (void)printf("%-12s", "WE32100");break;
			case EM_SPARC: (void)printf("%-12s", "SPARC");break;
			case EM_386: (void)printf("%-12s", "80386");break;
			case EM_68K: (void)printf("%-12s", "68000");break;
			case EM_88K: (void)printf("%-12s", "88000");break;
			case EM_486: (void)printf("%-12s", "80486");break;
			case EM_860: (void)printf("%-12s", "i860");break;
			case EM_MIPS: (void)printf("%-12s", "RS3000_BE");break;
			case EM_MIPS_RS3_LE: (void)printf("%-12s", "RS3000_LE");break;
			case EM_RS6000: (void)printf("%-12s", "RS6000");break;
			case EM_PA_RISC: (void)printf("%-12s", "PA_RISC");break;
			case EM_nCUBE: (void)printf("%-12s", "nCUBE");break;
			case EM_VPP500: (void)printf("%-12s", "VPP500");break;
			case EM_SPARC32PLUS: (void)printf("%-12s", "SPARC32PLUS");break;
			default:
				(void)printf("%-12d", elf_head_p->e_machine);
		}
		switch (elf_head_p->e_version)
		{
			case EV_NONE: (void)printf("Invalid\n");break;
			case EV_CURRENT: (void)printf("Current\n");break;
			default:
				(void)printf("%d\n", elf_head_p->e_version);
		}
	}
	(void)printf("%-#13x%-#11x%-#13x%-#12x%#x\n",
		elf_head_p->e_entry,
		elf_head_p->e_phoff,
		elf_head_p->e_shoff,
		elf_head_p->e_flags,
		elf_head_p->e_ehsize);
	(void)printf("%-#13x%-11u%-#13x%-12u%u\n\n",
		elf_head_p->e_phentsize,
		elf_head_p->e_phnum,
		elf_head_p->e_shentsize,
		elf_head_p->e_shnum,
		elf_head_p->e_shstrndx);

	return (Elf32_Ehdr *)elf_head_p;
}


/*
 * Process all of the command line options (except
 * for -a, -g, -f, and -o).  All of the options processed
 * by this function require the presence of the section
 * header table and will not be processed if it is not present.
 * Set up a buffer containing section name, section header,
 * and section descriptor for each section in the file.  This
 * structure is used to avoid duplicate calls to libelf functions.
 * Structure members for the symbol table, the debugging information,
 * and the line number information are global.  All of the
 * rest are local.
 */

static void
dump_section_table(elf_file, elf_head_p, filename)
Elf        *elf_file;
Elf32_Ehdr *elf_head_p;
char       *filename;
{

	static SCNTAB *buffer, *p_scns;

	Elf32_Shdr    *p_shdr;
	Elf_Scn       *scn = 0;
	char   *s_name = NULL;
	int           found = 0;
	static int    num_scns;


	num_scns = elf_head_p->e_shnum - 1;
	if ((buffer = (SCNTAB *)calloc (num_scns+1, sizeof(SCNTAB))) == NULL)
	{
		(void) fprintf(stderr, "%s: %s: cannot calloc space\n",
			prog_name, filename);
		return;
	}
	p_symtab = (SCNTAB *)0;
	p_debug = (SCNTAB *)0;
	p_line = (SCNTAB *)0;
	p_dynsym = (SCNTAB *)0;
	p_scns = buffer;
	p_head_scns = buffer;
	
	while ( (scn = elf_nextscn(elf_file, scn) ) != 0)
	{
		if( (p_shdr = elf32_getshdr(scn) ) == 0)
		{
			/*(void)fprintf(stderr, "%s: %s: cannot get section header\n",
			prog_name, filename);*/
                (void)fprintf(stderr, "%s: %s: %s\n", prog_name, filename, elf_errmsg(-1));
			return;
		}
		s_name = (char *)elf_strptr(elf_file, elf_head_p->e_shstrndx, p_shdr->sh_name);
		buffer->scn_name = s_name;
		buffer->p_shdr   = p_shdr;
		buffer->p_sd   =  scn;

		if (p_shdr->sh_type == SHT_SYMTAB)
		{
			found += 1;
			p_symtab = buffer;
		}
		if (strcmp(s_name, ".debug") == 0)
			p_debug = buffer;
		if (strcmp(s_name, ".line") == 0)
			p_line = buffer;
		if (p_shdr->sh_type == SHT_DYNSYM)
			p_dynsym = buffer;	
		
		buffer++;

	}

/* These functions depend upon the presence of the section header table
 * and will not be invoked in its absence
 */

	if(h_flag)
	{
		dump_shdr(p_scns, num_scns, filename);
	}
	if (p_symtab && (t_flag || T_flag) )
	{
		dump_symbol_table(elf_file, p_symtab, filename);
	}
	if (c_flag)
	{
		dump_string_table(p_scns, num_scns);
	}
	if (r_flag)
	{
		dump_reloc_table(elf_file, elf_head_p, p_scns, num_scns, filename);
	}
	if(L_flag)
	{
		dump_dynamic(elf_file, p_scns, num_scns, filename);
	}
	if(p_debug && D_flag)
	{
		dump_debug(filename);
	}
	if(p_line && l_flag)
	{
		dump_line(filename);
	}
	if (s_flag)
	{
		dump_section(elf_file, elf_head_p, p_scns, num_scns, filename);
	}
}


/*
 * Print the section header table. This function does not print the contents
 * of the section headers but sets up the parameters and then calls
 * print_shdr to print the contents.  Calling another function to print
 * the contents allows both -d and -n to work correctly
 * simultaneously.  Input is the SCNTAB structure,
 * the number of sections from the ELF header, and the filename.
 * Set the range of section headers to print if d_flag, and set
 * name of section header to print if n_flag.
 */

static void
dump_shdr(s, num_scns, filename)
SCNTAB *s;
int num_scns;
char *filename;
{

	SCNTAB *n_range, *d_range;	/* for use with -n and -d modifiers */

	int     i;
	int     found_it = 0;  /* for use with -n section_name */

	if (!p_flag) {
		(void)printf("\n	   **** SECTION HEADER TABLE ****\n");
		(void)printf("[No]\tType\tFlags\tAddr         Offset       Size        	Name\n");
		(void)printf("\tLink\tInfo\tAdralgn      Entsize\n\n");
	}

	if (n_flag)
	{
		n_range = s;

		for(i = 1; i <= num_scns; i++,n_range++)
		{
			if( ( strcmp(name, n_range->scn_name)) != 0)
				continue;
			else
			{
				found_it = 1;
				print_shdr(n_range, 1, i);
			}
		}

		if(!found_it)
		{
			(void)fprintf(stderr, "%s: %s: %s not found\n",
				prog_name, filename, name);
		}
	} /* end n_flag */

	if(d_flag)
	{
		d_range = s;
		d_num = check_range(d_low, d_hi, num_scns, filename);
		if(d_num < 0)
			return;
		d_range += d_low - 1;

		print_shdr(d_range, d_num, d_low);
	}	/* end d_flag */

	if(!n_flag && !d_flag)
		print_shdr(s, num_scns, 1);

}


/*
 * Print the section header table.  Input is the SCNTAB structure,
 * the number of sections, an index which is the number of the
 * section in the file, and the filename.  The values of the SCNTAB
 * structure, the number of sections, and the index are set in
 * dump_shdr depending on whether the -n or -d modifiers were set.
 */

static void
print_shdr(SCNTAB * s, int num_scns, int index)
{
	SCNTAB *p;
	int num;

	p = s;

	for(num=0; num<num_scns; num++, p++)
	{
		(void)printf("[%d]\t", index++);
		if(!v_flag) 
		{
			(void)printf("%lu\t%lu\t",
			p->p_shdr->sh_type,
			p->p_shdr->sh_flags);
		}
		else
		{
			switch ((Elf32_Word)p->p_shdr->sh_type)
			{
				case SHT_NULL: (void)printf("NULL"); break;
				case SHT_PROGBITS: (void)printf("PBIT"); break;
				case SHT_SYMTAB: (void)printf("SYMT"); break;
				case SHT_STRTAB: (void)printf("STRT"); break;
				case SHT_RELA: (void)printf("RELA"); break;
				case SHT_HASH: (void)printf("HASH"); break;
				case SHT_DYNAMIC: (void)printf("DYNM"); break;
				case SHT_NOTE: (void)printf("NOTE"); break;
				case SHT_NOBITS: (void)printf("NOBI"); break;
				case SHT_REL: (void)printf("REL "); break;
				case SHT_DYNSYM: (void)printf("DYNS"); break;
				case ((Elf32_Word)SHT_LOUSER):
					(void)printf("LUSR"); break;
				case ((Elf32_Word)SHT_HIUSER):
					(void)printf("HUSR"); break;
				case SHT_SHLIB: (void)printf("SHLB"); break;
				case SHT_SUNW_verdef:
					(void)printf("VERD"); break;
				case SHT_SUNW_verneed:
					(void)printf("VERN"); break;
				case SHT_SUNW_versym:
					(void)printf("VERS"); break;
				default:
					(void)printf("%lu", p->p_shdr->sh_type);break;
			}
			(void)printf("    ");
		
			if (p->p_shdr->sh_flags & SHF_WRITE)
				(void)printf("W");
			else
				(void)printf("-");
			if (p->p_shdr->sh_flags & SHF_ALLOC)
				(void)printf("A");
			else
				(void)printf("-");
			if (p->p_shdr->sh_flags & SHF_EXECINSTR)
				(void)printf("I");
			else
				(void)printf("-");
			(void)printf("\t");
			
		}
		(void)printf("%-#13x%-#13x%-#13lx\t%s\n",
			p->p_shdr->sh_addr,
			p->p_shdr->sh_offset,
			p->p_shdr->sh_size,
			p->scn_name);

		(void)printf("\t%lu\t%lu\t%-#13x%-#13lx\n\n",
			p->p_shdr->sh_link,
			p->p_shdr->sh_info,
			p->p_shdr->sh_addralign,
			p->p_shdr->sh_entsize);
	}
}

/*
 * Get the section descriptor and set the size of the
 * data returned.  Data is byte-order converted.
 */

void *
get_scndata(fd_scn, size)
Elf_Scn *fd_scn;
size_t    *size;
{
	Elf_Data *p_data;

	p_data = 0;
	if( (p_data = elf_getdata(fd_scn, p_data)) == 0 ||
		p_data->d_size == 0)
	{
		return NULL;
	}

	*size = p_data->d_size;
	return( p_data->d_buf );
}


/*
 * Get the section descriptor and set the size of the
 * data returned.  Data is raw (i.e., not byte-order converted).
 */

static void *
get_rawscn(fd_scn, size)
Elf_Scn *fd_scn;
size_t    *size;
{
	Elf_Data *p_data;

	p_data = 0;
	if( (p_data = elf_rawdata(fd_scn, p_data)) == 0 ||
		p_data->d_size == 0)
	{
		return NULL;
	}

	*size = p_data->d_size;
	return( p_data->d_buf );
}

/*
 * Print out the string tables.  Input is an opened ELF file,
 * the SCNTAB structure, the number of sections, and the filename.
 * Since there can be more than one string table, all sections are
 * examined and any with the correct type are printed out.
 */

static void
dump_string_table(SCNTAB * s, int num_scns)
{
	size_t section_size;
	unsigned char *strtab;
	int beg_of_string;
	int counter = 0;
	int str_off;
	int i;

	if(!p_flag)
	{
		(void)printf("\n     **** STRING TABLE INFORMATION ****\n");
	}

	for(i=0; i<num_scns; i++, s++)
	{
		if(s->p_shdr->sh_type != SHT_STRTAB)
			continue;

		str_off = 0;

		if(!p_flag)
		{
			(void)printf("\n%s:\n", s->scn_name);
			(void)printf("   <offset>  \tName\n");
		}
		section_size = 0;
		if ((strtab = (unsigned char *)get_scndata(s->p_sd, &section_size)) == NULL)
		{
			continue;
		}

		if (section_size != 0) {
			(void)printf("   <%d>  \t", str_off);
			beg_of_string = 0;
			while (section_size--) {
				unsigned char c = *strtab++;

				if(beg_of_string) {
					(void)printf("   <%d>  \t",str_off);
					counter++;
					beg_of_string = 0;
				}
				str_off++;
				switch(c) {
				case '\0':	(void)printf("\n");
						beg_of_string = 1;break;
				default:	putchar(c);
				}
			}
		}
	}
	(void)printf("\n");
}

 
/*
 * Load the archive string table(s) (for extended-length strings)
 * into an in-core table/list
 */

static struct stab_list_s *
load_arstring_table(STabList, fd, elf_file, p_ar, filename)
  struct 
    stab_list_s *STabList;
  int   	 fd;
  Elf    	*elf_file;
  Elf_Arhdr	*p_ar;
  char 		*filename;
{
int		 here;
struct
    stab_list_s *STL_entry, *STL_next;


if (p_ar)
    {
    STL_entry = (struct stab_list_s *) malloc (sizeof(struct stab_list_s));
    STL_entry->next    = 0;
    STL_entry->strings = 0;
    STL_entry->size    = 0;

    if ( ! STabList )
	STabList = STL_entry;
      else
	{
	STL_next = STabList;
	while (STL_next->next != (void *) 0)
	    STL_next = STL_next->next;
	STL_next->next = STL_entry;
	}

    STL_entry->size    = p_ar->ar_size;
    STL_entry->strings = (char *) malloc(p_ar->ar_size);

    here = elf_getbase(elf_file);
    if ( (lseek(fd, here, 0) ) != here)
	{
	(void)fprintf(stderr, "%s: %s: could not lseek\n", prog_name, filename);
	
	}

    if ( ( read(fd, STL_entry->strings, p_ar->ar_size)) == -1)
	{
	(void)fprintf(stderr, "%s: %s: could not read\n", prog_name, filename);
	}
    }

return (STabList);
}


/*
 * Print the symbol table.  This function does not print the contents
 * of the symbol table but sets up the parameters and then calls
 * print_symtab to print the symbols.  Calling another function to print
 * the symbols allows both -T and -n to work correctly
 * simultaneously.  Input is an opened ELF file, a pointer to the
 * symbol table SCNTAB structure, and the filename.
 * Set the range of symbols to print if T_flag, and set
 * name of symbol to print if n_flag.
 */

static void
dump_symbol_table(elf_file, p_symtab, filename)
Elf     *elf_file;
SCNTAB  *p_symtab;
char    *filename;
{
	Elf32_Sym  *sym_data;
	Elf32_Sym  *T_range, *n_range;	/* for use with -T and -n */
	int        count = 0;
	size_t	   sym_size;
	int        index = 1;
	int        found_it = 0;
	int        i;

	if(p_symtab == NULL)
	{
		(void)fprintf(stderr, "%s: %s: could not get symbol table\n", prog_name, filename);
		return;
	}

	/* get symbol table data */
	sym_data = NULL;
	sym_size = 0;
	if ( (sym_data = (Elf32_Sym *)get_scndata(p_symtab->p_sd, &sym_size) ) == NULL )
	{
		(void)printf("\n%s:\n", p_symtab->scn_name);
		(void)printf("No symbol table data\n");
		return;
	}

	count = sym_size/sizeof(Elf32_Sym);

	sym_data++;	/* first member holds the number of symbols */

	if (n_flag && t_flag && !T_flag)
	{
		n_range = sym_data;
		for(i=1; i<count; i++, n_range++)
		{
			if(strcmp(name, (char *)elf_strptr(elf_file, p_symtab->p_shdr->sh_link, n_range->st_name) ) != 0)
			{
				continue;
			}
			else
			{
				found_it = 1;
			if (!p_flag)
			{
				(void)printf("\n              ***** SYMBOL TABLE INFORMATION *****\n");
				(void)printf("[Index]  Value       Size     Type	Bind	Other	Shndx	Name");
			}
			(void)printf("\n%s:\n", p_symtab->scn_name);
			print_symtab(elf_file, p_symtab, n_range, 1, i);
			}
		}   /* end for */
		if (!found_it)
		{
			(void)fprintf(stderr, "%s: %s: %s not found\n",
				prog_name, filename, name);
		}
	}

	else if(T_flag)
	{

		T_num = check_range(T_low, T_hi, count, filename);
		if(T_num < 0)
			return;
		T_range = sym_data;
		T_range += T_low-1;
		index = T_low;

		if (!p_flag)
		{
			(void)printf("\n              ***** SYMBOL TABLE INFORMATION *****\n");
			(void)printf("[Index]  Value       Size     Type	Bind	Other	Shndx	Name");
		}
		(void)printf("\n%s:\n", p_symtab->scn_name);
		print_symtab(elf_file, p_symtab, T_range, T_num, index);
	}

	else {
		if (!p_flag)
		{
			(void)printf("\n              ***** SYMBOL TABLE INFORMATION *****\n");
			(void)printf("[Index]  Value       Size     Type	Bind	Other	Shndx	Name");
		}
		(void)printf("\n%s:\n", p_symtab->scn_name);
		print_symtab(elf_file, p_symtab, sym_data, count-1, 1);
	}

}


/*
 * Print the symbol table.  Input is an ELF file descriptor, a
 * pointer to the symbol table SCNTAB structure,
 * the number of symbols, a range of symbols to print,
 * an index which is the number of the
 * section in the file, and the filename.  The number of sections,
 * the range, and the index are set in
 * dump_symbol_table, depending on whether -n or -T were set.
 */

static void
print_symtab(Elf * elf_file, SCNTAB * p_symtab, Elf32_Sym * sym_data,
	int range, int index)
{
	while (range > 0)
	{
		char *sym_name = (char *)0;
		int	type, bind;

		type = ELF32_ST_TYPE(sym_data->st_info);
		bind = ELF32_ST_BIND(sym_data->st_info);


		(void)printf("[%d]\t ", index++);
		(void)printf("0x%-10lx", sym_data->st_value);
		(void)printf("%-9d", sym_data->st_size);

		if(!v_flag)
		{
			(void)printf("%d\t\t%d\t%d\t%#x\t", type, bind, sym_data->st_other, sym_data->st_shndx);
		}
		else
		{
			switch (type) {
			case STT_NOTYPE:	(void)printf("%s\t", "NOTY");break;
			case STT_OBJECT:	(void)printf("%s\t", "OBJT");break;
			case STT_FUNC:		(void)printf("%s\t", "FUNC");break;
			case STT_SECTION:	(void)printf("%s\t", "SECT");break;
			case STT_FILE:		(void)printf("%s\t", "FILE");break;
			default:	(void)printf("%d\t",type);
			}
			switch (bind) {
			case STB_LOCAL:	(void)printf("LOCL");break;
			case STB_GLOBAL:(void)printf("GLOB");break;
			case STB_WEAK:	(void)printf("WEAK");break;
			default:(void)printf("%d",bind);
			}
			(void)printf("\t  %d\t", sym_data->st_other);

			switch (sym_data->st_shndx) {
				case SHN_UNDEF: (void)printf("UNDEF"); break;
				case SHN_ABS:	(void)printf("ABS"); break;
				case SHN_COMMON: (void)printf("COMMON"); break;
				default:	(void)printf("%d", sym_data->st_shndx);
			 }
			(void)printf("\t");
		}
		
		/* support machines where NULL-deref causes core dump */
		if (sym_data->st_name == 0)
			sym_name = "";
		else
		if(C_flag)
			sym_name = demangled_name((char *)elf_strptr(elf_file, p_symtab->p_shdr->sh_link, sym_data->st_name));
		else
			sym_name = (char *)elf_strptr(elf_file, p_symtab->p_shdr->sh_link, sym_data->st_name);
		(void)printf("%s\n", sym_name);

		sym_data++;
		range--;
	}	/* end while */
}


/*
 * Print the archive header for each member of an archive.
 * Also call ar_sym_read to print the symbols in the
 * archive symbol table if g_flag.  Input is a file descriptor,
 * an ELF file descriptor, and the filename.  Putting the call
 * to dump the archive symbol table in this function is more
 * efficient since it is necessary to examine the archive member
 * name in the archive header to determin which member is the
 * symbol table.
 */

static void
dump_ar_hdr(int fd, Elf * elf_file, char * filename)
{
	extern int v_flag, g_flag, a_flag, p_flag;
	Elf_Arhdr  *p_ar;
	Elf        *arf;
	Elf_Cmd    cmd;
	int title=0;
	int err=0;

        char   buf[DATESIZE];

	cmd = ELF_C_READ;
	while((arf = elf_begin(fd, cmd, elf_file)) != 0)
	{
		p_ar = elf_getarhdr(arf);
		if(p_ar == NULL)
		{
			/*(void) fprintf(stderr, "%s: %s: cannot read archive header\n",
				prog_name, filename);*/
                (void)fprintf(stderr, "%s: %s: %s\n", prog_name, filename, elf_errmsg(-1));
			continue;
		}
		if( strcmp(p_ar->ar_name, "/") == 0)
		{
			if (g_flag)
				ar_sym_read(fd, arf, p_ar, filename);
		}
		else
			if (strcmp(p_ar->ar_name, "//") == 0)
			{
	                	StringTableList = load_arstring_table(
						StringTableList, fd, arf, p_ar,
						filename);
				cmd = elf_next(arf);
				elf_end(arf);
				continue;
			}

		else
		{
			if (a_flag)
			{
				(void)printf("%s[%s]:\n", filename, p_ar->ar_name);
                    		if(!p_flag && title==0)
                    		{
					if(!v_flag)
                        		printf("\n\n\t\t\t***ARCHIVE HEADER***\n	Date          Uid     Gid    Mode      Size	 Member Name\n\n");
					else
                        		printf("\n\n\t\t\t***ARCHIVE HEADER***\n	Date                   Uid    Gid   Mode     Size     Member Name\n\n");
                        		title=1;
                    		}

					if (!v_flag)
					{
		(void) printf("\t0x%.8lx  %6d  %6d  0%.6ho  0x%.8lx  %-s\n\n",
				p_ar->ar_date, 
				p_ar->ar_uid, 
				p_ar->ar_gid, 
				p_ar->ar_mode, 
				p_ar->ar_size,
				p_ar->ar_name); 
					}

					else
					{


	(void)setlocale(LC_TIME,"");
if ( (strftime(buf,DATESIZE,"%b %d %H:%M:%S %Y", localtime( &(p_ar->ar_date) )) ) == 0 )
                                {
                                    (void) fprintf(stderr, "%s: %s: don't have enough space to store the date\n", prog_name, filename);
                                        exit(1);
                                }
(void) printf("\t%s %6d %6d 0%.6ho 0x%.8lx %-s\n\n", 
		buf, 
		p_ar->ar_uid, 
		p_ar->ar_gid, 
		p_ar->ar_mode, 
		p_ar->ar_size,
		p_ar->ar_name); 

				}
			}
		}
		cmd = elf_next(arf);
		elf_end(arf);
	} /* end while */

        err = elf_errno();
        if ( err != 0)
        {
                (void) fprintf(stderr,"%s: %s: %s\n", prog_name, filename, elf_errmsg(err) );
        }

}



/*
 * Process member files of an archive.  This function provides
 * a loop through an archive equivalent the processing of
 * each_file for individual object files.
 */

static void
dump_ar_files(fd, elf_file, filename)
int fd;
Elf *elf_file;
char *filename;
{
	Elf_Arhdr  *p_ar;
	Elf        *arf;
	Elf_Cmd    cmd;
	Elf_Kind   file_type;
	Elf32_Ehdr *elf_head_p;
	char * fullname;


	cmd = ELF_C_READ;
	while((arf = elf_begin(fd, cmd, elf_file)) != 0)
	{
		p_ar = elf_getarhdr(arf);
		if(p_ar == NULL)
		{
			/*(void) fprintf(stderr, "%s: %s: cannot read archive header\n",
				prog_name, filename);*/
                (void)fprintf(stderr, "%s: %s: %s\n", prog_name, filename, elf_errmsg(-1));
			return;
		}
		if  ((  strcmp(p_ar->ar_name, "/") == 0) ||
		     (	strcmp(p_ar->ar_name, "//") == 0)   )
		{
			cmd = elf_next(arf);
			elf_end(arf);
			continue;
		}

		fullname = malloc(strlen(filename)+strlen(p_ar->ar_name)+3);
		(void) sprintf(fullname,"%s[%s]",filename,p_ar->ar_name);
		(void)printf("\n%s:\n", fullname);
		file_type = elf_kind(arf);
		if ( (file_type == ELF_K_ELF) || (file_type == ELF_K_COFF) )
		{
                        if (file_type == ELF_K_COFF)
                        {
                                fprintf(stderr, "%s: %s: warning - internal conversion of COFF file to ELF\n",prog_name, fullname);

                        }

			elf_head_p = dump_elf_header(arf, fullname);
			if (elf_head_p == (Elf32_Ehdr *)0)
				return;
			if (o_flag)
				dump_exec_header(arf,elf_head_p->e_phnum, fullname);
			if(x_flag)
				dump_section_table(arf, elf_head_p, fullname);
		}
		else
		{
			(void)fprintf(stderr, "%s: %s: invalid file type\n",
				prog_name, fullname);
			cmd = elf_next(arf);
			elf_end(arf);
			continue;
		}

		cmd = elf_next(arf);
		elf_end(arf);
	} /* end while */

}


/*
 * Print section contents. This function does not print the contents
 * of the sectionsbut sets up the parameters and then calls
 * print_section to print the contents.  Calling another function to print
 * the contents allows both -d and -n to work correctly
 * simultaneously. Input is an ELF file descriptor, the ELF header,
 * the SCNTAB structure, the number of sections, and the filename.
 * Set the range of sections if d_flag, and set section name if
 * n_flag.
 */

static void
dump_section(elf_file, p_ehdr, s, num_scns, filename)
Elf * elf_file;
Elf32_Ehdr *p_ehdr;
SCNTAB * s;
int num_scns;
char *filename;
{

	SCNTAB *n_range, *d_range;	/* for use with -n and -d modifiers */

	int     i;
	int     found_it = 0;  /* for use with -n section_name */

	if (n_flag)
	{
		n_range = s;

		for(i = 0; i < num_scns; i++,n_range++)
		{
			if( ( strcmp(name, n_range->scn_name)) != 0)
				continue;
			else
			{
				found_it = 1;
				print_section(elf_file, p_ehdr, n_range, 1, filename);
			}
		}

		if(!found_it)
		{
			(void)fprintf(stderr, "%s: %s: %s not found\n",
				prog_name, filename, name);
		}
	} /* end n_flag */

	if(d_flag)
	{
		d_range = s;
		d_num = check_range(d_low, d_hi, num_scns, filename);
		if(d_num < 0)
			return;
		d_range += d_low - 1;

		print_section(elf_file, p_ehdr, d_range, d_num, filename);
	}	/* end d_flag */

	if(!n_flag && !d_flag)
		print_section(elf_file, p_ehdr, s, num_scns, filename);

}


/*
 * Print section contents.  Input is an ELF file descriptor,
 * the ELF header, the SCNTAB structure,
 * the number of symbols, and the filename.
 * The number of sections,
 * and the offset into the SCNTAB structure will be
 * set in dump_section if d_flag or n_flag are set.
 * If v_flag is set, sections which can be interpreted will
 * be interpreted, otherwise raw data will be output in hexidecimal.
 */

static void
print_section(elf_file, p_ehdr, p, num_scns, filename)
Elf * elf_file;
Elf32_Ehdr *p_ehdr;
SCNTAB *p;
int num_scns;
char * filename;
{
	unsigned char    *p_sec;
	int	i;
	size_t	size;

#if 0
	if(filename != NULL)
		(void)printf("%s:\n", filename);
#endif

	for (i = 0; i < num_scns; i++,p++)
	{

		size = 0;
		if(s_flag && !v_flag)
			p_sec = (unsigned char *)get_rawscn(p->p_sd, &size);
		else
			p_sec = (unsigned char *)get_scndata(p->p_sd, &size);

		/*
		if (p_sec == (unsigned char *)0)
		{
			(void)fprintf(stderr, "%s: %s: no data in %s section\n", prog_name, filename, p->scn_name);
			continue;
		}
		*/

		if( (p->p_shdr->sh_type) == SHT_NOBITS)
		{
			continue;
		}
		if(s_flag && !v_flag)
		{
			(void)printf("\n%s:\n", p->scn_name);
			print_rawdata(p_sec, size);
			continue;
		}
		if( (p->p_shdr->sh_type) == SHT_SYMTAB)
		{
			dump_symbol_table(elf_file, p, filename);
			continue;
		}
		if( (p->p_shdr->sh_type) == SHT_DYNSYM)
		{
			dump_symbol_table(elf_file, p, filename);
			continue;
		}
		if( (p->p_shdr->sh_type) == SHT_STRTAB)
		{
			dump_string_table(p, 1);
			continue;
		}
		if( (p->p_shdr->sh_type) == SHT_RELA)
		{
			dump_reloc_table(elf_file, p_ehdr, p, 1, filename);
			continue;
		}
		if( (p->p_shdr->sh_type) == SHT_REL)
		{
			dump_reloc_table(elf_file, p_ehdr, p, 1, filename);
			continue;
		}
		if( (p->p_shdr->sh_type) == SHT_DYNAMIC)
		{
			dump_dynamic(elf_file, p, 1, filename);
			continue;
		}
		if (strcmp(p->scn_name, ".debug") == 0)
		{
			dump_debug(filename);
			continue;
		}
		if (strcmp(p->scn_name, ".line") == 0)
		{
			dump_line(filename);
			continue;
		}

		(void)printf("\n%s:\n", p->scn_name);
		print_rawdata(p_sec, size);
	}
	(void)printf("\n");
}

/*
 * Print raw data in hexidecimal.  Input is the section data to
 * be printed out and the size of the data.  Output is relative
 * to a table lookup in dumpmap.h.
 */

static void
print_rawdata(p_sec, size)
unsigned char *p_sec;
long size;
{
	int   j;
	int   count;

	count = 1;

	(void)printf("\t");
	for (j = size/sizeof(short); j > 0; --j, ++count)
	{
		(void)printf("%.2x%.2x  ", p_sec[0], p_sec[1]);
		p_sec += 2;
		if (count == 12)
		{
			(void)printf("\n\t");
			count = 0;
		}
	}

	/*
	 * take care of last byte if odd byte section
 	 */
	if ((size & 0x1L) == 1L)
		(void)printf("%.2x", *p_sec);
	(void)printf("\n");
}



/*
 * Print relocation information.  Since this information is
 * machine dependent, new sections must be added for each machine
 * that is supported.  Input is an ELF file descriptor, the ELF header,
 * the SCNTAB structure, the number of sections, and a filename.
 * Set up necessary information to print relocation information
 * and call the appropriate print function depending on the
 * type of relocation information.  If the symbol table is
 * absent, no relocation data is processed.  Input is an
 * ELF file descriptor, the ELF header, the SCNTAB structure,
 * and the filename.  Set range of d_flag and name if n_flag.
 */

static void
dump_reloc_table(elf_file, p_ehdr, p_scns, num_scns, filename)
Elf    *elf_file;
Elf32_Ehdr *p_ehdr;
SCNTAB *p_scns;
int    num_scns;
char *filename;
{
	Elf32_Rela *rela;
	Elf32_Rel  *rel;

	Elf32_Sym *sym_data;
	size_t    sym_size;
	size_t    reloc_size;
	SCNTAB *reloc_symtab;
	SCNTAB *head_scns;

	int r_title = 0;

	if( (!p_flag) && (!r_title) )
	{
		(void)printf("\n    **** RELOCATION INFORMATION ****\n");
		r_title = 1;
	}

	while (num_scns-- > 0)
	{
		if( (p_scns->p_shdr->sh_type != SHT_RELA) && (p_scns->p_shdr->sh_type != SHT_REL) ) {
			p_scns++;
			continue;
		}


	head_scns = p_head_scns;
	head_scns += (p_scns->p_shdr->sh_link -1);

	if (head_scns->p_shdr->sh_type == SHT_SYMTAB)
	{
		reloc_symtab = p_symtab;
	}
	else if (head_scns->p_shdr->sh_type  == SHT_DYNSYM)
	{
		reloc_symtab = p_dynsym;
	}
	else
	/*if(p_symtab == NULL)*/
	{
		(void)fprintf(stderr, "%s: %s: could not get symbol table\n", prog_name, filename);
		return;
	}

	sym_data = NULL;
	sym_size = 0;
	reloc_size = 0;

	if( (sym_data = (Elf32_Sym *)get_scndata(reloc_symtab->p_sd, &sym_size) ) == NULL)
	{
		(void)fprintf(stderr, "%s: %s: no symbol table data\n", prog_name, filename);
		return;
	}


	if(p_scns == NULL)
	{
		(void)fprintf(stderr, "%s: %s: no section table data\n", prog_name, filename);
		return;
	}
	if(p_scns->p_shdr->sh_type == SHT_RELA)
	{
		if(!n_flag && r_flag) 
			(void)printf("\n%s:\n", p_scns->scn_name);
		if(!p_flag && (!n_flag && r_flag))
			(void)printf("Offset      Symndx                Type              Addend\n\n");
		rela = (Elf32_Rela *)get_scndata(p_scns->p_sd, &reloc_size);
		if (rela == (Elf32_Rela *)0)
		{
			(void)fprintf(stderr, "%s: %s: no relocation information\n", prog_name, filename);
			return;
		}
		if(n_flag)
		{
			rn_flag=1;
			print_rela(elf_file, p_scns, rela, sym_data, p_ehdr, reloc_size, sym_size, filename, reloc_symtab);
		}
		if(d_flag)
		{
			rn_flag = 0;
			print_rela(elf_file, p_scns, rela, sym_data, p_ehdr, reloc_size, sym_size, filename, reloc_symtab);
		}
		if(!n_flag && !d_flag)
			print_rela(elf_file, p_scns, rela, sym_data, p_ehdr, reloc_size, sym_size, filename, reloc_symtab);
	}
	else
	{
		if(p_scns->p_shdr->sh_type == SHT_REL)
		{
			if(!n_flag && r_flag)
				(void)printf("\n%s:\n", p_scns->scn_name);
			if(!p_flag && (!n_flag && r_flag))
			{
				(void)printf("Offset      Symndx              Type\n\n");
			}
			rel = (Elf32_Rel *)get_scndata(p_scns->p_sd, &reloc_size);
			if (rel == (Elf32_Rel *)0)
			{
				(void)fprintf(stderr, "%s: %s: no relocation information\n", prog_name, filename);
				return;
			}
			if (n_flag)
			{
				rn_flag = 1;
				print_rel(elf_file, p_scns, rel, sym_data, p_ehdr, reloc_size, sym_size, filename, reloc_symtab);
			}
			if(d_flag)
			{
				rn_flag = 0;
				print_rel(elf_file, p_scns, rel, sym_data, p_ehdr, reloc_size, sym_size, filename, reloc_symtab);
			}
			if(!n_flag && !d_flag)
				print_rel(elf_file, p_scns, rel, sym_data, p_ehdr, reloc_size, sym_size, filename, reloc_symtab);
		}
	}
	p_scns++;
	}
}


/*
 * Print relocation data of type SHT_RELA
 * If d_flag, print data corresponding only to
 * the section or range of sections specified.
 * If n_flag, print data corresponding only to
 * the named section.
 */

static void
print_rela(elf_file, p_scns, r, sym_data, p_ehdr, reloc_size, sym_size, filename, reloc_symtab)
Elf    *elf_file;
SCNTAB *p_scns;
Elf32_Rela *r;
Elf32_Sym *sym_data;
Elf32_Ehdr *p_ehdr;
size_t reloc_size;
size_t sym_size;
char *filename;
SCNTAB *reloc_symtab;
{
	Elf32_Rela *rela;
	size_t      no_entries;
	size_t      no_syms;
	int       type, sym;
	Elf32_Sym *index;
	static int n_title = 0;

	char *sym_name;
	rela = r;
	no_entries = reloc_size / sizeof (Elf32_Rela);
	no_syms = sym_size / sizeof(Elf32_Sym);
	while(no_entries--)
	{
		type = ELF32_R_TYPE(rela->r_info);
		sym = ELF32_R_SYM(rela->r_info);
		if( (sym > no_syms-1) || (sym < 0) )
		{
			(void)fprintf(stderr, "%s: %s: invalid symbol table offset - %d - in %s\n",
			prog_name, filename, sym, p_scns->scn_name);
				rela++;
				continue;
		}
		index = sym_data + sym;
		sym_name = (char *) elf_strptr(elf_file,
			reloc_symtab->p_shdr->sh_link, index->st_name);
		if(r_flag && rn_flag)
		{
			if(strcmp(name, p_scns->scn_name) != 0)
			{
				rela++;
				continue;
			}
			if (!n_title)
			{
				(void)printf("\n%s:\n", p_scns->scn_name);
				(void)printf("Offset      Symndx                Type            Addend\n\n");
				n_title=1;
			}

		}
		if(d_flag)
		{
			if(!d_hi)
				d_hi = d_low;
			if( (sym<d_low) || (sym>d_hi) )
			{
				rela++;
				continue;
			}
		}

		(void)printf("%-#12x", (unsigned long)rela->r_offset);
		if (!v_flag)
		{
			(void)printf("%-22d%-18d", sym, type);
		}
		else
		{
			if(strlen(sym_name)){
				int len = strlen(sym_name)+1;
				char tmpstr[10];
				if (len > 22) {
					(void)sprintf(tmpstr, "%%-%ds", len);
					(void)printf(tmpstr, sym_name);
				}
				else
					(void)printf("%-22s", sym_name);
			}
			else
				(void)printf("%-22d", sym);
			switch(p_ehdr->e_machine)
			{
			case EM_M32:
				switch(type)
				{
				case(R_M32_NONE): (void)printf(Fmtrel, "R_M32_NONE"); break;
				case(R_M32_32): (void)printf(Fmtrel, "R_M32_32"); break;
				case(R_M32_32_S): (void)printf(Fmtrel, "R_M32_32_S"); break;
				case(R_M32_PC32_S): (void)printf(Fmtrel, "R_M32_PC32_S"); break;
				case(R_M32_GOT32_S): (void)printf(Fmtrel, "R_M32_GOT32_S"); break;
				case(R_M32_PLT32_S): (void)printf(Fmtrel, "R_M32_PLT32_S"); break;
				case(R_M32_COPY): (void)printf(Fmtrel, "R_M32_COPY"); break;
				case(R_M32_GLOB_DAT): (void)printf(Fmtrel, "R_M32_GLOB_DAT"); break;
				case(R_M32_JMP_SLOT): (void)printf(Fmtrel, "R_M32_JMP_SLOT"); break;
				case(R_M32_RELATIVE): (void)printf(Fmtrel, "R_M32_RELATIVE"); break;
				case(R_M32_RELATIVE_S): (void)printf(Fmtrel, "R_M32_RELATIVE_S"); break;
				default: (void)printf(Fmtreld, type); break;
				}
				break;
			case EM_386:
				switch(type)
				{
				case(R_386_NONE): (void)printf(Fmtrel, "R_386_NONE"); break;
				case(R_386_32): (void)printf(Fmtrel, "R_386_32"); break;
				case(R_386_GOT32): (void)printf(Fmtrel, "R_386_GOT32"); break;
				case(R_386_PLT32): (void)printf(Fmtrel, "R_386_PLT32"); break;
				case(R_386_COPY): (void)printf(Fmtrel, "R_386_COPY"); break;
				case(R_386_GLOB_DAT): (void)printf(Fmtrel, "R_386_GLOB_DAT"); break;
				case(R_386_JMP_SLOT): (void)printf(Fmtrel, "R_386_JMP_SLOT"); break;
				case(R_386_RELATIVE): (void)printf(Fmtrel, "R_386_RELATIVE"); break;
				case(R_386_GOTOFF): (void)printf(Fmtrel, "R_386_GOTOFF"); break;
				case(R_386_GOTPC): (void)printf(Fmtrel, "R_386_GOTPC"); break;
				default: (void)printf(Fmtreld, type); break;
				}
				break;
			case EM_SPARC:		/* SPARC */
			case EM_SPARC32PLUS:	/* SPARC32PLUS */
				switch(type) {
				case(R_SPARC_NONE):	(void)printf(Fmtrel, "R_SPARC_NONE"); break;
				case(R_SPARC_8):	(void)printf(Fmtrel, "R_SPARC_8"); break;
				case(R_SPARC_16):	(void)printf(Fmtrel, "R_SPARC_16"); break;
				case(R_SPARC_32):	(void)printf(Fmtrel, "R_SPARC_32"); break;
				case(R_SPARC_DISP8):	(void)printf(Fmtrel, "R_SPARC_DISP8"); break;
				case(R_SPARC_DISP16):	(void)printf(Fmtrel, "R_SPARC_DISP16"); break;
				case(R_SPARC_DISP32):	(void)printf(Fmtrel, "R_SPARC_DISP32"); break;
				case(R_SPARC_WDISP30):	(void)printf(Fmtrel, "R_SPARC_WDISP30"); break;
				case(R_SPARC_WDISP22):	(void)printf(Fmtrel, "R_SPARC_WDISP22"); break;
				case(R_SPARC_HI22):	(void)printf(Fmtrel, "R_SPARC_HI22"); break;
				case(R_SPARC_22):	(void)printf(Fmtrel, "R_SPARC_22"); break;
				case(R_SPARC_13):	(void)printf(Fmtrel, "R_SPARC_13"); break;
				case(R_SPARC_LO10):	(void)printf(Fmtrel, "R_SPARC_LO10"); break;
				case(R_SPARC_GOT10):	(void)printf(Fmtrel, "R_SPARC_GOT10"); break;
				case(R_SPARC_GOT13):	(void)printf(Fmtrel, "R_SPARC_GOT13"); break;
				case(R_SPARC_GOT22):	(void)printf(Fmtrel, "R_SPARC_GOT22"); break;
				case(R_SPARC_PC10):	(void)printf(Fmtrel, "R_SPARC_PC10"); break;
				case(R_SPARC_PC22):	(void)printf(Fmtrel, "R_SPARC_PC22"); break;
				case(R_SPARC_WPLT30):	(void)printf(Fmtrel, "R_SPARC_WPLT30"); break;
				case(R_SPARC_COPY):	(void)printf(Fmtrel, "R_SPARC_COPY"); break;
				case(R_SPARC_GLOB_DAT):	(void)printf(Fmtrel, "R_SPARC_GLOB_DAT");break;
				case(R_SPARC_JMP_SLOT):	(void)printf(Fmtrel, "R_SPARC_JMP_SLOT");break;
				case(R_SPARC_RELATIVE):	(void)printf(Fmtrel, "R_SPARC_RELATIVE");break;
				case(R_SPARC_UA32):	(void)printf(Fmtrel, "R_SPARC_UA32"); break;
				case(R_SPARC_PLT32):	(void)printf(Fmtrel, "R_SPARC_PLT32"); break;
				case(R_SPARC_HIPLT22):	(void)printf(Fmtrel, "R_SPARC_HIPLT22"); break;
				case(R_SPARC_LOPLT10):	(void)printf(Fmtrel, "R_SPARC_LOPLT10"); break;
				case(R_SPARC_PCPLT32):	(void)printf(Fmtrel, "R_SPARC_PCPLT32"); break;
				case(R_SPARC_PCPLT22):	(void)printf(Fmtrel, "R_SPARC_PCPLT22"); break;
				case(R_SPARC_PCPLT10):	(void)printf(Fmtrel, "R_SPARC_PCPLT10"); break;
				case(R_SPARC_10):	(void)printf(Fmtrel, "R_SPARC_10"); break;
				case(R_SPARC_11):	(void)printf(Fmtrel, "R_SPARC_11"); break;
				case(R_SPARC_64):	(void)printf(Fmtrel, "R_SPARC_64"); break;
				case(R_SPARC_OLO10):	(void)printf(Fmtrel, "R_SPARC_OLO10"); break;
				case(R_SPARC_HH22):	(void)printf(Fmtrel, "R_SPARC_HH22"); break;
				case(R_SPARC_HM10):	(void)printf(Fmtrel, "R_SPARC_HM10"); break;
				case(R_SPARC_LM22):	(void)printf(Fmtrel, "R_SPARC_LM22"); break;
				case(R_SPARC_PC_HH22):	(void)printf(Fmtrel, "R_SPARC_PC_HH22"); break;
				case(R_SPARC_PC_HM10):	(void)printf(Fmtrel, "R_SPARC_PC_HM10"); break;
				case(R_SPARC_PC_LM22):	(void)printf(Fmtrel, "R_SPARC_PC_LM22"); break;
				case(R_SPARC_WDISP16):	(void)printf(Fmtrel, "R_SPARC_WDISP16"); break;
				case(R_SPARC_WDISP19):	(void)printf(Fmtrel, "R_SPARC_WDISP19"); break;
				case(R_SPARC_GLOB_JMP):	(void)printf(Fmtrel, "R_SPARC_GLOB_JMP"); break;
				case(R_SPARC_7):	(void)printf(Fmtrel, "R_SPARC_7"); break;
				case(R_SPARC_5):	(void)printf(Fmtrel, "R_SPARC_5"); break;
				case(R_SPARC_6):	(void)printf(Fmtrel, "R_SPARC_6"); break;
				default:		(void)printf(Fmtreld, type); break;
				}
				break;
			default: (void)printf(Fmtreld, type); break;
			}
		}
		(void)printf("%ld\n", (long)rela->r_addend);
		rela++;
	}
}


/*
 * Print relocation data of type SHT_REL.
 * If d_flag, print data corresponding only to
 * the section or range of sections specified.
 * If n_flag, print data corresponding only to
 * the named section.
 */

static void
print_rel(elf_file, p_scns, r, sym_data, p_ehdr, reloc_size, sym_size, filename, reloc_symtab)
Elf    *elf_file;
SCNTAB *p_scns;
Elf32_Rel *r;
Elf32_Sym *sym_data;
Elf32_Ehdr *p_ehdr;
size_t reloc_size;
size_t sym_size;
char *filename;
SCNTAB *reloc_symtab;
{
	Elf32_Rel *rel;
	size_t      no_entries;
	int       type, sym;
	Elf32_Sym *index;
	size_t      no_syms;
	static int n_title = 0;

	char *sym_name;
	rel = r;
	no_entries = reloc_size / sizeof(Elf32_Rel);
	no_syms = sym_size / sizeof(Elf32_Sym);
	while(no_entries--)
	{
		type = ELF32_R_TYPE(rel->r_info);
		sym = ELF32_R_SYM(rel->r_info);
		if( (sym > no_syms-1) || (sym < 0) )
		{
			(void)fprintf(stderr, "%s: %s: invalid symbol table offset - %d - in %s\n",
			prog_name, filename, sym, p_scns->scn_name);
				rel++;
				continue;
		}
		index = sym_data + sym;
		sym_name = (char *) elf_strptr(elf_file,
			reloc_symtab->p_shdr->sh_link, index->st_name);
		if(r_flag && rn_flag)
		{
			if(strcmp(name, p_scns->scn_name) != 0)
			{
				rel++;
				continue;
			}
                        if (!n_title)
                        {
                                (void)printf("\n%s:\n", p_scns->scn_name);
               			(void)printf("Offset      Symndx              Type\n\n");
                                n_title=1;
                        }


		}
		if(d_flag)
		{
			if(!d_hi)
				d_hi = d_low;
			if( (sym<d_low) || (sym>d_hi) )
			{
				rel++;
				continue;
			}
		}

		(void)printf("%-#12x", (unsigned long)rel->r_offset);
		if (!v_flag)
		{
			(void)printf("%-20d%-18d", sym, type);
		}
		else
		{
			if(strlen(sym_name))
				(void)printf("%-20s", sym_name);
			else
				(void)printf("%-20d", sym);

			switch(p_ehdr->e_machine)
			{
			case EM_M32:
				switch(type)
				{
				case(R_M32_NONE): (void)printf(Fmtrel, "R_M32_NONE"); break;
				case(R_M32_32): (void)printf(Fmtrel, "R_M32_32"); break;
				case(R_M32_32_S): (void)printf(Fmtrel, "R_M32_32_S"); break;
				case(R_M32_PC32_S): (void)printf(Fmtrel, "R_M32_PC32_S"); break;
				case(R_M32_GOT32_S): (void)printf(Fmtrel, "R_M32_GOT32_S"); break;
				case(R_M32_PLT32_S): (void)printf(Fmtrel, "R_M32_PLT32_S"); break;
				case(R_M32_COPY): (void)printf(Fmtrel, "R_M32_COPY"); break;
				case(R_M32_GLOB_DAT): (void)printf(Fmtrel, "R_M32_GLOB_DAT"); break;
				case(R_M32_JMP_SLOT): (void)printf(Fmtrel, "R_M32_JMP_SLOT"); break;
				case(R_M32_RELATIVE): (void)printf(Fmtrel, "R_M32_RELATIVE"); break;
				case(R_M32_RELATIVE_S): (void)printf(Fmtrel, "R_M32_RELATIVE_S"); break;
				default: (void)printf(Fmtreld, type); break;
				}
				break;
			case EM_386:
				switch(type)
				{
				case(R_386_NONE): (void)printf(Fmtrel, "R_386_NONE"); break;
				case(R_386_32): (void)printf(Fmtrel, "R_386_32"); break;
				case(R_386_PC32): (void)printf(Fmtrel, "R_386_PC32"); break;
				case(R_386_GOT32): (void)printf(Fmtrel, "R_386_GOT32"); break;
				case(R_386_PLT32): (void)printf(Fmtrel, "R_386_PLT32"); break;
				case(R_386_COPY): (void)printf(Fmtrel, "R_386_COPY"); break;
				case(R_386_GLOB_DAT): (void)printf(Fmtrel, "R_386_GLOB_DAT"); break;
				case(R_386_JMP_SLOT): (void)printf(Fmtrel, "R_386_JMP_SLOT"); break;
				case(R_386_RELATIVE): (void)printf(Fmtrel, "R_386_RELATIVE"); break;
				case(R_386_GOTOFF): (void)printf(Fmtrel, "R_386_GOTOFF"); break;
				case(R_386_GOTPC): (void)printf(Fmtrel, "R_386_GOTPC"); break;
				default: (void)printf(Fmtreld, type); break;
				}
				break;
			case EM_SPARC:		/* SPARC */
			case EM_SPARC32PLUS:	/* SPARC32PLUS */
				switch(type) {
				case(R_SPARC_NONE):	(void)printf(Fmtrel, "R_SPARC_NONE"); break;
				case(R_SPARC_8):	(void)printf(Fmtrel, "R_SPARC_8"); break;
				case(R_SPARC_16):	(void)printf(Fmtrel, "R_SPARC_16"); break;
				case(R_SPARC_32):	(void)printf(Fmtrel, "R_SPARC_32"); break;
				case(R_SPARC_DISP8):	(void)printf(Fmtrel, "R_SPARC_DISP8"); break;
				case(R_SPARC_DISP16):	(void)printf(Fmtrel, "R_SPARC_DISP16"); break;
				case(R_SPARC_DISP32):	(void)printf(Fmtrel, "R_SPARC_DISP32"); break;
				case(R_SPARC_WDISP30):	(void)printf(Fmtrel, "R_SPARC_WDISP30"); break;
				case(R_SPARC_WDISP22):	(void)printf(Fmtrel, "R_SPARC_WDISP22"); break;
				case(R_SPARC_HI22):	(void)printf(Fmtrel, "R_SPARC_HI22"); break;
				case(R_SPARC_22):	(void)printf(Fmtrel, "R_SPARC_22"); break;
				case(R_SPARC_13):	(void)printf(Fmtrel, "R_SPARC_13"); break;
				case(R_SPARC_LO10):	(void)printf(Fmtrel, "R_SPARC_LO10"); break;
				case(R_SPARC_GOT10):	(void)printf(Fmtrel, "R_SPARC_GOT10"); break;
				case(R_SPARC_GOT13):	(void)printf(Fmtrel, "R_SPARC_GOT13"); break;
				case(R_SPARC_GOT22):	(void)printf(Fmtrel, "R_SPARC_GOT22"); break;
				case(R_SPARC_PC10):	(void)printf(Fmtrel, "R_SPARC_PC10"); break;
				case(R_SPARC_PC22):	(void)printf(Fmtrel, "R_SPARC_PC22"); break;
				case(R_SPARC_WPLT30):	(void)printf(Fmtrel, "R_SPARC_WPLT30"); break;
				case(R_SPARC_COPY):	(void)printf(Fmtrel, "R_SPARC_COPY"); break;
				case(R_SPARC_GLOB_DAT):	(void)printf(Fmtrel, "R_SPARC_GLOB_DAT");break;
				case(R_SPARC_JMP_SLOT):	(void)printf(Fmtrel, "R_SPARC_JMP_SLOT");break;
				case(R_SPARC_RELATIVE):	(void)printf(Fmtrel, "R_SPARC_RELATIVE");break;
				case(R_SPARC_UA32):	(void)printf(Fmtrel, "R_SPARC_UA32"); break;
				case(R_SPARC_PLT32):	(void)printf(Fmtrel, "R_SPARC_PLT32"); break;
				case(R_SPARC_HIPLT22):	(void)printf(Fmtrel, "R_SPARC_HIPLT22"); break;
				case(R_SPARC_LOPLT10):	(void)printf(Fmtrel, "R_SPARC_LOPLT10"); break;
				case(R_SPARC_PCPLT32):	(void)printf(Fmtrel, "R_SPARC_PCPLT32"); break;
				case(R_SPARC_PCPLT22):	(void)printf(Fmtrel, "R_SPARC_PCPLT22"); break;
				case(R_SPARC_PCPLT10):	(void)printf(Fmtrel, "R_SPARC_PCPLT10"); break;
				case(R_SPARC_10):	(void)printf(Fmtrel, "R_SPARC_10"); break;
				case(R_SPARC_11):	(void)printf(Fmtrel, "R_SPARC_11"); break;
				case(R_SPARC_64):	(void)printf(Fmtrel, "R_SPARC_64"); break;
				case(R_SPARC_OLO10):	(void)printf(Fmtrel, "R_SPARC_OLO10"); break;
				case(R_SPARC_HH22):	(void)printf(Fmtrel, "R_SPARC_HH22"); break;
				case(R_SPARC_HM10):	(void)printf(Fmtrel, "R_SPARC_HM10"); break;
				case(R_SPARC_LM22):	(void)printf(Fmtrel, "R_SPARC_LM22"); break;
				case(R_SPARC_PC_HH22):	(void)printf(Fmtrel, "R_SPARC_PC_HH22"); break;
				case(R_SPARC_PC_HM10):	(void)printf(Fmtrel, "R_SPARC_PC_HM10"); break;
				case(R_SPARC_PC_LM22):	(void)printf(Fmtrel, "R_SPARC_PC_LM22"); break;
				case(R_SPARC_WDISP16):	(void)printf(Fmtrel, "R_SPARC_WDISP16"); break;
				case(R_SPARC_WDISP19):	(void)printf(Fmtrel, "R_SPARC_WDISP19"); break;
				case(R_SPARC_GLOB_JMP):	(void)printf(Fmtrel, "R_SPARC_GLOB_JMP"); break;
				case(R_SPARC_7):	(void)printf(Fmtrel, "R_SPARC_7"); break;
				case(R_SPARC_5):	(void)printf(Fmtrel, "R_SPARC_5"); break;
				case(R_SPARC_6):	(void)printf(Fmtrel, "R_SPARC_6"); break;
				default:		(void)printf(Fmtreld, type); break;
				}
				break;
			default: (void)printf(Fmtreld, type); break;
			}
		}
		(void)printf("\n");
		rel++;
	}
}


/*
 * Print dynamic linking information.  Input is an ELF
 * file descriptor, the SCNTAB structure, the number of
 * sections, and the filename.
 */
static void
dump_dynamic(Elf * elf_file, SCNTAB * p_scns, int num_scns, char * filename)
{
	Elf32_Dyn *	p_dyn;
	Elf32_Phdr *	p_phdr;
	Elf32_Ehdr *	e_ehdr;
	char *		dt_name;
	int		index = 1;
	int		lib_scns = num_scns;
	SCNTAB *	l_scns = p_scns;
	int		header_num = 0;
	const char *	Fmttag = "%-12s";
	const char *	Fmtptr = "%#lx";

	if (!p_flag)
		(void) printf("\n  **** DYNAMIC SECTION INFORMATION ****\n");

	for (; num_scns > 0; num_scns--, p_scns++) {
		Elf32_Word	link;
		size_t		dyn_size;

		if (p_scns->p_shdr->sh_type != SHT_DYNAMIC)
			continue;

		if (!p_flag) {
			(void) printf("%s:\n", p_scns->scn_name);
			(void) printf("[INDEX]\tTag         Value\n");
		}

		if ((p_dyn = (Elf32_Dyn *)get_scndata(p_scns->p_sd,
		    &dyn_size)) == 0) {
			(void) fprintf(stderr, "%s: %s: no data in "
			    "%s section\n", prog_name, filename,
			    p_scns->scn_name);
			return;
		}

		link = p_scns->p_shdr->sh_link;
		while (p_dyn->d_tag != DT_NULL) {
			(void) printf("[%d]\t", index++);

			switch(p_dyn->d_tag) {
			case(DT_NEEDED):
				(void)printf(Fmttag, (const char *)"NEEDED");
				if(v_flag)
					dt_name = (char *)elf_strptr(elf_file,
					    link, p_dyn->d_un.d_ptr);
				if (v_flag && strlen(dt_name))
					(void) printf("%s", dt_name);
				else
					(void) printf(Fmtptr,
					    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_USED):
				(void)printf(Fmttag, (const char *)"USED");
				if(v_flag)
					dt_name = (char *)elf_strptr(elf_file,
					    link, p_dyn->d_un.d_ptr);
				if(v_flag && strlen(dt_name))
					(void) printf("%s", dt_name);
				else
					(void) printf(Fmtptr,
					    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_PLTRELSZ):
				(void) printf(Fmttag, (const char *)"PLTSZ");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			case(DT_PLTGOT):
				(void) printf(Fmttag, (const char *)"PLTGOT");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_HASH):
				(void) printf(Fmttag, (const char *)"HASH");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_STRTAB):
				(void) printf(Fmttag, (const char *)"STRTAB");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_SYMTAB):
				(void) printf(Fmttag, (const char *)"SYMTAB");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_RELA):
				(void) printf(Fmttag, (const char *)"RELA");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_RELASZ):
				(void) printf(Fmttag, (const char *)"RELASZ");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			case(DT_RELAENT):
				(void) printf(Fmttag, (const char *)"RELAENT");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			case(DT_STRSZ):
				(void) printf(Fmttag, (const char *)"STRSZ");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			case(DT_SYMENT):
				(void) printf(Fmttag, (const char *)"SYMENT");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			case(DT_INIT):
				(void) printf(Fmttag, (const char *)"INIT");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_FINI):
				(void) printf(Fmttag, (const char *)"FINI");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_SONAME):
				(void) printf(Fmttag, (const char *)"SONAME");
				if (v_flag)
					dt_name = (char *)elf_strptr(elf_file,
					    link, p_dyn->d_un.d_ptr);
				if (v_flag && strlen(dt_name))
					(void) printf("%s", dt_name);
				else
					(void) printf(Fmtptr,
					    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_FILTER):
				(void) printf(Fmttag, (const char *)"FILTER");
				if (v_flag)
					dt_name = (char *)elf_strptr(elf_file,
					    link, p_dyn->d_un.d_ptr);
				if (v_flag && strlen(dt_name))
					(void) printf("%s", dt_name);
				else
					(void) printf(Fmtptr,
					    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_AUXILIARY):
				(void) printf(Fmttag, 
					(const char *)"AUXILIARY");
				if (v_flag)
					dt_name = (char *)elf_strptr(elf_file,
					    link, p_dyn->d_un.d_ptr);
				if (v_flag && strlen(dt_name))
					(void) printf("%s", dt_name);
				else
					(void) printf(Fmtptr,
					    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_RPATH):
				(void) printf(Fmttag, (const char *)"RPATH");
				if (v_flag)
					dt_name = (char *)elf_strptr(elf_file,
					    link, p_dyn->d_un.d_ptr);
				if (v_flag && strlen(dt_name))
					(void) printf("%s", dt_name);
				else
					(void) printf(Fmtptr,
					    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_SYMBOLIC):
				(void) printf(Fmttag, (const char *)"SYMB");
				(void) printf("%s", (const char *)"(ignored)");
				break;
			case(DT_REL):
				(void) printf(Fmttag, (const char *)"REL");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_RELSZ):
				(void) printf(Fmttag, (const char *)"RELSZ");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			case(DT_RELENT):
				(void) printf(Fmttag, (const char *)"RELENT");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			case(DT_PLTREL):
				(void) printf(Fmttag, (const char *)"PLTREL");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			case(DT_DEBUG):
				(void) printf(Fmttag, (const char *)"DEBUG");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_TEXTREL):
				(void) printf(Fmttag, (const char *)"TEXTREL");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			case(DT_JMPREL):
				(void) printf(Fmttag, (const char *)"JMPREL");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_VERDEF):
				(void) printf(Fmttag, (const char *)"VERDEF");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_VERDEFNUM):
				(void) printf(Fmttag,
				    (const char *)"VERDEFNUM");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			case(DT_VERNEED):
				(void) printf(Fmttag, (const char *)"VERNEED");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_ptr);
				break;
			case(DT_VERNEEDNUM):
				(void) printf(Fmttag,
				    (const char *)"VERNEEDNUM");
				(void) printf(Fmtptr,
				    (unsigned long)p_dyn->d_un.d_val);
				break;
			default:
				(void) printf("%d", p_dyn->d_tag);
				break;
			}
			(void) printf("\n");
			p_dyn++;
		}
 	}

	/*
	 * Check for existence of static shared library information.
	 */
	p_phdr = elf32_getphdr(elf_file);
	e_ehdr = elf32_getehdr(elf_file);
	header_num = e_ehdr->e_phnum;
	while (header_num-- >0)
	{
		if(p_phdr->p_type == PT_SHLIB)
		{
			while (--lib_scns > 0)
			{
				if (strcmp(l_scns->scn_name, ".lib") == 0)
				{
					print_static(l_scns, filename);
				}
				l_scns++;
			}
		}
		p_phdr++;
	}
}

/*
 * Print static shared library information.
 */

static void
print_static(l_scns, filename)
SCNTAB *l_scns;
char * filename;
{
	size_t section_size;
	unsigned char *strtab;
	unsigned char *path, buf[1024];
	unsigned long *temp;
	unsigned long total, topath;

	(void)printf("\n  **** STATIC SHARED LIBRARY INFORMATION ****\n");
	(void)printf("\n%s:\n", filename);
	(void)printf("\t");
	section_size =0;
	if ((strtab = (unsigned char *)get_scndata(l_scns->p_sd, &section_size)) == NULL)
	{
		return;
	}

	while (section_size != 0)
	{
		/* LINTED */
		temp = (unsigned long *)strtab;
		total = temp[0];
		topath = temp[1];
		path = strtab + (topath*sizeof(long));
		strncpy( (char*)buf, (char*)path, (total - topath)*sizeof(long));
		fprintf(stdout, "%s\n", buf);
		strtab += total*sizeof(long);
		section_size -= (total*sizeof(long));
	}
}

/*
 * Set a range.  Input is a character string, a lower
 * bound and an upper bound.  This function converts 
 * a character string into its correct integer values,
 * setting the first value as the lower bound, and
 * the second value as the upper bound.  If more values
 * are given they are ignored with a warning.
 */

static void
set_range(s, low, high)
char *s;
int  *low;
int  *high;
{
	char * w;

	while( (w = strtok(s, ",") ) != NULL)
	{
		if(!(*low))
			*low = atol(w);
		else
			if(!(*high))
				*high = atol(w);
			else
			{
				(void)fprintf(stderr, "%s: too many arguments - %s ignored\n",
					prog_name, w);
					return;
			}
		s = NULL;
	} /* end while */
}


/*
 * Check that a range of numbers is valid.  Input is
 * a lower bound, an upper bound, a boundary condition,
 * and the filename.  Negative numbers and numbers greater
 * than the bound are invalid.  low must be smaller than hi.
 * The returned integer is the number of items in the
 * range if it is valid and -1 otherwise.
 */

static int
check_range(low, hi, bound, filename)
int low;
int hi;
int bound;
char *filename;
{
		if( (low>bound) || (low<=0) )
		{
			(void)fprintf(stderr, "%s: %s: number out of range, %d\n", prog_name, filename, low);
			return (-1) ;
		}
		if( (hi>bound) || (hi<0) )
		{
			(void)fprintf(stderr, "%s: %s: number out of range, %d\n", prog_name, filename, hi);
			return (-1) ;
		}

		if( hi && (low > hi) )
		{
			(void)fprintf(stderr, "%s: %s: invalid range, %d,%d\n", prog_name, filename, low, hi);
			return (-1) ;
		}
		if(hi)
			return (hi - low + 1);
		else
			return (1);
}

/* demangle C++ names */
static char *format = "%s\t[%s]";
static char *
demangled_name(char * s)
{
	extern char *	DemangleAndFormat(char *, char *);

	return (DemangleAndFormat(s, format));
}
