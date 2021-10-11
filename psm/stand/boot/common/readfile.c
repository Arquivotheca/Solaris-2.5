/*
 * Copyright (c) 1985-1994 Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)readfile.c	1.49	95/07/18 SMI"

#include <sys/sysmacros.h>
#include <sys/exechdr.h>
#include <sys/elf.h>
#include <sys/elf_notes.h>
#include <sys/bootconf.h>
#include <sys/reboot.h>
#include <sys/fcntl.h>
#include <sys/modctl.h>
#include <sys/link.h>
#include <sys/auxv.h>


union {
	struct exec X;
	Elf32_Ehdr Elfhdr;
} ex;

#define	x ex.X
#define	elfhdr ex.Elfhdr

typedef int	(*func_t)();

#define	FAIL	((func_t)-1)
#define	ALIGN(x, a)	\
	((a) == 0 ? (int)(x) : (((int)(x) + (a) - 1) & ~((a) - 1)))

int use_align = 0;
int npagesize = 0;

Elf32_Boot *	elfbootvec;	/* ELF bootstrap vector */
char *		module_path;	/* path for kernel modules */

/*
 * Read in a Unix executable file and return its entry point.
 * Handle the various a.out formats correctly.
 * "fd" is the standalone file descriptor to read from.
 * Print informative little messages if "print" is on.
 * Returns -1 for errors.
 */

#ifdef DEBUG
static int debug = 1;
#else DEBUG
static int debug = 0;
#endif DEBUG

#define	dprintf		if (debug) printf

static func_t 	readelf(int, int, Elf32_Ehdr *);
static func_t	iload(char *, Elf32_Phdr *, Elf32_Phdr *, auxv_t **);
static caddr_t	segbrk(caddr_t *, u_int, int);
static int	openpath(char *, char *, int);
static char *	getmodpath(char *);

extern int	get_progmemory(caddr_t, u_int, int);
extern char *	strrchr();

extern int 	verbosemode;
extern int	boothowto;
extern int	pagesize;
extern char	filename[];


#ifdef MPSAS
extern void sas_symtab(int start, int end);
#endif

func_t
readfile(int fd, int print)
{
	register int i;
	register int shared = 0;

	if (verbosemode)
		dprintf("fd = %x\n", fd);

	i = read(fd, (char *)&elfhdr, sizeof (elfhdr));
	if (x.a_magic == ZMAGIC || x.a_magic == NMAGIC)
		shared = 1;
	if (i != sizeof (elfhdr)) {
		printf("Error reading ELF header.\n");
		return (FAIL);
	}
	if (!shared && x.a_magic != OMAGIC) {
	    if (*(int *)(((Elf32_Ehdr *)&x)->e_ident) == *(int *)(ELFMAG)) {
		if (verbosemode) {
			dprintf("calling readelf, elfheader is:\n");
			dprintf("e_ident\t0x%x, 0x%x, 0x%x, 0x%x\n",
			    *(int *)&elfhdr.e_ident[0],
			    *(int *)&elfhdr.e_ident[4],
			    *(int *)&elfhdr.e_ident[8],
			    *(int *)&elfhdr.e_ident[12]);
			dprintf("e_machine\t0x%x\n", elfhdr.e_machine);
			dprintf("e_entry\t0x%x\n", elfhdr.e_entry);
			dprintf("e_shoff\t0x%x\n", elfhdr.e_shoff);
			dprintf("e_shnentsize\t0x%x\n", elfhdr.e_shentsize);
			dprintf("e_shnum\t0x%x\n", elfhdr.e_shnum);
			dprintf("e_shstrndx\t0x%x\n", elfhdr.e_shstrndx);
		}
			return (readelf(fd, print, &elfhdr));
	    } else {
		printf("File not executable.\n");
		return (FAIL);
	    }
	}
	return (FAIL);
}

/*
 * Macros to add attribute/values
 * to the ELF bootstrap vector
 * and the aux vector.
 */
#define	AUX(p, a, v)	{ (p)->a_type = (a); \
			((p)++)->a_un.a_val = (long)(v); }

#define	EBV(p, a, v)	{ (p)->eb_tag = (a); \
			((p)++)->eb_un.eb_val = (Elf32_Word)(v); }

static func_t
readelf(int fd, int print, Elf32_Ehdr *elfhdrp)
{

	Elf32_Phdr *phdr;	/* program header */
	Elf32_Nhdr *nhdr;	/* note header */
	int nphdrs, phdrsize;
	caddr_t allphdrs;
	caddr_t	namep, descp;
	u_int loadaddr, size, base;
	int off, i;
	int interp = 0;				/* interpreter required */
	static char dlname[MAXPATHLEN];		/* name of interpeter */
	u_int dynamic;				/* dynamic tags array */
	Elf32_Phdr *thdr;			/* "text" program header */
	Elf32_Phdr *dhdr;			/* "data" program header */
	func_t entrypt;				/* entry point of standalone */
	extern u_int icache_flush;

	if (elfhdrp->e_phnum == 0 || elfhdrp->e_phoff == 0)
		goto elferror;

	entrypt = (func_t)elfhdrp->e_entry;
	if (verbosemode)
		dprintf("Entry point: 0x%x\n", entrypt);

	/*
	 * Allocate and read in all the program headers.
	 */
	allphdrs = NULL;
	nhdr = NULL;
	nphdrs = elfhdrp->e_phnum;
	phdrsize = nphdrs * elfhdrp->e_phentsize;
	allphdrs = (caddr_t)kmem_alloc(phdrsize);
	if (allphdrs == NULL)
		goto elferror;
	if (verbosemode)
		dprintf("lseek: args = %x %x %x\n", fd, elfhdrp->e_phoff, 0);
	if (lseek(fd, elfhdrp->e_phoff, 0) == -1)
		goto elferror;
	if (read(fd, allphdrs, phdrsize) != phdrsize)
		goto elferror;

	/*
	 * First look for PT_NOTE headers that tell us what pagesize to
	 * use in allocating program memory.
	 */
	npagesize = 0;
	for (i = 0; i < nphdrs; i++) {
		phdr = (Elf32_Phdr *)(allphdrs + elfhdrp->e_phentsize * i);
		if (phdr->p_type != PT_NOTE)
			continue;
		if (verbosemode) {
			dprintf("allocating 0x%x bytes for note hdr\n",
				phdr->p_filesz);
		}
		nhdr = (Elf32_Nhdr *)kmem_alloc(phdr->p_filesz);
		if (nhdr == NULL)
			goto elferror;
		if (verbosemode)
			dprintf("seeking to 0x%x\n", phdr->p_offset);
		if (lseek(fd, phdr->p_offset, 0) == -1)
			goto elferror;
		if (verbosemode) {
			dprintf("reading 0x%x bytes into 0x%x\n",
				phdr->p_filesz, nhdr);
		}
		if (read(fd, (caddr_t)nhdr, phdr->p_filesz) != phdr->p_filesz)
			goto elferror;
		namep = (caddr_t)(nhdr + 1);
		if (verbosemode) {
			dprintf("p_note namesz %x descsz %x type %x\n",
				nhdr->n_namesz, nhdr->n_descsz, nhdr->n_type);
		}
		if (nhdr->n_namesz == strlen(ELF_NOTE_SOLARIS) + 1 &&
		    strcmp(namep, ELF_NOTE_SOLARIS) == 0 &&
		    nhdr->n_type == ELF_NOTE_PAGESIZE_HINT) {
			descp = namep + roundup(nhdr->n_namesz, 4);
			npagesize = *(int *)descp;
			if (verbosemode)
				dprintf("pagesize is %x\n", npagesize);
		}
		kmem_free(nhdr, phdr->p_filesz);
		nhdr = NULL;
	}

	/*
	 * Next look for PT_LOAD headers to read in.
	 */
	if (print)
		printf("Size: ");
	for (i = 0; i < nphdrs; i++) {
		phdr = (Elf32_Phdr *)(allphdrs + elfhdrp->e_phentsize * i);
		if (verbosemode) {
			dprintf("Doing header %d\n", i);
			dprintf("phdr\n");
			dprintf("\tp_offset = %x, p_vaddr = %x\n",
				phdr->p_offset, phdr->p_vaddr);
			dprintf("\tp_memsz = %x, p_filesz = %x\n",
				phdr->p_memsz, phdr->p_filesz);
		}
		if (phdr->p_type == PT_LOAD) {
			if (verbosemode)
				dprintf("seeking to 0x%x\n", phdr->p_offset);
			if (lseek(fd, phdr->p_offset, 0) == -1)
				goto elferror;

			if (phdr->p_flags & PF_X) {
				if (print)
					printf("%d+", phdr->p_filesz);
				/*
				 * If we found a new pagesize above, use it
				 * to adjust the memory allocation.
				 */
				loadaddr = phdr->p_vaddr;
				if (use_align && npagesize != 0) {
					off = loadaddr & (npagesize - 1);
					size = roundup(phdr->p_memsz + off,
						npagesize);
					base = loadaddr - off;
				} else {
					npagesize = 0;
					size = phdr->p_memsz;
					base = loadaddr;
				}
				/*
				 *  Check if it's text or data.
				 */
				if (phdr->p_flags & PF_W)
					dhdr = phdr;
				else
					thdr = phdr;

				if (verbosemode)
					dprintf("allocating memory: %x %x %x\n",
					    base, size, npagesize);
				/*
				 * We're all set up to read.
				 * Now let's allocate some memory.
				 */
				if (get_progmemory((caddr_t)base, size,
				    npagesize))
					goto elferror;

			} else if (phdr->p_vaddr == 0) {
				/*
				 * It's a PT_LOAD segment that is
				 * not executable and has a vaddr
				 * of zero.  We allocate boot memory
				 * for this segment, since we don't want
				 * it mapped in permanently as part of
				 * the kernel image.
				 */
				if ((loadaddr =
				    kmem_alloc(phdr->p_memsz)) == NULL)
					goto elferror;
				/*
				 * Save this to pass on
				 * to the interpreter.
				 */
				phdr->p_vaddr = loadaddr;
			}

			if (verbosemode) {
				dprintf("reading 0x%x bytes into 0x%x\n",
				phdr->p_filesz, loadaddr);
			}
			if (read(fd, loadaddr, phdr->p_filesz) !=
			    phdr->p_filesz)
				goto elferror;

			/* zero out BSS */
			if (phdr->p_memsz > phdr->p_filesz) {
				loadaddr += phdr->p_filesz;
				if (verbosemode) {
					dprintf("bss from 0x%x size 0x%x\n",
					    loadaddr,
					    phdr->p_memsz - phdr->p_filesz);
				}

				bzero(loadaddr, phdr->p_memsz - phdr->p_filesz);
				if (print)
					printf("%d Bytes\n",
					    phdr->p_memsz - phdr->p_filesz);
			}
#ifdef	MPSAS
			sas_symtab(phdr->p_vaddr,
				    phdr->p_vaddr + phdr->p_memsz);
#endif
		} else if (phdr->p_type == PT_INTERP) {
			/*
			 * Dynamically-linked executable.
			 */
			interp = 1;
			if (lseek(fd, phdr->p_offset, 0) == -1) {
				goto elferror;
			}
			/*
			 * Get the name of the interpreter.
			 */
			if (read(fd, dlname, phdr->p_filesz) !=
			    phdr->p_filesz ||
			    dlname[phdr->p_filesz - 1] != '\0')
				goto elferror;
		} else if (phdr->p_type == PT_DYNAMIC) {
			dynamic = phdr->p_vaddr;
		}
	}
	/*
	 * Load the interpreter
	 * if there is one.
	 */
	if (interp) {
		Elf32_Boot bootv[EB_MAX];		/* Bootstrap vector */
		auxv_t auxv[NUM_AUX_VECTORS * 2];	/* Aux vector */
		Elf32_Boot *bv = bootv;
		auxv_t *av = auxv;

		/*
		 * Load it.
		 */
		if ((entrypt = iload(dlname, thdr, dhdr, &av)) == FAIL)
			return (FAIL);
		/*
		 * Build bootstrap and aux vectors.
		 */
		EBV(bv, EB_AUXV, 0); /* fill in later */
		EBV(bv, EB_PAGESIZE, pagesize);
		EBV(bv, EB_DYNAMIC, dynamic);
		EBV(bv, EB_NULL, 0);

		AUX(av, AT_BASE, entrypt);
		AUX(av, AT_ENTRY, elfhdrp->e_entry);
		AUX(av, AT_PAGESZ, pagesize);
		AUX(av, AT_PHDR, allphdrs);
		AUX(av, AT_PHNUM, elfhdrp->e_phnum);
		AUX(av, AT_PHENT, elfhdrp->e_phentsize);
		if (npagesize)
			AUX(av, AT_SUN_LPAGESZ, npagesize);
		AUX(av, AT_SUN_IFLUSH, icache_flush);
		AUX(av, AT_NULL, 0);
		/*
		 * Realloc vectors and copy them.
		 */
		size = (caddr_t)bv - (caddr_t)bootv;
		if ((elfbootvec = (Elf32_Boot *)kmem_alloc(size)) == NULL)
			return (FAIL);
		bcopy(bootv, elfbootvec, size);

		size = (caddr_t)av - (caddr_t)auxv;
		if ((elfbootvec->eb_un.eb_ptr =
		    (Elf32_Addr)kmem_alloc(size)) == NULL)
			return (FAIL);
		bcopy(auxv, elfbootvec->eb_un.eb_ptr, size);
	} else {
		kmem_free(allphdrs, phdrsize);
	}

	return (entrypt);
elferror:
	if (allphdrs != NULL)
		kmem_free(allphdrs, phdrsize);
	if (nhdr != NULL)
		kmem_free(nhdr, phdr->p_filesz);
	printf("Elf read error.\n");
	return (FAIL);
}

/*
 * Load the interpreter.  It expects a
 * relocatable .o capable of bootstrapping
 * itself.
 */
static func_t
iload(char *rtld, Elf32_Phdr *thdr, Elf32_Phdr *dhdr, auxv_t **avp)
{
	Elf32_Ehdr *ehdr;
	u_int dl_entry = 0;
	u_int i;
	int fd;
	int size;
	caddr_t shdrs;
	caddr_t etext, edata;

	etext = (caddr_t)thdr->p_vaddr + thdr->p_memsz;
	edata = (caddr_t)dhdr->p_vaddr + dhdr->p_memsz;

	/*
	 * Get the module path.
	 */
	module_path = getmodpath(filename);

	if ((fd = openpath(module_path, rtld, O_RDONLY)) < 0) {
		printf("boot: cannot find %s\n", rtld);
		return (FAIL);
	}
	AUX(*avp, AT_SUN_LDNAME, rtld);
	/*
	 * Allocate and read the ELF header.
	 */
	if ((ehdr = (Elf32_Ehdr *)kmem_alloc(sizeof (Elf32_Ehdr))) == NULL)
		return (FAIL);

	if (read(fd, ehdr, sizeof (*ehdr)) != sizeof (*ehdr)) {
		printf("boot: error reading ELF header (%s).\n", rtld);
		return (FAIL);
	}

	size = ehdr->e_shentsize * ehdr->e_shnum;
	if ((shdrs = (caddr_t)kmem_alloc(size)) == NULL)
		return (FAIL);
	/*
	 * Read the section headers.
	 */
	if (lseek(fd, ehdr->e_shoff, 0) == -1 ||
	    read(fd, shdrs, size) != size) {
		printf("boot: error reading section headers\n");
		return (FAIL);
	}
	AUX(*avp, AT_SUN_LDELF, ehdr);
	AUX(*avp, AT_SUN_LDSHDR, shdrs);
	/*
	 * Load sections into the appropriate dynamic segment.
	 */
	for (i = 1; i < ehdr->e_shnum; i++) {
		Elf32_Shdr *sp;
		caddr_t *spp;
		caddr_t load;

		sp = (Elf32_Shdr *)(shdrs + (i*ehdr->e_shentsize));
		/*
		 * If it's not allocated and not required
		 * to do relocation, skip it.
		 */
		if (!(sp->sh_flags & SHF_ALLOC) &&
		    sp->sh_type != SHT_SYMTAB &&
		    sp->sh_type != SHT_STRTAB &&
#ifdef i386
		    sp->sh_type != SHT_REL)
#else
		    sp->sh_type != SHT_RELA)
#endif
			continue;
		/*
		 * If the section is read-only,
		 * it goes in as text.
		 */
		spp = (sp->sh_flags & SHF_WRITE)? &edata: &etext;
		/*
		 * Make some room for it.
		 */
		load = segbrk(spp, sp->sh_size, sp->sh_addralign);
		if (load == NULL) {
			printf("boot: allocating memory for sections failed\n");
			return (FAIL);
		}
		/*
		 * Compute the entry point of the linker.
		 */
		if (dl_entry == 0 &&
		    !(sp->sh_flags & SHF_WRITE) &&
		    (sp->sh_flags & SHF_EXECINSTR)) {
			dl_entry = (u_int)load + ehdr->e_entry;
		}

		/*
		 * If it's bss, just zero it out.
		 */
		if (sp->sh_type == SHT_NOBITS) {
			bzero(load, sp->sh_size);
		} else {
			/*
			 * Read the section contents.
			 */
			if (lseek(fd, sp->sh_offset, 0) == -1 ||
			    read(fd, load, sp->sh_size) != sp->sh_size) {
				printf("boot: error reading sections\n");
				return (FAIL);
			}
		}
		/*
		 * Assign the section's virtual addr.
		 */
		sp->sh_addr = (Elf32_Off)load;
	}
	/*
	 * Update sizes of segments.
	 */
	thdr->p_memsz = (Elf32_Word)etext - thdr->p_vaddr;
	dhdr->p_memsz = (Elf32_Word)edata - dhdr->p_vaddr;
	/* load and relocate symbol tables in SAS */
	close(fd);
	return ((func_t)dl_entry);
error:
	close(fd);
	printf("boot: error loading interpreter (%s)\n", rtld);
	return (FAIL);
}

/*
 * Extend the segment's "break" value by bytes.
 */
static caddr_t
segbrk(caddr_t *spp, u_int bytes, int align)
{
	caddr_t va, pva;
	int size = 0;
	unsigned int alloc_pagesize = pagesize;
	unsigned int alloc_align = 0;

	if (npagesize) {
		alloc_align = npagesize;
		alloc_pagesize = npagesize;
	}

	va = (caddr_t) ALIGN(*spp, align);
	pva = (caddr_t) roundup((u_int)*spp, alloc_pagesize);
	/*
	 * Need more pages?
	 */
	if (va + bytes > pva) {
		size = roundup((bytes - (pva - va)), alloc_pagesize);

		if (get_progmemory(pva, size, alloc_align)) {
			printf("boot: segbrk allocation failed, "
			    "0x%x bytes @ 0x%x\n", bytes, pva);
			return (NULL);
		}
	}
	*spp = va + bytes;

	return (va);
}

/*
 * Open the file using a search path and
 * return the file descriptor (or -1 on failure).
 */
static int
openpath(path, fname, flags)
char *path;
char *fname;
int flags;
{
	register char *p, *q;
	char buf[MAXPATHLEN];
	int fd;

	/*
	 * If the file name is absolute,
	 * don't use the module search path.
	 */
	if (fname[0] == '/')
		return (open(fname, flags));

	for (p = path; 1; p = q) {

		while (*p == ' ' || *p == '\t' || *p == ':')
			p++;
		if (*p == '\0')
			break;
		q = p;
		while (*q && *q != ' ' && *q != '\t' && *q != ':')
			q++;
		strncpy(buf, p, q - p);
		if (q[-1] != '/')
			buf[q - p] = '/';
		strcpy(&buf[q - p + 1], fname);

		if ((fd = open(buf, flags)) > 0)
			return (fd);
	}
	return (-1);
}

/*
 * Get the module search path.
 */
static char *
getmodpath(fname)
char *fname;
{
	register char *p = strrchr(fname, '/');
	static char mod_path[MOD_MAXPATH];
	int len;

	if (p == fname)
		p++;

	len = p - fname;
	strncpy(mod_path, fname, len);
	mod_path[len] = ' ';
	strcpy(&mod_path[len+1], MOD_DEFPATH);

	if (boothowto & RB_ASKNAME) {
		char buf[MOD_MAXPATH];

		printf("Enter default directory for modules [%s]: ", mod_path);
		gets(buf);
		if (buf[0] != '\0')
			strcpy(mod_path, buf);
	}
	return (mod_path);
}
