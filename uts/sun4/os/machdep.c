/*
 * Copyright (c) 1990-1992, 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)machdep.c	1.142	94/11/08 SMI"

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

#include <sys/vfs.h>
#include <sys/dnlc.h>
#include <sys/var.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>

#include <sys/dumphdr.h>
#include <sys/bootconf.h>
#include <sys/promif.h>
#include <sys/varargs.h>
#include <sys/modctl.h>
#include <sys/kvtopdata.h>
#include <sys/machsystm.h>

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

#include <vm/hat_sunm.h>

#include <sys/vtrace.h>
#include <sys/instance.h>
#include <sys/aflt.h>

void debug_enter(char *);
void abort_sequence_enter(char *);

#define	MINBUF		8		/* min # of buffers - was 16 */
#define	BUFPERCENT	(100 / 7)	/* % mem for bufs - was 10%, now 7% */

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
int nbuf = 0;
int noprintf = 0;	/* patch to non-zero to suppress kernel printf's */
int msgbufinit = 1;	/* message buffer has been initialized, ok to printf */
int nopanicdebug = 0;	/* 0 = call debugger (if any) on panic, 1 = reboot */

/*
 * Configuration parameters set at boot time.
 */
int dvmasize = 0;		/* usable dvma space- initialized in mlsetup */
extern struct map *dvmamap;	/* map to manage usable dvma space */
extern char DVMA[];

/*
 * These do not belong here, but the implementation of
 * the debugger (and reading crash dumps) require the
 * ability to read these variables directly from the
 * kernel using a namelist.
 */
int _curmod;		/* for use by kadb */
struct modctl modules;	/* head of linked list of modules */
char *default_path;	/* default module loading path */
struct swapinfo *swapinfo;	/* protected by the anon_lock */
/*
 * XXX The following must be declared here for libkvm support.
 */
proc_t *practive;		/* active process chain */
u_int nproc;

/*
 * On Sun4 machines klustsize and maxphys can be this size..
 */

int maxphys = 63 << 10;
int klustsize = 56 << 10;

extern u_int shm_alignment;		/* VAC address consistency modulus */
extern struct memlist *phys_install;

#if defined(SAS) || defined(MPSAS)
extern int _availmem;		/* physical memory available in SAS */
#endif	SAS

int mmu_3level = 0;		/* if non-zero, three level MMU present */

int ioc = 0;			/* I/O cache type (none == 0) */

#ifdef	BCOPY_BUF
int bcopy_buf = 0;		/* block copy buffer present */
#endif	BCOPY_BUF

#ifdef	CLK2
int clk2 = 0;			/* clock type, default to clock type 1 */
#endif	CLK2

/*
 * romp and dvec are set in locore on .entry:
 */
#if !defined(SAS) && !defined(MPSAS)
struct debugvec *dvec;
#endif
union sunromvec *romp = (union sunromvec *)0;

extern u_long pfnumbits;

/*
 * crash dump, libkvm support - see sys/kvtopdata.h for details
 */
extern struct kvtopdata kvtopdata;

#ifndef NCPU
#define	NCPU	1			/* this is a uniprocessor arch */
#endif  /* NCPU */

extern struct cpu	cpu0;		/* first CPU's data */
struct cpu	*cpu[NCPU] = {&cpu0};	/* pointers to all CPUs */
kthread_id_t	clock_thread;	/* clock interrupt thread pointer */

void cnputc(int, int);
void halt(char *);
void start_mon_clock();
void stop_mon_clock();
void write_scb_int(int, struct trapvec *);
void kvm_dup();

extern int ffs(long);

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
va_list  panicargs;


/*
 * This is the state of the world before the file system are sync'd
 * and system state is dumped. Should be put in panic data structure.
 */
label_t	panic_regs;	/* adb looks at these */
kthread_id_t panic_thread;
kthread_id_t panic_clock_thread;
struct cpu panic_cpu;
kmutex_t panic_lock;

static void complete_panic(void);

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
	prom_enter_mon();
#endif	DEBUGGING_KERNEL
	mdboot(A_REBOOT, AD_BOOT, NULL);
}

int
setup_panic(char *fmt, va_list adx)
{
#ifndef LOCKNEST	/* No telling what locks we hold when we call panic */

	extern int conslogging;
	int s;
	kthread_id_t tp;

	s = splzs();
	mutex_enter(&panic_lock);
	if (panicstr) {
		panicstr = fmt;
		panicargs = adx;
		(void) splx(s);
		return (0);
	}

	conslogging = 0;
	start_mon_clock();

	panicstr = fmt;
	panicargs = adx;
	panic_thread = curthread;
	panic_cpu = *curthread->t_cpu;

	/*
	 *  Panic code depends on clock running. If clock thread
	 * is blocked, then allocate new clock thread if possible.
	 */
	if (clock_thread && (clock_thread->t_state == TS_SLEEP)) {
		if (CPU->cpu_intr_thread) {
			tp = CPU->cpu_intr_thread;
			CPU->cpu_intr_thread = tp->t_link;
			tp->t_stk -= SA(MINFRAME);
			tp->t_pri = clock_thread->t_pri;
			tp->t_flag |= T_INTR_THREAD;
			panic_clock_thread = clock_thread;
			clock_thread = tp;
		} else {
			(void) splx(s);
			return (-1);
		}
	}

	/*
	 * If on interrupt stack, allocate new interrupt thread
	 * stack
	 */
	if ((CPU->cpu_on_intr)) {
		if (CPU->cpu_intr_thread) {
			tp = CPU->cpu_intr_thread;
			CPU->cpu_intr_thread = tp->t_link;
			CPU->cpu_intr_stack = tp->t_stk -= SA(MINFRAME);
			CPU->cpu_on_intr = 0;
		} else {
			(void) splx(s);
			return (-1);
		}
	}
	(void) splx(s);
	return (0);
#endif
}


static void
complete_panic(void)
{
	extern void prf(char *, va_list, vnode_t *, int);
	int s;
	static int in_sync = 0;
	static int in_dumpsys = 0;
	extern int sync_timeout;

	s = splzs();

	noprintf = 0;   /* turn printfs on */

	if (curthread->t_cred == NULL)
		curthread->t_cred = kcred;

	printf("panic: ");
	prf(panicstr, panicargs, (vnode_t *)NULL, PRW_CONS | PRW_BUF);
	printf("\n");

	if ((boothowto & RB_DEBUG) && (nopanicdebug == 0))
		debug_enter((char *)NULL);

	if (!in_sync) {
		in_sync = 1;
		vfs_syncall();
	} else {
		/* Assume we paniced while syncing and avoid timeout */
		sync_timeout = 0;
	}

	(void) setjmp(&curthread->t_pcb);	/* save stack ptr for dump */
	panic_regs = curthread->t_pcb;

	if (!in_dumpsys) {
		in_dumpsys = 1;
		dumpsys();
	}
	(void) splx(s);
}


/*
 * allow interrupt threads to run only don't allow them to nest
 * save the current interrupt count
 */
void
panic_hook()
{
	int s;

	if (panic_thread != curthread ||
	    CPU->cpu_on_intr > panic_cpu.cpu_on_intr)
		return;

	s = spl0();
	(void) splx(s);
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
	extern void reset_leaves(void);

	/*
	 * XXX - rconsvp is set to NULL to ensure that output messages
	 * are sent to the underlying "hardware" device using the
	 * monitor's printf routine since we are in the process of
	 * either rebooting or halting the machine.
	 */
	rconsvp = NULL;
	start_mon_clock();
	*INTREG &= ~IR_ENA_CLK10;	/* disable level10 clock interrupts */

#if defined(SAS) || defined(MPSAS)
	asm("t 255");
#else
	/* extreme priority; allow clock interrupts to monitor at level 14 */
	s = spl6();
	reset_leaves();			/* try and reset leaf devices */
	if (fcn == AD_HALT || fcn == AD_POWEROFF) {
		halt((char *)NULL);
		fcn &= ~RB_HALT;
		/* MAYBE REACHED */
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
					"mdboot: invalid function %d\n", fcn);
				bootstr = "";
				break;
			}
		}
		printf("rebooting...\n");
		*INTREG &= ~IR_ENA_INT;		/* disable all interrupts */
		prom_reboot(bootstr);
		/*NOTREACHED*/
	}
	(void) splx(s);
#endif /* SAS */
}

#if !defined(SAS) && !defined(MPSAS)
/*
 * Machine-dependent portion of dump-checking;
 * verify that a physical address is valid.
 */
int
dump_checksetbit_machdep(addr)
	u_longlong_t	addr;
{
#if	defined(SAS) || defined(MPSAS)
	return (addr < _availmem);
#else
	struct memlist  *pmem;

	for (pmem = phys_install; pmem; pmem = pmem->next) {
		if (pmem->address <= addr &&
		    addr < (pmem->address + pmem->size))
			return (1);
	}
	return (0);
#endif
}

/*
 * Dump a page frame.
 */
static caddr_t dumpkaddr = (caddr_t)-1;

int
dump_page(vp, pg, bn)
	struct vnode *vp;
	int pg;
	int bn;
{
	register int err = 0;
	struct pte pte;

	/*
	 * this used to be:
	 *
	 *	addr = &DVMA[mmu_ptob(dvmasize) - MMU_PAGESIZE];
	 *
	 * which is a total crock.
	 */
	if (dumpkaddr == (caddr_t)-1) {
		extern u_long getdvmapages(int, u_long, u_long, u_int,
				u_int, int);
		u_long daddr;

		daddr = getdvmapages(2, 0, 0, (u_int) -1, (u_int) -1, 0);
		if (daddr == 0)
			return (ENOMEM);
		dumpkaddr = (caddr_t)daddr;
	}

	pte = mmu_pteinvalid;
	pte.pg_v = 1;
	pte.pg_prot = KW;
	pte.pg_pfnum = pg;
	mmu_setpte(dumpkaddr, pte);
	err = VOP_DUMP(vp, dumpkaddr, bn, ctod(1));
	vac_pageflush(dumpkaddr);

	return (err);
}
int
dump_final(struct vnode *vp)
{
	return (NULL);
}

/*
 * Dump an arbitrary kernel-space object.
 */
int
dump_kaddr(vp, kaddr, bn, count)
	struct vnode *vp;
	caddr_t kaddr;
	int bn;
	int count;
{
	register int err = 0;
	struct pte pte[2], tpte;
	register int offset;

	/*
	 * this used to be:
	 *
	 *	addr = &DVMA[mmu_ptob(dvmasize) - 2 * MMU_PAGESIZE];
	 *
	 * which is a total crock.
	 */
	if (dumpkaddr == (caddr_t)-1) {
		extern u_long getdvmapages(int, u_long, u_long, u_int,
				u_int, int);
		u_long daddr;

		daddr = getdvmapages(2, 0, 0, (u_int) -1, (u_int) -1, 0);
		if (daddr == 0)
			return (ENOMEM);
		dumpkaddr = (caddr_t)daddr;
	}

	offset = (u_int)kaddr & MMU_PAGEOFFSET;
	pte[0] = mmu_pteinvalid;
	pte[1] = mmu_pteinvalid;
	pte[1].pg_v = 1;
	pte[1].pg_prot = KW;
	mmu_getpte(kaddr, &tpte);
	pte[1].pg_pfnum = tpte.pg_pfnum;

	while (count > 0 && !err) {
		pte[0] = pte[1];
		mmu_setpte(dumpkaddr, pte[0]);
		mmu_getpte(kaddr + MMU_PAGESIZE, &tpte);
		pte[1].pg_pfnum =
		    (tpte.pg_v && tpte.pg_type == OBMEM) ? tpte.pg_pfnum : 0;
		mmu_setpte(dumpkaddr + MMU_PAGESIZE, pte[1]);
		err = VOP_DUMP(vp, dumpkaddr + offset, bn, ctod(1));
		bn += ctod(1);
		count -= ctod(1);
		vac_pageflush(dumpkaddr);
		vac_pageflush(dumpkaddr + MMU_PAGESIZE);
		kaddr += MMU_PAGESIZE;
	}

	return (err);
}

#else /* SAS */
dump_page() {}
dump_checksetbit_machdep() { return (1); }
dump_kaddr() {}

#endif /* !SAS */


/*
 * Halt the machine and return to the monitor
 */
void
halt(s)
	char *s;
{
#if defined(SAS) || defined(MPSAS)
	if (s)
		printf("(%s) ", s);
	printf("Halted\n\n");
	asm("t 255");
#else
	start_mon_clock();
	*INTREG &= ~IR_ENA_CLK10;	/* disable level10 clock interrupts */
	if (s)
		prom_printf("(%s) ", s);
	prom_exit_to_mon();
	/*NOTREACHED*/
#endif SAS
}



/*
 *	Machine dependent abort sequence handling
 */
void
abort_sequence_enter(char *msg)
{
	debug_enter(msg);
}


/* XXX This should get moved to a sun/common file */
/*
 * Enter debugger.  Called when the user types L1-A or break or whenever
 * code wants to enter the debugger and possibly resume later.
 * If the debugger isn't present, enter the PROM monitor.
 */
void
debug_enter(char *msg)
{
	int s;
	label_t debug_save;

	if (msg)
		prom_printf("%s\n", msg);
	s = splzs();
	if (boothowto & RB_DEBUG) {
		/* We're at splzs already */
		debug_save = curthread->t_pcb;
		(void) setjmp(&curthread->t_pcb);
		{ func_t callit = (func_t)dvec; (*callit)(); }
		curthread->t_pcb = debug_save;
	} else {
		prom_enter_mon();
	}
	(void) splx(s);
}

/*
 * Given a pte, return an address and a type based
 * on the pte. The address takes on a set of units
 * based on the type of the pte.
 */

int
pte2atype(void *p, u_long offset, u_long *addrp, u_int *type)
{
	u_long endpfnum;
	register struct pte *pte = p;

	endpfnum = (u_long) pte->pg_pfnum + mmu_btop(offset);
	/*
	 * XXX: should check for page frame overflow for OBMEM/OBIO?
	 */
	if (pte->pg_type == OBMEM) {
		/*
		 * The address is the physical page frame number
		 */
		*addrp = endpfnum;
		*type = SP_OBMEM;
	} else if (pte->pg_type == OBIO) {
		/*
		 * The address is the physical page frame number
		 */
		*addrp = endpfnum;
		*type = SP_OBIO;

	} else if (pte->pg_type == VME_D16 || pte->pg_type == VME_D32) {
		int wide = pte->pg_type == VME_D32;
		/*
		 * VMEA16 space is stolen from the top 64k of VMEA24 space,
		 * and VMEA24 space is stolen from the top 24mb of VMEA32
		 * space.
		 */
		endpfnum = mmu_ptob(endpfnum & pfnumbits);
		if (endpfnum >= (u_int)VME16_BASE) {
			*type = (wide)? SP_VME16D32 : SP_VME16D16;
			endpfnum &= ((1<<16) - 1);
		} else if (endpfnum >= (u_int)VME24_BASE) {
			*type = (wide)? SP_VME24D32 : SP_VME24D16;
			endpfnum &= ((1<<24) - 1);
		} else {
			*type = (wide)? SP_VME32D32 : SP_VME32D16;
		}
		*addrp = endpfnum;
	} else {
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

char mon_clock_on = 0;			/* disables profiling */
u_int mon_memerr = 0;			/* monitor memory register setting */

void
start_mon_clock()
{
	extern trapvec mon_clock14_vec;

#if !defined(SAS) && !defined(MPSAS)
	if (!mon_clock_on) {
		mon_clock_on = 1;		/* disable profiling */
		write_scb_int(14, &mon_clock14_vec);	/* install mon vector */
		set_clk_mode(IR_ENA_CLK14, 0);	/* enable level 14 clk intr */
		if ((cputype == CPU_SUN4_260 ||
		    cputype == CPU_SUN4_470 ||
		    cputype == CPU_SUN4_330) && mon_memerr)
			MEMERR->me_err = mon_memerr;
	}
#endif /* !SAS */
}

void
stop_mon_clock()
{
	extern trapvec kclock14_vec;

#if !defined(SAS) && !defined(MPSAS)
	if (mon_clock_on) {
		mon_clock_on = 0;		/* enable profiling */
		set_clk_mode(0, IR_ENA_CLK14);	/* disable level 14 clk intr */
		write_scb_int(14, &kclock14_vec); /* install kernel vector */
		if (cputype == CPU_SUN4_260 || cputype == CPU_SUN4_470) {
			mon_memerr = MEMERR->me_err;
			MEMERR->me_err = EER_INTENA | EER_CE_ENA;
		}
	}
#endif /* !SAS */
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
write_scb_int(level, ip)
	register int level;
	struct trapvec *ip;
{
	register int s;
	register u_int savepte;
	register trapvec *sp;

	sp = &scb.interrupts[level - 1];
	s = spl8();

	/* save old mapping */
	savepte = map_getpgmap((caddr_t)sp);

	/* allow writes */
	map_setpgmap((caddr_t)sp, (u_int)(PG_V | PG_KW | savepte & PG_PFNUM));

	/* write out new vector code */
	*sp = *ip;

	/* flush out the write since we are changing mappings */
	vac_flush((caddr_t)sp, sizeof (struct trapvec));

	/* restore old mapping */
	(void) map_setpgmap((caddr_t)sp, savepte);

	(void) splx(s);
}

#if !defined(SAS) && !defined(MPSAS)
/*
 * Handler for monitor vector cmd -
 * For now we just implement the old "g0" and "g4"
 * commands and a printf hack.
 */
int v_handler_sync = 0;
void
v_handler(addr, str)
	int addr;
	char *str;
{
	struct scb *oldtbr;
	int s;

	curthread_setup(&cpu0);
	s = splhigh();
	oldtbr = set_tbr(&scb);
	(void) splx(s);

	switch (*str) {
	case '\0':
		/*
		 * No (non-hex) letter was specified on
		 * command line, use only the number given
		 */
		switch (addr) {
		case 0:		/* old g0 */
		case 0xd:	/* 'd'ump short hand */
			panic("zero");
			/*NOTREACHED*/
		case 4:		/* old g4 */
			tracedump();
			break;

		default:
			goto err;
		}
		break;

	case 'p':		/* 'p'rint string command */
	case 'P':
		prom_printf("%s\n", (char *)addr);
		break;

	case '%':		/* p'%'int anything a la printf */
		prom_printf(str, addr);
		prom_printf("\n");
		break;

	case 't':		/* 't'race kernel stack */
	case 'T':
		tracedump();
		break;

	case 'u':		/* d'u'mp hack ('d' look like hex) */
	case 'U':
		if (addr == 0xd) {
			panic("zero");
		} else
			goto err;
		break;

	default:
	err:
		prom_printf("Don't understand 0x%x '%s'\n", addr, str);
	}
	s = splhigh();
	(void) set_tbr(oldtbr);
	(void) splx(s);
}
#endif !SAS

/*
 * Duplicate kernel into every context.  From this point on,
 * adjustments to the mmu will automatically copy kernel changes.
 * Use the ROM to do this copy to avoid switching to unmapped
 * context.
 */
void
kvm_dup()
{
	register int c;
	register caddr_t va;

#if defined(SAS) && !defined(SIMUL) || defined(MPSAS)
	simdupctxt0();		/* make all segments the same as this one */
#else
	if (mmu_3level) {
		for (c = 1; c < NCTXS; c++) {
			register int i;
			va = NULL;
			for (i = 0; i < NSMGRPPERCTX; i++) {
				prom_setcxsegmap(c, va,
				    (mmu_getsmg(va))->smg_num);
				va += SMGRPSIZE;
			}
		}
	} else {
		for (c = 1; c < NCTXS; c++) {
			for (va = (caddr_t)0; va < hole_start; va += PMGRPSIZE)
				prom_setcxsegmap(c, va,
				    (mmu_getpmg(va))->pmg_num);
			for (va = hole_end; va != NULL;  va += PMGRPSIZE)
				prom_setcxsegmap(c, va,
				    (mmu_getpmg(va))->pmg_num);
		}
	}
#endif SAS
}



#ifdef	VAC
void
vac_init()
{
	vac_tagsinit();
}
#endif

#if !defined(SAS) && !defined(MPSAS)

/*
 * Console put and get character routines.
 */
/*ARGSUSED2*/
void
cnputs(char *buf, u_int bufsize, int device_in_use)
{
	prom_writestr(buf, bufsize);
}

/*ARGSUSED1*/
void
cnputc(register int c, int device_in_use)
{
	if (c == '\n')
		prom_putchar('\r');
	prom_putchar(c);
}

int
cngetc()
{
	register int c;

	while ((c = prom_mayget()) == -1)
		;
	return (c);
}

/*
 * Get a character from the console.
 */
getchar()
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
gets(cp)
	char *cp;
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

		case '@':	/* used in physical device names */
		default:
			*lp++ = (char)c;
		}
	}
}

#endif !SAS

#ifdef IOC

static int
get_pagenum(addr)
unsigned int addr;
{
	return ((addr&0xFE000) >> 13);
}

static u_int
get_ioctag(addr)
unsigned int addr;
{
	return (*(u_int *)(IOC_TAGS_ADDR + get_pagenum(addr)*32));
}

void
check_ioctag(addr)
	caddr_t addr;
{
	u_int ioc_tag;
	ioc_tag = get_ioctag((u_int)addr);
	if (ioc_tag & MODIFIED_IOC_LINE) {
		cmn_err(CE_PANIC,
		    "IOCACHE TAG IS MODIFIED? address 0x%x ioc tag 0x%x",
		    addr, ioc_tag);
		/* NOTREACHED */
	}
}

#endif	/* IOC */

/*
 * Flush all user lines in VAC.
 */
void
vac_flushallctx()
{
	register int i;
	extern struct	ctx	*ctxs;

	ASSERT(MUTEX_HELD(&sunm_mutex));

	if (ctxs[map_getctx()].c_clean)
		return;

	/*
	 * We mark them clean first, we have the hat mutex at
	 * this point so no one can allocate a context before
	 * we finish flushing them below. We can swtch to a new
	 * process which has a context and resume will mark the
	 * context of process as dirty by zeroing the clean flag.
	 */
	for (i = 1; i < NCTXS; i++)	/* skip kernel context */
		ctxs[i].c_clean = 1;

	vac_usrflush();
}

/*
 * DVMA pages are stored in a resource map as page numbers in the range
 * 1..dvmasize (inclusive).
 *
 * We assume that the caller has verified that addrlo and addrhi
 * are correctly ordered and between DVMA[0..mmu_ptob(dvmasize)]
 * (actually, having an addrlo < DVMA is assumed to be == DVMA)
 */
/* #define	DEBUG_GETDVMA	*/
#ifdef	DEBUG_GETDVMA
static int debug_getdvma;
#define	GPRINTF	if (debug_getdvma) printf
#endif

#define	ALIGN_REQUIRED(align)		(align != (u_int) -1)
#define	COUNTER_RESTRICTION(cntr)	(cntr != (u_int) -1)
#define	SEG_ALIGN(addr, seg, base)	(mmu_btop(((((addr) + (u_long) 1) +  \
					    (seg)) & ~(seg)) - (base)))

u_long
getdvmapages(int npages, u_long addrlo, u_long addrhi, u_int align,
	u_int cntr, int cansleep)
{
	u_long addr, ahi, alo, amax, amin, aseg;

	/*
	 * Convert lo && hi addresses into 1-based page offsets suitable
	 * for comparisons to entries managed by dvmamap. Note that the
	 * ahi will be the non-inclusive upper limit while the passed
	 * addrhi was the inclusive upper limit.
	 */

	if (addrlo < (u_long) DVMA)
		alo = 0;
	else
		alo = mmu_btop(addrlo - (u_long) DVMA);

	amax = mmu_btop((addrhi - (u_long) DVMA) + (u_long) 1);
	/*
	 * If we have a counter restriction we adjust ahi to the
	 * minimum of the maximum address and the end of the
	 * current segment. Actually it is the end+1 since ahi
	 * is always excluding. We then allocate dvma space out
	 * of a segment instead from the whole map. If the allocation
	 * fails we try the next segment.
	 */
	if (COUNTER_RESTRICTION(cntr)) {
		if (addrlo < (u_long) DVMA) {
			ahi = SEG_ALIGN((u_long) DVMA, cntr, (u_long) DVMA);
		} else {
			ahi = SEG_ALIGN(addrlo, cntr, (u_long) DVMA);
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
	 * If we have alo > 0 or ahi < dvmasize,
	 * then we have a 'constrained' allocation
	 * and we have to search dvmamap for a piece
	 * that fits our constraints.
	 *
	 * Furthermore, if we have a specified favorite
	 * alignment, we also search for a piece to fit
	 * that favorite alignment.
	 */

	if (alo > (u_long)0 || ahi < dvmasize || ALIGN_REQUIRED(align) ||
	    COUNTER_RESTRICTION(cntr)) {
		register struct map *mp;
		register u_int mask;

		if (ALIGN_REQUIRED(align)) {
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
				if (ALIGN_REQUIRED(align)) {
					u_long oaddr = addr;
					addr = (addr & ~mask) + align;
					if (addr < oaddr)
						addr += mask + 1;
#ifdef	DEBUG_GETDVMA
					GPRINTF(" algn %x addr %x.%x->%x.%x",
					    mmu_ptob(align), oaddr,
					    mmu_ptob(oaddr),
					    addr, mmu_ptob(addr));
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
			/*
			 * Let rmget do the rest of the work.
			 */
			addr = rmget(dvmamap, (long)npages, addr);
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
			if (ALIGN_REQUIRED(align)) {
				/*
				 * try it again with unaligned mappings. This
				 * is important for mappings that could
				 * not be aliased with the first scan and
				 * that have specific alo/ahi limits.
				 * (ethernet descriptor page on 4/490)
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
		} else {
			addr = rmalloc(dvmamap, npages);
		}
	}
	if (addr) {
		addr = mmu_ptob(addr) + (u_long) DVMA;
	}

	return (addr);
}

void
putdvmapages(u_long addr, int npages)
{
	addr = mmu_btop(addr - (u_long)DVMA);
	rmfree(dvmamap, (long)npages, addr);
}

/*
 * Allocate threads and stacks for interrupt handling.
 */
#define	NINTR_THREADS	(LOCK_LEVEL-1)	/* number of interrupt threads */
#ifdef REDCHECK
#define	INTR_STACK_SIZE	(roundup(8192+PAGESIZE, PAGESIZE))
#else
#define	INTR_STACK_SIZE	(roundup(8192, PAGESIZE))
#endif /* REDCHECK */

void
init_intr_threads()
{
	int i;

	for (i = 0; i < NINTR_THREADS; i++)
		(void) thread_create_intr(CPU);
	CPU->cpu_intr_stack = (caddr_t)segkp_get(segkp, INTR_STACK_SIZE,
		KPD_HASREDZONE | KPD_NO_ANON | KPD_LOCKED) +
		INTR_STACK_SIZE - SA(MINFRAME);
}

void
init_clock_thread()
{
	kthread_id_t tp;

	/*
	 * Create clock interrupt thread.
	 * The state is initially TS_FREE.  Think of this thread on
	 * a private free list until it runs.
	 */
	tp = thread_create(NULL, INTR_STACK_SIZE, NULL, NULL, 0,
		&p0, TS_FREE, 0);
	tp->t_stk -= SA(MINFRAME);
	tp->t_flag |= T_INTR_THREAD;	/* for clock()'s tick checking */
	tp->t_pri = v.v_nglobpris - 1 - LOCK_LEVEL + CLOCK_LEVEL;
	clock_thread = tp;
}

/*
 * Called from dumpsys() to ensure the kvtopdata is in the dump
 *
 * XXX	Not entirely convinced we need to do this specially ..
 */
void
dump_kvtopdata(void)
{
	caddr_t		i, j;
	struct pte	tpte;

	i = (caddr_t)(((u_int)&kvtopdata) & MMU_PAGEMASK);
	for (j = (caddr_t)&kvtopdata + sizeof (kvtopdata); i < j;
	    i += MMU_PAGESIZE) {
		mmu_getpte(i, &tpte);
		dump_addpage(tpte.pg_pfnum);
	}
}

/*
 * set_last_idle_cpu is non-empty only for sun4m/MP
 */
void
set_last_idle_cpu()
{
}

/*
 * XXX These probably ought to live somewhere else
 * XXX They are called from mem.c
 */

/*
 * Convert page frame number to an OBMEM page frame number
 * (i.e. put in the type bits)
 */
int
impl_obmem_pfnum(int pf)
{
	return (PGT_OBMEM | pf);
}

/*
 * A run-time interface to the MAKE_PFNUM macro.
 */
impl_make_pfnum(struct pte *pte)
{
	return (MAKE_PFNUM(pte));
}

/*
 * The next 3 entry points are for support for drivers which need to
 * be able to register a callback for an async fault, currently only nvsimm
 * drivers do this, and they exist only on sun4m and sun4d
 */

/*ARGSUSED*/
int
aflt_get_iblock_cookie(dev_info_t *dip, int fault_type,
    ddi_iblock_cookie_t *iblock_cookiep)
{
	return (AFLT_NOTSUPPORTED);
}

/*ARGSUSED*/
int
aflt_add_handler(dev_info_t *dip, int fault_type, void **hid,
    int (*func)(void *, void *), void *arg)
{
	return (AFLT_NOTSUPPORTED);
}

/*ARGSUSED*/
int
aflt_remove_handler(void *hid)
{
	return (AFLT_NOTSUPPORTED);
}
