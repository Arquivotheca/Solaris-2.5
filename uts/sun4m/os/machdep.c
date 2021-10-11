/*
 * Copyright (c) 1992, 1993, 1994 by Sun Microsystems, Inc.
 * ALL rights reserved.
 */

#pragma ident	"@(#)machdep.c	1.221	95/08/29 SMI"

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
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
#include <sys/varargs.h>
#include <sys/async.h>
#include <sys/promif.h>
#include <sys/comvec.h>
#include <sys/modctl.h>		/* for "procfs" hack */
#include <sys/kvtopdata.h>

#include <sys/consdev.h>
#include <sys/frame.h>

#define	SUNDDI_IMPL		/* so sunddi.h will not redefine splx() et al */
#include <sys/ddidmareq.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi.h>
#include <sys/bustypes.h>
#include <sys/clock.h>
#include <sys/physaddr.h>
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
#include <sys/spl.h>
#include <sys/machpcb.h>

#ifdef	IOC
#include <sys/iocache.h>
#endif /* IOC */

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
#include <sys/vm_machparam.h>
#include <vm/hat_srmmu.h>
#include <sys/iommu.h>
#include <sys/vtrace.h>
#include <sys/autoconf.h>
#include <sys/instance.h>
#include <sys/bt.h>
#include <sys/avintr.h>
#include <sys/aflt.h>
#include <sys/memctl.h>
#include <sys/mem.h>

#ifdef KDBX
/* only for kdbx */
#include <sys/kobj.h>
#endif /* KDBX */

#ifdef MP
static void idle_other_cpus(void);
static void resume_other_cpus(void);
static void stop_other_cpus(void);
extern int xc_level_ignore;
#endif /* MP */

#ifdef MP
extern void set_idle_cpu(int);
extern void unset_idle_cpu(int);
#endif /* MP */

extern void ic_flush();
extern void srmmu_unload(struct as *, caddr_t, u_int, int);

void	power_down(char *);

/*
 * The following are "implementation architecture" dependent constants made
 * available here in the form of initialized data for use by "implementation
 * architecture" independent modules. See machparam.h.
 */
const unsigned int	_pagesize	= PAGESIZE;
const unsigned int	_pageshift	= PAGESHIFT;
const unsigned int	_pageoffset	= PAGEOFFSET;
const unsigned int	_pagemask	= (unsigned int)PAGEMASK;
const unsigned int	_mmu_pagesize	= MMU_PAGESIZE;
const unsigned int	_mmu_pageshift	= MMU_PAGESHIFT;
const unsigned int	_mmu_pageoffset	= MMU_PAGEOFFSET;
const unsigned int	_mmu_pagemask	= (unsigned int)MMU_PAGEMASK;
const unsigned int	_kernelbase	= (unsigned int)KERNELBASE;
const unsigned int	_userlimit	= (unsigned int)USERLIMIT;
const unsigned int	_argsbase	= (unsigned int)ARGSBASE;
const unsigned long	_dfldsiz	= DFLDSIZ;
const unsigned long	_dflssiz	= DFLSSIZ;
const unsigned long	_maxdsiz	= MAXDSIZ;
const unsigned long	_maxssiz	= MAXSSIZ;
const unsigned int	_lotsfreefract	= LOTSFREEFRACT;
const unsigned int	_desfreefract	= DESFREEFRACT;
const unsigned int	_minfreefract	= MINFREEFRACT;
const unsigned int	_lotsfree	= LOTSFREE;
const unsigned int	_desfree	= DESFREE;
const unsigned int	_minfree	= MINFREE;
const unsigned int	_diskrpm	= DISKRPM;
const unsigned long	_dsize_limit	= DSIZE_LIMIT;
const unsigned long	_ssize_limit	= SSIZE_LIMIT;
const unsigned int	_pgthresh	= PGTHRESH;
const unsigned int	_maxslp		= MAXSLP;
const unsigned int	_maxhandspreadpages = MAXHANDSPREADPAGES;
const int		_ncpu 		= NCPU;
const unsigned int	_defaultstksz	= DEFAULTSTKSZ;
const unsigned int	_msg_bsize	= MSG_BSIZE;
const unsigned int	_nbpg		= NBPG;
const unsigned int	_usrstack	= (unsigned int)USRSTACK;

/*
 * Declare these as initialized data so we can patch them.
 */
int noprintf = 0;	/* patch to non-zero to suppress kernel printf's */
int nbuf = 0;
int msgbufinit = 1;	/* message buffer has been initialized, ok to printf */
static int nopanicdebug = 0;
			/* 0 = call debugger (if any) on panic, 1 = reboot */

#ifdef	KDBX
/*
 * Patched if using the source level kernel debugger
 */
int kdbx_useme = 0;
int kdbx_stopcpus = 0;
#endif /* KDBX */

/*
 * Miscellaneous hardware feature override flags
 */

int use_ic = 1;
int use_dc = 1;
int use_ec = 1;

/*
 * Viking/MXCC specific overrides
 */

int use_vik_prefetch = 0;
int use_mxcc_prefetch = 1;
int use_store_buffer = 1;
int use_multiple_cmds = 1;
int use_rdref_only = 0;

int do_pg_coloring = 0;		/* will be set for Viking/MXCC only */
int use_page_coloring = 1;	/* patch to 0 to disable above */

/* #ifndef	MP */
int Cpudelay = 0;		/* delay loop count/usec */
/* #endif MP */

int dvmasize = 255;		/* usable dvma space */

extern int cache;		/* address cache type (none = 0) */
/*
 * Boot hands us the romp.
 */
#if !defined(SAS) && !defined(MPSAS)
union sunromvec *romp = (union sunromvec *)0;
struct debugvec *dvec = (struct debugvec *)0;
#else
union sunromvec *romp = (struct sunromvec *)0;
#endif

int maxphys = 124 << 10;
int klustsize = 124 << 10;

int vac = 0;		/* virtual address cache type (none == 0) */
int vme = 1;		/* system has vmebus (for bcopy optimization) */

#ifdef	IOC
int ioc = 1;			/* I/O cache type (none == 0) */
#endif /* IOC */

#ifdef	BCOPY_BUF
int bcopy_buf = 0;		/* block copy buffer present */
#endif /* BCOPY_BUF */

/*
 * These do not belong here, but the implementation of
 * the debugger (and reading crash dumps) require the
 * ability to read these variables directly from the
 * kernel using a namelist.
 */
int _curmod;		/* for use by kadb */
#ifdef KDBX
struct module *_module_kdbx_stab;	/* for use by kdbx - force a stab */
#endif /* KDBX */
struct modctl modules;	/* head of linked list of modules */
char *default_path;	/* default module loading path */
struct swapinfo *swapinfo;	/* protected by the swapinfo_lock */
/*
 * XXX The following must be declared here for libkvm support.
 */
proc_t *practive;		/* active process chain */
u_int nproc;

/*
 * Globals for asynchronous fault handling
 */
#ifdef MP
/*
 * Synchronization used by the level-15 asynchronous fault
 * handling threads (one per CPU).  Each CPU which responds
 * to the broadcast level 15 interrupt, sets its entry in
 * aflt_sync.  Then, at the synchronization point, each CPU waits
 * for CPU 0 to clear aflt_sync.
 */
volatile u_int	aflt_sync[NCPU];
int	procset = 1;
#endif /* MP */

int	pokefault = 0;

/*
 * A dummy location where the flush_writebuffers routine
 * can perform a swap in order to flush the module write buffer.
 */
u_int	module_wb_flush;

/*
 * When nofault is set, if an asynchronous fault occurs, this
 * flag will get set, so that the probe routine knows something
 * went wrong.  It is up to the probe routine to reset this
 * once it's done messing around.
 */
volatile u_int	aflt_ignored = 0;

/*
 * When the first system-fatal asynchronous fault is detected,
 * this variable is atomically set.
 */
u_int	system_fatal = 0;

/*
 * ... and info specifying the fault is stored here:
 */
struct async_flt sys_fatal_flt[NCPU];

/*
 * Used to store a queue of non-fatal asynchronous faults to be
 * processed.
 */
struct	async_flt a_flts[NCPU][MAX_AFLTS];

/*
 * Incremented by 1 (modulo MAX_AFLTS) by the level-15 asynchronous
 * interrupt thread before info describing the next non-fatal fault
 * in a_flts[a_head].
 */
u_int	a_head[NCPU];

/*
 * Incremented by 1 (modulo MAX_AFLTS) by the level-12 asynchronous
 * fault handler thread before processing the next non-fatal fault
 * described by info in a_flts[a_tail].
 */
u_int	a_tail[NCPU];

/* Flag for testing directed interrupts */
int	test_dirint = 0;

/*
 * Interrupt Target Mask Register masks for each interrupt level.
 * Element n corresponds to interrupt level n.  To optimize the
 * interrupt code that manipulates this data structure, element 0 is
 * not used (no level 0 interrupts).
 */
u_int	itmr_masks[] = {0, SIR_L1, SIR_L2, SIR_L3, SIR_L4, SIR_L5,
			SIR_L6, SIR_L7, SIR_L8, SIR_L9, SIR_L10,
			SIR_L11, SIR_L12, SIR_L13, SIR_L14, SIR_L15};

/*
 * When set to non-zero, the nth element of this array indicates that
 * some CPU has masked level n interrupts (using the ITMR).
 * Set to the MID of the CPU that did the mask, so that upon returning
 * from the interrupt, a CPU can tell if it set the mask.  To optimize
 * the interrupt code that manipulates this data structure, element 0 is
 * not used (no level 0 interrupts).
 * The information contained here could have been represented with a
 * one-word bit mask, but then additional time consuming code would be
 * necessary to protect multiple CPUs accessing such a bit mask at the
 * same time.  Note that no two CPUs will ever be accessing the same
 * element simultaneously.
 */
u_char	ints_masked[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};


kthread_id_t clock_thread;	/* clock interrupt thread pointer */
kmutex_t memseg_lock;		/* lock for searching memory segments */

/*
 * workaround for hardware bug Ross 605
 */
int ross_hd_bug;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then reboots.
 * If we are called twice, then we avoid trying to
 * sync the disks as this often leads to recursive panics.
 */

/*
 * In case console is off, panicstr contains argument
 * to last call to panic.
 */
char	*panicstr = 0;
va_list panicargs;

/*
 * This is the state of the world before the file systems are sync'd
 * and system state is dumped.  Should be put in panic data structure.
 */
label_t	panic_regs;		/* adb looks at these */
kthread_id_t panic_thread;
kthread_id_t panic_clock_thread;
struct cpu panic_cpu;
kmutex_t paniclock;

static void complete_panic(void);

int small_4m = 0;		/* flag for small sun4m machines */

/*
 * The panic functionality has been separated into 2 parts to avoid
 * unecessary double panics. For example, in trap, we often took a
 * recursive mutex panic because we needed a lock that was needed to
 * print out the register windows
 */

void
panic(char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	do_panic(fmt, adx);
	va_end(adx);
}

void
do_panic(char *fmt, va_list adx)
{
	if (setup_panic(fmt, adx) == 0)
		complete_panic();

#ifdef	DEBUGGING_KERNEL
	if (!nopanicdebug)
		prom_enter_mon();
#endif
	mdboot(A_REBOOT, AD_BOOT, NULL);
}

int
setup_panic(char *fmt, va_list adx)
{
	int s;
	kthread_id_t tp;
	int i;

#ifndef LOCKNEST
	noprintf = 1;   /* turn printfs off */
	s = splzs();

	/*
	 * Allow only threads on panicking cpu to blow through locks.
	 * Use counter to avoid endless looping if code should panic
	 * before panicstr is set.
	 */
	mutex_enter(&paniclock);
	if (panicstr) {
		panicstr = fmt;
		panicargs = adx;
		(void) splx(s);
		return (0);
	}
	conslogging = 0;
	panic_cpu = *curthread->t_cpu;
	panicstr = fmt;
	panicargs = adx;
	/*
	 * Insure that we are neither preempted nor swapped out
	 * while setting up the panic state.
	 */
	thread_lock(curthread);
	curthread->t_bound_cpu = CPU;
	curthread->t_schedflag |= TS_DONT_SWAP;
	curthread->t_preempt++;
	thread_unlock(curthread);
	panic_thread = curthread;
	flush_windows();

#ifdef MP
	/*
	 * Order is important here.  We want to use a cross-call to
	 * flush the caches, but it is unsafe to assume the other
	 * cpus will respond.  They may already be spinning in
	 * a cross-call service routine at a high PIL level.  We
	 * try to capture them, and then reset their CPU_READY
	 * flags to keep from using them.
	 *
	 * When the QUIESCED bit is set, then all interrupts below
	 * level 9 are taken on the panic cpu.  The clock_thread
	 * is the exception.
	*/
	xc_capture_cpus(CPUSET_ALL_BUT(panic_cpu.cpu_id));
	for (i = 0; i < NCPU; i++) {
		if (i != panic_cpu.cpu_id && cpu_nodeid[i] != -1 &&
		    cpu[i] != NULL &&
		    (cpu[i]->cpu_flags & CPU_EXISTS)) {
			cpu[i]->cpu_flags &= ~CPU_READY;
			cpu[i]->cpu_flags |= CPU_QUIESCED;
		}
	}
	/*
	 * Ignore cross-call PSR levels, since all other cpus
	 * are no longer doing cross-calls.
	 */
	xc_level_ignore = 1;
	vac_flushall();			/* Flushes only panic cpu */
#endif /* MP */

	set_intmask(IR_ENA_INT, 1);	/* disable interrupts */
	set_itr_bycpu(panic_cpu.cpu_id);
	if (clock_thread)
		clock_thread->t_bound_cpu = CPU;
	set_intmask(IR_ENA_INT, 0);	/* enable interrupts */
#ifdef MP
	stop_other_cpus();
#endif /* MP */


	/*
	 * Pass this point if the clockthread thread runnable but
	 * not ON_PROC then it will take interrupts on panic_cpu.
	 * Otherwise we create new clock thread to take interrupts
	 * on panic_cpu
	 */
	if (clock_thread &&
	    (clock_thread->t_state == TS_SLEEP || clock_thread->t_lock)) {
		if (CPU->cpu_intr_thread) {
			tp = CPU->cpu_intr_thread;
			CPU->cpu_intr_thread = tp->t_link;
			tp->t_pri = clock_thread->t_pri;
			tp->t_cpu = CPU;
			CPU->cpu_intr_stack = tp->t_stk -= SA(MINFRAME);
			panic_clock_thread = clock_thread;
			clock_thread = tp;
		} else
			return (-1);
	}

	/*
	 * If on interrupt stack, allocate new interrupt thread
	 * stack
	 */
	if (CPU->cpu_on_intr) {
		if (CPU->cpu_intr_thread) {
			tp = CPU->cpu_intr_thread;
			CPU->cpu_intr_thread = tp->t_link;
			CPU->cpu_intr_stack = tp->t_stk -= SA(MINFRAME);
			CPU->cpu_on_intr = 0;
		} else
			return (-1);
	}
	if (curthread->t_cred == NULL)
		curthread->t_cred = kcred;

	(void) splx(s);
	return (0);
#endif /* LOCKNEST */
}

static void
complete_panic(void)
{
	static int in_sync = 0;
	static int in_dumpsys = 0;
	int s;
	extern int sync_timeout;

	s = splzs();

	noprintf = 0;   /* turn printfs on */

	if (curthread->t_cred == NULL)
		curthread->t_cred = kcred;

	printf("panic: ");
	prf(panicstr, panicargs, (vnode_t *)NULL, PRW_CONS | PRW_BUF);
	printf("\n");

	if (((boothowto & RB_DEBUG) || (obpdebug)) && (nopanicdebug == 0))
		debug_enter((char *)NULL);

	if (!in_sync) {
		in_sync = 1;
		vfs_syncall();
	} else {
		/* Assume we paniced while syncing and avoid timeout */
		sync_timeout = 0;
	}
	(void) setjmp(&panic_thread->t_pcb);	/* save stack ptr for dump */
	panic_regs = panic_thread->t_pcb;

	(void) splx(ipltospl(11));	/* let in the zs interrupt */
	if (!in_dumpsys) {
		in_dumpsys = 1;
		dumpsys();
	}
	(void) splx(s);
}


/*
 * Allow interrupt threads to run only don't allow them to nest
 * save the current interrupt count
 */
void
panic_hook(void)
{
	int s;

	if (CPU->cpu_id != panic_cpu.cpu_id)
		for (;;);

	if (panic_thread != curthread ||
	    CPU->cpu_on_intr > panic_cpu.cpu_on_intr)
		return;

	s = spl0();
	(void) splx(s);
}

/*
 * Check to see if the system has the ability of doing software power off.
 */
int
power_off_is_supported(void)
{
	int is_defined = 0;
	char *wordexists = "p\" power-off\" find nip swap ! ";

	prom_interpret(wordexists, (int)(&is_defined), 0, 0, 0, 0);
	if (is_defined != 0) /* is_defined has value -1 when defined */
		return (1);
	else
		return (0);
}


/*
 * Machine dependent code to reboot.
 * If "bootstr" is non-null, it is a pointer
 * to a string to be used as the argument string when rebooting.
 */
/*ARGSUSED*/
void
mdboot(int cmd, int fcn, char *bootstr)
{
	int s;

	/*
	 * XXX - rconsvp is set to NULL to ensure that output messages
	 * are sent to the underlying "hardware" device using the
	 * monitor's printf routine since we are in the process of
	 * either rebooting or halting the machine.
	 */
	rconsvp = NULL;

#if defined(SAS) || defined(MPSAS)
	asm("t 255");
#else
	s = spl6();
	reset_leaves(); 		/* try and reset leaf devices */
	if (fcn == AD_HALT) {
		halt((char *)NULL);
		fcn &= ~RB_HALT;
		/* MAYBE REACHED */
	} else if (fcn == AD_POWEROFF) {
		if (power_off_is_supported())
			power_down((char *)NULL);
		else
			halt((char *)NULL);
		/* NOTREACHED */
	} else {
		if (bootstr == NULL) {
			switch (fcn) {

			case AD_BOOT:
				bootstr = "";
				break;

			case AD_IBOOT:
				bootstr = "-a";
				break;

			case AD_SBOOT:
				bootstr = "-s";
				break;

			case AD_SIBOOT:
				bootstr = "-sa";
				break;
			default:
				cmn_err(CE_WARN,
				    "mdboot: invalid function %d", fcn);
				bootstr = "";
				break;
			}
		}
		prom_printf("rebooting...\n");
		prom_reboot(bootstr);
		/*NOTREACHED*/
	}
	(void) splx(s);
#endif /* SAS */
}

#ifndef SAS
/*
 * Machine-dependent portion of dump-checking;
 * verify that a physical address is valid.
 */
int
dump_checksetbit_machdep(u_longlong_t addr)
{
	struct memlist	*pmem;

	for (pmem = phys_install; pmem; pmem = pmem->next) {
		if (pmem->address <= addr &&
		    addr < (pmem->address + pmem->size))
			return (1);
	}
	return (0);
}

/*
 * Temporary address we use to map things when dumping them.
 */
caddr_t	dump_addr;
caddr_t	cur_dump_addr;

static	int	npgs = 0;
static	int	dbn = 0;

/*
 * Dump a page frame. Only setup the mapping until we have enough pages
 * to dump. "DUMPPAGES" increases the dump rate by more than 10 times!
 */
int
dump_page(struct vnode *vp, int pg, int bn)
{
	register caddr_t addr;
	register int err = 0;

	/*
	 * First setup the mapping for this page.
	 */
	addr = cur_dump_addr;

	srmmu_devload(kas.a_hat, &kas, addr, NULL, pg, PROT_READ,
	    HAT_NOCONSIST);
	cur_dump_addr += MMU_PAGESIZE;

	if (npgs == 0) {
		/*
		 * Remember were we have to start writing on the disk.
		 */
		dbn = bn;
	}
	npgs++;
	if (npgs == DUMPPAGES) {

		/*
		 * Write to the disk only if we have enough pages to flush.
		 * The dumpsys_doit() procedure will call dump_final() at the
		 * end where we will write out the last remaining set of pages
		 * to the disk
		 */

		addr = dump_addr;
		err = VOP_DUMP(vp, addr, dbn, ctod(npgs));

		/*
		 * Destroy the page mappings that we had setup.
		 */
		srmmu_unload(&kas, addr, MMU_PAGESIZE * npgs, 0);

		/*
		 * Restore the virtual address and pages that we have cached.
		 */

		npgs = 0;
		cur_dump_addr = dump_addr;
	}
	return (err);
}


int
dump_final(struct vnode *vp)
{
	int err;
	caddr_t addr = dump_addr;

	err = VOP_DUMP(vp, addr, dbn, ctod(npgs));
	srmmu_unload(&kas, addr, MMU_PAGESIZE * npgs, 0);
	npgs = 0;
	cur_dump_addr = dump_addr;
	return (err);
}

/*
 * XXX This is grotty, but for historical reasons, xxdump() routines
 * XXX (called by spec_dump) expect to be called with an object already
 * XXX in DVMA space, and break if it isn't.  This doesn't matter for
 * XXX nfs_dump, but to be general we do it for everyone.
 *
 * Dump an arbitrary kernel-space object.
 * We do this by mapping this object into DVMA space a page-worth's
 * of bytes at a time, since this object may not be on a page boundary
 * and may span multiple pages.  We must be careful because the object
 * (or trailing portion thereof) may not span a page boundary and the
 * next virtual address may map to i/o space, which could cause
 * heartache.
 * We assume that dvmasize is at least two pages.
 */
int
dump_kaddr(struct vnode *vp, caddr_t kaddr, int bn, int count)
{
	register caddr_t addr, addr2;
	register int err = 0;
	register int offset;
	int pf;
	int cxn = mmu_getctx();

	vac_pageflush(kaddr, cxn, FL_CACHE);

	offset = (u_int)kaddr & MMU_PAGEOFFSET;
	addr = dump_addr;
	addr2 = dump_addr + MMU_PAGESIZE;

	while (count > 0 && !err) {
		pf = va_to_pfn(kaddr);
		vac_pageflush(kaddr + MMU_PAGESIZE, cxn, FL_CACHE);

		if ((pf == -1) || !pf_is_memory(pf)) {
			cmn_err(CE_PANIC, "dumping non memory");
			goto update_ptr;
		}
		srmmu_devload(kas.a_hat, &kas, addr, NULL, pf, PROT_READ,
		    HAT_NOCONSIST);

		if (offset != 0) {

			pf = va_to_pfn(kaddr + MMU_PAGESIZE);
			if (pf == -1 || !pf_is_memory(pf)) {
				/*
				 * Small 4m machines with 16 MB memory
				 * configurations can land here - use
				 * address of msgbuf.
				 */
				pf = va_to_pfn((caddr_t)&msgbuf);
				if (pf == -1 || !pf_is_memory(pf)) {
					cmn_err(CE_PANIC, "dumping non memory");
					goto update_ptr;
				}
			}

			srmmu_devload(kas.a_hat, &kas, addr2, NULL, pf,
			    PROT_READ, HAT_NOCONSIST);
		}

		err = VOP_DUMP(vp, addr + offset, bn, ctod(1));
update_ptr:
		if (offset != 0) {
			srmmu_unload(&kas, addr, 2 * MMU_PAGESIZE, 0);
		} else {
			srmmu_unload(&kas, addr, MMU_PAGESIZE, 0);
		}
		bn += ctod(1);
		count -= ctod(1);
		kaddr += MMU_PAGESIZE;
		srmmu_unload(&kas, addr, 2 * MMU_PAGESIZE, 0);
	}

	return (err);
}

#else /* SAS */

/*ARGSUSED*/
int
dump_checksetbit_machdep(u_longlong_t addr)
{ return (1); }

/*ARGSUSED*/
int
dump_page(struct vnode *vp, int pg, int bn)
{ return (0); }

/*ARGSUSED*/
int
dump_kaddr(struct vnode *vp, caddr_t kaddr, int bn, int count)
{ return (0); }

#endif /* !SAS */

#ifdef	MP
static void
idle_other_cpus(void)
{
	int i;
	int cpuid = CPU->cpu_id;

	ASSERT(cpuid <= NCPU);

#ifdef KDBX
	kdbx_stopcpus = 1;
#endif /* KDBX */

	for (i = 0; i < NCPU; i++) {
		if (i != cpuid && cpu_nodeid[i] != -1 &&
		    cpu[i] != NULL && (cpu[i]->cpu_flags & CPU_EXISTS)) {
			prom_idlecpu((dnode_t)cpu_nodeid[i]);
		}
	}
}

static void
resume_other_cpus(void)
{
	int i;
	int cpuid = CPU->cpu_id;

	ASSERT(cpuid <= NCPU);

#ifdef KDBX
	kdbx_stopcpus = 0;
#endif /* KDBX */

	for (i = 0; i < NCPU; i++) {
		if (i != cpuid && cpu_nodeid[i] != -1 &&
		    cpu[i] != NULL && (cpu[i]->cpu_flags & CPU_EXISTS)) {
			prom_resumecpu((dnode_t)cpu_nodeid[i]);
		}
	}
}

static void
stop_other_cpus(void)
{
	int i;
	int cpuid = CPU->cpu_id;

	ASSERT(cpuid <= NCPU);

	for (i = 0; i < NCPU; i++) {
		if (i != cpuid && cpu_nodeid[i] != -1 &&
		    cpu[i] != NULL && (cpu[i]->cpu_flags & CPU_EXISTS)) {
			prom_stopcpu((dnode_t)cpu_nodeid[i]);
		}
	}
}

#endif	/* MP */

/*
 *	Machine dependent abort sequence handling
 */
void
abort_sequence_enter(char *msg)
{
	debug_enter(msg);
}

/*
 * Enter debugger.  Called when the user types L1-A or break or whenever
 * code wants to enter the debugger and possibly resume later.
 * If the debugger isn't present, enter the PROM monitor.
 */
int debug_enter_cpuid;

void
debug_enter(char *msg)
{
	int s;
	label_t debug_save;
	extern int vx_entered;

	if (msg)
		prom_printf("%s\n", msg);

	s = splzs();

	if (panic_thread == NULL) {
		/*
		 * Do not try cross-calls if we have too high
		 * a PSR level, since we may be deadlocked in
		 * the cross-call routines.
		 */
		if (getpil() < XC_MED_PIL)
			vac_flushall();
		idle_other_cpus();
	}

	debug_enter_cpuid = CPU->cpu_id;

	/*
	 * If we came in through vx_handler, then kadb won't talk to us
	 * anymore, so until we fix it, just drop into the prom--all we
	 * want to be able to do at this point is reboot
	 */
	if (!vx_entered && boothowto & RB_DEBUG) {
		debug_save = curthread->t_pcb;
		(void) setjmp(&curthread->t_pcb);
		{ func_t callit = (func_t)dvec; (*callit)(); }
		curthread->t_pcb = debug_save;
	} else {
		prom_enter_mon();
	}

	if (panic_thread == NULL)
		resume_other_cpus();

	(void) splx(s);
}

/*
 * Halt the machine and return to the monitor
 */
void
halt(char *s)
{
	flush_windows();
	stop_other_cpus();		/* send stop signal to other CPUs */

	if (s)
		prom_printf("(%s) ", s);

	prom_exit_to_mon();
	/*NOTREACHED*/
}


/*
 * Halt the machine and power off the system.
 */
void
power_down(char *s)
{
	flush_windows();
	stop_other_cpus();		/* send stop signal to other CPUs */

	if (s)
		prom_printf("(%s) ", s);

	prom_interpret("power-off", 0, 0, 0, 0, 0);
	/*
	 * If here is reached, for some reason prom's power-off command failed.
	 * Prom should have already printed out error messages. Exit to
	 * firmware.
	 */
	prom_exit_to_mon();
	/*NOTREACHED*/
}


/*
 * Enter monitor.  Called via cross-call from stop_cpus().
 */
void
mp_halt(char *msg)
{
	if (msg)
		prom_printf("%s\n", msg);
	prom_exit_to_mon();
}


/*
 * Explicitly set these so they end up in the data segment.
 * We clear the bss *after* initializing these variables in locore.s.
 */
char mon_clock_on = 0;			/* disables profiling */

/*
 * The variable mon_clock_on controls how hard level 14 timer interrupts
 * are handled. When the variable is set to 0, the kernel handles all
 * level14 interrupts. When the variable is set to 1, the PROM handles
 * hard level 14 interrupts. The kernel always handles soft level 14
 * interrupts. (See bugid #1168398).
 * The PROM needs level 14 interrupts to support the
 * prom_gettime() entry point which is needed by inetboot
 * when we load modules.
 */
void
init_mon_clock(void)
{
	/*
	 * This looks strange, but it's necessary to call set_clk_mode
	 * in this way. The first saves the clock limit register as
	 * set by OBP, and the second restores it and enables the
	 * clock to interrupt
	 */
	set_clk_mode(0, IR_ENA_CLK14); /* disable level 14 clk intr */
	mon_clock_on = 1;	/* disable profiling */
	set_clk_mode(IR_ENA_CLK14, 0); /* enable level 14 clk intr */
	splzs();
}

void
start_mon_clock(void)
{
	if (!mon_clock_on) {
		mon_clock_on = 1;		/* disable profiling */
		set_clk_mode(IR_ENA_CLK14, 0);	/* enable level 14 clk intr */
	}
}

void
stop_mon_clock(void)
{
	if (mon_clock_on) {
		mon_clock_on = 0;		/* enable profiling */
		set_clk_mode(0, IR_ENA_CLK14);	/* disable level 14 clk intr */
	}
}

/*
 * Write the scb, which is the first page of the kernel.
 * Normally it is write protected, we provide a function
 * to fiddle with the mapping temporarily.
 *	1) lock out interrupts
 *	2) save the old pte value of the scb page
 *	3) set the pte so it is writable
 *	4) write the desired vector into the scb
 *	5) restore the pte to its old value
 *	6) restore interrupts to their previous state
 */
void
write_scb_int(register int level, struct trapvec *ip)
{
	register int s;
	register trapvec *sp;

	/*
	 * Don't touch anything if V_TBR_WR_ADDR is not set up yet. This
	 * can happen if the system panics early in the boot process. We
	 * don't want to cause a DBE here.
	 */
	if (!tbr_wr_addr_inited)
		return;

	sp = &((struct scb *)V_TBR_WR_ADDR)->interrupts[level - 1];
	s = spl8();

	/*
	 * Ensure that only threads with the hat mutex flush do cache
	 * flushes.
	 */
	hat_enter(&kas);

	/*
	 * We purposely avoid using the CAPTURE_CPUS macro, because
	 * we always want to capture all CPUs, even when OPTIMAL_CACHE
	 * is enabled.
	 */
	if (vac) {
		int xc_lsave = xc_level_ignore;

		xc_level_ignore = 1;	/* disable xc assertion */
		xc_capture_cpus(-1);

		xc_level_ignore = xc_lsave;
	}

	/* write out new vector code */
	*sp = *ip;

	if (vac) {
		/*
		 * This flush ensures that the cache line that was updated when
		 * the trap vector was modified via the RW mapping above gets
		 * written to memory.
		 */
		vac_allflush(FL_CACHE);

		/*
		 * We flush again to deal with the scenerio where a trap could
		 * have occured before the last flush completed and caused the
		 * cache line associated with the RO mapping to become stale.
		 */
		vac_allflush(FL_CACHE);

		/*
		 * Finally, since the code above may only have flushed the
		 * external cache on some processors, we must also flush the
		 * internal cache.
		 */
		ic_flush();

		xc_release_cpus();
	}

	hat_exit(&kas);

	(void) splx(s);
}

#ifdef	DEBUGGING_MEM

static int dset;

#define	STOP()			if (dset) prom_enter_mon()
#define	DPRINTF(str)		if (dset) prom_printf((str))
#define	DPRINTF1(str, a)	if (dset) prom_printf((str), (a))
#define	DPRINTF2(str, a, b)	if (dset) prom_printf((str), (a), (b))
#define	DPRINTF3(str, a, b, c)	if (dset) prom_printf((str), (a), (b), (c))
#else	DEBUGGING_MEM
#define	STOP()
#define	DPRINTF(str)
#define	DPRINTF1(str, a)
#define	DPRINTF2(str, a, b)
#define	DPRINTF3(str, a, b, c)
#endif	DEBUGGING_MEM

#ifndef SAS
/*
 * Console put and get character routines.
 */
void
cnputs(char *buf, u_int bufsize, int device_in_use)
{
#ifdef MP
	if (device_in_use) {
		int s;
		/*
		 * This means that some other CPU may have a mapping
		 * to the device (framebuffer) that the OBP is about
		 * to render onto.  Some of the fancier framebuffers get upset
		 * about being accessed by more than one CPU - so stop
		 * the others in their tracks.
		 *
		 * This is a somewhat unfortunate 'hackaround' to the general
		 * problem of sharing a device between the OBP and userland.
		 *
		 * This should happen -very- rarely on a running system
		 * provided you have a console window redirecting console
		 * output when running your favourite window system ;-)
		 */
		s = splhi();
		xc_capture_cpus(CPUSET(CPU->cpu_id));
		idle_other_cpus();
		prom_writestr(buf, bufsize);
		resume_other_cpus();
		xc_release_cpus();
		(void) splx(s);
	} else
#endif /* MP */
		prom_writestr(buf, bufsize);
}

void
cnputc(register int c, int device_in_use)
{
#ifdef MP
	int s;
	if (device_in_use) {
		s = splhi();
		xc_capture_cpus(CPUSET(CPU->cpu_id));
		idle_other_cpus();
	}
#endif /* MP */

	if (c == '\n')
		prom_putchar('\r');
	prom_putchar(c);

#ifdef MP
	if (device_in_use) {
		resume_other_cpus();
		xc_release_cpus();
		(void) splx(s);
	}
#endif /* MP */
}

static int
cngetc(void)
{
	return ((int)prom_getchar());
}

/*
 * Get a character from the console.
 *
 * XXX	There's no need for both cngetc() and getchar() -- merge 'em
 */
static int
getchar(void)
{
	register c;

	c = cngetc();
	if (c == '\r')
		c = '\n';
	cnputc(c, 0);
	return (c);
}

/*
 * Get a line from the console.
 */
void
gets(char *cp)
{
	register char *lp;
	register c;

	lp = cp;
	for (;;) {
		c = getchar() & 0177;
		switch (c) {

		case '\n':
			*lp++ = '\0';
			return;

		case 0177:
			cnputc('\b', 0);
			/*FALLTHROUGH*/
		case '\b':
			cnputc(' ', 0);
			cnputc('\b', 0);
			/*FALLTHROUGH*/
		case '#':
			lp--;
			if (lp < cp)
				lp = cp;
			continue;

		case 'u'&037:
			lp = cp;
			cnputc('\n', 0);
			continue;

		case '@':	/* used in physical device names! */
		default:
			*lp++ = (char)c;
		}
	}
}

#endif /* !SAS */

#if defined(DEBUG) || defined(lint)
/*
 * Called by assembler files that want to debug_enter
 */
void
call_debug_from_asm(void)
{
	int s;

	s = splzs();		/* protect against re-entrancy */
	(void) setjmp(&curthread->t_pcb); /* for kadb */
	debug_enter((char *)NULL);
	(void) splx(s);
}
#endif /* DEBUG || lint */

#if defined(DEBUG) || defined(lint)

static int
chk_map_addr(u_int addr, char *str)
{
	if ((addr != 0) && (addr < 0xfc000)) {
		printf("*** chk_map_addr(%s) failed: addr=0x%x\n", str, addr);
		return (1);
	}
	return (0);
}

#endif	/* DEBUG || lint */

/* #define	DEBUG_GETDVMA	*/
#ifdef	DEBUG_GETDVMA
static int debug_getdvma;
#define	GPRINTF	if (debug_getdvma) printf
#endif

#define	ALIGN_REQUIRED(align)		(align != (u_int) -1)
#define	COUNTER_RESTRICTION(cntr)	(cntr != (u_int) -1)
#define	SEG_ALIGN(addr, seg)		(mmu_btop(((((addr) + (u_long) 1) +  \
					    (seg)) & ~(seg))))

#define	WITHIN_DVMAMAP(page)	\
	((page >= SBUSMAP_BASE) && (page - SBUSMAP_BASE < SBUSMAP_SIZE))

u_long
getdvmapages(int npages, u_long addrlo, u_long addrhi, u_int align,
	u_int cntr, int cansleep)
{
	u_int alignx = align;
	u_long alo = mmu_btop(addrlo);
	u_long ahi, amax, amin, aseg;
	u_long addr = 0;

	if (addrhi != (u_long) -1) {
		/*
		 * -1 is our magic NOOP for no high limit. If it's not -1,
		 * make addrhi 1 bigger since ahi is a non-inclusive limit,
		 * but addrhi is an inclusive limit.
		 */
		addrhi++;
		amax = mmu_btop(addrhi);
	} else {
		amax = mmu_btop(addrhi) + 1;
	}
	/*
	 * If we have a counter restriction we adjust ahi to the
	 * minimum of the maximum address and the end of the
	 * current segment. Actually it is the end+1 since ahi
	 * is always excluding. We then allocate dvma space out
	 * of a segment instead from the whole map. If the allocation
	 * fails we try the next segment.
	 */
	if (COUNTER_RESTRICTION(cntr)) {
		u_long a;

		if (WITHIN_DVMAMAP(alo)) {
			a = addrlo;
		} else {
			a = mmu_ptob(SBUSMAP_BASE);
		}
		/*
		 * check for wrap around
		 */
		if (a + (u_long) 1 + cntr <= a) {
			ahi = mmu_btop((u_long) -1) + 1;
		} else {
			ahi = SEG_ALIGN(a, cntr);
		}
		ahi = min(amax, ahi);
		aseg = ahi;
		amin = alo;
	} else {
		ahi = amax;
	}

	/*
	 * Okay. Now try and allocate the space.
	 *
	 * we may have a 'constrained' allocation;
	 * if so, we have to search dvmamap for a piece
	 * that fits the constraints.
	 *
	 * Furthermore, if we have a specified favorite
	 * alignment, we also search for a piece to fit
	 * that favorite alignment.
	 */

	if (WITHIN_DVMAMAP(alo) || WITHIN_DVMAMAP(ahi) ||
	    ALIGN_REQUIRED(align) || COUNTER_RESTRICTION(cntr)) {
		register struct map *mp;
		register u_int mask;

		if (vac && ALIGN_REQUIRED(align)) {
			align = mmu_btop(align);
			mask = mmu_btop(shm_alignment) - 1;
		}

		/*
		 * Search for a piece that will fit.
		 */
		mutex_enter(&maplock(dvmamap));
again:
		for (mp = mapstart(dvmamap); mp->m_size; mp++) {
			u_int ok, end;
			end = mp->m_addr + mp->m_size;
			if (alo < mp->m_addr) {
				if (ahi >= end)
					ok = (mp->m_size >= npages);
				else {
					end = ahi;
					ok = (mp->m_addr + npages <= ahi);
				}
				addr = mp->m_addr;
			} else {
				if (ahi >= end)
					ok = (alo + npages <= end);
				else {
					end = ahi;
					ok = (alo + npages <= ahi);
				}
				addr = alo;
			}
#ifdef	DEBUG_GETDVMA
			GPRINTF(" %x:%x alo %x ahi %x addr %x end %x",
			    mp->m_addr, mp->m_addr + mp->m_size, alo, ahi,
			    addr, end);
#endif
			if (ok) {
				if (vac && ALIGN_REQUIRED(align)) {
					u_long oaddr = addr;
					addr = (addr & ~mask) + align;
					if (addr < oaddr)
						addr += mask + 1;
#ifdef	DEBUG_GETDVMA
					GPRINTF(" algn %x addr %x.%x->%x.%x",
					    mmu_ptob(align), oaddr,
					    mmu_ptob(oaddr-1),
					    addr, mmu_ptob(addr-1));
#endif
					if (addr + npages > end) {
#ifdef	DEBUG_GETDVMA
						GPRINTF("-no\n");
#endif
						continue;
					}
				}
#ifdef	DEBUG_GETDVMA
				GPRINTF("-yes\n");
#endif
				break;
			}
#ifdef	DEBUG_GETDVMA
			GPRINTF("-no\n");
#endif
		}
		if (mp->m_size != 0) {
			u_long addrx = addr;
			/*
			 * Let rmget do the rest of the work.
			 */
			addr = rmget(dvmamap, (long)npages, addr);
#ifdef	DEBUG
			if (chk_map_addr(addr, "rmget")) {
				printf("addrx=0x%x, mp=0x%x, 0x%x\n",
					addrx, mp->m_addr, mp->m_size);
				printf("addrlo=0x%x, addrhi=0x%x, align=0x%x\n",
					addrlo, addrhi, alignx);
			}
#endif	/* DEBUG */
		} else {
			addr = 0;
		}

		if (addr == 0) {
			/*
			 * If we have a counter restriction we walk the
			 * dvma space in segments at a time. If we
			 * reach the last segment we reset alo and ahi
			 * to the original values. This allows us to
			 * walk the segments again in case we have to
			 * switch to unaligned mappings or we were out
			 * of resources.
			 */
			if (COUNTER_RESTRICTION(cntr)) {
				if (ahi < amax) {
					alo = ahi;
					ahi = min(amax,
						ahi + mmu_btopr(cntr));
					goto again;
				} else {
					/*
					 * reset alo and ahi in case we
					 * have to walk the segments again
					 */
					alo = amin;
					ahi = aseg;
				}
			}
			if (vac && ALIGN_REQUIRED(align)) {
				/*
				 * try it again with unaligned mappings. This
				 * is important for mappings that could
				 * not be aliased with the first scan and
				 * that have specific alo/ahi limits.
				 */
				align = (u_int) -1;
				goto again;
			}
		}
		if (addr == 0 && cansleep) {
#ifdef	DEBUG_GETDVMA
			GPRINTF("getdvmapages: sleep on constrained alloc\n");
#endif
			mapwant(dvmamap) = 1;
			cv_wait(&map_cv(dvmamap), &maplock(dvmamap));
			goto again;
		}
		mutex_exit(&maplock(dvmamap));
	} else {
		if (cansleep) {
			addr = rmalloc_wait(dvmamap, npages);
#ifdef	DEBUG
			if (chk_map_addr(addr, "rmget")) {
				printf("addrlo=0x%x, addrhi=0x%x\n",
					addrlo, addrhi);
			}
#endif	/* DEBUG */
		} else {
			addr = rmalloc(dvmamap, npages);
#ifdef	DEBUG
			if (chk_map_addr(addr, "rmget")) {
				printf("addrlo=0x%x, addrhi=0x%x\n",
					addrlo, addrhi);
			}
#endif	/* DEBUG */
		}
	}
	if (addr) {
		addr = mmu_ptob(addr);
	}

	return (addr);
}

void
putdvmapages(u_long addr, int npages)
{
	addr = mmu_btop(addr);
	rmfree(dvmamap, (long)npages, addr);
}

/*ARGSUSED2*/
u_long
get_map_pages(int npages, struct map *map, u_int align, int cansleep)
{
	u_long addr;

	if (cansleep)
		addr = rmalloc_wait(map, npages);
	else
		addr = rmalloc(map, npages);
	if (addr) {
		addr = mmu_ptob(addr);
	}
	return (addr);
}

/*
 * Allocate threads and stacks for interrupt handling.
 */
#define	NINTR_THREADS	(LOCK_LEVEL-1)	/* number of interrupt threads */
#ifdef REDCHECK
#define	INTR_STACK_SIZE	(roundup(8192 + PAGESIZE, PAGESIZE))
#else
#define	INTR_STACK_SIZE	(roundup(8192, PAGESIZE))
#endif /* REDCHECK */

void
init_intr_threads(struct cpu *cp)
{
	int i;

	for (i = 0; i < NINTR_THREADS; i++)
		(void) thread_create_intr(cp);

	cp->cpu_intr_stack = (caddr_t)segkp_get(segkp, INTR_STACK_SIZE,
		KPD_HASREDZONE | KPD_NO_ANON | KPD_LOCKED) +
		INTR_STACK_SIZE - SA(MINFRAME);
}

void
init_clock_thread(void)
{
	kthread_id_t tp;

	/*
	 * Create clock interrupt thread.
	 * The state is initially TS_FREE.  Think of this thread on
	 * a private free list until it runs.
	 */
	tp = thread_create(NULL, INTR_STACK_SIZE, init_clock_thread, NULL, 0,
		&p0, TS_FREE, 0);
	tp->t_flag |= T_INTR_THREAD;	/* for clock()'s tick checking */
	clock_thread = tp;
}


/*
 * Called from dumpsys() to ensure the kvtopdata is in the dump.
 * Also does msgbuf since it's no longer in ktextseg.
 *
 * XXX  Not entirely convinced we need to do this specially ..
 */
void
dump_kvtopdata(void)
{
	caddr_t		i, j;

	i = (caddr_t)(((u_int)&kvtopdata) & MMU_PAGEMASK);
	for (j = (caddr_t)&kvtopdata + sizeof (kvtopdata); i < j;
	    i += MMU_PAGESIZE) {
		dump_addpage(va_to_pfn(i));
	}
	i = (caddr_t)&msgbuf;
	j = i + sizeof (msgbuf);
	while (i < j) {
		dump_addpage(va_to_pfn(i));
		i += MMU_PAGESIZE;
	}
}

#define	isspace(c)	((c) == ' ' || (c) == '\t' || (c) == '\n' || \
			    (c) == '\r' || (c) == '\f' || (c) == '\013')

static void
parse_str(register char *str, register char *args[])
{
	register int i = 0;

	while (*str && isspace(*str))
		str++;
	while ((i < 8) && (*str)) {
		args[i++] = str;
		while (*str && (!isspace(*str)))
			str++;
		if (*str)
			*str++ = '\0';
		while (*str && isspace(*str))
			str++;
	}
}

int vx_entered = 0;
/*
 * Handler for monitor vector cmd -
 * For now we just implement the old "g0" and "g4"
 * commands and a printf hack.
 */
void
vx_handler(char *str)
{
	char *sargv[8];
	struct cmd_info {
		char	*cmd;
		int	func;
	};
#define	ENDADDR(a)	&a[sizeof (a) / sizeof (a[0])]
	static struct cmd_info vx_cmd[] = {
		"sync", 0,
	};
#define	vx_cmd_end	ENDADDR(vx_cmd)

	register struct cmd_info *cp;
	register int    func;
	struct scb *oldtbr;
	int i;

	reestablish_curthread();

	/*
	 * Since about prom rev 2.10, the level14 clock is
	 * enabled when we come in here.  Turn it off so we don't
	 * get hung up not handling level 14 interrupts.
	 */
	set_clk_mode(0, IR_ENA_CLK14);

	/*
	 * Set tbr to the appropriate value.
	 * See CPU_INDEX macro in srmmu/sys/mmu.h for more info.
	 * set_tbr performs splhi/splx equivalent, so we don't have to.
	 */
	switch (CPU->cpu_id) {
	case 0:
		oldtbr = set_tbr(&scb);
		break;
	default:
		oldtbr = set_tbr((struct scb *)
			(V_TBR_ADDR_BASE + (CPU->cpu_id*MMU_PAGESIZE)));
		break;
	}

	parse_str(str, sargv);
	func = -1;
	for (cp = (struct cmd_info *)vx_cmd;
	    cp < (struct cmd_info *)vx_cmd_end; cp++) {
		if (strcmp(sargv[0], cp->cmd) == 0) {
			func = cp->func;
			break;
		}
	}

	switch (func) {
	case 0:		/* sync */
		/*
		 * Avoid trying to talk to the other CPUs since they are
		 * already sitting in the prom and won't reply.
		 *
		 * This means that the vac_flushall() call in the panic
		 * code doesn't flush the caches on the other CPUs. This
		 * is not a problem since we know that idle_other_cpus()
		 * has already done so when we first got into the prom.
		 */
		for (i = 0; i < NCPU; i++) {
			if (i != CPU->cpu_id && cpu_nodeid[i] != -1 &&
			    cpu[i] != NULL &&
			    (cpu[i]->cpu_flags & CPU_EXISTS)) {
				cpu[i]->cpu_flags &= ~CPU_READY;
				cpu[i]->cpu_flags |= CPU_QUIESCED;
			}
		}
		nopanicdebug = 1;	/* don't try to bkpt to kadb/prom */
		vx_entered++;		/* allow interruption of core dump */
		cmn_err(CE_PANIC, "zero");
		/*NOTREACHED*/

	default:
		prom_printf("Don't understand '%s'\n", str);
	}
	(void) set_tbr(oldtbr);
}

#ifdef notdef
watchp(void)
{}
#endif /* notdef */


/*
 * set delay constant for usec_delay()
 * NOTE: we use the fact that the per-
 * processor clocks are available and
 * mapped properly at "*utimersp".
 */
void
setcpudelay(void)
{
	extern volatile struct count14	*utimersp;
	unsigned	r;		/* timer resolution, ~ns */
	unsigned	e;		/* delay time, us */
	unsigned	es;		/* delay time, ~ns */
	unsigned	t, f;		/* for time measurement */
	int		s;		/* saved PSR for inhibiting ints */
	u_int		orig_control;	/* original clock control register */
	u_int		orig_config;	/* original timer config register */
	u_int		orig_limit;	/* oringal counter/timer limit */

	if (Cpudelay != 0)	/* for MPSAS, adb patch Cpudelay = 1 */
		return;

	r = 512;		/* worst supported timer resolution */
	es = r * 100;		/* target delay in ~ns */
	e = ((es + 1023) >> 10); /* request delay in us, round up */
	es = e << 10;		/* adjusted target delay in ~ns */
	Cpudelay = 1;		/* initial guess */

	/*
	 * Save current configuration of cpu 0's timer so we can put things
	 * back the way we found them.  If the timer is configured as a
	 * counter/timer, save the limit register, reconfig as a user
	 * timer and restore everything on exit.  If configured already
	 * as a user timer, start it if it isn't running and turn it off
	 * before exit.  If already running, don't change it now or on exit.
	 *
	 * Note: If it is necessary to reconfig as a user timer, we can
	 * only restore the original limit register value on exit which
	 * causes the counter to be reinitialized to 500ns.  Otherwise,
	 * the counter register is read-only.
	 */
	if (((orig_config = v_level10clk_addr->config) & TMR0_CONFIG) == 0) {
		orig_limit = utimersp->timer_msw;
		v_level10clk_addr->config |= TMR0_CONFIG;
		orig_control = utimersp->control;
		utimersp->control = 1;
	} else
	if ((orig_control = utimersp->control) == 0)
		utimersp->control = 1;

	DELAY(1);		/* first time may be weird */
	do {
		Cpudelay <<= 1;	/* double until big enough */
		do {
			s = spl8();
			t = utimersp->timer_lsw;
			DELAY(e);
			f = utimersp->timer_lsw;
			(void) splx(s);
		} while (f < t);
		t = f - t;
	} while (t < es);
	Cpudelay = (Cpudelay * es + t) / t;
	if (Cpudelay < 0)
		Cpudelay = 0;

	do {
		s = spl8();
		t = utimersp->timer_lsw;
		DELAY(e);
		f = utimersp->timer_lsw;
		(void) splx(s);
	} while (f < t);
	t = f - t;

	/*
	 * Restore the original timer conifiguration.
	*/
	if ((orig_config & TMR0_CONFIG) == 0) { /* formerly a counter/timer */
		utimersp->control = orig_control;
		v_level10clk_addr->config = orig_config;
		utimersp->timer_msw = orig_limit;
	} else					/* formerly a user timer */
		if (orig_control == 0)
			utimersp->control = 0;
}

/*
 * This only gets called when we crash.
 * No point in pushing it down into
 * assembly code.
 */
void
vac_flushall(void)
{
	XCALL_PROLOG
	vac_ctxflush(0, FL_CACHE);	/* supv and ctx 0 */
	vac_usrflush(FL_ALLCPUS);	/* user */
	XCALL_EPILOG
}

/*
 * This routine has now been implemented in assembly, but keep it
 * around anyway for a while for debugging purposes.
 */
#ifdef	MP
#ifdef	XXX
static u_int	last_idle_cpu;	/* set by set_idle_cpu() */

void
new_interrupt_target()
{
	u_int cur_cpu, new_target;
	u_int saved_cpu, idle_cpu;
	struct cpu *cpup;

	if (ncpus == 1)
		return;

	/*
	 * Note that save_cpu indicates the current CPU which
	 * the ITR is set to, since this routine is run when
	 * an interrupt occurrs.
	 */
	saved_cpu = cur_cpu = getprocessorid();
	idle_cpu = last_idle_cpu;

	/*
	 * The strategy is to try to make the most recently
	 * idle CPU be the target of interrupts, with the
	 * exception that the current CPU is not to be chosen
	 * as a target because this could result in race
	 * conditions.  In that case, we start searching for
	 * the next CPU that exists, and let it be the new
	 * target.
	 */
	if (idle_cpu != cur_cpu)
		new_target = idle_cpu;
	else {
		while ((new_target = (cur_cpu++) % NCPU) != cur_cpu) {
			cpup = cpu[new_target];
			if (cpup != NULL && (cpup->cpu_flags & CPU_EXISTS))
				break;
		}
	}

	/*
	 * Only need to set the ITR when it will be set to a
	 * different target.
	 */
	if (new_target != saved_cpu)
		set_interrupt_target(new_target);
}
#endif	XXX
#endif MP

#ifdef  MP
/*
 * set_idle_cpu is called from idle() when a CPU becomes idle.
 */
/*ARGSUSED*/
void
set_idle_cpu(int cpun)
{}

/*
 * unset_idle_cpu is called from idle() when a CPU is no longer idle.
 */
/*ARGSUSED*/
void
unset_idle_cpu(int cpun)
{}
#endif /* MP */

/*
 * XXX These probably ought to live somewhere else - how about vm_machdep.c?
 * XXX They are called from mem.c
 */

/*
 * Convert page frame number to an OBMEM page frame number
 * (i.e. put in the type bits -- zero for this implementation)
 */
int
impl_obmem_pfnum(int pf)
{
	return (pf);
}

/*
 * A run-time interface to the MAKE_PFNUM macro.
 */
int
impl_make_pfnum(struct pte *pte)
{
	return (MAKE_PFNUM(pte));
}

/*
 * Given a pte, return an address and a type based
 * on the pte. The address takes on a set of units
 * based on the type of the pte.
 */

int
pte2atype(void *p, u_long offset, u_long *addrp, u_int *type)
{
	struct pte *pte = p;
	u_long endpfnum = MAKE_PFNUM(pte) + mmu_btop(offset);

	switch (impl_bustype(endpfnum)) {
	case BT_NVRAM:
	case BT_DRAM:
		/*
		 * The address is the physical page frame number
		 */
		*addrp = endpfnum;
		*type = SP_OBMEM;
		break;

	case BT_OBIO:
		/*
		 * The address is the physical page frame number
		 */
		*addrp = endpfnum;
		*type = SP_OBIO;
		break;

	case BT_VME:
	{
		u_long vme_bustype = PTE_BUSTYPE(endpfnum);
		u_long vme_pa = mmu_ptob(endpfnum);
		int wide = ((vme_bustype == VME_D32_USR) ||
			(vme_bustype == VME_D32_SUPV));

		/*
		 * VMEA16 space is stolen from the top 64k of VMEA24 space,
		 * and VMEA24 space is stolen from the top 16mb of VMEA32
		 * space.
		 */
		if (vme_pa >= (u_int)VME16_BASE) {
			*type = (wide) ? SP_VME16D32 : SP_VME16D16;
			vme_pa &= (u_int)VME16_MASK;
		} else if (vme_pa >= (u_int)VME24_BASE) {
			*type = (wide) ? SP_VME24D32 : SP_VME24D16;
			vme_pa &= (u_int)VME24_MASK;
		} else {
			*type = (wide) ? SP_VME32D32 : SP_VME32D16;
		}
		*addrp = vme_pa;
		break;
	}

	default:
		return (DDI_FAILURE);
		/*NOTREACHED*/
	}
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
int
aflt_get_iblock_cookie(dev_info_t *dip, int fault_type,
    ddi_iblock_cookie_t *iblock_cookiep)
{
	extern u_int sx_ctlr_present;

	/*
	 * Currently we only offer this service on C2/C2+ for nvsimms
	 * and SX.
	 */
	if (!nvsimm_present && !sx_ctlr_present)
		return (AFLT_NOTSUPPORTED);

	if (mc_type != MC_EMC && mc_type != MC_SMC)
		return (AFLT_NOTSUPPORTED);

	if (fault_type != AFLT_ECC && fault_type != AFLT_SX)
		return (AFLT_NOTSUPPORTED);

	*iblock_cookiep = (ddi_iblock_cookie_t)ipltospl(AFLT_HANDLER_LEVEL);
	return (AFLT_SUCCESS);
}

int
aflt_add_handler(dev_info_t *dip, int fault_type, void **hid,
    int (*func)(void *, void *), void *arg)
{
	extern struct memslot memslots[];
	extern void *sx_aflt_fun;
	extern u_int sx_ctlr_present;
	extern char nvsimm_name[];
	struct aflt_cookie *ac;
	void *hid2;
	struct regspec *rp;
	int regsize, slot;

	*hid = NULL;

	/*
	 * Currently we only offer this service on C2/C2+ for nvsimms
	 * and SX.
	 */
	if (mc_type != MC_EMC && mc_type != MC_SMC) {
		return (AFLT_NOTSUPPORTED);
	}

	switch (fault_type) {
	case AFLT_ECC:
		if (!nvsimm_present)
			return (AFLT_NOTSUPPORTED);

		if (strcmp(DEVI(dip)->devi_name, nvsimm_name) != 0 ||
		    ddi_prop_op(DDI_DEV_T_ANY, dip, PROP_LEN_AND_VAL_ALLOC,
		    DDI_PROP_DONTPASS, OBP_REG, (caddr_t)&rp, &regsize) !=
		    DDI_PROP_SUCCESS) {
			    return (AFLT_NOTSUPPORTED);
		}
		ASSERT(regsize >= 2 * sizeof (struct regspec));
		slot = PMEM_SLOT(rp->regspec_addr);
		ASSERT(memslots[slot].ms_bustype == BT_NVRAM);
		kmem_free(rp, regsize);
		if (memslots[slot].ms_func != NULL) {
			return (AFLT_FAILURE);
		}
		hid2 = (void *)&memslots[slot];
		break;

	case AFLT_SX:
		if (!sx_ctlr_present)
			return (AFLT_NOTSUPPORTED);

		if (sx_aflt_fun != NULL)
			return (AFLT_FAILURE);
		hid2 = (void *)func;
		break;

	default:
		return (AFLT_NOTSUPPORTED);
	}

	ac = kmem_zalloc(sizeof (struct aflt_cookie), KM_NOSLEEP);
	if (ac == NULL)
		return (AFLT_FAILURE);

	ac->handler_type = fault_type;
	ac->cookie = hid2;
	*hid = ac;

	switch (fault_type) {
	case AFLT_ECC:
		memslots[slot].ms_dip = (void *)dip;
		memslots[slot].ms_arg = arg;
		memslots[slot].ms_func = func;
		break;

	case AFLT_SX:
		sx_aflt_fun = hid2;
		break;
	}

	return (AFLT_SUCCESS);
}

int
aflt_remove_handler(void *hid)
{
	extern void *sx_aflt_fun;
	register struct memslot *mp;
	register struct aflt_cookie *ac = (struct aflt_cookie *)hid;

	if (ac == NULL)
		return (AFLT_FAILURE);

	switch (ac->handler_type) {
	case AFLT_ECC:
		mp = ac->cookie;
		if (mp == NULL || mp->ms_bustype != BT_NVRAM ||
		    mp->ms_func == NULL) {
			return (AFLT_FAILURE);
		}
		mp->ms_func = NULL;
		break;

	case AFLT_SX:
		if (ac->cookie == NULL)
			return (AFLT_FAILURE);
		sx_aflt_fun = NULL;
		break;

	default:
		return (AFLT_NOTSUPPORTED);
	}

	ac->handler_type = 0;
	ac->cookie = NULL;

	wait_till_seen(AFLT_HANDLER_LEVEL);
	kmem_free(ac, sizeof (struct aflt_cookie));

	return (AFLT_SUCCESS);
}


int
mem_bus_type(pfn)
{
	extern struct memslot memslots[];
	int slot = PMEM_PFN_SLOT(pfn);

	ASSERT(memslots[slot].ms_bustype);
	return (memslots[slot].ms_bustype);
}


/*
 * Initialize kernel thread's stack.
 */
caddr_t
thread_stk_init(caddr_t stk)
{
	return (stk - SA(MINFRAME));
}

/*
 * Initialize lwp's kernel stack.
 */
caddr_t
lwp_stk_init(klwp_t *lwp, caddr_t stk)
{
	struct machpcb *mpcb;

	stk -= SA(sizeof (struct machpcb));
	mpcb = (struct machpcb *)stk;
	bzero((caddr_t)mpcb, sizeof (struct machpcb));
	lwp->lwp_regs = (void *)&mpcb->mpcb_regs;
	lwp->lwp_fpu = (void *)&mpcb->mpcb_fpu;
	mpcb->mpcb_fpu.fpu_q = mpcb->mpcb_fpu_q;
	mpcb->mpcb_thread = lwp->lwp_thread;
	return (stk);
}

#ifdef KDBX
#define	MAXKADBWIN	8
int kdbx_maxwin = MAXKADBWIN;
struct allregs {		/* same as struct in adb */
	int	r_psr;
	int	r_pc;
	int	r_npc;
	int	r_tbr;
	int	r_wim;
	int	r_y;
	int	r_globals[7];
	struct rwindow	r_window[MAXKADBWIN];	/* locals, then ins */
} kdbx_regsave[NCPU];	/* save area for all CPUs' regs */

char kdbx_insofttrap[NCPU];	/* num cpus spinning in kdbx_softtrap */

/*
 * this routine is called by other CPUs when one CPU entered the debugger.
 * this routine is to save the registers of the other processor so they are
 * available to the debugger.
 */
kdbx_softtrap(cpuid, regs, tbr)
	int cpuid;
	struct regs *regs;
	int tbr;
{
	extern int nwindows;	/* number of windows */
	kthread_id_t t;
	struct allregs *arp;	/* allregs structure pointer */
	int cwp;	/* Current window pointer */
	int nextwin;	/* window number for the next window */
	int i, *dp, *sp;	/* used during register copy */

	/* save the processor state in the kdbx_regsave area */
	cwp = (regs->r_psr & PSR_CWP);
	arp = &(kdbx_regsave[cpuid]);
	arp->r_psr = regs->r_psr;	/* psr */
	arp->r_pc  = regs->r_pc;	/* pc */
	arp->r_npc = regs->r_npc;	/* npc */
	arp->r_tbr = tbr;		/* tbr */
	/* we can calculate the wim because a flush windows was done */
	/* we are in the trap window and the wim is cwp plus 2 */
	arp->r_wim = 1 << ((cwp+2) % nwindows); /* calculate wim */
	arp->r_y   = regs->r_y;		/* y */

	arp->r_globals[0] = regs->r_g1;		/* globals */
	arp->r_globals[1] = regs->r_g2;
	arp->r_globals[2] = regs->r_g3;
	arp->r_globals[3] = regs->r_g4;
	arp->r_globals[4] = regs->r_g5;
	arp->r_globals[5] = regs->r_g6;
	arp->r_globals[6] = regs->r_g7;

	/* copy the locals and the ins off of the stack */
	for (i = 0, sp = (int *)regs->r_o6, dp = (int *)&(arp->r_window[cwp]);
	    i < 16; i++)
		*dp = *sp;

	nextwin = (cwp == 0) ? nwindows - 1 : cwp - 1;
	arp->r_window[nextwin].rw_in[0] = regs->r_o0;	/* outs */
	arp->r_window[nextwin].rw_in[1] = regs->r_o1;
	arp->r_window[nextwin].rw_in[2] = regs->r_o2;
	arp->r_window[nextwin].rw_in[3] = regs->r_o3;
	arp->r_window[nextwin].rw_in[4] = regs->r_o4;
	arp->r_window[nextwin].rw_in[5] = regs->r_o5;
	arp->r_window[nextwin].rw_in[6] = regs->r_o6;
	arp->r_window[nextwin].rw_in[7] = regs->r_o7;

	kdbx_insofttrap[cpuid] = 1;
	prom_idlecpu(0);
	kdbx_insofttrap[cpuid] = 0;
}
#endif /* KDBX */
