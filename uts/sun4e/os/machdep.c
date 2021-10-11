/*
 * Copyright (c) 1990-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)machdep.c	1.92	94/11/08 SMI"

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/reg.h>
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
#include <sys/varargs.h>
#include <sys/obpdefs.h>
#include <sys/promif.h>
#include <sys/comvec.h>		/* still needed ? */
#include <sys/modctl.h>		/* for "procfs" hack */
#include <sys/kvtopdata.h>

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
#include <sys/buserr.h>
#include <sys/enable.h>
#include <sys/auxio.h>
#include <sys/trap.h>
#include <sys/eccreg.h>
#include <sys/vme.h>

#ifdef IOC
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

#include <vm/hat_sunm.h>

#include <sys/vtrace.h>
#include <sys/autoconf.h>
#include <sys/instance.h>
#include <sys/aflt.h>
#include <sys/mem.h>

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
int dvmasize = 120;		/* usable dvma space */
int xdvmasize = SYSPTSIZE - 1;	/* usable xdvma space */

/*
 * On Sun-4e machines klustsize and maxphys can be this size..
 * Fix for SVVS malloc test. sun4c 124 << 10, but 4e has 8k page.
 */
int maxphys = 63 << 10;
int klustsize = 56 << 10;

#if defined(SAS) || defined(MPSAS)
extern int _availmem;		/* physical memory available in SAS */
#endif /* SAS || MPSAS */

int mmu_3level = 0;		/* non-zero if three level MMU present */

#ifdef IOC
int ioc = 0;			/* I/O cache type (none == 0) */
#endif /* IOC */

#ifdef BCOPY_BUF
int bcopy_buf = 0;		/* block copy buffer present */
#endif /* BCOPY_BUF */

int	vac_hashwusrflush;	/* set to 1 if cache has HW user flush */

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
 * FORTH monitor gives us romp as a variable
 */
#if !defined(SAS) && !defined(MPSAS)
union sunromvec *romp;
struct debugvec *dvec;
#else
union sunromvec *romp = (struct sunromvec *)0;
#endif

#ifndef NCPU
#define	NCPU	1			/* this is a uniprocessor arch */
#endif /* NCPU */

extern struct cpu	cpu0;		/* first CPU's data */
struct cpu	*cpu[NCPU] = {&cpu0};	/* pointers to all CPUs */
kthread_id_t	clock_thread;		/* clock interrupt thread pointer */

/*
 * Processor interrupt levels equivalent to
 * the given VME interrupt level.
 */
#define	VME_LEVEL1	2
#define	VME_LEVEL2	3
#define	VME_LEVEL3	5
#define	VME_LEVEL4	8
#define	VME_LEVEL5	9
#define	VME_LEVEL6	11
#define	VME_LEVEL7	13

/* vme_levels mask == 0x2b2c */
int	vme_levels =	(1 << VME_LEVEL1) |
			(1 << VME_LEVEL2) |
			(1 << VME_LEVEL3) |
			(1 << VME_LEVEL4) |
			(1 << VME_LEVEL5) |
			(1 << VME_LEVEL6) |
			(1 << VME_LEVEL7);

int	vme_iackaddr[] = {	0, 0, VME_IACK1, VME_IACK2, 0, VME_IACK3,
				0, 0, VME_IACK4, VME_IACK5, 0, VME_IACK6,
				0, VME_IACK7, 0, 0 };

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
#endif
	mdboot(A_REBOOT, AD_BOOT, NULL);
}

int
setup_panic(char *fmt, va_list adx)
{
#ifndef LOCKNEST	/* No telling what locks we hold when we call panic */

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

	if (((boothowto & RB_DEBUG) || obpdebug) && (nopanicdebug == 0))
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
panic_hook(void)
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

	/*
	 * XXX - rconsvp is set to NULL to ensure that output messages
	 * are sent to the underlying "hardware" device using the
	 * monitor's printf routine since we are in the process of
	 * either rebooting or halting the machine.
	 */
	rconsvp = NULL;
	start_mon_clock();

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
				    "mdboot: invalid function %d", fcn);
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
dump_checksetbit_machdep(u_longlong_t addr)
{
	struct memlist *pmem;

	for (pmem = phys_install; pmem; pmem = pmem->next) {
		if (pmem->address <= addr &&
		    addr < (pmem->address + pmem->size))
			return (1);
	}
	return (0);
}

/*
 * Dump a page frame.
 */
int
dump_page(struct vnode *vp, int pg, int bn)
{
	register caddr_t addr;
	register int err;
	struct pte pte;

	addr = &DVMA[mmu_ptob(dvmasize) - MMU_PAGESIZE];
	pte = mmu_pteinvalid;
	pte.pg_v = 1;
	pte.pg_prot = KW;
	pte.pg_pfnum = pg;
	mmu_setpte(addr, pte);
	err = VOP_DUMP(vp, addr, bn, ctod(1));
	vac_pageflush(addr);

	return (err);
}

int
dump_final(struct vnode *vp)
{
	return (NULL);
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
	register caddr_t addr;
	register int err = 0;
	struct pte pte[2], tpte;
	register int offset;

	offset = (u_int)kaddr & MMU_PAGEOFFSET;
	addr = &DVMA[mmu_ptob(dvmasize) - 2 * MMU_PAGESIZE];
	pte[0] = mmu_pteinvalid;
	pte[1] = mmu_pteinvalid;
	pte[1].pg_v = 1;
	pte[1].pg_prot = KW;
	mmu_getpte(kaddr, &tpte);
	pte[1].pg_pfnum = tpte.pg_pfnum;

	while (count > 0 && !err) {
		pte[0] = pte[1];
		mmu_setpte(addr, pte[0]);
		mmu_getpte(kaddr + MMU_PAGESIZE, &tpte);
		pte[1].pg_pfnum = (tpte.pg_v && tpte.pg_type == OBMEM) ?
					tpte.pg_pfnum : 0;
		mmu_setpte(addr + MMU_PAGESIZE, pte[1]);
		err = VOP_DUMP(vp, addr + offset, bn, ctod(1));
		bn += ctod(1);
		count -= ctod(1);
		vac_pageflush(addr);
		vac_pageflush(addr + MMU_PAGESIZE);
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


/*
 * Halt the machine and return to the monitor
 */
void
halt(char *s)
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
	stop_mon_clock();
	*INTREG |= IR_ENA_CLK10;	/* enable level10 clock interrupts */
#endif /* SAS */
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
#ifdef	here_just_for_comment_different_from_sun4
	} else if (pte->pg_type == VME_D16 || pte->pg_type == VME_D32) {
		int wide = pte->pg_type == VME_D32;
		/*
		 * VMEA16 space is stolen from the top 64k of VMEA24 space,
		 * and VMEA24 space is stolen from the top 24mb of VMEA32
		 * space.
		 */
		endpfnum = mmu_ptob(endpfnum & pfnumbits);
		if (endpfnum >= VME16_BASE) {
			*type = (wide)? SP_VME16D32 : SP_VME16D16;
			endpfnum &= ((1<<16) - 1);
		} else if (endpfnum >= VME24_BASE) {
			*type = (wide)? SP_VME24D32 : SP_VME24D16;
			endpfnum &= ((1<<24) - 1);
		} else {
			*type = (wide)? SP_VME32D32 : SP_VME32D16;
		}
		*addrp = endpfnum;
#endif
	} else {
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

char mon_clock_on = 0;			/* disables profiling */

void
start_mon_clock(void)
{
#if !defined(SAS) && !defined(MPSAS)
	if (!mon_clock_on) {
		mon_clock_on = 1;		/* disable profiling */
		write_scb_int(14, &mon_clock14_vec);	/* install mon vector */
		set_clk_mode(IR_ENA_CLK14, 0);	/* enable level 14 clk intr */
	}
#endif /* !SAS */
}

void
stop_mon_clock(void)
{
#if !defined(SAS) && !defined(MPSAS)
	if (mon_clock_on) {
		mon_clock_on = 0;		/* enable profiling */
		set_clk_mode(0, IR_ENA_CLK14);	/* disable level 14 clk intr */
		write_scb_int(14, &kclock14_vec); /* install kernel vector */
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
write_scb_int(register int level, struct trapvec *ip)
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
void
v_handler(int addr, char *str)
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

#define	isspace(c)	((c) == ' ' || (c) == '\t' || (c) == '\n' || \
			    (c) == '\r' || (c) == '\f' || (c) == '\013')

static void
parse_str(char *str, char *args[])
{
	register int i;

	while (*str && isspace(*str))
		str++;
	i = 0;
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
 * Handler for OBP > V0 monitor vector cmd -
 * Currently, the PROM only uses the sync subcommand, but an interactive
 * interface is provided to allow the user to pass in any arbitrary string.
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
	register int	func;
	struct scb *oldtbr;
	int s;

	curthread_setup(&cpu0);

	/*
	 * Since about prom rev 2.10, the level14 clock is
	 * enabled when we come in here.  Turn it off so we don't
	 * get hung up not handling level 14 interrupts.
	 */
	set_clk_mode(0, IR_ENA_CLK14);

	s = splhigh();
	oldtbr = set_tbr(&scb);
	(void) splx(s);

	parse_str(str, sargv);
	func = -1;
	for (cp = (struct cmd_info *)vx_cmd;
	    cp < (struct cmd_info *)vx_cmd_end;
	    cp++) {
		if (strcmp(sargv[0], cp->cmd) == 0) {
			func = cp->func;
			break;
		}
	}

	switch (func) {
	case 0:		/* sync */
		panic("zero");
		/*NOTREACHED*/

	default:
		prom_printf("Don't understand '%s'\n", str);
	}
	s = splhigh();
	(void) set_tbr(oldtbr);
	(void) splx(s);
}
#endif /* !SAS */

/*
 * Duplicate kernel into every context.  From this point on,
 * adjustments to the mmu will automatically copy kernel changes.
 * Use the ROM to do this copy to avoid switching to unmapped
 * context.
 */
void
kvm_dup(void)
{
	register int c;
	register caddr_t va;

#if defined(SAS) && !defined(SIMUL) || defined(MPSAS)
	simdupctxt0();		/* make all segments the same as this one */
#else
#ifdef	MMU_3LEVEL
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
	} else
#endif /* MMU_3LEVEL */
	{
		for (c = 1; c < NCTXS; c++) {
			for (va = (caddr_t)0; va < hole_start;
			    va += PMGRPSIZE)
				prom_setcxsegmap(c, va,
				    (mmu_getpmg(va))->pmg_num);
			for (va = hole_end; va != NULL;  va += PMGRPSIZE)
				prom_setcxsegmap(c, va,
				    (mmu_getpmg(va))->pmg_num);
		}
	}
#endif /* SAS */
}

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

static int
cngetc(void)
{
	register int c;

	while ((c = prom_mayget()) == -1)
		;
	return (c);
}

/*
 * Get a character from the console.
 *
 * XXX	There's no need for both cngetc() and getchar() -- merge 'em
 * XXX	And it looks as if gets() should be doing the echoing
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

#endif /* !SAS */

/*
 * set delay constant for usec_delay()
 * delay ~= (usecs * (Cpudelay * 2 + 3) + 8) / mips
 *  => Cpudelay = ceil((mips - 3) / 2)
 * XXX should be in sparc_subr.s with usec_delay()
 */
void
setdelay(int mips)
{
	Cpudelay = 0;
	if (mips > 3)
		Cpudelay = (mips - 2) >> 1;
}

/*
 * Flush all user lines in VAC.
 */
void
vac_flushallctx(void)
{
	register int i;

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
 * 0..mapsize-1 (inclusive). On a sun4c/e we use dvmamap for small/fast
 * mappings and kernelmap for larger mappings. (see: os/rootnex.c)
 *
 * We assume that the caller has verified that addrlo and addrhi
 * are correctly ordered.
 * (actually, having an addrlo < mapbaseaddr is assumed to
 * be == mapbaseaddr)
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
	u_int cntr, int cansleep, struct map *map, int mapsize,
	u_long mapbaseaddr)
{
	u_long addr, ahi, alo, amax, amin, aseg;

	/*
	 * Convert lo && hi addresses into 1-based page offsets suitable
	 * for comparisons to entries managed by map. Note that the
	 * ahi will be the non-inclusive upper limit while the passed
	 * addrhi was the inclusive upper limit.
	 */

	if (addrlo < mapbaseaddr)
		alo = 0;
	else
		alo = mmu_btop(addrlo - mapbaseaddr);

	amax = mmu_btop((addrhi - mapbaseaddr) + (u_long) 1);
	/*
	 * If we have a counter restriction we adjust ahi to the
	 * minimum of the maximum address and the end of the
	 * current segment. Actually it is the end+1 since ahi
	 * is always excluding. We then allocate dvma space out
	 * of a segment instead from the whole map. If the allocation
	 * fails we try the next segment.
	 */
	if (COUNTER_RESTRICTION(cntr)) {
		if (addrlo < mapbaseaddr) {
			ahi = SEG_ALIGN(mapbaseaddr, cntr, mapbaseaddr);
		} else {
			ahi = SEG_ALIGN(addrlo, cntr, mapbaseaddr);
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
	 * If we have alo > 0 or ahi < mapsize,
	 * then we have a 'constrained' allocation
	 * and we have to search map for a piece
	 * that fits our constraints.
	 *
	 * Furthermore, if we have a specified favorite
	 * alignment, we also search for a piece to fit
	 * that favorite alignment.
	 */

	if (alo > (u_long)0 || ahi < mapsize || ALIGN_REQUIRED(align) ||
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
		mutex_enter(&maplock(map));
again:
		for (mp = mapstart(map); mp->m_size; mp++) {
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
			addr = rmget(map, (long)npages, addr);
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
				 */
				align = (u_int) -1;
				goto again;
			}
		}
		if (addr == 0 && cansleep) {
#ifdef	DEBUG_GETDVMA
			GPRINTF("getdvmapages: sleep on constrained alloc\n");
#endif
			mapwant(map) = 1;
			cv_wait(&map_cv(map), &maplock(map));
			goto again;
		}
		mutex_exit(&maplock(map));
	} else {
		if (cansleep) {
			addr = rmalloc_wait(map, npages);
		} else {
			addr = rmalloc(map, npages);
		}
	}
	if (addr) {
		addr = mmu_ptob(addr) + mapbaseaddr;
	}

	return (addr);
}

void
putdvmapages(u_long addr, int npages, struct map *map, u_long mapbaseaddr)
{
	addr = mmu_btop(addr - mapbaseaddr);
	rmfree(map, (long)npages, addr);
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

/*ARGSUSED*/
void
init_intr_threads(struct cpu *cp)
{
	int i;

	for (i = 0; i < NINTR_THREADS; i++)
		(void) thread_create_intr(CPU);

	CPU->cpu_intr_stack = (caddr_t)segkp_get(segkp, INTR_STACK_SIZE,
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
	tp = thread_create(NULL, INTR_STACK_SIZE, NULL, NULL, 0,
		&p0, TS_FREE, 0);
	tp->t_stk -= SA(MINFRAME);
	tp->t_flag |= T_INTR_THREAD;	/* for clock()'s tick checking */
	tp->t_pri = v.v_nglobpris - 1 - LOCK_LEVEL + CLOCK_LEVEL;
	clock_thread = tp;
}

/*
 * Called from dumpsys() to ensure the kvtopdata is in the dump.
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

#ifdef maybesomeday
/*
 * Set up VAC config word from imported variables, clear tags,
 * enable/disable cache. If the on argument is set to 2, we are in the
 * process of booting and must see how /boot has left the state of the
 * cache.
 *
 * void vac_control(int on);
 */
void
vac_control(int on)
{
	vac_info = (vac_size << VAC_INFO_VSS) +
		vac_hwflush ? VAC_INFO_HW : 0 +
		vac_linesize;

	switch (on) {
	case 0:
		off_enablereg(ENA_CACHE);
		break;
	case 2:
		if (get_enablereg() & ENA_CACHE)
			return;
		vac_tagsinit();
		/* fall through */
	case 1:
		on_enablereg(ENA_CACHE);
		break;
	}
}
#endif

/*
 * XXX These two probably ought to live somewhere else
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
int
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

/*ARGSUSED*/
void
sbus_set_64bit(u_int slot)
{
	cmn_err(CE_WARN, "This platform does not support 64-bit SBus");
}
