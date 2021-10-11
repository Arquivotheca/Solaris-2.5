/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)trap.c	1.58	95/10/15 SMI"

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
#include <sys/spl.h>
#include <sys/vm.h>
#include <sys/msgbuf.h>
#include <sys/sysinfo.h>
#include <sys/fault.h>
#include <sys/frame.h>
#include <sys/stack.h>
#include <sys/mmu.h>
#include <sys/pcb.h>
#include <sys/pte.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/kmem.h>
#include <sys/vtrace.h>
#include <sys/x_call.h>
#include <sys/prsystm.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/fpu/fpusystm.h>
#include <sys/fpu/fpu_simulator.h>
#include <sys/fpu/globals.h>
#include <sys/cmn_err.h>
#include <sys/mutex_impl.h>
#include <sys/module.h>
#include <sys/tnf.h>
#include <sys/tnf_probe.h>
#include <sys/privregs.h>
#include <sys/machpcb.h>
#include <sys/simulate.h>

#include <vm/hat.h>
#include <vm/hat_sfmmu.h>

#include <vm/seg_kmem.h>
#include <vm/as.h>
#include <vm/seg.h>

#include <sys/procfs.h>

#include <sys/modctl.h>

#include <sys/reboot.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>

#ifdef SYSCALLTRACE
int syscalltrace = 0;
#endif /* SYSCALLTRACE */

extern char *syscallnames[];

static int tudebug = 0;
static int tudebugbpt = 0;
static int tudebugfpe = 0;

static int alignfaults = 0;

#if defined(TRAPDEBUG) || defined(lint)
static int lodebug = 0;
#else
#define	lodebug	0
#endif defined(TRAPDEBUG) || defined(lint)

static void showregs(unsigned, struct regs *, caddr_t, u_int);
static enum seg_rw get_accesstype(struct regs *);
static int nfload(struct regs *);
static int swap_nc(struct regs *, int);
static int ldstub_nc(struct regs *, int);
void	trap_cleanup(struct regs *, u_int, k_siginfo_t *, int, clock_t);
void	trap_rtt(void);

static int
die(type, rp, addr, mmu_fsr)
	unsigned type;
	struct regs *rp;
	caddr_t addr;
	u_int mmu_fsr;
{
	char *trap_name = "trap";

#ifdef TRAPTRACE
	TRAPTRACE_FREEZE;
#endif

	setup_panic(trap_name, (va_list)NULL);

	printf("BAD TRAP: cpu=%d type=%x rp=%x addr=%x mmu_fsr=%x\n",
	    CPU->cpu_id, type, rp, addr, mmu_fsr);

	if (type == T_DATA_MMU_MISS && addr < (caddr_t)KERNELBASE) {
		char modname[MODMAXNAMELEN];

		if (mod_containing_pc((caddr_t)rp->r_pc, modname)) {
			printf("BAD TRAP occurred in module \"%s\" due to "
			    "an illegal access to a user address.\n",
			    modname);
		}
	}

	showregs(type, rp, addr, mmu_fsr);
	traceback((caddr_t)rp->r_sp);

	cmn_err(CE_PANIC, trap_name);
	return (0);	/* avoid optimization of restore in call's delay slot */
}

void
mmu_print_sfsr(sfsr)
	u_int sfsr;
{
	printf("MMU sfsr=%x:", sfsr);
	switch (X_FAULT_TYPE(sfsr)) {
	case FT_NONE:
		printf(" No Error");
		break;
	case FT_PRIV:
		printf(" Privilege Violation");
		break;
	case FT_SPEC_LD:
		printf(" Speculative Load on E-bit Page");
		break;
	case FT_ATOMIC_NC:
		printf(" Atomic to Uncacheable Page");
		break;
	case FT_ILL_ALT:
		printf(" Illegal lda or sta");
		break;
	case FT_NFO:
		printf(" Normal Access to NFO Page");
		break;
	case FT_RANGE_PC:
		printf(" PC Out of Range");
		break;
	case FT_RANGE_REG:
		printf(" Jump to Register out of Range");
	default:
		printf(" Unknown Error");
		break;
	}
	if (sfsr) {
		printf(" on ASI 0x%x E %d CID %d PRIV %d W %d OW %d FV %d",
			(sfsr & SFSR_ASI) >> SFSR_ASI_SHIFT,
			(sfsr & SFSR_E) != 0,
			(sfsr & SFSR_CTX) >> SFSR_CT_SHIFT,
			(sfsr & SFSR_PR) != 0,
			(sfsr & SFSR_W) != 0,
			(sfsr & SFSR_OW) != 0,
			(sfsr & SFSR_FV) != 0);
	}
	printf("\n");
}

#ifndef	CALL_WORKS
int	ill_calls;
#endif

#define	IS_FLUSH(i)	(((i) & 0xc1f80000) == 0x81d80000)
#define	IS_SWAP(i)	(((i) & 0xc1f80000) == 0xc0780000)
#define	IS_LDSTUB(i)	(((i) & 0xc1f80000) == 0xc0680000)

/*
 * Called from the trap handler when a processor trap occurs.
 */
/*VARARGS2*/
void
trap(rp, type, mmu_fsr, addr)
	register struct regs *rp;
	register unsigned type;
	u_int mmu_fsr;
	caddr_t addr;
{
	proc_t *p = ttoproc(curthread);
	klwp_id_t lwp = ttolwp(curthread);
	struct machpcb *mpcb = NULL;
	clock_t syst;
	k_siginfo_t siginfo;
	u_int fault = 0;
	int driver_mutex = 0;	/* get unsafe_driver before returning */
	int stepped = 0;
	greg_t oldpc;
	int mstate;
	char *badaddr;
	faultcode_t res;
	u_int fault_type;
	enum seg_rw rw;
	u_int lofault;
	int instr;
	int iskernel;
	extern faultcode_t pagefault(caddr_t, enum fault_type,
		enum seg_rw, int);

	CPU_STAT_ADDQ(CPU, cpu_sysinfo.trap, 1);
	syst = p->p_stime;

	ASSERT(curthread->t_schedflag & TS_DONT_SWAP);

	if (USERMODE(rp->r_tstate) || (type & T_USER)) {
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
		type |= T_USER;
		ASSERT((type == (T_WIN_UNDERFLOW | T_USER)) ||
			lwp->lwp_regs == rp);
		lwp->lwp_state = LWP_SYS;
		mpcb = lwptompcb(lwp);
		if (curthread->t_proc_flag & TP_MSACCT || tnf_tracing_active) {
			switch (type) {
			case T_DATA_MMU_MISS + T_USER:
			case T_WIN_UNDERFLOW + T_USER:
				mstate = LMS_DFAULT;
				break;
			case T_INSTR_MMU_MISS + T_USER:
				mstate = LMS_TFAULT;
				break;
			default:
				mstate = LMS_TRAP;
				break;
			}
			/* Kernel probe */
			TNF_PROBE_1(thread_state, "thread", /* CSTYLED */,
				tnf_microstate, state, (char)mstate);
			if (curthread->t_proc_flag & TP_MSACCT)
				mstate = new_mstate(curthread, mstate);
		} else {
			mstate = LMS_USER;
		}
		siginfo.si_signo = 0;
		stepped =
		    lwp->lwp_pcb.pcb_step != STEP_NONE &&
		    ((oldpc = rp->r_pc), prundostep()) &&
		    mmu_btop(addr) == mmu_btop(oldpc);
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

#ifdef	F_DEFERRED
	/*
	 * Take any pending floating point exceptions now.
	 * If the floating point unit has an exception to handle,
	 * just return to user-level to let the signal handler run.
	 * The instruction that got us to trap() will be reexecuted on
	 * return from the signal handler and we will trap to here again.
	 * This is necessary to disambiguate simultaneous traps which
	 * happen when a floating-point exception is pending and a
	 * machine fault is incurred.
	 */
	if (type & USER) {
		/*
		 * FP_TRAPPED is set only by sendsig() when it copies
		 * out the floating-point queue for the signal handler.
		 * It is set there so we can test it here and in syscall().
		 */
		mpcb->mpcb_flags &= ~FP_TRAPPED;
		syncfpu();
		if (mpcb->mpcb_flags & FP_TRAPPED) {
			/*
			 * trap() has have been called recursively and may
			 * have stopped the process, so do single step
			 * support for /proc.
			 */
			mpcb->mpcb_flags &= ~FP_TRAPPED;
			goto out;
		}
	}
#endif

	switch (type) {

	default:
		/*
		 * Check for user software trap.
		 */
		if (type & T_USER) {
			if (tudebug)
				showregs(type, rp, (caddr_t)0, 0);
			if ((type & ~T_USER) >= T_SOFTWARE_TRAP) {
				bzero((caddr_t)&siginfo, sizeof (siginfo));
				siginfo.si_signo = SIGILL;
				siginfo.si_code  = ILL_ILLTRP;
				siginfo.si_addr  = (caddr_t)rp->r_pc;
				siginfo.si_trapno = type &~ T_USER;
				fault = FLTILL;
				break;
			}
		}
		addr = (caddr_t)rp->r_pc;
		(void) die(type, rp, addr, 0);
		/*NOTREACHED*/

	case T_ALIGNMENT:	/* supv alignment error */
		if (nfload(rp))
			goto out;
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
		(void) die(type, rp, addr, 0);
		/*NOTREACHED*/

	case T_INSTR_EXCEPTION:		/* sys instruction access exception */
		addr = (caddr_t)rp->r_pc;
		(void) die(type, rp, addr, mmu_fsr);
		/*NOTREACHED*/

	case T_INSTR_MMU_MISS:		/* sys instruction mmu miss */
		addr = (caddr_t)rp->r_pc;
		(void) die(type, rp, addr, 0);
		/*NOTREACHED*/

	case T_DATA_EXCEPTION:		/* system data access exception */
		if (X_FAULT_TYPE(mmu_fsr) != FT_NFO) {
			if (nfload(rp))
				goto out;
			addr = (caddr_t)((uint)addr & TAGACC_VADDR_MASK);
			(void) die(type, rp, addr, mmu_fsr);
			/*NOTREACHED*/
		}
		/* fall into ... */

	case T_DATA_MMU_MISS:		/* system data mmu miss */
	case T_DATA_PROT:		/* system data protection fault */
		if (nfload(rp))
			goto out;
		/*
		 * XXX Can we move some if this junk code to pagefault?
		 */
		/* may have been expected by C (e.g. bus probe) */
		if (curthread->t_nofault) {
			label_t *ftmp;

			ftmp = curthread->t_nofault;
			curthread->t_nofault = 0;
#ifndef LOCKNEST
			if (driver_mutex)
				mutex_enter(&unsafe_driver);
#endif	/* LOCKNEST */
			TRACE_0(TR_FAC_TRAP, TR_C_TRAP_HANDLER_EXIT,
				"C_trap_handler_exit");
			TRACE_0(TR_FAC_TRAP, TR_TRAP_END, "trap_end");
			longjmp(ftmp);
		}
		lofault = curthread->t_lofault;
		curthread->t_lofault = 0;

		if (curthread->t_proc_flag & TP_MSACCT)
			mstate = new_mstate(curthread, LMS_KFAULT);
		else
			mstate = LMS_SYSTEM;

		switch (type) {
		case T_DATA_PROT:
			fault_type = F_PROT;
			rw = S_WRITE;
			break;
		case T_INSTR_MMU_MISS:
			fault_type = F_INVAL;
			rw = S_EXEC;
			break;
		case T_DATA_MMU_MISS:
		case T_DATA_EXCEPTION:
			/*
			 * The hardware doesn't update the sfsr on mmu
			 * misses so it is not easy to find out whether
			 * the access was a read or a write so we need
			 * to decode the actual instruction.
			 *  XXX BUGLY HW
			 */
			fault_type = F_INVAL;
			rw = get_accesstype(rp);
			break;
		default:
			cmn_err(CE_PANIC, "trap: unknown type %x\n", type);
			break;
		}
		/*
		 * We determine if access was done to kernel or user
		 * address space.  The addr passed into trap is really the
		 * tag access register.
		 */
		iskernel = (((uint)addr & TAGACC_CTX_MASK) == KCONTEXT);
		addr = (caddr_t)((uint)addr & TAGACC_VADDR_MASK);

		/*
		 * We assume that everything below KERNELBASE is a user
		 * address.  XXX Can we fix this?  Do all kernel deamons
		 * depend on this?  I know icode does - ali
		 */
		if (iskernel) {
			res = pagefault(addr, fault_type, rw, iskernel);
		} else {
			res = pagefault(addr, fault_type, rw, iskernel);
			if ((res == FC_NOMAP) && (addr < (caddr_t)USRSTACK) &&
			    grow((int *)addr))
				res = 0;
		}

		if (curthread->t_proc_flag & TP_MSACCT)
			(void) new_mstate(curthread, mstate);

		/*
		 * Restore lofault.  If we resolved the fault, exit.
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

		if (lofault == 0)
			(void) die(type, rp, addr, 0);
		/*
		 * Cannot resolve fault.  Return to lofault.
		 */
		if (lodebug) {
			showregs(type, rp, addr, 0);
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

	case T_INSTR_EXCEPTION + T_USER: /* user instruction access exception */
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_addr = (caddr_t)rp->r_pc;
		siginfo.si_signo = SIGSEGV;
		siginfo.si_code = X_FAULT_TYPE(mmu_fsr) == FT_PRIV ?
			SEGV_ACCERR : SEGV_MAPERR;
		fault = FLTBOUNDS;
		break;

	case T_INSTR_MMU_MISS + T_USER:	/* user instruction mmu miss */
	case T_DATA_MMU_MISS + T_USER:	/* user data mmu miss */
	case T_DATA_PROT + T_USER:	/* user data protection fault */
	case T_WIN_UNDERFLOW + T_USER:	/* window underflow in user_rtt */
		iskernel = 0;
		switch (type) {
		case T_INSTR_MMU_MISS + T_USER:
			addr = (caddr_t)rp->r_pc;
			fault_type = F_INVAL;
			rw = S_EXEC;
			break;

		case T_DATA_MMU_MISS + T_USER:
			addr = (caddr_t)((uint)addr & TAGACC_VADDR_MASK);
			fault_type = F_INVAL;
			/*
			 * The hardware doesn't update the sfsr on mmu misses
			 * so it is not easy to find out whether the access
			 * was a read or a write so we need to decode the
			 * actual instruction.  XXX BUGLY HW
			 */
			rw = get_accesstype(rp);
			break;

		case T_DATA_PROT + T_USER:
			addr = (caddr_t)((uint)addr & TAGACC_VADDR_MASK);
			fault_type = F_PROT;
			rw = S_WRITE;
			break;

		case T_WIN_UNDERFLOW + T_USER:
			addr = (caddr_t)((uint)addr & TAGACC_VADDR_MASK);
			fault_type = F_INVAL;
			rw = S_READ;
			break;

		default:
			cmn_err(CE_PANIC, "trap: unknown type %x\n", type);
			break;
		}
		/*
		 * If we are single stepping do not call pagefault
		 */
		if (stepped) {
			res = FC_NOMAP;
			goto badbus;
		}

		res = pagefault(addr, fault_type, rw, 0);

		/*
		 * If pagefault succeed, ok.
		 * Otherwise grow the stack automatically.
		 */
		if (res == 0 || (res == FC_NOMAP &&
		    (type == T_DATA_MMU_MISS + T_USER) &&
		    (addr < (caddr_t)USRSTACK) && grow((int *)addr))) {
			lwp->lwp_lastfault = FLTPAGE;
			lwp->lwp_lastfaddr = addr;
			if (prismember(&p->p_fltmask, FLTPAGE)) {
				bzero((caddr_t)&siginfo,
				    sizeof (siginfo));
				siginfo.si_addr = addr;
				(void) stop_on_fault(FLTPAGE, &siginfo);
			}
			goto out;
		}
		/*
		 * check if the pc was a flush instruction.  The ABI
		 * allows users to especify an illegal address on the flush
		 * instruction so we simply return in this case.
		 * XXX The hardware should set a bit indicating this trap was
		 * caused by a flush instruction.  Instruction decoding is
		 * bugly!
		 */
		if (type != (T_INSTR_MMU_MISS + T_USER)) {
			if (nfload(rp))
				goto out;
			instr = fuword((int *)rp->r_pc);
			if (IS_FLUSH(instr)) {
				/* skip the flush instruction */
				rp->r_pc = rp->r_npc;
				rp->r_npc += 4;
				goto out;
				/*NOTREACHED*/
			}
		}

		if (tudebug)
			showregs(type, rp, addr, 0);

		/*
		 * In the case where both pagefault and grow fail,
		 * set the code to the value provided by pagefault.
		 */
	badbus:
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_addr = addr;
		if (FC_CODE(res) == FC_OBJERR) {
			siginfo.si_errno = FC_ERRNO(res);
			if (siginfo.si_errno != EINTR) {
				siginfo.si_signo = SIGBUS;
				siginfo.si_code = BUS_OBJERR;
				fault = FLTACCESS;
			}
		} else { /* FC_NOMAP || FC_PROT */
			siginfo.si_signo = SIGSEGV;
			siginfo.si_code = (res == FC_NOMAP)?
				SEGV_MAPERR : SEGV_ACCERR;
			fault = FLTBOUNDS;
		}
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

	case T_DATA_EXCEPTION + T_USER:	/* user data access exception */
		if (nfload(rp))
			goto out;
		instr = fuword((int *)rp->r_pc);
		if (IS_FLUSH(instr)) {
			/* skip the flush instruction */
			rp->r_pc = rp->r_npc;
			rp->r_npc += 4;
			goto out;
			/*NOTREACHED*/
		}
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_addr = addr;
		switch (X_FAULT_TYPE(mmu_fsr)) {
		case FT_ATOMIC_NC:
			if ((IS_SWAP(instr) && swap_nc(rp, instr)) ||
			    (IS_LDSTUB(instr) && ldstub_nc(rp, instr))) {
				/* skip the atomic */
				rp->r_pc = rp->r_npc;
				rp->r_npc += 4;
				goto out;
			}
			/* fall into ... */
		case FT_PRIV:
			siginfo.si_signo = SIGSEGV;
			siginfo.si_code = SEGV_ACCERR;
			fault = FLTBOUNDS;
			break;
		case FT_SPEC_LD:
		case FT_ILL_ALT:
			siginfo.si_signo = SIGILL;
			siginfo.si_code = ILL_ILLADR;
			fault = FLTILL;
			break;
		default:
			siginfo.si_signo = SIGSEGV;
			siginfo.si_code = SEGV_MAPERR;
			fault = FLTBOUNDS;
			break;
		}
		break;

	case T_ALIGNMENT + T_USER:	/* user alignment error */
		if (tudebug)
			showregs(type, rp, addr, 0);
		/*
		 * If the user has to do unaligned references
		 * the ugly stuff gets done here.
		 */
		alignfaults++;
		if (nfload(rp))
			goto out;
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
			} else {
				if (calc_memaddr(rp, &badaddr) == SIMU_UNALIGN)
					siginfo.si_addr = badaddr;
				else
					siginfo.si_addr = (caddr_t)rp->r_pc;
			}
			fault = FLTACCESS;
		}
		break;

	case T_PRIV_INSTR + T_USER:	/* privileged instruction fault */
		if (tudebug)
			showregs(type, rp, (caddr_t)0, 0);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_signo = SIGILL;
		siginfo.si_code = ILL_PRVOPC;
		siginfo.si_addr = (caddr_t)rp->r_pc;
		fault = FLTILL;
		break;

#ifndef	CALL_WORKS
	case T_UNIMP_INSTR:		/* illegal instruction fault */
		instr = *(int *)rp->r_pc;
		if ((instr & 0xc0000000) == 0x40000000) {
			int pc;

			rp->r_o7 = (long long)rp->r_pc;
			pc = rp->r_pc + ((instr & 0x3fffffff) << 2);
			rp->r_pc = rp->r_npc;
			rp->r_npc = pc;
			ill_calls++;
			goto cleanup;
		}
		addr = (caddr_t)rp->r_pc;
		(void) die(type, rp, addr, 0);
		/*NOTREACHED*/
#endif
	case T_UNIMP_INSTR + T_USER:	/* illegal instruction fault */
#ifndef	CALL_WORKS
		instr = fuword((int *)rp->r_pc);
		if ((instr & 0xc0000000) == 0x40000000) {
			int pc;

			rp->r_o7 = (long long)rp->r_pc;
			pc = rp->r_pc + ((instr & 0x3fffffff) << 2);
			rp->r_pc = rp->r_npc;
			rp->r_npc = pc;
			ill_calls++;
			goto out;
		}
#endif
		if (tudebug)
			showregs(type, rp, (caddr_t)0, 0);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		/*
		 * Try to simulate the instruction.
		 */
		switch (simulate_unimp(rp, &badaddr)) {
		case SIMU_RETRY:
			goto out;	/* regs are already set up */
			/*NOTREACHED*/

		case SIMU_SUCCESS:
			/* skip the successfully simulated instruction */
			rp->r_pc = rp->r_npc;
			rp->r_npc += 4;
			goto out;
			/*NOTREACHED*/

		case SIMU_FAULT:
			siginfo.si_signo = SIGSEGV;
			siginfo.si_code = SEGV_MAPERR;
			siginfo.si_addr = badaddr;
			fault = FLTBOUNDS;
			break;

		case SIMU_DZERO:
			siginfo.si_signo = SIGFPE;
			siginfo.si_code = FPE_INTDIV;
			siginfo.si_addr = (caddr_t)rp->r_pc;
			fault = FLTIZDIV;
			break;

		case SIMU_UNALIGN:
			siginfo.si_signo = SIGBUS;
			siginfo.si_code = BUS_ADRALN;
			siginfo.si_addr = badaddr;
			fault = FLTACCESS;
			break;

		case SIMU_ILLEGAL:
		default:
			siginfo.si_signo = SIGILL;
			siginfo.si_code = ILL_ILLOPC;
			siginfo.si_addr = (caddr_t)rp->r_pc;
			fault = FLTILL;
			break;
		}
		break;

	case T_IDIV0 + T_USER:		/* integer divide by zero */
	case T_DIV0 + T_USER:		/* integer divide by zero */
		if (tudebug && tudebugfpe)
			showregs(type, rp, (caddr_t)0, 0);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_signo = SIGFPE;
		siginfo.si_code = FPE_INTDIV;
		siginfo.si_addr = (caddr_t)rp->r_pc;
		fault = FLTIZDIV;
		break;

	case T_INT_OVERFLOW + T_USER:	/* integer overflow */
		if (tudebug && tudebugfpe)
			showregs(type, rp, (caddr_t)0, 0);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_signo = SIGFPE;
		siginfo.si_code  = FPE_INTOVF;
		siginfo.si_addr  = (caddr_t)rp->r_pc;
		fault = FLTIOVF;
		break;

	case T_BREAKPOINT + T_USER:	/* breakpoint trap (t 1) */
		if (tudebug && tudebugbpt)
			showregs(type, rp, (caddr_t)0, 0);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_signo = SIGTRAP;
		siginfo.si_code = TRAP_BRKPT;
		siginfo.si_addr = (caddr_t)rp->r_pc;
		fault = FLTBPT;
		break;

	case T_TAG_OVERFLOW + T_USER:	/* tag overflow (taddcctv, tsubcctv) */
		if (tudebug)
			showregs(type, rp, (caddr_t)0, 0);
		bzero((caddr_t)&siginfo, sizeof (siginfo));
		siginfo.si_signo = SIGEMT;
		siginfo.si_code = EMT_TAGOVF;
		siginfo.si_addr = (caddr_t)rp->r_pc;
		fault = FLTACCESS;
		break;

	case T_WIN_OVERFLOW + T_USER:	/* finish user window overflow */
		/*
		 * This trap is entered from sys_rtt in locore.s when, upon
		 * return to user is is found that there are user windows in
		 * lpw_pcb.pcb_wbuf.  This happens because they could not be
		 * saved on the user stack, either because it wasn't resident
		 * or because it was misaligned. We flush the user's windows
		 * to make sure pcb_wbcnt does not change while we are doing
		 * this.
		 */
	    {
		register int j;

		bzero((caddr_t)&siginfo, sizeof (siginfo));
		flush_user_windows();
		for (j = 0; j < mpcb->mpcb_wbcnt; j++) {
			register char *sp;
			register int error;

			sp = mpcb->mpcb_spbuf[j];
			if ((int)sp & (STACK_ALIGN - 1)) {
				siginfo.si_signo = SIGILL;
				siginfo.si_code = ILL_BADSTK;
				siginfo.si_addr = (caddr_t)rp->r_pc;
				fault = FLTILL;
				break;
			}
			/*
			 * If the xcopyout fails then zap the process with
			 * a SIGSEGV - process may be managing its own stack
			 * growth by taking SIGSEGVs on a different signal
			 * stack.
			 */
			error = xcopyout((caddr_t)&mpcb->mpcb_wbuf[j],
			    sp, sizeof (struct rwindow));
			/*
			 * EINTR comes from a signal during pagefault.
			 * We should not post another signal.
			 */
			if (error == EINTR)
				break;
			if (error != 0) {
				siginfo.si_signo = SIGSEGV;
				siginfo.si_code = SEGV_MAPERR;
				siginfo.si_addr = (caddr_t)sp;
				fault = FLTBOUNDS;
				break;
			}
		}
		if (j == mpcb->mpcb_wbcnt) {
			mpcb->mpcb_wbcnt = 0;
			goto out;
		} else if (j) {
			register int i;

			/*
			 * Normalize lwp->lwp_pcb.pcb_wbuf for possible
			 * return to user signal handler.
			 */
			mpcb->mpcb_wbcnt -= j;
			i = 0;
			while (i < mpcb->mpcb_wbcnt) {
				bcopy((caddr_t)&mpcb->mpcb_wbuf[j++],
				    (caddr_t)&mpcb->mpcb_wbuf[i++],
				    sizeof (struct rwindow));
			}
		}
		if (tudebug)
			showregs(type, rp, (caddr_t)0, 0);
	    }
		break;

	case T_AST + T_USER:		/* profiling or resched psuedo trap */
		if (lwp->lwp_oweupc && lwp->lwp_prof.pr_scale) {
			mutex_enter(&p->p_pflock);
			addupc((void (*)())rp->r_pc, &lwp->lwp_prof, 1);
			mutex_exit(&p->p_pflock);
			lwp->lwp_oweupc = 0;
		}
		break;
	}

	/*
	 * We can't get here from a system trap
	 */
	ASSERT(type & T_USER);

	trap_cleanup(rp, fault, &siginfo, oldpc == rp->r_pc, syst);
out:
	trap_rtt();
	if (curthread->t_proc_flag & TP_MSACCT)
		(void) new_mstate(curthread, mstate);
	/* Kernel probe */
	TNF_PROBE_1(thread_state, "thread", /* CSTYLED */,
		tnf_microstate, state, LMS_USER);

	TRACE_0(TR_FAC_TRAP, TR_C_TRAP_HANDLER_EXIT, "C_trap_handler_exit");
	return;

cleanup:	/* system traps end up here */
	ASSERT(!(type & T_USER));
	/*
	 * If the unsafe_driver mutex was held by the thread on entry,
	 * we released it so we could call other drivers.  We re-enter it here.
	 */
	if (driver_mutex)
		mutex_enter(&unsafe_driver);

	TRACE_0(TR_FAC_TRAP, TR_C_TRAP_HANDLER_EXIT, "C_trap_handler_exit");
}

void
trap_cleanup(rp, fault, sip, restartable, syst)
	register struct regs *rp;
	u_int fault;
	k_siginfo_t *sip;
	int restartable;
	clock_t syst;
{
	extern void aio_cleanup();
	proc_t *p = ttoproc(curthread);
	klwp_id_t lwp = ttolwp(curthread);

	if (fault) {
		/*
		 * Remember the fault and fault address
		 * for real-time (SIGPROF) profiling.
		 */
		lwp->lwp_lastfault = fault;
		lwp->lwp_lastfaddr = sip->si_addr;

		/*
		 * If a debugger has declared this fault to be an
		 * event of interest, stop the lwp.  Otherwise just
		 * deliver the associated signal.
		 */
		if (sip->si_signo != SIGKILL &&
		    prismember(&p->p_fltmask, fault) &&
		    stop_on_fault(fault, sip) == 0)
			sip->si_signo = 0;
	}

	if (sip->si_signo)
		trapsig(sip, restartable);

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
}

/*
 * Called from fp_traps when a floating point trap occurs.
 * Note that the T_DATA_EXCEPTION case does not use X_FAULT_TYPE(mmu_fsr),
 * because mmu_fsr (now changed to code) is always 0.
 * Note that the T_UNIMP_INSTR case does not call simulate_unimp(),
 * because the simulator only simulates multiply and divide instructions,
 * which would not cause floating point traps in the first place.
 * XXX - Supervisor mode floating point traps?
 */
int   fpu_traps = 0;

/*VARARGS2*/
void
fpu_trap(rp, type, addr, code, rw)
	register struct regs *rp;
	register unsigned type;
	caddr_t addr;
	register u_int code;
	register enum seg_rw rw;
{
	proc_t *p = ttoproc(curthread);
	klwp_id_t lwp = ttolwp(curthread);
	clock_t syst;
	k_siginfo_t siginfo;
	u_int fault = 0;
	greg_t oldpc;
	int mstate;
	char *badaddr;

	CPU_STAT_ADDQ(CPU, cpu_sysinfo.trap, 1);
	syst = p->p_stime;

	ASSERT(curthread->t_schedflag & TS_DONT_SWAP);

	fpu_traps++;
	if (USERMODE(rp->r_tstate)) {
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
		ASSERT(lwp->lwp_regs == rp);
		lwp->lwp_state = LWP_SYS;
		if (curthread->t_proc_flag & TP_MSACCT)
			mstate = new_mstate(curthread, LMS_TRAP);
		else
			mstate = LMS_USER;
		siginfo.si_signo = 0;
		oldpc = rp->r_pc;
		type |= T_USER;
	} else {
		if (MUTEX_OWNER_LOCK(&unsafe_driver) &&
		    UNSAFE_DRIVER_LOCK_HELD()) {
			mutex_exit(&unsafe_driver);
		}
	}

	TRACE_1(TR_FAC_TRAP, TR_C_TRAP_HANDLER_ENTER,
		"C_fpu_trap_handler_enter:type %x", type);

	if (tudebug && tudebugfpe)
		showregs(type, rp, addr, 0);

	bzero((caddr_t)&siginfo, sizeof (siginfo));
	oldpc = (greg_t)addr;
	siginfo.si_code = code;
	siginfo.si_addr = addr;

	switch (type) {

	case T_FP_EXCEPTION_IEEE + T_USER:	/* FPU arithmetic exception */
		siginfo.si_signo = SIGFPE;
		fault = FLTFPE;
		break;

	case T_DATA_EXCEPTION + T_USER:		/* user data access exception */
		siginfo.si_signo = SIGSEGV;
		fault = FLTBOUNDS;
		break;

	case T_ALIGNMENT + T_USER:		/* user alignment error */
		/*
		 * If the user has to do unaligned references
		 * the ugly stuff gets done here.
		 * Only handles vanilla loads and stores.
		 */
		alignfaults++;
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
			} else {
				if (calc_memaddr(rp, &badaddr) == SIMU_UNALIGN)
					siginfo.si_addr = badaddr;
				else
					siginfo.si_addr = (caddr_t)rp->r_pc;
			}
			fault = FLTACCESS;
		}
		break;

	case T_UNIMP_INSTR + T_USER:		/* illegal instruction fault */
		siginfo.si_signo = SIGILL;
		siginfo.si_code = ILL_ILLTRP;
		fault = FLTILL;
		break;

	default:
		(void) die(type, rp, addr, 0);
		/*NOTREACHED*/
	}

	/*
	 * We can't get here from a system trap
	 * Never restart any instruction which got here from an fp trap.
	 */
	ASSERT(type & T_USER);

#ifdef	lint
	oldpc = oldpc;
#endif
	trap_cleanup(rp, fault, &siginfo, 0, syst);
out:
	trap_rtt();
	if (curthread->t_proc_flag & TP_MSACCT)
		(void) new_mstate(curthread, mstate);
}

void
trap_rtt(void)
{
	klwp_id_t lwp = ttolwp(curthread);

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

	TRACE_0(TR_FAC_TRAP, TR_C_TRAP_HANDLER_EXIT, "C_trap_handler_exit");
}

#define	IS_LDASI(o)	\
	((o) == (uint)0xC0C00000 || (o) == (uint)0xC0800000 ||	\
	(o) == (uint)0xC1800000)
#define	IS_IMM_ASI(i)	(((i) & 0x2000) == 0)
#define	IS_ASINF(a)	(((a) & 0xF6) == 0x82)
#define	IS_FLOAT(i)	(((i) & 0x1000000) != 0)
#define	IS_LDDA(i)	(((i) & 0xC1F80000) == 0xC0980000)

static
int
nfload(struct regs *rp)
{
	u_int instr, asi, op3, rd, len;
	struct as *as;
	caddr_t addr;
	u_longlong_t zero;
	extern int segnf_create();
	extern int putreg();

	if (USERMODE(rp->r_tstate))
		instr = fuword((int *)rp->r_pc);
	else
		instr = *(int *)rp->r_pc;

	op3 = instr & 0xC1E00000;
	if (!IS_LDASI(op3))
		return (0);
	if (IS_IMM_ASI(instr))
		asi = (instr & 0x1FE0) >> 5;
	else
		asi = (rp->r_tstate >> TSTATE_ASI_SHIFT) &
			TSTATE_ASI_MASK;
	if (!IS_ASINF(asi))
		return (0);
	if (calc_memaddr(rp, &addr) == SIMU_SUCCESS) {
		len = 1;
		as = USERMODE(rp->r_tstate) ? ttoproc(curthread)->p_as : &kas;
		as_rangelock(as);
		if (as_gap(as, len, &addr, &len, 0, addr) == 0)
			as_map(as, addr, len, segnf_create, NULL);
		as_rangeunlock(as);
	}
	zero = 0;
	rd = (instr >> 25) & 0x1f;
	if (IS_FLOAT(instr)) {
		u_int dbflg = ((instr >> 19) & 3) == 3;

		if (dbflg) {		/* clever v9 reg encoding */
			if (rd & 1)
				rd = (rd & 0x1e) | 0x20;
			rd >>= 1;
		}
		if (fpu_exists) {
			if (dbflg)
				_fp_write_pdreg(&zero, rd);
			else
				_fp_write_pfreg((u_int *)&zero, rd);
		} else {
			kfpu_t *fp = lwptofpu(curthread->t_lwp);

			if (dbflg)
				fp->fpu_fr.fpu_dregs[rd] = zero;
			else
				fp->fpu_fr.fpu_regs[rd] = 0;
		}
	} else {
		putreg(&zero, &rp->r_tstate, (u_int *)rp->r_sp, rd, &addr);
		if (IS_LDDA(instr))
			putreg(&zero, &rp->r_tstate, (u_int *)rp->r_sp,
				rd + 1, &addr);
	}
	rp->r_pc = rp->r_npc;
	rp->r_npc += 4;
	return (1);
}

kmutex_t atomic_nc_mutex;

/*
 * The following couple of routines are for userland drivers which
 * do atomics to noncached addresses.  This sort of worked on previous
 * platforms -- the operation really wasn't atomic, but it didn't generate
 * a trap as sun4u systems do.
 */

static int
swap_nc(struct regs *rp, int instr)
{
	u_longlong_t rdata, mdata;
	caddr_t addr, *badaddr;
	int tmp, rd;

	flush_user_windows_to_stack();
	rd = (instr >> 25) & 0x1f;
	if (calc_memaddr(rp, &addr) != SIMU_SUCCESS)
		return (0);
	if (getreg(&rp->r_tstate, (u_int *)rp->r_sp, rd, &rdata, &badaddr))
		return (0);
	mutex_enter(&atomic_nc_mutex);
	tmp = fuword((int *)addr);
	if (tmp == -1) {
		tmp = fubyte(addr);
		if (tmp == -1) {
			mutex_exit(&atomic_nc_mutex);
			return (0);
		}
		tmp = -1;
	}
	mdata = (u_longlong_t)tmp;
	if (suword((int *)addr, (int)rdata) == -1) {
		mutex_exit(&atomic_nc_mutex);
		return (0);
	}
	putreg(&mdata, &rp->r_tstate, (u_int *)rp->r_sp, rd, &badaddr);
	mutex_exit(&atomic_nc_mutex);
	return (1);
}

static int
ldstub_nc(struct regs *rp, int instr)
{
	u_longlong_t mdata;
	caddr_t addr, *badaddr;
	int tmp, rd;

	flush_user_windows_to_stack();
	rd = (instr >> 25) & 0x1f;
	if (calc_memaddr(rp, &addr) != SIMU_SUCCESS)
		return (0);
	mutex_enter(&atomic_nc_mutex);
	tmp = fubyte(addr);
	if (tmp == -1) {
		mutex_exit(&atomic_nc_mutex);
		return (0);
	}
	mdata = (u_longlong_t)tmp;
	if (subyte(addr, 0xff) == -1) {
		mutex_exit(&atomic_nc_mutex);
		return (0);
	}
	putreg(&mdata, &rp->r_tstate, (u_int *)rp->r_sp, rd, &badaddr);
	mutex_exit(&atomic_nc_mutex);
	return (1);
}

/*
 * Patch non-zero to disable preemption of threads in the kernel.
 */
int IGNORE_KERNEL_PREEMPTION = 0;	/* XXX - delete this someday */

struct kpreempt_cnts {	/* kernel preemption statistics */
	int	kpc_idle;	/* executing idle thread */
	int	kpc_intr;	/* executing interrupt thread */
	int	kpc_clock;	/* executing clock thread */
	int	kpc_blocked;	/* thread has blocked preemption (t_preempt) */
	int	kpc_notonproc;	/* thread is surrendering processor */
	int	kpc_inswtch;	/* thread has ratified scheduling decision */
	int	kpc_prilevel;	/* processor interrupt level is too high */
	int	kpc_apreempt;	/* asynchronous preemption */
	int	kpc_spreempt;	/* synchronous preemption */
}	kpreempt_cnts;

/*
 * kernel preemption: forced rescheduling
 *	preempt the running kernel thread.
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
		if (curthread->t_state != TS_ONPROC ||
		    curthread->t_disp_queue != &CPU->cpu_disp) {
			/* this thread will be calling swtch() shortly */
			kpreempt_cnts.kpc_notonproc++;
			if (CPU->cpu_thread != CPU->cpu_dispthread) {
				/* already in swtch(), force another */
				kpreempt_cnts.kpc_inswtch++;
				siron();
			}
			return;
		}

		if (((asyncspl != KPREEMPT_SYNC) ? spltoipl(asyncspl) :
		    getpil()) >= LOCK_LEVEL) {
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
		 * block preemption so we don't have multiple preemptions
		 * pending on the interrupt stack
		 */
		curthread->t_preempt++;
		if (asyncspl != KPREEMPT_SYNC) {
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

	if ((int)sp & (STACK_ALIGN - 1)) {
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
	DELAY(2000000);
}

/*
 * General system stack backtrace
 */
void
tracedump()
{
	label_t l;

	(void) setjmp(&l);
	traceback((caddr_t)l.val[1]);
}

#ifdef TRAPWINDOW
long trap_window[25];
#endif TRAPWINDOW

/*
 * Print out debugging info.
 */
/*ARGSUSED*/
void
showregs(type, rp, addr, mmu_fsr)
	register unsigned type;
	register struct regs *rp;
	caddr_t addr;
	u_int mmu_fsr;
{
	int s;

	s = spl7();
	type &= ~T_USER;
	printf("%s: ", u.u_comm);

	switch (type) {
	case T_ALIGNMENT:
		printf("alignment error:\n");
		break;
	case T_INSTR_EXCEPTION:
		printf("text access exception:\n");
		break;
	case T_DATA_EXCEPTION:
		printf("data access exception:\n");
		break;
	case T_PRIV_INSTR:
		printf("privileged instruction faultn");
		break;
	case T_UNIMP_INSTR:
		printf("illegal instruction fault");
		break;
	case T_IDIV0:
		printf("integer divide zero trap:\n");
		break;
	case T_DIV0:
		printf("zero divide trap:\n");
		break;
	case T_INT_OVERFLOW:
		printf("integer overflow:\n");
		break;
	case T_BREAKPOINT:
		printf("breakpoint trap:\n");
		break;
	case T_TAG_OVERFLOW:
		printf("tag overflow:\n");
		break;
	default:
		if (type >= T_SOFTWARE_TRAP && type <= T_ESOFTWARE_TRAP)
			printf("software trap 0x%x\n", type - T_SOFTWARE_TRAP);
		else
			printf("trap type = %d\n", type);
		break;
	}
	if (type == T_DATA_EXCEPTION || type == T_INSTR_EXCEPTION) {
		mmu_print_sfsr(mmu_fsr);
	} else if (addr) {
		printf("addr=0x%x\n", addr);
	}

#ifndef	lint
	/*
	 * LINT: maformed format string (%llx ?).
	 */
	printf("pid=%d, pc=0x%x, sp=0x%x, tstate=0x%llx, context=%d\n",
	    (ttoproc(curthread) && ttoproc(curthread)->p_pidp) ?
	    (ttoproc(curthread)->p_pid) : 0, rp->r_pc, rp->r_sp,
	    rp->r_tstate, sfmmu_getctx_sec());
	if (USERMODE(rp->r_tstate)) {
		printf("o0-o7: %llx, %llx, %llx, %llx, %llx, %llx, "
		    "%llx, %llx\n", rp->r_o0, rp->r_o1, rp->r_o2, rp->r_o3,
		    rp->r_o4, rp->r_o5, rp->r_o6, rp->r_o7);
	}
	printf("g1-g7: %llx, %llx, %llx, %llx, %llx, %llx, %llx\n",
	    rp->r_g1, rp->r_g2, rp->r_g3,
	    rp->r_g4, rp->r_g5, rp->r_g6, rp->r_g7);
#endif	lint

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
#endif TRAPWINDOW
	if (tudebug > 1 && (boothowto & RB_DEBUG)) {
		debug_enter((char *)NULL);
	}
	(void) splx(s);
}

#define	INSTR_IS_WRITE(instr)	(((instr) >> 21) & 1)

static
enum seg_rw
get_accesstype(struct regs *rp)
{
	int instr;

	if (USERMODE(rp->r_tstate)) {
		instr = fuword((int *)rp->r_pc);
	} else {
		instr = *(int *)rp->r_pc;
	}
	if (INSTR_IS_WRITE(instr))
		return (S_WRITE);
	else
		return (S_READ);
}
