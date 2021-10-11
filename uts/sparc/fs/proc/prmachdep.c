/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)prmachdep.c	1.45	95/07/25 SMI"	/* SVr4.0 1.8	*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/inline.h>
#include <sys/kmem.h>
#include <sys/proc.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/pcb.h>
#include <sys/buf.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/cpuvar.h>

#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>
#include <sys/archsystm.h>
#include <sys/cmn_err.h>
#include <sys/stack.h>
#include <sys/machpcb.h>
#include <sys/fpu/fpusystm.h>

#include <sys/pte.h>
#include <sys/map.h>
#include <sys/vmmac.h>
#include <sys/mman.h>
#include <sys/vmparam.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/seg_kp.h>
#include <vm/page.h>

#include <fs/proc/prdata.h>

/*
 * Force a thread into the kernel if it is not already there.
 * This is a no-op on uniprocessors.
 */
/* ARGSUSED */
void
prpokethread(kthread_t *t)
{
#ifdef MP
	if (t->t_state == TS_ONPROC && t->t_cpu != CPU)
		poke_cpu(t->t_cpu->cpu_id);
#endif /* MP */
}

/*
 * Map a target process's u-block in and out.  prumap() makes it addressable
 * (if necessary) and returns a pointer to it.
 */
struct user *
prumap(p)
	register proc_t *p;
{
	return (PTOU(p));
}

/* ARGSUSED */
void
prunmap(p)
	proc_t *p;
{
	/*
	 * With paged u-blocks, there's nothing to do in order to unmap.
	 */
}

/*
 * Return general registers.
 */
void
prgetprregs(lwp, prp)
	register klwp_t *lwp;
	register prgregset_t prp;
{
	gregset_t gr;

	extern void getgregs();

	getgregs(lwp, gr);
	bzero((caddr_t)prp, sizeof (prgregset_t));

	/*
	 * Can't copy since prgregset_t and gregset_t
	 * use different defines.
	 */
	prp[R_G1] = gr[REG_G1];
	prp[R_G2] = gr[REG_G2];
	prp[R_G3] = gr[REG_G3];
	prp[R_G4] = gr[REG_G4];
	prp[R_G5] = gr[REG_G5];
	prp[R_G6] = gr[REG_G6];
	prp[R_G7] = gr[REG_G7];

	prp[R_O0] = gr[REG_O0];
	prp[R_O1] = gr[REG_O1];
	prp[R_O2] = gr[REG_O2];
	prp[R_O3] = gr[REG_O3];
	prp[R_O4] = gr[REG_O4];
	prp[R_O5] = gr[REG_O5];
	prp[R_O6] = gr[REG_O6];
	prp[R_O7] = gr[REG_O7];

	if (lwp->lwp_pcb.pcb_xregstat != XREGNONE) {
		prp[R_L0] = lwp->lwp_pcb.pcb_xregs.rw_local[0];
		prp[R_L1] = lwp->lwp_pcb.pcb_xregs.rw_local[1];
		prp[R_L2] = lwp->lwp_pcb.pcb_xregs.rw_local[2];
		prp[R_L3] = lwp->lwp_pcb.pcb_xregs.rw_local[3];
		prp[R_L4] = lwp->lwp_pcb.pcb_xregs.rw_local[4];
		prp[R_L5] = lwp->lwp_pcb.pcb_xregs.rw_local[5];
		prp[R_L6] = lwp->lwp_pcb.pcb_xregs.rw_local[6];
		prp[R_L7] = lwp->lwp_pcb.pcb_xregs.rw_local[7];

		prp[R_I0] = lwp->lwp_pcb.pcb_xregs.rw_in[0];
		prp[R_I1] = lwp->lwp_pcb.pcb_xregs.rw_in[1];
		prp[R_I2] = lwp->lwp_pcb.pcb_xregs.rw_in[2];
		prp[R_I3] = lwp->lwp_pcb.pcb_xregs.rw_in[3];
		prp[R_I4] = lwp->lwp_pcb.pcb_xregs.rw_in[4];
		prp[R_I5] = lwp->lwp_pcb.pcb_xregs.rw_in[5];
		prp[R_I6] = lwp->lwp_pcb.pcb_xregs.rw_in[6];
		prp[R_I7] = lwp->lwp_pcb.pcb_xregs.rw_in[7];
	}

	prp[R_PSR] = gr[REG_PSR];
	prp[R_PC]  = gr[REG_PC];
	prp[R_nPC] = gr[REG_nPC];
	prp[R_Y]   = gr[REG_Y];
}

/*
 * Set general registers.
 */
void
prsetprregs(lwp, prp)
	register klwp_t *lwp;
	register prgregset_t prp;
{
	gregset_t gr;

	extern void setgregs();

	gr[REG_G1] = prp[R_G1];
	gr[REG_G2] = prp[R_G2];
	gr[REG_G3] = prp[R_G3];
	gr[REG_G4] = prp[R_G4];
	gr[REG_G5] = prp[R_G5];
	gr[REG_G6] = prp[R_G6];
	gr[REG_G7] = prp[R_G7];

	gr[REG_O0] = prp[R_O0];
	gr[REG_O1] = prp[R_O1];
	gr[REG_O2] = prp[R_O2];
	gr[REG_O3] = prp[R_O3];
	gr[REG_O4] = prp[R_O4];
	gr[REG_O5] = prp[R_O5];
	gr[REG_O6] = prp[R_O6];
	gr[REG_O7] = prp[R_O7];

	lwp->lwp_pcb.pcb_xregs.rw_local[0] = prp[R_L0];
	lwp->lwp_pcb.pcb_xregs.rw_local[1] = prp[R_L1];
	lwp->lwp_pcb.pcb_xregs.rw_local[2] = prp[R_L2];
	lwp->lwp_pcb.pcb_xregs.rw_local[3] = prp[R_L3];
	lwp->lwp_pcb.pcb_xregs.rw_local[4] = prp[R_L4];
	lwp->lwp_pcb.pcb_xregs.rw_local[5] = prp[R_L5];
	lwp->lwp_pcb.pcb_xregs.rw_local[6] = prp[R_L6];
	lwp->lwp_pcb.pcb_xregs.rw_local[7] = prp[R_L7];

	lwp->lwp_pcb.pcb_xregs.rw_in[0] = prp[R_I0];
	lwp->lwp_pcb.pcb_xregs.rw_in[1] = prp[R_I1];
	lwp->lwp_pcb.pcb_xregs.rw_in[2] = prp[R_I2];
	lwp->lwp_pcb.pcb_xregs.rw_in[3] = prp[R_I3];
	lwp->lwp_pcb.pcb_xregs.rw_in[4] = prp[R_I4];
	lwp->lwp_pcb.pcb_xregs.rw_in[5] = prp[R_I5];
	lwp->lwp_pcb.pcb_xregs.rw_in[6] = prp[R_I6];
	lwp->lwp_pcb.pcb_xregs.rw_in[7] = prp[R_I7];

	lwp->lwp_pcb.pcb_xregstat = XREGMODIFIED;

	/*
	 * Silently align the stack pointer.
	 */
	gr[REG_SP] &= ~(STACK_ALIGN-1);
	/*
	 * setgregs will only allow the condition codes to be set.
	 */
	gr[REG_PSR] = prp[R_PSR];

	gr[REG_PC]  = prp[R_PC];
	gr[REG_nPC] = prp[R_nPC];
	gr[REG_Y]   = prp[R_Y];

	setgregs(lwp, gr);
}

/*
 * Return the value of the PC from the supplied register set.
 */
prgreg_t
prgetpc(prp)
	prgregset_t prp;
{
	return (prp[R_PC]);
}

/*
 * Does the system support floating-point, either through hardware
 * or by trapping and emulating floating-point machine instructions?
 */
int
prhasfp()
{
	/*
	 * SunOS5.0 emulates floating-point if FP hardware is not present.
	 */
	return (1);
}

/*
 * Get floating-point registers.
 */
void
prgetprfpregs(lwp, pfp)
	register klwp_t *lwp;
	register prfpregset_t *pfp;
{

	extern void getfpregs();

	bzero((caddr_t)pfp, sizeof (prfpregset_t));
	/*
	 * This works only because prfpregset_t is intentionally
	 * constructed to be identical to fpregset_t, with additional
	 * space for the floating-point queue at the end.
	 */
	getfpregs(lwp, (fpregset_t *)pfp);
	/*
	 * This is supposed to be a pointer to the floating point queue.
	 * We can't provide such a thing through the /proc interface.
	 */
	pfp->pr_filler = NULL;
	/*
	 * XXX: to be done: fetch the FP queue if it is non-empty.
	 */
}

/*
 * Set floating-point registers.
 */
void
prsetprfpregs(lwp, pfp)
	register klwp_t *lwp;
	register prfpregset_t *pfp;
{

	extern void setfpregs();
	/*
	 * XXX: to be done: store the FP queue if it is non-empty.
	 */
	pfp->pr_qcnt = 0;
	/*
	 * We set fpu_en before calling setfpregs() in order to
	 * retain the semantics of this operation from older
	 * versions of the system.  SunOS 5.4 and prior never
	 * queried fpu_en; they just set the registers.  The
	 * proper operation if fpu_en is zero is to disable
	 * floating point in the target process, but this can
	 * only change after a proper end-of-life period for
	 * the old semantics.
	 */
	pfp->pr_en = 1;
	/*
	 * This works only because prfpregset_t is intentionally
	 * constructed to be identical to fpregset_t, with additional
	 * space for the floating-point queue at the end.
	 */
	setfpregs(lwp, (fpregset_t *)pfp);
}

/*
 * Does the system support extra register state?
 */
int
prhasx(void)
{
	extern int xregs_exists;

	return (xregs_exists);
}

/*
 * Get the size of the extra registers.
 */
int
prgetprxregsize(void)
{
	extern int xregs_getsize(void);

	return (xregs_getsize());
}

/*
 * Get extra registers.
 */
void
prgetprxregs(lwp, prx)
	register klwp_t *lwp;
	register caddr_t prx;
{
	extern void xregs_get(struct _klwp *, caddr_t);

	(void) xregs_get(lwp, prx);
}

/*
 * Set extra registers.
 */
void
prsetprxregs(lwp, prx)
	register klwp_t *lwp;
	register caddr_t prx;
{
	extern void xregs_set(struct _klwp *, caddr_t);

	(void) xregs_set(lwp, prx);
}

/*
 * Return the base (lower limit) of the process stack.
 */
caddr_t
prgetstackbase(p)
	register proc_t *p;
{
	return ((caddr_t)USRSTACK - p->p_stksize);
}

/*
 * Return the "addr" field for pr_addr in prpsinfo_t.
 * This is a vestige of the past, so whatever we return is OK.
 */
caddr_t
prgetpsaddr(p)
	register proc_t *p;
{
	return ((caddr_t)p);
}

/*
 * Arrange to single-step the lwp.
 */
void
prstep(lwp)
	klwp_t *lwp;
{
	lwp->lwp_pcb.pcb_step = STEP_REQUESTED;
	lwp->lwp_pcb.pcb_tracepc = NULL;
}

/*
 * Undo prstep().
 */
void
prnostep(lwp)
	klwp_t *lwp;
{
	lwp->lwp_pcb.pcb_step = STEP_NONE;
	lwp->lwp_pcb.pcb_tracepc = NULL;
}

/*
 * Return non-zero if a single-step is in effect.
 */
int
prisstep(lwp)
	klwp_t *lwp;
{
	return (lwp->lwp_pcb.pcb_step != STEP_NONE);
}

/*
 * Set the PC to the specified virtual address.
 */
void
prsvaddr(lwp, vaddr)
	klwp_t *lwp;
	caddr_t vaddr;
{
	register struct regs *r = lwptoregs(lwp);

	/*
	 * pc and npc must be word aligned on sparc.
	 * We silently make it so to avoid a watchdog reset.
	 */
	r->r_pc = (int)vaddr & ~03;
	r->r_npc = r->r_pc + 4;
}

/* XXX -- belongs in some header file */
extern caddr_t ppmapin(struct page *, u_int, caddr_t);
extern int pf_is_memory(u_int pfnum);

/*
 * Map address "addr" in address space "as" into a kernel virtual address.
 * The memory is guaranteed to be resident and locked down.
 */
caddr_t
prmapin(as, addr, writing)
	struct as *as;
	caddr_t addr;
	int writing;
{
	page_t *pp;
	caddr_t kaddr;
	u_int pfnum;
	u_long x;

	/*
	 * XXX - Because of past mistakes, we have bits being returned
	 * by getpfnum that are actually the page type bits of the pte.
	 * When the object we are trying to map is a memory page with
	 * a page structure everything is ok and we can use the optimal
	 * method, ppmapin.  Otherwise, we have to do something special.
	 */
	pfnum = hat_getpfnum(as, addr);
	if (pf_is_memory(pfnum)) {
		pp = page_numtopp_nolock(pfnum);
		if (pp != NULL) {
			kaddr = ppmapin(pp, writing ?
				(PROT_READ | PROT_WRITE) : PROT_READ,
				(caddr_t)-1);
			return (kaddr + ((int)addr & PAGEOFFSET));
		}
	}

	/*
	 * Oh well, we didn't have a page struct for the object we were
	 * trying to map in; ppmapin doesn't handle devices, but allocating a
	 * slot from kernelmap allows ppmapout to free virutal space when done.
	 */
	x = rmalloc_wait(kernelmap, (long)CLSIZE);
	kaddr = kmxtob(x);

	hat_devload(kas.a_hat, &kas, kaddr, NULL, pfnum,
		writing ? (PROT_READ | PROT_WRITE) : PROT_READ, 0);

	return (kaddr + ((int)addr & PAGEOFFSET));
}

/*
 * Unmap address "addr" in address space "as"; inverse of prmapin().
 */
/* ARGSUSED */
void
prmapout(as, addr, vaddr, writing)
	struct as *as;
	caddr_t addr;
	caddr_t vaddr;
	int writing;
{
	extern void ppmapout(caddr_t);

	vaddr = (caddr_t)((long)vaddr & PAGEMASK);
	ppmapout(vaddr);
}


#define	BAMASK	0xffc00000	/* for masking out disp22 from ba, a */
#define	BAA	0x30800000	/* ba, a without disp22 */
#define	FBAA	0x31800000	/* fba, a without disp22 */
#define	CBAA	0x31c00000	/* cba, a without disp22 */

/*
 * Prepare to single-step the lwp if requested.
 * This is called by the lwp itself just before returning to user level.
 */
void
prdostep()
{
	register klwp_id_t lwp = ttolwp(curthread);
	register struct regs *r = lwptoregs(lwp);

	ASSERT(lwp != NULL);
	ASSERT(r != NULL);

	if (lwp->lwp_pcb.pcb_step == STEP_NONE ||
	    lwp->lwp_pcb.pcb_step == STEP_ACTIVE)
		return;

	if (lwp->lwp_pcb.pcb_step == STEP_WASACTIVE) {
		if (r->r_npc == (greg_t)lwp->lwp_pcb.pcb_tracepc)
			r->r_npc = USERLIMIT;
		else {
			lwp->lwp_pcb.pcb_tracepc = (caddr_t)r->r_pc;
			r->r_pc = USERLIMIT;
		}
#if 0
	} else if (as_checkprot(lwptoproc(lwp)->p_as,
	    (caddr_t)r->r_pc, NBPW, PROT_USER|PROT_READ)) {
		lwp->lwp_pcb.pcb_tracepc = (caddr_t)r->r_npc;
		r->r_npc = USERLIMIT;
#endif
	} else {
		/*
		 * Single-stepping on sparc is effected by setting nPC
		 * to an invalid address and expecting FLTBOUNDS to
		 * occur after the instruction at PC is executed.
		 * This is not the whole story, however; we must
		 * deal with branch-always instructions with the
		 * annul bit set as a special case here.
		 *
		 * fuiword() returns -1 on error and we can't distinguish
		 * this from a legitimate instruction of all 1's.
		 * However 0xffffffff is not one of the branch-always
		 * instructions we are interested in.  No problem.
		 */
		register long instr = fuiword((int *)r->r_pc);
		register long i = instr & BAMASK;

		if (i == BAA || i == FBAA || i == CBAA) {
			/*
			 * For ba, a and relatives, compute the
			 * new PC from the instruction.
			 */
			i = (instr << 10) >> 8;
			lwp->lwp_pcb.pcb_tracepc = (caddr_t)r->r_pc + i;
			r->r_pc = USERLIMIT;
			r->r_npc = USERLIMIT + 4;
		} else {
			lwp->lwp_pcb.pcb_tracepc = (caddr_t)r->r_npc;
			r->r_npc = USERLIMIT;
		}
	}

	lwp->lwp_pcb.pcb_step = STEP_ACTIVE;
}

/*
 * Wrap up single stepping of the lwp.
 * This is called by the lwp itself just after it has taken
 * the FLTBOUNDS trap.  We fix up the PC and nPC to have their
 * proper values after the step.  We return 1 to indicate that
 * this fault really is the one we are expecting, else 0.
 *
 * This is also called from syscall() and stop() to reset PC
 * and nPC to their proper values for debugger visibility.
 */
int
prundostep()
{
	register klwp_id_t lwp = ttolwp(curthread);
	register int rc = 0;

	ASSERT(lwp != NULL);

	if (lwp->lwp_pcb.pcb_step == STEP_ACTIVE) {
		register struct regs *r = lwptoregs(lwp);

		ASSERT(r != NULL);

		if (r->r_pc == USERLIMIT || r->r_pc == USERLIMIT + 4) {
			if (r->r_pc == USERLIMIT) {
				r->r_pc = (greg_t)lwp->lwp_pcb.pcb_tracepc;
				if (r->r_npc == USERLIMIT + 4)
					r->r_npc = r->r_pc + 4;
			} else {
				r->r_pc = (greg_t)lwp->lwp_pcb.pcb_tracepc + 4;
				r->r_npc = r->r_pc + 4;
			}
			rc = 1;
		} else {
			r->r_npc = (greg_t)lwp->lwp_pcb.pcb_tracepc;
		}
		lwp->lwp_pcb.pcb_step = STEP_WASACTIVE;
	}

	return (rc);
}

/*
 * Make sure the lwp is in an orderly state
 * for inspection by a debugger through /proc.
 * Called from stop().
 */
/* ARGSUSED1 */
void
prstop(register klwp_t *lwp, int why, int what)
{
	register struct regs *r = lwptoregs(lwp);
	register kfpu_t *pfp = lwptofpu(lwp);
	extern void fp_prsave(kfpu_t *);

	/*
	 * We must be called by the current thread only.
	 */
	ASSERT(ttolwp(curthread) == lwp);

	/*
	 * Make sure we don't deadlock on a recursive call to prstop().
	 * stop() tests the lwp_nostop flag.
	 */
	ASSERT(lwp->lwp_nostop == 0);
	lwp->lwp_nostop = 1;

	flush_user_windows_to_stack();
	if (lwp->lwp_pcb.pcb_step != STEP_NONE)
		(void) prundostep();

	if (lwp->lwp_pcb.pcb_xregstat == XREGNONE) {
		/*
		 * Attempt to fetch the last register window from the stack.
		 * If that fails, look for it in the pcb.
		 * If that fails, give up.
		 */
		if (copyin((caddr_t)r->r_sp, (caddr_t)&lwp->lwp_pcb.pcb_xregs,
		    sizeof (struct rwindow)) == 0)
			lwp->lwp_pcb.pcb_xregstat = XREGPRESENT;
		else {
			register struct machpcb *mpcb = lwptompcb(lwp);
			register int i;

			for (i = 0; i < mpcb->mpcb_wbcnt; i++) {
				if ((caddr_t)r->r_sp == mpcb->mpcb_spbuf[i]) {
					bcopy((caddr_t)&mpcb->mpcb_wbuf[i],
					    (caddr_t)&lwp->lwp_pcb.pcb_xregs,
					    sizeof (struct rwindow));
					lwp->lwp_pcb.pcb_xregstat = XREGPRESENT;
					break;
				}
			}
		}
	}

	/*
	 * Make sure the floating point state is saved.
	 */
	fp_prsave(pfp);

	if (copyin((caddr_t)r->r_pc, (caddr_t)&lwp->lwp_pcb.pcb_instr,
	    sizeof (lwp->lwp_pcb.pcb_instr)) == 0)
		lwp->lwp_pcb.pcb_flags |= INSTR_VALID;
	else {
		lwp->lwp_pcb.pcb_flags &= ~INSTR_VALID;
		lwp->lwp_pcb.pcb_instr = 0;
	}
	save_syscall_args();

	ASSERT(lwp->lwp_nostop == 1);
	lwp->lwp_nostop = 0;
}

/*
 * Fetch the user-level instruction on which the lwp is stopped.
 * It was saved by the lwp itself, in prstop().
 * Return non-zero if the instruction is valid.
 */
int
prfetchinstr(lwp, ip)
	register klwp_t *lwp;
	register long *ip;
{
	*ip = (long)lwp->lwp_pcb.pcb_instr;
	return (lwp->lwp_pcb.pcb_flags & INSTR_VALID);
}

void
prgetwindows(lwp, gwp)
	register klwp_t *lwp;
	register gwindows_t *gwp;
{
	extern void getgwins();
	getgwins(lwp, gwp);
}
