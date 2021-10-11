/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)machdep.c	1.54	95/07/18 SMI"

/* from machdep.c: 1.28	89/12/07 SMI" */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/vmmac.h>
#include <sys/buserr.h>
#include <sys/enable.h>
#include <sys/proc.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/pte.h>
#include <sys/scb.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/clock.h>
#include <sys/intreg.h>
#include <sys/t_lock.h>
#include <sys/eeprom.h>
#include <sys/asm_linkage.h>
#include <sys/privregs.h>
#include <sys/frame.h>
#include "allregs.h"
#include <sys/debug/debug.h>
#include <sys/debug/debugger.h>
#include <sys/bootconf.h>
#include <sys/promif.h>
#include <sys/openprom.h>

extern int errno;

int istrap = 0;
int scbsyncdone=0;

/*
 * The next group of variables and routines handle the
 * Open Boot Prom devinfo or property information.
 *
 * These machine-dependent quantities are set from the prom properties.
 * For the time being, set these to "large, safe" values.
 */

extern short cputype;
extern int vac_size;			/* cache size */
extern int vac_linesize;		/* size of a cache line */
extern int pagesize;

/*
 * Open proms give us romp as a variable
 */
union sunromvec *romp;

int fake_bpt;			/* place for a fake breakpoint at startup */
jmp_buf debugregs;		/* context for debugger */
jmp_buf mainregs;		/* context for debuggee */
jmp_buf_ptr curregs;		/* pointer to saved context for each process */
int cache_on;			/* cache is being used */
struct allregs regsave; 	/* temp save area--align to double */
struct scb *mon_tbr, *our_tbr;	/* storage for %tbr's */

extern char start[], estack[], etext[], edata[], end[];
extern int bss_start;		/* XXX hack because cross ld is broken */
extern struct scb *gettbr();

#ifdef PARTIAL_ALIGN
int partial_align;
#endif
/*
 * Definitions for registers in jmp_buf
 */
#define	JB_PC	0
#define	JB_SP	1

extern setcontext(), getsegmap(), setsegmap(), getpgmap(), setpgmap();
void Setpgmap();

static volatile u_long counter_vaddr, intreg_vaddr;

/*
 * Startup code after relocation.
 */
startup(void)
{
	register int i;
	int pg;
	register int vaddr;
	pstack_t *stk;
	dnode_t sp[OBP_STACKDEPTH];
	dnode_t node;

	/*
	 * Suspect this tbr is entirely pointless, since the OBP
	 * saves and restores it's own tbr anyway ..
	 */
	mon_tbr = gettbr();

	/*
	 * In memoriam:
	 *
	 *	Setpgmap((caddr_t)EEPROM_ADDR,
	 *	 PG_V | PG_KR | PGT_OBIO | PG_NC | btop(OBIO_EEPROM_ADDR));
	 *
	 *	Setpgmap((caddr_t)COUNTER_ADDR,
	 *	 PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_COUNTER_ADDR));
	 *
	 *	Setpgmap((caddr_t)INTREG_ADDR,
	 *	 PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_INTREG_ADDR));
	 *
	 * (Why did we ever need to look at the EEPROM anyway?)
	 */
	stk = prom_stack_init(sp, sizeof (sp));
	node = prom_findnode_byname(prom_rootnode(), "counter-timer", stk);
	if (node != OBP_NONODE && node != OBP_BADNODE &&
	    prom_getproplen(node, "address") != 0)
		(void) prom_getprop(node, "address", (caddr_t)&counter_vaddr);
	else
		prom_printf("Warning: missing 'address' for 'counter-timer'\n");
	prom_stack_fini(stk);

	stk = prom_stack_init(sp, sizeof (sp));
	node = prom_findnode_byname(prom_rootnode(), "interrupt-enable", stk);
	if (node != OBP_NONODE && node != OBP_BADNODE &&
	    prom_getproplen(node, "address") != 0)
		(void) prom_getprop(node, "address", (caddr_t)&intreg_vaddr);
	else
		prom_printf("Warning: missing 'address' for 'interrupt-enable'\n");
	prom_stack_fini(stk);

	set_clk_mode((u_char)IR_ENA_CLK14, (u_char)0);

	/*
	 * Fix up old scb.
	 */
	kadbscbsync();
	spl13();		/* we can take nmi's now */

	/*
	 * Now make text (and dvec) read only,
	 * this also sets a stack redzone
	 */
	for (i = (int)start; i < (int)etext; i += pagesize) {
		pg = getpgmap(i);
		Setpgmap(i, (pg & ~PG_PROT) | PG_KR);
	}

	/*
	 * XXX	Why are we doing this here?
	 */
	if (!(cache_on = getenablereg() & ENA_CACHE))
		vac_init();			/* invalidate entire cache */
}

scbsync()
{
	kadbscbsync();
	scbsyncdone = 1;
}

kadbscbsync()
{
	register struct scb *tbr;
	register int otbr_pg;
	extern trapvec tcode;

	tbr = gettbr();
	otbr_pg = getpgmap(tbr);
	Setpgmap(tbr, (otbr_pg & ~PG_PROT) | PG_KW);

	tbr->user_trap[ST_KADB_TRAP] = tcode;
	tbr->user_trap[ST_KADB_BREAKPOINT] = tcode;
	Setpgmap(tbr, otbr_pg);
	if (scbstop) {
		/*
		 * We're running interactively. Trap into the debugger
		 * so the user can look around before continuing.
		 * We use trap ST_KADB_TRAP: "enter debugger"
		 */
		scbstop = 0;
		asm_trap(ST_KADB_TRAP);
	}
}

/*
 * Set and/or clear the desired clock bits in the interrupt
 * register. Because the counter interrupts are level sensitive, not
 * edge sensitive, we no longer have to be careful about wedging time.
 * We clear outstanding clock interrupts since they will surely be
 * piled up. However, our first interval is still of random length, since
 * we do not reset the counters.
 */
void
set_clk_mode(u_char on, u_char off)
{
	register u_char intreg, dummy;
	register int s;

	/*
	 * make sure that we are only playing w/
	 * clock interrupt register bits
	 */
	on &= (IR_ENA_CLK14 | IR_ENA_CLK10);
	off &= (IR_ENA_CLK14 | IR_ENA_CLK10);

	/*
	 * Get a copy of current interrupt register,
	 * turning off any undesired bits (aka `off')
	 */
	intreg = *(char *)intreg_vaddr & ~(off | IR_ENA_INT);

	/*
	 * Next we turns off the CLK10 and CLK14 bits to avoid any
	 * triggers, and clear any outstanding clock interrupts.
	 */
	*(char *)intreg_vaddr &= ~(IR_ENA_CLK14 | IR_ENA_CLK10);
	/* SAS simulates the counters, so okay to clear any interrupt */
	dummy = ((struct counterregs *)counter_vaddr)->limit10;
	dummy = ((struct counterregs *)counter_vaddr)->limit14;
#ifdef lint
	dummy = dummy;
#endif

	/*
	 * Now we set all the desired bits
	 * in the interrupt register.
	 */
	*(char *)intreg_vaddr |= (intreg | on);	/* enable interrupts */
}

/*
 * Sys_trap trap handlers.
 */

/*
 * level15 (memory error) interrupt.
 */
level15()
{
	/*
	 * For now, the memory error regs are not mapped into the debugger,
	 * so we just print a message.
	 */
	printf("memory error\n");
}

/*
 * Miscellanous fault error handler
 */
fault(trap, trappc, trapnpc)
	register int trap;
	register int trappc;
	register int trapnpc;
{
	register int ondebug_stack;
	register u_int *pc;
	register u_int realpc;

	ondebug_stack = (getsp() > (int)etext && getsp() < (int)estack);

	/* Assume that nofault won't be non-NULL if we're not in kadb.
	 * ondebug_stack won't be true early on, and it's possible to
	 * get an innocuous data fault looking for kernel symbols then.
	 */
	if (trap == T_DATA_FAULT && nofault) {
		jmp_buf_ptr sav = nofault;

		nofault = NULL;
		_longjmp(sav, 1);
		/*NOTREACHED*/
	}

	traceback((caddr_t)getsp());
	/*
	 * If we are on the debugger stack and
	 * abort_jmp is set, do a longjmp to it.
	 */
	if (abort_jmp && ondebug_stack) {
		printf("abort jump: trap %x sp %x pc %x npc %x\n",
			trap, getsp(), trappc, trapnpc);
		printf("etext %x estack %x edata %x nofault %x\n",
			etext, estack, edata, nofault);
		_longjmp(abort_jmp, 1);
		/*NOTREACHED*/
	}

	/*
	 * Ok, the user faulted while not in the
	 * debugger. Enter the main cmd loop
	 * so that the user can look around...
	 */
	/*
	 * There is a problem here since we really need to tell cmd()
	 * the current registers.  We would like to call cmd() in locore
	 * but the interface is not really set up to handle this (yet?)
	 */

	printf("fault and calling cmd: trap %x sp %x pc %x npc %x\n",
	    trap, getsp(), trappc, trapnpc);
	cmd();	/* error not resolved, enter debugger */
}
long trap_window[25];

static jmp_buf_ptr saved_jb;
static jmp_buf jb;
extern int debugging;


/*
 * Peekc is so named to avoid a naming conflict
 * with adb which has a variable named peekc
 */
int
Peekc(addr)
	char *addr;
{
	u_char val;

	saved_jb = nofault;
	nofault = jb;
	errno = 0;
	if (!_setjmp(jb)) {
		val = *addr;
		/* if we get here, it worked */
		nofault = saved_jb;
		return ((int)val);
	}
	/* a fault occured */
	nofault = saved_jb;
	errno = EFAULT;
	return (-1);
}

short
peek(addr)
	short *addr;
{
	short val;

	saved_jb = nofault;
	nofault = jb;
	errno = 0;
	if (!_setjmp(jb)) {
		val = *addr;
		/* if we get here, it worked */
		nofault = saved_jb;
		return (val);
	}
	/* a fault occured */
	nofault = saved_jb;
	errno = EFAULT;
	return (-1);
}
long
peekl(addr)
	long *addr;
{
	long val;

	saved_jb = nofault;
	nofault = jb;
	errno = 0;
	if (!_setjmp(jb)) {
		val = *addr;
		/* if we get here, it worked */
		nofault = saved_jb;
		return (val);
	}
	/* a fault occured */
	nofault = saved_jb;
	errno = EFAULT;
	return (-1);
}

int
pokec(addr, val)
	char *addr;
	char val;
{

	saved_jb = nofault;
	nofault = jb;
	errno = 0;
	if (!_setjmp(jb)) {
		*addr = val;
		/* if we get here, it worked */
		nofault = saved_jb;
		return (0);
	}
	/* a fault occured */
	nofault = saved_jb;
	errno = EFAULT;
	return (-1);
}

int
pokes(addr, val)
	short *addr;
	short val;
{
	saved_jb = nofault;
	nofault = jb;
	errno = 0;
	if (!_setjmp(jb)) {
		*addr = val;
		/* if we get here, it worked */
		nofault = saved_jb;
		return (0);
	}
	/* a fault occured */
	nofault = saved_jb;
	errno = EFAULT;
	return (-1);
}

int
pokel(addr, val)
	long *addr;
	long val;
{

	saved_jb = nofault;
	nofault = jb;
	errno = 0;
	if (!_setjmp(jb)) {
		*addr = val;
		/* if we get here, it worked */
		nofault = saved_jb;
		return (0);
	}
	/* a fault occured */
	nofault = saved_jb;
	errno = EFAULT;
	return (-1);
}

poketext(addr, val)
	int *addr;
	int val;
{
	int pg = 0;

	pg = getpgmap((int)addr);
	if ((pg & PG_V) == 0) {
		if (debugging > 2)
			printf("poketext: invalid page map %X at %X\n",
			    pg, addr);
		goto err;
	}
	if ((pg & PGT_MASK) != PGT_OBMEM) {
		if (debugging > 2)
			printf("poketext: incorrect page type %X at %X\n",
			    pg, addr);
		goto err;
	}

	vac_pageflush((caddr_t) addr);
	if (btop(addr + sizeof (int) - 1) != btop(addr))
		vac_pageflush((caddr_t) (addr + sizeof (int) - 1));

	if ((pg & PG_PROT) == PG_KR)
		Setpgmap(addr, (pg & ~PG_PROT) | PG_KW);
	else if ((pg & PG_PROT) == PG_URKR)
		Setpgmap(addr, (pg & ~PG_PROT) | PG_UW);
	/* otherwise it is already writeable */
	*addr = val;		/* should be prepared to catch a fault here? */
	/*
	 * Reset to page map to previous entry,
	 * but mark as modified
	 */
	vac_pageflush((caddr_t) addr);
	if (btop(addr + sizeof (int) - 1) != btop(addr))
		vac_pageflush((caddr_t) (addr + sizeof (int) - 1));
	Setpgmap(addr, pg | PG_M);
	errno = 0;
	return (0);

err:
	errno = EFAULT;
	return (-1);
}

scopy(from, to, count)
	register char *from;
	register char *to;
	register int count;
{
	register int val;

	for (; count > 0; count--) {
		if ((val = Peekc(from++)) == -1)
			goto err;
		if (pokec(to++, val) == -1)
			goto err;
	}
	return (0);
err:
	errno = EFAULT;
	return (-1);
}

/*
 * Setup a new context to run at routine using stack whose
 * top (end) is at sp.  Assumes that the current context
 * is to be initialized for mainregs and new context is
 * to be set up in debugregs.
 */
spawn(sp, routine)
	char *sp;
	func_t routine;
{
	char *fp;
	int res;
	extern void _exit();

	if (curregs != 0) {
		printf("bad call to spawn\n");
		_exit(1);
	}
	if ((res = _setjmp(mainregs)) == 0) {
		/*
		 * Setup top (null) window.
		 */
		sp -= WINDOWSIZE;
		((struct rwindow *)sp)->rw_rtn = 0;
		((struct rwindow *)sp)->rw_fp = 0;
		/*
		 * Setup window for routine with return to exit.
		 */
		fp = sp;
		sp -= WINDOWSIZE;
		((struct rwindow *)sp)->rw_rtn = (int)exit - 8;
		((struct rwindow *)sp)->rw_fp = (int)fp;
		/*
		 * Setup new return window with routine return value.
		 */
		fp = sp;
		sp -= WINDOWSIZE;
		((struct rwindow *)sp)->rw_rtn = (int)routine - 8;
		((struct rwindow *)sp)->rw_fp = (int)fp;
		/* copy entire jump buffer to debugregs */
		bcopy((caddr_t)mainregs, (caddr_t)debugregs, sizeof (jmp_buf));
		debugregs[JB_SP] = (int)sp;	/* set sp */
		curregs = debugregs;
		regsave.r_npc = (int)&fake_bpt;
		_longjmp(debugregs, 1);		/* jump to new context */
		/*NOTREACHED*/
	}

}

doswitch()
{
	int res;

	if ((res = _setjmp(curregs)) == 0) {
		/*
		 * Switch curregs to other descriptor
		 */
		if (curregs == mainregs) {
			curregs = debugregs;
		} else /* curregs == debugregs */ {
			curregs = mainregs;
		}
		_longjmp(curregs, 1);
		/*NOTREACHED*/
	}
	/*
	 * else continue on in new context
	 */
}

/*
 * Main interpreter command loop.
 */
cmd()
{
	int resetclk = 0;
	u_char intreg;
	int addr, t;
	int i;
	u_char interreg;
	int s;

	dorun = 0;
	i = 0;

	/*
	 * See if the sp says that we are already on the debugger stack
	 */
	reg = (struct regs *)&regsave;
	addr = getsp();
	if (addr > (int)etext && addr < (int)estack) {
		printf("Already in debugger!\n");
		return;
	}
	cache_on = getenablereg() &  ENA_CACHE;

	do {
		doswitch();
		if (dorun == 0)
			printf("cmd: nothing to do\n");
	} while (dorun == 0);
	/* we don't need to splx since we are returning to the caller */
	/* and will reset his/her state */
}

/*
 * Call into the monitor (hopefully)
 */
montrap()
{
	our_tbr = gettbr();
	settbr(mon_tbr);
	(void) prom_enter_mon();
	settbr(our_tbr);
}

/*
 * Set the pme for address v using the software pte given.
 * Setpgmap() automatically turns on the ``no cache'' bit
 * for all mappings between 'start' and 'start + DEBUGSIZE.'
 */
void
Setpgmap(v, pte)
	caddr_t v;
	int pte;
{
	if (v >= (caddr_t)start && v <= (caddr_t)(start + DEBUGSIZE))
		pte |= PG_NC;
	setpgmap(v, pte);
}

void
traceback(sp)
	caddr_t sp;
{
	register u_int tospage;
	register struct frame *fp;
	static int done = 0;

#ifdef PARTIAL_ALIGN
	if (partial_align? ((int)sp & 0x3): ((int)sp & 0x7)) {
#else
	if ((int)sp & (STACK_ALIGN-1)) {
#endif PARTIAL_ALIGN
		printf("traceback: misaligned sp = %x\n", sp);
		return;
	}
	flush_windows();
	tospage = (u_int)btoc(sp);
	fp = (struct frame *)sp;
	printf("Begin traceback... sp = %x\n", sp);
	while (btoc((u_int)fp) == tospage) {
		if (fp == fp->fr_savfp) {
			printf("FP loop at %x", fp);
			break;
		}
		printf("Called from %x, fp=%x, args=%x %x %x %x %x %x\n",
		    fp->fr_savpc, fp->fr_savfp,
		    fp->fr_arg[0], fp->fr_arg[1], fp->fr_arg[2],
		    fp->fr_arg[3], fp->fr_arg[4], fp->fr_arg[5]);
#ifdef notdef
		printf("\tl0-l7: %x, %x, %x, %x, %x, %x, %x, %x\n",
		    fp->fr_local[0], fp->fr_local[1],
		    fp->fr_local[2], fp->fr_local[3],
		    fp->fr_local[4], fp->fr_local[5],
		    fp->fr_local[6], fp->fr_local[7]);
#endif
		fp = fp->fr_savfp;
		if (fp == 0)
			break;
	}
	printf("End traceback...\n");
}

/*
 * XXX	This is badly broken - see bugid 1075606
 *
 *	For V0 it's true, but for V2 and above,
 *	it's almost certainly wrong.
 */

/* A utility used for stepping by adb_ptrace() */

int
in_prom(uint addr)
{
        return ((addr >= (unsigned)SUNMON_START) &&
		(addr <= (unsigned)SUNMON_END));
}


/*
 * Note: other OBP-based machines will use getprop() to get
 * these values.
 */
#define	SUN4C_NPMGRPS_DEFAULT		128
#define	SUN4C_PAGESIZE_DEFAULT		0x1000
#define	SUN4C_VACSIZE_DEFAULT		0x10000
#define	SUN4C_VACLINESIZE_DEFAULT	16

#define KADB_DEBUG
#ifdef KADB_DEBUG
int debug_prop = 0;	/* Turn on to enable debugging message */
#define	DPRINTF	if (debug_prop) prom_printf
#else
#define	DPRINTF
#endif

void
mach_fiximp()
{
	dnode_t	rootnode = prom_rootnode();
	extern u_int segmask;
	extern int icache_flush;

	icache_flush = 0;

	/*
	 * Trapped in a world we never made! Determine the physical
	 * constants which govern this universe.
	 */
	if (getprop(rootnode, "mmu-npmg", &npmgrps) != sizeof (int)) {
		npmgrps = SUN4C_NPMGRPS_DEFAULT;
		DPRINTF(
		    "Warning: missing 'mmu-npmg' property - set to %d\n",
		    npmgrps);
	}

	segmask = npmgrps - 1;

	if (getprop(rootnode, "vac-size", &vac_size) != sizeof (int)) {
		vac_size = SUN4C_VACSIZE_DEFAULT;
		DPRINTF(
		    "Warning: missing 'vac-size' property - set to %d\n",
		    vac_size);
	}

	if (getprop(rootnode, "vac-linesize",
	    &vac_linesize) != sizeof (int)) {
		vac_linesize = SUN4C_VACLINESIZE_DEFAULT;
		DPRINTF(
		    "Warning: missing 'vac-linesize' property - set to %d\n",
		    vac_linesize);
	}

	pagesize = SUN4C_PAGESIZE_DEFAULT;

#ifdef	KADB_DEBUG
	DPRINTF("kadb: npmgrps %d vac_size %d vac_linesize %d\n",
	    npmgrps, vac_size, vac_linesize);
#endif	/* KADB_DEBUG */
}
