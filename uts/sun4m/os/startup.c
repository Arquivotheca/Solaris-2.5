/*
 * Copyright (c) 1992-1995, by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)startup.c 1.94     95/09/26 SMI"

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/systm.h>
#include <sys/machsystm.h>
#include <sys/user.h>
#include <sys/map.h>
#include <sys/mman.h>
#include <sys/vm.h>

#include <sys/disp.h>
#include <sys/class.h>

#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/kmem.h>
#include <sys/kstat.h>

#include <sys/reboot.h>
#include <sys/uadmin.h>

#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/callo.h>
#include <sys/msgbuf.h>

#include <sys/procfs.h>
#include <sys/acct.h>

#include <sys/vfs.h>
#include <sys/dnlc.h>
#include <sys/var.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>

#include <sys/dumphdr.h>
#include <sys/bootconf.h>
#include <sys/memlist.h>
#include <sys/varargs.h>
#include <sys/async.h>
#include <sys/promif.h>
#include <sys/modctl.h>		/* for "procfs" hack */
#include <sys/kvtopdata.h>

#include <sys/consdev.h>
#include <sys/frame.h>

#define	SUNDDI_IMPL		/* so sunddi.h will not redefine splx() et al */
#include <sys/sunddi.h>
#include <sys/ddidmareq.h>
#include <sys/dki_lock.h>
#include <sys/autoconf.h>
#include <sys/clock.h>
#include <sys/pte.h>
#include <sys/scb.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/stack.h>
#include <sys/eeprom.h>
#include <sys/intreg.h>
#include <sys/memerr.h>
#include <sys/buserr.h>
#include <sys/auxio.h>
#include <sys/trap.h>
#include <sys/module.h>
#include <sys/x_call.h>

#ifdef	IOC
#include <sys/iocache.h>
#endif	IOC

#include <vm/hat.h>
#include <vm/anon.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/seg_kp.h>
#include <sys/swap.h>
#include <sys/thread.h>
#include <sys/sysconf.h>
#include <sys/vmparam.h>
#include <vm/hat_srmmu.h>
#include <sys/iommu.h>
#include <sys/vtrace.h>
#include <sys/instance.h>

/*
 * External Routines:
 */
extern void param_calc(int);
extern void param_init();
extern void mxcc_knobs(void);
extern void set_pages_vcolor();

/*
 * External Data:
 */
extern int do_pg_coloring;
extern int deferred_pg_coloring;
extern lksblk_t *lksblks_head;
extern int maxusers;
extern int use_table_walk;
extern int viking;
extern int tsunami;
extern int vac_size;	/* cache size in bytes */
extern u_int vac_mask;	/* VAC alignment consistency mask */
extern volatile u_int aflt_sync[];
extern u_int a_head[];
extern u_int a_tail[];

/*
 * Global Data Definitions:
 */

/*
* new memory fragmentations possible in startup() due to BOP_ALLOCs. this
* depends on number of BOP_ALLOC calls made and requested size, memory size `
*  combination.
*/
#define	POSS_NEW_FRAGMENTS	10
/*
 * Declare these as initialized data so we can patch them.
 */
int physmem = 0;	/* memory size in pages, patch if you want less */
int kernprot = 1;	/* write protect kernel text */

int use_cache = 1;		/* cache not reliable (605 bugs) with MP */
int vac_copyback = 1;
char	*cache_mode = (char *)0;
int use_mix = 1;

int chkkas = 1;		/* check kernel address space after kvm_init */

#ifdef	IOC
/*
 * Enable IOC
 */
int use_ioc = 1;	/* variable to patch to have the kernel use i/o cache */
int cachenet = 1;	/* iocache the network */
int cachevme = 1;	/* iocache the vme */
int iocdebug = 0;	/* debugging flag for ioc code */
int iocenable = 0;	/* vestigial */
#else	!IOC
#define	use_ioc 0
#define	cachenet 0
#define	cachevme 0
#define	iocdebug 0
#endif	!IOC


struct bootops *bootops = 0;	/* passed in from boot in %o2 */
char obpdebugger[32] = "misc/obpsym";

caddr_t s_text;			/* start of kernel text segment */
caddr_t e_text;			/* end of kernel text segment */
caddr_t s_data;			/* start of kernel data segment */
caddr_t e_data;			/* end of kernel data segment */

caddr_t econtig;		/* end of first block of contiguous kernel */
caddr_t ncbase;			/* beginning of non-cached segment */
caddr_t	ncend;			/* end of non-cached segment */

struct map *dvmamap;		/* map to manage usable dvma space */

u_int shm_alignment = 0;	/* VAC address consistency modulus */
struct memlist *phys_install;	/* Total installed physical memory */
struct memlist *phys_avail;	/* Available (unreserved) physical memory */
struct memlist *virt_avail;	/* Available (unmapped?) virtual memory */
int memexp_flag;		/* memory expansion card flag */

/*
 * VM data structures
 */
int page_hashsz;		/* Size of page hash table (power of two) */
u_int pagehash_sz;
struct page *pp_base;		/* Base of system page struct array */
u_int pp_sz;			/* Size in bytes of page struct array */
struct	page **page_hash;	/* Page hash table */
struct	seg *segkmap;		/* Kernel generic mapping segment */
struct	seg ktextseg;		/* Segment used for kernel executable image */
struct	seg kvalloc;		/* Segment used for "valloc" mapping */
struct	seg *segkp;		/* Segment for pageable kernel virt. memory */
struct	seg *seg_debug;		/* Segment for debugger */
struct	seg *seg_ncmem;		/* Segment for non-cached mem */
struct	memseg *memseg_base;
u_int	memseg_sz;		/* Used to translate a va to page */
struct	vnode unused_pages_vp;

u_int	stklo, stkhi;
caddr_t segkmap_lo, segkmap_hi;

/*
 * VM data structures allocated early during boot.
 */
/*
 *	Fix for bug 1119063.
 */
#define	KERNELMAP_SZ(frag)	\
	max(MMU_PAGESIZE,	\
	roundup((sizeof (struct map) * SYSPTSIZE/2/(frag)), MMU_PAGESIZE))

u_int memlist_sz;
u_int kernelmap_sz;
caddr_t startup_alloc_vaddr = (caddr_t)SYSBASE + MMU_PAGESIZE;
caddr_t startup_alloc_size;
u_int	startup_alloc_chunk_size = 20 * MMU_PAGESIZE;
u_int msgbuf_sz;
u_int pmeminstall;		/* total physical memory installed */

char tbr_wr_addr_inited = 0;

iommu_pte_t *ioptes, *eioptes; 	/* virtual addr of ioptes */
iommu_pte_t *phys_iopte;	/* phys addr of ioptes */

/*
 * Saved beginning page frame for kernel .data and last page
 * frame for up to end[] for the kernel. These are filled in
 * by kvm_init().
 */
u_int kpfn_dataseg, kpfn_endbss;

/*
 * crash dump, libkvm support - see sys/kvtopdata.h for details
 */
struct kvtopdata kvtopdata;

/*
 * Static Routines:
 */
static void ioc_init(void);
static void iocache_on(void);
static void physlist_add(u_longlong_t, u_longlong_t, struct memlist **);
static void kphysm_init(page_t *, struct memseg *, u_int, u_int);
static void kvm_init(void);
static void setup_kvpm(void);

static void kern_preprom(void);
static void kern_postprom(void);

void sx_vrfy_exist(void);
u_int sx_ctlr_present = 0;

static int sunpc_present(void);

/*
 * workaround for Ross 605 hardware bug
 */

extern int ross_mod_mcr;
int ross_iopb_workaround;
int downrev_ross_detected = 0;

/*
 * Enable some debugging messages concerning memory usage...
 */
#ifdef  DEBUGGING_MEM
static int debugging_mem;
static void
printmemlist(char *title, struct memlist *listp)
{
	struct memlist *list;

	if (!debugging_mem)
		return;

	printf("%s\n", title);
	if (!listp)
		return;

	for (list = listp; list; list = list->next) {
		prom_printf("addr = 0x%x%8x, size = 0x%x%8x\n",
		    (u_int)(list->address >> 32), (u_int)list->address,
		    (u_int)(list->size >> 32), (u_int)(list->size));
	}
}

#define	debug_pause(str)	if (prom_getversion() > 0) halt((str))
#define	MPRINTF(str)		if (debugging_mem) prom_printf((str))
#define	MPRINTF1(str, a)	if (debugging_mem) prom_printf((str), (a))
#define	MPRINTF2(str, a, b)	if (debugging_mem) prom_printf((str), (a), (b))
#define	MPRINTF3(str, a, b, c) \
	if (debugging_mem) prom_printf((str), (a), (b), (c))
#else	/* DEBUGGING_MEM */
#define	MPRINTF(str)
#define	MPRINTF1(str, a)
#define	MPRINTF2(str, a, b)
#define	MPRINTF3(str, a, b, c)
#endif	/* DEBUGGING_MEM */

/* Simple message to indicate that the bootops pointer has been zeroed */
#ifdef DEBUG
static int bootops_gone_on = 0;
#define	BOOTOPS_GONE() \
	if (bootops_gone_on) \
		prom_printf("The bootops vec is zeroed now!\n");
#else
#define	BOOTOPS_GONE()
#endif DEBUG

/*
 * Monitor pages may not be where this sez they are.
 * and the debugger may not be there either.
 *
 * Also, note that 'pages' here are *physical* pages,
 * which are 4k on sun4m. Thus, for us the first 4 pages
 * of physical memory are equivalent of the first two
 * 8k pages on the sun4.
 *
 *		  Physical memory layout
 *		(not necessarily contiguous)
 *
 *		_________________________
 *		|	monitor pages	|
 *    availmem -|-----------------------|
 *		|			|
 *		|	page pool	|
 *		|			|
 *		|-----------------------|
 *		|   configured tables	|
 *		|	buffers		|
 *   firstaddr -|-----------------------|
 *		|   hat data structures |
 *		|-----------------------|
 *		|    kernel data, bss	|
 *		|-----------------------|
 *		|    interrupt stack	|
 *		|-----------------------|
 *		|    kernel text (RO)	|
 *		|-----------------------|
 *		|    trap table (4k)	|
 *		|-----------------------|
 *	page 2-3|	 msgbuf		|
 *		|-----------------------|
 *	page 0-1|	reclaimed	|
 *		|_______________________|
 *
 *
 *		  Virtual memory layout.
 *		/-----------------------\
 *		|	INVALID		|
 * 0xFFFC0000  -|-----------------------|
 *		|	 DVMA		|			(1 M)
 * 0xFFF00000  -|-----------------------|- DVMA
 *		|	monitor		|			(2 M)
 * 0xFFD00000  -|-----------------------|- MONSTART
 *		|    exec args area	|			(1 M)
 * 0xFFC00000  -|-----------------------|- ARGSBASE
 *		| quick page map region |			(512 K)
 * 0xFFB80000  -|-----------------------|- PPMAPBASE
 *		| 	not used	|
 * 0xFEF05000  -|-----------------------|
 *		|   TBR writable addr	|
 *		|    TBR addr cpu3	|
 *		|    TBR addr cpu2	|
 *		|    TBR addr cpu1	|
 *		|    TBR addr base	|
 * 0xFEF00000  -|-----------------------|- V_WKBASE_ADDR
 *		|  OBP "large" mappings	|			(11 M max)
 * 0xFE400000  -|-----------------------|
 *		|			|
 *		|	segkp		|			(38M)
 *		|			|
 * 0xFBE00000  -|-----------------------|
 *		|	debugger	|			(1 M)
 * 0xFBD00000  -|-----------------------|- DEBUGADDR/Syslimit
 *		|	Sysmap		|	SYSPTSIZE * MMU_PAGESIZE
 *		|			|			(100 M)
 * 0xF5903000  -|-----------------------|
 *		|	 msgbuf		|			(8 K)
 * 0xF5901000  -|-----------------------|
 *		|	not used	|
 * 0xF5900000  -|-----------------------|- SYSBASE/Sysbase/E_Sysbase/ncend
 *		|    non-cached ptes	|
 *		|-----------------------|- ncbase
 *		|	 unused		|	(at least 40M for SunPC)
 *		|-----------------------|
 *		|	segkmap		|	SEGMAPSIZE	(16 M)
 *		|-----------------------|- econtig
 *		|    cached ptes	|
 *		|-----------------------|
 *		|    vm structures	|
 *		|-----------------------|- end
 *		|	kernel		|
 *		|-----------------------|
 *		|   trap table (4k)	|
 *		|-----------------------|
 *		|  user copy red zone	|
 *		|	(invalid)	|
 * 0xF0000000  -|-----------------------|- KERNELBASE
 *		|	user stack	|
 *		:			:
 *		:			:
 *		|	user data	|
 *		|-----------------------|
 *		|	user text	|
 * 0x00002000  -|-----------------------|
 *		|	invalid		|
 * 0x00000000  _|_______________________|
 *
 *
 * NOTE:For any future change in this table, please keep in mind
 *	the assumptions for CPR (checkpoint/resume), which requires all
 *	kernel memory (non-user pages) to be included in kernel address
 *	space (kas). If new memory regions are created, add corresponding
 *	segements into the kas link list as well.
 *
 */

/* Variables for large block allocations */
extern u_short *l1_freecnt;
extern u_char *l2_freecnt;
extern int l1_free_tblsz, l2_free_tblsz;

extern void init_srmmu_alloc(caddr_t *palloc_base, caddr_t *preal_base);
extern void build_pfn_hash();
extern void srmmu_reserve(struct as *, caddr_t, u_int, u_int);

static int iswpgs, ihwpgs;

/*
 * Machine-dependent startup code
 */
void
startup(void)
{
	register unsigned i;
	u_int npages;
	struct segmap_crargs a;
	int dbug_mem, memblocks;
	u_int pp_giveback;
	caddr_t memspace;
	u_int memspace_sz;
	u_int nppstr;
	u_int avmem;
	u_int freetbl_sz;
	u_int kv_reserve;
	caddr_t va;
	caddr_t real_base, alloc_base, high_alloc_base;
	int real_sz, alloc_sz, high_alloc_sz;
	int extra, extra_phys;
	caddr_t extra_virt;
	struct memlist *memlist, *new_memlist;
	struct memlist *cur;
	u_int debug_start_va;		/* debugger start va */
	int ctxalign, iomalign;
	int	max_virt_segkp;
	int	max_phys_segkp;
	u_int	max_contig_mem;
	caddr_t	align_base;
	int ctxpgs;
	caddr_t va_cur;

#if defined(SAS)
	u_int mapaddr;
#endif /* !SAS */
	extern char Syslimit[];
	extern void mt_lock_init();
	extern caddr_t mm_map, cur_dump_addr, dump_addr;
	extern int viking_ncload_bug;
	extern caddr_t e_text, e_data;
	extern void ialloc_ptbl();

	(void) check_boot_version(BOP_GETVERSION(bootops));

	dki_lock_setup(lksblks_head);

	/*
	 * Initialize the turnstile allocation mechanism.
	 */
	tstile_init();

	kncinit();

	/*
	 * Install PROM callback handler (give both, promlib picks the
	 * appropriate handler.
	 */
	if (prom_sethandler(NULL, vx_handler) != 0)
		prom_panic("No handler for PROM?");

/*
 * XXX -
 * Eventually this code will be comming back once the msgbuf is allowed
 * to be at a variable location.  Right now though, it is wired down to
 * physical pages 2 and 3.
 *
 * XXX	It's not quite as easy as that.  Boot needs to offer an
 *	interface to non-volatile memory first.
 */
#ifdef	notdef
	/*
	 * Map in msgbuf.
	 */
	msgbuf_sz = roundup(sizeof (struct msgbuf), MMU_PAGESIZE);
	memspace = (caddr_t)BOP_ALLOC(bootops, (caddr_t)&msgbuf, msgbuf_sz,
		BO_NO_ALIGN);
	if (memspace != (caddr_t)&msgbuf)
		prom_panic("msgbuf alloc failure");
#endif


	/*
	 * Initialize enough of the system to allow kmem_alloc
	 * to work by calling boot to allocate its memory until
	 * the time that kvm_init is completed.  The page structs
	 * are allocated after rounding up end to the nearest page
	 * boundary; kernelmap, and the memsegs are intialized and
	 * the space they use comes from the area managed by kernelmap.
	 * With appropriate initialization, they can be reallocated
	 * later to a size appropriate for the machine's configuration.
	 *
	 * At this point, memory is allocated for things that will never
	 * need to be freed, this used to be "valloced".  This allows a
	 * savings as the pages don't need page structures to describe
	 * them because them will not be managed by the vm system.
	 */

	/*
	 * We're loaded aligned on a 256k boundary, so we have some slop
	 * from end to the end of allocated memory.
	 *
	 * Since we want to "valloc" static data structures with large
	 * pages, we use real_base to point to the end of the actual
	 * allocations, and alloc_base to point to where the next allocation
	 * will be.
	 */
	real_base = (caddr_t)roundup((u_int)e_data, MMU_PAGESIZE);
	alloc_base = (caddr_t)roundup((u_int)real_base, L3PTSIZE);
	if (va_to_pa((u_int)real_base) == -1 && real_base != alloc_base) {
		va = (caddr_t)BOP_ALLOC(bootops, real_base,
					alloc_base - real_base, BO_NO_ALIGN);
		if (va != real_base)
			panic("alignment correction alloc failed");
	}

	/*
	 * Remember any slop after e_text so we can add it to the
	 * physavail list.
	 */
	extra_virt = (caddr_t)roundup((u_int)e_text, MMU_PAGESIZE);
	extra_phys = va_to_pa((u_int)extra_virt);
	if (extra_phys != -1)
		extra = roundup((u_int)e_text, L3PTSIZE) - (u_int)extra_virt;
	else
		extra = 0;
	extra = mmu_btop(extra);

	/*
	 * take the most current snapshot we can by calling mem-update
	 */
	if (BOP_GETPROPLEN(bootops, "memory-update") == 0)
		BOP_GETPROP(bootops, "memory-update", NULL);

	/*
	 * Remember what the physically available highest page is
	 * so that dumpsys works properly, and find out how much
	 * memory is installed.
	 */
	installed_top_size(bootops->boot_mem->physinstalled, &physmax,
	    &physinstalled);
	pmeminstall = ptob(physinstalled);

	/*
	 * Get the list of physically available memory to size
	 * the number of page structures needed.
	 */
	size_physavail(bootops->boot_mem->physavail, &npages, &memblocks);
	npages += extra;
	l1_free_tblsz = (physmax >> MMU_STD_FIRSTSHIFT) + 1;
	l2_free_tblsz = (physmax >> MMU_STD_SECONDSHIFT) + 1;
	freetbl_sz = l1_free_tblsz * sizeof (*l1_freecnt) + l2_free_tblsz *
		sizeof (*l2_freecnt);

	/*
	 * If physmem is patched to be non-zero, use it instead of
	 * the monitor value unless physmem is larger than the total
	 * amount of memory on hand.
	 */
	if (physmem == 0 || physmem > npages)
		physmem = npages;

	/*
	 * The page structure hash table size is a power of 2
	 * such that the average hash chain length is PAGE_HASHAVELEN.
	 */
	page_hashsz = npages / PAGE_HASHAVELEN;
	page_hashsz = 1 << highbit(page_hashsz);	/* IMPORTANT */
	pagehash_sz = sizeof (struct page *) * page_hashsz;

	/*
	 * Knob! Initialize pte2hme_hashsz
	 */
	if (pte2hme_hashsz == 0) {
		pte2hme_hashsz = (npages >> 2);
		pte2hme_hashsz = 1 << highbit(pte2hme_hashsz);  /* IMPORTANT */
		pte2hmehash_sz = pte2hme_hashsz * sizeof (struct srhment *);
	}

	/*
	 * Some of the locks depend on page_hashsz being set!
	 */
	mt_lock_init();

	/*
	 * The memseg list is for the chunks of physical memory that
	 * will be managed by the vm system.  The number calculated is
	 * a guess as boot may fragment it more when memory allocations
	 * are made before kphysm_init().  Currently, there are two
	 * allocations before then, so we assume each causes fragmen-
	 * tation, and add a couple more for good measure.
	 */
	memseg_sz = sizeof (struct memseg) * (memblocks + POSS_NEW_FRAGMENTS);
	pp_sz = sizeof (struct page) * npages;
	real_sz = roundup(pagehash_sz + pte2hmehash_sz + memseg_sz + pp_sz +
		freetbl_sz, MMU_PAGESIZE);

	/*
	 * Allocate enough 256k chunks to map the page structs.  If we
	 * can't use all of it, we'll give some back later.
	 */
	alloc_sz = real_sz - (alloc_base - real_base);
	if (alloc_sz > 0) {
		alloc_sz = roundup(alloc_sz, L3PTSIZE);
		memspace = (caddr_t)
			BOP_ALLOC(bootops, alloc_base, alloc_sz, L3PTSIZE);
		if (memspace != alloc_base)
			panic("system page struct alloc failure");
		alloc_base += alloc_sz;
		ASSERT(MMU_L2_OFF(alloc_base) == 0);

		/*
		 * We don't need page structs for the memory we just allocated
		 * so we subtract an appropriate amount.
		 */
		nppstr = btop(alloc_sz);
		pp_giveback = (nppstr * sizeof (struct page)) & MMU_PAGEMASK;
		pp_sz -= pp_giveback;
		npages -= nppstr;
	}

	page_hash = (struct page **)real_base;
	pte2hme_hash = (struct srhment **)((u_int)page_hash + pagehash_sz);
	memseg_base = (struct memseg *)((u_int)pte2hme_hash + pte2hmehash_sz);
	pp_base = (struct page *)((u_int)memseg_base + memseg_sz);

	/*
	 * Now align the base to 32 byte boundary.
	 */
	pp_base = (page_t *)roundup((u_int)pp_base,  32);
	ASSERT(((u_int)pp_base & 0x1F) == 0);

	l1_freecnt = (u_short *)((u_int)pp_base + pp_sz);
	l2_freecnt = (u_char *)(l1_freecnt + l1_free_tblsz);
	real_base = (caddr_t)roundup((u_int)l2_freecnt + l2_free_tblsz,
				MMU_PAGESIZE);
	econtig = real_base;

	/*
	 * the memory lists from boot, and early versions of the kernelmap
	 * is allocated from the virtual address region managed by kernelmap
	 * so that later they can be freed and/or reallocated.
	 */
	memlist_sz = bootops->boot_mem->extent;
	/*
	 * Between now and when we finish copying in the memory lists,
	 * allocations happen so the space gets fragmented and the
	 * lists longer.  Leave enough space for lists twice as long
	 * as what boot says it has now; roundup to a pagesize.
	 */
	memlist_sz *= 2;
	memlist_sz = roundup(memlist_sz, MMU_PAGESIZE);
	kernelmap_sz = KERNELMAP_SZ(4);
	memspace_sz =  memlist_sz + kernelmap_sz;
	memspace = (caddr_t)BOP_ALLOC(bootops, startup_alloc_vaddr,
	    memspace_sz, BO_NO_ALIGN);
	startup_alloc_vaddr += memspace_sz;
	startup_alloc_size += memspace_sz;
	if (memspace == NULL)
		halt("Boot allocation failed.");

	memlist = (struct memlist *)memspace;
	kernelmap = (struct map *)((u_int)memlist + memlist_sz);

	mapinit(kernelmap, (long)(SYSPTSIZE - 1), (u_long)1,
		"kernel map", kernelmap_sz / sizeof (struct map));
	/*
	 * take the most current snapshot we can by calling mem-update
	 */
	if (BOP_GETPROPLEN(bootops, "memory-update") == 0)
		BOP_GETPROP(bootops, "memory-update", NULL);


	/*
	 * Remove the space used by BOP_ALLOC from the kernelmap.
	 */
	mutex_enter(&maplock(kernelmap));
	if (rmget(kernelmap, btop(startup_alloc_size), 1) == 0)
		prom_panic("can't make initial kernelmap allocation");

	/*
	 * Remove the area actually used by the OBP (if any)
	 */
	virt_avail = memlist;
	copy_memlist(bootops->boot_mem->virtavail, &memlist);
	for (cur = virt_avail; cur->next; cur = cur->next) {
		u_int	range_base = cur->address + cur->size;

		if (range_base > (u_int)startup_alloc_vaddr &&
		    range_base < (u_int)Syslimit) {
			u_int	range_size;

			range_size = cur->next->address - range_base;
			if (rmget(kernelmap, btop(range_size),
			    btop(range_base - (u_int)Sysbase)) == 0)
				prom_panic("can't remove OBP hole");
		}
	}
	mutex_exit(&maplock(kernelmap));

	(void) sx_vrfy_exist();

	/*
	 * XXX -
	 * First add a memory list element for physical pages below
	 * the msgbuf.  This all assumes that the message buffer is
	 * locked down to physical pages 2 and 3.  There are proposals
	 * to allow the msgbug to be at a variable location, so once
	 * one of these proposals is implemented, this code needs to
	 * be ripped out.
	 */
	phys_avail = memlist;
	if (!copy_physavail(bootops->boot_mem->physavail, &memlist,
	    0x2000, 0x2000))
		halt("Can't deduct msgbuf from physical memory list");

	/*
	 * Add any extra mem after e_text to physavail list.
	 */
	if (extra) {
		u_longlong_t start = (u_longlong_t)((u_int)extra_phys);
		u_longlong_t len = (u_longlong_t)mmu_ptob(extra);

		physlist_add(start, len, &memlist);
	}

#if defined(SAS) || defined(MPSAS)
	/* for SAS, memory is contiguos */
	page_init(&pp, npages, pp_base, memseg_base);

	first_page = memsegs->pages_base;
	if (first_page < mapaddr + btoc(econtig - e_data))
		first_page = mapaddr + btoc(econtig - e_data);
	memialloc(first_page, mapaddr + btoc(econtig - e_data),
	    memseg->pages_end);
#else	/* SAS || MPSAS */

	/*
	 * Initialize the page structures from the memory lists.
	 */
	kphysm_init(pp_base, memseg_base, npages, memblocks+POSS_NEW_FRAGMENTS);
#endif	/* SAS || MPSAS */

	availsmem = freemem;
	availrmem = freemem;

	/*
	 * Initialize kernel memory allocator.
	 */
	kmem_init();

	/*
	 * Initialize the kstat framework.
	 */
	kstat_init();

	/* check for memory expansion and set flag if it's there */
	memexp_flag = check_memexp(bootops->boot_mem->physinstalled,
	    MEMEXP_START);

	/*
	 * Lets display the banner early so the user has some idea that
	 * UNIX is taking over the system.
	 */
	cmn_err(CE_CONT,
	    "\rSunOS Release %s Version %s [UNIX(R) System V Release 4.0]\n",
	    utsname.release, utsname.version);
	cmn_err(CE_CONT, "Copyright (c) 1983-1995, Sun Microsystems, Inc.\n");
#ifdef DEBUG
	cmn_err(CE_CONT, "DEBUG enabled\n");
#endif
#ifdef TRACE
	cmn_err(CE_CONT, "TRACE enabled\n");
#endif

	/*
	 * Check to see if this is a Ross module and next test if the
	 * impl and rev together is greater that ROSS_A8. New revs of
	 * 605 Ross modules have lower rev numbers.
	 */
	if (ross_mod_mcr && (((ross_mod_mcr >> 24) & 0x1F) > ROSS605_A8)) {
		cmn_err(CE_WARN,
		"DOWN-REV ROSS 605 CPU DETECTED - PLEASE UPGRADE.\n%s%s",
		"RUNNING IN UNI-PROCESSOR MODE.",
		" PERFORMANCE WILL BE AFFECTED.\n");
		downrev_ross_detected = 1;
		use_mp = 0;
		ross_iopb_workaround = 1;
	}

	/*
	 * Read system file, (to set maxusers, physmem.......)
	 *
	 * Variables that can be set by the system file shouldn't be
	 * used until after the following initialization!
	 */
	mod_read_system_file(boothowto & RB_ASKNAME);

	/*
	 * Calculate default settings of system parameters based upon
	 * maxusers, yet allow to be overridden via the /etc/system file.
	 */
	param_calc(maxusers);

	/*
	 * Initialize loadable module system and apply the 'set' commands
	 * gleaned from the /etc/system file.
	 */
	mod_setup();

	/*
	 * Initialize system parameters
	 */
	param_init();

	/*
	 * If obpdebug is set, load the obpsym debugger module, now.
	 */
	if (obpdebug)
		(void) modload(NULL, obpdebugger);

	/*
	 * apply use_mxcc_prefetch and use_multiple_cmds
	 */
	if (mxcc)
		mxcc_knobs();

#if !(defined(SAS) || defined(MPSAS))

	/*
	 * If debugger is in memory, note the pages it stole from physmem.
	 * XXX: Should this happen with V2 Proms?  I guess it would be
	 * set to zero in this case?
	 */
	if (boothowto & RB_DEBUG)
		dbug_mem = *dvec->dv_pages;
	else
		dbug_mem = 0;

#endif /* !(SAS || MPSAS) */

	/*
	 * maxmem is the amount of physical memory we're playing with.
	 */
	maxmem = physmem;

	/*
	 * Calculate how many page tables we will have.
	 * The algorithm is to have enough ptes to map all
	 * of memory 4 times over (due to fragmentation).  We
	 * add in the size of the I/O Mapper, since it is always there.
	 * We also add in enough tables to map the kernel text/data once.
	 * There is an upper limit, however, since we use indices
	 * in page table lists.
	 */

	ASSERT(MMU_L2_OFF(alloc_base) == 0);
	ASSERT(MMU_L3_OFF(real_base) == 0);

	init_srmmu_var();

	if (nctxs > v.v_proc + 1) {
		nctxs = v.v_proc + 1;
		while (nctxs & (nctxs - 1)) {
			nctxs = (nctxs | (nctxs - 1)) + 1;
		}
	}


	if (viking)
		ctxalign = MAX(4096, sizeof (struct ptp) * nctxs);
	else if (tsunami)
		ctxalign = 4096;
	else /* ross || swift */
		ctxalign = 16384;

	ctxpgs = roundup(nctxs * sizeof (struct ctx), MMU_PAGESIZE);
	ctxpgs = mmu_btop(ctxpgs);
	/*
	 * iswpgs contain pure s/w data structure which always
	 * should be cached.
	 */
	iswpgs = ctxpgs;
	real_sz = mmu_ptob(iswpgs);
	alloc_sz = real_sz - (alloc_base - real_base);

	if (alloc_sz > 0) {
		alloc_sz = roundup(alloc_sz, L3PTSIZE);
		boot_alloc(alloc_base, alloc_sz, L3PTSIZE);
		alloc_base += alloc_sz;
		ASSERT(MMU_L2_OFF(alloc_base) == 0);
	}


	/*
	 * We know real_base is page aligned.
	 */
	va_cur = real_base;
	ctxs = (struct ctx *)va_cur;
	va_cur += (nctxs * sizeof (struct ctx));
	ectxs = (struct ctx *)va_cur;
	real_base = va_cur;

	/*
	 * Now allocate h/w related pages.
	 *
	 * ihwpgs contain pages that srmmu h/w uses to do table
	 * walk which may require to be non-cached.
	 */
	ialloc_ptbl();

	ihwpgs = mmu_btopr(nctxs * sizeof (struct ptp));
	real_sz = mmu_ptob(ihwpgs);

	high_alloc_sz = IOMMU_N_PTES * sizeof (struct iommu_pte);
	iomalign = high_alloc_sz;

	/* XXX  - works but not correct for swift */
	if (viking && mxcc && use_table_walk) {
		real_base = (caddr_t)
		roundup((u_int)real_base, ctxalign);
		alloc_sz = roundup(real_base + real_sz - alloc_base, L3PTSIZE);

		/*
		 * Check if the size being requested is larger than
		 * the max contig physical memory available at this time.
		 * If it is, break the request into two requests, such that
		 * real_sz (representing memory for pte) is allocated
		 * contiguously. Also allign them properly.
		 */
		if (BOP_GETPROPLEN(bootops, "memory-update") == 0)
			BOP_GETPROP(bootops, "memory-update", NULL);
		max_contig_mem = (u_int)get_max_phys_size(
		    bootops->boot_mem->physavail);

		if (alloc_sz > max_contig_mem) {
			align_base = (caddr_t)
			    ((u_int)real_base & (u_int)~L3PTSIZE);

			alloc_sz = roundup(align_base - alloc_base, L3PTSIZE);
			if (alloc_sz > 0) {
				boot_alloc(alloc_base, alloc_sz, L3PTSIZE);
				alloc_base += alloc_sz;
			}

			alloc_sz = roundup(real_base + real_sz - align_base,
			    L3PTSIZE);
			if (alloc_sz > 0) {
				boot_alloc(alloc_base, alloc_sz, L3PTSIZE);
				alloc_base += alloc_sz;
			}
		} else {
			/* Allocate cached memory  */
			if (alloc_sz > 0) {
				boot_alloc(alloc_base, alloc_sz, L3PTSIZE);
				alloc_base += alloc_sz;
			}
		}
	} else {
			high_alloc_sz += real_sz;
			high_alloc_sz = roundup(high_alloc_sz, L3PTSIZE);
			real_base = (caddr_t) SYSBASE - high_alloc_sz;
			/* No non-cached memory is allocated yet. */
	}

	high_alloc_base = (caddr_t) SYSBASE - high_alloc_sz;
	ncbase = high_alloc_base;
	ncend = (caddr_t) SYSBASE;

	ASSERT(MMU_L2_OFF(alloc_base) == 0);

	/* We should be done with allocating cached memory by now. */
	econtig = alloc_base;

	/*
	 * Allocate uncached memory.  If we're just allocating the iommu
	 * table, don't try to use l2 ptes.  If the iommu alignment is
	 * larger than L3PTSIZE, allocate it separately.
	 */
	if (high_alloc_sz < L3PTSIZE)
		boot_alloc(high_alloc_base, high_alloc_sz, iomalign);
	else if (iomalign < L3PTSIZE)
		boot_alloc(high_alloc_base, high_alloc_sz, L3PTSIZE);
	else {
		high_alloc_sz = roundup(real_sz, L3PTSIZE);
		boot_alloc(high_alloc_base, high_alloc_sz, L3PTSIZE);
		high_alloc_base += high_alloc_sz;
		high_alloc_sz = ncend - high_alloc_base;
		boot_alloc(high_alloc_base, high_alloc_sz, iomalign);
	}

	/*
	 * Regardless the memory holding h/w page tables,
	 * context table is cached or not, it's now allocated.
	 */
	contexts = (struct ptp *)real_base;
	econtexts = contexts + nctxs;
	va_cur = (caddr_t)(roundup((u_int)econtexts, MMU_PAGESIZE));

	/*
	 * The iommu alignment is the same as its size.  Since DEBUGADDR
	 * is aligned to 1M, that is also the gratest alignment the iommu
	 * table can be.  The max the hw supports is 2M.
	 */
	real_base = ncend - iomalign;
	real_sz = iomalign;
	ioptes = (struct iommu_pte *)real_base;
	phys_iopte = (struct iommu_pte *)va_to_pa((u_int)ioptes);
	if (phys_iopte == (struct iommu_pte *)-1)
		panic("invalid ioptes");
	eioptes = ioptes + IOMMU_N_PTES;

	if ((dvmamap = (struct map *)
	    kmem_zalloc(sizeof (struct map) * SBUSMAP_FRAG,
			KM_NOSLEEP)) == NULL) {
		prom_panic("Can't allocate memory for sbusmap structs");
	}

	/*
	 * Initialize the hat layer.
	 */
	hat_init();

	/*
	 * Initialize segment management stuff.
	 */
	seg_init();

	if (modloadonly("fs", "specfs") == -1)
		halt("Can't load specfs");

	if (modloadonly("misc", "swapgeneric") == -1)
		halt("Can't load swapgeneric");

	dispinit(NULL);

	setup_ddi();

	/*
	 * Lets take this opportunity to load the the root device.
	 */
	if (loadrootmodules() != 0)
		debug_enter("Can't load the root filesystem");

	/*
	 * Allocate some space to copy physavail into .. as usual there
	 * are some horrid chicken and egg problems to be avoided when
	 * copying memory lists - i.e. this very allocation could change 'em.
	 */
	new_memlist = (struct memlist *)kmem_zalloc(memlist_sz, KM_NOSLEEP);

	/*
	 * Call back into boot and release boots resources.
	 */
	BOP_QUIESCE_IO(bootops);

	/*
	 * Copy physinstalled list into kernel space.
	 */
	phys_install = memlist;
	copy_memlist(bootops->boot_mem->physinstalled, &memlist);

	/*
	 * Virtual available next.
	 */
	virt_avail = memlist;
	copy_memlist(bootops->boot_mem->virtavail, &memlist);

	/*
	 * Copy phys_avail list, again.
	 * Both the kernel/boot and the prom have been allocating
	 * from the original list we copied earlier.
	 */
	cur = new_memlist;
	if (!copy_physavail(bootops->boot_mem->physavail, &new_memlist,
	    0x2000, 0x2000))
		halt("Can't deduct msgbuf from physical memory list");

	/*
	 * Last chance to ask our booter questions ..
	 */

	/*
	 * For checkpoint-resume:
	 * Get kadb start address from prom "debugger-start" property,
	 * which is the same as segkp_limit at this point.
	 */
	debug_start_va = 0;
	(void) BOP_GETPROP(bootops, "debugger-start", (caddr_t)&debug_start_va);

	/*
	 * We're done with boot.  Just after this point in time, boot
	 * gets unmapped, so we can no longer rely on its services.
	 * Zero the bootops to indicate this fact.
	 */
	bootops = (struct bootops *)NULL;
	BOOTOPS_GONE();

	/*
	 * The kernel removes the pages that were allocated for it from
	 * the freelist, but we now have to find any -extra- pages that
	 * the prom has allocated for it's own book-keeping, and remove
	 * them from the freelist too. sigh.
	 */
	fix_prom_pages(phys_avail, cur);

	/*
	 * In order to avoid re-writing the load_l* routines to work around
	 * the viking/mbus non-cached ld bug, we drop out of super-scaler
	 * mode here (the bug only occurs in multiple instruction groups).
	 * We will return to super-scaler mode in the cache initialization
	 * stuff below.
	 */
	if (viking_ncload_bug)
		bpt_reg(0, MBAR_MIX);

	/*
	 * Copy in prom's level 1, level 2, and level 3 page tables,
	 * set up extra level 2 and level 3 page tables for the kernel,
	 * and switch to the kernel's context.
	 */
	hat_kern_setup();

	/*
	 * Set a flag to tell write_scb_int() that it can access V_TBR_WR_ADDR.
	 */
	tbr_wr_addr_inited = 1;

	/*
	 * Initialize VM system, and map kernel address space.
	 */
	kvm_init();

	/*
	 * Now that segkmem is functional, we can free the extra
	 * memory list that was used to take the final copy of phys_avail
	 */
	kmem_free(cur, memlist_sz);

	/*
	 * Setup a map for translating kernel virtual addresses;
	 * used by dump and libkvm.
	 */
	setup_kvpm();

	/*
	 * XXX	*hopefully* all these ENABLED messages are going to get
	 *	totally cleaned away eventually ..
	 */

	if (cache & CACHE_VAC) {
		if (use_cache) {
			/*
			 * XXX	A note for sun4m VAC machines:
			 *
			 * When we were loaded by boot (who used prom_alloc())
			 * we came in on non-cached pages.  We should probably
			 * do something about turning the cacheable bit on,
			 * before we start loading modules etc. since we do a
			 * fair amount of work before we get to kvm_init() ..
			 */
			cache_init();
			turn_cache_on(getprocessorid());
			shm_alignment = vac_size;
			vac_mask = MMU_PAGEMASK & (shm_alignment - 1);
			if (cache_mode && cache_mode[0])
				cmn_err(CE_CONT, "?vac: enabled in %s mode\n",
				    cache_mode);
			else
				cmn_err(CE_CONT, "?vac: enabled\n");
		} else {
			vac = 0;		/* indicate cache is off */
			cache &= CACHE_IOCOHERENT;
			shm_alignment = PAGESIZE;
			printf("VAC DISABLED\n");
		}
	} else if (cache & CACHE_PAC) {
		if (use_cache) {
			cache_init();
			turn_cache_on(getprocessorid());
			if (cache_mode && cache_mode[0])
				cmn_err(CE_CONT, "?pac: enabled - %s\n",
				    cache_mode);
			else
				cmn_err(CE_CONT, "?pac: enabled\n");
		} else {
			vac = 0;		/* indicate cache is off */
			cache &= CACHE_IOCOHERENT;	/* keep flag for ddi */
			if (cache_mode && cache_mode[0])
				cmn_err(CE_CONT, "%s PAC DISABLED\n",
					cache_mode);
			else
				cmn_err(CE_CONT, "PAC DISABLED\n");
		}
	}
#ifdef notdef
	if (viking) {
		if (use_ic)
			cmn_err(CE_CONT, "?\tInstruction Cache ENABLED\n");
		else
			cmn_err(CE_CONT, "\tInstruction Cache DISABLED\n");
		if (use_dc)
			cmn_err(CE_CONT, "?\tData Cache ENABLED\n");
		else
			cmn_err(CE_CONT, "\tData Cache DISABLED\n");
		if (use_store_buffer)
			cmn_err(CE_CONT, "?\tStore Buffer ENABLED\n");
		else
			cmn_err(CE_CONT, "\tStore Buffer DISABLED\n");
		if (use_mix)
			cmn_err(CE_CONT, "?\tSuperscalar ENABLED\n");
		else
			cmn_err(CE_CONT, "\tSuperscalar DISABLED\n");
		if (use_vik_prefetch)
			cmn_err(CE_CONT,
			    "?\tSuperSPARC Data Prefetcher ENABLED\n");
		else
			cmn_err(CE_CONT,
			    "?\tSuperSPARC Data Prefetcher DISABLED\n");
	}
	if (mxcc) {
		if (use_ec) {
			if (use_table_walk)
				cmn_err(CE_CONT,
					"?\tTable Walk Cacheable ENABLED\n");
			else
				cmn_err(CE_CONT,
					"\tTable Walk Cacheable DISABLED\n");
			cmn_err(CE_CONT, "?\tExternal Cache ENABLED\n");
		}
		else
			cmn_err(CE_CONT, "\tExternal Cache DISABLED\n");
		if (use_mxcc_prefetch)
			cmn_err(CE_CONT, "?\tMXCC Prefetch ENABLED\n");
		else
			cmn_err(CE_CONT, "\tMXCC Prefetch DISABLED\n");
		if (use_multiple_cmds)
			cmn_err(CE_CONT, "?\tMultiple Command ENABLED\n");
		else
			cmn_err(CE_CONT, "\tMultiple Command DISABLED\n");
	}
#endif

#ifdef IOC
	if (ioc) {
		if (use_ioc) {
			ioc_init();
			iocache_on();
			iocenable = 1;
		} else {
			iocenable = 0;
			ioc = 0;
		}
	}
#endif IOC

	/*
	 * do this -after- the caches are on
	 *
	 * Cpudelay shouldn't be used until the rootnexus
	 * is initialized anyway.
	 */
	setcpudelay();

	start_mon_clock();
	(void) splzs();			/* allow hi clock ints but not zs */

	/*
	 * Initialize the address map for cache consistent mappings
	 * to random pages, must be done after kernelmap initialized.
	 */
	ppmapinit();

	/*
	 * Allocate a vm slot for the dev mem driver, and 2 slots for dump.
	 * XXX - this should be done differently, see ppcopy.
	 */
	i = rmalloc(kernelmap, 1);
	mm_map = (caddr_t)kmxtob(i);
	i = rmalloc(kernelmap, DUMPPAGES);
	cur_dump_addr = dump_addr = kmxtob(i);

	/*
	 * Miscellaneous asynchronous fault initialization.
	 */
	for (i = 0; i < NCPU; i++) {
#ifdef	MP
		aflt_sync[i] = 0;
#endif	MP
		a_head[i] = MAX_AFLTS - 1;
		a_tail[i] = MAX_AFLTS - 1;
	}

	/*
	 * If the following is true, someone has patched
	 * phsymem to be less than the number of pages that
	 * the system actually has.  Remove pages until system
	 * memory is limited to the requested amount.  Since we
	 * have allocated page structures for all pages, we
	 * correct the amount of memory we want to remove
	 * by the size of the memory used to hold page structures
	 * for the non-used pages.
	 */
	if (physmem < npages) {
		u_int diff, off;
		struct page *pp;

		cmn_err(CE_WARN, "limiting physmem to %d pages", physmem);

		off = 0;
		diff = npages - physmem;
		diff -= mmu_btopr(diff * sizeof (struct page));
		while (diff--) {
			pp = page_create_va(&unused_pages_vp, off,
					MMU_PAGESIZE, PG_WAIT | PG_EXCL,
					&kas, (caddr_t)off);
			if (pp == NULL)
				cmn_err(CE_PANIC, "limited physmem too much!");
			page_io_unlock(pp);
			page_downgrade(pp);
			availrmem--;
			off += MMU_PAGESIZE;
		}
	}

	/*
	 * Print some facts and figures about the processors we found
	 */
	for (i = 0; i < NCPU; i++) {
		extern struct cpu cpus[];
		if (cpus[i].cpu_m.cpu_info)
			cmn_err(CE_CONT, "?%s", cpus[i].cpu_m.cpu_info);
	}

	/*
	 * When printing memory, show the total as physmem less
	 * that stolen by a debugger.
	 */
	cmn_err(CE_CONT, "?mem = %dK (0x%x)\n",
	    (physinstalled - dbug_mem) << (PAGESHIFT - 10),
	    ptob(physinstalled - dbug_mem));

	if (iom) {
		/* this map is in "pages" */
		mapinit(dvmamap, (long)SBUSMAP_SIZE,
			(u_long)SBUSMAP_BASE, "sbus map space", SBUSMAP_FRAG);
	}

	/*
	 * cmn_err doesn't do long long's and %u is treated
	 * just like %d, so we do this hack to get decimals
	 * > 2G printed.
	 */
	avmem = ctob((u_int)freemem);
	if (avmem >= (u_int)0x80000000)
		cmn_err(CE_CONT, "?avail mem = %d%d\n", avmem /
		    (1000 * 1000 * 1000), avmem % (1000 * 1000 * 1000));
	else
		cmn_err(CE_CONT, "?avail mem = %d\n", avmem);

	/*
	 * 1140792: Horrible hack for SunPC.
	 * 1144765: sun4m version of same horrible hack for SunPC.
	 *
	 * The SunPC driver needs a gap in the kernel
	 * address space that it can as_gap() into to get its various
	 * globs of DMA-able address space.  Problem is, there's no
	 * particular guarantee that there is such a gap - sigh.  So,
	 * right here, we ask the question "do we have one of these cards?"
	 * If so we "adjust things" appropriately.
	 *
	 * XXX	Of course the better question to ask is why the *^#$@ do
	 *	they need any kernel address space to do DMA into when DMA
	 *	is done in the iommu address space .. sigh.
	 */
	if (sunpc_present()) {
		/*
		 * Then we reserve 40Mbytes of 'unused' kernel virtual
		 * above segkp and below ncbase.  This is so awful.
		 */
		kv_reserve = 40 * (1 << 20);
	} else
		kv_reserve = 0;

	/*
	 * Initialize the segkp segment type.  We position it
	 * after the configured tables and buffers (whose end
	 * is given by econtig) and before V_WKBASE_ADDR.
	 * Also in this area are the debugger (if present)
	 * and segkmap (size SEGMAPSIZE).
	 */

	/* XXX - cache alignment? */
	va = (caddr_t) DEBUGADDR_END;
	ASSERT(((u_int)econtig & PAGEOFFSET) == 0);

	max_virt_segkp = mmu_btop(V_MX_SEGKP);
	max_phys_segkp = (physmem * 2);
	i = ptob(min(max_virt_segkp, max_phys_segkp));

	/*
	 * 1201049: segkmap assumes that its segment base and size are
	 * at least MAXBSIZE aligned.  We can guarantee this without
	 * introducing a hole in the kernel address space by ensuring
	 * that the previous segment -- segkp -- *ends* on a MAXBSIZE
	 * boundary.  (Avoiding a hole between segkp and segkmap is just
	 * paranoia in case anyone assumes that they're contiguous.)
	 *
	 * The following statement ensures that (va + i) is at least
	 * MAXBSIZE aligned.  Note that it also results in correct page
	 * alignment regardless of page size (exercise for the reader).
	 *
	 * XXX This is not needed anymore since segkmap moved away.
	 */
	i -= (u_int)va & MAXBOFFSET;

	rw_enter(&kas.a_lock, RW_WRITER);
	segkp = seg_alloc(&kas, va, i);
	if (segkp == NULL)
		cmn_err(CE_PANIC, "startup: cannot allocate segkp");
	if (segkp_create(segkp) != 0)
		cmn_err(CE_PANIC, "startup: segkp_create failed");
	rw_exit(&kas.a_lock);

	stklo = btop(segkp->s_base);
	stkhi = stklo + btop(segkp->s_size);

	/*
	 * Now create generic mapping segment.  This mapping
	 * goes SEGMAPSIZE beyond econtig.  But if the total
	 * virtual address is greater than the amount of free
	 * memory that is available, then we trim back the
	 * segment size to that amount
	 */
	va = econtig;

	/*
	 * 1201049: segkmap base address must be MAXBSIZE aligned.
	 *
	 * At this point, va is aligned to L3PTSIZE (256k) since it
	 * has the value of econtig which comes from alloc_base which
	 * is always L3PTSIZE (256K) aligned.
	 */
	ASSERT(((u_int)va & MAXBOFFSET) == 0);

	i = SEGMAPSIZE;
	if (i > mmu_ptob(freemem)) {
		i = roundup(mmu_ptob(freemem), L3PTSIZE);
	}
	i &= MAXBMASK;	/* 1201049: segkmap size must be MAXBSIZE aligned */


	/*
	 * Now verify that there are indeed enough room left.
	 */
	if ((va + i + kv_reserve) > ncbase) {
		cmn_err(CE_PANIC, "startup out of kernel vaddr", va, i, ncbase);
	}

	rw_enter(&kas.a_lock, RW_WRITER);
	segkmap = seg_alloc(&kas, va, i);
	if (segkmap == NULL)
		cmn_err(CE_PANIC, "cannot allocate segkmap");
	a.prot = PROT_READ | PROT_WRITE;

	a.vamask = (16 * MAXBSIZE) - 1;

	if (segmap_create(segkmap, (caddr_t)&a) != 0)
		panic("segmap_create segkmap");

	segkmap_lo = segkmap->s_base;
	segkmap_hi = segkmap_lo + segkmap->s_size;

	/*
	 * KAS_NO_FAULT macro in hat_srmmu.c relies on segkmap
	 * be aligned with L3 ptbl boundary. If this is changed,
	 * that macro needs to be changed accordingly.
	 */
	ASSERT(MMU_L2_OFF(segkmap_lo) == 0);
	ASSERT(MMU_L2_OFF(segkmap_hi) == 0);

	rw_exit(&kas.a_lock);

	/*
	 * Create a segment for kadb for checkpoint-resume.
	 */
	if (debug_start_va != 0) {
		rw_enter(&kas.a_lock, RW_WRITER);
		seg_debug = seg_alloc(&kas, (caddr_t)debug_start_va,
			DEBUGSIZE);
		if (seg_debug == NULL)
			cmn_err(CE_PANIC, "cannot allocate seg_debug");
		(void) segkmem_create(seg_debug, (caddr_t)NULL);
		rw_exit(&kas.a_lock);
	}

	/*
	 * Also create a segment for non-cached pte for CPR
	 */
	rw_enter(&kas.a_lock, RW_WRITER);
	seg_ncmem = seg_alloc(&kas, ncbase, (ncend - ncbase));
	if (seg_ncmem == NULL)
		cmn_err(CE_PANIC, "cannot allocate seg_ncmem");
	(void) segkmem_create(seg_ncmem, (caddr_t)NULL);
	rw_exit(&kas.a_lock);

	/*
	 * DO NOT MOVE THIS BEFORE mod_setup() is called since
	 * _db_install() is in a loadable module that will be
	 * loaded on demand.
	 */
	{
		/* XXX Is this stuff needed anymore? */
		extern int gdbon;
		extern void _db_install(void);

		if (gdbon)
			_db_install();
	}

	/*
	 * Perform tasks that get done after most of the VM
	 * initialization has been done but before the clock
	 * and other devices get started.
	 */
	kern_setup1();

	/*
	 * Garbage collect any kmem-freed memory that really came from
	 * boot but was allocated before kvseg was initialized, and send
	 * it back into segkmem.
	 */
	kmem_gc();

	/*
	 * Configure the root devinfo node.
	 */
	configure();		/* set up devices */

	init_intr_threads(CPU);

	iommu_init();
}

void
post_startup(void)
{
	extern void page_list_color();
	extern int cmem_allocated;

	/*
	 * Configure the rest of the system.
	 */

#ifdef notdef	/* see hwbcopy.c */
	/*
	 * Block Copy init
	 */
	hwbc_init();
#endif

	/*
	 * Hack for SX. Physical memory gets so fragmented that the SX cmem
	 * driver cannot reserve the requested amount of physical memory.
	 * This situation is only true when page coloring is turned on in the
	 * case of SuperSPARC processors with MXCC and HyperSPARC which supports
	 * virtual page coloring. What we do here is to defer page coloring,
	 * force load the SX cmem driver and then enable page coloring.
	 */
	ddi_install_driver("sx_cmem");
	cmem_allocated = 1;

	if (do_pg_coloring & PG_COLORING_DEFERRED) {
		page_list_color();
		ASSERT(do_pg_coloring == PG_COLORING_ON);
	}

	/*
	 * Perform forceloading tasks for /etc/system.
	 */
	(void) mod_sysctl(SYS_FORCELOAD, NULL);
	/*
	 * ON4.0: Force /proc module in until clock interrupt handle fixed
	 * ON4.0: This must be fixed or restated in /etc/systems.
	 */
	(void) modload("fs", "procfs");

	maxmem = freemem;

	/*
	 * Install the "real" pre-emption guards
	 */
	prom_set_preprom(kern_preprom);
	prom_set_postprom(kern_postprom);

	(void) spl0();		/* allow interrupts */

	memerr_init();
}

/*
 * These routines are called immediately before and
 * immediately after calling into the firmware.  The
 * firmware is significantly confused by preemption -
 * particularly on MP machines - but also on UP's too.
 *
 * We install the first set in mlsetup(), this set
 * is installed at the end of post_startup().
 *
 * On sun4m MP, there are some subtle problems that
 * await the unwary.  First, some background:
 *
 * 1.	The 'mutex' that's implemented by the OBP contains
 *	the notion of which CPU 'owns' the lock - and it is
 *	re-entrant on that CPU.
 *
 * This is one of the reasons why pre-emption doesn't work
 * - if you exit the OBP on a -different- CPU, it leaves
 * the OBP locked on the original CPU.
 *
 * 2.	The sun4m implementation uses cross-calls (directed
 *	software interrupts) to synchronize the processors at
 *	various points e.g. for mmu synchronization.
 *
 * This *may* lead to a problem:
 *
 * - A bound thread is executing in the PROM on CPU 0.
 *   The PROM holds the re-entrant spin lock on behalf of this CPU.
 *
 * - Another thread on CPU 1 executes a cross call.  This rips
 *   all the other CPUs away from whatever they were doing, and
 *   puts them into a holding pattern while the call completes.
 *   This includes the thread that was in the PROM.
 *
 * - The other thread then calls into the PROM on CPU 1, finds
 *   that because it's executing on the wrong CPU, it can't
 *   get the PROM spin lock, and spins forever.  Bad.
 *
 * Hence the assertion below.
 */

static void
kern_preprom(void)
{
	curthread->t_preempt++;
#ifdef DEBUG
	/* This check is bad and causes a panic, see bug# 1122898 */
#if 0
	{
		struct cpu *cp, *this = curthread->t_cpu;

		for (cp = this->cpu_next; cp && cp != this; cp = cp->cpu_next)
			if (!panicstr && cp->cpu_m.in_prom == 1)
				panic("cross-call deadlock");

		this->cpu_m.in_prom++;
	}
#endif
	/* end This check is bad and causes a panic */
#endif
}

static void
kern_postprom(void)
{
#ifdef DEBUG
	curthread->t_cpu->cpu_m.in_prom = 0;
#endif
	curthread->t_preempt--;
}

#ifdef IOC
static void
ioc_init(void)
{
	int ioc_addr, i;

	/* invalid all ioc lines */
	if (ioc) {
		for (i = 0; i < IOC_CACHE_LINES; i++) {
			ioc_addr = i * IOC_LINE_MAP;
			ioc_addr &= IOC_ADDR_MSK;
			ioc_addr >>= IOC_RW_SHIFT;
			ioc_addr |= IOC_TAG_PHYS_ADDR;
			do_load_ioc(ioc_addr, 0);
		}
	}
}

/*
 * Turn on the io cache during startup.
 */
static void
iocache_on(void)
{
	(*VMEBUS_ICR) |= VMEICP_IOC;
}

#endif IOC

/*
 * Add to a memory list.
 */
static void
physlist_add(
	u_longlong_t	start,
	u_longlong_t	len,
	struct memlist	**memlistp)
{
	struct memlist *cur, *new, *last;
	u_longlong_t end = start + len;

	new = *memlistp;
	new->address = start;
	new->size = len;
	*memlistp = new + 1;
	for (cur = phys_avail; cur; cur = cur->next) {
		last = cur;
		if (cur->address >= end) {
			new->next = cur;
			new->prev = cur->prev;
			cur->prev = new;
			if (cur == phys_avail)
				phys_avail = new;
			else
				new->prev->next = new;
			return;
		}
		if (cur->address + cur->size > start)
			panic("munged phys_avail list");
	}
	new->next = NULL;
	new->prev = last;
	last->next = new;
}

/*
 * See 1140792. "do we have a SunPC card?"
 */
static int
sunpc_present(void)
{
	static char *names[] = {
		"SUNW,SunPC",
		"SUNW,sdos",
		NULL
	};
	int i;

	for (i = 0; names[i] != NULL; i++) {
		dnode_t sp[OBP_STACKDEPTH];
		pstack_t *stk;
		dnode_t node;

		stk = prom_stack_init(sp, sizeof (sp));
		node = prom_findnode_byname(prom_rootnode(), names[i], stk);
		prom_stack_fini(stk);

		if (node != OBP_NONODE && node != OBP_BADNODE)
			return (1);
	}

	return (0);
}

/*
 * kphysm_init() tackles the problem of initializing physical memory.
 * The old startup made some assumptions about the kernel living in
 * physically contiguous space which is no longer valid.
 */
static void
kphysm_init(page_t *pp, struct memseg *memsegp, u_int npages, u_int blks)
{
	int index;
	struct memlist *pmem;
	struct memseg *memsegp_tmp;
	struct memseg **pmemseg;
	u_int np, dopages;
	u_int first_page;
	u_int num_free_pages = 0;
	struct memseg *mssort;
	struct memseg **psort;
	int curmax;
	struct memseg *largest_memseg;
	struct memseg *oldmemseglist;
	extern void page_coloring_init(void);
	struct memseg *msegend;

	oldmemseglist = memsegp;
	memsegp_tmp = memsegp;

	for (index = 0, pmem = phys_avail; pmem; index += num_free_pages,
	    pmem = pmem->next) {

		first_page = mmu_btop(pmem->address);
		num_free_pages = mmu_btop(pmem->size);

		ASSERT(num_free_pages <= (npages - index));
/*
 * XXX
 * Delete the following after the hat_getpfnum error return changes from
 * 0 to -1.  As it is, physical page 0 is confused with the error return.
 */
		/*
		 * Don't put page 0 on the free page list.
		 */
		if (first_page == 0 && num_free_pages > (u_int)0) {
			first_page++;
			num_free_pages--;
		}
/* XXX */

		if (num_free_pages)
			page_init(&pp[index], num_free_pages, first_page,
			    memsegp_tmp++);
	}

	/*
	 * Page coloring must be deferred until after we try to load the
	 * SX cmem driver. This is because memory gets too fragmented and
	 * the cmem driver cannot allocate contiguous memory. We force load
	 * the SX cmem driver in post_startup().
	 */
	if (do_pg_coloring && deferred_pg_coloring) {
		do_pg_coloring |= PG_COLORING_DEFERRED;
	} else {
		deferred_pg_coloring = 0;
		page_coloring_init();
	}

	/*
	 * Initialize memory free list.
	 */
	np = 0;
	msegend = memsegp + blks;
	for (memsegp_tmp = memsegp; memsegp_tmp;
	    memsegp_tmp = memsegp_tmp->next) {
		ASSERT(memsegp_tmp < msegend);
		first_page = memsegp_tmp->pages_base;
		dopages = memsegp_tmp->pages_end - first_page;
/*
 * XXX
 * Delete the following after the hat_getpfnum error return changes from
 * 0 to -1.  As it is, physical page 0 is confused with the error return.
 */
		/*
		 * Don't put page 0 on the free page list.
		 */
		if (first_page == 0 && dopages > (u_int)0) {
			first_page++;
			dopages--;
		}
/* XXX */
		if ((np + dopages) > npages)
			dopages = npages - np;
		np += dopages;
		if (dopages != 0) {
			if (vac && do_pg_coloring)
				set_pages_vcolor(first_page,
					first_page + dopages);
			memialloc(first_page, first_page, first_page + dopages);
		}
	}

	/*
	 * sort the memseg list so that searches that miss on the
	 * last_memseg hint will search the largest segments first.
	 */
	psort = &mssort;
	while (oldmemseglist != NULL) {
		curmax = 0;
		/* find the largest memseg */
		for (memsegp_tmp = oldmemseglist; memsegp_tmp;
		    memsegp_tmp = memsegp_tmp->next) {
			first_page = memsegp_tmp->pages_base;
			dopages = memsegp_tmp->pages_end - first_page;
			if (dopages > curmax) {
				largest_memseg =  memsegp_tmp;
				curmax = dopages;
			}
		}
		/* remove it from the list */
		memsegp_tmp = oldmemseglist;
		pmemseg = &oldmemseglist;
		do {
			if (memsegp_tmp == largest_memseg)
				*pmemseg = memsegp_tmp->next;
			else
				pmemseg = &memsegp_tmp->next;
		} while ((memsegp_tmp = memsegp_tmp->next) != NULL);
		/* insert onto new list */
		largest_memseg->next = NULL;
		*psort = largest_memseg;
		psort = &largest_memseg->next;
	}
	memsegs = mssort;

	build_pfn_hash();
}

/*
 * Kernel VM initialization.
 * Assumptions about kernel address space ordering:
 *	(1) gap (user space)
 *	(2) kernel text
 *	(3) kernel data/bss
 *	(4) gap
 *	(5) kernel data structures
 *	(6) gap
 *	(7) debugger (optional)
 *	(8) monitor
 *	(9) gap (possibly null)
 *	(10) dvma
 *	(11) devices
 */
static void
kvm_init(void)
{
	register caddr_t va;
	u_int range_size, range_base, range_end;
	u_int pfnum;
	struct memlist *cur;
	extern caddr_t e_text;
	extern caddr_t e_data;
	extern int segkmem_ready;
	u_int valloc_base = roundup((u_int)e_data, PAGESIZE);

#ifndef KVM_DEBUG
#define	KVM_DEBUG 0	/* 0 = no debugging, 1 = debugging */
#endif

#if KVM_DEBUG > 0
#define	KVM_HERE \
	printf("kvm_init: checkpoint %d line %d\n", ++kvm_here, __LINE__);
#define	KVM_DONE	{ printf("kvm_init: all done\n"); kvm_here = 0; }
	int kvm_here = 0;
#else
#define	KVM_HERE
#define	KVM_DONE
#endif

KVM_HERE
	/*
	 * Put the kernel segments in kernel address space.  Make it a
	 * "kernel memory" segment objects.
	 */
	rw_enter(&kas.a_lock, RW_WRITER);

	(void) seg_attach(&kas, (caddr_t)KERNELBASE,
	    (u_int)(e_data - KERNELBASE), &ktextseg);
	(void) segkmem_create(&ktextseg, (caddr_t)NULL);

KVM_HERE
	(void) seg_attach(&kas, (caddr_t)valloc_base, (u_int)econtig -
		(u_int)valloc_base, &kvalloc);
	(void) segkmem_create(&kvalloc, (caddr_t)NULL);

KVM_HERE
	/*
	 * We're about to map out /boot.  This is the beginning of the
	 * system resource management transition. We can no longer
	 * call into /boot for I/O or memory allocations.
	 */
	(void) seg_attach(&kas, (caddr_t)SYSBASE,
	    (u_int)(Syslimit - SYSBASE), &kvseg);
	(void) segkmem_create(&kvseg, (caddr_t)Sysmap);

	rw_exit(&kas.a_lock);

KVM_HERE
	rw_enter(&kas.a_lock, RW_READER);

	/*
	 * Now we can ask segkmem for memory instead of boot.
	 */
	segkmem_ready = 1;

	/*
	 * Invalidate unused portion of the region managed by kernelmap.
	 * (We know that the PROM never allocates any mappings here by
	 * itself without updating the 'virt-avail' list, so that we can
	 * simply render anything that is on the 'virt-avail' list invalid)
	 *
	 * The u_int cast for address and size are needed to workaround
	 * compiler bug 1124059 and are ok because these are virtual
	 * addresses.
	 */
	for (cur = virt_avail; cur && (u_int)cur->address < (u_int) Syslimit;
	    cur = cur->next) {
		range_base = MAX((u_int)cur->address, (u_int)Sysbase);
		range_end  = MIN((u_int)(cur->address + cur->size),
		    (u_int)Syslimit);
		if (range_end > range_base)
			as_setprot(&kas, (caddr_t)range_base,
			    range_end - range_base, 0);
	}

	/*
	 * Validate any used portions, there may be several fragments of
	 * 'used' virtual memory in this range, so we hunt 'em all down.
	 */
	for (cur = virt_avail; cur->next; cur = cur->next) {
		if ((range_base = cur->address + cur->size) < (u_int) Sysbase)
			continue;
		if (range_base > (u_int) Syslimit)
			break;
		range_size = cur->next->address - range_base;

		/* skip stuff in the obplrg mapping range, frame buffers */
		if (range_base < (u_int) BIG_OBP_MAP ||
		    range_base >= (u_int) BIG_OBP_MAP_END) {
			(void) as_fault(kas.a_hat, &kas, (caddr_t)range_base,
				range_size, F_SOFTLOCK, S_OTHER);
		}
		(void) as_setprot(&kas, (caddr_t)range_base, range_size,
			PROT_READ | PROT_WRITE | PROT_EXEC);
	}

	rw_exit(&kas.a_lock);

KVM_HERE

	/*
	 * Allocate and lock level3 (and possibly level 2) page tables
	 * for the range of addresses used by the execargs and ppmap{in,out}.
	 */
	srmmu_reserve(&kas, (caddr_t)ARGSBASE, NCARGS, 0);
	srmmu_reserve(&kas, (caddr_t)PPMAPBASE, PPMAPSIZE, 0);

KVM_HERE

	/*
	 * Now create a segment for the DVMA virtual
	 * addresses using the segkmem segment driver.
	 */
	rw_enter(&kas.a_lock, RW_WRITER);
	(void) seg_attach(&kas, DVMA, (u_int)ctob(dvmasize), &kdvmaseg);
	(void) segkmem_create(&kdvmaseg, (caddr_t)NULL);
	rw_exit(&kas.a_lock);

KVM_HERE

	/*
	 * Find the begining page frames of the kernel data
	 * segment and the ending page frame (-1) for bss.
	 */
	if ((pfnum = va_to_pfn((caddr_t)roundup((u_int)e_text,
	    DATA_ALIGN))) != -1)
		kpfn_dataseg = pfnum;
	if ((pfnum = va_to_pfn(e_data)) != -1)
		kpfn_endbss = pfnum;

	/*
	 * Verify all memory pages that have mappings, have a mapping
	 * on the mapping list; take this out when we are sure things
	 * work.
	 */
	if (chkkas) {
		for (va = (caddr_t)SYSBASE; va < DVMA; va += PAGESIZE) {
			struct page *pp;
			struct pte *pte;
			struct pte tpte;
			int level;

			pte = (struct pte *)srmmu_ptefind_nolock(&kas,
				(caddr_t)va, &level);

			mmu_readpte(pte, &tpte);
			if (pte_valid(&tpte) &&
			    pf_is_memory(MAKE_PFNUM(&tpte))) {
				pp = page_numtopp_nolock(MAKE_PFNUM(&tpte));
				if (pp && pp->p_mapping == NULL) {
					cmn_err(CE_PANIC,
	"mapping page at va %x, no mapping on mapping list for pp %x \n",
						va, pp);
				}
			}
		}
	}

KVM_DONE
}

static void
setup_kvpm(void)
{
	struct pte *pte, tpte;
	u_int va;
	int level, i = 0;
	u_int pages = 0;
	u_int nextpfnum, lastpfnum;
	u_int off, span, npgs, pfn;

	lastpfnum = (u_int)-1;
	/*LINTED: variable may be used before set: span*/
	for (va = (u_int)KERNELBASE; va < (u_int)econtig; va += span) {
		pte = srmmu_ptefind_nolock(&kas, (caddr_t)va, &level);
		switch (level) {
		case 1:
			off = MMU_L1_OFF(va);
			span = L2PTSIZE - off;
			break;
		case 2:
			off = MMU_L2_OFF(va);
			span = L3PTSIZE - off;
			break;
		case 3:
			off = 0;
			span = MMU_PAGESIZE;
			break;
		default:
			cmn_err(CE_PANIC, "Unknown pte level: %d", level);
			break;
		}
		npgs = btop(span);
		mmu_readpte(pte, &tpte);
		if (pte_valid(&tpte)) {
			if (lastpfnum == (u_int)-1) {
				lastpfnum = tpte.PhysicalPageNumber + btop(off);
				kvtopdata.kvtopmap[i].kvpm_vaddr = va;
				kvtopdata.kvtopmap[i].kvpm_pfnum = lastpfnum;
				pages = npgs;
				nextpfnum = lastpfnum + npgs;
			} else if (nextpfnum == tpte.PhysicalPageNumber) {
				nextpfnum = tpte.PhysicalPageNumber + npgs;
				pages += npgs;
			} else {
				kvtopdata.kvtopmap[i].kvpm_len = pages;
				lastpfnum = tpte.PhysicalPageNumber;
				pages = npgs;
				nextpfnum = lastpfnum + npgs;
				i++;
				if (i >= NKVTOPENTS) {
					cmn_err(CE_WARN, "out of kvtopents");
					pages = 0;
					break;
				}
				kvtopdata.kvtopmap[i].kvpm_vaddr = va;
				kvtopdata.kvtopmap[i].kvpm_pfnum = lastpfnum;
			}
		} else if (lastpfnum != (u_int)-1) {
			lastpfnum = (u_int)-1;
			kvtopdata.kvtopmap[i].kvpm_len = pages;
			pages = 0;
			i++;
			if (i >= NKVTOPENTS) {
				cmn_err(CE_WARN, "out of kvtopents");
				break;
			}
		}
	}
	if (pages) {
		kvtopdata.kvtopmap[i].kvpm_len =
			pages  - btop((u_int)econtig - va);
		i++;
		pages = 0;
	}
	lastpfnum = (u_int)-1;
	/*
	 * Pages allocated early from sysmap region that don't
	 * have page structures and need to be entered in to the
	 * kvtop array for libkvm.  The rule for memory pages is:
	 * it is either covered by a page structure or included in
	 * kvtopdata.
	 */
	for (va = (u_int)Sysbase; va < (u_int)startup_alloc_vaddr;
	    va += PAGESIZE) {
		pfn = va_to_pfn((caddr_t)va);
		if (pfn != (u_int)-1) {
			if (lastpfnum == (u_int)-1) {
				lastpfnum = pfn;
				kvtopdata.kvtopmap[i].kvpm_vaddr = va;
				kvtopdata.kvtopmap[i].kvpm_pfnum = lastpfnum;
				pages = 1;
			} else if (lastpfnum+1 == pfn) {
				lastpfnum = pfn;
				pages++;
			} else {
				kvtopdata.kvtopmap[i].kvpm_len = pages;
				lastpfnum = pfn;
				pages = 1;
				i++;
				if (i >= NKVTOPENTS) {
					cmn_err(CE_WARN, "out of kvtopents");
					break;
				}
				kvtopdata.kvtopmap[i].kvpm_vaddr = va;
				kvtopdata.kvtopmap[i].kvpm_pfnum = lastpfnum;
			}
		} else if (lastpfnum != (u_int)-1) {
			lastpfnum = (u_int)-1;
			kvtopdata.kvtopmap[i].kvpm_len = pages;
			pages = 0;
			i++;
			if (i >= NKVTOPENTS) {
				cmn_err(CE_WARN, "out of kvtopents");
				break;
			}
		}
	}
	if (pages)
		kvtopdata.kvtopmap[i].kvpm_len = pages;
	else
		i--;

	kvtopdata.hdr.version = KVTOPD_VER;
	kvtopdata.hdr.nentries = i + 1;
	kvtopdata.hdr.pagesize = MMU_PAGESIZE;

	msgbuf.msg_map = va_to_pa((u_int)&kvtopdata);
}

/*
 * Verify the SX existence by checking OBP's sx node
 */
void
sx_vrfy_exist(void)
{
	dnode_t nodeid;
	dnode_t sp[OBP_STACKDEPTH];
	pstack_t *stk;

	/*
	 * Note: O/S devinfo may not have been setup at early stage of startup.
	 *	 So, we need to search the prom tree instead.
	 */
	stk = prom_stack_init(sp, sizeof (sp));
	nodeid = prom_findnode_byname(prom_nextnode(0), "SUNW,sx", stk);
	prom_stack_fini(stk);
	if (nodeid != OBP_NONODE)
		sx_ctlr_present++;
}
