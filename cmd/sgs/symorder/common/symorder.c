/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)symorder.c	1.14	93/11/23 SMI"

#include <stdio.h>

#ifdef ELF_OBJ
#include <libelf.h>
#include <sys/file.h>
#include <fcntl.h>
#define BADELF 2
#else
#include <a.out.h>
#endif

struct	symlist	{
	char	*name;			/* Name of the symbol */
#ifdef ELF_OBJ
	Elf32_Sym 	*sym;		/* Elf Symbol table entry */
#else
	struct	nlist	*sym;		/* Ptr to its symbol table entry */
#endif
};
#define	SYMLIST_SIZE	100		/* Start with this much */
#define streq(a, b)	(strcmp(a, b) == 0)
#define strdup(str)	strcpy(malloc(strlen(str) + 1), str)

struct	symlist	*symlist;		/* Head of the list */
int	symlist_size;			/* Current size of symlist[] */
int	insert_pt;			/* Current insertion point */
int	silent;				/* Keep quiet about missing symbols */
int 	compare();
char	*cmdname;
char	*strcpy();
char	*malloc();
#ifdef ELF_OBJ
char	*elf_copy_string();
#else
char	*copy_string();
#endif

#ifdef ELF_OBJ
int	elf_process_sym_tab();
void	elf_write_symtab();
#endif

main(argc, argv)
int	argc;
char	*argv[];
{
	cmdname = argv[0];
	argc--;
	argv++;
	while (argc > 0 && argv[0][0] == '-') {
		switch (argv[0][1]) {
		case 's':
			silent = 1;
			break;
		}
		argc--;
		argv++;
	}
			
	if (argc != 2) {
		error("Usage: %s [-s] symsfile objectfile", cmdname);
	}
	read_syms2order(argv[0]);
#ifdef ELF_OBJ
	(void)elf_order_syms(argv[1]);
#else
	order_syms(argv[1]);
#endif
	exit(0);
}

/*
 * Read the list of symbols to move to the head of the class.
 */
read_syms2order(file)
char	*file;
{
	FILE	*fp;
	char	name[100];

	fp = fopen(file, "r");
	if (fp == NULL) {
		error("Could not open %s", file);
	}
	while (fscanf(fp, "%s", name) == 1) {
		insert(name);
	}
	fclose(fp);
}

/*
 * Insert another name into the list of symbols to look for.
 */
insert(name)
char	*name;
{
	int	size;

	if (symlist_size == 0) {
		symlist_size = SYMLIST_SIZE;
		size = symlist_size * sizeof(struct symlist);
		symlist = (struct symlist *) calloc(size, 1);
	}
	if (insert_pt >= symlist_size) {
		symlist_size += SYMLIST_SIZE;
		size = symlist_size * sizeof(struct symlist);
		symlist = (struct symlist *) realloc(symlist, size);
	}
	symlist[insert_pt].name = strdup(name);
	symlist[insert_pt].sym = NULL;
	insert_pt++;
}

#ifndef ELF_OBJ
/*
 * Re-order the symbols.
 * Move the requested symbols to the beginning of the symbol table.
 * Do not otherwise disturb the order of the remaining symbols.
 */
order_syms(objfile)
char	*objfile;
{
	FILE	*fp;
	struct	exec	hdr;
	struct	nlist	*syms;
	struct 	nlist	*symp;
	char	*strings;
	int	stringtabsize;
	int	nsyms;
	int	i;

	if ((fp = fopen(objfile, "r+")) == NULL) {
		error("Could not open %s", objfile);
	}
	if ((fread(&hdr, sizeof(hdr), 1, fp)) != 1 || N_BADMAG(hdr)) {
		error("%s is not an object file", objfile);
	}
	if (hdr.a_syms == 0) {
		error("%s is stripped", objfile);
	}
	syms = (struct nlist *) malloc(hdr.a_syms);
	fseek(fp, N_SYMOFF(hdr), 0);
	nsyms = hdr.a_syms/sizeof(struct nlist);
	fread(syms, sizeof(struct nlist), nsyms, fp); 
	fread(&stringtabsize, sizeof(stringtabsize), 1, fp);
	strings = malloc(stringtabsize);
	fseek(fp, N_STROFF(hdr), 0);
	fread(strings, stringtabsize, 1, fp);

	for (symp = syms; symp < &syms[nsyms]; symp++) {
		if (symp->n_un.n_strx > stringtabsize) {
			fprintf(stderr, "symbol %d - string index too large %d\n", i, symp->n_un.n_strx);
		} else {
			check_inlist(symp, strings);
		}
	}
	qsort(symlist, insert_pt, sizeof(struct symlist), compare);
	fseek(fp, N_SYMOFF(hdr), 0);
	write_symtab(fp, syms, nsyms, strings, stringtabsize);

	fclose(fp);
	free(syms);
	free(strings);
}

#else
/*
 * Re-order the symbols.
 * Move the requested symbols to the beginning of the symbol table.
 * Do not otherwise disturb the order of the remaining symbols.
 */
elf_order_syms(objfile)
char *objfile;
{
	int 		fd;
	Elf		*elf_file;
	Elf_Kind	file_type;
	Elf_Cmd		cmd;


        if(elf_version(EV_CURRENT) == EV_NONE) {
                (void)fprintf(stderr,"%s: %s: Libelf is out of date\n",
                 	cmdname, objfile);
                exit(BADELF);
        }

        if ((fd = open((objfile), O_RDWR)) == -1) {
                (void)fprintf(stderr, "%s:  %s: cannot read/write file\n", 
					cmdname, objfile);
		return;
        }
        
        cmd = ELF_C_RDWR;

        if ( (elf_file = elf_begin(fd, cmd, (Elf *) 0)) == NULL ) {
                (void)fprintf(stderr, "%s: %s: %s\n", cmdname, objfile, 
					elf_errmsg(-1));
                return;
        }
	
        file_type = elf_kind(elf_file);

        if(file_type == ELF_K_AR)
        {
               	(void)fprintf(stderr, "%s: %s: archive file\n", cmdname, 
					objfile);
		return;
        }

        else

        {
                if ( (file_type == ELF_K_ELF) || (file_type == ELF_K_COFF) )
                {
			(void)elf_process_sym_tab(elf_file, objfile,fd);
		}
		
		else
		{
			(void)fprintf(stderr, "%s: %s: invalid file type\n",
                                cmdname, objfile);
                        elf_end(elf_file);
                        (void)close(fd);
                }
        }
        elf_end(elf_file);
        (void)close(fd);
	return;
}

#endif

#ifdef ELF_OBJ

/* Get the ELF header and, if it exists, begin processing 
 * of the file; otherwise, return from  processing the 
 * file with a warning.
 */
static 
elf_process_sym_tab (elf_file, filename, fd)
Elf 	*elf_file;
char	*filename;
int	fd;
{
	Elf32_Ehdr    *p_ehdr;
	Elf32_Sym     *psymtab, *ptmpsymtab;
        Elf32_Shdr    *secthdr, *sym_sh, *shent, *str_sh;
        char          *sname;
	Elf32_Sym     *sp;
        unsigned long nsym;                     /* Number of symbols found */
        Elf32_Word    symsect, strsect;         /* Section table indices */
	char	      *strings;
	char	      *strtab;			/* string table */
        int           i;
	int	      strtabsize;


	p_ehdr =  elf32_getehdr(elf_file);

	if (p_ehdr == (Elf32_Ehdr *)0) {
                (void)fprintf(stderr, "%s: %s: %s\n", cmdname, filename, 
					elf_errmsg(-1));
                return;
	}

	if (p_ehdr->e_shnum == 0) {
		fprintf(stderr, "No section header entries.\n");
		return;
	}
	
	if (lseek(fd, (long) p_ehdr->e_shoff, L_SET) != p_ehdr->e_shoff) {
                fprintf(stderr, "Unable to seek to section header.\n");
                return;
        }
        if ((secthdr = (Elf32_Shdr *)
                        malloc(p_ehdr->e_shentsize*p_ehdr->e_shnum)) == NULL) {
                fprintf(stderr, "Unable to allocate section header.\n");
                return;
        }
	
	if (read(fd, secthdr, p_ehdr->e_shentsize * p_ehdr->e_shnum) !=
                p_ehdr->e_shentsize * p_ehdr->e_shnum) {
                fprintf(stderr,"Unable to read ELF header section.\n");
                return;
        }
 
        /* If there is a section for section names, allocate a buffer to
         *  read it and seek to its offset in the file.
         */
        if ((p_ehdr->e_shstrndx != SHN_UNDEF) && ((sname =
                (char *)malloc(secthdr[p_ehdr->e_shstrndx].sh_size)) != NULL) &&
                (lseek(fd, (long) secthdr[p_ehdr->e_shstrndx].sh_offset,
                        L_SET) == secthdr[p_ehdr->e_shstrndx].sh_offset)) {
 
        /* Read the section names.  If the read fails, make it look
         * as though none of this happened.
         */
                if (read(fd, sname, secthdr[p_ehdr->e_shstrndx].sh_size) !=
                        secthdr[p_ehdr->e_shstrndx].sh_size) {
                        (void) free(sname);
                sname = NULL;
                }
        }

        shent   = secthdr;              /* set to first entry */
        for (i = 0; i < (int) p_ehdr->e_shnum; i++, shent++ ){
                        /* go thru each section header */
                if (shent->sh_type ==  SHT_SYMTAB) {
                        symsect = i;
                        strsect = shent->sh_link;
                break;
                }
        }
        if (symsect == 0) {
                fprintf(stderr, " Symbol Table section not found \n");
                return;
        }
        sym_sh = &secthdr[symsect];
        nsym = sym_sh->sh_size/sym_sh->sh_entsize;
	str_sh = &secthdr[sym_sh->sh_link];
	strtabsize = str_sh->sh_size ;
	strtab = (char *)malloc(strtabsize);
        lseek(fd, (long)sym_sh->sh_offset, L_SET);
	if (read(fd, strtab , str_sh->sh_size) != str_sh->sh_size) {
		fprintf(stderr, "Warning - error reading string table\n");
		return;
	}
        lseek(fd, (long)sym_sh->sh_offset, L_SET);
        psymtab = (Elf32_Sym *) malloc(nsym*sizeof(Elf32_Sym));
        if (read(fd, psymtab, nsym*sizeof(Elf32_Sym))!=nsym*sizeof(Elf32_Sym)) {
                fprintf(stderr, "Warning - error reading symbl table\n");
                return;
        }
	ptmpsymtab = psymtab;
	ptmpsymtab++;
        for(sp=ptmpsymtab ; sp< &ptmpsymtab[nsym-1]; sp++) {
	        if(sp->st_name) {
			strings = (char *)elf_strptr(elf_file, strsect, 
							sp->st_name);
			check_inlist(sp, strings);
                }

        }
	qsort(symlist, insert_pt, sizeof(struct symlist), compare);
	ptmpsymtab = psymtab;
	elf_write_symtab(fd, ptmpsymtab, nsym-1, strtab, sym_sh,
				str_sh, strtabsize, elf_file, strsect);
	free(psymtab);
	free(strtab);
	return;
}

#endif

#ifdef ELF_OBJ

/*
 * Write the symbol table in the new order.
 * First, walk the symlist writing each symbol.
 * Then walk the list again writing the intervening symbols.
 */

static void
elf_write_symtab(fd, psymtab, nsym, strtab, sym_sh,
		     str_sh, strtabsize, elf_file, strsect)
int		fd;
Elf32_Sym	*psymtab;
unsigned long	nsym;
char		*strtab;
Elf32_Shdr	*sym_sh;
Elf32_Shdr	*str_sh;
int		strtabsize;
Elf		*elf_file;
Elf32_Word	strsect;
{

	register	struct symlist *sl;
	char		*newstrtab;
	char	 	*src;
	int		total;
	int	 	n;
	
	newstrtab = malloc(strtabsize);
	src = newstrtab;
	total = 0;
	lseek(fd, (long)sym_sh->sh_offset, L_SET);
	write(fd, psymtab, sizeof(Elf32_Sym));
	psymtab++;
	for (sl = symlist ; sl <&symlist[insert_pt]; sl++) {
		if (sl->sym != NULL) {
			src = elf_copy_string(newstrtab, src, strtab, sl->sym, 1,strsect,elf_file);
			write(fd, sl->sym, sizeof(Elf32_Sym));
			total++;
		}
		else if (!silent) {
			fprintf(stderr, "Symbol `%s' not found\n",
					sl->name);
		}
	}
	if (total == 0) {
		error("No symbols found!");
	}
	insert_pt = total;
	n = symlist[0].sym - psymtab ;
	src = elf_copy_string(newstrtab, src, strtab, psymtab, n,strsect,elf_file);
	write(fd, psymtab, n*sizeof(Elf32_Sym));
	total += n;

	for (sl = symlist; sl <&symlist[insert_pt - 1]; sl++) {
		n = sl[1].sym - sl->sym - 1;
		src = elf_copy_string(newstrtab, src, strtab, &sl->sym[1], n,strsect,elf_file);
		write(fd, &sl->sym[1], n*sizeof(Elf32_Sym));
		total += n;
	}
	
	sl = &symlist[insert_pt - 1];
	n = nsym - (sl->sym - psymtab + 1) ;
	src = elf_copy_string(newstrtab, src, strtab, &sl->sym[1], n,strsect,elf_file);
	write(fd, &sl->sym[1], n*sizeof(Elf32_Sym));
	*src = '\0';
	total += n;
	if (total != nsym) 
		error ("Symbol botch was %d now is %d", nsym, total);
	if ((src - newstrtab)  != strtabsize) {
		error("String table botch - was %d now is %d", strtabsize,
		    (src - newstrtab));
	}
	lseek(fd, (long) str_sh->sh_offset, L_SET) ;
	write(fd, newstrtab, strtabsize);
	return;
}

#else

/*
 * Write the symbol table in the new order.
 * First, walk the symlist writing each symbol.
 * Then walk the list again writing the intervening symbols.
 */
write_symtab(fp, syms, nsyms, strings, stringsize)
FILE	*fp;
struct	nlist	*syms;
int	nsyms;
char	*strings;
int	stringsize;
{
	register struct	symlist	*sl;
	char	*newstrings;
	char	*to;
	int	tot;
	int	n;

	newstrings = malloc(stringsize);
	to = newstrings;
	tot = 0;
	for (sl = symlist; sl < &symlist[insert_pt]; sl++) {
		if (sl->sym != NULL) {
			to = copy_string(newstrings, to, strings, sl->sym, 1);
			fwrite(sl->sym, sizeof(struct nlist), 1, fp);
			tot++;
		} else if (!silent) {
			fprintf(stderr, "Symbol `%s' not found\n",
			    sl->name);
		}
	}
	if (tot == 0) {
		error("No symbols found!");
	}

	/*
	 * "tot" counts the number of symbols which were found.
	 * Set "insert_pt" to "tot", so that symbols that weren't
	 * found are eliminated from the list.
	 */
	insert_pt = tot;

	n = symlist[0].sym - syms;
	to = copy_string(newstrings, to, strings, syms, n);
	fwrite(syms, sizeof(struct nlist), n, fp);
	tot += n;

	for (sl = symlist; sl < &symlist[insert_pt - 1]; sl++) {
		n = sl[1].sym - sl->sym - 1;
		to = copy_string(newstrings, to, strings, &sl->sym[1], n);
		fwrite(&sl->sym[1], sizeof(struct nlist), n, fp);
		tot += n;
	}
	sl = &symlist[insert_pt - 1];
	n = nsyms - (sl->sym - syms + 1);
	to = copy_string(newstrings, to, strings, &sl->sym[1], n);
	fwrite(&sl->sym[1], sizeof(struct nlist), n, fp);
	tot += n;
	if (tot != nsyms) {
		error("Symbol botch was %d now is %d", nsyms, tot);
	}
	if (((to - newstrings + sizeof(int) + 3 ) & ~3) != stringsize) {
		error("String table botch - was %d now is %d", stringsize,
		    to - newstrings + sizeof (int) );
	}
	fwrite(&stringsize, sizeof(stringsize), 1, fp);
	fwrite(newstrings, stringsize - sizeof(int), 1, fp);
}

#endif

/*
 * Check if a symbol is in the list to be re-ordered.
 */
check_inlist(symp, strings)
#ifdef ELF_OBJ
Elf32_Sym	*symp;
#else
struct	nlist	*symp;
#endif
char	*strings;
{
	struct	symlist	*sl;

	for (sl = symlist; sl < &symlist[insert_pt]; sl++) {
#ifdef ELF_OBJ
		if (streq(sl->name, strings)) {
#else
		if (streq(sl->name, &strings[symp->n_un.n_strx])) {
#endif
			sl->sym = symp;
		}
	}
}


/*
 * Qsort comparison routine.
 * Null symbol pointers are greater than any valid symbol pointer,
 * so symbols which aren't found float to the end of the table.
 */
compare(a, b)
struct	symlist	*a;
struct	symlist	*b;
{
		if (a->sym == NULL) {
			if (b->sym != NULL) 
				return(1);
			 else 
				 return(0); 
		} else if (b->sym == NULL) {
			return(-1);
		} else if (a->sym < b->sym) {
			return(-1);
		} else if (a->sym == b->sym) {
			return(0);
		} else {
			return(1);
		}
	}

#ifndef ELF_OBJ

/*
 * Copy a symbol's name into the new string table.
 * Update the string index in the symbol.
 * Do this for 'n' symbols.
 */
char *
copy_string(newstrings, to, strings, sym, n)
char	*newstrings;
register char	*to;
char	*strings;
struct	nlist	*sym;
int	n;
{
	register char	*from;
	int	newix;
	int	ix;
	int	i;

	for (i = 0; i < n; i++) {
		ix = sym->n_un.n_strx;
		if (ix != 0) {
			newix = to - newstrings + sizeof(int);
			from = &strings[ix];
			while ((*to++ = *from++) != '\0')
				;
			sym->n_un.n_strx = newix;
		}
		sym++;
	}
	return(to);
}

#else

char *
elf_copy_string(newstrings, to, strings, sym, n,strsect,elf_file)
char	*newstrings;
register char	*to;
char	*strings;
Elf32_Sym	*sym ;
Elf		*elf_file;
Elf32_Word	strsect;
int	n;
{
	register char	*from;
	Elf32_Word	newix;
	static int first_string = 1;
	int	i;

	for (i = 0; i < n; i++) {
		if (sym->st_name) {
			from = (char *)elf_strptr(elf_file, strsect,sym->st_name); 
			if (first_string) {
				*to++ = '\0';
				first_string = 0;
			}
			sym->st_name = to - newstrings;
			while ((*to++ = *from++) != '\0')
				;
		}
		sym++;
	}
	return(to);
}
#endif

error(str, a, b, c, d, e)
char	*str;
{
	fprintf(stderr, "%s: ", cmdname);
	fprintf(stderr, str, a, b, c, d, e);
	fprintf(stderr, "\n");
	exit(1);
}

