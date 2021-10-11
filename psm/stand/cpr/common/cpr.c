/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)cpr.c 1.38     93/10/18 SMI"

/*
 * This module contains the boot portion of checkpoint-resume
 * All code in this module are platform independent.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/fs/ufs_fs.h>
#include <sys/elf.h>
#include <sys/pte.h>
#include <sys/vnode.h>
#include <sys/bootconf.h>
#include <sys/promif.h>
#include <sys/clock.h>		  /* for COUNTER_ADDR */
#include <sys/eeprom.h>		 /* for EEPROM_ADDR */
#include <sys/memerr.h>		 /* for MEMERR_ADDR */
#include <sys/intreg.h>		 /* for INTREG_ADDR */
#include <sys/comvec.h>
#include <sys/fcntl.h>
#include <sys/cpr.h>
#include <sys/cpr_impl.h>

/* XXX: Move to cpr_impl.c later */
#include <sys/mmu.h>

#define	MAXVALUE 0xFFFFFFFF
						/* From cpr_compress.c */
extern uint_t cpr_decompress(uchar_t *, uint_t, uchar_t *);
extern void srmmu_mmu_setctxreg();
extern void cpr_set_nvram(char *boot_file, char *silent);
extern pfn_is_valid(u_int pfn);
extern struct memlist *fill_memlists(char *name, char *prop);

void cpr_relocate_promem(void);
void cpr_mem_update(void);
void cpr_set_bootmap(void);
static void update_memlist(char *name, char *prop, struct memlist **);
static void cpr_create_nonvirtavail(struct memlist **);
static int cpr_is_prom_mem(u_int pa);
static void cpr_sort_memlist(struct memlist *, struct memlist **);

#define	CPR_MAX_MEMLIST		8
struct memlist mlbuf[CPR_MAX_MEMLIST];

typedef int (*cpr_func_t)();

/* Global variables */
int totbitmaps, totpages;
u_char reserved_va[MMU_PAGESIZE];	/* XXX: Hard coded for now */
u_char compressed_data[CPR_MAXCONTIG*MMU_PAGESIZE];
u_char machdep_buf[1024];		/* XXX: Hard coded for now */
u_int  mapva, ptva, free_va;	/* temp virtual address used for mapping */
struct cpr_bitmap_desc *bitmap_desc;
struct bootops *bootops;
int bad_pg, bad_pg_desc, compressed, no_compress, no_va, not_zero, zero_page;
int bad_pg_magic, good_pg, bad_compress;
int cur_bitmap_inx;	/* Current bitmap to search for a free page */
u_int  cur_pfn;		/* Current pfn to start searching */

int use_align;
int cpr_debug;

/* XXXX: need to clean this up!!! */
struct sun4m_machdep {
	u_int mmu_ctp;
	u_int mmu_ctx;
};

struct sun4m_machdep *mach_info;

#define	RELOCATE_BOOT		0
#define	RELOCATE_CPRBOOT	1

/*
 * ufsboot may grow its memory usage in the middle of read. Reserve extra
 * pages for it.
 */
#define	BOOT_SCRATCHMEM_GROW	(8 * MMU_PAGESIZE)

char target_bootname[80];
char target_bootargs[80];

/*
 * main routine for resuming from a checkpoint state file
 * performs the following sequentially:
 *
 *	- finds and opens the state file
 *	- reads in elf header
 *	- reads in memory usage bitmap
 *	- relocate boot, cprboot and prom out of the way
 *	- call machine dependent routine (MMU setup)
 *	- read in physical memory image
 *	- jump back into kernel
 *
 * to resume from checkpoint: boot cpr /CPR
 */


union sunromvec *romp;

main(union sunromvec *iromp)
{
	cpr_func_t cpr_resume;
	caddr_t	cpr_thread;
	struct cpr_terminator ct_tmp; /* cprboot version of terminator */
	u_int cpr_pfn;
	caddr_t	qsavp;
	int i, npages = 0, n_read = 0;
	char pathname[CPR_PATHMAX];
	int error = 0;
	int fd;

#ifdef OLD
	errp("op2_bootargs=%s\n", iromp->obp.op2_bootargs);
#endif OLD

	ct_tmp.tm_cprboot_start.tv_sec = prom_gettime() / 1000;

	prom_printf("\033[p");
	prom_printf("\014");
	prom_printf("\033[1P");
	prom_printf("\033[18;21H");
	prom_printf("Restoring the System. Please Wait... ");


#ifdef REPEART_CPRBOOT
	strcpy(pathname, CPR_STATE_FILE);
#else REPEART_CPRBOOT
	if (cpr_process_bootfile(pathname)) {
		errp("cannot read cprboot info, please do a normal boot\n");
			return (-1);
	}
#endif REPEART_CPRBOOT


	/*
	 * For Sunergy only since Sunergy prom doesn't turn on cache;
	 * We do it here to speed things up. Other platforms are no-op.
	 */
	turn_cache_on();

	if ((fd = open(pathname, O_RDONLY)) == -1) {
		errp("can't open %s, please do a normal reboot\n", pathname);
		return (-1);
	}

	/* XXX: Have to decide what error to return later */
	if ((error = cpr_read_headers(fd, &cpr_resume,
		&cpr_thread, &cpr_pfn, &qsavp)) != 0)
		return (-1);

	if ((error = cpr_read_machdep(fd)) != 0)
		return (-1);

	if ((error = cpr_read_bitmap(fd)) != 0)
		return (-1);

	/*
	 * map in the first page of kernel resume code, need to allocate
	 * mapping early since prom_map may allocate memory and all memory
	 * allocation needs to be done in the very begining so we will have
	 * an easier time relocate them.
	 */
	DEBUG4(errp("Mapping kernel page: va=%x  pfn=%x\n",
		cpr_resume, cpr_pfn));
	if (prom_map(MMU_L3_BASE(cpr_resume), 0, PN_TO_ADDR(cpr_pfn),
		MMU_PAGESIZE) == 0)
		errp("PROM_MAP resume kernel page failed\n");

	/*
	 * reserve mapping for two page aligned virtual addresses,
	 * this will be used later for buffering.
	 */
	if ((mapva = (u_int)prom_map(0, 0, 0, MMU_PAGESIZE)) == 0)
		errp("PROM_MAP mapva page failed\n");

	if ((free_va = (u_int)prom_map(0, 0, 0,
		(CPR_MAXCONTIG * MMU_PAGESIZE))) == 0)
		errp("PROM_MAP free_va failed\n");

	if ((ptva = (u_int)prom_map((caddr_t)0, 0, 0, MMU_PAGESIZE)) == 0)
		errp("PROM_MAP ptva page failed\n");

	cpr_set_bootmap();

	cpr_relocate_page_tables();

	/*
	 * Relocate boot and cprboot.
	 */
	if (cpr_relocate_boot(RELOCATE_BOOT) != 0)
		return (-1);

	DEBUG4(errp("*** Boot relocated\n"));

	if (cpr_relocate_boot(RELOCATE_CPRBOOT) != 0)
		return (-1);

	DEBUG4(errp("*** cproot relocated\n"));

	/*
	 * THERE CAN BE NO MORE PROM MEMORY ALLOCATION AFTER THIS POINT!!!
	 * Violating this rule will result in severe bodily harm to
	 * the coder!
	 */
	cpr_relocate_promem();

	/*
	 * read in pages
	 *
	 * XXX: this is inline because I suspect that we
	 *	  have exceeded the max reg stack win allowed
	 *	+ Yes, the trap stuff was not setup so we fail
	 */
	while (good_pg < totpages) {

		if (cpr_read_phys_page(fd) != -1) {
			n_read++;
		} else {
			errp("phys page read err: read=%d good=%d total=%d\n",
				n_read, good_pg, totpages);
			prom_enter_mon();
		}
	}
	DEBUG4(errp("Read=%d totpages=%d no_compress=%d compress=%d\n",
		good_pg, totpages, no_compress, compressed));

	if ((error = cpr_read_terminator(fd, &ct_tmp)) != 0)
		return (-1);
	/* NO MORE time exhaustive activities after this */

	mach_info = (struct sun4m_machdep *)&machdep_buf;

	/*
	 * remap the kernel entry page again, the mapping may be chgd
	 */
	prom_unmap(MMU_L3_BASE(cpr_resume), MMU_PAGESIZE);
	if (prom_map(MMU_L3_BASE(cpr_resume), 0, PN_TO_ADDR(cpr_pfn),
		MMU_PAGESIZE) == 0)
		errp("remapping resume kernel page failed\n");

	/*
	 * jump back to the kernel
	 */
	DEBUG4(errp("Jmp to kernel ... ctp=%x ctx=%x tp=%x pc=%x\n",
	mach_info->mmu_ctp, mach_info->mmu_ctx, cpr_thread, cpr_resume));

	(*cpr_resume)(mach_info->mmu_ctp, mach_info->mmu_ctx,
		cpr_thread, qsavp);
}

int
cpr_read_cprinfo(int fd, char *buf, int magic)
{
	struct cprinfo ci;

	read(fd, (char *)&ci, sizeof (struct cprinfo));

	if (ci.ci_magic != magic)
		return (-1);

	strcpy(buf, ci.ci_path);

	return (0);
}

int
cpr_process_bootfile(char *buf)
{
	struct cprinfo ci;
	int fd;

	/*
	 * set the boot-file (or alike) to default
	 */
	if ((fd = open(CPRINFO_DFLTBOOT, O_RDONLY)) != -1) {
		read(fd, (char *)&ci, sizeof (struct cprinfo));
		if (ci.ci_magic == CPRINFO_DFLTBOOT_MAGIC)
			cpr_set_nvram(ci.ci_bootfile, 0);
	}

	/*
	 * figure out the correct pathname and open the state file for reading.
	 */
	if (((fd = open(CPRINFO_GEN, O_RDONLY)) == -1 ||
	    cpr_read_cprinfo(fd, buf, CPRINFO_GENERIC_MAGIC)) &&
	    ((fd = open(CPRINFO_TURBO, O_RDONLY)) == -1 ||
	    cpr_read_cprinfo(fd, buf, CPRINFO_TURBO_MAGIC)))
		return (-1);

	return (0);
}

/*
 * Read the elf and cpr dump header
 */
static int
cpr_read_headers(int fd, cpr_func_t *cpr_resume, caddr_t *cpr_thread,
		int *cpr_pfn, caddr_t *qsavp)
{
	int error = 0;

	/* Read elf header */
	if ((error = cpr_read_elf(fd)) != 0)
		return (error);

	/* Read cpr dump descriptor and fill in cpr_resume, resume start pc */
	error = cpr_read_cdump(fd, cpr_resume, cpr_thread, cpr_pfn, qsavp);

	return (error);
}

/*
 * Read and verify the elf header.
 */
static int
cpr_read_elf(int fd)
{
	Elf32_Ehdr elfhdr;

	if (read(fd, &elfhdr, sizeof (Elf32_Ehdr)) < sizeof (Elf32_Ehdr)) {
		errp("cpr_read_elf: Error reading the elf header\n");
		return (-1);
	}
	if (elfhdr.e_type != ET_CORE) {
		errp("cpr_read_elf: Wrong elf header type\n");
		return (-1);
	}
	return (0);
}


/*
 * Read and verify cpr dump descriptor.
 * Return -1 upon error; otherwise return 0.
 */
static int
cpr_read_cdump(int fd, cpr_func_t *rtnpc, caddr_t *rtntp,
u_int *rtnpfn, caddr_t *qsavp)
{
	cdd_t cdump;	  /* cpr verison of dump descriptor */

	if (read(fd, &cdump, sizeof (cdd_t)) < sizeof (cdd_t)) {
		errp("cpr_read_cdump: Error reading cpr dump descriptor\n");
		return (-1);
	}
	if (cdump.cdd_magic != CPR_DUMP_MAGIC) {
		errp("cpr_read_cdump: Bad dump Magic %x\n", cdump.cdd_magic);
		return (-1);
	}
	if ((totbitmaps = cdump.cdd_bitmaprec) < 0) {
		errp("cpr_read_cdump: bad bitmap %d\n", cdump.cdd_bitmaprec);
		return (-1);
	}
	if ((totpages = cdump.cdd_dumppgsize) < 0) {
		errp("cpr_read_cdump: Bad pg tot %d\n", cdump.cdd_dumppgsize);
		return (-1);
	}

	cpr_debug = cdump.cdd_debug;

	*rtnpc = (cpr_func_t)cdump.cdd_rtnpc;
	*rtnpfn = cdump.cdd_rtnpc_pfn;
	*rtntp = cdump.cdd_curthread;
	*qsavp = cdump.cdd_qsavp;

	DEBUG4(errp("cpr_read_cdump: Rtn pc=%x, tp=%x, pfn=%x, qsavp=%x\n",
		*rtnpc, *rtntp, *rtnpc, *qsavp));

	return (0);
}

/*
 * Read and verify cpr dump terminator.
 * Return -1 upon error; otherwise return 0.
 */
int
cpr_read_terminator(int fd, struct cpr_terminator *ctp)
{
	struct cpr_terminator ct_saved, *cp; /* terminator from the statefile */


	if ((cpr_read(fd, &ct_saved, sizeof (struct cpr_terminator), 0))
		!= sizeof (struct cpr_terminator)) {
		errp("cpr_read_terminator: err reading cpr terminator\n");
		return (-1);
	}
	if (ct_saved.magic != CPR_TERM_MAGIC) {
		errp("cpr_read_terminator: bad terminator magic %x (v.s %x)\n",
			ct_saved.magic, CPR_TERM_MAGIC);
		return (-1);
	}

	prom_unmap(mapva, MMU_PAGESIZE);

	if (prom_map((caddr_t)mapva, 0, PN_TO_ADDR(ct_saved.pfn),
		MMU_PAGESIZE) == 0)
		errp("PROM_MAP terminator kernel page failed va=%x, pa=%x\n",
			MMU_L3_BASE(ct_saved.va), PN_TO_ADDR(ct_saved.pfn));

	/*
	 * add the offset to reach the terminator in the kernel so that we
	 * can directly change the retored kernel image.
	 */
	cp = (struct cpr_terminator *)(mapva + ct_saved.va -
		MMU_L3_BASE(ct_saved.va));

	cp->real_statef_size = ct_saved.real_statef_size;
	cp->tm_shutdown = ct_saved.tm_shutdown;
	cp->tm_cprboot_start.tv_sec = ctp->tm_cprboot_start.tv_sec;
	cp->tm_cprboot_end.tv_sec = prom_gettime() / 1000;

	return (0);
}

/*
 * Read and verify cpr machdep info.
 * Return -1 upon error; otherwise return 0.
 */
static int
cpr_read_machdep(int fd)
{
	cmd_t cmach;

	if (read(fd, &cmach, sizeof (cmd_t)) < sizeof (cmd_t)) {
		errp("cpr_read_machdep: Err reading cpr machdep descriptor\n");
		return (-1);
	}
	if (cmach.md_magic != CPR_MACHDEP_MAGIC) {
		errp("cpr_read_machdep:Bad machdep magic %x\n", cmach.md_magic);
		return (-1);
	}
	if (read(fd, &machdep_buf, cmach.md_size) < cmach.md_size) {
		errp("cpr_read_machdep: Error reading machdep info\n");
		return (-1);
	}
	DEBUG4(errp("cpr_read_machdep: Machdep info size %d\n", cmach.md_size));

	return (0);
}

/*
 * Read in the memory usage bitmap so we know which
 * physical pages are free for us to use
 */
cpr_read_bitmap(int fd)
{
	int i, bitmapsize;
	char *bitmap, *auxbitmap;
	cbd_t *bp;

	/*
	 * dynamically allocate the bitmap desc array
	 * Read in the bitmap descriptor follow by the actual bitmap
	 */
	bitmap_desc = (cbd_t *)prom_alloc((caddr_t)0,
		totbitmaps * sizeof (cbd_t), 0);

	for (i = 0; i < totbitmaps; i++) {
		bp = &bitmap_desc[i];
		if (read(fd, bp, sizeof (cbd_t))
			< sizeof (cbd_t)) {
			errp("cpr_read_bitmap: Err reading bitmap des %d\n", i);
			return (-1);
		}

		bitmapsize = bp->cbd_size;

		/*
		 * dynamically allocate the bitmap for both the map we are
		 * about to read and the map we will use for relocation.
		 */
		bitmap = prom_alloc((caddr_t)0, bitmapsize, 0);
		auxbitmap = prom_alloc((caddr_t)0, bitmapsize, 0);
		bp->cbd_bitmap = bitmap;
		bp->cbd_auxmap = auxbitmap; /* boot mem actg */

		if (read(fd, bitmap, bitmapsize) < bitmapsize) {
			errp("cpr_read_bitmap: err reading bitmap %d\n", i);
			return (-1);
		}

		if (bp->cbd_magic != CPR_BITMAP_MAGIC) {
			errp("cpr_read_bitmap: Bitmap %d BAD MAGIC %x\n", i,
				bp->cbd_magic);
			return (-1);
		}
	}
	return (0);
}


/*
 * rellocate and remap any prom page that will be used by the incoming kernel.
 */
void
cpr_relocate_promem()
{
	struct memlist *vmem, *nvmavail = NULL;
	u_int pa, va, pfn, opfn;
	struct pte *ptep;
	int cnt = 0;

#ifdef NOTNOW
	/*
	 * This is a nice to have procedure to make sure the memlist is
	 * updated. Due to the difficulty of referencing fill_memlists() in
	 * the libraries, we defer this implementation to a later time.
	 * we may have change the prom mem usage. update them now.
	 */
	cpr_mem_update();
#endif NOTNOW
	cpr_create_nonvirtavail(&nvmavail);

	/*
	 * move and remap any conflict prom page used by coming kernel
	 */
	for (vmem = nvmavail; vmem; vmem = vmem->next) {
		/*
		 * address + size can be MAXVALUE so check u_int overflow
		 */
		for (va = vmem->address; va >= vmem->address &&
			va < (vmem->address + vmem->size); va += MMU_PAGESIZE) {
			/*
			 * check if it belongs to prom mem
			 */
			if (!cpr_is_prom_mem(va_to_pa(va)))
				continue;

			ptep = (struct pte *)cpr_srmmu_ptefind((caddr_t)va);
			if (ptep == (struct pte *)-1)
				continue;

			opfn = cpr_va_to_pfn(va);

			if ((pfn = cpr_relocate_page(opfn, ptep, va)) == opfn)
				continue;

			if (pfn == -1) {
			errp("cpr_relocate_promem:fail to reloc va=%x pfn=%x\n",
				va, cpr_va_to_pfn(va));
				continue;
			}

			DEBUG4(errp("reloc'ing promem: va=%x opfn=%x npfn=%x\n",
				va, opfn, pfn));
			cnt++;
		}
	}
	DEBUG4(errp("\n**** %d prom pages were relocated\n", cnt));
}

void
cpr_mem_update()
{
	struct memlist *pfreelistp, *vfreelistp;

	update_memlist("virtual-memory", "available", &vfreelistp);
	update_memlist("memory", "available", &pfreelistp);

	bootops->boot_mem->physavail = pfreelistp;
	bootops->boot_mem->virtavail = vfreelistp;
}

static void
update_memlist(char *name, char *prop, struct memlist **list)
{
	if (prom_getversion() > 0) {
		/* Just take another prom snapshot */
#ifdef NOTNOW
		*list = fill_memlists(name, prop);
#endif NOTNOW
	}
}

static void
cpr_create_nonvirtavail(struct memlist **nvmavail)
{
	struct memlist *vmem, *sortvmem = NULL;
	u_int newsize;
	int i = 0;

	/*
	 * sort the virtavail mem list
	 */
	for (vmem = bootops->boot_mem->virtavail; vmem; vmem = vmem->next) {
		cpr_sort_memlist(vmem, &sortvmem);
	}
	/*
	 * XXX hardcode max virtual MAXVALUE
	 */
	for (vmem = sortvmem; vmem; vmem = vmem->next) {
		u_int size, nsize, addr, naddr;
		if (i >= CPR_MAX_MEMLIST) {
			errp("cpr_create_nonvirtavail: not enough mlbuf\n");
			return;
		}
		addr = vmem->address;
		size = vmem->size;
		/*
		 * last one
		 */
		if (vmem->next == NULL) {
			if ((newsize = MAXVALUE - (addr + size)) == 0)
				continue;
			mlbuf[i].size = newsize;
			mlbuf[i].address = addr + size;
			mlbuf[i].next = *nvmavail;
			*nvmavail = &mlbuf[i];

			i++;
			continue;
		}

		naddr = vmem->next->address;
		nsize = vmem->next->size;

		/*
		 * special processing for the 1st one
		 */
		if (vmem == sortvmem && addr != 0) {
			mlbuf[i].size = addr;
			mlbuf[i].address = 0;
			mlbuf[i].next = *nvmavail;
			*nvmavail = &mlbuf[i];

			i++;
			/* fall through to make up # of loops needed */
		}
		/*
		 * the middle ones
		 */
		if ((newsize = naddr - (addr + size)) == 0)
			continue;
		mlbuf[i].size = newsize;
		mlbuf[i].address = addr + size;
		mlbuf[i].next = *nvmavail;
		*nvmavail = &mlbuf[i];
		i++;
	}
	DEBUG4(errp("cpr_create_nonvirtavail: # of memlists = %d\n", i));
}

static int
cpr_is_prom_mem(u_int pa)
{
	struct memlist *pp;

	/*
	 * if the page doesn't have a mapped virtual addr, assume it's
	 * ok to use.
	 */
	if (pa == MAXVALUE)
		return (0);

	for (pp = bootops->boot_mem->physavail; pp; pp = pp->next) {
		if ((pa >= pp->address) && pa < (pp->address + pp->size))
			return (1);
	}
	return (0);
}

static void
cpr_sort_memlist(struct memlist *seg, struct memlist **smem)
{
	struct memlist *mp;

	/*
	 * add seg to the head of the smem list
	 */
	if (*smem == NULL || seg->address < (*smem)->address) {
		seg->next = *smem;
		*smem = seg;
		return;
	}
	for (mp = *smem; mp; mp->next) {
		/*
		 * add seg to the tail of the smem list
		 */
		if (seg->address >= mp->address && mp->next == NULL) {
			seg->next = mp->next;
			mp->next = seg;
			return;
		}
		/*
		 * insert seg into middle of the list
		 */
		if (seg->address >= mp->address &&
		    seg->address < mp->next->address) {
			seg->next = mp->next;
			mp->next = seg;
			return;
		}
	}
	/* NOTREACHED */
	DEBUG4(errp("cpr_sort_memlist: cannot sort entry %x\n", seg));
}

static int
cpr_read_phys_page(int fd)
{
	uint_t len;
	cpd_t cpgdesc;		 /* cpr page descriptor */
	caddr_t datap;
	ulong cpr_va, cpr_pa;
	u_int rtn_bufp;	/* ptr return from cpr_read() */

	/* First read page descriptor */
	if ((cpr_read(fd, &cpgdesc, sizeof (cpd_t), &rtn_bufp))
		!= sizeof (cpd_t)) {
		errp("cpr_read_phys_page: Error reading page desc\n");
		bad_pg_desc++;
		return (-1);
	}

	if (cpgdesc.cpd_magic != CPR_PAGE_MAGIC) {
		errp("cpr_read_phys_page: Page BAD MAGIC cpg=%x\n", &cpgdesc);
		errp("BAD MAGIC (%x) should be (%x)\n",
			cpgdesc.cpd_magic, CPR_PAGE_MAGIC);
		bad_pg_magic++;
		return (-1);
	}

	/*
	 * Get physical address, should be page aligned.
	 */
	cpr_pa = PN_TO_ADDR(cpgdesc.cpd_pfn);

	DEBUG4(errp("about to read: pa=%x pfn=%x len=%x\n",
		cpr_pa, cpgdesc.cpd_pfn, cpgdesc.cpd_length));


	/*
	 * XXX: Map the physical page to the virtual address,
	 * and read into the virtual.
	 * XXX: There is potential problem in the OBP, we just
	 * want to predefine an virtual address for it, so that
	 * OBP won't mess up any of its own memory allocation.
	 * REMEMBER to change it later !
	 */
	prom_unmap(free_va, (cpgdesc.cpd_page * MMU_PAGESIZE));
	if ((u_int)prom_map((caddr_t)free_va, 0, cpr_pa,
		(cpgdesc.cpd_page * MMU_PAGESIZE)) == 0)
		errp("PROM MAP failed : cpr_pa %x\n", cpr_pa);

	/*
	 * Copy non-compressed data directly to the mapped vitrual;
	 * for compressed data, decompress them directly from
	 * the cpr_read() buffer to the mapped virtual.
	 */
	if (!(cpgdesc.cpd_flag & CPD_COMPRESS))
		datap = (caddr_t)free_va;
	else
		datap = NULL;

	if (cpr_read(fd, datap, cpgdesc.cpd_length, &rtn_bufp)
		!= cpgdesc.cpd_length) {
		errp("cpr_read_phys_page: Err reading page: len %d\n",
			cpgdesc.cpd_length);
		bad_pg++;
		return (-1);
	}

	/* Decompress data into physical page directly */
	if (cpgdesc.cpd_flag & CPD_COMPRESS) {
		len = cpr_decompress((u_char *)rtn_bufp, cpgdesc.cpd_length,
		    (uchar_t *)free_va);

		if (len != (cpgdesc.cpd_page * MMU_PAGESIZE)) {
		errp("cpr_read_page: bad decompressed len %d comprsed len %d\n",
				len, cpgdesc.cpd_length);
			bad_compress++;
			return (-1);
		}
		compressed += cpgdesc.cpd_page;
	} else {
		no_compress += cpgdesc.cpd_page;
	}
	good_pg += cpgdesc.cpd_page;

	DEBUG4(errp("Read: pa=%x pfn=%x\n", cpr_pa, cpgdesc.cpd_pfn));

	return (0);
}

int
cpr_find_free_page()
{
	int i, j;
	register u_int npfn;
	cbd_t *bp;

	if (cur_bitmap_inx >= totbitmaps) {
		errp("cpr_find_free_page: ran out of free pages\n");
		return (-1);
	}

	for (i = cur_bitmap_inx; i < totbitmaps; i++) {
		bp = &bitmap_desc[i];
		for (j = cur_pfn; j < bp->cbd_size * NBBY; j++) {

			npfn = bp->cbd_spfn + j;
			if (cpr_kpage_is_free(npfn) &&
			    cpr_bpage_is_free(npfn)) {

				cpr_boot_setbit(npfn);
				cur_pfn = j + 1;

				if (cur_pfn < bp->cbd_size * NBBY)
					cur_bitmap_inx = i;
				else {  /* roll over to next bitmap */
					cur_bitmap_inx = i + 1;
					cur_pfn = 0;
				}
	DEBUG4(errp("cpr_find_free_page: npfn %x cur_pfn %x cur_bitmap %d\n",
			npfn, cur_pfn, cur_bitmap_inx));

				return (npfn);
			}
		}
	}
	errp("cpr_find_free_page: no free page found (cbi=%x)\n",
		cur_bitmap_inx);
	return (-1);
}

static int
cpr_setbit(int pfn, int kernel)
{
	cbd_t *bm;
	char *mapp;
	int i;

	for (i = 0; i < totbitmaps; i++) {
		bm = &bitmap_desc[i];
		if ((pfn >= bm->cbd_spfn) && (pfn <= bm->cbd_epfn)) {
			if (kernel)
				mapp = bm->cbd_bitmap;
			else
				mapp = bm->cbd_auxmap;
			if (isclr(mapp, (pfn - bm->cbd_spfn))) {
				setbit(mapp, (pfn - bm->cbd_spfn));
				return (0);
			} else
				return (-1);
		}
	}
	return (-1);
}

int
cpr_kernel_setbit(pfn)
{
	return (cpr_setbit(pfn, 1));
}

int
cpr_boot_setbit(pfn)
{
	return (cpr_setbit(pfn, 0));
}

static
page_is_free(int pfn, int kernel)
{
	cbd_t *bm;
	int bit, i;

	for (i = 0; i < totbitmaps; i++) {
		bm = &bitmap_desc[i];
		if ((pfn >= bm->cbd_spfn) && (pfn <= bm->cbd_epfn)) {
		if (kernel)
			bit = isset(bm->cbd_bitmap, (pfn - bm->cbd_spfn));
		else
			bit = isset(bm->cbd_auxmap, (pfn - bm->cbd_spfn));

		if (bit)
			return (0);
		else
			return (1);
		}
	}
	return (1);
}

int
cpr_kpage_is_free(int pfn)
{
	return (page_is_free(pfn, 1));
}

int
cpr_bpage_is_free(int pfn)
{
	return (page_is_free(pfn, 0));
}

/*
 * page relocation routine
 *
 * if this physical page is being used by the incoming kernel then
 * we move it to the next free page that is not being used by kernel
 * or boot.
 */
int
cpr_relocate_page(u_int pfn, u_int pte_pa, u_int va)
{
	register u_int pte;
	register u_int npfn;
	u_int pa, nva, npa;
	u_int i, j;

	if (cpr_kpage_is_free(pfn))
		return (pfn);

	if ((npfn = cpr_find_free_page()) == -1) {
		errp("cpr_relocate_page: cannot relocate pfn %x\n", pfn);
		return (-1);
	}
	/*
	 * we need to watch for aliasing problem here
	 * on platforms with virtual caches
	 */
	pa = PN_TO_ADDR(pfn);
	npa = PN_TO_ADDR(npfn);

	DEBUG4(errp("reloc pa=%x to npa=%x: va=%x pfn=%x npfn=%x pte_pa=%x\n",
		pa, npa, va, pfn, npfn, pte_pa));

	prom_unmap(mapva, MMU_PAGESIZE);
	if ((u_int) prom_map((caddr_t) mapva, 0, npa, MMU_PAGESIZE) != mapva) {
		errp("cpr_relocate_page: mapva failed\n");
		return (-1);
	}
	move_page(va, mapva);

	/*
	 * need to convert a pfn to a ptp and store
	 * it back to the page table.  Have to make sure
	 * that all important variables are in registers
	 *
	 * be careful not to change the code sequence here!
	 * if you don't know what you are doing you may cause
	 * same pte be allocated twice.
	 */
	pte = ldphys(pte_pa);
	pte = (pte & 0xff) | (npfn << 8);
	stphys(pte_pa, pte);

	srmmu_mmu_flushpage(va);

	return (npfn);
}

/*
 * tag pages in use from the boot side in the boot memory usage map,
 * this includes all pages in use by prom, /boot and /cprboot,
 * the algorithm for find prom pages is physinstalled - physavail.
 */
void
cpr_set_bootmap()
{
	register u_int va;
	caddr_t start, end;
	struct memlist *pavail, *pmem;
	caddr_t memaddr;
	u_int spa, epa, pa, pfn, cnt, i, j;
	cbd_t *bm;
	char *bits;

	/*
	 * Get the latest snapshot of memory layout from the
	 * prom before figuring out pages used by the prom.
	 */
	if (BOP_GETPROPLEN(bootops, "memory-update") == 0)
		BOP_GETPROP(bootops, "memory-update", NULL);

	for (pavail = (struct memlist *)bootops->boot_mem->physavail;
		pavail; pavail = pavail->next) {
		spa = pavail->address;
		epa = pavail->address + pavail->size;

		DEBUG4(errp("Phys avail addr %x  epa %x\n", spa, epa));

		for (pa = spa; pa < epa; pa += MMU_PAGESIZE) {
			pfn = ADDR_TO_PN(pa);
			if ((pfn != -1) && ((pfn & 0x70000) == 0)) {
				cpr_boot_setbit(pfn);
				cnt++;
			}
		}
	}
	DEBUG4(errp("*** %d pages not mark for prom pages ***\n", cnt));

	for (i = 0; i < totbitmaps; i++) {
		bm = &bitmap_desc[i];
		for (j = 0, bits = bm->cbd_auxmap; j < bm->cbd_size; j++)
			bits[j] = ~bits[j];
	}

	/*
	 * tag all pages used by /boot
	 */
	/*
	 * Kludge for in case ufsboot does not export the properties.
	 */
	if ((BOP_GETPROP(bootops, "boot-start", &start) != 0) ||
	    (BOP_GETPROP(bootops, "boot-end", &end) != 0)) {
		start = (caddr_t) 0x100000;
		end = (caddr_t) 0x250000;
	}
	end += BOOT_SCRATCHMEM_GROW;
	DEBUG4(errp("Tag auxmap for boot start %x end %x\n", start, end));

	for (va = (u_int)start; va <= (u_int)end; va += MMU_PAGESIZE) {
		if ((pfn = cpr_va_to_pfn(va)) != (u_int) -1) {
			DEBUG4(errp("Tagging pfn=%x\n", pfn));
			if (cpr_boot_setbit(pfn))
				errp("failed to tag auxmap for pfn=%x", pfn);
		}
	}

	/*
	 * tag all pages used by /cprboot
	 */
	cpr_getprop(bootops, "cprboot-start", &start);
	cpr_getprop(bootops, "cprboot-end", &end);

	DEBUG4(errp("Tag auxmap for cprboot start %x end %x\n", start, end));
	for (va = (u_int)start; va <= (u_int)end; va += MMU_PAGESIZE) {
		if ((pfn = cpr_va_to_pfn(va)) != (u_int) -1)
			cpr_boot_setbit(pfn);
	}
}

static int
cpr_relocate_boot(int flag)
{
	register u_int va, opfn, npfn, npa;
	caddr_t start, end;
	unsigned char *from, *to;
	int i, j;
	struct pte *ptep;
	union ptes tmppte;

	DEBUG4(errp("Enter cpr relocate boot %d\n", flag));

	/*
	 * Kludge for in case ufsboot does not export the properties.
	 */
	if (flag == RELOCATE_BOOT) {
		if ((BOP_GETPROP(bootops, "boot-start", &start) != 0) ||
		    (BOP_GETPROP(bootops, "boot-end", &end) != 0)) {
		errp("cannot read boot-start/end; set to default values\n");
			start = (caddr_t) 0x100000;
			end = (caddr_t) 0x250000;
		}
		DEBUG4(errp("BOOT START %x  END %x\n", start, end));
		end += BOOT_SCRATCHMEM_GROW;
	} else if (flag == RELOCATE_CPRBOOT) {
		cpr_getprop(bootops, "cprboot-start", &start);
		cpr_getprop(bootops, "cprboot-end", &end);
		DEBUG4(errp("CPRBOOT START %x  END %x\n", start, end));
	}

	for (va = (u_int)start; va <= (u_int)end; va += MMU_PAGESIZE) {

		if ((opfn = cpr_va_to_pfn(va)) == (u_int) -1)
		    continue;

		/*
		 * make these pages cacheable, so we run faster
		 * XXX: may speed this loop up later?
		 */
		ptep = (struct pte *)cpr_srmmu_ptefind((caddr_t)va);
		if (ptep == (struct pte *)-1)
			continue;

		tmppte.pte_int = ldphys((int)ptep);
		tmppte.pte.Cacheable = 1;

		if (cpr_kpage_is_free(opfn)) {
			stphys((int)ptep, tmppte.pte_int);
			continue;
		}

		if ((npfn = cpr_find_free_page()) == -1) {
			errp(" cpr_relocate_boot: NO FREE PAGE\n");
			return (-1);
		}
		npa = PN_TO_ADDR(npfn);

		DEBUG4(errp("cpr_relocate_boot: va %x  opfn %x  npfn %x\n",
			va, opfn, npfn));

		prom_unmap(free_va, MMU_PAGESIZE);
		if ((u_int)prom_map((caddr_t)free_va, 0, npa,
			MMU_PAGESIZE) != free_va) {
			errp("cpr_relocate_boot: Prom_map failed\n");
			return (-1);
		}
		move_page(va, free_va);

		/*
		 * stores the new physical pfn into PTE
		 */
		tmppte.pte.PhysicalPageNumber = npfn;
		stphys((int)ptep, tmppte.pte_int);

		srmmu_mmu_flushpage(va);
	}
	return (0);
}
