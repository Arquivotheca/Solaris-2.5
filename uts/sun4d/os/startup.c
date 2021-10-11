/*
 * Copyright (c) 1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)startup.c 1.70     95/10/19 SMI"

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/systm.h>
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
#include <sys/bcopy_if.h>
#include <sys/machsystm.h>
#include <sys/autoconf.h>
#include <sys/dki_lock.h>

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
#include <sys/varargs.h>
#include <sys/promif.h>
#include <sys/modctl.h>		/* for "procfs" hack */
#include <sys/kvtopdata.h>

#include <sys/consdev.h>
#include <sys/frame.h>

#define	SUNDDI_IMPL		/* so sunddi.h will not redefine splx() et al */
#include <sys/sunddi.h>
#include <sys/ddidmareq.h>
#include <sys/clock.h>
#include <sys/pte.h>
#include <sys/scb.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/stack.h>
#include <sys/eeprom.h>
#include <sys/led.h>
#include <sys/memerr.h>
#include <sys/trap.h>
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
#include <sys/syserr.h>
#include <sys/physaddr.h>

/*
 * External Routines:
 */
extern void hat_kern_setup(void);
extern void init_intr_threads(struct cpu *);
extern void kncinit();
extern void param_calc(int);
extern void param_init();
extern void level15_post_startup(void);
extern void resetcpudelay(void);
extern u_int intr_mxcc_cntrl_get(void);
extern void intr_mxcc_cntrl_set(u_int value);

/*
 * External Data
 */
extern lksblk_t *lksblks_head;
extern int obpdebug;
extern int maxusers;
extern char DVMA[];
extern volatile u_int aflt_sync[];
extern u_int a_head[];
extern u_int a_tail[];
extern kmutex_t long_print_lock;
extern struct cpu cpu0;

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

int chkkas = 1;		/* check kernel address space after kvm_init */

struct bootops *bootops = 0;	/* passed in from boot in %o2 */
char obpdebugger[32] = "misc/obpsym";

caddr_t econtig;		/* end of contiguous kernel */

caddr_t s_text;			/* start of kernel text segment */
caddr_t e_text;			/* end of kernel text segment */
caddr_t s_data;			/* start of kernel data segment */
caddr_t e_data;			/* end of kernel data segment */

struct map *dvmamap;		/* map to manage usable dvma space */

u_int shm_alignment = 0;	/* VAC address consistency modulus */
struct memlist *phys_install;	/* Total installed physical memory */
struct memlist *phys_avail;	/* Available (unreserved) physical memory */
struct memlist *virt_avail;	/* Available (unmapped?) virtual memory */

u_int n_bootbus;		/* number of BootBusses on system */
u_int bootbusses;		/* CPUSET of cpus owning bootbusses */

u_int use_mxcc_prefetch = 0;	/* set me to 1 to slow down DBMS */
u_int use_multiple_cmds = 1;	/* enable MC for MXCC 4.0 and above */
u_int really_use_multiple_cmds = 0;	/* use at own risk */

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
struct	memseg *memseg_base;
u_int	memseg_sz;		/* Used to translate a va to page */
struct	vnode unused_pages_vp;	/* used to limit physmem */
struct	vnode unused_pages_vp2;	/* a hack because we support >4G memory */

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
static void check_prom_error(void);
static void kphysm_init();
static void kvm_init();
static void physlist_add(u_longlong_t, u_longlong_t, struct memlist **);
static void setup_kvpm();
static void set_bootbusses(void);
void mxcc_knobs(void);	/* called by startup() and mp_startup() */
void bw_knobs(void);	/* called by startup() and mp_startup() */

static void kern_preprom(void);
static void kern_postprom(void);

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
 * which are 4k on sun4d. Thus, for us the first 4 pages
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
 *                Virtual memory layout.
 *              /-----------------------\
 *              |       INVALID         |			(1M)
 * 0xFFF00000  -|-----------------------|- DVMA
 *              |       monitor         |                       (2 M)
 * 0xFFD00000  -|-----------------------|- MONSTART/Syslimit/E_Syslimit
 *              |       Sysmap          |                       (14 M)
 * 0xFEF00000  -|-----------------------|-
 *              |  OBP "large" mappings |
 *              |          OR           |                       (11 M)
 *              |  Sysmap, if not used  |
 *              |       by OBP.         |
 * 0xFE400000  -|-----------------------|-
 *              |                       |
 *              |                       |
 *              |       Sysmap          |                       (146 M)
 *              |                       |
 *              |                       |
 * 0xF5200000  -|-----------------------|-
 *              |       debugger        |
 *              |          OR           |                       (1 M)
 *              |       Sysmap, if no   |
 *              |       debugger.       |
 * 0xF5100000  -|-----------------------|- DEBUGSTART (Don't changes)
 *              |                       |
 *              |                       |
 *              |                       |
 *              |       Sysmap          |			(79M)
 *              |                       |
 *              |                       |
 * 0xF0200000  -|-----------------------|- SYSBASE
 *              |  exec args area       | 		ARGSSIZE (1 M)
 * 0xF0100000  -|-----------------------|- ARGSBASE
 *              | quick page map region |  		PPMAPSIZE (.5 M)
 * 0xF0080000  -|-----------------------|- PPMAPBASE
 *              |                       |
 *              |  Segmap segment       | 		SEGMAPSIZE (32 M)
 *              |                       |
 * 0xEE080000  -|-----------------------|- SEGMAPBASE
 *              |                       |
 *              |                       |
 *              |        segkp          |
 *              |                       |
 *              |                       |
 *             -|-----------------------|- econtig
 *              |    page structures    |
 *             -|-----------------------|- end
 *              |       kernel          |
 *             -|-----------------------|
 *              |   trap table (4k)     |
 * 0xE0040000  -|-----------------------|- start
 *              |  user copy red zone   |
 *              |       (invalid)       |                       (256 K)
 * 0xE0000000  -|-----------------------|- KERNELBASE
 *              |       user stack      |
 *              :                       :
 *              :                       :
 *              |       user data       |
 *             -|-----------------------|-
 *              |       user text       |
 * 0x00002000  -|-----------------------|-
 *              |       invalid         |
 * 0x00000000  _|_______________________|
 *
 */

#ifdef NOT_SUPPORTED
/* Variables for large block allocations */
extern u_short *l1_freecnt;
extern u_char *l2_freecnt;
extern int l1_free_tblsz, l2_free_tblsz;
#endif /* NOT_SUPPORTED */

int	nsegkp;
int	sysmapsz;


static int itotalpgs;

/*
 * Machine-dependent startup code
 */
void
startup()
{
	register unsigned i;
	u_int npages;
	pa_t tmp_pa;
	struct segmap_crargs a;
	int dbug_mem, memblocks;
	struct pte tpte;
	u_int pp_giveback;
	u_int addr;
	caddr_t memspace;
	u_int memspace_sz;
	u_int nppstr;
	u_int segkp_limit = 0;
	u_int align;
	u_int freetbl_sz = 0;
	void map_ctxtbl(void);
	u_longlong_t avmem;
	caddr_t va;
	caddr_t real_base, alloc_base;
	int real_sz, alloc_sz;
	int extra;
	pa_t extra_phys;
	caddr_t extra_virt;
	struct memlist *memlist, *new_memlist;
	struct memlist *cur;
	int	max_virt_segkp;
	int	max_phys_segkp;
	int	segkp_len;
	int	il3pgs, ctxpgs;
	caddr_t va_cur;

#if defined(SAS)
	u_int mapaddr;
#endif /* !SAS */
	extern char Syslimit[];
	extern caddr_t e_data;
	extern void mt_lock_init();
	extern void mt_trace_init();
	extern caddr_t mm_map, cur_dump_addr, dump_addr;
	extern void setup_ddi(void);
	extern int srmmu_use_inv;
	extern int enable_sm_wa;
	extern int disable_1151159;

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
	/* XXX Payne - added Flag */
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
	if (va_to_pa(real_base) == -1 && real_base != alloc_base) {
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
	extra_phys = va_to_pa(extra_virt);
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

	/*
	 * Get the list of physically available memory to size
	 * the number of page structures needed.
	 */
	size_physavail(bootops->boot_mem->physavail, &npages, &memblocks);
	npages += extra;

#ifdef NOT_SUPPORTED
	l1_free_tblsz = (physmax >> MMU_STD_FIRSTSHIFT) + 1;
	l2_free_tblsz = (physmax >> MMU_STD_SECONDSHIFT) + 1;
	freetbl_sz = l1_free_tblsz * sizeof (*l1_freecnt) + l2_free_tblsz *
		sizeof (*l2_freecnt);
#endif /* NOT_SUPPORTED */

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
		pte2hme_hashsz = 1 << highbit(pte2hme_hashsz);	/* IMPORTANT */
		pte2hmehash_sz = pte2hme_hashsz * sizeof (struct srhment *);
	}

	/*
	 * Some of the locks depend on page_hashsz being setup beforehand.
	 */
	mt_lock_init();

	/*
	 * The memseg list is for the chunks of physical memory that
	 * will be managed by the vm system.  The number calculated is
	 * a guess as boot may fragment it more when memory allocations
	 * are made before kphysm_init(). Currently, there are two
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
	 * Now align the base to 64 byte boundary.
	 */
	pp_base = (page_t *)roundup((u_int)pp_base,  64);
	ASSERT(((u_int)pp_base & 0x3F) == 0);

#ifdef NOT_SUPPORTED
	l1_freecnt = (u_short *)((u_int)pp_base + pp_sz);
	l2_freecnt = (u_char *)(l1_freecnt + l1_free_tblsz);
	real_base = (caddr_t)roundup((u_int)l2_freecnt + l2_free_tblsz,
				MMU_PAGESIZE);
#else
	real_base = (caddr_t)roundup((u_int)pp_base + pp_sz, MMU_PAGESIZE);
#endif /* NOT_SUPPORTED */

	econtig = real_base;
	ASSERT(((u_int)econtig & MMU_PAGEOFFSET) == 0);

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
	kernelmap_sz = KERNELMAP_SZ(2);
	memspace_sz =  memlist_sz + kernelmap_sz;

	memspace = (caddr_t)BOP_ALLOC(bootops, startup_alloc_vaddr,
	    memspace_sz, BO_NO_ALIGN);

	startup_alloc_vaddr += memspace_sz;
	startup_alloc_size += memspace_sz;
	if (memspace == NULL)
		halt("Boot allocation failed.");
	bzero(memspace, memspace_sz);

	memlist = (struct memlist *)memspace;
	kernelmap = (struct map *)((u_int)memlist + memlist_sz);

	mapinit(kernelmap, (long)(SYSPTSIZE - 1), (u_long)1,
		"kernel map", kernelmap_sz / sizeof (struct map));
	sysmapsz = SYSPTSIZE - 1;

	/*
	 * take the most current snapshot we can by calling mem-update
	 */
	if (BOP_GETPROPLEN(bootops, "memory-update") == 0)
		BOP_GETPROP(bootops, "memory-update", NULL);

	/*
	 * Remove the space used by BOP_ALLOC and msgbuf from the kernelmap.
	 */
	mutex_enter(&maplock(kernelmap));
	if (rmget(kernelmap, btop(startup_alloc_size), 1) == 0)
		prom_panic("can't make initial kernelmap allocation");
	sysmapsz -= (btop(startup_alloc_size));

	/*
	 * Remove the area actually used by the OBP (if any)
	 */
	virt_avail = memlist;
	copy_memlist(bootops->boot_mem->virtavail, &memlist);
	for (cur = virt_avail; cur->next; cur = cur->next) {
		u_int   range_base = cur->address + cur->size;

		if (range_base > (u_int)startup_alloc_vaddr &&
			range_base < (u_int)Syslimit) {

			u_int   range_size;

			range_size = cur->next->address - range_base;
			if (rmget(kernelmap, btop(range_size),
				btop(range_base - (u_int)Sysbase)) == 0)
					prom_panic("can't remove OBP hole");
#ifdef VADDR_DEBUG
			else
				printf("startup: rmget: size=0x%x, base=0x%x\n",
					range_size, range_base);
#endif VADDR_DEBUG
			sysmapsz -= btop(range_size);
		}
	}
	mutex_exit(&maplock(kernelmap));

	phys_avail = memlist;
	if (!copy_physavail(bootops->boot_mem->physavail, &memlist,
	    0x2000, 0x2000))
		halt("Can't deduct msgbuf from physical memory list");

	/*
	 * Add any extra mem after e_text to physavail list.
	 */
	if (extra) {
		u_longlong_t start = extra_phys;
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

#if defined(VIKING_BUG_1151159)
	/*
	 * Certain cpu revisions do not enable this workaround by default.
	 * In these situations:
	 * The instructions for activating this workaround are to set
	 * enable_sm_wa=1 in /etc/system. set_cpu_revision() is the routine
	 * which handles the activation of dynamic workarounds. The boot
	 * cpu executes set_cpu_revision() before /etc/system is read and
	 * processed. So the boot cpu must check for the need to activate
	 * this workaround as soon as the /etc/system file has been read
	 * and processed. Checking for the need to activate this workaround
	 * in set_cpu_revision() works correctly for all other cpus.
	 */
	if ((enable_sm_wa) && (disable_1151159 == 0)) {
		extern void vik_1151159_wa(void);

		vik_1151159_wa();
	}
#endif VIKING_BUG_1151159

	/*
	 * Initialize system parameters
	 */
	param_init();

	/*
	 * If obpdebug is set, load the obpsym debugger module, now.
	 */

	if (obpdebug)
		(void) modload(NULL, obpdebugger);

	mutex_init(&long_print_lock, "long print lock",
	    MUTEX_DEFAULT, DEFAULT_WT);

	check_prom_error();

	/*
	 * apply use_mxcc_prefetch and use_multiple_cmds to this CPU
	 */
	mxcc_knobs();

	/*
	 * apply bw_lccnt to this CPU
	 */
	bw_knobs();

	/*
	 * check that the machine state (supersparc/mxcc) is reasonable
	 */
	check_options(1);


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
	 *	Can not move these table allocations before the space
	 *	allocation for page structs. The reason is that these depend on
	 *	the value of v.v_proc which is computed in param_calc() that
	 *	is called after the page structure allocation.
	 *
	 *	If we could allocate these before we could have save some
	 *	space in allocation page structures. We would not have
	 *	to allocate page structures for these pages. Too bad!
	 */
	nctxs = 1 << 16;
	nctxs = MIN(nctxs, (v.v_proc + 1));

	ASSERT(MMU_L2_OFF(alloc_base) == 0);
	ASSERT(MMU_L3_OFF(real_base) == 0);

	/*
	 * Get some srmmu data structures to start with.
	 */
	init_srmmu_var();

	ctxpgs = roundup(nctxs * sizeof (struct ctx), MMU_PAGESIZE);
	ctxpgs = mmu_btop(ctxpgs);

	ialloc_ptbl();
	itotalpgs = ctxpgs;
	alloc_sz = roundup(real_base + mmu_ptob(itotalpgs) - alloc_base,
			L3PTSIZE);

	if (alloc_sz > 0) {
		boot_alloc(alloc_base, alloc_sz, L3PTSIZE);
		alloc_base += alloc_sz;
	}
	va_cur = real_base;
	ctxs = (struct ctx *)va_cur;
	va_cur += (nctxs * sizeof (struct ctx));
	ectxs = (struct ctx *)va_cur;
	real_base = va_cur;

	econtig = alloc_base;

	map_ctxtbl();		/* sets contexts and econtexts */

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

	set_bootbusses();	/* initialize global "bootbusses" */

	led_blink_all();	/* indicate the kernel controls machine */

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
	 * Copy in prom's level 1, level 2, and level 3 page tables,
	 * set up extra level 2 and level 3 page tables for the kernel,
	 * and switch to the kernel's context.
	 */
	hat_kern_setup();

	/*
	 * Initialize VM system, and map kernel address space.
	 */
	kvm_init();

	/*
	 * Now that segkmem is fully functional, we can free the extra
	 * memory list that was used to take the final copy of phys_avail
	 */
	kmem_free(cur, memlist_sz);

	/*
	 * Using the pte update algorithm that invalidates is fatal
	 * during kvm_init because it temporarily unmaps the code
	 * and data used in the algorithm.  To prevent that we don't
	 * allow the invalidate algorithm until after kvm_init.  Note
	 * the kernel is only running on one cpu during kvm_init so
	 * it is ok to use a simpler pte update that doesn't invalidate.
	 */
	srmmu_use_inv = 1;

	/*
	 * Setup a map for translating kernel virtual addresses;
	 * used by dump and libkvm.
	 */
	setup_kvpm();

	led_blink_all();	/* indicate the kernel controls machine */

	start_mon_clock();
	/*
	 * xxx actually, we're already at ipl12 before this call
	 * so this spl is unnecessary.
	 */
	(void) splzs();			/* allow hi clock ints but not zs */

	/*
	 * Allocate a vm slot for the dev mem driver, and 2 slots for dump.
	 * XXX - this should be done differently, see ppcopy.
	 */
	i = rmalloc(kernelmap, 1);
	mm_map = (caddr_t)kmxtob(i);
	i = rmalloc(kernelmap, DUMPPAGES);
	cur_dump_addr = dump_addr = kmxtob(i);

	/*
	 * If the following is true, someone has patched
	 * phsymem to be less than the number of pages that
	 * the system actually has.  Remove pages until system
	 * memory is limited to the requested amount.  Since we
	 * have allocated page structures for all pages, we
	 * correct the amount of memory we want to remove
	 * by the size of the memory used to hold page structures
	 * for the non-used pages.
	 *
	 * Since we can have more than 4G of memory, you could reduce
	 * physmem by more than that and run out of space on
	 * the unused_pages_vp.  If the offset of the vnode wraps
	 * around we use a second vnode.  This is a hack and a general
	 * solution would probably be to allocate unused vnodes
	 * dynamically based on how much physmem was being reduced,
	 * but the gain (would anyone care?) hardly seems worth it.
	 */
	if (physmem < npages) {
		u_int diff, off;
		struct page *pp;
		struct vnode *vp = &unused_pages_vp;

		cmn_err(CE_WARN, "limiting physmem to %d pages", physmem);

		off = 0;
		diff = npages - physmem;
		diff -= mmu_btopr(diff * sizeof (struct page));
		while (diff--) {
			pp = page_create_va(vp, off,
				MMU_PAGESIZE, PG_WAIT | PG_EXCL,
				&kas, (caddr_t)off);
			if (pp == NULL)
				cmn_err(CE_PANIC, "limited physmem too much!");
			page_io_unlock(pp);
			page_downgrade(pp);
			availrmem--;
			off += MMU_PAGESIZE;
			if (off == 0)
				vp = &unused_pages_vp2;
		}
	}


	/*
	 * When printing memory, show the total as physmem less
	 * that stolen by a debugger.  printf currently can't
	 * print >4G, so do the trailing zero hack because we
	 * know the number is a multiple of 0x1000 (which is our
	 * pagesize).
	 */
	cmn_err(CE_CONT, "?mem = %dK (0x%x000)\n",
	    (physinstalled - dbug_mem) << (PAGESHIFT - 10),
	    physinstalled - dbug_mem);

	/*
	 * cmn_err doesn't do long long's and %u is treated
	 * just like %d, so we do this hack to get decimals
	 * > 2G printed.
	 */
	avmem = (u_longlong_t)freemem << PAGESHIFT;
	if (avmem >= 0x80000000ull)
		cmn_err(CE_CONT, "?avail mem = %d%d\n",
		    (u_int)(avmem / (1000 * 1000 * 1000)),
		    (u_int)(avmem % (1000 * 1000 * 1000)));
	else
		cmn_err(CE_CONT, "?avail mem = %d\n", (u_int)avmem);

	segkp_limit = SEGMAPBASE;

	/*
	 * Initialize the segkp segment type.  We position it
	 * after the configured tables and buffers (whose end
	 * is given by econtig) and before Sysbase.
	 */

	/* XXX - cache alignment? */
	va = econtig;
	max_virt_segkp = btop(segkp_limit - (u_int)econtig);
	max_phys_segkp = physmem * 2;
	segkp_len = ptob(min(max_virt_segkp, max_phys_segkp));
	nsegkp = segkp_len/3;

	rw_enter(&kas.a_lock, RW_WRITER);
	segkp = seg_alloc(&kas, va, segkp_len);
	if (segkp == NULL)
		cmn_err(CE_PANIC, "startup: cannot allocate segkp");
	if (segkp_create(segkp) != 0)
		cmn_err(CE_PANIC, "startup: segkp_create failed");
	rw_exit(&kas.a_lock);

	/*
	 * Now create generic mapping segment.  This mapping
	 * goes SEGMAPSIZE beyond Syslimit.  But if the total
	 * virtual address is greater than the amount of free
	 * memory that is available, then we trim back the
	 * segment size to that amount.
	 */
	va = (caddr_t)SEGMAPBASE;
	/*
	 * 1201049: segkmap base address must be MAXBSIZE aligned
	 */
	ASSERT(((u_int)va & MAXBOFFSET) == 0);

	i = SEGMAPSIZE;
	if (i > mmu_ptob(freemem))
		i = mmu_ptob(freemem);
	i &= MAXBMASK;	/* 1201049: segkmap size must be MAXBSIZE aligned */

	rw_enter(&kas.a_lock, RW_WRITER);
	segkmap = seg_alloc(&kas, va, i);
	if (segkmap == NULL)
		cmn_err(CE_PANIC, "cannot allocate segkmap");
	a.prot = PROT_READ | PROT_WRITE;

	a.vamask = (16 * MAXBSIZE) - 1;

	if (segmap_create(segkmap, (caddr_t)&a) != 0)
		panic("segmap_create segkmap");
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

	led_blink_all();	/* indicate the kernel controls machine */

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

	init_soft_stuffs();		/* bletch */

	/*
	 * setup system TOD and sync all slave TOD to system TOD clock.
	 */
	init_all_tods();

	led_blink_all();	/* indicate the kernel controls machine */
}

void
post_startup()
{
	/*
	 * Block Copy init
	 */
	hwbc_init();

	/*
	 * Initialize the level15 interrupt handler.
	 */
	level15_post_startup();

	/*
	 * load, start, & attach additional cpu's.
	 */
	ddi_install_driver("cpu");

	led_init();

	/*
	 * Reset the Cpudelay variable using the hardware timers
	 */
	resetcpudelay();

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

	/*
	 * xxx actually we are already at IPL 0, so this call
	 * is redundant.  We've been at IPL 0 since rootconf().
	 */
	(void) spl0();		/* allow interrupts */
}

/*
 * See if POST has recorded any problems with the system by reading
 * the front pannel yellow LED.
 *
 * also on sc2000, check that both busses are enabled.
 */
static void
check_prom_error(void)
{
	/*
	 * hardware register bit for System Yellow LED is active low,
	 * which means reverse logic.
	 */
	if (!(intr_bb_status1() & STATUS1_YL_LED)) {
		cmn_err(CE_WARN, "System booted with disabled components.");
	}

	if ((sun4d_model == MODEL_SC2000) && (n_xdbus == 1)) {
		int badbus;
		/*
		 * <= pilot PROM does not have disabled-xdbus property,
		 * so we'd print "XDbus -1" here.
		 */
		if (good_xdbus == 0)
			badbus = 1;
		else if (good_xdbus == 1)
			badbus = 0;

		cmn_err(CE_WARN,
			"System operating with XDbus %d disabled.", badbus);
	}

}

/*
 * These routines are called immediately before and
 * immediately after calling into the firmware.  The
 * firmware is significantly confused by preemption -
 * particularly on MP machines - but also on UP's too.
 *
 * We install the first set in mlsetup(), this set
 * is installed at the end of post_startup().
 */

static void
kern_preprom(void)
{
	curthread->t_preempt++;
}

static void
kern_postprom(void)
{
	curthread->t_preempt--;
}

/*
 * Add to a memory list.
 */
static void
physlist_add(
	u_longlong_t    start,
	u_longlong_t    len,
	struct memlist  **memlistp)
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
	struct memseg *msegend;
	extern void page_coloring_init(void);

	oldmemseglist = memsegp;
	memsegp_tmp = memsegp;

	for (index = 0, pmem = phys_avail; pmem; index += num_free_pages,
	    pmem = pmem->next) {

		first_page = (u_int)(pmem->address >> PAGESHIFT);
		num_free_pages = (u_int)(pmem->size >> PAGESHIFT);

		ASSERT(num_free_pages <= (npages - index));
/*
 * XXX
 * Delete the following after the hat_getpfnum error return changes from
 * 0 to -1.  As it is, physical page 0 is confused with the error return.
 */
		/*
		 * Don't put page 0 on the free page list.
		 */
		if (first_page == 0 && num_free_pages > 0) {
			first_page++;
			num_free_pages--;
		}
/* XXX */

		if (num_free_pages)
			page_init(&pp[index], num_free_pages, first_page,
			    memsegp_tmp++);
	}

	/*
	 * Initialize memory free list.
	 */
	page_coloring_init();

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
		if (first_page == 0 && dopages > 0) {
			first_page++;
			dopages--;
		}
/* XXX */
		if ((np + dopages) > npages)
			dopages = npages - np;
		np += dopages;
		if (dopages != 0)
			memialloc(first_page, first_page, first_page + dopages);
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
kvm_init()
{
	register caddr_t va;
	u_int range_size, range_base, range_end;
	u_int pfnum;
	struct memlist *cur, *prev;
	extern void _start();
	extern caddr_t e_text, e_data;
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
	 * Validate to Syslimit.  There may be several fragments of
	 * 'used' virtual memory in this range, so we hunt 'em all down.
	 */
	for (cur = virt_avail; cur->next; cur = cur->next) {
		if ((range_base = cur->address + cur->size) < (u_int)Sysbase)
			continue;
		if (range_base > (u_int)Syslimit)
			break;
		range_size = cur->next->address - range_base;
		(void) as_fault(kas.a_hat, &kas, (caddr_t)range_base,
			range_size, F_SOFTLOCK, S_OTHER);
		(void) as_setprot(&kas, (caddr_t)range_base, range_size,
			PROT_READ | PROT_WRITE | PROT_EXEC);
	}

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
	for (cur = virt_avail; cur && (u_int)cur->address < (u_int)Syslimit;
	    cur = cur->next) {
		range_base = MAX((u_int)cur->address, (u_int)Sysbase);
		range_end  = MIN((u_int)(cur->address + cur->size),
		    (u_int)Syslimit);
		if (range_end > range_base)
			as_setprot(&kas, (caddr_t)range_base,
			    range_end - range_base, 0);
	}
	rw_exit(&kas.a_lock);

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
	 * Allocate and lock level3 (and possibly level 2) page tables
	 * for the range of addresses used by the execargs and ppmap{in,out}.
	 */
	srmmu_reserve(&kas, (caddr_t)ARGSBASE, NCARGS, 0);

KVM_HERE

	/*
	 * Find the begining page frames of the kernel data
	 * segment and the ending page frame (-1) for bss.
	 */
	if ((pfnum = va_to_pfn((caddr_t)(roundup((u_int)e_text, DATA_ALIGN))))
	    != -1)
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

			/* No need to lock down ptbl */
			pte = srmmu_ptefind_nolock(&kas, (caddr_t)va, &level);

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
setup_kvpm()
{
	struct pte *pte, tpte;
	u_int va;
	int i = 0;
	int level = 0;
	u_int pages = 0;
	u_int nextpfnum, lastpfnum = -1;
	int off, span, npgs, pfn;

	for (va = KERNELBASE; va < (u_int)econtig; va += span) {

		if (level != 3 || (MMU_L2_OFF(va) < MMU_L3_SIZE)) {
			pte = srmmu_ptefind_nolock(&kas, (caddr_t)va, &level);
		} else {
			pte++;
		}

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
		}
		npgs = btop(span);
		mmu_readpte(pte, &tpte);
		if (pte_valid(&tpte)) {
			if (lastpfnum == -1) {
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
		} else if (lastpfnum != -1) {
			lastpfnum = -1;
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
		if (pfn != -1) {
			if (lastpfnum == -1) {
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
		} else if (lastpfnum != -1) {
			lastpfnum = -1;
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

	msgbuf.msg_map = va_to_pa((caddr_t)&kvtopdata);
}

/*
 * initialize kernel global CPUSET "bootbusses"
 * if a bit is on, it means that cpu_id has a bootbus.
 *
 * as implemented, can not be called before setup_ddi()
 */

static void
set_bootbusses(void)
{
	dev_info_t	*dip;

	dip = ddi_root_node();

	/*
	 * search 1st level children in devinfo tree for cpu-unit
	 */
	dip = ddi_get_child(dip);	/* 1st child of root */
	while (dip) {
		char	*name;

		name = ddi_get_name(dip);
		/*
		 * if cpu-unit
		 */
		if (strcmp("cpu-unit", name) == 0) {
			dev_info_t	*dip1;

			dip1 = ddi_get_child(dip);	/* child of cpu-unit */
			while (dip1) {
				char	*name1;

				name1 = ddi_get_name(dip1);

				if (strcmp("bootbus", name1) == 0) {
					int	devid;
					int	cpu_id;
				if (prom_getprop((dnode_t)ddi_get_nodeid(dip),
					    PROP_DEVICE_ID, (caddr_t)&devid)
					    == -1) {
						cmn_err(CE_WARN,
							"set_bootbusses: "
							" no %s for %s\n",
								PROP_DEVICE_ID,
								name);
						continue;
					}
					cpu_id = xdb_cpu_unit(devid);

					CPUSET_ADD(bootbusses, cpu_id);
					n_bootbus += 1;
				}
				dip1 = ddi_get_next_sibling(dip1);
			}
		}
		dip = ddi_get_next_sibling(dip);
	}

	/* found nothing */
	if (n_bootbus == 0) {
		cmn_err(CE_WARN, "no bootbusses found!");
	}
}

/*
 * set local MXCC.PF and MXCC.MC according to globals
 * use_mxcc_prefetch and use_multiple_cmds
 */

#ifdef DEBUG
u_int mxcc_knobs_bail = 0;
#endif DEBUG

void
mxcc_knobs(void)
{
	u_int setbits = 0;
	u_int clrbits = 0;
	u_int old, new;
	u_int mrev;

#ifdef DEBUG
	if (mxcc_knobs_bail)
		return;
#endif DEBUG

	/*
	 * bug in MXCC revisions < 3.0 cause parity error on ASI 2
	 * (local) access to Component ID register.  So we use ECSR access.
	 */
	mrev = (mxcc_cid_get_ecsr(CPU->cpu_id) & MXCC_CID_MREV_MASK) >>
		MXCC_CID_MREV_SHIFT;

	if (use_mxcc_prefetch)
		setbits |= MXCC_PF;
	else
		clrbits |= MXCC_PF;

	if (really_use_multiple_cmds) {
		setbits |= MXCC_MC;
		if (!MXCC_MC_OK(mrev))
			cmn_err(CE_WARN, "MXCC.MC enabled "
				"on MXCC mrev 0x%x (cpu %d)",
				mrev, CPU->cpu_id);
	} else
	if (use_multiple_cmds && MXCC_MC_OK(mrev))
		setbits |= MXCC_MC;
	else
		clrbits |= MXCC_MC;

	old = intr_mxcc_cntrl_get();
	new = old & ~clrbits;
	new |= setbits;
	if (new != old)
		intr_mxcc_cntrl_set(new);
}

u_int bw_lccnt = BW_LCCNT_WRITE_UPDATE;

/*
 * set Bus Watcher control registers in local CPU unit
 * according to the global value bw_lccnt.
 */

#ifdef DEBUG
u_int do_bw_knobs = 1;
#endif DEBUG

void
bw_knobs(void)
{
	u_int bus;
	extern u_int n_xdbus;
	u_int devid = CPUID_2_DEVID(CPU->cpu_id);

#ifdef DEBUG
	if (!do_bw_knobs)
		return;
#endif DEBUG

	for (bus = 0; bus < n_xdbus; bus++) {
		u_int addr;
		u_int old;
		u_int new;

		addr = CSR_ADDR_BASE + (devid << CSR_DEVID_SHIFT) +
			(bus << CSR_BUS_SHIFT);

		old = bw_cntl_get(addr);

		new = (old & ~(BW_CTL_LCCNT_MASK));

		new = new |
			((bw_lccnt << BW_CTL_LCCNT_SHIFT) & BW_CTL_LCCNT_MASK);

		if (new != old)
			bw_cntl_set(new, addr);

	}
}
