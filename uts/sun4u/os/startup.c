/*
 * Copyright (c) 1992-1994, by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)startup.c 1.93     95/10/15 SMI"

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
#include <sys/scb.h>
#include <sys/stack.h>
#include <sys/eeprom.h>
#include <sys/intreg.h>
#include <sys/trap.h>
#include <sys/x_call.h>
#include <sys/privregs.h>


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
#include <vm/hat_sfmmu.h>
#include <sys/iommu.h>
#include <sys/vtrace.h>
#include <sys/instance.h>
#include <sys/kobj.h>
#include <sys/async.h>
#include <sys/spitasi.h>

#include <sys/prom_debug.h>
#ifdef TRAPTRACE
#include <sys/traptrace.h>
#endif /* TRAPTRACE */

/*
 * External Data:
 */
extern lksblk_t *lksblks_head;
extern int vac_size;	/* cache size in bytes */
extern u_int vac_mask;	/* VAC alignment consistency mask */

int snooping = 0;
u_int snoop_interval = 50 * 1000000;
extern void deadman();
extern void tickint_init();
extern void tickint_clnt_add();

/*
 * Global Data Definitions:
 */

/*
 * Declare these as initialized data so we can patch them.
 */
int physmem = 0;	/* memory size in pages, patch if you want less */
int kernprot = 1;	/* write protect kernel text */

#ifdef DEBUG
int forthdebug	= 1;	/* Load the forthdebugger module */
#else
int forthdebug	= 0;	/* Don't load the forthdebugger module */
#endif DEBUG

#define	FDEBUGSIZE (50 * 1024)
#define	FDEBUGFILE "misc/forthdebug"

int use_cache = 1;		/* cache not reliable (605 bugs) with MP */
int vac_copyback = 1;
char	*cache_mode = (char *)0;
int use_mix = 1;
int prom_debug = 0;

struct bootops *bootops = 0;	/* passed in from boot in %o2 */
caddr_t boot_tba;
u_int	tba_taken_over = 0;

/*
 * DEBUGADDR is where we expect the deubbger to be if it's there.
 * We really should be allocating virtual addresses by looking
 * at the virt_avail list.
 */
#define	DEBUGADDR		((caddr_t)0xedd00000)

caddr_t s_text;			/* start of kernel text segment */
caddr_t e_text;			/* end of kernel text segment */
caddr_t s_data;			/* start of kernel data segment */
caddr_t e_data;			/* end of kernel data segment */

caddr_t		econtig;	/* end of first block of contiguous kernel */
caddr_t		ncbase;		/* beginning of non-cached segment */
caddr_t		ncend;		/* end of non-cached segment */
caddr_t		sdata;		/* beginning of data segment */
caddr_t		extra_etva;	/* beginning of end of text - va */
u_longlong_t	extra_etpa;	/* beginning of end of text - pa */
u_int		extra_et;	/* bytes from end of text to 4MB boundary */

u_int	ndata_remain_sz;	/* bytes from end of data to 4MB boundary */
caddr_t	nalloc_base;		/* beginning of nucleus allocation */
caddr_t nalloc_end;		/* end of nucleus allocatable memory */
caddr_t valloc_base;		/* beginning of kvalloc segment	*/

u_int shm_alignment = 0;	/* VAC address consistency modulus */
struct memlist *phys_install;	/* Total installed physical memory */
struct memlist *phys_avail;	/* Available (unreserved) physical memory */
struct memlist *virt_avail;	/* Available (unmapped?) virtual memory */
int memexp_flag;		/* memory expansion card flag */

/*
 * VM data structures
 */
int page_hashsz;		/* Size of page hash table (power of two) */
struct page *pp_base;		/* Base of system page struct array */
u_int pp_sz;			/* Size in bytes of page struct array */
struct	page **page_hash;	/* Page hash table */
struct	seg *segkmap;		/* Kernel generic mapping segment */
struct	seg ktextseg;		/* Segment used for kernel executable image */
struct	seg kvalloc;		/* Segment used for "valloc" mapping */
struct	seg *segkp;		/* Segment for pageable kernel virt. memory */
struct	seg *seg_debug;		/* Segment for debugger */
struct	memseg *memseg_base;
u_int	memseg_sz;		/* Used to translate a va to page */
struct	vnode unused_pages_vp;

/*
 * VM data structures allocated early during boot.
 */
/*
 *	Fix for bug 1119063.
 */
#define	KERNELMAP_SZ(frag)	\
	max(MMU_PAGESIZE,	\
	roundup((sizeof (struct map) * (physmem)/2/(frag)), MMU_PAGESIZE))

u_int pagehash_sz;
u_int memlist_sz;
u_int kernelmap_sz;
/*
 * startup_alloc_vaddr is initialized to 1 page + SYSBASE instead of being
 * exactly SYSBASE because rmap uses 0 as a delimeter so we can't have
 * offset 0 (ie. page 0) as part of the resource map. Page 0 is simply never
 * used. The fact that startup_alloc_vaddr starts at page 1 of the resource
 * map is also hardwired in the call to rmget.
 */
caddr_t startup_alloc_vaddr = (caddr_t)SYSBASE + MMU_PAGESIZE;
caddr_t startup_alloc_size;
u_int	startup_alloc_chunk_size = 20 * MMU_PAGESIZE;
u_int pmeminstall;		/* total physical memory installed */

char tbr_wr_addr_inited = 0;

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
static void memlist_add(u_longlong_t, u_longlong_t, struct memlist **,
	struct memlist **);
static void kphysm_init(page_t *, struct memseg *, u_int);
static void kvm_init(void);
static void setup_kvpm(void);
static void check_obp_version(void);

struct cpu *prom_cpu;
kmutex_t prom_mutex;
kcondvar_t prom_cv;
static void kern_preprom(void);
static void kern_postprom(void);

static void startup_init(void);
static void startup_memlist(void);
static void startup_modules(void);
static void startup_bop_gone(void);
static void startup_vm(void);
static void startup_end(void);
static void setup_trap_table(void);
static caddr_t iommu_tsb_alloc(caddr_t);

static u_int npages;
static int dbug_mem;
static int debug_start_va;
static struct memlist *memlist;

int sbus_iommu_tsb_alloc_size = IOMMU_TSB_TBL_SIZE;
int pci_iommu_tsb_alloc_size = IOMMU_TSB_TBL_SIZE;

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
 * which are 8k on sun4u.
 *
 *		  Physical memory layout
 *		(not necessarily contiguous)
 *		(THIS IS SOMEWHAT WRONG)
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
 *	page 1	|	 msgbuf		|
 *		|-----------------------|
 *	page 0	|	reclaimed	|
 *		|_______________________|
 *
 *
 *
 *	      Kernels Virtual Memory Layout.
 *		/-----------------------\
 *		|			|
 *		|	OBP/kadb/...	|
 *		|			|
 * 0xF0000000  -|-----------------------|- SYSEND
 *		|			|
 *		|			|
 *		|  segkmem segment	|	(SYSEND - SYSBASE = 2.5G)
 *		|			|
 *		|			|
 * 0x50000000  -|-----------------------|- SYSBASE
 *		|			|
 *		|  segmap segment	|	SEGMAPSIZE	(256M)
 *		|			|
 * 0x40000000  -|-----------------------|- SEGMAPBASE
 *		|			|
 *		|	segkp		|	SEGKPSIZE	(256M)
 *		|			|
 * 0x30000000  -|-----------------------|- SEGKPBASE
 *		|			|
 *             -|-----------------------|- ARGSBASE (SEGKPBASE - NCARGS)
 *		|			|
 *             -|-----------------------|- PPMAPBASE (ARGSBASE - PPMAPSIZE)
 *		:			:
 *		:			:
 *		|			|
 *		|-----------------------|- econtig
 *		|    vm structures	|
 * 0x10800000	|-----------------------|- nalloc_end
 *		|	  tsb		|
 *		|-----------------------|
 *		|    hmeblk pool	|
 *		|-----------------------|
 *		|    hmeblk hashtable	|
 *		|-----------------------|- end/nalloc_base
 *		|  kernel data & bss	|
 * 0x10400000	|-----------------------|
 *		|			|
 *		|-----------------------|- etext
 *		|	kernel text	|
 *		|-----------------------|
 *		|   trap table (48k)	|
 * 0x10000000  -|-----------------------|- KERNELBASE
 *		|			|
 *		|	invalid		|
 *		|			|
 * 0x00000000  _|_______________________|
 *
 *
 *
 *
 *	       Users Virtual Memory Layout.
 *		/-----------------------\
 *		|			|
 *		|        invalid 	|
 *		|			|
 * 0xF0000000  -|-----------------------|- USERLIMIT
 *		|	user stack	|
 *		:			:
 *		:			:
 *		:			:
 *		|	user data	|
 *	       -|-----------------------|-
 *		|	user text	|
 * 0x00002000  -|-----------------------|-
 *		|	invalid		|
 * 0x00000000  _|_______________________|
 *
 */

/*
 * Machine-dependent startup code
 */
void
startup(void)
{
	startup_init();
	startup_memlist();
	startup_modules();
	startup_bop_gone();
	startup_vm();
	startup_end();
}

static void
startup_init(void)
{
	extern void dki_lock_setup(lksblk_t *);
	extern void ppmapinit(void);
	extern void kncinit(void);
	extern void init_vx_handler(void);
	extern int callback_handler(cell_t *arg_array);
#ifdef TRAPTRACE
	TRAP_TRACE_CTL	*ctlp;
#endif /* TRAPTRACE */

	check_obp_version();
	(void) check_boot_version(BOP_GETVERSION(bootops));

	dki_lock_setup(lksblks_head);

	/*
	 * Initialize the turnstile allocation mechanism.
	 */
	tstile_init();

	kncinit();

	/*
	 * Initialize the address map for cache consistent mappings
	 * to random pages, must done after vac_size is set.
	 */
	ppmapinit();

	/*
	 * Initialize the PROM callback handler and install the PROM
	 * callback handler. For this 32 bit client program, we install
	 * "callback_handler" which is the glue that binds the 64 bit
	 * prom callback handler to the 32 bit client program callback
	 * handler: vx_handler.
	 */
	init_vx_handler();
	(void) prom_set_callback((void *)callback_handler);

#ifdef TRAPTRACE
	/*
	 * initialize the trap trace buffer for this cpu
	 * XXX todo, dynamically allocate this buffer too
	 */
	ctlp = &trap_trace_ctl[CPU->cpu_id];
	ctlp->d.vaddr_base = trap_tr0;
	ctlp->d.offset = ctlp->d.last_offset = 0;
	ctlp->d.limit = TRAP_TSIZE;		/* XXX dynamic someday */
	ctlp->d.paddr_base = va_to_pa(trap_tr0);

#endif /* TRAPTRACE */
}

static void
startup_memlist(void)
{
	u_int real_sz;
	caddr_t real_base;
	caddr_t alloc_base;
	int memblocks = 0;
	caddr_t memspace;
	u_int memspace_sz;
	struct memlist *cur;
	u_int syslimit = (u_int)Syslimit;	/* See: 1124059 */
	u_int sysbase = (u_int)Sysbase;		/* See: 1124059 */
	caddr_t bop_alloc_base;
	u_int kmapsz;

	extern int ecache_linesize;
	extern void mt_lock_init(void);
	extern caddr_t ndata_alloc_cpus();
	extern caddr_t ndata_alloc_hat();
	extern caddr_t e_text, e_data;

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
	 * We're loaded by boot with the following configuration (as
	 * specified in the sun4u/conf/Mapfile):
	 * 	text:		4 MB chunk aligned on a 4MB boundary
	 *	data & bss:	4 MB chunk aligned on a 4MB boundary
	 * These two chunks will eventually be mapped by 2 locked 4MB ttes
	 * and will represent the nucleus of the kernel.
	 * This gives us some free space that is already allocated.
	 * The free space in the text chunk is currently being returned
	 * to the physavail list. Eventually it would be nice to use this
	 * space for other kernel text and thus take more advantage of the
	 * kernel 4MB tte.
	 * The free space in the data-bss chunk is used for nucleus allocatable
	 * data structures and we reserve it using the nalloc_base and
	 * nalloc_end variables.  This space is currently being used for
	 * hat data structures required for tlb miss handling operations.
	 * We align nalloc_base to a l2 cache linesize because this is the
	 * line size the hardware uses to maintain cache coherency
	 */

	nalloc_base = (caddr_t)roundup((u_int)e_data, MMU_PAGESIZE);
	nalloc_end = (caddr_t)roundup((u_int)nalloc_base, MMU_PAGESIZE4M);
	valloc_base = nalloc_base;

	/*
	 * Calculate the start of the data segment.
	 */
	sdata = (caddr_t)((u_int)e_data & MMU_PAGEMASK4M);

	PRM_DEBUG(nalloc_base);
	PRM_DEBUG(nalloc_end);
	PRM_DEBUG(sdata);

	/*
	 * Remember any slop after e_text so we can add it to the
	 * physavail list.
	 */
	PRM_DEBUG(e_text);
	extra_etva = (caddr_t)roundup((u_int)e_text, MMU_PAGESIZE);
	PRM_DEBUG((u_int)extra_etva);
	extra_etpa = va_to_pa(extra_etva);
	PRM_DEBUG((u_int)extra_etpa);
	if (extra_etpa != (u_int)-1) {
		extra_et = roundup((u_int)e_text, MMU_PAGESIZE4M) -
			(u_int)extra_etva;
	} else {
		extra_et = 0;
	}
	PRM_DEBUG((u_int)extra_et);

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
	PRM_DEBUG(pmeminstall);
	PRM_DEBUG(physmax);

	/*
	 * Get the list of physically available memory to size
	 * the number of page structures needed.
	 */
	size_physavail(bootops->boot_mem->physavail, &npages, &memblocks);

	/* Account for any pages after e_text and e_data */
	npages += mmu_btop(extra_et);
	npages += mmu_btopr(nalloc_end - nalloc_base);
	PRM_DEBUG(npages);

	/*
	 * npages is the maximum of available physical memory possible.
	 * (ie. it will never be more than this)
	 */

	/*
	 * Allocate cpus structs from the nucleus data area and
	 * update nalloc_base.
	 */
	nalloc_base = (caddr_t)roundup((u_int)ndata_alloc_cpus(nalloc_base),
		ecache_linesize);

	if (nalloc_base > nalloc_end) {
		cmn_err(CE_PANIC, "no more nucleus memory after cpu alloc");
	}
	/*
	 * Allocate hat related structs from the nucleus data area and
	 * update nalloc_base.
	 */
	nalloc_base = ndata_alloc_hat(nalloc_base, nalloc_end, npages);
	nalloc_base = (caddr_t)roundup((u_int)nalloc_base, ecache_linesize);
	if (nalloc_base > nalloc_end) {
		cmn_err(CE_PANIC, "no more nucleus memory after hat alloc");
	}

	/*
	 * Given our current estimate of npages we do a premature calculation
	 * on how much memory we are going to need to support this number of
	 * pages.  This allows us to calculate a good start virtual address
	 * for other BOP_ALLOC operations.
	 * We want to do the BOP_ALLOCs before the real allocation of page
	 * structs in order to not have to allocate page structs for this
	 * memory.  We need to calculate a virtual address because we want
	 * the page structs to come before other allocations in virtual address
	 * space.  This is so some (if not all) of page structs can actually
	 * live in the nucleus.
	 */
	/*
	 * The page structure hash table size is a power of 2
	 * such that the average hash chain length is PAGE_HASHAVELEN.
	 */
	page_hashsz = npages / PAGE_HASHAVELEN;
	page_hashsz = 1 << highbit((u_long)page_hashsz);
	pagehash_sz = sizeof (struct page *) * page_hashsz;

	/*
	 * The memseg list is for the chunks of physical memory that
	 * will be managed by the vm system.  The number calculated is
	 * a guess as boot may fragment it more when memory allocations
	 * are made before kphysm_init().  Currently, there are two
	 * allocations before then, so we assume each causes fragmen-
	 * tation, and add a couple more for good measure.
	 */
	memseg_sz = sizeof (struct memseg) * (memblocks + 4);
	pp_sz = sizeof (struct page) * npages;
	real_sz = pagehash_sz + memseg_sz + pp_sz;
	PRM_DEBUG(real_sz);

	bop_alloc_base = (caddr_t)roundup((uint)(nalloc_end + real_sz),
		MMU_PAGESIZE);
	PRM_DEBUG(bop_alloc_base);

	/*
	 * Add other BOP_ALLOC operations here
	 */
	alloc_base = bop_alloc_base;
	/*
	 * Allocate IOMMU TSB array.  We do this here so that the physical
	 * memory gets deducted from the PROM's physical memory list.
	 */
	alloc_base = (caddr_t)roundup((u_int)iommu_tsb_alloc(alloc_base),
		ecache_linesize);
	PRM_DEBUG(alloc_base);

	/*
	 * The only left to allocate for the kvalloc segment should be the
	 * vm data structures.
	 */
	/*
	 * take the most current snapshot we can by calling mem-update
	 */
	if (BOP_GETPROPLEN(bootops, "memory-update") == 0)
		BOP_GETPROP(bootops, "memory-update", NULL);
	npages = 0;
	memblocks = 0;
	size_physavail(bootops->boot_mem->physavail, &npages, &memblocks);
	PRM_DEBUG(npages);
	/* account for memory after etext */
	npages += mmu_btop(extra_et);

	/*
	 * Calculate the remaining memory in nucleus data area.
	 * We need to figure out if page structs can fit in there or not.
	 * We also make sure enough page structs get created for any physical
	 * memory we might be returning to the system.
	 */
	ndata_remain_sz = (u_int) (nalloc_end - nalloc_base);
	PRM_DEBUG(ndata_remain_sz);
	pp_sz = sizeof (struct page) * npages;
	if (ndata_remain_sz > pp_sz) {
		npages += mmu_btop(ndata_remain_sz - pp_sz);
	}
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
	page_hashsz = 1 << highbit((u_long)page_hashsz);
	pagehash_sz = sizeof (struct page *) * page_hashsz;

	/*
	 * The memseg list is for the chunks of physical memory that
	 * will be managed by the vm system.  The number calculated is
	 * a guess as boot may fragment it more when memory allocations
	 * are made before kphysm_init().  Currently, there are two
	 * allocations before then, so we assume each causes fragmen-
	 * tation, and add a couple more for good measure.
	 */
	memseg_sz = sizeof (struct memseg) * (memblocks + 4);
	pp_sz = sizeof (struct page) * npages;
	real_sz = pagehash_sz + memseg_sz;
	real_sz = roundup(real_sz, ecache_linesize) + pp_sz;
	PRM_DEBUG(real_sz);

	/*
	 * Allocate the page structures from the remaining memory in the
	 * nucleus data area.
	 */
	real_base = nalloc_base;

	if (ndata_remain_sz >= real_sz) {
		/*
		 * Figure out the base and size of the remaining memory.
		 */
		nalloc_base += real_sz;
		ASSERT(nalloc_base <= nalloc_end);
		ndata_remain_sz = nalloc_end - nalloc_base;
	} else if (ndata_remain_sz < real_sz) {
		/*
		 * The page structs need extra memory allocated through
		 * BOP_ALLOC.
		 */
		real_sz = roundup((real_sz - ndata_remain_sz),
			MMU_PAGESIZE);
		memspace = (caddr_t)BOP_ALLOC(bootops, nalloc_end, real_sz,
			MMU_PAGESIZE);
		if (memspace != nalloc_end)
			panic("system page struct alloc failure");

		nalloc_base = nalloc_end;
		ndata_remain_sz = 0;
		if ((nalloc_end + real_sz) > bop_alloc_base) {
			prom_panic("vm structures overwrote other bop alloc!");
		}
	}
	PRM_DEBUG(nalloc_base);
	PRM_DEBUG(ndata_remain_sz);
	PRM_DEBUG(real_base + real_sz);
	nalloc_base = (caddr_t)roundup((uint)nalloc_base, MMU_PAGESIZE);
	ndata_remain_sz = nalloc_end - nalloc_base;

	page_hash = (struct page **)real_base;
	memseg_base = (struct memseg *)((u_int)page_hash + pagehash_sz);
	pp_base = (struct page *)roundup((u_int)memseg_base + memseg_sz,
		ecache_linesize);
	PRM_DEBUG(page_hash);
	PRM_DEBUG(memseg_base);
	PRM_DEBUG(pp_base);
	econtig = alloc_base;
	PRM_DEBUG(econtig);

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
	memspace_sz = memlist_sz + kernelmap_sz;
	memspace = (caddr_t)BOP_ALLOC(bootops, startup_alloc_vaddr,
	    memspace_sz, BO_NO_ALIGN);
	startup_alloc_vaddr += memspace_sz;
	startup_alloc_size += memspace_sz;
	if (memspace == NULL)
		halt("Boot allocation failed.");

	memlist = (struct memlist *)memspace;
	kernelmap = (struct map *)((u_int)memlist + memlist_sz);

	kmapsz = (u_int)(SYSEND - SYSBASE);
	kmapsz >>= MMU_PAGESHIFT;
	mapinit(kernelmap, (long)(kmapsz - 1), (u_long)1,
		"kernel map", kernelmap_sz / sizeof (struct map));
	/*
	 * take the most current snapshot we can by calling mem-update
	 */
	if (BOP_GETPROPLEN(bootops, "memory-update") == 0)
		BOP_GETPROP(bootops, "memory-update", NULL);

	/*
	 * Remove the space used by BOP_ALLOC from the kernelmap
	 * plus the area actually used by the OBP (if any)
	 * ignoring virtual addresses in virt_avail, above Syslimit.
	 *
	 * Note that we handle sysbase/syslimit as u_int via a temporary
	 * variable to workaround compiler bug 1124059. sysbase and syslimt
	 * have the same address value as Sysbase, Syslimit (respectively).
	 */

	virt_avail = memlist;
	copy_memlist(bootops->boot_mem->virtavail, &memlist);

	mutex_enter(&maplock(kernelmap));
	for (cur = virt_avail; cur->next; cur = cur->next) {
		u_longlong_t range_base, range_size;

		if ((range_base = cur->address + cur->size) <
		    (u_longlong_t)sysbase)
			continue;
		if (range_base >= (u_longlong_t)syslimit)
			break;
		/*
		 * Limit the range to end at Syslimit.
		 */
		range_size = MIN(cur->next->address,
		    (u_longlong_t)syslimit) - range_base;
		if (rmget(kernelmap, btop(range_size),
		    btop((u_int)range_base - sysbase)) == 0)
			prom_panic("can't remove OBP hole");
	}
	mutex_exit(&maplock(kernelmap));

	phys_avail = memlist;
	(void) copy_physavail(bootops->boot_mem->physavail, &memlist, 0, 0);

	/*
	 * Add any extra mem after e_text to physavail list.
	 */
	if (extra_et) {
		memlist_add(extra_etpa, (u_longlong_t)extra_et, &memlist,
			&phys_avail);
	}
	/*
	 * Add any extra nucleus mem to physavail list.
	 */
	if (ndata_remain_sz) {
		ASSERT(nalloc_end == (nalloc_base + ndata_remain_sz));
		memlist_add(va_to_pa(nalloc_base),
			(u_longlong_t)ndata_remain_sz, &memlist, &phys_avail);
	}

	/*
	 * Initialize the page structures from the memory lists.
	 */
	kphysm_init(pp_base, memseg_base, npages);

	availsmem = freemem;
	PRM_DEBUG(availsmem);
	availrmem = freemem;
	PRM_DEBUG(availrmem);

	/*
	 * Some of the locks depend on page_hashsz being set!
	 * kmem_init() depends on this; so, keep it here.
	 */
	mt_lock_init();

	/*
	 * Initialize kernel memory allocator.
	 */
	kmem_init();

	/*
	 * Initialize the kstat framework.
	 */
	kstat_init();
}

static void
startup_modules(void)
{
	extern void param_calc(int);
	extern void param_init(void);
	extern void create_va_to_tte(void);

	extern int maxusers;

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

	/*
	 * Initialize system parameters
	 */
	param_init();

	/*
	 * If debugger is in memory, note the pages it stole from physmem.
	 * XXX: Should this happen with V2 Proms?  I guess it would be
	 * set to zero in this case?
	 */
	if (boothowto & RB_DEBUG)
		dbug_mem = *dvec->dv_pages;
	else
		dbug_mem = 0;

	/*
	 * maxmem is the amount of physical memory we're playing with.
	 */
	maxmem = physmem;

	/* Set segkp limits. */
	ncbase = DEBUGADDR;
	ncend = DEBUGADDR;

	/*
	 * Initialize the hat layer.
	 */
	hat_init();

	/*
	 * Initialize segment management stuff.
	 */
	seg_init();

	/*
	 * Create the va>tte handler, so the prom can understand
	 * kernel translations.  The handler is installed later, just
	 * as we are about to take over the trap table from the prom.
	 */
	create_va_to_tte();

	/*
	 * If obpdebug or forthdebug is set, load the obpsym kernel
	 * symbol support module, now.
	 */
	if ((obpdebug) || (forthdebug)) {
		obpdebug = 1;
		(void) modload("misc", "obpsym");
	}

	/*
	 * Load the forthdebugger if forthdebug is set.
	 */
	if (forthdebug) {
		extern void forthdebug_init(void);
		forthdebug_init();
	}

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
}

static void
startup_bop_gone(void)
{
	struct memlist *cur, *new_memlist;

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
	 *
	 * XXX: mlsetup now deducts the msgbuf phys and virt addresses from
	 * both available lists, do we still need to copy this memory list?
	 */
	cur = new_memlist;
	(void) copy_physavail(bootops->boot_mem->physavail, &new_memlist, 0, 0);

	/*
	 * Make sure we add any memory we added back to the old list.
	 */
	if (extra_et) {
		memlist_add(extra_etpa, (u_longlong_t)extra_et, &new_memlist,
			&cur);
	}
	if (ndata_remain_sz) {
		memlist_add(va_to_pa(nalloc_base),
			(u_longlong_t)ndata_remain_sz, &new_memlist, &cur);

	}

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
}

static void
startup_vm(void)
{
	register unsigned i;
	struct segmap_crargs a;
	u_int avmem;
	caddr_t va;
	int	max_virt_segkp;
	int	max_phys_segkp;
	extern caddr_t mm_map, cur_dump_addr, dump_addr;
	extern void hat_kern_setup(void);
	extern void install_va_to_tte(void);
	extern kmutex_t atomic_nc_mutex;

	/*
	 * get prom's mappings, create hments for them and switch
	 * to the kernel context.
	 */
	hat_kern_setup();

	/*
	 * Take over trap table
	 */
	mutex_init(&atomic_nc_mutex, "non-$ atomic lock", MUTEX_DEFAULT, NULL);
	setup_trap_table();

	/*
	 * Install the va>tte handler, so that the prom can handle
	 * misses and understand the kernel table layout in case
	 * we need call into the prom.
	 */
	install_va_to_tte();

	/*
	 * Set a flag to indicate that the tba has been taken over.
	 */
	tba_taken_over = 1;

	/*
	 * Need to remove prom's locked translations.
	 */
	obp_remove_locked_ttes();

	/*
	 * Set a flag to tell write_scb_int() that it can access V_TBR_WR_ADDR.
	 */
	tbr_wr_addr_inited = 1;

	/*
	 * Initialize VM system, and map kernel address space.
	 */
	kvm_init();

	/*
	 * Setup a map for translating kernel virtual addresses;
	 * used by dump and libkvm.
	 */
	setup_kvpm();

	/*
	 * XXX4U: previously, we initialized and turned on
	 * the caches at this point. But of course we have
	 * nothing to do, as the prom has already done this
	 * for us -- main memory must be E$able at all times.
	 */

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
	 * When printing memory, show the total as physmem less
	 * that stolen by a debugger.
	 */
	cmn_err(CE_CONT, "?mem = %dK (0x%x)\n",
	    (physinstalled - dbug_mem) << (PAGESHIFT - 10),
	    ptob(physinstalled - dbug_mem));

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
	 * Initialize the segkp segment type.  We position it
	 * after the configured tables and buffers (whose end
	 * is given by econtig) and before V_WKBASE_ADDR.
	 * Also in this area are the debugger (if present)
	 * and segkmap (size SEGMAPSIZE).
	 */

	/* XXX - cache alignment? */
	va = (caddr_t)SEGKPBASE;
	ASSERT(((u_int)va & PAGEOFFSET) == 0);

	max_virt_segkp = btop(SEGKPSIZE);
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
	 */
	i -= (u_int)va & MAXBOFFSET;

	rw_enter(&kas.a_lock, RW_WRITER);
	segkp = seg_alloc(&kas, va, i);
	if (segkp == NULL)
		cmn_err(CE_PANIC, "startup: cannot allocate segkp");
	if (segkp_create(segkp) != 0)
		cmn_err(CE_PANIC, "startup: segkp_create failed");
	rw_exit(&kas.a_lock);

	/*
	 * Now create generic mapping segment.  This mapping
	 * goes SEGMAPSIZE beyond SEGMAPBASE.  But if the total
	 * virtual address is greater than the amount of free
	 * memory that is available, then we trim back the
	 * segment size to that amount
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

	a.vamask = vac_size - 1;

	if (segmap_create(segkmap, (caddr_t)&a) != 0)
		panic("segmap_create segkmap");
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
}

static void
startup_end()
{
#ifdef FIXME
	register unsigned i;
#endif FIXME

	/*
	 * Initialize interrupt related stuff
	 */
	init_intr_threads(CPU);
	tickint_init();			/* Tick_Compare register interrupts */

	(void) splzs();			/* allow hi clock ints but not zs */

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
	 * Initialize errors.
	 */
	error_init();

	/*
	 * Install the "real" pre-emption guards before DDI services
	 * are available.
	 */
	mutex_init(&prom_mutex, "prom mutex", MUTEX_DEFAULT, NULL);
	cv_init(&prom_cv, "prom cv", CV_DEFAULT, NULL);
	(void) prom_set_preprom(kern_preprom);
	(void) prom_set_postprom(kern_postprom);
	CPU->cpu_m.mutex_ready = 1;
	start_mon_clock();

	/*
	 * Configure the root devinfo node.
	 */
	configure();		/* set up devices */

}

void
setup_trap_table()
{
	extern struct scb trap_table;
	extern setwstate();

	intr_init(CPU);			/* init interrupt request free list */
	setwstate(WSTATE_KERN);
	prom_set_traptable((void *)&trap_table);
}

void
post_startup(void)
{
	extern int has_central;
	extern void load_central_modules();

	/*
	 * Configure the rest of the system.
	 * Perform forceloading tasks for /etc/system.
	 */
	(void) mod_sysctl(SYS_FORCELOAD, NULL);
	/*
	 * ON4.0: Force /proc module in until clock interrupt handle fixed
	 * ON4.0: This must be fixed or restated in /etc/systems.
	 */
	(void) modload("fs", "procfs");

	/* load all modules needed by central if it exists */
	if (has_central) {
		load_central_modules();
	}

	maxmem = freemem;

	(void) spl0();		/* allow interrupts */
	if (snooping)
		tickint_clnt_add(deadman, snoop_interval);

}

/*
 * These routines are called immediately before and
 * immediately after calling into the firmware.  The
 * firmware is significantly confused by preemption -
 * particularly on MP machines - but also on UP's too.
 *
 * Cheese alert.
 *
 * We have to handle the fact that when slave cpus start, they
 * aren't yet read for mutex's (i.e. they are still running on
 * the prom's tlb handlers, so they will fault if they touch
 * curthread).
 *
 * To handle this, the cas on prom_cpu is the actual lock, the
 * mutex is so "adult" cpus can cv_wait/cv_signal themselves.
 * This routine degenerates to a spin lock anytime a "juvenile"
 * cpu has the lock.
 */
static void
kern_preprom(void)
{
	struct cpu *cp, *prcp;
	extern int cas(int *, int, int);
	extern void membar_consumer();
#ifdef DEBUG
	extern greg_t getpil();
#endif /* DEBUG */

	for (;;) {
		cp = cpu[getprocessorid()];
		if (cp->cpu_m.mutex_ready) {
			/*
			 * Disable premption, and re-validate cp.  We can't
			 * move from a mutex_ready cpu to a non mutex_ready
			 * cpu, so just getting the current cpu is ok.
			 *
			 * Try the lock.  If we dont't get the lock,
			 * re-enable preemption and see if we should
			 * sleep.
			 */
			kpreempt_disable();
			cp = CPU;
			if (cas((int *)&prom_cpu, 0, (u_int)cp) == 0)
				break;
			kpreempt_enable();
			/*
			 * We have to be very careful here since both
			 * prom_cpu and prcp->cpu_m.mutex_ready can
			 * be changed at any time by a non mutex_ready
			 * cpu.
			 *
			 * If prom_cpu is mutex_ready, prom_mutex
			 * protects prom_cpu being cleared on us.
			 * If prom_cpu isn't mutex_ready, we only know
			 * it will change prom_cpu before changing
			 * cpu_m.mutex_ready, so we invert the check
			 * order with a membar in between to make sure
			 * the lock holder really will wake us.
			 */
			mutex_enter(&prom_mutex);
			prcp = prom_cpu;
			if (prcp != NULL && prcp->cpu_m.mutex_ready != 0) {
				membar_consumer();
				if (prcp == prom_cpu)
					cv_wait(&prom_cv, &prom_mutex);
			}
			mutex_exit(&prom_mutex);
			/*
			 * Check for panic'ing.
			 */
			if (panicstr) {
				panic_hook();
				return;
			}
		} else {
			/*
			 * Non mutex_ready cpus just grab the lock
			 * and run with it.
			 */
			ASSERT(getpil() == PIL_MAX);
			if (cas((int *)&prom_cpu, 0, (u_int)cp) == 0)
				break;
		}
	}
}

static void
kern_postprom(void)
{
	struct cpu *cp;
	extern void membar_producer();

	cp = cpu[getprocessorid()];
	ASSERT(prom_cpu == cp || panicstr);
	if (cp->cpu_m.mutex_ready) {
		kpreempt_enable();
		mutex_enter(&prom_mutex);
		prom_cpu = NULL;
		cv_signal(&prom_cv);
		mutex_exit(&prom_mutex);
	} else {
		prom_cpu = NULL;
		membar_producer();
	}
}

/*
 * Add to a memory list.
 * start = start of new memory segment
 * len = length of new memory segment in bytes
 * memlistp = pointer to array of available memory segment structures
 * curmemlistp = memory list to which to add segment.
 */
static void
memlist_add(u_longlong_t start, u_longlong_t len, struct memlist **memlistp,
	struct memlist **curmemlistp)
{
	struct memlist *cur, *new, *last;
	u_longlong_t end = start + len;

	new = *memlistp;
	new->address = start;
	new->size = len;
	*memlistp = new + 1;
	for (cur = *curmemlistp; cur; cur = cur->next) {
		last = cur;
		if (cur->address >= end) {
			new->next = cur;
			new->prev = cur->prev;
			cur->prev = new;
			if (cur == *curmemlistp)
				*curmemlistp = new;
			else
				new->prev->next = new;
			return;
		}
		if (cur->address + cur->size > start)
			panic("munged memory list = 0x%x\n", curmemlistp);
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
kphysm_init(page_t *pp, struct memseg *memsegp, u_int npages)
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

	oldmemseglist = memsegp;
	memsegp_tmp = memsegp;

	for (index = 0, pmem = phys_avail; pmem; index += num_free_pages,
	    pmem = pmem->next) {

		first_page = mmu_btop(pmem->address);
		num_free_pages = mmu_btop(pmem->size);

		if (num_free_pages > npages - index)
			num_free_pages = npages - index;
		if (num_free_pages)
			page_init(&pp[index], num_free_pages, first_page,
			    memsegp_tmp++);
	}

	/*
	 * Initialize memory free list.
	 */
	page_coloring_init();

	np = 0;
	for (memsegp_tmp = memsegp; memsegp_tmp;
	    memsegp_tmp = memsegp_tmp->next) {
		first_page = memsegp_tmp->pages_base;
		dopages = memsegp_tmp->pages_end - first_page;
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
	u_int pfnum;
	struct memlist *cur;
	u_int syslimit = (u_int)Syslimit;	/* See: 1124059 */
	u_int sysbase = (u_int)Sysbase;		/* See: 1124059 */
	extern caddr_t e_text;
	extern caddr_t e_data;
	extern int segkmem_ready;

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
	(void) segkmem_create(&kvseg, (caddr_t)NULL);

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
	 *
	 * Note that we handle sysbase/syslimit as u_ints via a temporary
	 * variable to workaround compiler bug 1124059. sysbase and syslimt
	 * have the same address value as Sysbase, Syslimit (respectively).
	 */
	for (cur = virt_avail; cur->next; cur = cur->next) {
		u_longlong_t range_base, range_size;

		if ((range_base = cur->address + cur->size) <
		    (u_longlong_t)sysbase)
			continue;
		if (range_base >= (u_longlong_t)syslimit)
			break;
		/*
		 * Limit the range to end at Syslimit.
		 */
		range_size = MIN(cur->next->address, (u_longlong_t)syslimit) -
		    range_base;
		(void) as_setprot(&kas, (caddr_t)range_base, (u_int)range_size,
		    PROT_READ | PROT_WRITE | PROT_EXEC);
	}

	/*
	 * Invalidate unused portion of the region managed by kernelmap.
	 * (We know that the PROM never allocates any mappings here by
	 * itself without updating the 'virt-avail' list, so that we can
	 * simply render anything that is on the 'virt-avail' list invalid)
	 * (Making sure to ignore virtual addresses above 2**32.)
	 *
	 * Note that we handle sysbase/syslimit as u_int via a temporary
	 * variable to workaround compiler bug 1124059. sysbase and syslimt
	 * have the same address value as Sysbase, Syslimit (respectively).
	 */
	for (cur = virt_avail; cur && cur->address < (u_longlong_t)syslimit;
	    cur = cur->next) {
		u_longlong_t range_base, range_end;

		range_base = MAX(cur->address, (u_longlong_t)sysbase);
		range_end  = MIN(cur->address + cur->size,
		    (u_longlong_t)syslimit);
		if (range_end > range_base)
			as_setprot(&kas, (caddr_t)range_base,
			    (u_int)(range_end - range_base), 0);
	}
	rw_exit(&kas.a_lock);

	/*
	 * Find the begining page frames of the kernel data
	 * segment and the ending page frame (-1) for bss.
	 */
	/*
	 * FIXME - nobody seems to use them but we could later on.
	 */
	pfnum = va_to_pfn((caddr_t)roundup((u_int)e_text, DATA_ALIGN));
	if (pfnum != (u_int)-1)
		kpfn_dataseg = pfnum;
	if ((pfnum = va_to_pfn(e_data)) != -1)
		kpfn_endbss = pfnum;

KVM_DONE
}

static void
setup_kvpm(void)
{
	u_int va;
	int i = 0;
	u_int pages = 0;
	u_int pfnum, nextpfnum;
	u_int lastpfnum;
	u_int npgs, pfn;

	lastpfnum = (u_int)-1;
	npgs = btop(MMU_PAGESIZE);

	rw_enter(&kas.a_lock, RW_READER);
	for (va = (u_int)KERNELBASE; va < (u_int)econtig; va += MMU_PAGESIZE) {
		pfnum = hat_getpfnum(&kas, (caddr_t)va);
		if (pfnum != (u_int)-1) {
			if (lastpfnum == (u_int)-1) {
				/* first pfnum on this entry */
				lastpfnum = pfnum;
				kvtopdata.kvtopmap[i].kvpm_vaddr = va;
				kvtopdata.kvtopmap[i].kvpm_pfnum = lastpfnum;
				pages = npgs;
				nextpfnum = lastpfnum + npgs;
			} else if (nextpfnum == pfnum) {
				/* contiguous pfn so update current entry */
				nextpfnum = pfnum + npgs;
				pages += npgs;
			} else {
				/* not contiguous so end current entry and */
				/* start new one */
				kvtopdata.kvtopmap[i].kvpm_len = pages;
				lastpfnum = pfnum;
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
		if (pfn != (u_int) -1) {
			if (lastpfnum == (u_int) -1) {
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
		} else if (lastpfnum != (u_int) -1) {
			lastpfnum = (u_int) -1;
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

	rw_exit(&kas.a_lock);

	msgbuf.msg_map = va_to_pa((caddr_t)&kvtopdata);
}

/*
 * Use boot to allocate the physical memory needed for the IOMMU's TSB arrays.
 * When accessing this memory, the IOMMU will be using MMU bypass mode,
 * so we can relinquish the virtual address mappings to the space.
 *
 * WARNING - since this routine uses boot to allocate memory, it MUST
 * be called before the kernel takes over memory allocation from boot.
 */
u_longlong_t iommu_tsb_physaddr[] = {	0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0};

caddr_t
iommu_tsb_alloc(caddr_t alloc_base)
{
	caddr_t vaddr;
	int i, total_size, size;
	caddr_t iommu_alloc_base = (caddr_t)roundup((u_int)alloc_base,
	    MMU_PAGESIZE);

	/*
	 * determine the amount of physical memory required for the TSB arrays
	 *
	 * assumes iommu_tsb_physaddr[] has already been initialized, i.e.
	 * map_wellknown_devices()
	 */
	for (i = total_size = 0; i < MAX_UPA; i++)
		total_size += iommu_tsb_physaddr[i];
	if (total_size == 0)
		return (alloc_base);

	/*
	 * allocate the physical memory for the TSB arrays
	 */
	if ((vaddr = (caddr_t)BOP_ALLOC(bootops, iommu_alloc_base,
	    total_size, MMU_PAGESIZE)) == NULL)
		cmn_err(CE_PANIC, "Cannot allocate IOMMU TSB arrays");

	/*
	 * assign the physical addresses for each TSB
	 */
	for (i = 0; i < MAX_UPA; i++) {
		if ((size = iommu_tsb_physaddr[i]) != 0) {
			iommu_tsb_physaddr[i] = va_to_pa((caddr_t)vaddr);
			vaddr += size;
		}
	}

	return (iommu_alloc_base + total_size);
}

char obp_tte_str[] =
	"%x constant MMU_PAGESHIFT "
	"%x constant TTE8K "
	"%x constant SFHME_SIZE "
	"%x constant SFHME_TTE "
	"%x constant HMEBLK_TAG "
	"%x constant HMEBLK_NEXT "
	"%x constant HMEBLK_MISC "
	"%x constant HMEBLK_HME1 "
	"%x constant NHMENTS "
	"%x constant HBLK_SZMASK "
	"%x constant HBLK_RANGE_SHIFT "
	"%x constant HMEBP_HBLK "
	"%x constant HMEBUCKET_SIZE "
	"%x constant HTAG_SFMMUPSZ "
	"%x constant HTAG_REHASHSZ "
	"%x constant MAX_HASHCNT "
	"%x constant uhme_hash "
	"%x constant khme_hash "
	"%x constant UHMEHASH_SZ "
	"%x constant KHMEHASH_SZ "
	"%x constant KHATID "
	"%x constant CTX_SIZE "
	"%x constant CTX_SFMMU "
	"%x constant ctxs "
	"%x constant ASI_MEM "

	": PHYS-X@ ( phys -- data ) "
	"   ASI_MEM spacex@ "
	"; "

	": PHYS-W@ ( phys -- data ) "
	"   ASI_MEM spacew@ "
	"; "

	": PHYS-L@ ( phys -- data ) "
	"   ASI_MEM spaceL@ "
	"; "

	": TTE_PAGE_SHIFT ( ttesz -- hmeshift ) "
	"   3 * MMU_PAGESHIFT + "
	"; "

	": TTE_IS_VALID ( ttep -- flag ) "
	"   PHYS-X@ 0< "
	"; "

	": HME_HASH_SHIFT ( ttesz -- hmeshift ) "
	"   dup TTE8K =  if "
	"      drop HBLK_RANGE_SHIFT "
	"   else "
	"      TTE_PAGE_SHIFT "
	"   then "
	"; "

	": HME_HASH_BSPAGE ( addr hmeshift -- bspage ) "
	"   tuck >> swap MMU_PAGESHIFT - << "
	"; "

	": HME_HASH_FUNCTION ( sfmmup addr hmeshift -- hmebp ) "
	"   >> over xor swap                    ( hash sfmmup ) "
	"   KHATID <>  if                       ( hash ) "
	"      UHMEHASH_SZ and                  ( bucket ) "
	"      HMEBUCKET_SIZE * uhme_hash +     ( hmebp ) "
	"   else                                ( hash ) "
	"      KHMEHASH_SZ and                  ( bucket ) "
	"      HMEBUCKET_SIZE * khme_hash +     ( hmebp ) "
	"   then                                ( hmebp ) "
	"; "

	": HME_HASH_TABLE_SEARCH ( hmebp hblktag -- null | hmeblkp ) "
	"   >r HMEBP_HBLK + x@			( hmeblkp ) ( r: hblktag ) "
	"   begin                               ( hmeblkp ) ( r: hblktag ) "
	"      dup  if                          ( hmeblkp ) ( r: hblktag ) "
	"         dup HMEBLK_TAG + PHYS-X@ r@ =  if ( hmeblkp ) ( r: hblktag ) "
	"            true                       ( hmeblkp true ) "
						"( r: hblktag ) "
	"         else                          ( hmeblkp ) ( r: hblktag ) "
	"            HMEBLK_NEXT + PHYS-X@ false     ( hmeblkp' false ) "
						"( r: hblktag ) "
	"         then                          ( hmeblkp flag ) "
						"( r: hblktag ) "
	"      else                             ( null ) ( r: hblktag ) "
	"         true                          ( null true ) ( r: hblktag ) "
	"      then                             ( hmeblkp flag ) "
						"( r: hblktag ) "
	"   until                               ( null | hmeblkp ) "
						"( r: hblktag ) "
	"   r> drop                             ( null | hmeblkp ) "
	"; "

	": CNUM_TO_SFMMUP ( cnum -- sfmmup ) "
	"   CTX_SIZE * ctxs + CTX_SFMMU + l@ "
	"; "

	": HME_HASH_TAG ( sfmmup rehash addr -- hblktag ) "
	"   over HME_HASH_SHIFT HME_HASH_BSPAGE      ( sfmmup rehash bspage ) "
	"   HTAG_REHASHSZ << or HTAG_SFMMUPSZ << or  ( hblktag ) "
	"; "

	": HBLK_TO_TTEP ( hmeblkp addr -- ttep ) "
	"   over HMEBLK_MISC + PHYS-L@ HBLK_SZMASK and  ( hmeblkp addr ttesz ) "
	"   TTE8K =  if                            ( hmeblkp addr ) "
	"      MMU_PAGESHIFT >> NHMENTS 1- and     ( hmeblkp hme-index ) "
	"   else                                   ( hmeblkp addr ) "
	"      drop 0                              ( hmeblkp 0 ) "
	"   then                                   ( hmeblkp hme-index ) "
	"   SFHME_SIZE * + HMEBLK_HME1 +           ( hmep ) "
	"   SFHME_TTE +                            ( ttep ) "
	"; "

	": unix-tte ( addr cnum -- false | tte-data true ) "
	"   over h# 20 >> 0<>  if             ( addr cnum ) "
	"      2drop false                    ( false ) "
	"   else                              ( addr cnum ) "
	"      CNUM_TO_SFMMUP                 ( addr sfmmup ) "
	"      MAX_HASHCNT 1+ 1  do           ( addr sfmmup ) "
	"         2dup swap i HME_HASH_SHIFT  "
					"( addr sfmmup sfmmup addr hmeshift ) "
	"         HME_HASH_FUNCTION           ( addr sfmmup hmebp ) "
	"         over i 4 pick               "
				"( addr sfmmup hmebp sfmmup rehash addr ) "
	"         HME_HASH_TAG                ( addr sfmmup hmebp hblktag ) "
	"         HME_HASH_TABLE_SEARCH       "
					"( addr sfmmup { null | hmeblkp } ) "
	"         ?dup  if                    ( addr sfmmup hmeblkp ) "
	"            nip swap HBLK_TO_TTEP    ( ttep ) "
	"            dup TTE_IS_VALID  if     ( valid-ttep ) "
	"               PHYS-X@ true          ( tte-data true ) "
	"            else                     ( invalid-tte ) "
	"               drop false            ( false ) "
	"            then                     ( false | tte-data true ) "
	"            unloop exit              ( false | tte-data true ) "
	"         then                        ( addr sfmmup ) "
	"      loop                           ( addr sfmmup ) "
	"      2drop false                    ( false ) "
	"   then                              ( false ) "
	"; "
;

void
create_va_to_tte(void)
{
	char *bp;
	extern int khmehash_num, uhmehash_num;
	extern struct hmehash_bucket *khme_hash, *uhme_hash;

#define	OFFSET(type, field)	((int)(&((type *)0)->field))

	bp = (char *)kobj_zalloc(MMU_PAGESIZE, KM_SLEEP);

	/*
	 * Teach obp how to parse our sw ttes.
	 */
	sprintf(bp, obp_tte_str,
		MMU_PAGESHIFT,
		TTE8K,
		sizeof (struct sf_hment),
		OFFSET(struct sf_hment, hme_tte),
		OFFSET(struct hme_blk, hblk_tag),
		OFFSET(struct hme_blk, hblk_nextpa),
		OFFSET(struct hme_blk, hblk_misc),
		OFFSET(struct hme_blk, hblk_hme),
		NHMENTS,
		HBLK_SZMASK,
		HBLK_RANGE_SHIFT,
		OFFSET(struct hmehash_bucket, hmeh_nextpa),
		sizeof (struct hmehash_bucket),
		HTAG_SFMMUPSZ,
		HTAG_REHASHSZ,
		MAX_HASHCNT,
		uhme_hash,
		khme_hash,
		UHMEHASH_SZ,
		KHMEHASH_SZ,
		KHATID,
		sizeof (struct ctx),
		OFFSET(struct ctx, c_sfmmu),
		ctxs,
		ASI_MEM);
	prom_interpret(bp, 0, 0, 0, 0, 0);

	kobj_free(bp, MMU_PAGESIZE);
}

void
install_va_to_tte(void)
{
	/*
	 * advise prom that he can use unix-tte
	 */
	prom_interpret("' unix-tte is va>tte-data", 0, 0, 0, 0, 0);
}


void
forthdebug_init(void)
{
	char *bp = NULL;
	struct _buf *file = NULL;
	int read_size, ch;
	int buf_size = 0;

	file = kobj_open_path(FDEBUGFILE, 1);
	if (file == (struct _buf *)-1) {
		cmn_err(CE_CONT, "Can't open %s\n", FDEBUGFILE);
		goto bad;
	}

	/*
	 * the first line should be \ <size>
	 * XXX it would have been nice if we could use lex() here
	 * instead of doing the parsing here
	 */
	while (((ch = kobj_getc(file)) != -1) && (ch != '\n')) {
		if ((ch) >= '0' && (ch) <= '9') {
			buf_size = buf_size * 10 + ch - '0';
		} else if (buf_size) {
			break;
		}
	}

	if (buf_size == 0) {
		cmn_err(CE_CONT, "can't determine size of %s\n", FDEBUGFILE);
		goto bad;
	}

	/*
	 * skip to next line
	 */
	while ((ch != '\n') && (ch != -1)) {
		ch = kobj_getc(file);
	}

	/*
	 * Download the debug file.
	 */
	bp = (char *)kobj_zalloc(buf_size, KM_SLEEP);
	read_size = kobj_read_file(file, bp, buf_size, 0);
	if (read_size < 0) {
		cmn_err(CE_CONT, "Failed to read in %s\n", FDEBUGFILE);
		goto bad;
	}
	if (read_size == buf_size && kobj_getc(file) != -1) {
		cmn_err(CE_CONT, "%s is larger than %d\n",
			FDEBUGFILE, buf_size);
		goto bad;
	}
	bp[read_size] = 0;
	cmn_err(CE_CONT, "Read %d bytes from %s\n", read_size, FDEBUGFILE);
	prom_interpret(bp, 0, 0, 0, 0, 0);

bad:
	if (file != (struct _buf *) -1) {
		kobj_close_file(file);
	}

	/*
	 * Make sure the bp is valid before calling kobj_free.
	 */
	if (bp != NULL) {
		kobj_free(bp, buf_size);
	}
}

/*
 * Check obp version
 * Right now we just print a warning if we see a downrev prom.
 * Later, we will panic if the level is unsupported.
 */
static void
check_obp_version()
{
	int level;

	if (prom_test_method("SUNW,dtlb-load",
			prom_getphandle(prom_mmu_ihandle())) != 0)
		level = CE_PANIC;
	else if (prom_test_method("SUNW,retain",
			prom_getphandle(prom_memory_ihandle())) != 0)
		level = CE_WARN;
	else
		level = 0;
	if (level) {
		cmn_err(CE_CONT, "Downrev version of OBP detected\n");
		cmn_err(CE_CONT, "Please flash-update to at least:\n");
		cmn_err(CE_CONT, "\tNeutron   Beta2 Version 1\n");
		cmn_err(CE_CONT, "\tElectron  Beta2 Version 1\n");
		cmn_err(CE_CONT, "\tPulsar    Beta2 Version 1\n");
		cmn_err(CE_CONT, "\tSunfire   Pre-ALPHA Version 1\n");
		cmn_err(level, "downrev prom");
	}
}
