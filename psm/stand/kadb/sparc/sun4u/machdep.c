/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 */

#pragma ident "@(#)machdep.c	1.4	95/09/12 SMI"

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/vmmac.h>
#include <sys/mmu.h>
#include <sys/scb.h>
#include <sys/privregs.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/t_lock.h>
#include <sys/asm_linkage.h>
#include <sys/frame.h>
#include "allregs.h"
#include <sys/debug/debug.h>
#include <sys/debug/debugger.h>
#include <sys/promif.h>
#include <sys/openprom.h>
#include <sys/bootconf.h>
#include <sys/module.h>
#ifdef	IOC
#include <sys/iocache.h>
#endif /* IOC */

static void mp_init(void);
void set_prom_callback(void);
void reload_prom_callback(void);
void idle_other_cpus(void);
void resume_other_cpus(void);
void do_bkpt(caddr_t);

extern struct scb *gettba(void);
extern struct bootops *bootops;
extern int errno;

int cur_cpuid;
u_int cpu_nodeid[NCPU];
lock_t kadblock = 0;		/* MP lock used by kadb */
int fake_bpt;			/* place for a fake breakpoint at startup */

struct allregs_v9 regsave;
v9_fpregset_t fpuregs;

int scbsyncdone = 0;
struct scb *mon_tba;
struct scb *kern_tba;

extern char estack[], etext[], edata[];
extern int exit(int);

#ifdef PARTIAL_ALIGN
int partial_align;
#endif

#define	JB_PC	0
#define	JB_SP	1
jmp_buf mainregs;		/* context for debuggee */
jmp_buf debugregs;		/* context for debugger */
jmp_buf_ptr curregs;		/* pointer to saved context for each process */
static jmp_buf jb;
static jmp_buf_ptr saved_jb;

/*
 * Startup code after relocation.
 */
startup()
{
	mon_tba = gettba();	/* save PROM's %tba */
	mp_init();
	set_prom_callback();
}

/*
 * Miscellanous fault/error handler, called from trap(). If
 * we took a trap while in the debugger, we longjmp back to
 * where we were
 */
fault(trap, trappc, trapnpc)
	register int trap;
	register int trappc;
	register int trapnpc;
{
	register int ondebug_stack;

	ondebug_stack = (getsp() > (int)etext && getsp() < (int)estack);

	if ((trap == T_DATA_MMU_MISS || trap == FAST_DMMU_MISS_TT) &&
	    nofault && ondebug_stack) {
		jmp_buf_ptr sav = nofault;

		nofault = NULL;
		_longjmp(sav, 1);
		/*NOTREACHED*/
	}

	traceback((caddr_t)getsp());

	if (abort_jmp && ondebug_stack) {
		printf("abort jump: trap %x sp %x pc %x npc %x\n",
			trap, getsp(), trappc, trapnpc);
		printf("etext %x estack %x edata %x nofault %x\n",
			etext, estack, edata, nofault);
		_longjmp(abort_jmp, 1);
		/*NOTREACHED*/
	}

	/*
	 * Ok, the user faulted while not in the debugger. Enter the
	 * main cmd loop so that the user can look around...
	 *
	 * There is a problem here since we really need to tell cmd()
	 * the current registers.  We would like to call cmd() in locore
	 * but the interface is not really set up to handle this (yet?)
	 */

	printf("fault: calling cmd, trap %x sp %x pc %x npc %x\n",
	    trap, getsp(), trappc, trapnpc);
	cmd();	/* error not resolved, enter debugger */
}

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

	/*
	 * XXX - currently the Fusion kernel text is mapped read/write, so
	 * we don't need to muck with page protections. This may change
	 * soon...
	 */
	saved_jb = nofault;
	nofault = jb;
	errno = 0;
	if (!_setjmp(jb)) {
		*addr = val;
		/*
		 * If we get here, it worked. Flush the i-cache.
		 */
		sf_iflush(addr);
		nofault = saved_jb;
		return (0);
	}
	/* a fault occured */
	nofault = saved_jb;
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


	db_printf(4, "spawn: sp=%X routine=%X", sp, routine);

	if (curregs != 0) {
		printf("bad call to spawn\n");
		exit(1);
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

/*
 * Primitive context switch between debugger and debuggee.
 */
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
	/* else continue on in new context */
}

/*
 * Main interpreter command loop.
 */
cmd()
{
	int addr;

	dorun = 0;

	/*
	 * Make sure we aren't already on the debugger stack; if we are,
	 * we took some sort of fault (e.g. unexpected TLB miss) inside
	 * the debugger itself. Since we can't repair that or go back to
	 * userland, just bail into the PROM.
	 */
	reg = (struct regs *)&regsave;	/* XXX */
	addr = getsp();

	if (addr > (int)etext && addr < (int)estack) {
		printf("cmd: fault inside debugger! etext=%X estack=%X sp=%X\n",
		    etext, estack, addr);
		_exit();
	}

	do {
		doswitch();
		if (dorun == 0)
			printf("cmd: nothing to do\n");
	} while (dorun == 0);
}

/*
 * Called from mlsetup() in the kernel to update the kernel's trap table with
 * the appropriate traps for breakpoints and L1-A.
 */
scbsync()
{
	if (scbsyncdone) {
		set_prom_callback();
		return;
	}

	if (scbstop) {
		/*
		 * We're running interactively. Trap into the debugger
		 * so the user can look around before continuing.
		 * We use trap ST_KADB_TRAP: "enter debugger"
		 */
		scbstop = 0;
		asm_trap(ST_KADB_TRAP);
	}
	scbsyncdone = 1;
}

/*
 * Call into the PROM monitor.
 */
montrap()
{
	struct scb *our_tba;

	our_tba = gettba();
	db_printf(8, "montrap: our_tba = 0x%x, mon_tba = 0x%x\n",
	    our_tba, mon_tba);
	settba(mon_tba);
	(void) prom_enter_mon();
	settba(our_tba);
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
		fp = fp->fr_savfp;
		if (fp == 0)
			break;
	}
	printf("End traceback...\n");
}


/*
 * Determine whether the input address is suitable for single-stepping;
 * called by adb_ptrace(). This could be more rigorous...
 */
int
in_prom(caddr_t addr)
{
	return (addr >= (caddr_t)mon_tba);
}

/*
 * Called from the trap handler to idle all other CPUs before we
 * do anything.
 *
 * XXX - FIXME. Our PROM doesn't do this. We have to have each CPU
 * idle itself.
 */
void
idle_other_cpus(void)
{
	int i;
	int cpuid = cur_cpuid;

	if (cpuid > NCPU)
		printf("cur_cpuid: %d is bogus\n", cur_cpuid);
#ifdef KERNEL_AGENT
	if (kdbx_stopcpus_addr != (int *)0) {
		pokel(kdbx_stopcpus_addr, 1);
	} else {
		printf("idle_other_cpus: regs for other cpus not saved\n");
	}
#endif /* KERNEL_AGENT */
	for (i = 0; i < NCPU; i++) {
#ifdef LATER
		if (i != cpuid && cpu_nodeid[i] != -1)
			prom_idlecpu((dnode_t)cpu_nodeid[i]);
#endif
	}
}

void
resume_other_cpus(void)
{
	int i;
	int cpuid = cur_cpuid;

	if (cpuid > NCPU)
		printf("cur_cpuid: %d is bogus\n", cur_cpuid);

#ifdef KERNEL_AGENT
	if (kdbx_stopcpus_addr != (int *)0) {
		pokel(kdbx_stopcpus_addr, 0);
	}
#endif /* KERNEL_AGENT */

	for (i = 0; i < NCPU; i++) {
		if (i != cpuid && cpu_nodeid[i] != -1)
			prom_resumecpu((dnode_t)cpu_nodeid[i]);
	}
}

const char kadb_defer_word[] =
	": kadb_callback "
	"  %%pc  h# %x  l! "
	"  %%npc h# %x  l!"
	"  %%g1 h# %x  x!"
	"  %%g2 h# %x  x!"
	"  %%g3 h# %x  x!"
	"  %%g4 h# %x  x!"
	"  %%g5 h# %x  x!"
	"  %%g6 h# %x  x!"
	"  %%g7 h# %x  x!"
	"  1 %%tstate h# %x  x!"
	"  1 %%tt h# %x  l!"
	"  %%pil h# %x  l!"
	"  h# %x   set-pc "
	"    go "
	"; ";

u_long saved_pc, saved_npc;
u_longlong_t saved_g1, saved_g2, saved_g3, saved_g4;
u_longlong_t saved_g5, saved_g6, saved_g7;
u_longlong_t saved_tstate;
int saved_tt, saved_pil;

/*
 * Inform the PROM of the address to jump to when it takes a breakpoint
 * trap.
 */
void
set_prom_callback()
{
	char str[256];

	sprintf(str, kadb_defer_word, &saved_pc, &saved_npc, &saved_g1,
	    &saved_g2, &saved_g3, &saved_g4, &saved_g5, &saved_g6,
	    &saved_g7, &saved_tstate, &saved_tt, &saved_pil, trap);
	prom_interpret(str, 0, 0, 0, 0, 0);
	reload_prom_callback();
}

char kadb_prom_hook[] = " ['] kadb_callback is debugger-hook ";

/*
 * Reload the PROM's idea of "debugger-hook". Must be called after a trap
 * has been taken and before another one has occured.
 */
void
reload_prom_callback()
{
	prom_interpret(kadb_prom_hook, 0, 0, 0, 0, 0);
}

/*
 * Currently, the MID is the same as the cpu id, but we might want to
 * change this later.
 */
#define	UPAID2CPU(upaid)	(upaid)

/*
 * Perform MP initialization. Walk the device tree and save the node IDs
 * of all CPUs in the system.
 */
void
mp_init()
{
	dnode_t nodeid;
	dnode_t sp[OBP_STACKDEPTH];
	pstack_t *stk;
	int upa_id, cpuid, i;
	extern caddr_t cpu_startup;


	for (i = 0; i < NCPU; i++)
		cpu_nodeid[i] = -1;

	stk = prom_stack_init(sp, sizeof (sp));
	for (nodeid = prom_findnode_bydevtype(prom_rootnode(), "cpu", stk);
	    nodeid != OBP_NONODE; nodeid = prom_nextnode(nodeid),
	    nodeid = prom_findnode_bydevtype(nodeid, "cpu", stk)) {
		if (prom_getprop(nodeid, "upa-portid",
		    (caddr_t)&upa_id) == -1) {
			prom_printf("cpu node %x without upa-portid prop\n",
			    nodeid);
			continue;
		}
		cpuid = UPAID2CPU(upa_id);
		cpu_nodeid[cpuid] = (u_int)nodeid;
	}
	prom_stack_fini(stk);
}

mach_fiximp()
{
	extern int icache_flush;

	icache_flush = 1;
}
