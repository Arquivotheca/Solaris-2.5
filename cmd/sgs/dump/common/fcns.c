/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)fcns.c	6.4	94/08/31 SMI"

#include	<stdio.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<string.h>
#include	<libelf.h>
#include	<limits.h>
#include	<sys/elf_M32.h>
#include	<sys/elf_386.h>
#include	<sys/elf_SPARC.h>
#include	"dump.h"

extern int	p_flag;
extern char *	prog_name;

/*
 * Symbols in the archive symbol table are in machine independent
 * representation.  This function translates each symbol.
 */
static long
sgetl(char * buffer)
{
	register long	w = 0;
	register int	i = CHAR_BIT * sizeof(long);

	while ((i -= CHAR_BIT) >= 0)
		w |= (long) ((unsigned char) *buffer++) << i;
	return (w);
}

/*
 * Print the symbols in the archive symbol table.
 * The function requires a file descriptor returned by
 * a call to open(), a pointer to an archive file opened with elf_begin(),
 * a pointer to the archive header of the associated archive symbol table,
 * and the name of the archive file.
 * Seek to the start of the symbol table and read it into memory.
 * Assume that the number of entries recorded in the beginning
 * of the archive symbol table is correct but check for truncated
 * symbol table.
 */
void
ar_sym_read(int fd, Elf * elf_file, Elf_Arhdr * ar_p, char * filename)
{
	long    here;
	long	symsize;
	long	num_syms;
	char    *offsets;
	long    n;
	typedef unsigned char    word[4];
	word *ar_sym;

	(void)printf("%s:\n", filename);

	if(!p_flag)
	{
		(void)printf("     **** ARCHIVE SYMBOL TABLE ****\n");
		(void)printf("%-8s%s\n\n", "Offset", "Name" );
	}

	if ((symsize = ar_p->ar_size) == 0)
	{
		(void)fprintf( stderr, "%s: %s: cannot read symbol table header\n",
			prog_name, filename);
		return;
	}
	if ((ar_sym = (word *)malloc(symsize * sizeof(char))) == NULL)
	{
		(void)fprintf(stderr, "%s: %s: could not malloc space\n", prog_name, filename);
		return;
	}

	here = elf_getbase(elf_file);
	if( (lseek(fd, here, 0) ) != here)
	{
		(void)fprintf(stderr, "%s: %s: could not lseek\n", prog_name, filename);
		return;
	}

	if( (read(fd, ar_sym, symsize * sizeof(char) )) == -1)
	{
		(void)fprintf(stderr, "%s: %s: could not read\n", prog_name, filename);
		return;
	}

	num_syms = sgetl((char *)ar_sym);
	ar_sym++; 
	offsets = (char *)ar_sym;
	offsets += (num_syms)*sizeof(long);

	for( ; num_syms; num_syms--, ar_sym++) {
		(void) printf("%-8ld", sgetl((char *)ar_sym));
		if ((n = strlen(offsets)) == NULL) {
			(void) fprintf(stderr, "%s: %s: premature EOF\n",
				prog_name, filename);
			return;
		}
		(void) printf("%s\n", offsets);
		offsets += n + 1;
	}
	return;
}

/*
 * Print the program execution header.  Input is an
 * opened ELF or COFF object file, the number of structure
 * instances in the header as recorded in the ELF header, and the filename.
 */
void
dump_exec_header(elf_file, nseg, filename)
Elf      *elf_file;
unsigned nseg;
char     *filename;
{
	Elf32_Phdr  	*p_phdr;
	int		counter;
	extern int      v_flag, p_flag;
	extern char     *prog_name;

	if(!p_flag)
	{
		(void)printf(" ***** PROGRAM EXECUTION HEADER *****\n");
		(void)printf("Type        Offset      Vaddr       Paddr\n");
		(void)printf("Filesz      Memsz       Flags       Align\n\n");
	}

	if ( (p_phdr = elf32_getphdr(elf_file)) == 0)
	{
		/*(void)fprintf(stderr,"%s: %s: problem reading program exec header\n",
			prog_name, filename);*/
		return;
	}

	for (counter=0; counter<nseg; counter++)
	{
		if(p_phdr == 0)
		{
			(void)fprintf(stderr, "%s: %s: premature EOF on program exec header\n",
				prog_name, filename);
			return;
		}

		if(!v_flag)
		{
			(void)printf("%-12d%-#12x%-#12x%-#12x\n%-#12x%-#12x%-12lu%-#12x\n\n",
				(int)p_phdr->p_type,
				p_phdr->p_offset,
				p_phdr->p_vaddr,
				p_phdr->p_paddr,
				(unsigned long)p_phdr->p_filesz,
				(unsigned long)p_phdr->p_memsz,
				(unsigned long)p_phdr->p_flags,
				(unsigned long)p_phdr->p_align);
		}
		else
		{
			switch (p_phdr->p_type)
			{
				case PT_NULL: (void)printf("%-12s", "NULL"); break;
				case PT_LOAD: (void)printf("%-12s", "LOAD"); break;
				case PT_DYNAMIC: (void)printf("%-12s", "DYN"); break;
				case PT_INTERP: (void)printf("%-12s", "INTERP"); break;
				case PT_NOTE: (void)printf("%-12s", "NOTE"); break;
				case PT_PHDR: (void)printf("%-12s", "PHDR"); break;
				case PT_SHLIB: (void)printf("%-12s", "SHLIB"); break;
				default:
					(void)printf("%-12d", (int)p_phdr->p_type);break;
			}
			(void)printf("%-#12x%-#12x%-#12x\n%-#12x%-#12x",
				p_phdr->p_offset,
				p_phdr->p_vaddr,
				p_phdr->p_paddr,
				(unsigned long)p_phdr->p_filesz,
				(unsigned long)p_phdr->p_memsz);

			switch (p_phdr->p_flags)
			{
				case 0: printf("%-12s", "---"); break;
				case PF_X: (void)printf("%-12s", "--x"); break;
				case PF_W: (void)printf("%-12s", "-w-"); break;
				case PF_W+PF_X: (void)printf("%-12s", "-wx"); break;
				case PF_R: (void)printf("%-12s", "r--"); break;
				case PF_R+PF_X: (void)printf("%-12s", "r-x"); break;
				case PF_R+PF_W: (void)printf("%-12s", "rw-"); break;
				case PF_R+PF_W+PF_X: (void)printf("%-12s", "rwx"); break;
				default:
					(void)printf("%-12d", p_phdr->p_flags);break;
			}
			(void)printf("%-#12x\n\n", (unsigned long)p_phdr->p_align);
		}
		p_phdr++;
	}
}

