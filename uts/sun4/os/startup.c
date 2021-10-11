/*
 * Copyright (c) 1990-1992, 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)startup.c	1.45	94/08/26 SMI"

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/thread.h>
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
#include <sys/session.h>
#include <sys/ucontext.h>
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
#include <sys/memlist.h>
#include <sys/openprom.h>
#include <sys/promif.h>
#include <sys/varargs.h>
#include <sys/modctl.h>
#include <sys/kvtopdata.h>
#include <sys/archsystm.h>
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
#ifdef	sun4
#include <sys/eccreg.h>
#endif	/* sun4 */
#include <sys/buserr.h>
#include <sys/enable.h>
#ifndef	sun4
#include <sys/auxio.h>
#endif	/* !sun4 */
#include <sys/trap.h>

#include <sys/iocache.h>

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
extern struct _kthread t0;

/*
 * Global Routines:
 *
 * mlsetup()
 */

/*
 * Global Data:
 */

struct cpu	cpu0;		/* first CPU's data */
struct _klwp	lwp0;
struct proc	p0;

/*
 * Declare these as initialized data so we can patch them.
 */
int physmem = 0;	/* memory size in pages, patch if you want less */
int kernprot = 1;	/* write protect kernel text */
int chkkas = 1;		/* verify dynamic kernel data as sane in kvm_init */

struct bootops *bootops = 0;	/* passed in from boot in %o2 */

u_int dfldsiz;		/* default data size limit */
u_int dflssiz;		/* default stack size limit */
u_int maxdsiz;		/* max data size limit */
u_int maxssiz;		/* max stack size limit */

caddr_t s_text;		/* start of kernel text segment */
caddr_t e_text;		/* end of kernel text segment */
caddr_t s_data;		/* start of kernel data segment */
caddr_t e_data;		/* end of kernel data segment */

#ifdef	VAC
#ifndef MPSAS		/* no cache in mpsas */
int use_vac = 1;	/* variable to patch to have the kernel use the cache */
#else
int use_vac = 0;
#endif
#else	!VAC
#define	use_vac 0
#endif	!VAC
u_int	vac_mask;		/* vac alignment consistency mask */
int	vac_hashwusrflush;	/* set to 1 if cache has HW user flush */

/*
 * For implementations that have fewer than the default number of page
 * frame bits (19), startup() patch back this variable as appropriate.
 */

u_long pfnumbits = PG_PFNUM & ~PG_TYPE;

int use_ioc = 1;	/* patch to 0 to have the kernel not use IOC */

#ifdef	BCOPY_BUF
int bcopy_res = -1;	/* block copy buffer reserved (in use) */
int use_bcopy = 1;	/* variable to patch to have kernel use bcopy buffer */
int bcopy_cnt = 0;	/* number of bcopys that used the buffer */
int bzero_cnt = 0;	/* number of bzeros that used the buffer */
#endif	BCOPY_BUF

u_int shm_alignment;		/* VAC address consistency modulus */
struct memlist *phys_install;	/* Total installed physical memory */
struct memlist *phys_avail;	/* Available (unreserved) physical memory */
struct memlist *virt_avail;	/* Available (unmapped?) virtual memory */
u_int debugger_start = 0;	/* where debgger starts, if present */

/*
 * VM data structures
 */
int	page_hashsz;		/* Size of page hash table (power of two) */
struct	page *pp_base;		/* Base of system page struct array */
u_int	pp_sz;
u_int	pp_giveback;
struct	page **page_hash;	/* Page hash table */
struct	seg *segkmap;		/* Kernel generic mapping segment */
struct	seg *segkp;		/* Segment for pageable kernel virt. memory */
struct	seg ktextseg;		/* Segment used for kernel executable image */
struct	seg kvalloc;		/* Segment used for "valloc" mapping */
struct	seg kdebugseg;		/* Segment used for mapping the debugger */
struct	seg kmonseg;		/* Segment used for mapping the prom */
struct	seg kdevregseg;		/* Seg used for mapping random device regs */
struct	memseg *memseg_base;	/* Used to translate a va to page */
u_int	memseg_sz;
struct	vnode unused_pages_vp;
struct	vnode prom_pages_vp;

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
caddr_t startup_alloc_vaddr = (caddr_t)SYSBASE + MMU_PAGESIZE;
caddr_t startup_alloc_size;
u_int	startup_alloc_chunk_size = 20 * MMU_PAGESIZE;
u_int	memlist_sz;
u_int	kernelmap_sz;
u_int	ekernelmap_sz;
u_int	hwpmgs_sz;
u_int	ctxs_sz;

#ifdef MMU_3LEVEL
u_int	smgrps_sz;
u_int	sments_sz;
#endif MMU_3LEVEL

/*
 * Configuration parameters set at boot time.
 */

int do_pg_coloring = 0;		/* will be set for non-VAC Sun-4/110 only */
int use_page_coloring = 1;	/* patch to 0 to disable above */

/*
 * SUN4POC: this should be somewhere else and retrieved via a proplist
 * value a la fiximp_obp().
 */
static struct cpuinfo {
	u_int cpui_mmutype : 1; 	/* mmu type, 0=2 level, 1=3 level */
	u_int cpui_vac : 2;		/* has virtually addressed cache */
	u_int cpui_ioctype : 2;		/* I/O cache type, 0 = none */
	u_int cpui_iom : 1;		/* has I/O MMU */
	u_int cpui_clocktype : 2;	/* clock type */
	u_int cpui_bcopy_buf : 1;	/* has bcopy buffer */
	u_int cpui_sscrub : 1;		/* requires software ECC scrub */
	u_int cpui_linesize : 8;	/* cache line size (if any) */
	u_short cpui_nlines;		/* number of cache lines */
	u_short cpui_dvmasize;		/* size of DMA area (in MMU pages) */
	u_short cpui_cpudelay;		/* cpu delay factor (cache on) */
	u_short cpui_ncdelay;		/* cpu delay factor (cache off) */
	u_short cpui_nctx;		/* number of contexts */
	u_int cpui_nsme;		/* number of segment map entries */
	u_int cpui_npme;		/* number of page map entries */
	u_int cpui_mips_on;		/* approx mips with cache on */
	u_int cpui_mips_off;		/* approx mips with cache off */
	u_char cpui_freq;		/* cpu clock freq rounded to mhz */
} cpuinfo[] = {
#define	VW	VAC_WRITEBACK
#define	VX	VAC_WRITETHRU
#define	VN	NO_VAC
/*
 * XXX	Once the unused and duplicate stuff is eliminated, each
 *	table entry will fit onto a single line.
 *
 * mmu vac  ioc    iom    clk bcp scr lsz   nln  dsz
 * cdly ndly nctx  nsme  npme mion mioff freq
 */
{   0,  VW,   0,    00,    00,  0,  0, 16, 8192, 126,
    7,   2,  16, 65536, 16384, 10,  1, 17
},							/* 4_260 */
{   0,  VN,   0,    00,    00,  0,  0,  0,    0, 126,
    7,   7,   8, 32768,  8192,  8,  8, 15
},							/* 4_110 */
{   0,  VX,   0,    00,    01,  0,  0, 16, 8192, 126,
    11,  4,  16, 65536,  8192, 16,  1, 25
},							/* 4_330 */
{   1,  VW,   1,    00,    01,  1,  0, 32, 4096, 126,
    16,  3,  64, 16384, 32768, 20,  1, 33
}							/* 4_470 */
};
static struct cpuinfo *cpuinfop;	/* misc machine info */

u_int nctxs;			/* no. of implemented contexts */
u_int npmgrps;			/* number of pmgrps in page map */
u_int nsmgrps;			/* number of smgrps in segment map */

int Cpudelay;			/* delay loop count/usec */

u_int segmask;			/* mask for segment numbers */
caddr_t hole_start;		/* addr of start of MMU "hole" */
caddr_t hole_end;		/* addr of end of MMU "hole" */
int hole_shift;			/* "hole" check shift to get high addr bits */
caddr_t econtig;		/* End of first block of contiguous kernel */

int vac;			/* vac write policy */
int vac_size;			/* cache size in bytes */
int vac_linesize;		/* size of a cache line */
int vac_hwflush;		/* cache has HW flush */
int vac_nlines;			/* number of lines in the cache */
int vac_pglines;		/* number of cache lines in a page */

struct map *dvmamap;		/* map to manage usable dvma space */

extern int maxusers;

/*
 * Dynamically allocated MMU structures
 */
extern struct	hatops	*sys_hatops;
extern struct	ctx	*ctxs,	*ctxsNCTXS;
extern struct	pmgrp	*pmgrps, *pmgrpsNPMGRPS;
extern struct	hwpmg	*hwpmgs, *hwpmgsNHWPMGS;
extern struct 	pmgrp	**pmghash, **pmghashNPMGHASH;

#ifdef	MMU_3LEVEL
extern struct	smgrp	*smgrps, *smgrpsNSMGRPS;
extern struct	sment	*sments, *smentsNSMENTS;
#endif	MMU_3LEVEL

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
static void init_cpu_info(struct cpu *);
static void ioc_init(void);
static void kphysm_init(struct page *, struct memseg *, u_int, u_int);
static void kvm_init();
static void setup_kvpm();

static void kern_splr_preprom(void);
static void kern_splx_postprom(void);
static void kern_preprom(void);
static void kern_postprom(void);

/*
 * Debugging Stuff:
 */
#ifdef	DEBUGGING_MEM

/*
 * XXX	Most of this stuff should go..
 */
extern int	npmgrpssw;
extern int	npmghash;

static int machdep_dset; /* patch to 1 for debug, 2 for line numbers too */

static void
print_state(int lineno)
{
	if (machdep_dset > 1)
		printf("machdep.c line %d: ", lineno);

	printf("hats %x nhats %x hatsNHATS %x\n", hats, nhats, hatsNHATS);
	printf("ctxs %x nctxs %x ctxsNCTXS %x\n", ctxs, nctxs, ctxsNCTXS);
	printf("pmgrps %x npmgrpssw %x pmgrpsNPMGRPS %x\n", pmgrps,
	    npmgrpssw, pmgrpsNPMGRPS);
	printf("hwpmgs %x npmgrps %x hwpmgsNHWPMGS %x\n", hwpmgs,
	    npmgrps, hwpmgsNHWPMGS);
	printf("pmghash %x npmghash %x pmghashNPMGHASH %x\n", pmghash,
	    npmghash, pmghashNPMGHASH);
	printf("*pmghash %x\n", *(int *)pmghash);

	if (mmu_3level)
		printf("3-level mmu\n");

	printf("kernelmap %x dvmamap %x ncache %x\n", kernelmap, dvmamap,
	    ncache);

	printf("memseg %x\n", memseg);
	printf("page_hashsz %x page_hash %x\n", page_hashsz, page_hash);
}

static void
print_mem_list(struct memlist *listp)
{
	struct memlist *list;

	if (!listp)
		return;

	list = listp;
	do {
		prom_printf("vaddr %x size %x next %x prev %x nextavail %x\n",
		    (u_int)list->address, (u_int)list->size, list->next,
		    list->prev, (u_int)(list->address + list->size));
		list = list->next;
	} while (list && list != listp);
}


static void
dump_mmu_entry(caddr_t addr)
{
	struct pte	pte;
	u_int		map_getsgmap();

	printf("vaddr = %x pmgrp = %x\n", addr, map_getsgmap(addr));

	mmu_getpte(addr, &pte);
	printf("v %x prot %x nc %x type %x r %x m %x pfnum %x pte %x\n",
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
			printf("machdep.c line %d: ", __LINE__);	\
		printf args;		\
	}
#define	PRINT_MEM_LIST(arg)	if (machdep_dset) print_mem_list((arg))
#define	DUMP_MMU_RANGE(a1, a2)	if (machdep_dset) dump_mmu_range((a1), (a2))
#define	DUMP_MMU_ENTRY(addr)	if (machdep_dset) dump_mmu_entry((addr))
#define	PRINT_STATE()		if (machdep_dset) print_state(__LINE__)

#else   /* DEBUGGING_MEM */

#define	DPRINTF(args)
#define	PRINT_MEM_LIST(arg)
#define	DUMP_MMU_RANGE(a1, a2)
#define	DUMP_MMU_ENTRY(addr)
#define	PRINT_STATE()

#endif  /* DEBUGGING_MEM */

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
 *		  Physical memory layout
 *
 *		|-----------------------|
 *	page 1	|	 msgbuf		|
 *		|-----------------------|
 *	page 0	| used for onboard ie	|
 *		|_______________________|
 *
 *
 *		  Virtual memory layout.
 *
 *	The Intel 82586 ethernet chip needs to access 0xFFFFFFF4
 *	for initialization so we burn a page at the top just for it
 *	(see if_ie.c)
 *		 _______________________
 *		|	ie init page	|
 * 0xFFFFE000  -|-----------------------|
 *		|  ethernet descriptors |
 * 0xFFFFC000  -|-----------------------|
 *		|    Kernel Virtual	|
 *		| mappings for DVMA and	|
 *		| noncached iopb memory	|
 * 0xFFF00000  -|-----------------------|- DVMA
 *		|	monitor		|
 * 0xFFD00000  -|-----------------------|- SUNMON_START
 *		|	segkmap		|	SEGMAPSIZE	(4 M)
 * 0xFF900000  -|-----------------------|- E_Syslimit
 *		|	E_Sysmap	|			(9 M)
 *		| primary kmem_alloc	|
 *		| pool area (kernelmap) |
 * 0xFF000000  -|-----------------------|- E_Sysbase
 *		|	unused		|			(16 M)
 * 0xFE000000  -|-----------------------|- Syslimit
 *		|    	Sysmap 		|			(32 M)
 *		| secondary kmem_alloc	|
 *		| pool area (kernelmap)	|
 * 0xFC000000  -|-----------------------|- Sysbase
 *		|    exec args area	|			(1 M)
 * 0xFBF00000  -|-----------------------|- ARGSBASE
 *		|	SEGTEMP2	|			(256 K)
 *		|-----------------------|
 *		|	SEGTEMP		|			(256 K)
 * 0xFBE80000  -|-----------------------|-
 *		|	unused		|
 * 0xFBE52000  -|-----------------------|
 *		|	iocache tags	|
 * 0xFBE51000  -|-----------------------|
 *		|	iocache data	|
 * 0xFBE50000  -|-----------------------|
 *		|	iocache flush	|
 * 0xFBE4C000  -|-----------------------|
 *		|    counter - timer 	|
 * 0xFBE4A000  -|-----------------------|
 *		|    interrupt reg	|
 * 0xFBE48000  -|-----------------------|
 *		|   memory error reg	|
 * 0xFBE46000  -|-----------------------|
 *		|    memory ecc reg	|
 * 0xFBE44000  -|-----------------------|
 *		|	eeprom		|
 * 0xFBE42000  -|-----------------------|
 *		|	 clock		|			(256 K)
 * 0xFBE40000  -|-----------------------|- MDEVBASE
 *		| quick page map region |			(256 K)
 * 0xFBE00000	|-----------------------|- PPMAPBASE
 *		|	debugger	|			(1 M)
 * 0xFBD00000  -|-----------------------|- DEBUGSTART
 *		|    kobj workspace	|
 * 0xF1000000  -|-----------------------|
 *		|	 unused		|
 *		|-----------------------|
 *		|	 segkp		|
 *		|-----------------------|- econtig
 *		|  page structures	|
 *		|-----------------------|- end
 *		|	kernel		|
 *		|-----------------------|
 *		|   trap table (4k)	|
 * 0xF0004000  -|-----------------------|- start
 *		|	 msgbuf		|
 * 0xF0002000  -|-----------------------|- msgbuf
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
 */

/*
 * Setup routine called right before main(). Interposing this function
 * before main() allows us to call it in a machine-independent fashion.
 */

void
mlsetup(struct regs *rp)
{
	register caddr_t addr;
	extern struct classfuncs sys_classfuncs;
	extern int clock_type;
	extern pri_t maxclsyspri;

	/*
	 * initialize t0
	 */
	t0.t_stk = (caddr_t)rp - MINFRAME;
	t0.t_pri = maxclsyspri - 3;
	t0.t_schedflag = TS_LOAD | TS_DONT_SWAP;
	t0.t_procp = &p0;
	t0.t_lwp = &lwp0;
	t0.t_forw = &t0;
	t0.t_back = &t0;
	t0.t_next = &t0;
	t0.t_prev = &t0;
	t0.t_cpu = &cpu0;
	t0.t_disp_cpu = &cpu0;
	t0.t_bind_cpu = PBIND_NONE;
	t0.t_clfuncs = &sys_classfuncs.thread;
	THREAD_ONPROC(&t0, &cpu0);

	lwp0.lwp_thread = &t0;
	lwp0.lwp_pcb.pcb_uwm = 0;
	lwp0.lwp_regs = rp;
	lwp0.lwp_procp = &p0;
	t0.t_tid = p0.p_lwpcnt = p0.p_lwprcnt = p0.p_lwptotal = 1;

	p0.p_exec = NULL;
	p0.p_stat = SRUN;
	p0.p_flag = SSYS;
	p0.p_tlist = &t0;
	p0.p_stksize = 0x2000;	/* use an absolute value not related to NBPG */
	p0.p_as = &kas;
	sigorset(&p0.p_ignore, &ignoredefault);

	cpu0.cpu_thread = &t0;
	cpu0.cpu_dispthread = &t0;
	cpu0.cpu_idle_thread = &t0;
	cpu0.cpu_flags = CPU_READY | CPU_RUNNING | CPU_EXISTS | CPU_ENABLE;
#ifdef	TRACE
	cpu0.cpu_trace.event_map = null_event_map;
#endif	/* TRACE */

	/*
	 * Initialize list of available and active CPUs.
	 */
	cpu_list_init(&cpu0);

	prom_init("kernel");	/* Set romp to constant value on sun4 */
	prom_set_preprom(kern_splr_preprom);
	prom_set_postprom(kern_splx_postprom);

	setcputype();

	/*
	 * Set some system parameters now based upon machine type.
	 *
	 * XXX	This should probably be done a little bit more
	 *	intelligently.  And do we really need all the info?
	 */
	cpuinfop = &cpuinfo[(cputype & CPU_MACH) - 1];

	clock_type = cpuinfop->cpui_clocktype;
	dvmasize = cpuinfop->cpui_dvmasize;
	mmu_3level = cpuinfop->cpui_mmutype;

	nctxs = cpuinfop->cpui_nctx;
	npmgrps = cpuinfop->cpui_npme / NPMENTPERPMGRP;

	if (cpuinfop->cpui_nlines > 0) {
		vac_size = cpuinfop->cpui_linesize * cpuinfop->cpui_nlines;
		vac = cpuinfop->cpui_vac;
		vac_linesize = cpuinfop->cpui_linesize;
	} else {
		vac_size = 0;
		vac = NO_VAC;
		vac_linesize = 0;

		if (use_page_coloring)
			do_pg_coloring = 1;
	}

	segmask = PMGRP_INVALID;

	if (mmu_3level) {
		/* no hole in a 3 level mmu, just set it halfway */
		nsmgrps = cpuinfop->cpui_nsme / NSMENTPERSMGRP;
		hole_shift = 31;
		hole_start = (caddr_t)(1 << hole_shift);
	} else {
		register u_int npmgrpperctx;
		/* compute hole size in 2 level MMU */
		npmgrpperctx = cpuinfop->cpui_nsme / nctxs;
		hole_start = (caddr_t)((npmgrpperctx / 2) * PMGRPSIZE);
		hole_shift = ffs((long)hole_start) - 1;
	}
	hole_end = (caddr_t)(-(long)hole_start);

#ifdef	VA_HOLE
	if (!mmu_3level)
		ASSERT(hole_shift < 31);
#endif	/* VA_HOLE */

	if (cpuinfop->cpui_vac) {
		vac_nlines = cpuinfop->cpui_nlines;
		vac_pglines = PAGESIZE / vac_linesize;
		if (mmu_3level)
			vac_hashwusrflush = 1;
	}

	/*
	 * Map in devices.
	 *
	 * For 3 level MMU we use the middle smeg in the segment map
	 * We always use the middle pmeg in the page map.
	 *
	 * Since kadb lies in the same region as MDEVBASE we check
	 * the region map entry first and only set it if it doesn't
	 * already point to a valid segment map group.
	 */
	if (mmu_3level)
		if (map_getrgnmap((caddr_t)MDEVBASE) == SMGRP_INVALID)
			map_setrgnmap((caddr_t)MDEVBASE, nsmgrps / 2);

	map_setsgmap((caddr_t)MDEVBASE, npmgrps / 2);
	/* init pmeg */
	for (addr = (caddr_t)MDEVBASE; addr < (caddr_t)MDEVBASE + PMGRPSIZE;
	    addr += MMU_PAGESIZE)
		map_setpgmap(addr, 0);

	map_setpgmap((caddr_t)EEPROM_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_EEPROM_ADDR));

	map_setpgmap((caddr_t)CLOCK0_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_CLOCK_ADDR));

	map_setpgmap((caddr_t)MEMERR_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_MEMERR_ADDR));

	map_setpgmap((caddr_t)INTREG_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_INTREG_ADDR));

	map_setpgmap((caddr_t)ECCREG_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_ECCREG0_ADDR));

	if (cputype == CPU_SUN4_330 || cputype == CPU_SUN4_470)
	    map_setpgmap((caddr_t)COUNTER_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_COUNTER_ADDR));

	if (cputype == CPU_SUN4_470)
	    /* remap eccreg for Sunray */
	    map_setpgmap((caddr_t)ECCREG_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_ECCREG1_ADDR));

	/*
	 * Need to map in the msgbuf.  We'll use the region, and segment
	 * that is used for KERNELBASE.  The grody thing here is that
	 * we'll fix the msgbuf to physical page 1.  I hate
	 * wiring this stuff down, but we must preserve the msgbuf across
	 * reboots.
	 */
	map_setpgmap((caddr_t)&msgbuf, PG_V | PG_KW | PGT_OBMEM | 0x1);

	/*
	 * Save the kernel's level 14 interrupt vector code and install
	 * the monitor's. This lets the monitor run the console until we
	 * take it over.
	 */
	kclock14_vec = scb.interrupts[14 - 1];
	start_mon_clock();
	/*
	 * Death for printfs until later..
	 */
	(void) splzs();			/* allow hi clock ints but not zs */
	bootflags();

#if !defined(SAS) && !defined(MPSAS)
	/*
	 * If the boot flags say that kadb is there,
	 * test and see if it really is by peeking at DVEC.
	 * If is isn't, we turn off the RB_DEBUG flag else
	 * we call the debugger scbsync() routine.
	 * The kdbx debugger agent does the dvec and scb sync stuff,
	 * and sets RB_DEBUG for debug_enter() later on.
	 */
	if ((boothowto & RB_DEBUG) != 0) {
		if (dvec == NULL || ddi_peeks((dev_info_t *)0,
		    (short *)dvec, (short *)0) != DDI_SUCCESS)
			boothowto &= ~RB_DEBUG;
		else {
			extern trapvec kadb_tcode, trap_kadb_tcode;

			(*dvec->dv_scbsync)();

			/*
			 * Now steal back the traps.
			 * We "know" that kadb steals trap 125 and 126,
			 * and that it uses the same trap code for both.
			 */
			kadb_tcode = scb.user_trap[ST_KADB_TRAP];
			scb.user_trap[ST_KADB_TRAP] = trap_kadb_tcode;
			scb.user_trap[ST_KADB_BREAKPOINT] = trap_kadb_tcode;
		}
	}
#endif
}

/*
 * Machine-dependent startup code
 */
void
startup()
{
	register int unixsize;
	register unsigned i, j;
	u_int npages;
	struct segmap_crargs a;
	int  memblocks;
	caddr_t memspace;
	caddr_t tmp_mapaddr;
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

	/*
	 * Initialize the turnstile allocation mechanism.
	 */
	tstile_init();
	kncinit();

	if (cputype == CPU_SUN4_110) {
		/*
		 * 4/110s have only 15 bits worth of valid page frame number.
		 */
		pfnumbits &= 0x7fff;
	}

	ppmapinit();

#ifdef	VAC
	if (cpuinfop->cpui_vac) {
		extern int nopagereclaim;
		int cache_already;

		/*
		 * The Sun-4/260's have a hardware bug which causes
		 * non-ready floating point numbers to not be waited
		 * for before they are stored to non-cached pages.
		 * The most common way user's get non-cached pages is
		 * when they "reclaim" a page which already mapped
		 * into DVMA space.  As a kludge effort to reduce the
		 * likelyhood of this happening, we turn off page
		 * reclaims on Sun-4/260's.  Yuck...
		 */
		if (cputype == CPU_SUN4_260)
			nopagereclaim = 1;

		cache_already = get_enablereg() & ENA_CACHE;
		Cpudelay = cpuinfop->cpui_cpudelay;
		if (use_vac) {
			/*
			 * If /boot has brought us up with the cache
			 * on, don't perform the vac_init().
			 */
			if (!cache_already)
				vac_init();
			/*
			 * All sun4 VACs are coherent wrt I/O
			 */
			vac = cpuinfop->cpui_vac | VAC_IOCOHERENT;
			shm_alignment = vac_size;
			vac_mask = MMU_PAGEMASK & (shm_alignment - 1);
			if (!cache_already)
				on_enablereg(ENA_CACHE);
		} else  {
			/*
			 * XXX	If we really want to support this we
			 *	should flush the cache and turn if off.
			 */
			if (cache_already) {
				printf("CPU CACHE ALREADY ON!\n");
				vac = cpuinfop->cpui_vac | VAC_IOCOHERENT;
				shm_alignment = vac_size;
			} else {
				printf("CPU CACHE IS OFF!\n");
				vac = 0;
				shm_alignment = PAGESIZE;
				Cpudelay = cpuinfop->cpui_ncdelay;
			}
		}
	}
#else
	Cpudelay = cpuinfop->cpui_ncdelay;
#endif	VAC

#ifdef	IOC
	ioc = cpuinfop->cpui_ioctype;
#endif	IOC

	if (ioc) {
		ioc_init();
		if (use_ioc)
			on_enablereg(ENA_IOCACHE);
		else
			printf("IO CACHE IS OFF!\n");
	}
	if (!(ioc && use_ioc)) {
		ioc = 0;
	}

#ifdef	BCOPY_BUF
	bcopy_buf = cpuinfo->cpui_bcopy_buf;
	if (bcopy_buf) {
		if (use_bcopy) {
			bcopy_res = 0;		/* allow use of hardware */
		} else {
			printf("bcopy buffer disabled\n");
			bcopy_res = -1;		/* reserve now, hold forever */
		}
	}
#endif	/* BCOPY_BUF */

	if (maxdsiz == 0)
		maxdsiz = (int)hole_start - USRTEXT;
	if (maxssiz == 0)
		maxssiz = (int)hole_start - KERNELSIZE;
	if (dfldsiz == 0)
		dfldsiz = maxdsiz;

	/*
	 * The default stack size of 8M allows an optimization of mmu mapping
	 * resources so that in normal use a single mmu region map entry (smeg)
	 * can be used to map both the stack and shared libraries.
	 */
	if (dflssiz == 0)
		dflssiz = (8*1024*1024);
#ifndef	sun4
	/*
	 * initialize handling of memory errors
	 */
	memerr_init();
#endif

	/*
	 * allow interrupts now,
	 * after memory error handling has been initialized (sun4c only)
	 */

	*INTREG |= IR_ENA_INT;

#if defined(SAS) || defined(MPSAS)
	/* SAS has contigouous memory */
	physmem = btop(_availmem);
	physmax = physmem - 1;
#else
	/*
	 * v_vector_cmd is the handler for the monitor vector command .
	 * We install v_handler() there for Unix.
	 */
	(void) prom_sethandler(v_handler, 0);

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
	 * some of these locks depend on page_hashsz.
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
	if (mmu_3level) {
		smgrps_sz = sizeof (struct smgrp) * NSMGRPS;
		sments_sz = sizeof (struct sment) * NSMGRPS * NSMENTPERSMGRP;
	}

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
	if (mmu_3level)
		memspace_sz += (smgrps_sz + sments_sz);
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

	if (mmu_3level) {
		smgrps = (struct smgrp *)memspace;
		smgrpsNSMGRPS = smgrps + NSMGRPS;
		sments = (struct sment *)smgrpsNSMGRPS;
		smentsNSMENTS = sments + NSMGRPS * NSMENTPERSMGRP;
		page_hash = (struct page **)smentsNSMENTS;
	} else
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
	 * wiring down the msgbuf to physical page(s) 0x2000..0x4000, and yes,
	 * this can probably be made more generic, maybe with /boot, but our
	 * reconcile window beckons.
	 */

	/* take the most current snapshot we can by calling mem-update */
	if (BOP_GETPROPLEN(bootops, "memory-update") == 0)
		BOP_GETPROP(bootops, "memory-update", NULL);

	{
		struct memlist *hilist = NULL;
		u_longlong_t lophys, hiphys;
		struct memlist *this;

		this = (struct memlist *)bootops->boot_mem->physavail;
		/*
		 * This is horrible, isn't it.  Well, the PROM doesn't
		 * seem to realize that it's gone and used some physical
		 * pages in its own magic address range.  So we have to
		 * figure out how many it stole.  Yeah, so we're relying
		 * on the fact that the prom allocated pages from the
		 * top down. Sorry.
		 */
		for (hiphys = 0; this; this = this->next) {
			if ((this->address + this->size) > hiphys) {
				hiphys = this->address + this->size;
				hilist = this;
			}
		}

		if (hilist != NULL) {
			lophys = hilist->address + hilist->size;
			for (va = (caddr_t)SUNMON_START;
			    va < (caddr_t)SUNMON_END;
			    va += MMU_PAGESIZE) {
				struct pte pte, *ptep = &pte;

				mmu_getpte(va, &pte);
				if (ptep->pg_v &&
				    ptep->pg_type == OBMEM) {
					u_int physaddr;

					physaddr = ptep->pg_pfnum *
						MMU_PAGESIZE;
					if (physaddr < lophys)
						lophys = physaddr;
				}
			}

#ifdef DEBUG
			if (boothowto & RB_VERBOSE && chkkas)
				prom_printf(
				"Notice: PROM using %d additional pages\n",
				    ((hilist->address + hilist->size) -
					lophys) / MMU_PAGESIZE);
#endif	/* DEBUG */

			hilist->size = lophys - hilist->address;
		}
	}

	phys_avail = memlist;
	if ((cputype & CPU_ARCH) == SUN4_ARCH && cputype != CPU_SUN4_330) {
		if (!copy_physavail(bootops->boot_mem->physavail, &memlist,
		    0, 0x4000))
			halt("cannot find phys pages for IE SCB and msgbuf");
	} else {
		if (!copy_physavail(bootops->boot_mem->physavail, &memlist,
		    0x2000, 0x2000))
			halt("cannot find phys pages for msgbuf");
	}

#if defined(SAS) || defined(MPSAS)
	/* for SAS, memory is contiguous */
	page_init(&pp[0], npages, pp_base, memseg_base);

	first_page = memsegs->pages_base;
	if (first_page < mapaddr + btoc(econtig - e_data))
		first_page = mapaddr + btoc(econtig - e_data);
	memialloc(first_page, mapaddr + btoc(econtig - e_data),
		memsegs->pages_end);
#else   SAS
	/*
	 * Initialize the page structures from the memory list
	 * that we just constructed out of the prom (less any
	 * physical pages we deducted).
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
	 * Read system file, (to set maxusers, nprmgrps, physmem....)
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
	 * maxmem is the amount of physical memory we're playing with.
	 */
	maxmem = physmem;

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

	if (modload("misc", "swapgeneric") == -1)
		halt("Can't load swapgeneric");

	dispinit(NULL);

	setup_ddi();

	/*
	 * Lets take this opportunity to load the the root device.
	 */
	if (loadrootmodules() != 0)
		debug_enter("Can't load the root filesystem");

	/*
	 * Call back into boot and inform it we're done with I/O
	 */
	BOP_QUIESCE_IO(bootops);

	/*
	 * Copy the remaining memory lists into kernel space.
	 *
	 * Physical installed first
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

	/*
	 * We're done with boot.  Just after this point in time, boot
	 * gets unmapped, so we can no longer rely on its services.
	 * Zero the bootops to indicate this fact.
	 */
	bootops = (struct bootops *)NULL;
	BOOTOPS_GONE();

	/*
	 * flush any stale data left in the cache from booting.
	 */
	vac_usrflush();

	/*
	 * Initialize VM system, and map kernel address space.
	 */
	kvm_init();

	/*
	 * Setup a map for translating kernel vitual addresses;
	 * used by dump and libkvm.
	 */
	setup_kvpm();

#if 0
	/*
	 * allocate the remaining kernel data structures.
	 * copy tempory kernelmap into an appriately sized new one
	 * and free the old one.
	 * XXX - RAZ - fix me later
	 */
	if ((kernelmap = (struct map *) kmem_zalloc(
	    sizeof (struct map) * (4 * v.v_proc), 0)) == NULL)
		panic("Cannot allocate system memory");
#endif


	/*
	 * if defined(SUN4_330) || defined(SUN4_470)...
	 * fix for bug in SUNRAY IU version 1, bit 0 of ASI 1 cycle late
	 */
	if ((getpsr()>>24) == 0x10)
		hat_chgprot(&kas, (caddr_t)&scb, PAGESIZE, PROT_USER);
	/* Sun-4/330  dislikes cache hits on 1st cycle of a trap */
	if (cputype == CPU_SUN4_330)
		vac_dontcache((caddr_t)&scb);

	/*
	 * Determine if anything on the VME bus lives in the range of
	 * VME addresses (low 1 Mb) that correspond with system DVMA.
	 * We go through both the 16 bit and 32 bit device types.
	 */
#if !defined(SAS) && !defined(MPSAS)
	disable_dvma();
	j = rmalloc(kernelmap, 1);
	tmp_mapaddr = (caddr_t)kmxtob(j);

#define	TESTVAL	0xA55A		/* memory test value */
	for (i = 0; i < dvmasize; i++) {
		segkmem_mapin(&kvseg, tmp_mapaddr, MMU_PAGESIZE,
		    PROT_READ | PROT_WRITE, (u_int)(i | PGT_VME_D16), 0);
		if (ddi_pokes((dev_info_t *)0,
		    (short *)tmp_mapaddr, TESTVAL) == DDI_SUCCESS)
			break;
		segkmem_mapin(&kvseg, tmp_mapaddr, MMU_PAGESIZE,
		    PROT_READ | PROT_WRITE, (u_int)(i | PGT_VME_D32), 0);
		if (ddi_pokes((dev_info_t *)0,
		    (short *)tmp_mapaddr, TESTVAL) == DDI_SUCCESS)
			break;
	}
#undef	TESTVAL
	if (i < dvmasize) {
		printf("CAN'T HAVE PERIPHERALS IN RANGE 0 - %dKB\n",
		    mmu_ptob(dvmasize) / 1024);
		halt("dvma collision");
	}

	rmfree(kernelmap, (size_t)1, j);
	enable_dvma();
#endif

	/*
	 * Allocate a vm slot for the dev mem driver.
	 * XXX - this should be done differently, see pagecopy.
	 */
	i = rmalloc(kernelmap, 1);
	mm_map = (caddr_t)kmxtob(i);

	/*
	 * When printing memory, show the total as physmem less
	 * that stolen by a debugger.
	 */
#if defined(SAS) || defined(MPSAS)
	cmn_err(CE_CONT, "?mem = %dK (0x%x)\n", _availmem / 1024, _availmem);
#else
	cmn_err(CE_CONT, "?mem = %dK (0x%x)\n",
	    physinstalled << (PAGESHIFT - 10),
	    ptob(physinstalled));
#endif SAS

	/*
	 * unixsize doesn't include any loaded modules,
	 * it is everything that is not backed by page structures.
	 */
	unixsize = btoc(econtig - KERNELBASE);

	/*
	 * This might be too large, but that's too bad.
	 */
	if ((dvmamap = (struct map *) kmem_zalloc(
		sizeof (struct map) * dvmasize, 0)) == NULL)
			panic("Cannot allocate dvmamap");
	/*
	 * The dvmamap manages the space DVMA[0..mmu_ptob(dvmasize)].
	 * We manage it in the range [ 1..dvmasize + 1 ]. The users
	 * of dvmamap can't use it directly- instead they call
	 * getdvmapages() to return a base virtual address for the
	 * requested number of pages.
	 */
	mapinit(dvmamap, (long)dvmasize, (u_long)1,
	    "DVMA map space", dvmasize);

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
			pp = page_create(&unused_pages_vp, off,
				MMU_PAGESIZE, PG_WAIT | PG_EXCL);
			if (pp == NULL)
				cmn_err(CE_PANIC, "limited physmem too much!");
			page_io_unlock(pp);
			availrmem--;
			off += MMU_PAGESIZE;
		}
	}

	cmn_err(CE_CONT, "?avail mem = %d\n", ctob(freemem));

	if (segkp_limit == 0)
		segkp_limit = (u_int)PPMAPBASE - DEBUGSIZE;

	/*
	 * Initialize the u-area segment type.  We position it
	 * after the configured tables and buffers (whose end
	 * is given by econtig) and before MDEVBASE.
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
	configure();	/* set up devices */

	init_cpu_info(CPU);
	init_intr_threads();
}

void
post_startup()
{
	/*
	 * initialize handling of memory errors
	 * IMPORTANT: memerr_init() initializes ecc regs and turns on ecc
	 * reporting. If an ecc error recieved at this time, a timeout
	 * will be set up. Timeout expects callouts be initialized.
	 * Hence, memerr_init should be done AFTER callouts are initialized.
	 * Or else, an ecc error will cause a panic 'timeout table overflow'.
	 */
	memerr_init();

	/*
	 * Forceload some drivers here because interrupts are on
	 * and the handler needs to be there. We know that the
	 * prom isn't resetting the device's interrupts.
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
}

/*
 * Nothing to do.
 */
void
start_other_cpus(void)
{}

/*
 * Init CPU info - get CPU type info for processor_info system call.
 */
static void
init_cpu_info(struct cpu *cp)
{
	register processor_info_t *pi = &cp->cpu_type_info;

	pi->pi_clock = cpuinfop->cpui_freq;

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
 * The first set is installed in mlsetup(), the second set
 * is installed at the end of post_startup().
 */

static int saved_spl;

static void
kern_splr_preprom(void)
{
	saved_spl = spl7();
}

static void
kern_splx_postprom(void)
{
	(void) splx(saved_spl);
}

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

#ifdef	IOC
/*
 * ioc_init()
 * Initialize the iocache tags to be invalid and zero data.
 * Extra ram in the iocache is used as fast memory to hold
 * the ethernet descriptors so that cache misses (which tend
 * to cause over/underruns) never occur when the ethernet is
 * reading or writing control informaion.  Map second half of
 * i/o cache data ram into a fixed address (0xFFFF8000).  The
 * tags for the descriptors should be marked valid, modified,
 * and the tag address must match the virtual address where it
 * is mapped (so the ethernet descriptor cache always hits)
 */
static void
ioc_init(void)
{
	register u_long  *p;

	/*
	 * map in tags and initialize them
	 * we use the flush virtual address temporarily
	 * since we normally don't touch the tags or data
	 * once the i/o cache is initialized
	 */
	map_setpgmap((caddr_t)IOC_FLUSH_ADDR,
	    PG_V | PG_KW | PGT_OBIO | btop(OBIO_IOCDATA_ADDR));

	/* zero both data and tag */
	p = (u_long *) IOC_FLUSH_ADDR;
	while (p != ((u_long *) (IOC_FLUSH_ADDR+0x2000)))
		*p++ = 0;

	/* map in flush page for i/o cache */
	map_setpgmap((caddr_t)IOC_FLUSH_ADDR,
		PG_V | PG_KW | PGT_OBIO | btop(OBIO_IOCFLUSH_ADDR));

	/* XXX - for debugging only,  remove later */
	map_setpgmap((caddr_t)IOC_DATA_ADDR,
		PG_V | PG_KW | PGT_OBIO | btop(OBIO_IOCDATA_ADDR));
}
#endif IOC

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
 *	(4) page structures
 *	(5) gap
 *	(6) devices at MDEVBASE
 *	(7) kmem_pool
 *	(8) gap (possibly null)
 *	(9) debugger (optional)
 *	(10) monitor
 *	(11) gap (possibly null)
 *	(12) dvma
 */
void
kvm_init()
{
	int i;
	register caddr_t va, tv;
	struct pte pte;
	struct memlist *cur;
	u_int range_base, range_size, range_end;
	struct hat *hat;
	struct sunm *sunm;
	extern char t0stack[];
	extern caddr_t e_text;
	extern caddr_t s_data;
	extern caddr_t e_data;
	extern struct ctx *kctx;
	extern int segkmem_ready;


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

KVM_HERE
	/*
	 * Put kernel segment in kernel address space.  Make it a
	 * "kernel memory" segment object.
	 */
	rw_enter(&kas.a_lock, RW_WRITER);

	hat = kas.a_hat;		/* initialized in hat_init */
	sunm = (struct sunm *)hat->hat_data;
	sunm->sunm_ctx = kctx;

	(void) seg_attach(&kas, (caddr_t)KERNELBASE,
		(u_int)(e_data - KERNELBASE), &ktextseg);
	(void) segkmem_create(&ktextseg, (caddr_t)NULL);

KVM_HERE
	(void) seg_attach(&kas, (caddr_t)valloc_base, (u_int)econtig -
		(u_int)valloc_base, &kvalloc);
	(void) segkmem_create(&kvalloc, (caddr_t)NULL);

KVM_HERE
	if (boothowto & RB_DEBUG && debugger_start) {
		(void) seg_attach(&kas, (caddr_t)debugger_start,
			DEBUGSIZE, &kdebugseg);
		(void) segkmem_create(&kdebugseg, (caddr_t)NULL);
	}

KVM_HERE
	(void) seg_attach(&kas, (caddr_t)MDEVBASE, PMGRPSIZE, &kdevregseg);
	(void) segkmem_create(&kdevregseg, (caddr_t)NULL);

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
	 * NOW we can ask segkem for memory instead of boot.
	 */
	segkmem_ready = 1;

	/*
	 * Make sure the invalid pmeg and smeg have no valid entries
	 */
	map_setsgmap(SEGTEMP, PMGRP_INVALID);
	for (i = 0; i < PMGRPSIZE; i += PAGESIZE) {
		mmu_getpte(SEGTEMP + i, &pte);
		if (pte.pg_v) {
			pte.pg_v = 0;
			map_setpgmap(SEGTEMP + i, *(u_int *)&pte);
		}
	}

KVM_HERE
	if (mmu_3level) {
		/*
		 * Make sure the invalid smeg is completely invalid
		 */
		map_setrgnmap(REGTEMP, SMGRP_INVALID);
		for (i = 0; i < SMGRPSIZE; i += PMGRPSIZE) {
			struct pmgrp *pmgrp;
			pmgrp = mmu_getpmg((caddr_t)(REGTEMP + i));
			if (pmgrp->pmg_num != PMGRP_INVALID)
				mmu_pmginval((caddr_t)(REGTEMP + i));
		}
	}

	/*
	 * Invalidate segments before kernel.
	 */
	if (mmu_3level) {

		for (va = (caddr_t)0; va < (caddr_t)KERNELBASE;
		    va += SMGRPSIZE) {
			/*
			 * Is this entire *region* "available"?
			 */
			if (address_in_memlist(virt_avail, va, SMGRPSIZE) &&
			    mmu_getsmg(va) != smgrp_invalid) {
				mmu_smginval(va);
			} else {
				/*
				 * Nope: so reserve those pmegs which are not
				 * invalid, and remove user access to those
				 * addresses.
				 */
				for (tv = va; tv < va + SMGRPSIZE;
				    tv += PMGRPSIZE) {
					if (mmu_getpmg(tv) != pmgrp_invalid) {
						DPRINTF(("va %x reserved\n",
						    tv));
						sunm_pmgreserve(&kas, tv,
							PMGRPSIZE);
						hat_chgprot(&kas, tv,
						PMGRPSIZE, ~PROT_USER);
					} else {
						if (mmu_getpmg(tv) !=
						    pmgrp_invalid)
							mmu_pmginval(tv);
					}
				}
			}
		}
	} else {
		/*
		 * For 2 level MMUs there is a hole in the middle of the
		 * virtual address space.
		 */
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
		for (va = hole_end; va < (caddr_t)KERNELBASE;
		    va += PMGRPSIZE) {
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
	va = (caddr_t)roundup((u_int)e_data, PAGESIZE);

KVM_HERE
	/*
	 * Validate the valloc'ed structures
	 */
	(void) as_fault(hat, &kas, va, (u_int)(econtig - va),
			F_SOFTLOCK, S_OTHER);
	(void) as_setprot(&kas, va, (u_int)(econtig - va),
	    PROT_READ | PROT_WRITE | PROT_EXEC);
	va = (caddr_t)roundup((u_int)econtig, PAGESIZE);

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
	 * Invalidate everything on the virt_avail list after va,
	 * validate any portions that have mappings and are not
	 * on the virt_avail list.
	 * This includes:
	 *		SYSBASE - Syslimit. (kernelmap)
	 *		the debugger, if present
	 *		E_SYSBASE - E_Syslimit (ekernelmap)
	 *		the prom monitor
	 *
	 * The address range starting at MDEVBASE is a special
	 * case because the kernel has already initialized this
	 * pmeg and did so without updating the memory lists.
	 */
	/* invalidate loop */
	for (tv = va; tv != (caddr_t)0; tv += PMGRPSIZE) {
		if ((u_int)tv == MDEVBASE) {
			sunm_pmgreserve(&kas, (caddr_t)tv, PMGRPSIZE);
			continue;
		}
		if (address_in_memlist(virt_avail, tv, PMGRPSIZE)) {
			if (mmu_getpmg(tv) != pmgrp_invalid) {
				mmu_pmginval(tv);
			}
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

	/* validate loop */
	for (cur = virt_avail; cur->next; cur = cur->next) {
		if ((range_base = cur->address + cur->size) < (u_int)va)
			continue;
		range_size = cur->next->address - range_base;
		(void) as_fault(hat, &kas, (caddr_t)range_base, range_size,
			F_SOFTLOCK, S_OTHER);
		(void) as_setprot(&kas, (caddr_t)range_base, range_size,
			PROT_READ | PROT_WRITE | PROT_EXEC);
	}


KVM_HERE
	rw_enter(&kas.a_lock, RW_READER);

#if 0
	DPRINTF(("Dumping DVMA area\n"));
	DUMP_MMU_RANGE((caddr_t)0xfff00000, (caddr_t)&DVMA[ptob(dvmasize)]);
	DPRINTF(("Dumping sysmem_list area\n"));
	DUMP_MMU_RANGE((caddr_t)0xf81f0000, (caddr_t)0xf8266000);
	tv = (caddr_t)&DVMA[ptob(dvmasize)];
	for (; va < tv; va += PAGESIZE) {
		DPRINTF(("pte at %x invalid\n", va));
		mmu_setpte(va, mmu_pteinvalid);
	}
#endif

KVM_HERE
	/*
	 * Invalidate all the unlocked pmegs
	 * and smegs (if they exist).
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

KVM_HERE
	/*
	 * Ugliness for the onboard ethernet.
	 * Map in the topmost page to the first physical page
	 * (which we used to reclaim to the free list).
	 *
	 * But don't use the hat layer-
	 * I tried to do this the right way (just by extending kdvmaseg
	 * up to the end of memory) and found out how much the segment stuff
	 * and the hat layer depends on no integer overflow occurring).
	 */

	*((u_int *) &pte) = 0;
	if (cputype != CPU_SUN4_330) {
		pte.pg_prot = KW;
		pte.pg_v = 1;
		pte.pg_nc = 1;
	}
	mmu_setpte((caddr_t)-PAGESIZE, pte);

KVM_HERE

	/*
	 * Reserve a pmeg for pagecopy, pagezero, pagesum mappings.
	 */
	sunm_reserve(&kas, (caddr_t)PPMAPBASE, PMGRPSIZE);

	/*
	 * Find the beginning page frames of the kernel data
	 * segment and the ending pageframe (-1) for bss.
	 */
	mmu_getpte((caddr_t)(roundup((u_int)e_text, DATA_ALIGN)), &pte);
	kpfn_dataseg = pte.pg_pfnum;
	mmu_getpte((caddr_t)e_data, &pte);
	kpfn_endbss = pte.pg_pfnum;

KVM_HERE

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
	struct pte tpte;
	u_int va;
	int i = 0;
	u_int pages = 0;
	int lastpfnum = -1;

	for (va = (u_int)KERNELBASE; va < (u_int)econtig; va += PAGESIZE) {
		mmu_getpte((caddr_t)va, &tpte);
		if (tpte.pg_v) {
			if (lastpfnum == (u_int)-1) {
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
					cmn_err(CE_WARN, "out of kvtopents\n");
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
				cmn_err(CE_WARN, "out of kvtopents\n");
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
