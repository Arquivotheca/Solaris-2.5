/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)readelf.c	1.14	94/10/11 SMI"

#include	"gprof.h"
#include	<stdlib.h>
#include	<sys/file.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<string.h>
#include	<sysexits.h>

#include	<libelf.h>

#ifdef DEBUG
static void	debug_dup_del(nltype *, nltype *);

#define	DPRINTF(msg, file)	if (debug & ELFDEBUG) \
					printf(msg, file);

#define	PRINTF(msg)		if (debug & ELFDEBUG) \
					printf(msg);

#define	DEBUG_DUP_DEL(keeper, louser)	if (debug & ELFDEBUG) \
						debug_dup_del(keeper, louser);

#else
#define	DPRINTF(msg, file)
#define	PRINTF(msg)
#define	DEBUG_DUP_DEL(keeper, louser)
#endif



#define	VOID_P		void *

unsigned long textbegin, textsize;

/* Prototype definitions first */

static void	process(char *filename, int fd);
static void	get_symtab(Elf32_Ehdr elfhdr, int fd, char *filename);
static nltype *	readsyms(Elf32_Sym *data, size_t num);
static int	compare(nltype *a, nltype *b);
static void	get_textseg(Elf32_Ehdr elfhdr, int fd, char *filename);

/* Some file global variables */

static char *		strtab;			/* String Table */
static Elf32_Sym *	symtab;			/* Symbol Table */
static Elf32_Shdr *	secthdr;		/* Section header */


void
getnfile(char * aoutname)
{
	int	fd;

	DPRINTF(" Attempting to open %s  \n", aoutname);
	if ((fd = open((aoutname), O_RDONLY)) == -1) {
		(void) fprintf(stderr, "%s: cannot read file \n", aoutname);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_NOINPUT);
	}
	process(aoutname, fd);
	(void) close(fd);
}

/*
 * Get the ELF header and,  if it exists, call get_symtab()
 * to begin processing of the file; otherwise, return from
 * processing the file with a warning.
 */

static void
process(char *filename, int fd)
{

	Elf32_Ehdr elfhdr;		/* Elf header */
	extern bool cflag;

	DPRINTF(" Attempting to open ELF header for %s \n", filename);
	if (read(fd, &elfhdr, sizeof (elfhdr)) != sizeof (elfhdr)) {
		fprintf(stderr, "%s: unable to read ELF header \n", filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_IOERR);
	}
	/*
	 * First get all the symbol table and string table info
	 */
	DPRINTF(" Attempting to open symbol table for: %s \n", filename);
	get_symtab(elfhdr, fd, filename);
	if (cflag) {
	/*
	 * Next get the text information based on the Program Header Table
	 */
		DPRINTF(" Attempting to open text segment for: %s \n",
		    filename);
		get_textseg(elfhdr, fd, filename);
	}
}

	/*
	 * Get text info. The way to do this, is to look
	 * at Program Header table and look for a program header
	 * which does not have a writable section. This has
	 * got to be the text section, right? :-)
	 * No more libelf functions to access the segment
	 * Mostly, because a seg. can consists of various
	 * sections, all contiguous <- Need to change this
	 * if shared libs are to be read correctly
	 */

static void
get_textseg(Elf32_Ehdr elfhdr, int fd, char *filename)
{
	Elf32_Phdr *	proghdr, *phent;
	int		i;

	DPRINTF(" Attempting to open Program Header for: %s \n", filename);
	if (elfhdr.e_phnum == 0) {
		fprintf(stderr, "%s: no program Header table \n", filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_IOERR);
	}
	if (lseek(fd, (long) elfhdr.e_phoff, L_SET) !=
			elfhdr.e_phoff) {
		fprintf(stderr,
		    "%s: unable to seek Program Header \n", filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_IOERR);
	}
	if ((proghdr = (Elf32_Phdr *) malloc(elfhdr.e_phentsize *
		elfhdr.e_phnum)) == NULL) {
		fprintf(stderr,
		    "%s: unable to allocate program header.\n", filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_IOERR);
	}

	DPRINTF(" Attempting to open Program table for: %s \n", filename);
	if (read(fd, proghdr, elfhdr.e_phentsize * elfhdr.e_phnum) !=
			elfhdr.e_phentsize * elfhdr.e_phnum) {
		fprintf(stderr,
		    "%s: unable to read ELF program header\n", filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_IOERR);
	}
	phent = proghdr;

	/*
	 * Look thru each Program Header table to see there is
	 * one section which is not writable
	 * This must be the text section
	 */

	DPRINTF(" Searching for text segment in: %s \n", filename);
	DPRINTF("\n Program Header table *** (%d entries)\n", elfhdr.e_phnum);
	for (i = 0; i < (int) elfhdr.e_phnum; i++) {

#ifdef DEBUG	/* Easier than creating more ifdef definitions at top */
		/* Print out all the fields of the Program Table header */

		if (debug & ELFDEBUG) {
			printf("Entry no: %d:: \n", i);
			printf("\t\tp_offset: 0x%x", phent->p_offset);
			printf("\tp_vaddr: 0x%x\n", phent->p_vaddr);
			printf("p_paddr: 0x%x", phent->p_paddr);
			printf("\tp_filesz: 0x%x", phent->p_filesz);
			printf("\tp_memsz: 0x%x\n", phent->p_memsz);
			printf("p_flags: [ ");
			if (phent->p_flags & PF_X)
				printf("PF_X ");
			if (phent->p_flags & PF_W)
				printf("PF_W ");
			if (phent->p_flags & PF_R)
				printf("PF_R ");
			if (phent->p_flags & PF_MASKPROC)
				printf("PF_MASKPROC ");
			printf("]");
			printf("\tp_align: 0x%x\n", phent->p_align);
		}
#endif

		if (!(phent->p_flags & PF_W)) {
			/* Found one! */
			textbegin = phent->p_vaddr;
			textsize  = phent->p_filesz;
			textspace = malloc(textsize);
			if (lseek(fd, (long) phent->p_offset, L_SET) !=
				phent->p_offset) {
				fprintf(stderr,
				    "Unable to seek Text section \n");
				return;
			}
			DPRINTF("Reading in the text section: %s \n", filename);
			if (read(fd, textspace, textsize) != textsize) {
				fprintf(stderr, "Unable to read Text \n");
				return;
			}
		break;
		}
		phent ++;
	}		/* end of for loop */
}

#ifdef	DEBUG
static void
debug_dup_del(nltype * keeper, nltype * louser)
{
	printf("remove_dup_syms: discarding sym %s over sym %s\n",
		louser->name, keeper->name);
}
#endif

static void
remove_dup_syms(nltype * nl, int * sym_count)
{
	int	i;
	int	index;
	int	nextsym;

	nltype *	orig_list;
	if ((orig_list = malloc(sizeof (nltype) * *sym_count)) == NULL) {
		fprintf(stderr, "gprof: remove_dup_syms: malloc failed\n");
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_UNAVAILABLE);
	}
	memcpy(orig_list, nl, sizeof (nltype) * *sym_count);

	for (i = 0, index = 0, nextsym = 1; nextsym < *sym_count; nextsym++) {
		int	i_type;
		int	n_bind;
		int	n_type;
		/*
		 * If orig_list[nextsym] points to a new symvalue, then we
		 * will copy our keeper and move on to the next symbol.
		 */
		if ((orig_list + i)->value < (orig_list + nextsym)->value) {
			*(nl + index++) = *(orig_list +i);
			i = nextsym;
			continue;
		}

		/*
		 * If these two symbols have the same info, then we
		 * keep the first and keep checking for dups.
		 */
		if ((orig_list + i)->syminfo ==
		    (orig_list + nextsym)->syminfo) {
			DEBUG_DUP_DEL(orig_list + i, orig_list + nextsym);
			continue;
		}
		n_bind = ELF32_ST_BIND((orig_list + nextsym)->syminfo);
		i_type = ELF32_ST_TYPE((orig_list + i)->syminfo);
		n_type = ELF32_ST_TYPE((orig_list + nextsym)->syminfo);
		/*
		 * If they have the same type we take the stronger
		 * bound function.
		 */
		if (i_type == n_type) {
			if (n_bind == STB_WEAK) {
				DEBUG_DUP_DEL((orig_list + i),
				    (orig_list + nextsym));
				continue;
			}
			DEBUG_DUP_DEL((orig_list + nextsym),
			    (orig_list + i));
			i = nextsym;
			continue;
		}
		/*
		 * If the first symbol isn't of type NOTYPE then it must
		 * be the keeper.
		 */
		if (i_type != STT_NOTYPE) {
			DEBUG_DUP_DEL((orig_list + i),
			    (orig_list + nextsym));
			continue;
		}
		/*
		 * Throw away the first one and take the new
		 * symbol
		 */
		DEBUG_DUP_DEL((orig_list + nextsym), (orig_list + i));
		i = nextsym;
	}
	if ((orig_list + i)->value > (nl + index - 1)->value)
		*(nl + index++) = *(orig_list +i);

	*sym_count = index;
}


static void
get_symtab(Elf32_Ehdr elfhdr, int fd, char *filename)
{

	Elf32_Shdr *	sym_sh, *str_sh, *shent;
	char *		sname;
	Elf32_Sym *	sp;
	unsigned long	nsym;			/* Number of symbols found */
	Elf32_Word	symsect;		/* Section table indices */
	Elf32_Word	dynsymsect;		/* dynamic symbol table index */
	int		i;



	/* First get the section header table */

	if (elfhdr.e_shnum == 0) {
		fprintf(stderr, "%s: no section header entries.\n", filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_IOERR);
	}

	if (lseek(fd, (long) elfhdr.e_shoff, L_SET) != elfhdr.e_shoff) {
		fprintf(stderr, "%s: unable to seek to section header.\n",
		    filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_IOERR);
	}
	if ((secthdr = (Elf32_Shdr *)
			malloc(elfhdr.e_shentsize*elfhdr.e_shnum)) == NULL) {
		fprintf(stderr, "%s: unable to allocate section header.\n",
		    filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_UNAVAILABLE);
	}

	if (read(fd, secthdr, elfhdr.e_shentsize * elfhdr.e_shnum) !=
		elfhdr.e_shentsize * elfhdr.e_shnum) {
		fprintf(stderr, "%s: unable to read ELF header section.\n",
		    filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_IOERR);
	}

	/*
	 * If there is a section for section names, allocate a buffer to
	 * read it and seek to its offset in the file.
	 */
	if ((elfhdr.e_shstrndx != SHN_UNDEF) && ((sname =
		(char *)malloc(secthdr[elfhdr.e_shstrndx].sh_size)) != NULL) &&
		(lseek(fd, (long) secthdr[elfhdr.e_shstrndx].sh_offset,
			L_SET) == secthdr[elfhdr.e_shstrndx].sh_offset)) {

	/*
	 * Read the section names.  If the read fails, make it look
	 * as though none of this happened.
	 */
		if (read(fd, sname, secthdr[elfhdr.e_shstrndx].sh_size) !=
			secthdr[elfhdr.e_shstrndx].sh_size) {
			(void) free(sname);
		sname = NULL;
		}
	}

	DPRINTF("\n** Section Header Table ** (%d entries)\n", elfhdr.e_shnum);


	symsect = 0;
	dynsymsect = 0;
	shent	= secthdr;		/* set to first entry */
	for (i = 0; i < (int) elfhdr.e_shnum; i++, shent++) {
			/* go thru each section header */
		if (shent->sh_type ==  SHT_SYMTAB) {
			symsect = i;
			break;
		}
		if (shent->sh_type == SHT_DYNSYM)
			dynsymsect = i;
	}
	if (symsect == 0) {
		if (dynsymsect == 0) {
			fprintf(stderr, "%s: symbol Table section not found \n",
			    filename);
			fprintf(stderr, "Exiting due to error(s)...\n");
			exit(EX_SOFTWARE);
		}
		/*
		 * There is no SYMTAB (it's been stripped), but there is
		 * a DYNSYM.  In that case we will use the DYNSYM symbol
		 * table.
		 */
		symsect = dynsymsect;
	}
	sym_sh = &secthdr[symsect];
	nsym = sym_sh->sh_size/sym_sh->sh_entsize;
	DPRINTF("\n **SYMBOL TABLE ** (%d entries)\n", nsym);
	if (sym_sh->sh_link == elfhdr.e_shnum ||
		secthdr[sym_sh->sh_link].sh_size == 0) {
		fprintf(stderr, "%s: empty string table; no symbols.\n",
		    filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_SOFTWARE);
	}
	str_sh = &secthdr[sym_sh->sh_link];
	strtab = (char *) malloc(str_sh->sh_size +
		sizeof (str_sh->sh_size) + 8);
	/* LINTED */
	*(int *) strtab = str_sh->sh_size + sizeof (str_sh->sh_size);
	lseek(fd, (long) str_sh->sh_offset, L_SET);
	if (read(fd, strtab + sizeof (str_sh->sh_size), str_sh->sh_size)
		!= str_sh->sh_size) {
		fprintf(stderr, "%s: error reading string table\n", filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_IOERR);
	}
	lseek(fd, (long)sym_sh->sh_offset, L_SET);
	symtab = (Elf32_Sym *) malloc(nsym*sizeof (Elf32_Sym));
	if (read(fd, symtab, nsym*sizeof (Elf32_Sym)) !=
	    nsym*sizeof (Elf32_Sym)) {
		fprintf(stderr, "%s: error reading symbol table\n", filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
		exit(EX_IOERR);
	}
	for (sp = symtab; sp < &symtab[nsym]; sp++) {
		if (sp->st_name) {
			sp->st_name =  (int) strtab + sp->st_name +
						sizeof (str_sh->sh_size);
		}
	}
	nl = readsyms(symtab, nsym);
	free(symtab);
	if (nl == NULL) {
		fprintf(stderr, "%s: problem reading symbol data \n", filename);
		fprintf(stderr, "Exiting due to error(s)...\n");
			exit(EX_SOFTWARE);
	}
	PRINTF("Sorting by value \n");
	qsort(nl, nname, sizeof (nltype),
		(int (*) (const void *, const void *)) compare);
	PRINTF("Removing duplicates\n");
	remove_dup_syms(nl, &nname);
}

/* These defines are taken from prof code */

/*
 * IS_EXEC ensures that the symbol is in an executable section.
 * This matters because a symbol of type STT_NOTYPE may not be
 * in an executable section
 */
#define	IS_EXEC(s)	(((s) > 0) && (secthdr[(s)].sh_flags & SHF_EXECINSTR))
/*
 * FUNC insures that the symbol should be reported.  We want
 * to report only those symbols that are functions (STT_FUNC)
 * or "notype" (STT_NOTYPE... "printf", for example).  Also,
 * unless the gflag is set, the symbol must be global.
 */

#define	IS_VOID(i) \
	(((ELF32_ST_TYPE(i) == STT_NOTYPE)) && \
	((ELF32_ST_BIND(i) == STB_GLOBAL) || \
	(ELF32_ST_BIND(i) == STB_WEAK) || \
	(!aflag && (ELF32_ST_BIND(i) == STB_LOCAL))))

#define	FUNC(i)  \
	(((ELF32_ST_TYPE(i) == STT_FUNC)) && \
	((ELF32_ST_BIND(i) == STB_GLOBAL) || \
	(ELF32_ST_BIND(i) == STB_WEAK) || \
	(!aflag && (ELF32_ST_BIND(i) == STB_LOCAL))))

#define	IS_FUNC(s, i) \
	(FUNC(i) || (IS_VOID(i) && (s < SHN_LORESERVE) && IS_EXEC(s)))


/*
 * Translate symbol table data particularly for sorting.
 * Input is the symbol table data structure, number of symbols,
 * opened ELF file, and the string table link offset.
 */
static nltype *
readsyms(Elf32_Sym *data, size_t num)
{
	nltype *	s;
	nltype *	nl_etext = NULL;
	int		etext_found;
	int		i;
	Elf32_Sym *	dp;
	extern int	aflag;
	extern nltype *	npe;

	/*
	 * This loop exists only to detect number of function names
	 * so that space is not overallocated for it
	 */
	nname = 0;
	dp = data;
	for (i = 0; i < num; i++, dp++) {
		if (IS_FUNC(dp->st_shndx, dp->st_info))
			nname++;
	}
	DPRINTF(" %d: function names found in executable \n", nname);

	/*
	 * make room for the PRF symbols
	 */
	nname += PRF_SYMCNT;
	if ((npe = (nltype *)calloc(nname+1, sizeof (nltype))) == NULL) {
		(void) fprintf(stderr, "readsyms: cannot calloc space\n");
		return (NULL);
	}

	s = npe;	/* save pointer to head of array */

	nname = 0;
	for (i = 0; i < num; i++) {
		etext_found = 1;
		if ((IS_FUNC(data->st_shndx, data->st_info) &&
		    data->st_value) || ((data->st_name) &&
		    ((etext_found = (int)strcmp((char *)data->st_name,
		    PRF_ETEXT)) == 0))) {
			npe->name = (char *) data->st_name;
			npe->value = data->st_value;
			npe->syminfo = data->st_info;
			if (etext_found == 0)
				nl_etext = npe;
			if ((lflag == TRUE) &&
			    (ELF32_ST_BIND(data->st_info) == STB_LOCAL)) {
				/*
				 * If the 'locals only' flag is on then
				 * we add the 'local' symbols to the
				 * 'exclusion' lists.
				 */
				addlist(Elist, npe->name);
				addlist(elist, npe->name);
			}
			DPRINTF("Index %d:", nname);
			DPRINTF("\tValue: 0x%x\t", npe->value);
			DPRINTF("Name: %s \n", npe->name);
			nname ++;
			npe ++;
		}

		data++;
	}	/* end for loop */

	/*
	 * setting up two dummy entries...
	 */
	if (nl_etext) {
		npe->name = PRF_EXTSYM;
		npe->value = nl_etext->value + 1;
		npe->syminfo = ELF32_ST_INFO(STB_GLOBAL, STT_FUNC);
		DPRINTF("Index %d:", nname);
		DPRINTF("\tValue: 0x%x\t", npe->value);
		DPRINTF("Name: %s \n", npe->name);
		nname ++;
		npe ++;
		npe->name = PRF_MEMTERM;
		npe->value = (unsigned long)0xffffffff;
		npe->syminfo = ELF32_ST_INFO(STB_GLOBAL, STT_FUNC);
		DPRINTF("Index %d:", nname);
		DPRINTF("\tValue: 0x%x\t", npe->value);
		DPRINTF("Name: %s \n", npe->name);
		nname ++;
		npe ++;
	}


	DPRINTF(" Returning %d function name values \n", nname);
	if (s == npe) {
		(void) fprintf(stderr,
		    "readsyms: no valid function names found in executable \n");
		return ((nltype *)NULL);
	}
	else
		return (s);
}

/*
 * compare either by name or by value for sorting.
 * This is the comparison function called by qsort to
 * sort the symbols either by name or value when requested.
 */

static int
compare(nltype *a, nltype *b)
{
	if (a->value > b->value)
		return (1);
	else
		return ((a->value == b->value) - 1);
}
