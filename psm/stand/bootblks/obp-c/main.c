/*
 * Copyright (c) 1985-1994, Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)main.c	1.11	95/07/14 SMI"

#include <sys/types.h>
#include <sys/elf.h>
#include <sys/param.h>

#include <sys/platnames.h>
#include <sys/boot_redirect.h>

#include "cbootblk.h"

static unsigned long read_elf_file(int fd, char *);

static int
bbopen(char *pathname, void *arg)
{
#ifdef DEBUG
	puts("** Try: "); puts(pathname); puts("\n");
#endif
	return (openfile(arg, pathname));
}

void
main(void *ptr)
{
	unsigned long load;
	char *dev;
	int fd, count, once = 0;
	char letter[2];
	static char fullpath[MAXPATHLEN];

	fw_init(ptr);
#if defined(DEBUG) || defined(lint)
	puts(ident);
#endif
	dev = getbootdevice(0);
retry:
	fd = open_platform_file(fscompname, bbopen, dev, fullpath, 0);
	if (fd != -1) {
#ifdef DEBUG
		puts("** ELF load ");
		puts(dev); puts(" "); puts(fullpath); puts("\n");
#endif
		load = read_elf_file(fd, fullpath);
		(void) closefile(fd);
		exitto((void *)load, ptr);
		/*NOTREACHED*/
	}

	/*
	 * PSARC/1994/396: Try for a slice redirection file.
	 */
	if (once == 0 &&
	    (fd = openfile(dev, BOOT_REDIRECT)) != -1) {
		once = 1;
		seekfile(fd, (off_t)0);
		count = readfile(fd, letter, 1);
		(void) closefile(fd);
		if (count == 1) {
			letter[1] = '\0';
			dev = getbootdevice(letter);
#ifdef DEBUG
			puts("** Redirection device ");
			puts(dev); puts("\n");
#endif
			goto retry;
			/*NOTREACHED*/
		}
	}

#ifdef notdef
	/*
	 * Finally, try for the old program.
	 * XXX Delete this before beta!
	 */
	(void) strcpy(fullpath, fscompname);
	if ((fd = openfile(dev, fullpath)) != -1) {
		puts("Warning: loading boot program from /");
		puts(" (should come from /platform)\n");
		load = read_elf_file(fd, fullpath);
		(void) closefile(fd);
		exitto((void *)load, ptr);
		/*NOTREACHED*/
	}
#endif
	puts("bootblk: can't find the boot program\n");
}

static unsigned long
read_elf_file(int fd, char *file)
{
	Elf32_Ehdr elfhdr;
	Elf32_Phdr phdr;	/* program header */
	register int i;

	seekfile(fd, (off_t)0);
	if (readfile(fd, (char *)&elfhdr, sizeof (elfhdr)) != sizeof (elfhdr))
		goto bad;
	if (elfhdr.e_ident[EI_MAG0] != ELFMAG0 ||
	    elfhdr.e_ident[EI_MAG1] != ELFMAG1 ||
	    elfhdr.e_ident[EI_MAG2] != ELFMAG2 ||
	    elfhdr.e_ident[EI_MAG3] != ELFMAG3 ||
	    elfhdr.e_phnum == 0)
		goto bad;

	for (i = 0; i < (int)elfhdr.e_phnum; i++) {
		seekfile(fd,
		    (off_t)(elfhdr.e_phoff + (elfhdr.e_phentsize * i)));
		if (readfile(fd, (char *)&phdr, sizeof (phdr)) < sizeof (phdr))
			goto bad;
		if (phdr.p_type != PT_LOAD)
			continue;
		seekfile(fd, (off_t)phdr.p_offset);
		if (readfile(fd, (char *)phdr.p_vaddr, (int)phdr.p_filesz) <
		    phdr.p_filesz)
			goto bad;
		if (phdr.p_memsz > phdr.p_filesz)
			bzero((caddr_t)phdr.p_vaddr + phdr.p_filesz,
			    (size_t)(phdr.p_memsz - phdr.p_filesz));
	}
	return ((unsigned long)elfhdr.e_entry);
bad:
	(void) closefile(fd);
	puts("bootblk: "); puts(file); puts(" - not an ELF file\n");
	exit();
	/*NOTREACHED*/
}
