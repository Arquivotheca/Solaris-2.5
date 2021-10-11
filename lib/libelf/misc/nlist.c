/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)nlist.c	1.5	92/07/17 SMI" 	/* SVr4.0 1.13	*/

/*LINTLIBRARY*/

#include "libelf.h"
#include <nlist.h>
#if COFF_NLIST_SUPPORTED
#include "aouthdr.h"
#include "filehdr.h"
#include "scnhdr.h"
#include "reloc.h"
#endif /* COFF_NLIST_SUPPORTED */
#include "linenum.h"
#include "syms.h"

#undef  BADMAG
#define BADMAG(x)	(!ISCOFF(x))

#ifndef FLEXNAMES
#	define FLEXNAMES 1
#endif
#undef n_name		/* this patch causes problems here */

#ifndef __STDC__
#define const
#endif

#define SPACE 100		/* number of symbols read at a time */
#define ISELF (strncmp(magic_buf, ELFMAG, SELFMAG) == 0)

#if COFF_NLIST_SUPPORTED
static char sym_buf[SPACE * SYMESZ];
static int num_in_buf = 0;
static char *next_entry = (char *)0;
#endif /* COFF_NLIST_SUPPORTED */

extern long lseek();
extern int open(), read(), close(), strncmp(), strcmp();

#ifdef __STDC__
	static int _elf_nlist(int, struct nlist *);
#if COFF_NLIST_SUPPORTED
	static int _coff_nlist(int, struct nlist *);
#endif /* COFF_NLIST_SUPPORTED */
	static int end_elf_job(int, Elf *);
#if COFF_NLIST_SUPPORTED
	static void sym_close(int);
	static int sym_read(int, struct syment *, int);
	static int fill_sym_buf(int, int);
#endif /* COFF_NLIST_SUPPORTED */
#else
	static int _elf_nlist();
#if COFF_NLIST_SUPPORTED
	static int _coff_nlist();
#endif /* COFF_NLIST_SUPPORTED */
	static int end_elf_job();
#if COFF_NLIST_SUPPORTED
	static void sym_close();
	static int sym_read();
	static int fill_sym_buf();
#endif /* COFF_NLIST_SUPPORTED */
#endif

int
nlist(name, list)
	const char *name;
	struct nlist *list;
{
	register struct nlist *p;
	char magic_buf[5];
	int fd;

	for (p = list; p->n_name && p->n_name[0]; p++) { /* n_name can be ptr */
		p->n_type = 0;
		p->n_value = 0L;
		p->n_scnum = 0;
		p->n_sclass = 0;
		p->n_numaux = 0;
	}
	
	if ((fd = open(name, 0)) < 0)
		return(-1);
	if(read(fd, magic_buf, 4) == -1) {
		(void) close(fd);
		return(-1);
	}
	magic_buf[4] = '\0';
	if (lseek(fd, 0L, 0) == -1L) { /* rewind to beginning of object file */
		(void) close(fd);
		return (-1);
	}

	if ( ISELF )
		return _elf_nlist(fd, list);
	else
#if COFF_NLIST_SUPPORTED
		return _coff_nlist(fd, list);
#else /* COFF_NLIST_SUPPORTED */
		return (-1);
#endif /* COFF_NLIST_SUPPORTED */
}

static int
_elf_nlist(fd, list)
	int fd;
	struct nlist *list;
{
	Elf	   *elfdes;	/* ELF descriptor */
	Elf32_Shdr *s_buf;	/* buffer storing section header */
	Elf_Data   *symdata;	/* buffer points to symbol table */
	Elf_Scn    *secidx = 0;	/* index of the section header table */
	Elf32_Sym  *sym;	/* buffer storing one symbol information */
	Elf32_Sym  *sym_end;	/* end of symbol table */
	unsigned   strtab;	/* index of symbol name in string table */

	if (elf_version(EV_CURRENT) == EV_NONE) {
		(void) close(fd);
		return (-1);
	}
	elfdes = elf_begin(fd, ELF_C_READ, (Elf *)0);
	if (elf32_getehdr(elfdes) == (Elf32_Ehdr *)0)
		return end_elf_job (fd, elfdes);

	while ((secidx = elf_nextscn(elfdes, secidx)) != 0) {
		if ((s_buf = elf32_getshdr(secidx)) == (Elf32_Shdr *)0)
			return end_elf_job (fd, elfdes);
		if (s_buf->sh_type != SHT_SYMTAB) /* not symbol table */
			continue;
		symdata = elf_getdata(secidx, (Elf_Data *)0);
		if (symdata == 0)
			return end_elf_job (fd, elfdes);
		if (symdata->d_size == 0)
			break;
		strtab = s_buf->sh_link;
		sym = (Elf32_Sym *) (symdata->d_buf);
		sym_end = sym + symdata->d_size / sizeof(Elf32_Sym);
		for ( ; sym < sym_end; ++sym) {
			struct nlist *p;
			register char *name;
			name = elf_strptr(elfdes, strtab, (size_t)sym->st_name);
			if (name == 0)
				continue;
			for (p = list; p->n_name && p->n_name[0]; ++p) {
				if (strcmp(p->n_name, name))
					continue;
				p->n_value = sym->st_value;
				p->n_type = ELF32_ST_TYPE(sym->st_info);
				p->n_scnum = sym->st_shndx;
				break;
			}
		}
		break;
		/* Currently there is only one symbol table section
		** in an object file, but this restriction may be
		** relaxed in the future.
		*/
	}
	(void) elf_end(elfdes);
	(void) close(fd);
	return 0;
}

static int
end_elf_job (fd, elfdes)
	int fd;
	Elf *elfdes;
{
	(void) elf_end (elfdes);
	(void) close (fd);
	return (-1);
}

#if COFF_NLIST_SUPPORTED
static int
_coff_nlist(fd, list)
	int fd;
	struct nlist *list;
{
#ifdef __STDC__
	extern void *malloc();
#else
	extern char *malloc();
#endif
	extern void free();
	struct	filehdr	buf;
	struct	syment	sym;
	long n;
	int bufsiz=FILHSZ;
#if FLEXNAMES
	char *strtab = (char *)0;
	long strtablen;
#endif
	register struct nlist *p;
	register struct syment *q;
	long	sa;

	if(read(fd, (char *)&buf, bufsiz) == -1) {
		(void) close(fd);
		return(-1);
	}

	if (BADMAG(buf.f_magic))
	{
		(void) close(fd);
		return (-1);
	}
	sa = buf.f_symptr;	/* direct pointer to sym tab */
	if (lseek(fd, (long)sa, 0) == -1L) {
		(void) close(fd);
		return (-1);
	}
	q = &sym;
	n = buf.f_nsyms;	/* num. of sym tab entries */

	while (n)
	{
		if(sym_read(fd, &sym, SYMESZ) == -1) {
			sym_close(fd);
			return(-1);
		}
		n -= (q->n_numaux + 1L);
			for (p = list; p->n_name && p->n_name[0]; ++p)
			{
				if (p->n_value != 0L && p->n_sclass == C_EXT)
					continue;
				/*
				* For 6.0, the name in an object file is
				* either stored in the eight long character
				* array, or in a string table at the end
				* of the object file.  If the name is in the
				* string table, the eight characters are
				* thought of as a pair of longs, (n_zeroes
				* and n_offset) the first of which is zero
				* and the second is the offset of the name
				* in the string table.
				*/
#if FLEXNAMES
				if (q->n_zeroes == 0L)	/* in string table */
				{
					if (strtab == (char *)0) /* need it */
					{
						long home = lseek(fd, 0L, 1);
						if (home == -1L) {
							sym_close(fd);
							return (-1);
						}
						if (lseek(fd, buf.f_symptr +
							buf.f_nsyms * SYMESZ,
							0) == -1 || read(fd,
							(char *)&strtablen,
							sizeof(long)) !=
							sizeof(long) ||
							(strtab = (char *)malloc(
							(unsigned)strtablen))
							== (char *)0 ||
							read(fd, strtab +
							sizeof(long),
							strtablen -
							sizeof(long)) !=
							strtablen -
							sizeof(long) ||
							strtab[strtablen - 1]
							!= '\0' ||
							lseek(fd, home, 0) ==
							-1)
						{
							(void) lseek(fd,home,0);
							sym_close(fd);
							if (strtab != (char *)0)
								free(strtab);
							return (-1);
						}
					}
					if (q->n_offset < sizeof(long) ||
						q->n_offset >= strtablen)
					{
						sym_close(fd);
						if (strtab != (char *)0)
							free(strtab);
						return (-1);
					}
					if (strcmp(&strtab[q->n_offset],
						p->n_name))
					{
						continue;
					}
				}
				else
#endif /*FLEXNAMES*/
				{
					if (strncmp(q->_n._n_name,
						p->n_name, SYMNMLEN))
					{
						continue;
					}
				}
				p->n_value = q->n_value;
				p->n_type = q->n_type;
				p->n_scnum = q->n_scnum;
				p->n_sclass = q->n_sclass;
				break;
			}
	}
#if FLEXNAMES
	sym_close(fd);
	if (strtab != (char *)0)
		free(strtab);
#endif
	return (0);
}

static void sym_close(fd)
int fd;
{
	num_in_buf = 0;
	next_entry = (char *)0; 

	(void) close(fd);
}


static int sym_read(fd, sym, size)	/* buffered read of symbol table */
int fd;
struct syment *sym;
int size; 
{
#ifdef __STDC__
	extern void *memcpy();
#else
	extern char *memcpy();
#endif
	long where = 0L;

	if ((where = lseek(fd, 0L, 1)) == -1L) {
		sym_close(fd);
		return (-1);
	}

	if(!num_in_buf) {
		if(fill_sym_buf(fd, size) == -1) return(-1);
	}
	(void) memcpy((char *)sym, next_entry, size);
	num_in_buf--;

	if(sym->n_numaux && !num_in_buf) {
		if(fill_sym_buf(fd, size) == -1) return(-1);
	}
	if ((lseek(fd, where + SYMESZ + (AUXESZ * sym->n_numaux), 0)) == -1L) {
		sym_close(fd);
		return (-1);
	}
	size += AUXESZ * sym->n_numaux;
	num_in_buf--;

	next_entry += size;
	return(0);
}

static int
fill_sym_buf(fd, size)
int fd;
int size;
{
	if((num_in_buf = read(fd, sym_buf, size * SPACE)) == -1) 
		return(-1);
	num_in_buf /= size;
	next_entry = &(sym_buf[0]);
	return(0);
}
#endif /* COFF_NLIST_SUPPORTED */
