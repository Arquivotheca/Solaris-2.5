/*
 * Copyright (c) 1990-1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)startup.c	1.32	94/08/26 SMI"

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
#include <sys/procfs.h>
#include <sys/acct.h>
#include <sys/time.h>
#include <sys/bitmap.h>
#include <sys/autoconf.h>
#include <sys/dki_lock.h>

#include <sys/vfs.h>
#include <sys/dnlc.h>
#include <sys/var.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>

#include <sys/dumphdr.h>
#include <sys/bootconf.h>
#include <sys/openprom.h>
#include <sys/varargs.h>
#include <sys/obpdefs.h>
#include <sys/promif.h>
#include <sys/comvec.h>		/* still needed ? */
#include <sys/modctl.h>		/* for "procfs" hack */
#include <sys/kvtopdata.h>
#include <sys/machsystm.h>
#include <sys/fpu/fpusystm.h>

#include <sys/consdev.h>
#include <sys/frame.h>

#define	SUNDDI_IMPL		/* so sunddi.h will not redefine splx() et al */
#include <sys/sunddi.h>
#include <sys/psw.h>
#include <sys/reg.h>
#include <sys/clock.h>
#include <sys/pte.h>
#include <sys/scb.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/mem.h>
#include <sys/stack.h>
#include <sys/eeprom.h>
#include <sys/intreg.h>
#include <sys/memerr.h>
#include <sys/buserr.h>
#include <sys/enable.h>
#include <sys/auxio.h>
#include <sys/trap.h>
#include <sys/eccreg.h>
#include <sys/vme.h>

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

#include <vm/hat_sunm.h>

#include <sys/vtrace.h>
#include <sys/instance.h>

/*
 * External Routines:
 */
extern void param_calc(int);
extern void param_init();

#define	MONSIZE	(SUNMON_END - SUNMON_START)

/*
* new memory fragmentations are possible in startup() due to BOP_ALLOCs. this
* depends on number of BOP_ALLOC calls made and requested size, memory size `
*  combination.
*/
#define	POSS_NEW_FRAGMENTS	10

/*
 * External Data:
 */
extern lksblk_t *lksblks_head;
extern int ondelay;
extern int maxusers;

/*
 * Dynamically allocated MMU structures
 */
extern struct	hatops	*sys_hatops;
extern struct	ctx	*ctxs,	*ctxsNCTXS;
extern struct	hwpmg	*hwpmgs, *hwpmgsNHWPMGS;

#ifdef	MMU_3LEVEL
extern struct	smgrp	*smgrps, *smgrpsNSMGRPS;
extern struct	sment	*sments, *smentsNSMENTS;
#endif	MMU_3LEVEL

/*
 * Global Routines:
 *
 * startup()
 * post_startup()
 */

/*
 * Global Data Definitions:
 */

/*
 * Declare these as initialized data so we can patch them.
 */
int physmem = 0;	/* memory size in pages, patch if you want less */
int kernprot = 1;	/* write protect kernel text */
int chkkas = 1;		/* verify dynamic kernel data as sane in kvm_init */

u_int dfldsiz;		/* default data size limit */
u_int dflssiz;		/* default stack size limit */
u_int maxdsiz;		/* max data size limit */
u_int maxssiz;		/* max stack size limit */

caddr_t s_text;		/* start of kernel text segment */
caddr_t e_text;		/* end of kernel text segment */
caddr_t s_data;		/* start of kernel data segment */
caddr_t e_data;		/* end of kernel data segment */

struct bootops *bootops = 0;	/* passed in from boot in %o2 */
char obpdebugger[32] = "misc/obpsym";

/*
 * Configuration parameters set at boot time.
 */
caddr_t hole_start;		/* addr of start of MMU "hole" */
caddr_t hole_end;		/* addr of end of MMU "hole" */
int hole_shift;			/* "hole" check shift to get high addr bits */
caddr_t econtig;		/* End of first block of contiguous kernel */
struct map *dvmamap;		/* map to manage usable dvma space */

#ifdef	VAC
#ifndef MPSAS		/* no cache in mpsas */
int use_vac = 1;	/* variable to patch to have kernel use the cache */
#else
int use_vac = 0;
#endif
#else	!VAC
#define	use_vac 0
#endif	!VAC
u_int	vac_mask;		/* vac alignment consistency mask */

u_int shm_alignment;		/* VAC address consistency modulus */
struct memlist *phys_install;	/* Total installed physical memory */
struct memlist *phys_avail;	/* Available (unreserved) physical memory */
struct memlist *virt_avail;	/* Available (unmapped?) virtual memory */
int memexp_flag;		/* memory expansion card flag */
u_int debugger_start = 0;	/* where debgger starts, if present */

/*
 * VM data structures
 */

int	page_hashsz;		/* Size of page hash table (power of two) */
struct	page *pp_base;		/* Base of system page struct array */
u_int	pp_sz;
struct	page **page_hash;	/* Page hash table */
struct	seg *segkmap;		/* Kernel generic mapping segment */
struct	seg ktextseg;		/* Segment used for kernel executable image */
struct	seg kvalloc;		/* Segment used for "valloc" mapping */
struct  seg kdebugseg;		/* Segment used for mapping the debugger */
struct  seg kmonseg;		/* Segment used for mapping the prom */
struct	seg *segkp;		/* Segment for pageable kernel virt. memory */
struct	memseg *memseg_base;	/* Used to translate a va to page */
u_int	memseg_sz;

/*
 * VM data structures allocated early during boot
 */
/*
 *	Fix for bug 1119063.
 */
#define	KERNELMAP_SZ(frag)	\
	max(MMU_PAGESIZE,	\
	roundup((sizeof (struct map) * SYSPTSIZE/2/(frag)), MMU_PAGESIZE))

caddr_t	valloc_base;		/* base of "valloc" data */
u_int	pagehash_sz;
u_int	memlist_sz;
u_int	kernelmap_sz;
u_int	ekernelmap_sz;
caddr_t	startup_alloc_vaddr = (caddr_t)SYSBASE + MMU_PAGESIZE;
caddr_t	startup_alloc_size;
u_int	startup_alloc_chunk_size = 20 * MMU_PAGESIZE;
u_int	hwpmgs_sz;
u_int	ctxs_sz;
struct vnode	unused_pages_vp;
struct vnode	prom_pages_vp;

/*
 * Saved beginning page frame for kernel .data and last page
 * frame for up to end[] for the kernel. These are filled in
 * by kvm_init().
 */
u_int	kpfn_dataseg, kpfn_endbss;

/*
 * crash dump, libkvm support - see sys/kvtopdata.h for details
 */
struct kvtopdata kvtopdata;

/*
 * Static Routines:
 */
static void init_cpu_info(struct cpu *);
static void kphysm_init(struct page *, struct memseg *, u_int, u_int);
static void kvm_init();
static void setup_kvpm();

static void kern_preprom(void);
static void kern_postprom(void);

/*
 * Debugging Stuff:
 */

#ifdef DEBUGGING_MEM

static int machdep_dset; /* patch to 1 for debug, 2 for line numbers too */
static char FILE[] = __FILE__;	/* save some data */

static void
print_mem_list(char *title, struct memlist *listp)
{
	struct memlist *list;

	prom_printf("%s\n", title);
	if (!listp)
		return;

	for (list = listp; list; list = list->next) {
		prom_printf("addr = 0x%x%8x, size = 0x%x%8x\n",
		    (u_int)(list->address >> 32), (u_int)list->address,
		    (u_int)(list->size >> 32), (u_int)list->size);
		if (machdep_dset > 1)
			prom_printf("next 0x%x prev 0x%x next avail 0x%x%8x\n",
			    list->next, list->prev,
			    (u_int)((list->address + list->size) >> 32),
			    (u_int)(list->address + list->size));
	}
}

static void
dump_mmu_entry(caddr_t addr)
{
	struct pte pte;

	prom_printf("vaddr = %x pmgrp = %x\n", addr, map_getsgmap(addr));
	mmu_getpte(addr, &pte);
	prom_printf("v %x prot %x nc %x type %x r %x m %x pfnum %x pte %x\n",
	    pte.pg_v, pte.pg_prot, pte.pg_nc, pte.pg_type, pte.pg_r,
	    pte.pg_m, pte.pg_pfnum, *(u_int *)&pte);
}

static void
dump_mmu_range(caddr_t startaddr, caddr_t endaddr)
{

	for (; startaddr < endaddr; startaddr += MMU_PAGESIZE)
		dump_mmu_entry(startaddr);
}

#define	DPRINTF(args)			\
	if (machdep_dset) {		\
		if (machdep_dset > 1)	\
			prom_printf("machdep.c line %d: ", __LINE__);	\
		printf args;		\
	}
#define	PRINT_MEM_LIST(a1, a2)	if (machdep_dset) print_mem_list((a1), (a2))
#define	DUMP_MMU_RANGE(a1, a2)	if (machdep_dset) dump_mmu_range((a1), (a2))
#define	DUMP_MMU_ENTRY(addr)	if (machdep_dset) dump_mmu_entry((addr))

#else /* DEBUGGING_MEM */

#define	DPRINTF(args)
#define	PRINT_MEM_LIST(arg1, arg2)
#define	DUMP_MMU_RANGE(adr1, adr2)
#define	DUMP_MMU_ENTRY(addr)

#endif /* DEBUGGING_MEM */

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
 * which are 8k on sun4e. Thus, for us the first 4 pages
 * of physical memory are equivalent of the first two
 * 8k pages on the sun4.
 *
 * Note the lack of information about physical memory layout;
 * you cannot depend on anything more.  What is assumed
 * and required from the memory allocation support of boot/prom
 * is that the memory used for the kernel segments allocated in
 * one request will be returned as one large contiguous chunk.
 * Thus the kernel text, data, bss, and the valloc data
 * need not be contiguous to one another but will be contiguous
 * within.
 *
 * XXX - the "quick page map region" hasn't been implemented yet,
 * but will be an extension/replacement for the va_cache used by
 * pagecopy and will also be used by ppcopy and the mem driver
 *
 *
 *		Physical memory layout
 *		(not necessarily contiguous)
 *
 *		|-----------------------|
 *	page 1	|	msgbuf		|
 *		|-----------------------|
 *	page 0	|  unused- reclaimed    |
 *		|_______________________|
 *
 *
 *		Virtual memory layout.
 * 0xFFFFF000  -|-----------------------|
 *		|	interrupt reg	|	NPMEG-2
 * 0xFFFFE000  -|-----------------------|
 *		|   memory error reg    |
 * 0xFFFFC000  -|-----------------------|
 * 		|	ECC memory reg	|
 * 0xFFFFA000  -|-----------------------|
 * 		| eeprom, idprom, clock |
 * 0xFFFF8000  -|-----------------------|
 * 		|	counter regs	|
 * 0xFFFF6000  -|-----------------------|
 *		|	auxio reg	|
 * 0xFFFF4000  -|-----------------------|
 *		|	VME reg		|
 * 0xFFFF2000  -|-----------------------|
 *		|	Kernel Virtual	|
 *		| mappings for DVMA and |
 *		| noncached iopb memory |
 * 0xFFF00000  -|-----------------------|- DVMA
 *		|	monitor		|
 * 0xFFD00000  -|-----------------------|- SUNMON_START
 *		|	segkmap		|	SEGMAPSIZE	(4 M)
 * 0xFFB00000  -|-----------------------|- E_Syslimit
 *		|	E_Sysmap	|			(9 M)
 *		| primary kmem_alloc    |
 *		| pool area (kernelmap) |
 * 0xFF000000  -|-----------------------|- Syslimit, E_Sysbase
 *		|	Sysmap		|
 *		| secondary kmem_alloc	|			(48 M)
 *		| pool area (kernelmap) |
 * 0xFC000000  -|-----------------------|- Sysbase
 *		|    exec args area	|			(1 M)
 * 0xFBF00000  -|-----------------------|- ARGSBASE
 *		|	SEGTEMP2	|			(256 K)
 *		|-----------------------|
 *		|	SEGTEMP		|			(256 K)
 * 0xFBE80000  -|-----------------------|-
 *		| quick page map region |			(512 K)
 * 0xFBE00000  -|-----------------------|- PPMAPBASE
 *		|	debugger	|			(1 M)
 * 0xFBD00000  -|-----------------------|- DEBUGSTART
 *		|	unused		|	XXX kobj workspace??
 *		|-----------------------|
 *		|	segkp		|
 *		|-----------------------|- econtig
 *		|    page structures    |
 *		|-----------------------|- end
 *		|	kernel		|
 *		|-----------------------|
 *		|	trap table (4k)	|
 * 0xF0004000  -|-----------------------|- start
 *		|	msgbuf		|
 * 0xF0002000  -|-----------------------|- msgbuf
 *		|  user copy red zone   |
 *		|	(invalid)	|
 * 0xF0000000  -|-----------------------|- KERNELBASE
 *		|	user stack	|
 *		|			|
 *		|			|
 *		|	user data	|
 *		|-----------------------|
 *		|	user text	|
 * 0x00002000  -|-----------------------|
 *		|	invalid		|
 * 0x00000000  _|_______________________|
 */


/*
 * Machine-dependent startup code
 */
void
startup()
{
	register int unixsize;
	register unsigned i;
	u_int npages;
	struct segmap_crargs a;
	int dbug_mem, memblocks;
	u_int pp_giveback;
	caddr_t memspace;
	u_int memspace_sz;
	u_int nppstr;
	u_int segkp_limit = 0;
	caddr_t va;
	struct memlist *memlist;
	int	max_virt_segkp;
	int	max_phys_segkp;

	extern caddr_t e_data;
	extern void mt_lock_init();

	(void) check_boot_version(BOP_GETVERSION(bootops));

#ifdef VAC
#define	NO_OP	0x01000000
	/*
	 * XXX - There are are several sets of instructions in locore.s
	 * to work around hardware bug #1050558. It applies only to the
	 * cache+ chip first used by SS2. For those machines that don't
	 * have this bug, we no-op the instructions here.
	 * There is a long, general sequence at sys_trap, and 4 3-instruction
	 * sequences at: go_multiply_check, go_window_overflow,
	 * and go_window_underflow.
	 */
	if ((cpu_buserr_type != 1) || (use_vac == 0)) {
		int	*nopp = (int *)NULL;
		int	**noppp;
#ifndef lint
		extern int sys_trap[];
		extern int go_multiply_check[];
		extern int go_window_overflow[];
		extern int go_window_underflow[];
#endif lint
		static int	*noppps[] = {
#ifndef lint
			go_multiply_check,
			go_window_overflow,
			go_window_underflow,
#endif lint
			(int *)0
		};

#ifndef lint
		nopp = sys_trap;
#endif lint
		for (i = 0; i < 7; i++)
			*nopp++ = NO_OP;

		for (noppp = noppps; *noppp; noppp++) {
			nopp = *noppp;
			for (i = 0; i < 3; i++)
				*nopp++ = NO_OP;
		}
	}
#endif VAC

	dki_lock_setup(lksblks_head);

	/*
	 * Initialize the turnstile allocation mechanism.
	 */
	tstile_init();

	kncinit();

	ppmapinit();

#ifdef VAC
	if (vac) {
		if (use_vac) {

			/*
			 * For sun4e architectures, boot turns on the cache,
			 * by default, but gives us pages that are marked
			 * uncached.  So before we go any further, mark our
			 * text, data and bss as cacheable.  (kvm_init does
			 * a better job of this later .. we do this here for
			 * the sake of early boot performance).
			 */
			for (va = (caddr_t)KERNELBASE;
			    va < e_data; va += MMU_PAGESIZE) {
				u_int pte;

				pte = map_getpgmap(va);
				if (pte & PG_V)
					pte &= ~PG_NC;
				map_setpgmap(va, pte);
			}

			/*
			 * 2 as an argument to vac_control indicates that
			 * we are booting the system.
			 */
			vac_control(2);
			setdelay(ondelay);
			shm_alignment = vac_size;
			vac_mask = MMU_PAGEMASK & (shm_alignment - 1);
		} else  {
			printf("CACHE IS OFF!\n");
			vac_control(0);
			shm_alignment = PAGESIZE;
		}
	}
#endif VAC

#ifdef	IOC
	ioc = 0;
	if (ioc) {
		ioc_init();
		if (use_ioc) {
			on_enablereg(ENA_IOCACHE);
			iocenable = 1;
		} else {
			printf("IO CACHE IS OFF!\n");
			iocenable = 0;
			ioc = 0;
		}
	}
#endif	/* IOC */

#ifdef	BCOPY_BUF
	bcopy_buf = 0;
	if (bcopy_buf) {
		if (use_bcopy) {
			bcopy_res = 0;		/* allow use of hardware */
		} else {
			printf("bcopy buffer disabled\n");
			bcopy_res = -1;		/* reserve now, hold forever */
		}
	}
#endif	/* BCOPY_BUF */

#ifdef	MMU_3LEVEL
	mmu_3level = 0;
	if (mmu_3level) {
		/* no hole in a 3 level mmu, just set it halfway */
		/* XXX no cpuinfo */
		nsmgrps = cpuinfop->cpui_nsme / NSMENTPERSMGRP;
		hole_shift = 31;
		hole_start = (caddr_t)(1 << hole_shift);
	} else
#endif	/* MMU_3LEVEL */
	{
		/* compute hole size in 2 level MMU */
		hole_start = (caddr_t)((NPMGRPPERCTX / 2) * PMGRPSIZE);
		hole_shift = ffs((long)hole_start) - 1;
	}
	hole_end = (caddr_t)(-(long)hole_start);
	if (maxdsiz == 0)
		maxdsiz = (int)hole_start - USRTEXT;
	if (maxssiz == 0)
		maxssiz = (int)hole_start - KERNELSIZE;
	if (dfldsiz == 0)
		dfldsiz = maxdsiz;

#ifdef	VA_HOLE
	ASSERT(hole_shift < 31);
#endif	/* VA_HOLE */

	/*
	 * The default stack size of 8M allows an optimization of mmu mapping
	 * resources so that in normal use a single mmu region map entry (smeg)
	 * can be used to map both the stack and shared libraries.
	 */
	if (dflssiz == 0)
		dflssiz = (8*1024*1024);

	/*
	* Sun4e has ECC memory support.
	* initialize Sun4/E ECC memory.
	*/
	ecc_init();

	/*
	 * initialize handling of memory errors
	 */
	memerr_init();

	/*
	 * allow interrupts now,
	 * after memory error handling has been initialized
	 * Must turn on SBus IRQ 6 also, because the prom
	 * didn't do it for us.
	 */
	*INTREG |= (IR_ENA_INT | IR_ENA_LVL8);


#if defined(SAS) || defined(MPSAS)
	/* SAS has contigouous memory */
	npages = physmem = btop(_availmem);
	physmax = physmem - 1;
	memblocks = 0; /* set this to zero so the valloc below ends up w/ 1 */
#else
	/*
	 * Install PROM callback handler (give both, promlib picks the
	 * appropriate handler.
	 */
	if (prom_sethandler(v_handler, vx_handler) != 0)
		panic("No handler for PROM?");

	/* take the most current snapshot we can by calling mem-update */
	if (BOP_GETPROPLEN(bootops, "memory-update") == 0)
		BOP_GETPROP(bootops, "memory-update", NULL);

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
	valloc_base = (caddr_t)roundup((u_int)e_data, MMU_PAGESIZE);

	/*
	 * Get the list of physically available memory to size
	 * the number of pages structures needed.
	 */
	size_physavail(bootops->boot_mem->physavail, &npages, &memblocks);

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
	page_hashsz = 1 << highbit(page_hashsz);
	pagehash_sz = sizeof (struct page *) * page_hashsz;

	/*
	 * Some of these locks depend on page_hashsz.
	 */
	mt_lock_init();

	/*
	 * The fixed size mmu data structures are allocated now.
	 * The dynamic data is allocated after we have enough
	 * of the system initialized to use kmem_alloc().
	 * The number of hwpmgs and ctxs are constants for any
	 * machine and are initialized in setcputype().
	 */
	hwpmgs_sz = sizeof (struct hwpmg) * npmgrps;
	ctxs_sz = sizeof (struct ctx) * nctxs;

	/*
	 * The memseg list is for the chunks of physical memory that
	 * will be managed by the vm system.  The number calculated is
	 * a guess as boot may fragment it more when memory allocations
	 * are made before kvm_init(), twice as many are allocated
	 * than are currently needed.
	 */
	memseg_sz = sizeof (struct memseg) * (memblocks + POSS_NEW_FRAGMENTS);
	pp_sz = sizeof (struct page) * npages;
	memspace_sz = pagehash_sz + hwpmgs_sz + ctxs_sz + memseg_sz + pp_sz;
	memspace_sz = roundup(memspace_sz, MMU_PAGESIZE);

	/*
	 * We don't need page structs for the memory we are allocating
	 * so we subtract an appropriate amount.
	 */
	nppstr = btop(memspace_sz - (btop(memspace_sz) * sizeof (struct page)));
	pp_giveback = nppstr * sizeof (struct page);
	pp_giveback &= MMU_PAGEMASK;

	memspace_sz -= pp_giveback;
	npages -= btopr(memspace_sz);
	pp_sz -= pp_giveback;

	memspace = (caddr_t)BOP_ALLOC(bootops, valloc_base, memspace_sz,
	    BO_NO_ALIGN);
	if (memspace != valloc_base)
		panic("system page struct alloc failure");
	bzero(memspace, memspace_sz);

	page_hash = (struct page **)memspace;
	hwpmgs = (struct hwpmg *)((u_int)page_hash + pagehash_sz);
	hwpmgsNHWPMGS = hwpmgs + npmgrps;
	ctxs = (struct ctx *)hwpmgsNHWPMGS;
	ctxsNCTXS = ctxs + nctxs;
	memseg_base = (struct memseg *)((u_int)ctxs + ctxs_sz);
	pp_base = (struct page *)((u_int)memseg_base + memseg_sz);
	econtig = valloc_base + memspace_sz;
	ASSERT(((u_int)econtig & MMU_PAGEOFFSET) == 0);

	/*
	 * the memory lists from boot, and early versions of kernelmap
	 * and ekernelmap are allocated now from the virtual address
	 * region managed by kernel map so that later they can be
	 * freed and/or reallocated.
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
	ekernelmap_sz = MMU_PAGESIZE;
	memspace_sz =  memlist_sz + ekernelmap_sz + kernelmap_sz;
	memspace = (caddr_t)BOP_ALLOC(bootops, startup_alloc_vaddr,
	    memspace_sz, BO_NO_ALIGN);
	startup_alloc_vaddr += memspace_sz;
	startup_alloc_size += memspace_sz;
	if (memspace == NULL)
		halt("Boot allocation failed.");
	bzero(memspace, memspace_sz);

	memlist = (struct memlist *)memspace;
	kernelmap = (struct map *)((u_int)memlist + memlist_sz);
	ekernelmap = (struct map *) ((u_int)kernelmap + kernelmap_sz);

	mapinit(ekernelmap, (long)(E_SYSPTSIZE - 1), (u_long)1,
	    "ethernet addressable kernel map",
	    ekernelmap_sz / sizeof (struct map));
	mapinit(kernelmap, (long)(SYSPTSIZE - 1), (u_long)1,
	    "kernel map", kernelmap_sz / sizeof (struct map));

	mutex_enter(&maplock(kernelmap));
	if (rmget(kernelmap, btop(startup_alloc_size), 1) == 0)
		panic("can't make initial kernelmap allocation");
	mutex_exit(&maplock(kernelmap));

	/*
	 * We need to start copying /boots memlists into kernel memory
	 * space since all of /boots memory will be reclaimed by the kernel.
	 * We only copy the phys_avail list at this time because we want the
	 * kernel to know how much physical memory is available to it now
	 * so it can size it's memsegs, page lists, and page hash arrays.
	 * The only physical memory not seen by the kernel at this time is
	 * /unix text, data, and bss, PROM memory, and memory used by the
	 * debugger if it's resident.  The virt_avail and phys_installed list
	 * will be copied later, once /boot has completed memory allocations
	 * on behalf of the kernel.
	 *
	 * We need to break up boots physical available memlists to deduct
	 * the physical pages which are used for the msgbuf.  Yes we are
	 * wiring down the msgbuf to physical pages 2 and 3, and yes this can
	 * probably be made more generic, maybe with /boot, but our
	 * reconcile window beckons.
	 */

	/* take the most current snapshot we can by calling mem-update */
	if (BOP_GETPROPLEN(bootops, "memory-update") == 0)
		BOP_GETPROP(bootops, "memory-update", NULL);

	phys_avail = memlist;
	if (!copy_physavail(bootops->boot_mem->physavail, &memlist,
	    0x2000, 0x2000))
		halt("Can't deduct msgbuf from physical memory list");

#if defined(SAS) || defined(MPSAS)
	/* for SAS, memory is contiguous */
	page_init(pp_base, npages, pp_base, memseg_base);

	first_page = memsegs->pages_base;
	if (first_page < mapaddr + btoc(econtig - e_data))
		first_page = mapaddr + btoc(econtig - e_data);
	memialloc(first_page, mapaddr + btoc(econtig - e_data),
		memsegs->pages_end);
#else   SAS
	/*
	 * Initialize the page structures from the memory lists.
	 */
	kphysm_init(pp_base, memseg_base, npages, memblocks+POSS_NEW_FRAGMENTS);
#endif  SAS

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
	 * Remember what the physically available highest page is
	 * so that dumpsys works properly, and find out how much
	 * memory is installed.
	 */
	installed_top_size(bootops->boot_mem->physinstalled, &physmax,
	    &physinstalled);

#endif /* SAS */

	/*
	 * Lets display the banner early so the user has some idea that
	 * Unix is taking over the system.
	 *
	 * Good {morning, afternoon, evening, night}.
	 */
	cmn_err(CE_CONT,
	    "\rSunOS Release %s Version %s [UNIX(R) System V Release 4.0]\n",
	    utsname.release, utsname.version);
	cmn_err(CE_CONT, "Copyright (c) 1983-1994, Sun Microsystems, Inc.\n");
#ifdef DEBUG
	cmn_err(CE_CONT, "DEBUG enabled\n");
#endif
#ifdef TRACE
	cmn_err(CE_CONT, "TRACE enabled\n");
#endif

	/*
	 * Read system file, (to set maxusers, npmgrps, physmem....)
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
	 * IF obpdebug is set, load the obpsym debugger module, now.
	 */
	if (obpdebug)
		(void) modload(NULL, obpdebugger);

#if !(defined(SAS) || defined(MPSAS))

	/*
	 * If debugger is in memory, note the pages it stole from physmem.
	 * XXX: Should this happen with V2 Proms?  I guess it would be
	 * set to zero in this case?
	 */
	if (boothowto & RB_DEBUG) {
		dbug_mem = *dvec->dv_pages;
	} else
		dbug_mem = 0;

#endif /* !(SAS || MPSAS) */

	/*
	 * maxmem is the amount of physical memory we're playing with.
	 */
	maxmem = physmem;

#if 0
	/*
	 * allocate the remaining kernel data structures.
	 * copy temporary kernelmap into an appropriately sized new one
	 * and free the old one.
	 * XXX - RAZ - fix me later
	 */
	if ((tmp = (struct map *)kmem_zalloc(sizeof (struct map) *
	    4 * v.v_proc, KM_NOSLEEP)) == NULL)
		panic("Cannot allocate memory for kernelmap");
#endif 0

	/*
	 * Initialize the hat layer.
	 */
	hat_init();

	/*
	 * Initialize segment management stuff.
	 */
	seg_init();

	/*
	 * Load some key modules.
	 */
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
	 * Call back into boot and release boots resources.
	 */
	BOP_QUIESCE_IO(bootops);

	/*
	 * Copy the remaining lists into kernel space
	 */
	phys_install = memlist;
	copy_memlist(bootops->boot_mem->physinstalled, &memlist);


	/*
	 * Virtual available next
	 */
	virt_avail = memlist;
	copy_memlist(bootops->boot_mem->virtavail, &memlist);

	/*
	 * Last chance to ask our booter questions ..
	 */
	(void) BOP_GETPROP(bootops, "debugger-start", (caddr_t)&debugger_start);
	segkp_limit = debugger_start;

	if (segkp_limit == 0)
		segkp_limit = (u_int)PPMAPBASE - DEBUGSIZE;

	/*
	 * We're done with boot.  Just after this point in time, boot
	 * gets unmapped, so we can no longer rely on its services.
	 * Zero the bootops to indicate this fact.
	 */
	bootops = (struct bootops *)NULL;
	BOOTOPS_GONE();

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
	 * Allocate a vm slot for the dev mem driver.
	 * XXX - this should be done differently, see pagecopy.
	 */
	i = rmalloc(kernelmap, 1);
	mm_map = (caddr_t)kmxtob(i);

	/*
	 * XXX: This should be much larger.
	 */
	if ((dvmamap = (struct map *)kmem_zalloc(sizeof (struct map) *
	    dvmasize >> 1, KM_NOSLEEP)) == NULL)
		panic("Cannot allocate memory for dvmamap");

	/*
	 * If the following is true, someone has patched
	 * physmem to be less than the number of pages that
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
			pp = page_create(&unused_pages_vp, off,
				MMU_PAGESIZE, PG_WAIT | PG_EXCL);
			if (pp == NULL)
				cmn_err(CE_PANIC, "limited physmem too much!");
			page_io_unlock(pp);
			availrmem--;
			off += MMU_PAGESIZE;
		}
	}

	/*
	 * When printing memory, show the total as physmem less
	 * that stolen by a debugger.
	 */
#if defined(SAS) || defined(MPSAS)
	cmn_err(CE_CONT, "?mem = %dK (0x%x)\n", _availmem / 1024, _availmem);
#else
	cmn_err(CE_CONT, "?mem = %dK (0x%x)\n",
	    (physinstalled - dbug_mem) << (PAGESHIFT - 10),
	    ptob(physinstalled - dbug_mem));
#endif SAS

	/*
	 * unixsize doesn't include any loaded modules,
	 * it is everything that is not backed by page structures.
	 */
	unixsize = btoc(econtig - KERNELBASE);

	/*
	 * The dvmamap manages the space DVMA[0..mmu_ptob(dvmasize)].
	 * We manage it in the range [ 1..dvmasize + 1 ]. The users
	 * of dvmamap can't use it directly- instead they call
	 * getdvmapages() to return a base virtual address for the
	 * requested number of pages.
	 */
	mapinit(dvmamap, (long)dvmasize, (u_long)1,
	    "DVMA map space", dvmasize >> 1);

#if !defined(SAS) && !defined(MPSAS)
	enable_dvma();
#endif

	cmn_err(CE_CONT, "?avail mem = %d\n", ctob(freemem));

	/*
	 * Initialize the u-area segment type.  We position it
	 * after the page struct array (whose end is given by econtig)
	 * and before Sysbase.
	 */

	va = (caddr_t)roundup((u_int)econtig, PMGRPSIZE);

	max_virt_segkp = btop(segkp_limit - (u_int)va);
	max_phys_segkp = physmem * 2;
	i = ptob(min(max_virt_segkp, max_phys_segkp));

	rw_enter(&kas.a_lock, RW_WRITER);
	segkp = seg_alloc(&kas, va, i);
	if (segkp == NULL)
		panic("startup: cannot allocate segkp");
	if (segkp_create(segkp) != 0)
		panic("startup: segkp_create failed");
	rw_exit(&kas.a_lock);

	/*
	 * Now create generic mapping segment.  This mapping
	 * goes NCARGS beyond Syslimit up to the SEGTEMP area.
	 * But if the total virtual address is greater than the
	 * amount of free memory that is available, then we trim
	 * back the segment size to that amount.
	 */
	va = E_Syslimit;
	i = SEGMAPSIZE;
	rw_enter(&kas.a_lock, RW_WRITER);
	segkmap = seg_alloc(&kas, va, i);
	if (segkmap == NULL)
		panic("cannot allocate segkmap");
	a.prot = PROT_READ | PROT_WRITE;

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

	init_cpu_info(CPU);
	init_intr_threads(CPU);
}

void
post_startup()
{
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
	 * hw_serial, used by gethostid(), filled in by call to setcputype()
	 */

	/*
	 * Compress the hwpmgs used by the kernel into the lowest
	 * parts of the hwpmg map.  When we have to steal resources,
	 * this allows the searches to skip locked kernel hwpmgs.
	 *
	 * XXX	Version 0 PROMs don't really allow us to do this safely
	 *	See bugid 1080549.
	 */
	if (prom_getversion() != 0)
		sunm_hwpmgshuffle();

	/*
	 * Install the "real" pre-emption guards
	 */
	prom_set_preprom(kern_preprom);
	prom_set_postprom(kern_postprom);

	(void) spl0();		/* allow interrupts */
}

/*
 * Nothing to do.
 */
void
start_other_cpus(void)
{}

/*
 * Begin Static Routines:
 */

/*
 * Init CPU info - get CPU type info for processor_info system call.
 */
static void
init_cpu_info(struct cpu *cp)
{
	register processor_info_t *pi = &cp->cpu_type_info;

	/*
	 * get clock frequency for processor_info system call.
	 * This will be zero if the property is not there.
	 */
	pi->pi_clock = (ddi_getprop(DDI_DEV_T_ANY, ddi_root_node(),
		0, "clock-frequency", 0) + 500000) / 1000000;

	strcpy(pi->pi_processor_type, "sparc");

	/*
	 * configure() might have filled in 'unsupported' for certain
	 * FPU revs and turned off fpu_exists.
	 */
	if (fpu_exists)
		strcpy(pi->pi_fputypes, "sparc");
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
 * kphysm_init() tackles the problem of initializing
 * physical memory.  The old startup made some assumptions about the
 * kernel living in physically contiguous space which is no longer valid.
 */
static void
kphysm_init(struct page *pp, struct memseg *memsegp, u_int npages, u_int blks)
{
	int index;
	struct memlist *pmem;
	struct memseg *memsegp_tmp;
	struct memseg **pmemseg;
	u_int np, dopages;
	u_int first_page, num_free_pages;
	struct memseg *mssort;
	struct memseg **psort;
	int curmax;
	struct memseg *largest_memseg;
	struct memseg *oldmemseglist;
	struct memseg *msegend;

	oldmemseglist = memsegp;
	memsegp_tmp = memsegp;

	index = 0;
	for (pmem = phys_avail; pmem; pmem = pmem->next) {

		first_page = mmu_btop(pmem->address);
		num_free_pages = mmu_btop(pmem->size);

		ASSERT(num_free_pages <= npages - index);

		if (num_free_pages)
			page_init(&pp[index], num_free_pages, first_page,
			    memsegp_tmp++);

		index += num_free_pages;
	}

	np = 0;
	msegend = memsegp + blks;
	for (memsegp_tmp = memsegp; memsegp_tmp;
	    memsegp_tmp = memsegp_tmp->next) {
		ASSERT(memsegp_tmp < msegend);
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
kvm_init()
{
	int i;
	register caddr_t va, tv;
	struct pte pte;
	struct memlist *cur;
	u_int range_size, range_base, range_end;
	struct hat *hat;
	struct sunm *sunm;
	extern caddr_t e_text;
	extern caddr_t e_data;
	extern caddr_t s_data;
	extern char t0stack[];
	extern struct ctx *kctx;
	extern int segkmem_ready;


/* XXXX kvm_int debugging stuff; for debugging strange cache & mmu bugs */
#ifndef KVM_DEBUG
#define	KVM_DEBUG 0		/* 0 = no debugging, 1 = debugging */
#endif

#if KVM_DEBUG > 0
#define	KVM_HERE \
	prom_printf("kvm_init: checkpoint %d line %d\n", ++kvm_here, __LINE__);
#define	KVM_DONE { printf("kvm_init: all done\n"); kvm_here = 0; }
	int kvm_here = 0;
#else
#define	KVM_HERE
#define	KVM_DONE
#endif

	/*
	 * This is the key memory list we look at, over and
	 * over again in this routine.
	 */
	PRINT_MEM_LIST("virt-avail", virt_avail);

KVM_HERE
	/*
	 * Put kernel segment in kernel address space.  Make it a
	 * "kernel memory" segment object.
	 */
	rw_enter(&kas.a_lock, RW_WRITER);

	hat = kas.a_hat;		/* initialized in hat_init */
	sunm = (struct sunm *)hat->hat_data;
	sunm->sunm_ctx = kctx;

	(void) seg_attach(&kas, (caddr_t)KERNELBASE, (u_int)(e_data -
	    KERNELBASE), &ktextseg);
	(void) segkmem_create(&ktextseg, (caddr_t)NULL);

KVM_HERE
	(void) seg_attach(&kas, (caddr_t)valloc_base, (u_int)econtig -
	    (u_int)valloc_base, &kvalloc);
	(void) segkmem_create(&kvalloc, (caddr_t)NULL);


	/*
	 * We're about to map out /boot.  This is the beginning of the
	 * system resource management transition. We can no longer
	 * call into /boot for I/O or memory allocations..
	 */

KVM_HERE
	if (boothowto & RB_DEBUG && debugger_start) {
		(void) seg_attach(&kas, (caddr_t)debugger_start,
			DEBUGSIZE, &kdebugseg);
		(void) segkmem_create(&kdebugseg, (caddr_t)NULL);
	}

KVM_HERE
	(void) seg_attach(&kas, (caddr_t)SYSBASE,
		(u_int)(Syslimit - SYSBASE), &kvseg);
	(void) segkmem_create(&kvseg, (caddr_t)Sysmap);

KVM_HERE
	(void) seg_attach(&kas, (caddr_t)E_SYSBASE,
		(u_int)(E_Syslimit - E_SYSBASE), &E_kvseg);
	(void) segkmem_create(&E_kvseg, (caddr_t)E_Sysmap);

KVM_HERE
	(void) seg_attach(&kas, (caddr_t)SUNMON_START, MONSIZE, &kmonseg);
	(void) segkmem_create(&kmonseg, (caddr_t)NULL);
	rw_exit(&kas.a_lock);

KVM_HERE
	rw_enter(&kas.a_lock, RW_READER);

	/*
	 * NOW we can ask segkmem for memory instead of boot
	 */
	segkmem_ready = 1;

#ifdef  MMU_3LEVEL
	/*
	 * If the system has a three level MMU make sure the invalid
	 * smeg is completely invalid
	 */
	if (mmu_3level) {
		for (i = 0; i < SMGRPSIZE; i += PMGRPSIZE) {
			struct pmgrp *pmgrp;
			pmgrp = mmu_getpmg((caddr_t)(REGTEMP + i));
			if (pmgrp->pmg_num != PMGRP_INVALID)
				mmu_pmginval((caddr_t)(REGTEMP + i));
		}
	}
#endif  MMU_3LEVEL

	/*
	 * Make sure the invalid pmeg is completely invalid.
	 * We use map_setpgmap and write the mmu directly
	 * because we assert in mmu_setpte that we are not
	 * writing the invalid pmeg.
	 */
	map_setsgmap(SEGTEMP, PMGRP_INVALID);
	for (i = 0; i < PMGRPSIZE; i += MMU_PAGESIZE) {
		mmu_getpte(SEGTEMP + i, &pte);
		if (pte.pg_v) {
			DPRINTF(("invalid pmeg was not invalid, fixing!\n"));
			pte.pg_v = 0;
			map_setpgmap(SEGTEMP + i, *(u_int *)&pte);
		}
	}

	/*
	 * Invalidate segments before kernel.
	 */

#ifdef  MMU_3LEVEL
	if (mmu_3level) {
		/*
		 * Make sure the invalid smeg is completely invalid
		 */
		/* XXX - done above, not needed, but verify */
		for (i = 0; i < SMGRPSIZE; i += PMGRPSIZE) {
			struct pmgrp *pmgrp;
			pmgrp = mmu_getpmg((caddr_t)(REGTEMP + i));
			if (pmgrp->pmg_num != PMGRP_INVALID)
				mmu_pmginval((caddr_t)(REGTEMP + i));
		}

		for (va = (addr_t)0; va < KERNELBASE; va += SMGRPSIZE) {
			/*
			 * Is this entire segment "available"?
			 */
			if (address_in_memlist(virt_avail, va, SMGRPSIZE)) {
				DPRINTF(("smeg at %x invalid\n", va));
				mmu_smginval(va);
			} else {
				/*
				 * No: so reserve those pmegs which are not
				 * invalid, and remove user access to those
				 * addresses.
				 */
				for (tv = va; tv < va + SMGRPSIZE; tv +=
				    PMGRPSIZE) {
					if (mmu_getpmg(tv) != pmgrp_invalid) {
					    DPRINTF(("va %x reserved\n", tv));
					    sunm_pmgreserve(&kas, tv,
						PMGRPSIZE);
					    hat_chgprot(&kas, tv,
						PMGRPSIZE, ~PROT_USER);
					}
				}
			}
		}
	} else
#endif	MMU_3LEVEL

	{
		for (va = (caddr_t)0; va < hole_start; va += PMGRPSIZE) {
			/*
			 * Is this entire pmeg "available"?
			 */
			if (address_in_memlist(virt_avail, va, PMGRPSIZE)) {
				if (mmu_getpmg(va) != pmgrp_invalid)
					mmu_pmginval(va);
			} else {
				/*
				 * No: so reserve those pmegs which are not
				 * invalid, and remove user access to those
				 * addresses.
				 */
				if (mmu_getpmg(va) != pmgrp_invalid) {
					DPRINTF(("va %x reserved\n", va));
					sunm_pmgreserve(&kas, va, PMGRPSIZE);
					hat_chgprot(&kas, va, PMGRPSIZE,
					    ~PROT_USER);
				}
			}
		}
		for (va = hole_end; va < (caddr_t)KERNELBASE; va += PMGRPSIZE) {
			/*
			 * Is this entire pmeg "available"?
			 */
			if (address_in_memlist(virt_avail, va, PMGRPSIZE)) {
				if (mmu_getpmg(va) != pmgrp_invalid)
					mmu_pmginval(va);
			} else {
				/*
				 * No: so reserve those pmegs which are not
				 * invalid, and remove user access to those
				 * addresses.
				 */
				if (mmu_getpmg(va) != pmgrp_invalid) {
					DPRINTF(("va %x reserved\n", va));
					sunm_pmgreserve(&kas, va, PMGRPSIZE);
					hat_chgprot(&kas, va, PMGRPSIZE,
					    ~PROT_USER);
				}
			}
		}
	}

	rw_exit(&kas.a_lock);

KVM_HERE
	kvm_dup();

KVM_HERE
	/*
	 * Initialize the kernel page maps.
	 */
	va = (caddr_t)KERNELBASE;

	/* user copy red zone */
	mmu_setpte((caddr_t)KERNELBASE, mmu_pteinvalid);
	(void) as_fault(hat, &kas, va, PAGESIZE, F_SOFTLOCK, S_OTHER);
	(void) as_setprot(&kas, va, PAGESIZE, 0);
	va += PAGESIZE;

KVM_HERE
	/*
	 * msgbuf
	 */
	(void) as_fault(hat, &kas, va, PAGESIZE, F_SOFTLOCK, S_OTHER);
	(void) as_setprot(&kas, va, PAGESIZE, PROT_READ | PROT_WRITE);
	va += PAGESIZE;

KVM_HERE
	/*
	 * (Normally) Read-only until end of text.
	 */
	(void) as_fault(hat, &kas, va, (u_int)(e_text - va),
		F_SOFTLOCK, S_OTHER);
	(void) as_setprot(&kas, va, (u_int)(e_text - va), (u_int)
		(PROT_READ | PROT_EXEC | ((kernprot == 0)? PROT_WRITE : 0)));

KVM_HERE
	/*
	 * Sometimes there can be a page or pages that are invalid
	 * between text and data, find them and invalidate them now,
	 * t0stack is the first thing in data space.
	 */
	for (cur = virt_avail; cur; cur = cur->next) {
		range_base = MAX((u_int)cur->address, (u_int)e_text);
		range_end = MIN((u_int)(cur->address + cur->size),
			(u_int)e_text + t0stack - e_text);
		if (range_end > range_base) {
			as_setprot(&kas, (caddr_t)range_base,
				range_end - range_base, 0);
		}
	}

KVM_HERE
	va = s_data;
	/*
	 * Writable until end.
	 */
	(void) as_fault(hat, &kas, va, (u_int)(e_data - va),
		F_SOFTLOCK, S_OTHER);
	(void) as_setprot(&kas, va, (u_int)(e_data - va),
		PROT_READ | PROT_WRITE | PROT_EXEC);
	va = (caddr_t)roundup((u_int)e_data, MMU_PAGESIZE);

KVM_HERE
	/*
	 * Validate the valloc'ed structures.
	 */
	(void) as_fault(hat, &kas, va, (u_int)(econtig - va),
		F_SOFTLOCK, S_OTHER);
	(void) as_setprot(&kas, va, (u_int)(econtig - va),
	    PROT_READ | PROT_WRITE | PROT_EXEC);
	va = (caddr_t)roundup((u_int)econtig, MMU_PAGESIZE);

	/*
	 * Invalidate the rest of the pmeg containing econtig.
	 */
	(void) as_fault(hat, &kas, va,
		roundup((u_int)econtig, PMGRPSIZE) - (u_int)econtig,
		F_SOFTLOCK, S_OTHER);
	(void) as_setprot(&kas, va,
		roundup((u_int)econtig, PMGRPSIZE) - (u_int)econtig, 0);
	va = (caddr_t)roundup((u_int)va, PMGRPSIZE);

KVM_HERE
	/*
	 * Run through the range from va to the last pmg of kas,
	 * invalidate everything on the virt_avail list,
	 * validate any portions that have mappings and
	 * are not on the virt_avail list.
	 * This includes:
	 *		SYSBASE - Syslimit. (kernelmap)
	 *		OBP large mapping area
	 *		the debugger, if present
	 *		E_SYSBASE - E_Syslimit (ekernelmap)
	 * 		the prom
	 */
	/* Invalidate loop */
	for (tv = va; tv < (caddr_t)-PMGRPSIZE; tv += PMGRPSIZE) {
		if (address_in_memlist(virt_avail, tv, PMGRPSIZE)) {
			mmu_pmginval(tv);
		} else {
			for (cur = virt_avail; cur; cur = cur->next) {
				range_base = MAX((u_int)cur->address,
					(u_int)tv);
				range_end = MIN((u_int)(cur->address +
					cur->size), (u_int)tv + PMGRPSIZE);
				if (range_end > range_base) {
					as_setprot(&kas, (caddr_t)range_base,
						range_end - range_base, 0);
				}
			}
		}
	}

	/* Validate loop */
	for (cur = virt_avail; cur->next; cur = cur->next) {
		if ((range_base = cur->address + cur->size) < (u_int)va)
			continue;
		if (range_base > (u_int)-PMGRPSIZE)
			break;
		range_size = cur->next->address - range_base;
		(void) as_fault(hat, &kas, (caddr_t)range_base, range_size,
			F_SOFTLOCK, S_OTHER);
		(void) as_setprot(&kas, (caddr_t)range_base, range_size,
			PROT_READ | PROT_WRITE | PROT_EXEC);
	}

	rw_enter(&kas.a_lock, RW_READER);

KVM_HERE
	/*
	 * Loop through the last segment and set page protections.
	 * We want to invalidate any other pages in last segment
	 * besides the u area, EEPROM_ADDR, COUNTER_ADDR, MEMERR_ADDR,
	 * AUXIO_ADDR, and INTREG_ADDR.
	 */
	for (va = (caddr_t)-PMGRPSIZE; va != (caddr_t)0; va += PAGESIZE) {
		if ((u_int)va != (u_int)EEPROM_ADDR &&
		    (u_int)va != (u_int)COUNTER_ADDR &&
		    (u_int)va != (u_int)MEMERR_ADDR &&
		    (u_int)va != (u_int)AUXIO_ADDR &&
		    (u_int)va != (u_int)VME_ADDR &&
		    (u_int)va != (u_int)ECCREG_ADDR &&
		    (u_int)va != (u_int)INTREG_ADDR) {
			mmu_setpte(va, mmu_pteinvalid);
			DPRINTF(("Invalidating pte for va %x\n", va));
		}
	}
	sunm_pmgreserve(&kas, (caddr_t)-PMGRPSIZE, PMGRPSIZE);

KVM_HERE
	/*
	 * Invalidate all the unlocked pmegs.
	 */
	sunm_pmginit();
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
	 * Allocate pmegs for DVMA space.
	 */
	sunm_reserve(&kas, (caddr_t)DVMA, (u_int)ctob(dvmasize));

	/*
	 * Reserve a pmeg for pagecopy, pagezero, pagesum mappings.
	 */
	sunm_reserve(&kas, (caddr_t)PPMAPBASE, PMGRPSIZE);

KVM_HERE

	/*
	 * Find the beginning page frames of the kernel data
	 * segment and the ending pageframe (-1) for bss.
	 */
	mmu_getpte((caddr_t)(roundup((u_int)e_text, DATA_ALIGN)), &pte);
	kpfn_dataseg = pte.pg_pfnum;
	mmu_getpte((caddr_t)e_data, &pte);
	kpfn_endbss = pte.pg_pfnum;

	/*
	 * Verify all memory pages that have mappings, have a mapping
	 * on the mapping list; take this out when we are sure things
	 * work.
	 */
	if (chkkas) {
		for (va = (caddr_t)SYSBASE; va < DVMA; va += PAGESIZE) {
			struct page *pp;

			mmu_getpte(va, &pte);
			if (pte.pg_v && (pte.pg_type == OBMEM)) {
				pp = page_numtopp_nolock(pte.pg_pfnum);
				if (pp && pp->p_mapping == NULL) {
					cmn_err(CE_PANIC,
		"page at va %x, no mapping on mapping list for pp %x \n",
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
	struct pte tpte;
	u_int va;
	int i = 0;
	u_int pages = 0;
	int lastpfnum = -1;

	for (va = (u_int)KERNELBASE; va < (u_int)econtig; va += PAGESIZE) {
		mmu_getpte((caddr_t)va, &tpte);
		if (tpte.pg_v) {
			if (lastpfnum == -1) {
				lastpfnum = tpte.pg_pfnum;
				kvtopdata.kvtopmap[i].kvpm_vaddr = va;
				kvtopdata.kvtopmap[i].kvpm_pfnum = lastpfnum;
				pages = 1;
			} else if (lastpfnum+1 == tpte.pg_pfnum) {
				lastpfnum = tpte.pg_pfnum;
				pages++;
			} else {
				kvtopdata.kvtopmap[i].kvpm_len = pages;
				lastpfnum = tpte.pg_pfnum;
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
	/*
	 * Pages allocated early from sysmap region that don't
	 * have page structures and need to be entered in to the
	 * kvtop array for libkvm.  The rule for memory pages is:
	 * it is either covered by a page structure or included in
	 * kvtopdata.
	 */
	for (va = (u_int)Sysbase; va < (u_int)startup_alloc_vaddr;
	    va += PAGESIZE) {
		mmu_getpte((caddr_t)va, &tpte);
		if (tpte.pg_v) {
			if (lastpfnum == -1) {
				lastpfnum = tpte.pg_pfnum;
				kvtopdata.kvtopmap[i].kvpm_vaddr = va;
				kvtopdata.kvtopmap[i].kvpm_pfnum = lastpfnum;
				pages = 1;
			} else if (lastpfnum+1 == tpte.pg_pfnum) {
				lastpfnum = tpte.pg_pfnum;
				pages++;
			} else {
				kvtopdata.kvtopmap[i].kvpm_len = pages;
				lastpfnum = tpte.pg_pfnum;
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

	mmu_getpte((caddr_t)&kvtopdata, &tpte);
	msgbuf.msg_map = (tpte.pg_pfnum << MMU_PAGESHIFT) |
		((u_int)(&kvtopdata) & MMU_PAGEOFFSET);
}
