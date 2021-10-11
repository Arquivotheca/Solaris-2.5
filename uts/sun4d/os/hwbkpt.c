/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)hwbkpt.c	1.16	95/02/03 SMI"

/*
 * XXX
 *
 * Currently nothing in this file is used and it is likely to be
 * deleted in the future.  The code below was written according
 * to SuperSPARC documentation, however the chip appears to behave
 * differently making most of this code unusable.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/privregs.h>
#include <sys/hwbkpt.h>
#include <sys/machparam.h>
#include <sys/cpuvar.h>
#include <sys/varargs.h>
#include <vm/hat_srmmu.h>

u_int ikbp_set = 0;		/* bitmap of CPUs that have breakpoints set */

typedef struct {
	int	reload;		/* reload bkpt has been set */
	int	flags;		/* flags passed to ikbp_set_bkpt() */
	int	bkpt_ctx;	/* context to match */
	int	save_ctx;	/* place to save context during reload */
	u_ll	save_bkv;	/* save bkv during reload */
	u_ll	save_bkm;	/* save bkm */
	u_ll	save_bkc;	/* save bkc */
	bkpt_func_t bkpt_func;	/* function to execute at breakpoint */
} bkpt_info_t;

static bkpt_info_t bkpt_info[NCPU];
kmutex_t ikbp_mutex;

static int access_size_type(greg_t pc, int *type);

void
ikbp_init()
{
	mutex_init(&ikbp_mutex, "intra-kernel breakpoint mutex",
	    MUTEX_DEFAULT, NULL);
}

/*VARARGS2*/
int
ikbp_set_bkpt(int flags, bkpt_func_t func, ...)
{
	va_list ap;
	int ret = 1;
	caddr_t vaddr;
	int ctx;
	u_ll paddr, pmask;
	u_int vmask;
	int cpuid;
	bkpt_info_t *bip;

	va_start(ap, func);

	cpuid = CPU->cpu_id;
	bip = &bkpt_info[cpuid];

	bip->flags = flags;
	bip->bkpt_func = func;
	if (flags & IKBP_CODE)
		bip->save_bkc = BKC_CSPACE | BKC_CBKEN | BKC_CBFEN;
	else {
		bip->save_bkc = BKC_DBFEN;
		if (flags & IKBP_READ)
			bip->save_bkc |= BKC_DBREN;
		if (flags & IKBP_WRITE)
			bip->save_bkc |= BKC_DBWEN;
	}
	if (flags & IKBP_VIRT) {
		vaddr = va_arg(ap, caddr_t);
		vmask = va_arg(ap, u_int);
		if (flags & IKBP_XLATE) {
			ret = srmmu_xlate(-1, vaddr, &paddr, NULL, NULL);
			bip->save_bkc |= BKC_PAMD;
			bip->save_bkv = paddr & MASK_36;
			bip->save_bkm = vmask;
		} else {
			bip->save_bkv = (u_ll) ((u_int) vaddr);
			bip->save_bkm = vmask;
			ctx = va_arg(ap, int);
			bip->bkpt_ctx = ctx;
		}
	} else {
		bip->save_bkc |= BKC_PAMD;
		paddr = va_arg(ap, u_ll);
		pmask = va_arg(ap, u_ll);
		bip->save_bkv = paddr & MASK_36;
		bip->save_bkm = pmask & MASK_36;
	}

	va_end(ap);

	if (ret != 1)
		return (0);

	set_bkv(bip->save_bkv);
	set_bkm(bip->save_bkm);
	set_bkc(bip->save_bkc);

	mutex_enter(&ikbp_mutex);
	ikbp_set |= (1 << cpuid);
	mutex_exit(&ikbp_mutex);

	return (1);
}

void
ikbp_clr_bkpt(void)
{
	int cpuid;
	bkpt_info_t *bip;

	cpuid = CPU->cpu_id;
	bip = &bkpt_info[cpuid];

	set_bkv((u_ll) 0);
	set_bkm((u_ll) 0);
	set_bkc((u_ll) 0);
	bip->bkpt_func = NULL;
	bip->reload = 0;

	mutex_enter(&ikbp_mutex);
	ikbp_set &= ~(1 << cpuid);
	mutex_exit(&ikbp_mutex);
}

int
check_hwbkpt(struct regs *rp, caddr_t addr)
{
	u_ll bks, bkc;
	int cpuid;
	bkpt_info_t *bip;
	int size, type;
	u_ll paddr, pmatch;

	bks = get_bks();
	if ((bks & BKS_MASK) == 0)
		return (0);

	cpuid = CPU->cpu_id;
	bip = &bkpt_info[cpuid];

	/*
	 * if this is a "reload breakpoint" just reload the
	 * breakpoint registers and continue
	 */
	if (bip->reload) {
		set_bkv(bip->save_bkv);
		set_bkm(bip->save_bkm);
		set_bkc(bip->save_bkc);
		bip->bkpt_ctx = bip->save_ctx;
		bip->reload = 0;
		return (1);
	}

	/*
	 * If it's a physical address breakpoint, or virtual and either
	 * it matches on all contexts, or we match the specified
	 * context, call the breakpoint function.
	 */

	if ((bip->flags & IKBP_VIRT) == 0 || bip->bkpt_ctx == ALL_CTX ||
	    mmu_getctx() == bip->bkpt_ctx) {

		/*
		 * if it's a data breakpoint figure out the size and
		 * type of the access
		 */
		if (bks & BKS_DBKFS)
			size = access_size_type(rp->r_pc, &type);
		else {
			addr = (caddr_t)rp->r_pc;
			size = 4;
			type = IKBP_READ;	/* not really */
		}

		/*
		 * if this a physical address bkpt or we were asked to
		 * do the translation, provide the physical address.
		 */
		paddr = 0;
		pmatch = 0;
		if ((bip->flags & IKBP_VIRT) == 0 ||
		    (bip->flags & IKBP_XLATE)) {
			(void) srmmu_xlate(-1, addr, &paddr, NULL, NULL);
			if (bip->flags & IKBP_XLATE)
				pmatch = bip->save_bkv;
		}

		/*
		 * clear the control register while
		 * we execute the breakpoint function
		 */
		bkc = 0;
		set_bkc(bkc);

		(*bip->bkpt_func)(rp, addr, paddr, pmatch, size, type);
	}

	/*
	 * set at code breakpoint at the next pc so we can
	 * execute the instruction at the original breakpoint
	 * and so we get a chance to reload the breakpoint
	 * registers
	 */
	bip->save_ctx = bip->bkpt_ctx;
	bip->bkpt_ctx = ALL_CTX;
	set_bkv((u_ll) rp->r_npc);
	set_bkm((u_ll) 0);
	set_bkc((u_ll) (BKC_CSPACE | BKC_CBFEN | BKC_CBKEN));
	bip->reload = 1;

	return (1);
}

#define	getbits(word, upbit, lowbit) \
	(((unsigned)(word) << (32 - (upbit) - 1)) >> \
	    ((32 - (upbit) - 1) + (lowbit)))

/*
 * Decode the instruction that caused the fault to determine the
 * size of the access.  If something strange happened and the pc
 * points to a non-memory instruction, return 0.  The two LSB of
 * op3 indicate the size of the access, with some exceptions.  Also
 * determine the type (read or write) by checking if this is a ld,
 * st, ldstub, or swap.
 */

static int
access_size_type(greg_t pc, int *type)
{
	u_int ins;
	u_int op3;

	ins = *((u_int *) pc);

	if (getbits(ins, 31, 30) != 3) {
		*type = 0;
		return (0);
	}

	op3 = getbits(ins, 24, 19);

	/*
	 * figure out the type
	 */
	if (op3 & (1 << 2)) {
		if (op3 & (1 << 3))
			/* ldstub, ldstuba, swap, swapa */
			*type = IKBP_READ | IKBP_WRITE;
		else
			*type = IKBP_WRITE;
	} else
		*type = IKBP_READ;

	switch (op3 & 3) {
	case 0:
		return (4);
	case 1:
		if (op3 == 0x21 || op3 == 0x25 || op3 == 0x31 || op3 == 0x35)
			/* ldfsr, stfsr, ldcsr, stcsr */
			return (4);
		else
			return (1);
	case 2:
		if (op3 == 0x26 || op3 == 0x36)
			/* stdfq, stdcq */
			return (8);
		else
			return (2);
	case 3:
		if (op3 = 0xf || op3 == 0x1f)
			/* swap, swapa */
			return (4);
		else
			return (8);
	}

	return (0);
}

/*
 * Decide if two address ranges (given by starting address and length)
 * overlap.  This is useful in a breakpoint function to decide if
 * the address and size of the access that caused the breakpoint
 * touches the range you're interested in (it may not because you had
 * to use a certain mask to catch all access types).
 */

int
ikbp_overlap(caddr_t v1, int s1, caddr_t v2, int s2)
{
	caddr_t v1e = v1 + s1 - 1;
	caddr_t v2e = v2 + s2 - 1;

	if ((v1 >= v2 && v1 <= v2e) ||
	    (v1e >= v2 && v1e <= v2e) ||
	    (v2 >= v1 && v2 <= v1e) ||
	    (v2e >= v1 && v2e <= v1e))
		return (1);
	else
		return (0);
}

int
ikbp_overlap_phys(u_ll p1, int s1, u_ll p2, int s2)
{
	u_ll p1e = p1 + s1 - 1;
	u_ll p2e = p2 + s2 - 1;

	if ((p1 >= p2 && p1 <= p2e) ||
	    (p1e >= p2 && p1e <= p2e) ||
	    (p2 >= p1 && p2 <= p1e) ||
	    (p2e >= p1 && p2e <= p1e))
		return (1);
	else
		return (0);
}
