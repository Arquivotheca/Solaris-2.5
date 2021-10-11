/*
 * Copyright (c) 1990, 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)trap.c	1.65	94/11/09 SMI"

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/class.h>
#include <sys/syscall.h>
#include <sys/cpuvar.h>
#include <sys/vm.h>
#include <sys/msgbuf.h>
#include <sys/sysinfo.h>
#include <sys/fault.h>
#include <sys/frame.h>
#include <sys/stack.h>
#include <sys/aio_impl.h>
#include <sys/buserr.h>
#include <sys/mmu.h>
#include <sys/pcb.h>
#include <sys/cpu.h>
#include <sys/psw.h>
#include <sys/pte.h>
#include <sys/reg.h>
#include <sys/ddi.h>
#include <sys/trap.h>
#include <sys/kmem.h>
#include <sys/vtrace.h>
#include <sys/simulate.h>
#include <sys/prsystm.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/fpu/fpusystm.h>
#include <sys/cmn_err.h>
#include <sys/mutex_impl.h>

#include <vm/hat.h>
#include <vm/hat_sunm.h>

#include <vm/seg_kmem.h>
#include <vm/as.h>
#include <vm/seg.h>

#include <sys/procfs.h>

#include <sys/reboot.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>
#include <sys/modctl.h>
#include <sys/memerr.h>

#define	USER	0x10000		/* user-mode flag added to type */

char	*trap_type[] = {
	"Zero",
	"Text fault",
	"Illegal instruction",
	"Privileged instruction",
	"Floating point unit disabled",
	"Window overflow",
	"Window underflow",
	"Memory address alignment",
	"Floating point exception",
	"Data fault",
	"Tag overflow",
	"Trap 0x0B",
	"Trap 0x0C",
	"Trap 0x0D",
	"Trap 0x0E",
	"Trap 0x0F",
	"Spurious interrupt",
	"Interrupt level 1",
	"Interrupt level 2",
	"Interrupt level 3",
	"Interrupt level 4",
	"Interrupt level 5",
	"Interrupt level 6",
	"Interrupt level 7",
	"Interrupt level 8",
	"Interrupt level 9",
	"Interrupt level A",
	"Interrupt level B",
	"Interrupt level C",
	"Interrupt level D",
	"Interrupt level E",
	"Interrupt level F",
	"AST",
};

#define	TRAP_TYPES	(sizeof (trap_type) / sizeof (trap_type[0]))

static int tudebug = 0;
static int tudebugbpt = 0;
static int tudebugfpe = 0;

static int alignfaults = 0;

#if defined(TRAPDEBUG) || defined(lint)
static int tdebug = 0;
static int lodebug = 0;
static int faultdebug = 0;
#else
#define	tdebug	0
#define	lodebug	0
#define	faultdebug	0
#endif /* defined(TRAPDEBUG) || defined(lint) */

static void showregs(unsigned, struct regs *, caddr_t, u_int, enum seg_rw);

static int
die(type, rp, addr, be, rw)
	unsigned type;
	struct regs *rp;
	caddr_t addr;
	u_int be;
	enum seg_rw rw;
{
	char *trap_name = type < TRAP_TYPES ? trap_type[type] : "trap";

	(void) setup_panic(trap_name, (va_list)NULL);

	printf("BAD TRAP\n");
	showregs(type, rp, addr, be, rw);
	traceback((caddr_t)rp->r_sp);
	panic(trap_name);
	return (0);	/* avoid optimization of restore in call's delay slot */
}

/*
 * Called from the trap handler when a processor trap occurs.
 * Addr, be and rw only are passed for text and data faults.
 *
 * Note: All user-level traps that might call stop() must exit
 * trap() by 'goto out' or by falling through.  This is so that
 * prdostep() can be called to enable hardware single stepping.
 */
/*VARARGS2*/
void
trap(type, rp, addr, be, rw)
	register unsigned type;
	register struct regs *rp;
	register caddr_t addr;
	register u_int be;
	register enum seg_rw rw;
{
	proc_t *p = ttoproc(curthread);
	klwp_id_t lwp = ttolwp(curthread);
	clock_t syst;
	u_int lofault;
	faultcode_t pagefault(), res;
	k_siginfo_t siginfo;
	u_int fault = 0;
	int driver_mutex = 0;	/* get unsafe_driver before returning */
	int stepped;
	greg_t oldpc;
	int mstate;
	char *badaddr;

	CPU_STAT_ADDQ(CPU, cpu_sysinfo.trap, 1);
	syst = p->p_stime;

	ASSERT(curthread->t_schedflag & TS_DONT_SWAP);
	if (tdebug)
		showregs(type, rp, addr, be, rw);

	mstate = curthread->t_mstate;
	if (USERMODE(rp->r_psr)) {
		/*
		 * Set up the current cred to use during this trap. u_cred
		 * no longer exists.  t_cred is used instead.
		 * The current process credential applies to the thread for
		 * the entire trap.  If trapping from the kernel, this
		 * should already be set up.
		 */
		if (curthread->t_cred != p->p_cred) {
			crfree(curthread->t_cred);
			curthread->t_cred = crgetcred();
		}
		ASSERT(lwp != NULL);
		type |= USER;
		ASSERT(lwp->lwp_regs == rp);
		lwp->lwp_state = LWP_SYS;
		if (curthread->t_proc_flag & TP_MSACCT) {
			switch (type) {
			case T_DATA_FAULT + USER:
				mstate = LMS_DFAULT;
				break;
			case T_TEXT_FAULT + USER:
				mstate = LMS_TFAULT;
				break;
			default:
				mstate = LMS_TRAP;
				break;
			}
			mstate = new_mstate(curthread, mstate);
		}
		siginfo.si_signo = 0;
		stepped =
		    lwp->lwp_pcb.pcb_step != STEP_NONE &&
		    ((oldpc = rp->r_pc), prundostep()) &&
		    addr == (caddr_t)oldpc;
		/* this assignment must not precede call to prundostep() */
		oldpc = rp->r_pc;
	} else {
		if (MUTEX_OWNER_LOCK(&unsafe_driver) &&
		    UNSAFE_DRIVER_LOCK_HELD()) {
			driver_mutex = 1;
			mutex_exit(&unsafe_driver);
		}
	}

	TRACE_1(TR_FAC_TRAP, TR_C_TRAP_HANDLER_ENTER,
		"C_trap_handler_enter:type %x", type);

	/*
	 * take any pending floating point exceptions now
	 */
	syncfpu();

	switch (type) {

	default:
		/*
		 * Check for user software trap.
		 */
		if (type & USER) {
			if (tudebug)
				showregs(type, rp, (caddr_t)0, 0, S_OTHER);
			if (((type & ~USER) >= T_SOFTWARE_TRAP) ||
			    ((type & ~USER) & CP_BIT)) {
				bzero((caddr_t)&siginfo, sizeof (siginfo));
				siginfo.si_signo = SIGILL;
				siginfo.si_code  = ILL_ILLTRP;
				siginfo.si_addr  = (caddr_t)rp->r_pc;
				siginfo.si_trapno = type &~ USER;
				fault = FLTILL;
				break;
			}
		}
		(void) die(type, rp, addr, be, rw);
		/*NOTREACHED*/

	case T_TEXT_FAULT:		/* system text access fault */
		if (be & SE_MEMERR) {
			if (lodebug)
				showregs(type, rp, addr, be, rw);
			memerr(MERR_SYNC, be, addr, type, rp);
			/* memerr returns if recoverable, panics if not */
			goto cleanup;
		}
		(void) die(type, rp, addr, be, rw);
		/*NOTREACHED*/

	case T_DATA_FAULT:		/* system data access fault */
		/* may have been expected by C (e.g. bus probe) */
		if (curthread->t_nofault) {
			label_t *ftmp;

			ftmp = curthread->t_nofault;
			curthread->t_nofault = 0;
#ifndef LOCKNEST
			if (driver_mutex)
				mutex_enter(&unsafe_driver);
#endif /* LOCKNEST */
			TRACE_0(TR_FAC_TRAP, TR_C_TRAP_HANDLER_EXIT,
				"C_trap_handler_exit");
			TRACE_0(TR_FAC_TRAP, TR_TRAP_END, "trap_end");
			longjmp(ftmp);
		}
		if (be & SE_MEMERR) {
			if (lodebug)
				showregs(type, rp, addr, be, rw);
			memerr(MERR_SYNC, be, addr, type, rp);
			/* memerr returns if recoverable, panics if not */
			goto cleanup;
		}

		if (be & SE_SIZERR) {
			if (lodebug)
				showregs(type, rp, addr, be, rw);
		}
		/* may be fault caused by timeout on read of VME vector */
		if (be & SE_TIMEOUT) {
			extern int vme_read_vector(), vme_ifault;

			if (lodebug)
				showregs(type, rp, addr, be, rw);
			if ((int (*)())rp->r_pc == vme_read_vector) {
				/*
				* If we get a fault reading the VME
				* vector, return to the instruction
				* just after the ASI that caused the
				* fault. We'll be a level14, so the
				* store of vme_ifault as one is a
				* reasonable method to signal the
				* vme_read_vector routine that something
				* died.
				*/
				rp->r_pc += 4;
				rp->r_npc += 4;
				vme_ifault = 1;
				goto cleanup;
			}

			/*
			 * XXX  FIX ME FIX ME- need to put in an SBUS
			 * spurious interrupt routine
			 */

			/* Ignore timeout if lofault set */
			if (curthread->t_lofault) {
				if (lodebug) {
					showregs(type, rp, addr, be, rw);
					traceback((caddr_t)rp->r_sp);
				}
				rp->r_g1 = EFAULT;
				rp->r_pc = curthread->t_lofault;
				rp->r_npc = curthread->t_lofault + 4;
				goto cleanup;
			}
			printf("KERNEL DATA ACCESS TIMEOUT\n");
			showregs(type, rp, addr, be, rw);
			traceback((caddr_t)rp->r_sp);
			panic("system bus error timeout");
			/*NOTREACHED*/
		}

		/*
		 * There is a bug in Campus and Calvin, where a
		 * protection error on a LDSTUB (and perhaps SWAP?)
		 * will not set the RW bit in the SER.
		 * bugid 1049150 (hw), 104151 (this workaround)
		 */
		if (be & SE_PROTERR && rw == S_READ && is_atomic(rp))
			rw = S_WRITE;
		/*
		 * See if we can handle as pagefault. Save lofault
		 * across this. Here we assume that an address
		 * less than KERNELBASE is a user fault.
		 * We can do this as copy.s routines verify that the
		 * starting address is less than KERNELBASE before
		 * starting and because we know that we always have
		 * KERNELBASE mapped as invalid to serve as a "barrier".
		 * Because of SF9010 bug (see below), we must validate
		 * the bus error register when more than one bit is on.
		 */
		if ((be & (be - 1)) != 0) {	/* more than one bit set? */
			struct pte pte;

			mmu_getpte(addr, &pte);
			if (pte_valid(&pte)) {
				if (be & SE_PROTERR &&
				    rw == S_WRITE && pte_ronly(&pte))
					be = SE_PROTERR;
				else
					be &= ~SE_PROTERR;
			} else {
				be = SE_INVALID;
			}
		} else if ((be & ~(SE_RW|SE_INVALID|SE_PROTERR)) != 0) {
			/*
			 * In this case, we might have a SBus Error.
			 * In any case, this is a gross hardware
			 * error that we should trap. In any case,
			 * we need to skip over the crap below which
			 * is trying to resolve page faults, etc..
			 */
			lofault = curthread->t_lofault;
			res = FC_HWERR;
			goto skip;

		}
		lofault = curthread->t_lofault;
		curthread->t_lofault = 0;

		if (curthread->t_proc_flag & TP_MSACCT)
			mstate = new_mstate(curthread, LMS_KFAULT);
		if (addr < (caddr_t)KERNELBASE) {
			if (lofault == 0)
				(void) die(type, rp, addr, be, rw);
			res = pagefault(addr,
				(be & SE_PROTERR)? F_PROT: F_INVAL, rw, 0);
			if (res == FC_NOMAP && grow((int *)addr))
				res = 0;
		} else {
			res = pagefault(addr,
				(be & SE_PROTERR)? F_PROT: F_INVAL, rw, 1);
		}
		if (curthread->t_proc_flag & TP_MSACCT)
			(void) new_mstate(curthread, mstate);

		/*
		 * Restore lofault. If we resolved the fault, exit.
		 * If we didn't and lofault wasn't set, die.
		 */
		curthread->t_lofault = lofault;
		if (res == 0)
			goto cleanup;

		/*
		 * Check for mutex_exit hook.  This is to protect mutex_exit
		 * from deallocated locks.  It would be too expensive to use
		 * nofault there.
		 */
		{
			void	mutex_exit_nofault(void);
			void	mutex_exit_fault(kmutex_t *);

			if (rp->r_pc == (int)mutex_exit_nofault) {
				rp->r_pc = (int)mutex_exit_fault;
				rp->r_npc = (int)mutex_exit_fault + 4;
				goto cleanup;
			}
		}

#ifndef	XXXX	/* Bogus kernel page faults (P1_5 only?) */
		/*
		 * If the pme says this is a good page, print bogosity
		 * message and ignore it.
		 */
		if (good_addr(addr)) {
			struct pte pte;

			mmu_getpte(addr, &pte);
			if (pte_valid(&pte) && (rw != S_WRITE ||
			    !pte_ronly(&pte))) {
#if 0	/* message only useful on a UP machine w/o KSLICE */
				printf("BOGUS page fault on valid page: ");
				showregs(type, rp, addr, be, rw);
#endif
				goto cleanup;
			}
		}
#endif /* XXXX */

skip:
		if (lofault == 0)
			(void) die(type, rp, addr, be, rw);

		/*
		 * Cannot resolve fault.  Return to lofault.
		 */
		if (lodebug) {
			showregs(type, rp, addr, be, rw);
			traceback((caddr_t)rp->r_sp);
		}
		if (FC_CODE(res) == FC_OBJERR)
			res = FC_ERRNO(res);
		else
			res = EFAULT;
		rp->r_g1 = res;
		rp->r_pc = curthread->t_lofault;
		rp->r_npc = curthread->t_lofault + 4;
		goto cleanup;

	case T_ALIGNMENT:
		if (curthread->t_lofault && curthread->t_onfault) {
			label_t *ftmp;

			ftmp = curthread->t_onfault;
			curthread->t_onfault = NULL;
			curthread->t_lofault = 0;
#ifndef	LOCKNEST
			if (driver_mutex)
				mutex_enter(&unsafe_driver);
#endif	LOCKNEST
			TRACE_0(TR_FAC_TRAP, TR_TRAP_END, "trap_end");
			longjmp(ftmp);
		}
		(void) die(type, rp, addr, be, rw);
		/*NOTREACHED*/

	case T_DATA_FAULT + USER:	/* user data access fault */
	case T_TEXT_FAULT + USER:	/* user text access fault */
		/*
		 * The SPARC processor prefetches instructions.
		 * The bus error register may also reflect faults
		 * that occur during prefetch in addition to the one
		 * that caused the current fault. For example:
		 *	st	[proterr]	! end of page
		 *	...			! invalid page
		 * will cause both SE_INVALID and SE_PROTERR.
		 * The gate array version of sparc (SF9010) has a bug
		 * which works as follows: When the chip does a prefetch
		 * to an invalid page the board loads garbage into the
		 * chip. If this garbage looks like a branch instruction
		 * a prefetch will be generated to some random address
		 * even though the branch is annulled. This can cause
		 * bits in the bus error register to be set. In this
		 * case we have to validate the bus error register bits.
		 * We only handle true SE_INVALID and SE_PROTERR faults.
		 * SE_MEMERR is given to memerr(), which will handle
		 * recoverable parity errors.
		 * All others cause the user to die.
		 */

		if (be & SE_MEMERR) {
			memerr(MERR_SYNC, be, addr, type & ~USER, rp);
			/* memerr returns if recoverable, panics if not */
			break;
		}
		/*
		 * There is a bug in Campus and Calvin, where a
		 * protection error on a LDSTUB (and perhaps SWAP?)
		 * will not set the RW bit in the SER.
		 * bugid 1040150 (hw), 1040151 (this workaround)
		 */
		if (be & SE_PROTERR && rw == S_READ && is_atomic(rp))
			rw = S_WRITE;
		if ((be & (be - 1)) != 0) {	/* more than one bit set? */
			struct pte pte;

			mmu_getpte(addr, &pte);
			if (pte_valid(&pte)) {
				if (be & SE_PROTERR && (pte_konly(&pte) ||
				    (rw == S_WRITE && pte_ronly(&pte))))
					be = SE_PROTERR;
				else
					be &= ~(SE_PROTERR|SE_INVALID);
			} else {
				be = SE_INVALID;
			}
		}
		if ((be & ~(SE_INVALID|SE_PROTERR)) != 0) {
			/*
			 * There was an error in the buserr register besides
			 * invalid and protection - we cannot handle it.
			 */
			res = FC_HWERR;
		} else if (stepped) {
			res = FC_NOMAP;
		} else if (addr > (caddr_t)KERNELBASE &&
		    type == T_DATA_FAULT + USER &&
		    lwp->lwp_pcb.pcb_step != STEP_NONE) {
			/*
			 * While single-stepping on newer model sparc chips,
			 * we can get a text fault on a prefetch through
			 * nPC (and beyond) while also taking a data fault
			 * on a load or store instruction that is being
			 * single-stepped.  The resulting condition is a
			 * data fault with the offending address being the
			 * prefetch address, not the load or store address.
			 * To work around this problem, we emulate the
			 * load or store instruction with do_unaligned().
			 */
			res = FC_NOMAP;
			switch (do_unaligned(rp, &badaddr)) {
			case SIMU_SUCCESS:
				rp->r_pc = rp->r_npc;
				rp->r_npc += 4;
				stepped = 1;
				break;
			case SIMU_FAULT:
				addr = badaddr;
				break;
			}
		} else {
			if (faultdebug) {
				char *fault_str;

				switch (rw) {
				case S_READ:
					fault_str = "read";
					break;
				case S_WRITE:
					fault_str = "write";
					break;
				case S_EXEC:
					fault_str = "exec";
					break;
				default:
					fault_str = "";
					break;
				}
				printf("user %s fault:  addr=0x%x be=0x%x\n",
				    fault_str, addr, be);
			}
			res = pagefault(addr,
				(be & SE_PROTERR)? F_PROT: F_INVAL, rw, 0);
			/*
			 * If pagefault() succeeded, ok.
			 * Otherwise attempt to grow the stack.
			 */
			if (res == 0 || (res == FC_NOMAP &&
			    type == T_DATA_FAULT + USER &&
			    addr < (caddr_t)KERNELBASE && grow((int *)addr))) {
				lwp->lwp_lastfault = FLTPAGE;
				lwp->lwp_lastfaddr = addr;
				if (prismember(&p->p_fltmask, FLTPAGE)) {
				    bzero((caddr_t)&siginfo, sizeof (siginfo));
				    siginfo.si_addr = addr;
				    (void) stop_on_fault(FLTPAGE, &siginfo);
				}
				goto out;
			}
		}

		if (tudebug)
			showregs(type, rp, addr, be, rw);
		/*
		 * In the case where both pagefault and grow fail,
		 * set the code to the value provided by pagefault.
		 * We map all errors returned from pagefault() to SIGSEGV.
		 */
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_addr = addr;
		switch (FC_CODE(res)) {
		case FC_HWERR:
		case FC_NOSUPPORT:
			siginfo.si_signo = SIGBUS;
			siginfo.si_code = BUS_ADRERR;
			fault = FLTACCESS;
			break;
		case FC_ALIGN:
			siginfo.si_signo = SIGBUS;
			siginfo.si_code = BUS_ADRALN;
			fault = FLTACCESS;
			break;
		case FC_OBJERR:
			if ((siginfo.si_errno = FC_ERRNO(res)) != EINTR) {
				siginfo.si_signo = SIGBUS;
				siginfo.si_code = BUS_OBJERR;
				fault = FLTACCESS;
			}
			break;
		default:	/* FC_NOMAP or FC_PROT */
			siginfo.si_signo = SIGSEGV;
			siginfo.si_code =
			    (res == FC_NOMAP)? SEGV_MAPERR : SEGV_ACCERR;
			fault = FLTBOUNDS;
			/*
			 * If this is the culmination of a single-step,
			 * reset the addr, code, signal and fault to
			 * indicate a hardware trace trap.
			 */
			if (stepped) {
				if (lwp->lwp_pcb.pcb_step == STEP_WASACTIVE) {
					lwp->lwp_pcb.pcb_step = STEP_NONE;
					lwp->lwp_pcb.pcb_tracepc = NULL;
					oldpc = rp->r_pc - 4;
					siginfo.si_code = TRAP_TRACE;
					siginfo.si_addr = (caddr_t)rp->r_pc;
					siginfo.si_signo = SIGTRAP;
					fault = FLTTRACE;
				} else {
					/*
					 * Another stop took place;
					 * cancel this trace trap.
					 */
					siginfo.si_signo = 0;
					fault = 0;
				}
			}
			break;
		}
		break;

	case T_ALIGNMENT + USER:	/* user alignment error */
		if (tudebug)
			showregs(type, rp, (caddr_t)0, 0, S_OTHER);
		/*
		 * if the user has to do unaligned references
		 * the ugly stuff gets done here
		 */
		alignfaults++;
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		if (lwp->lwp_pcb.pcb_flags & FIX_ALIGNMENT) {
			if (do_unaligned(rp, &badaddr) == SIMU_SUCCESS) {
				rp->r_pc = rp->r_npc;
				rp->r_npc += 4;
				goto out;
			}
			siginfo.si_signo = SIGSEGV;
			siginfo.si_code = SEGV_MAPERR;
			siginfo.si_addr = badaddr;
			fault = FLTBOUNDS;
		} else {
			siginfo.si_signo = SIGBUS;
			siginfo.si_code = BUS_ADRALN;
			if (rp->r_pc & 3) {	/* offending address, if pc */
				siginfo.si_addr = (caddr_t)rp->r_pc;
			} else {		/* address of ld/st reference */
				if (calc_memaddr(rp, &badaddr) == SIMU_SUCCESS)
					siginfo.si_addr = badaddr;
				else
					siginfo.si_addr = (caddr_t)rp->r_pc;
			}
			fault = FLTACCESS;
		}
		break;

	case T_PRIV_INSTR + USER:	/* privileged instruction fault */
		if (tudebug)
			showregs(type, rp, (caddr_t)0, 0, S_OTHER);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_signo = SIGILL;
		siginfo.si_code = ILL_PRVOPC;
		siginfo.si_addr = (char *)rp->r_pc;
		fault = FLTILL;
		break;

	case T_UNIMP_INSTR + USER:	/* illegal instruction fault */
		if (tudebug)
			showregs(type, rp, (caddr_t)0, 0, S_OTHER);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		/*
		 * Try to simulate the instruction.
		 */
		switch (simulate_unimp(rp, &badaddr)) {
		case SIMU_SUCCESS:
			rp->r_pc = rp->r_npc;
			rp->r_npc += 4;
			goto out;
			/*NOTREACHED*/
		case SIMU_FAULT:
			siginfo.si_signo = SIGSEGV;
			siginfo.si_code  = SEGV_MAPERR;
			siginfo.si_addr  = badaddr;
			fault = FLTBOUNDS;
			break;
		case SIMU_DZERO:
			siginfo.si_signo = SIGFPE;
			siginfo.si_code  = FPE_INTDIV;
			siginfo.si_addr  = (char *)rp->r_pc;
			fault = FLTIZDIV;
			break;
		case SIMU_UNALIGN:
			siginfo.si_signo = SIGBUS;
			siginfo.si_code  = BUS_ADRALN;
			siginfo.si_addr  = badaddr;
			fault = FLTACCESS;
			break;
		case SIMU_ILLEGAL:
		default:
			siginfo.si_signo = SIGILL;
			siginfo.si_code  = ILL_ILLOPC;
			siginfo.si_addr  = (caddr_t)rp->r_pc;
			fault = FLTILL;
			break;
		}
		break;

	case T_IDIV0 + USER:		/* integer divide by zero */
	case T_DIV0 + USER:		/* integer divide by zero */
		if (tudebug && tudebugfpe)
			showregs(type, rp, (caddr_t)0, 0, S_OTHER);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_signo = SIGFPE;
		siginfo.si_code  = FPE_INTDIV;
		siginfo.si_addr  = (caddr_t)rp->r_pc;
		fault = FLTIZDIV;
		break;

	case T_INT_OVERFLOW + USER:	/* integer overflow */
		if (tudebug && tudebugfpe)
			showregs(type, rp, (caddr_t)0, 0, S_OTHER);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_signo = SIGFPE;
		siginfo.si_code  = FPE_INTOVF;
		siginfo.si_addr  = (caddr_t)rp->r_pc;
		fault = FLTIOVF;
		break;

	case T_FP_DISABLED:
		/* occurs when probing for the fpu and it is not found */
		if (rp->r_psr & PSR_EF) {
			fpu_exists = 0;
			rp->r_psr &= ~PSR_EF;
			rp->r_pc = rp->r_npc;
			rp->r_npc += 4;
			goto cleanup;
		}
		/* this shouldn't happen */
		(void) die(type, rp, addr, be, rw);
		/*NOTREACHED*/

	case T_FP_DISABLED + USER:	/* FPU disabled trap */
		if (tudebug && tudebugfpe)
			showregs(type, rp, addr, 0, S_OTHER);
		fp_disabled(rp);
		goto out;

	case T_FP_EXCEPTION + USER:	/* FPU arithmetic exception */
		if (tudebug && tudebugfpe)
			showregs(type, rp, addr, 0, S_OTHER);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		oldpc = (greg_t)addr;
		siginfo.si_signo = SIGFPE;
		siginfo.si_code  = be;
		siginfo.si_addr  = addr;
		fault = FLTFPE;
		break;

	case T_BREAKPOINT + USER:	/* breakpoint trap (t 1) */
		if (tudebug && tudebugbpt)
			showregs(type, rp, (caddr_t)0, 0, S_OTHER);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_signo = SIGTRAP;
		siginfo.si_code  = TRAP_BRKPT;
		siginfo.si_addr  = (caddr_t)rp->r_pc;
		fault = FLTBPT;
		break;

	case T_TAG_OVERFLOW + USER:	/* tag overflow (taddcctv, tsubcctv) */
		if (tudebug)
			showregs(type, rp, (caddr_t)0, 0, S_OTHER);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_signo = SIGEMT;
		siginfo.si_code  = EMT_TAGOVF;
		siginfo.si_addr  = (caddr_t)rp->r_pc;
		fault = FLTACCESS;
		break;

	case T_WIN_OVERFLOW + USER:	/* finish user window overflow */
		/*
		 * This trap is entered from sys_rtt in locore.s when, upon
		 * return to user is is found that there are user windows in
		 * u.u_wbuf. This happens because they could not be saved on
		 * the user stack, either because it wasn't resident or because
		 * it was misaligned. We flush the user's windows to make
		 * sure pcb_wbcnt does not change while we are doing this.
		 */
	    {
		register int j;

		bzero((caddr_t)&siginfo, sizeof (siginfo));
		flush_user_windows();
		for (j = 0; j < lwp->lwp_pcb.pcb_wbcnt; j++) {
			register char *sp;
			register int error;

			sp = lwp->lwp_pcb.pcb_spbuf[j];
			if ((int)sp & (STACK_ALIGN-1)) {
				siginfo.si_signo = SIGILL;
				siginfo.si_code  = ILL_BADSTK;
				siginfo.si_addr  = (caddr_t)rp->r_pc;
				fault = FLTILL;
				break;
			}
			/*
			 * If the xcopyout fails then zap the process with
			 * a SIGSEGV - process may be managing its own stack
			 * growth by taking SIGSEGVs on a different signal
			 * stack.
			 */
			error = xcopyout((caddr_t)&lwp->lwp_pcb.pcb_wbuf[j],
			    sp, sizeof (struct rwindow));
			/*
			 * EINTR comes from a signal during pagefault.
			 * We should not post another signal.
			 */
			if (error == EINTR)
				break;
			if (error != 0) {
				siginfo.si_signo = SIGSEGV;
				siginfo.si_code  = SEGV_MAPERR;
				siginfo.si_addr  = (caddr_t)sp;
				fault = FLTBOUNDS;
				break;
			}
		}
		if (j == lwp->lwp_pcb.pcb_wbcnt) {
			lwp->lwp_pcb.pcb_wbcnt = 0;
			goto out;
		} else if (j) {
			register int i;

			/*
			 * Normalize lwp->lwp_pcb.pcb_wbuf for possible
			 * return to user signal handler.
			 */
			lwp->lwp_pcb.pcb_wbcnt -= j;
			i = 0;
			while (i < lwp->lwp_pcb.pcb_wbcnt) {
				bcopy((caddr_t)&lwp->lwp_pcb.pcb_wbuf[j++],
				    (caddr_t)&lwp->lwp_pcb.pcb_wbuf[i++],
				    sizeof (struct rwindow));
			}
		}
		if (tudebug)
			showregs(type, rp, (caddr_t)0, 0, S_OTHER);
	    }
		break;

	case T_AST + USER:		/* profiling or resched psuedo trap */
		if (lwp->lwp_oweupc && lwp->lwp_prof.pr_scale) {
			mutex_enter(&p->p_pflock);
			addupc((void(*)())rp->r_pc, &lwp->lwp_prof, 1);
			mutex_exit(&p->p_pflock);
			lwp->lwp_oweupc = 0;
		}
		break;
	}

	/*
	 * We can't get here from a system trap
	 */
	ASSERT(type & USER);

	if (fault) {
		/*
		 * Remember the fault and fault adddress
		 * for real-time (SIGPROF) profiling.
		 */
		lwp->lwp_lastfault = fault;
		lwp->lwp_lastfaddr = siginfo.si_addr;
		/*
		 * If a debugger has declared this fault to be an
		 * event of interest, stop the lwp.  Otherwise just
		 * deliver the associated signal.
		 */
		if (siginfo.si_signo != SIGKILL &&
		    prismember(&p->p_fltmask, fault) &&
		    stop_on_fault(fault, &siginfo) == 0)
			siginfo.si_signo = 0;
	}

	if (siginfo.si_signo)
		trapsig(&siginfo, oldpc == rp->r_pc);

	if (lwp->lwp_prof.pr_scale) {
		int ticks;
		clock_t tv = p->p_stime;

		ticks = (tv - syst) * 1000/HZ;

		if (ticks) {
			mutex_enter(&p->p_pflock);
			addupc((void(*)())rp->r_pc, &lwp->lwp_prof, ticks);
			mutex_exit(&p->p_pflock);
		}
	}

	if (ISHOLD(p))
		holdlwp();

	/*
	 * An AST is set on the current thread when
	 * a signal is posted to a preempted thread.
	 */
	if (curthread->t_astflag | curthread->t_sig_check) {
		/*
		 * Turn off the AST flag before checking all the conditions that
		 * may have caused an AST.  This flag is on whenever a signal or
		 * unusual condition should be handled after the next trap or
		 * syscall.
		 */
		astoff(curthread);
		curthread->t_sig_check = 0;

		/*
		 * for kaio requests that are on the per-process poll queue,
		 * aiop->aio_pollq, they're AIO_POLL bit is set, the kernel
		 * should copyout their result_t to user memory. by copying
		 * out the result_t, the user can poll on memory waiting
		 * for the kaio request to complete.
		 */
		if (p->p_aio)
			aio_cleanup();

		/*
		 * If this LWP was asked to hold, call holdlwp(), which will
		 * stop.  holdlwps() sets this up and calls pokelwps() which
		 * sets the AST flag.
		 *
		 * Also check TP_EXITLWP, since this is used by fresh new LWPs
		 * through lwp_rtt().  That flag is set if the lwp_create(2)
		 * syscall failed after creating the LWP.
		 */
		if (ISHOLD(p))
			holdlwp();

		/*
		 * All code that sets signals and makes ISSIG evaluate true must
		 * set t_astflag afterwards.
		 */
		if (ISSIG_PENDING(curthread, lwp, p)) {
			if (issig(FORREAL))
				psig();
			curthread->t_sig_check = 1;
		}

		if (curthread->t_rprof != NULL) {
			realsigprof(0, 0);
			curthread->t_sig_check = 1;
		}
	}

out:	/* We can't get here from a system trap */
	ASSERT(type & USER);

	/*
	 * Restore register window if a debugger modified it.
	 * Set up to perform a single-step if a debugger requested it.
	 */
	if (lwp->lwp_pcb.pcb_xregstat != XREGNONE)
		xregrestore(lwp, 0);

	/*
	 * Set state to LWP_USER here so preempt won't give us a kernel
	 * priority if it occurs after this point.  Call CL_TRAPRET() to
	 * restore the user-level priority.
	 *
	 * It is important that no locks (other than spinlocks) be entered
	 * after this point before returning to user mode (unless lwp_state
	 * is set back to LWP_SYS).
	 */
	lwp->lwp_state = LWP_USER;
	if (curthread->t_trapret) {
		curthread->t_trapret = 0;
		thread_lock(curthread);
		CL_TRAPRET(curthread);
		thread_unlock(curthread);
	}
	if (CPU->cpu_runrun)
		preempt();
	if (lwp->lwp_pcb.pcb_step != STEP_NONE)
		prdostep();
	if (curthread->t_proc_flag & TP_MSACCT)
		(void) new_mstate(curthread, mstate);

	TRACE_0(TR_FAC_TRAP, TR_C_TRAP_HANDLER_EXIT, "C_trap_handler_exit");

	return;

cleanup:	/* system traps end up here */
	ASSERT(!(type & USER));

	/*
	 * If the unsafe_driver mutex was held by the thread on entry,
	 * we released it so we could call other drivers.  We re-enter it here.
	 */
	if (driver_mutex)
		mutex_enter(&unsafe_driver);

	TRACE_0(TR_FAC_TRAP, TR_C_TRAP_HANDLER_EXIT, "C_trap_handler_exit");
}

/*
 * Wrapper for hat_fault traps.
 */
faultcode_t
hat_fault_trap(register struct hat *hat, caddr_t addr)
{
	register klwp_t	*lwp;
	register faultcode_t fc;
	register char old_state;

	/*
	 * Set LWP state to LWP_SYS so if preempted while holding hat
	 * locks, the thread will be given kernel priority.
	 */
	lwp = curthread->t_lwp;
	if (lwp != NULL) {
		old_state = lwp->lwp_state;
		lwp->lwp_state = LWP_SYS;
	}

	/*
	 * Do not allow preemption while in hat fault.
	 * Part of preempting the current thread is syncfpu(),
	 * which might need to simulate an fp instruction that
	 * may need to reenter the hat layer for mappings.
	 * This is a cheap way to avoid the above lock recursion.
	 */
	kpreempt_disable();

	/*
	 * Call hat_fault() to handle the fault.
	 */
	fc = hat_fault(hat, addr);

	kpreempt_enable();

	/*
	 * Set state to LWP_USER here so preempt won't give us a kernel
	 * priority if it occurs after this point.  Call CL_TRAPRET() to
	 * restore the user-level priority.
	 *
	 * It is important that no locks (other than spinlocks) be entered
	 * after this point before returning to user mode (unless lwp_state
	 * is set back to LWP_SYS).
	 */
	if (lwp != NULL && old_state != LWP_SYS) {
		lwp->lwp_state = old_state;
		if (curthread->t_trapret) {
			curthread->t_trapret = 0;
			thread_lock(curthread);
			CL_TRAPRET(curthread);
			thread_unlock(curthread);
		}
	}
	return (fc);
}

int IGNORE_KERNEL_PREEMPTION = 0;
struct kpreempt_cnts {		/* kernel preemption statistics */
	int	kpc_idle;	/* executing idle thread */
	int	kpc_intr;	/* executing interrupt thread */
	int	kpc_clock;	/* executing clock thread */
	int	kpc_blocked;	/* thread has blocked preemption (t_preempt) */
	int	kpc_notonproc;	/* thread is surrendering processor */
	int	kpc_prilevel;	/* processor interrupt level is too high */
	int	kpc_apreempt;	/* asynchronous preemption */
	int	kpc_spreempt;	/* synchronous preemption */
}	kpreempt_cnts;

/*
 * kernel preemption: forced rescheduling, preempt the running kernel thread.
 *	a non-zero value of asyncspl indicates preemption after an interrupt,
 *	and includes the old IPL.
 */
void
kpreempt(int asyncspl)
{
	if (IGNORE_KERNEL_PREEMPTION) {
		aston(CPU->cpu_dispthread);
		return;
	}
	/*
	 * Check that conditions are right for kernel preemption
	 */
	do {
		if (curthread->t_preempt) {
			/*
			 * either a privileged thread (idle, panic, interrupt)
			 *	or will check when t_preempt is lowered
			 */
			if (curthread->t_pri < 0)
				kpreempt_cnts.kpc_idle++;
			else if (curthread->t_flag & T_INTR_THREAD) {
				kpreempt_cnts.kpc_intr++;
				if (curthread == clock_thread)
					kpreempt_cnts.kpc_clock++;
			} else
				kpreempt_cnts.kpc_blocked++;
			aston(CPU->cpu_dispthread);
			return;
		}
		if (curthread->t_state != TS_ONPROC) {
			/* this thread will be calling swtch() shortly */
			kpreempt_cnts.kpc_notonproc++;
			return;
		}

		if ((asyncspl ? spltoipl(asyncspl) : getpil()) >= LOCK_LEVEL) {
			/*
			 * We can't preempt this thread if it is at
			 * a PIL > LOCK_LEVEL since it may be holding
			 * a spin lock (like sched_lock).
			 */
			siron();	/* check back later */
			kpreempt_cnts.kpc_prilevel++;
			return;
		}

		/*
		 * Take any pending fpu exceptions now.
		 */
		syncfpu();

		/*
		 * block preemption so we don't have multiple preemptions
		 * pending on the interrupt stack
		 */
		curthread->t_preempt++;
		if (asyncspl) {
			/* interrupt, restore previous PIL */
			(void) splx(asyncspl);
			kpreempt_cnts.kpc_apreempt++;
		} else
			kpreempt_cnts.kpc_spreempt++;

		preempt();
		curthread->t_preempt--;
	} while (CPU->cpu_kprunrun);
}

/*
 * Print out a traceback for kernel traps
 */
void
traceback(sp)
	caddr_t sp;
{
	register u_int tospage;
	register struct frame *fp;
	static int done = 0;

	if (panicstr && done++ > 0)
		return;

	if ((int)sp & (STACK_ALIGN-1)) {
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
#if !defined(SAS) && !defined(MPSAS)
#ifdef VAC
	/* push msgbuf to mem */
	vac_flush((caddr_t)&msgbuf, (int)sizeof (msgbuf));
#endif /* VAC */
	DELAY(2000000);
#endif /* !SAS  && !MPSAS */
}

/*
 * General system stack backtrace
 */
void
tracedump(void)
{
	label_t l;

	(void) setjmp(&l);
	traceback((caddr_t)l.val[1]);
}

#ifdef TRAPWINDOW
long trap_window[25];
#endif /* TRAPWINDOW */

/*
 * Print out debugging info.
 */
static void
showregs(type, rp, addr, be, rw)
	register unsigned type;
	register struct regs *rp;
	caddr_t addr;
	u_int be;
	enum seg_rw rw;
{
	int s = spl7();

	type &= ~USER;
	printf("%s: ", u.u_comm);
	if (type < TRAP_TYPES)
		printf("%s\n", trap_type[type]);
	else switch (type) {
	case T_SYSCALL:
		printf("syscall trap:\n");
		break;
	case T_BREAKPOINT:
		printf("breakpoint trap:\n");
		break;
	case T_DIV0:
		printf("zero divide trap:\n");
		break;
	case T_FLUSH_WINDOWS:
		printf("flush windows trap:\n");
		break;
	case T_SPURIOUS:
		printf("spurious interrupt:\n");
		break;
	case T_AST:
		printf("AST\n");
		break;
	default:
		if (type >= T_SOFTWARE_TRAP && type <= T_ESOFTWARE_TRAP)
			printf("software trap 0x%x\n", type - T_SOFTWARE_TRAP);
		else
			printf("bad trap = %d\n", type);
		break;
	}
	if (type == T_DATA_FAULT || type == T_TEXT_FAULT) {
		if (good_addr(addr)) {
			struct pte pte;

			mmu_getpte(addr, &pte);
			printf("%s %s fault at addr=0x%x, pme=0x%x\n",
			    (USERMODE(rp->r_psr)? "user": "kernel"),
			    (rw == S_WRITE? "write": "read"),
			    addr, *(int *)&pte);
		} else {
			printf("bad %s %s fault at addr=0x%x\n",
			    (USERMODE(rp->r_psr)? "user": "kernel"),
			    (rw == S_WRITE? "write": "read"),
			    addr);
		}
		printf("Sync Error Reg %b\n", be, SYNCERR_BITS);
	} else if (addr) {
		printf("addr=0x%x\n", addr);
	}
	printf("pid=%d, pc=0x%x, sp=0x%x, psr=0x%x, context=%d\n",
	    (ttoproc(curthread) && ttoproc(curthread)->p_pidp) ?
	    (ttoproc(curthread)->p_pid) : 0, rp->r_pc, rp->r_sp,
	    rp->r_psr, map_getctx());
	if (USERMODE(rp->r_psr)) {
		printf("o0-o7: %x, %x, %x, %x, %x, %x, %x, %x\n",
		    rp->r_o0, rp->r_o1, rp->r_o2, rp->r_o3,
		    rp->r_o4, rp->r_o5, rp->r_o6, rp->r_o7);
	}
	printf("g1-g7: %x, %x, %x, %x, %x, %x, %x\n",
	    rp->r_g1, rp->r_g2, rp->r_g3,
	    rp->r_g4, rp->r_g5, rp->r_g6, rp->r_g7);
#ifdef TRAPWINDOW
	printf("trap_window: wim=%x\n", trap_window[24]);
	printf("o0-o7: %x, %x, %x, %x, %x, %x, %x, %x\n",
	    trap_window[0], trap_window[1], trap_window[2], trap_window[3],
	    trap_window[4], trap_window[5], trap_window[6], trap_window[7]);
	printf("l0-l7: %x, %x, %x, %x, %x, %x, %x, %x\n",
	    trap_window[8], trap_window[9], trap_window[10], trap_window[11],
	    trap_window[12], trap_window[13], trap_window[14], trap_window[15]);
	printf("i0-i7: %x, %x, %x, %x, %x, %x, %x, %x\n",
	    trap_window[16], trap_window[17], trap_window[18], trap_window[19],
	    trap_window[20], trap_window[21], trap_window[22], trap_window[23]);
#endif /* TRAPWINDOW */
#ifdef VAC
	/* push msgbuf to mem */
	vac_flush((caddr_t)&msgbuf, (int)sizeof (msgbuf));
#endif /* VAC */
#if !defined(SAS) && !defined(MPSAS)
	if (tudebug > 1 && (boothowto & RB_DEBUG)) {
		debug_enter((char *)NULL);
	}
#endif /* SAS */
	(void) splx(s);
}
