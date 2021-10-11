/*
 * Copyright (c) 1992-1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)machdep.c	1.227	95/07/13 SMI"

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/syserr.h>
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
#include <sys/processor.h>
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
#include <sys/psr.h>
#include <sys/machpcb.h>

#define	SUNDDI_IMPL		/* so sunddi.h will not redefine splx() et al */
#include <sys/sunddi.h>
#include <sys/ddidmareq.h>
#include <sys/spl.h>
#include <sys/clock.h>
#include <sys/pte.h>
#include <sys/scb.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/stack.h>
#include <sys/eeprom.h>
#include <sys/trap.h>
#include <sys/led.h>
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
#include <sys/aflt.h>
#include <sys/mem.h>
#include <sys/physaddr.h>

#ifdef KDBX
#include <sys/kobj.h>
#endif /* KDBX */

extern struct cpu cpu0;				/* gotta have one to start */
extern struct cpu *cpu[];			/* per-cpu generic data */

static void idle_other_cpus(void);
static void resume_other_cpus(void);
static void stop_other_cpus(void);
void power_down(char *);

int do_stopcpu;		/* we are running FCS and later PROM, do prom_stopcpu */

int __cg92_used;	/* satisfy C compiler reference for -xcg92 option */

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
int nopanicdebug = 0;	/* 0 = call debugger (if any) on panic, 1 = reboot */
#ifdef KDBX
int kdbx_useme = 0;
int kdbx_stopcpus = 0;
#endif

extern int	initing;
extern u_int	nwindows;	/* computed in locore.s */
extern u_int	last_idle_cpu;  /* computed in disp.c */

int do_pg_coloring = 1;		/* patch to 0 to disable */

/*
 * Default delay loop count is set for 70 MHz SuperSPARC processors.
 * This algorithm is (Max CPU MHZ/2) * .95
 */
int Cpudelay = 34;		/* delay loop count/usec */

int dvmasize = 255;		/* usable dvma space */

/*
 * Boot hands us the romp.
 */
#if !defined(SAS) && !defined(MPSAS)
union sunromvec *romp = (union sunromvec *)0;
struct debugvec *dvec = (struct debugvec *)0;
#else
union sunromvec *romp = (struct sunromvec *)0;
#endif

int maxphys = 124 << 10;	/* XXX What is this used for? */
int klustsize = 124 << 10;	/* XXX "		    " */

/*
 * Most of these vairables are set to indicate whether or not a particular
 * architectural feature is implemented.  If the kernel is not configured
 * for these features they are #defined as zero. This allows the unused
 * code to be eliminated by dead code detection without messy ifdefs all
 * over the place.
 */

int vac = 0;			/* virtual address cache type (none == 0) */

#ifdef	IOC
int ioc = 1;			/* I/O cache type (none == 0) */
#endif /* IOC */

/*
 * These do not belong here, but the implementation of
 * the debugger (and reading crash dumps) require the
 * ability to read these variables directly from the
 * kernel using a namelist.
 */
int _curmod;		/* for use by kadb */
#ifdef KDBX
struct module *_module_kdbx_stab; /* for use by kdbx */
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

int	pokefault = 0;

/*
 * A dummy location where the flush_writebuffers routine
 * can perform a swap in order to flush the module write buffer.
 */
u_int	module_wb_flush;

kthread_id_t clock_thread;	/* clock interrupt thread pointer */
kmutex_t memseg_lock;		/* lock for searching memory segments */

kmutex_t long_print_lock;	/* lock for calling cmn_err() with <128 chars */

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

static kthread_id_t panic_thread2;

static void complete_panic(char *, va_list);

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
		complete_panic(fmt, adx);

#ifdef	DEBUGGING_KERNEL
	if (!nopanicdebug)
		prom_enter_mon();
#endif
	mdboot(A_REBOOT, AD_BOOT, NULL);
}

extern dev_info_t *cpu_get_dip();
static int clk_restart;
static int mon_clock_restart;

int
setup_panic(char *fmt, va_list adx)
{
	kthread_id_t tp;
	int i;

#ifndef LOCKNEST
	noprintf = 1;   /* turn printfs off */
#ifdef NOTUSED
	s = splzs();
#endif /* NOTUSED */

	/*
	 * Allow only threads on panicking cpu to blow through locks.
	 * Use counter to avoid endless looping if code should panic
	 * before panicstr is set.
	 */
	mutex_enter(&paniclock);
	if (panicstr) {
		/*
		 * why overwrite the original panic string?
		 */
#ifdef NOT_USED
		panicstr = fmt;
		panicargs = adx;
		(void) splx(s);
#endif /* NOT_USED */
		/*
		 * flush/save this new panic thread state as much as
		 * we can but core dump may have dumped memory
		 * involved here then it still won't show the
		 * correct trace on adb. We try anyway.
		 */
		panic_thread2 = curthread;
		flush_windows();
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
	curthread->t_schedflag |= TS_DONT_SWAP;
	curthread->t_preempt++;
	panic_thread = curthread;
	thread_unlock(curthread);


	/*
	 * Order is important here. We need the cross-call to flush
	 * the caches. Once the QUIESCED bit is set, then all interrupts
	 * below level 9 are taken on the panic cpu. clock_thread is the
	 * exception.
	 */
	flush_windows();
#ifdef MP
	for (i = 0; i < NCPU; i++) {
		if (i != panic_cpu.cpu_id && cpu[i] != NULL &&
		    (cpu[i]->cpu_flags & CPU_EXISTS)) {
			cpu[i]->cpu_flags &= ~CPU_READY;
			cpu[i]->cpu_flags |= CPU_QUIESCED;
		}
	}
#endif /* MP */
	if (clock_thread)
		clock_thread->t_bound_cpu = CPU;

	if (do_stopcpu)
		stop_other_cpus();
	else
		/* workaround for OBP bug */
		idle_other_cpus();

	/*
	 * we direct all intr to the panicing CPU. Maybe we can
	 * look around and pick another CPU which may be at lower
	 * intr pri level?
	 */
	set_all_itr_by_cpuid(CPU->cpu_id);

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
		} else {
			return (-1);
		}
	}

	/*
	 * Restart a clock on the panicing cpu ...
	 */
	if (CPU->cpu_id != cpu0.cpu_id && clk_restart == 0) {
		clk_restart++;
		clkstart();
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
		} else {
			return (-1);
		}
	}
	if (curthread->t_cred == NULL)
		curthread->t_cred = kcred;

#ifdef NOT_USED
	(void) splx(s);
#endif /* NOT_USED */
	return (0);
#endif /* LOCKNEST */
}

static int in_dumpsys = 0;
static int lowered_pil = 0;

static void
complete_panic(char *fmt, va_list adx)
{
	static int in_sync = 0;
	static int dump_cpu;
	u_int ipl, old_ipl;
	extern int sync_timeout;

#define	ZSINTR	(12)
#define	MONINTR	(14)

#ifdef NOT_USED
	int s;

	s = splzs();
	noprintf = 0;   /* turn printfs on */
#endif /* NOT_USED */

	if (curthread->t_cred == NULL)
		curthread->t_cred = kcred;

	printf("panic[cpu%d]/thread=0x%x: ", CPU->cpu_id, curthread);
	prf(fmt, adx, (vnode_t *)NULL, PRW_CONS | PRW_BUF);
	printf("\n");

	if (((boothowto & RB_DEBUG) || (obpdebug)) && (nopanicdebug == 0))
		debug_enter((char *)NULL);

	(void) setjmp(&curthread->t_pcb);	/* save stack ptr for dump */
	if (curthread == panic_thread)
		panic_regs = panic_thread->t_pcb;

	if (!in_sync) {
		/*
		 * If the panic cpu comes in at an IPL that's masking
		 * clock intrs (e.g. a sync from the PROM) drop it to
		 * CLOCK_LEVEL - 1, so sync timeouts work.
		 */
		old_ipl = ipl = getpil();
		if (old_ipl >= CLOCK_LEVEL) {
			lowered_pil = 1;
			ipl = CLOCK_LEVEL - 1;
			(void) splx(ipltospl(ipl));
		}
		in_sync = 1;
		vfs_syncall();

		/* Switch back the IPL if we changed it */
		if (old_ipl != ipl)
			(void) splx(ipltospl(old_ipl));
	} else {
		/* Assume we paniced while syncing and avoid timeout */
		sync_timeout = 0;
	}

	if (!in_dumpsys) {
		in_dumpsys = 1;
		dump_cpu = CPU->cpu_id;

		/*
		 * Don't take anymore i/o intr. This prevents
		 * us from taking another interrupt introduced
		 * panic. We let clock runs so that timeout
		 * would work.
		 */
		old_ipl = ipl = spltoipl(getpsr());
		if (ipl < (CLOCK_LEVEL - 1)) {
			ipl = CLOCK_LEVEL - 1;
			(void) splx(ipltospl(ipl));
		}

		/*
		 * panic_hook will not be opening windows for intr to come in
		 * since in_dumpsys is no longer 0. We TRY to arrange someone
		 * to monitor the keyboard for L1-A to abort the dump.
		 */
		if (!(CPU->cpu_id == cpu0.cpu_id && ipl < ZSINTR) &&
		    !mon_clock_restart) {
			extern int mon_clock_on;

			/*
			 * Lower the PIL (was set to 0xF in locore.s) to allow
			 * L14 mon_clock to come in. Note: IPL is >= 12 when
			 * it gets here, so clock (timeout) won't work, but
			 * we at least make sure L1-A would still work. We
			 * have lowered IPL from 14, 15 to 13 but we've
			 * lowered it to 0 in panic_hook while doing fs
			 * sync, so why care now.
			 */
			ipl = MONINTR - 1;
			(void) splx(ipltospl(ipl));

			mon_clock_on = 1;
			enable_profiling(CPU->cpu_id);
			mon_clock_restart++;
		}

		dumpsys();
		in_dumpsys = 2;
		/*
		 * Put back to the original lvl. this should not be required
		 * since we are going to reboot anyway .. but, just be in
		 * the safe side.
		 */
		if (ipl != old_ipl)
			(void) splx(ipltospl(old_ipl));
	}

	/*
	 * FIXME: a workaround (aka hack) to avoid that core dump
	 *	get aborted by panic in a interrupt thread.
	 */
	if (in_dumpsys == 1) {
		if (CPU->cpu_id == dump_cpu) {
			printf("Dump Aborted.\n");
			prom_enter_mon();
		} else {
#ifdef DEBUG
			printf("New intr panic caught.\n");
#endif /* DEBUG */
			for (;;);
		}
	}

#ifdef NOT_USED
	(void) splx(s);
#endif /* NOT_USED */
}

/*
 * Allow interrupt threads to run only don't allow them to nest
 * save the current interrupt count
 */
void
panic_hook(void)
{
	int s;

	if (CPU->cpu_id != panic_cpu.cpu_id &&
	    !(CPU->cpu_on_intr || curthread->t_flag & T_INTR_THREAD)) {
		for (;;);
	}

	if (panic_thread != curthread)
		return;

	if (!in_dumpsys) {
		s = spl0();
		(void) splx(s);
	}
}

/*
 * Sun4d has its own way of doing software power off.
 */
extern	int	power_off_is_supported(void);

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

	/*
	 * we don't call start_mon_clock() here since we need L14
	 * vector for OBP MBox to work.
	 */

#if defined(SAS) || defined(MPSAS)
	asm("t 255");
#else
	/* extreme priority; allow clock interrupts to monitor at level 14 */
	s = spl6();
	reset_leaves(); 		/* try and reset leaf devices */
	if (fcn == AD_HALT) {
		halt((char *)NULL);
		fcn &= ~RB_HALT;
		/* MAYBE REACHED */
	} else if (fcn == AD_POWEROFF) {
		if (power_off_is_supported())
			power_down(NULL);
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
		led_set_cpu(CPU->cpu_id, 0x7);
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
 * For a ~90 Mb dump here is a table of values:
 *
 *	#pages flush together		Time taken
 *	---------------------		----------
 *		1			5:46		(original code)
 *		4			1:30
 *		8			0:55
 *		16			0:40		Hence DUMPPAGES = 16
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

#endif	/* SRMMU */

#ifdef	SRMMU
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

	offset = (u_int)kaddr & MMU_PAGEOFFSET;
	addr = dump_addr;
	addr2 = dump_addr + MMU_PAGESIZE;

	while (count > 0 && !err) {
		pf = va_to_pfn(kaddr);
		if (pf == -1 || !pf_is_memory(pf)) {
cmn_err(CE_PANIC, "dummping non memory");
			goto update_ptr;
		}
		srmmu_devload(kas.a_hat, &kas, addr, NULL, pf, PROT_READ,
			HAT_NOCONSIST);

		if (offset != 0) {
			pf = va_to_pfn(kaddr + MMU_PAGESIZE);
			if (pf == -1 || !pf_is_memory(pf)) {
cmn_err(CE_PANIC, "dummping non memory 2");
				goto update_ptr;
			}
			srmmu_devload(kas.a_hat, &kas, addr2, NULL, pf,
			    PROT_READ, HAT_NOCONSIST);
		}

		err = VOP_DUMP(vp, addr + offset, bn, ctod(1));
update_ptr:
		if (offset != 0)
			srmmu_unload(&kas, addr, 2 * MMU_PAGESIZE, 0);
		else
			srmmu_unload(&kas, addr, MMU_PAGESIZE, 0);

		bn += ctod(1);
		count -= ctod(1);
		kaddr += MMU_PAGESIZE;
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

static void
idle_other_cpus(void)
{
	int i;
	int cpuid = CPU->cpu_id;
	dev_info_t *dip;

	ASSERT(cpuid <= NCPU);

	for (i = 0; i < NCPU; i++) {
		if ((i != cpuid) && (dip = cpu_get_dip(i))) {
			prom_idlecpu((dnode_t)ddi_get_nodeid(dip));
		}
	}
}

static void
resume_other_cpus(void)
{
	int i;
	int cpuid = CPU->cpu_id;
	dev_info_t *dip;

	ASSERT(cpuid <= NCPU);

	for (i = 0; i < NCPU; i++) {
		if ((i != cpuid) && (dip = cpu_get_dip(i))) {
			prom_resumecpu((dnode_t)ddi_get_nodeid(dip));
		}
	}
}

static void
stop_other_cpus(void)
{
	int i;
	int cpuid = CPU->cpu_id;
	dev_info_t *dip;

	ASSERT(cpuid <= NCPU);

	for (i = 0; i < NCPU; i++) {
		if ((i != cpuid) && (dip = cpu_get_dip(i))) {
			prom_stopcpu((dnode_t)ddi_get_nodeid(dip));
		}
	}
}

/*
 *	Machine dependent abort sequence handling
 */

#define		SR1_KEYPOS		(1 << 4) /* fourth bit */

void
abort_sequence_enter(char *msg)
{
	if (!(xdb_bb_status1_get() & SR1_KEYPOS)) {
		debug_enter(msg);
		intr_clear_pend_local(SPLTTY);
	}
}

/*
 * Enter debugger.  Called when the user types L1-A or break or whenever
 * code wants to enter the debugger and possibly resume later.
 * If the debugger isn't present, enter the PROM monitor.
 */
void
debug_enter(char *msg)
{
	int s;

	if (msg)
		prom_printf("%s\n", msg);

	s = splzs();

	if (ncpus > 1 && panic_thread == NULL)
#ifdef KDBX
		if (kdbx_useme == 0)
#endif	/* KDBX */
		idle_other_cpus();

	if (boothowto & RB_DEBUG) {
		label_t debug_save = curthread->t_pcb;
		(void) setjmp(&curthread->t_pcb);
		{ func_t callit = (func_t)dvec; (*callit)(); }
		curthread->t_pcb = debug_save;
	} else {
		led_set_cpu(CPU->cpu_id, 0x9);
		prom_enter_mon();
		led_set_cpu(CPU->cpu_id, LED_CPU_RESUME);
	}

	if (ncpus > 1 && panic_thread == NULL)
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
#ifdef	MP
	if (ncpus > 1) {
		if (do_stopcpu)
			stop_other_cpus();
		else
			/* workaround for an OBP bug */
			idle_other_cpus();
	}
#endif /* MP */

	if (s)
		prom_printf("(%s) ", s);
	led_set_cpu(CPU->cpu_id, 0x7);
	prom_exit_to_mon();
	/*NOTREACHED*/
}


void
power_down(char *s)
{
	flush_windows();
#ifdef	MP
	if (ncpus > 1) {
		if (do_stopcpu)
			stop_other_cpus();
		else
			/* workaround for an OBP bug */
			idle_other_cpus();
	}
#endif /* MP */

	if (s)
		prom_printf("(%s) ", s);
	power_off();	/* trip the circuit breaker */
	/*NOTREACHED*/
	led_set_cpu(CPU->cpu_id, 0x7);
	prom_exit_to_mon();
	/*NOTREACHED*/
}


/*
 * Write the scb, which is the first page of the kernel.
 * Normally it is write protected, we provide a function
 * to fiddle with the mapping temporarily.
 *	1) lock out interrupts
 *	2) change protections to make writable
 *	3) write the desired vector into the scb
 *	4) change protections to make write-protected
 *	5) restore interrupts to their previous state
 */
void
write_scb_int(int level, struct trapvec *ip)
{
	register int s;
	register trapvec *sp;
	caddr_t addr;

	sp = &scb.interrupts[level - 1];
	s = spl8();

	addr = (caddr_t)((u_int)sp & PAGEMASK);

	rw_enter(&kas.a_lock, RW_READER);
	hat_chgprot(&kas, addr, PAGESIZE, PROT_READ | PROT_WRITE | PROT_EXEC);

	/* write out new vector code */
	*sp = *ip;

	hat_chgprot(&kas, addr, PAGESIZE, PROT_READ | PROT_EXEC);
	rw_exit(&kas.a_lock);

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
/*ARGSUSED2*/
void
cnputs(char *buf, u_int bufsize, int device_in_use)
{
	if (device_in_use) {
		int s;
		/*
		 * This means that some other CPU may have a mapping
		 * to the device (framebuffer) that the OBP is about
		 * to render onto.  Some of the fancier framebuffers get upset
		 * about being accessed by more than one CPU - so stop
		 * the others in their tracks.  The usual result is
		 * a completely wedged system which requires a hard reset.
		 *
		 * This is an unfortunate 'hackaround' to the general
		 * problem of sharing a device between the OBP and userland.
		 *
		 * Fortunately, this should happen -very- rarely on a
		 * running system provided you have a console window
		 * redirecting console output when running your
		 * favourite window system ;-)
		 */
		s = splhi();
		idle_other_cpus();
		prom_writestr(buf, bufsize);
		resume_other_cpus();
		(void) splx(s);
	} else
		prom_writestr(buf, bufsize);
}

/*ARGSUSED1*/
void
cnputc(register int c, int device_in_use)
{
	int s;

	if (device_in_use) {
		s = splhi();
		idle_other_cpus();
	}
	if (c == '\n')
		prom_putchar('\r');
	prom_putchar(c);
	if (device_in_use) {
		resume_other_cpus();
		(void) splx(s);
	}
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
static char
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
			/* FALLTHROUGH */
		case '\b':
			cnputc(' ', 0);
			cnputc('\b', 0);
			/* FALLTHROUGH */
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

#endif	/* DEBUG */

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
	u_int cntr, int cansleep, struct map *dvmamap)
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
	 * NOTE: align here really means VAC aliasing. It's not doing
	 * what I thought it should be doing: sastify a special DVMA alignment
	 * from users. Now that ddi_dmareq does not support alignment from user
	 * land as I expected, we are going to punt on this since we have no
	 * VAC alias problem to begin with. (impala)
	 *
	 * the following align code is not for us anyway. (for example,
	 * shm_alignment means nothing for us). We need to differently when
	 * we really support alignment from user.
	 */
	ASSERT(align == (u_int) -1); /* isn't cast great? -1 is now a u_int! */

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
#ifdef VAC
		register u_int mask;

		if (ALIGN_REQUIRED(align)) {
			align = mmu_btop(align);
			mask = mmu_btop(shm_alignment) - 1;
		}
#endif /* VAC */

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
#ifdef VAC
				if (ALIGN_REQUIRED(align)) {
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
#endif /* VAC */
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
#ifdef VAC
			if (ALIGN_REQUIRED(align)) {
				align = (u_int) -1;
				goto again;
			}
#endif VAC
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
putdvmapages(u_long addr, int npages, struct map *dvmamap)
{
	addr = mmu_btop(addr);
	rmfree(dvmamap, (long)npages, addr);
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
 *
 * XXX  Not entirely convinced we need to do this specially ..
 */
void
dump_kvtopdata(void)
{
	caddr_t		i, j;
	u_int 		pfn;
	int		level;

	i = (caddr_t)(((u_int)&kvtopdata) & MMU_PAGEMASK);
	for (j = (caddr_t)&kvtopdata + sizeof (kvtopdata); i < j;
	    i += MMU_PAGESIZE) {
		if ((pfn = va_to_pfn(i)) != (u_int) -1)
			dump_addpage(pfn);
		else
			printf("dump_kvtopdata: bad kvtopdat\n");
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
	int s;

	reestablish_curthread();
	s = splhigh();
	oldtbr = set_tbr(&scb);
	(void) splx(s);

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
		 * this prevents do_panic to go back to OBP
		 * again. OBP does not like us to go back
		 * from callbacks.
		 */
		nopanicdebug = 1;
		cmn_err(CE_PANIC, "zero");
		/*NOTREACHED*/

	default:
		prom_printf("Don't understand '%s'\n", str);
	}
	s = splhigh();
	(void) set_tbr(oldtbr);
	(void) splx(s);
}

#ifdef VAC
/*
 * This only gets called when we crash.
 * No point in pushing it down into
 * assembly code.
 */
void
vac_flushall(void)
{
	XCALL_PROLOG
	vac_ctxflush(0);	/* supv and ctx 0 */
	vac_usrflush();		/* user */
	XCALL_EPILOG
}
#endif /* VAC */

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

/* XXX */
void
sun4d_stub_nopanic(void)
{
	prom_printf("sun4d_stub_nopanic: called at pc 0x%x\n", caller());
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
 * MXCC Control knobs (pg 38)
 * SuperSPARC MCNTL knobs (pg 96)
 * SuperSPARC breakpoint ACTION knobs (pg 129)
 * BusWatcher Prescaler (pg 61)
 * BootBus Semaphore 0 (pg 17)
 */
static int option_print = 1;
char *mxcc_cntrl_bits = "\20\12RC\7WI\6PF\5MC\4PE\3CE\2CS\1HC";
char *vik_mcntl_bits = "\20\23PF\21TC\20AC\17SE\16BT\15PE\14MB\13SB"
	"\12IE\11DE\10PSO\2NF\1EN";
char *vik_action_bits = "\20\15MIX"
	"\10STEN_CBK\7STEN_ZIC\6STEN_DBK\5STEN_ZCC"
	"\4IEN_CBK\3IEN_ZIC\2IEN_DBK\1IEN_ZCC";

#include <sys/cpuvar.h>		/* CPU */
extern u_int intr_mxcc_cntrl_get(void);
extern void intr_mxcc_cntrl_set(u_int value);
extern u_int intr_vik_mcntl_get(void);
extern void intr_vik_mcntl_set(u_int value);
extern u_int intr_bb_sema_status(void);

void
check_options(int master)
{
	struct cpu *cp = CPU;
	u_int cpu_id = cp->cpu_id;
	u_int mxcc_cntl = intr_mxcc_cntrl_get();
	u_int mcntl = intr_vik_mcntl_get();
	u_int action = intr_vik_action_get();
	u_int prescaler = intr_prescaler_get();
	u_int freq = 256 - ((prescaler >> 8) & 0xff);
	u_int sema = intr_bb_sema_status();
	u_int id_sb = sema & 0x3;
	u_int held = (id_sb & 1) ? ((id_sb >> 1) == (cpu_id & 1)) : 0;

	if (option_print == 0) {
		return;	/* don't print anything */
	}

	mutex_enter(&long_print_lock);
	cmn_err(CE_CONT, "?cpu%d:"
		"\tBW Prescaler/XDBus Frequency=%dMHz\n"
		"\tBootbus Semaphore %sheld (0x%x)\n"
		"\tMXCC control register=0x%b\n"
		"\tSuperSPARC MCNTL register=0x%b\n"
		"\tSuperSPARC ACTION register=0x%b\n",
		cpu_id, freq, held ? "" : "NOT ", sema,
		mxcc_cntl, mxcc_cntrl_bits,
		mcntl, vik_mcntl_bits,
		action, vik_action_bits);
	mutex_exit(&long_print_lock);

	/*
	 * I know this is gross, but it's only temporary....
	 */
	if (master)

#if defined(VIKING_BUG_MFAR2) || \
	defined(VIKING_BUG_1151159) || defined(VIKING_BUG_PTP2)
		cmn_err(CE_CONT, "?kernel contains workarounds for "
		    "SuperSPARC bugs: "

#ifdef VIKING_BUG_MFAR2
		    "MFAR2 "
#endif
#ifdef VIKING_BUG_1151159
		    "1151159 "
#endif
#ifdef VIKING_BUG_PTP2
		    "PTP2 "
#endif
		    "\n");
#endif

	/* printf("	BCIPL=%x\n",	bitfield(action, 8, (12-8))); */
	/* printf("	DBC=%x\n",	bitfield(mxcc_cntl, 7, (9 - 7))); */
	/* printf("	Impl=%x\n",	bitfield(mcntl, 28, (32-28))); */
	/* printf("	Ver=%x\n",	bitfield(mcntl, 24, (28-24))); */
	/* printf("	rsvd17=%x\n",	bitfield(mcntl, 17, (18 - 17))); */
	/* printf("	rsvd19=%x\n",	bitfield(mcntl, 23, (24 - 19))); */
	/* printf("	rsvd2=%x\n",	bitfield(mcntl,  2, ( 7 -  2))); */
}

/*
 * indicates lowest revision cpu in this system.
 * used for dynamic workaround handling.
 */
int cpu_revision = 0;

/*
 * keep track of the revisions per cpu for debug or so we
 * can check with adb on a running kernel.
 */
static char cpu_revs[NCPU];

/*
 * SuperSPARC bug workarounds the kernel is able to handle
 */
int mfar2_bug = 0;		/* SuperSPARC bug MFAR2 */

/*
 * SuperSPARC PTP2 bug:
 * PTP2 is the second level page table pointer that is cached by Viking.
 * It is used to access tables of 4K PTEs.  A DEMAP REGION is used to
 * flush the TLB of all entries matching VA 31:24, and can come from
 * outside in systems that support demaps over the bus, or can be an
 * internal TLB FLUSH instruction.
 *
 * TLB entries are all flushed correctly, but the PTP2 is not always
 * invalidated.  PTP2 is only invalidated if VA 31:18 match, which is a
 * stronger condition than REGION DEMAP, that being VA 31:24 match.
 *
 * It is possible that an OS remapping memory could issue a REGION flush,
 * but the old PTP2 could later be used to fetch a PTE from the old page
 * table.
 *
 * CONTEXT, SEGMENT, and PAGE demaps correctly invalidate PTP2.
 */
int viking_ptp2_bug = 0;	/* SuperSPARC bug PTP2 */

/*
 * enable_sm_wa allows manually enabling SuperSPARC
 * bug 1151159, i.e. via /etc/system
 * the name of this variable matches the name used for sun4m
 */
int enable_sm_wa = 0;

/*
 * disable_1151159 provides the ability to disable SuperSPARC
 * bug 1151159 regardless of all other settings.
 *
 * for disable_1151159 to take effect for the boot cpu, it must
 * be set to a non-zero value before the boot cpu executes
 * set_cpu_revision(). For example, setting disable_1151159=1 in
 * /etc/system is too late but setting it via kadb as early as
 * possible would work.
 */
int disable_1151159 = 0;

/*
 * Based on the mask version and implementation fields of the
 * processor status word and on the version and implementation
 * fields of the module control register, try to determine the
 * version of SuperSPARC processor.
 *
 * Due to the way these fields are maintained, exact version
 * determination is impossible.
 *
 * The following table provides the necessary information for
 * determining the SuperSPARC processor version.
 *
 * +-------+----------+------------+-----------+------------+----------+
 * | Rev   | PSR.IMPL | MCNTL.IMPL | MCNTL.VER |  JTAG.CID  | FSR.VER  |
 * |       | (4 bits) | (4 bits)   | (4 bits)  |  (32 bits) | (3 bits) |
 * |       | PSR.VER  |            |           |            |          |
 * |       | (4 bits) |            |           |            |          |
 * +-------+----------+------------+-----------+------------+----------+
 * | 1.x   |   0x40   |     0      |     0     | 0x0000402F |     0    |
 * +-------+----------+------------+-----------+------------+----------+
 * | 2.x   |   0x41   |     0      |     0     | 0x0000402F |     0    |
 * +-------+----------+------------+-----------+------------+----------+
 * | 3.x   |   0x40   |     0      |     1     | 0x1000402F |     0    |
 * +-------+----------+------------+-----------+------------+----------+
 * | 4.x   |   0x40   |     0      |     2     | 0x2000402F |     0    |
 * +-------+----------+------------+-----------+------------+----------+
 * | 5.x   |   0x40   |     0      |     3     | 0x3000402F |     0    |
 * +-------+----------+------------+-----------+------------+----------+
 * | 3.5   |   0x40   |     0      |     4     | 0x1000402F |     0    |
 * +-------+----------+------------+-----------+------------+----------+
 *	SuperSPARC2 (Voyager)
 * +-------+----------+------------+-----------+------------+----------+
 * | 1.x   |   0x40   |     0      |     8     | 0x0001602f |     0    |
 * +-------+----------+------------+-----------+------------+----------+
 * | 2.x   |   0x40   |     0      |     9     | 0x1001602f |     0    |
 * +-------+----------+------------+-----------+------------+----------+
 *
 */

#define	SSPARC_CPU_NAMELEN	PI_TYPELEN
#define	SSPARC_FPU_NAMELEN	PI_FPUTYPE

struct supersparc_version {
	u_int	psr_impl;
	u_int	psr_ver;
	u_int	mcr_impl;
	u_int	mcr_ver;
	char	cpu_name[SSPARC_CPU_NAMELEN];
	char	fpu_name[SSPARC_FPU_NAMELEN];
} ss_version[] = {
	{ 0x00000000, 0x00000000, 0x00000000, 0x00000000, "SuperSPARC",
	"SuperSPARC" },
	{ 0x40000000, 0x00000000, 0x00000000, 0x00000000, "SuperSPARC 1.2",
	"SuperSPARC" },
	{ 0x40000000, 0x01000000, 0x00000000, 0x00000000, "SuperSPARC 2.0",
	"SuperSPARC" },
	{ 0x40000000, 0x00000000, 0x00000000, 0x01000000, "SuperSPARC 3.0",
	"SuperSPARC" },
	{ 0x40000000, 0x00000000, 0x00000000, 0x02000000, "SuperSPARC 4.0",
	"SuperSPARC" },
	{ 0x40000000, 0x00000000, 0x00000000, 0x03000000, "SuperSPARC 5.0",
	"SuperSPARC" },
	{ 0x40000000, 0x00000000, 0x00000000, 0x04000000, "SuperSPARC 3.5",
	"SuperSPARC" },
	{ 0x40000000, 0x00000000, 0x00000000, 0x08000000, "SuperSPARC2 1.0",
	"SuperSPARC" },
	{ 0x40000000, 0x00000000, 0x00000000, 0x09000000, "SuperSPARC2 2.0",
	"SuperSPARC" },
};

/*
 * the following constants correspond to the entries in the
 * ss_version[] array, declared above.
 */
#define	SSPARC_REV_DEFAULT	0	/* default identification entry */
#define	SSPARC_REV_1DOT2	1
#define	SSPARC_REV_2DOT0	2
#define	SSPARC_REV_3DOT0	3
#define	SSPARC_REV_4DOT0	4
#define	SSPARC_REV_5DOT0	5
#define	SSPARC_REV_3DOT5	6
#define	SSPARC2_REV_1DOT0	7
#define	SSPARC2_REV_2DOT0	8


/*
 * set_cpu_version is called for each cpu in the system and
 * determines the version of SuperSPARC processor and which
 * dynamic bug workarounds are necessary.
 * in the event of mixed SuperSPARC versions on an MP system,
 * variables are set to reflect a union of the bugs present.
 */
void
set_cpu_revision(void)
{
	char *pstr1 = "kernel needs workaround for SuperSPARC"
			" bug %d for revision %d cpu %d\n";
	char *pstr2 = "kernel needs workaround for SuperSPARC"
			" %s bug for revision %d cpu %d\n";
	int enable_1151159 = 0;		/* SuperSPARC bug 1151159 */
	u_int psr = getpsr();
	u_int mcr = intr_vik_mcntl_get();
	u_int version;
	int i;

	/*
	 * determine this cpu's SuperSPARC version
	 */
	version = cpu_revs[CPU->cpu_id] = SSPARC_REV_DEFAULT;
	for (i = 0; i < (sizeof (ss_version) / sizeof (ss_version[0])); i++) {
		if ((ss_version[i].psr_impl == (psr & PSR_IMPL)) &&
		    (ss_version[i].psr_ver == (psr & PSR_VER)) &&
		    (ss_version[i].mcr_impl  == (mcr & MCR_IMPL)) &&
		    (ss_version[i].mcr_ver  == (mcr & MCR_VER))) {
			cpu_revs[CPU->cpu_id] = version = i;
			break;
		}
	}

	/*
	 * based on the version of SuperSPARC cpu,
	 * determine which workarounds are necessary
	 */
	switch (version) {
		case SSPARC_REV_1DOT2:
			cmn_err(CE_PANIC, "SuperSparc 1.X is no longer"
			    " supported.");
			/* NOT REACHED */

		case SSPARC_REV_2DOT0:
			mfar2_bug = 1;
			enable_1151159 = 1;
			viking_ptp2_bug = 1;
			break;
		case SSPARC_REV_3DOT0:
			/*
			 * The mfar2  bug is suppose to be fixed in this version
			 * of SuperSPARC but until we have a chance to verify
			 * that it is indeed fixed, let's be safe and make sure
			 * we have the code to handle the workaround enabled.
			 */
			mfar2_bug = 1;
			enable_1151159 = 1;
			viking_ptp2_bug = 1;
			break;
		case SSPARC_REV_4DOT0:
		case SSPARC_REV_5DOT0:
		case SSPARC_REV_3DOT5:
			/*
			 * The mfar2  bug is suppose to be fixed in this version
			 * of SuperSPARC but until we have a chance to verify
			 * that it is indeed fixed, let's be safe and make sure
			 * we have the code to handle the workaround enabled.
			 */
			mfar2_bug = 1;
			viking_ptp2_bug = 1;
			break;
		case SSPARC2_REV_1DOT0:
		case SSPARC2_REV_2DOT0:
		default:
			/* Unknown versions run without workarounds */
			break;
	}

	/*
	 * In the following checks, if the cpu_id is 0 then we assume
	 * we're too early in kernel initialization to call panic()
	 * and thus must use prom_panic().
	 */

#if !defined(VIKING_BUG_MFAR2)
	if (mfar2_bug) {
		if (CPU->cpu_id == cpu0.cpu_id) {
			prom_printf(pstr2, "mfar2", version, CPU->cpu_id);
			prom_panic("\n");
		} else {
			panic(pstr2, "mfar2", version, CPU->cpu_id);
		}
		/* NOTREACHED */
	}
#endif !VIKING_BUG_MFAR2

#if defined(VIKING_BUG_1151159)
	if ((enable_sm_wa || enable_1151159) && (disable_1151159 == 0)) {
		extern void vik_1151159_wa(void);

		vik_1151159_wa();
	}
#endif VIKING_BUG_1151159

#if !defined(VIKING_BUG_PTP2)
	if (viking_ptp2_bug) {
		if (CPU->cpu_id == cpu0.cpu_id) {
			prom_printf(pstr2, "ptp2", version, CPU->cpu_id);
			prom_panic("\n");
		} else {
			panic(pstr2, "ptp2", version, CPU->cpu_id);
		}
		/* NOTREACHED */
	}
#endif

	/*
	 * keep track of lowest revision cpu in this system.
	 */
	if (cpu_revision == 0 || version < cpu_revision)
		cpu_revision = version;
}

/*
 * search 1st level children in devinfo tree for io-unit
 * read CID's from each IOC, return 1 if old one found.
 */

int
need_ioc_workaround(void)
{
	dev_info_t	*dip;
	u_int n_io_unit = 0;

	dip = ddi_root_node();

	for (dip = ddi_get_child(dip); dip; dip = ddi_get_next_sibling(dip)) {
		char	*name;
		int	devid;
		u_int	b;
		u_int	io_unit_base;

		name = ddi_get_name(dip);

		if (strcmp("io-unit", name))
			continue;

		n_io_unit += 1;

		if (prom_getprop((dnode_t)ddi_get_nodeid(dip),
		    PROP_DEVICE_ID, (caddr_t)&devid) == -1) {
			cmn_err(CE_WARN,
				"need_ioc_workaround(): "
				" no %s for %s", PROP_DEVICE_ID, name);
			continue;
		}

		for (b = 0; b < n_xdbus; ++b) {
			if (ioc_get_cid(devid, b) == IOC_CID_DW_BUG)
				return (1);
		}
	}

	/* found nothing */
	if (n_io_unit == 0) {
		cmn_err(CE_WARN, "no io-units found!");
		return (1);	/* assume worst case */
	}
	return (0);
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
