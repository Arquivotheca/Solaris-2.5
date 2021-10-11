/*
 * Copyright (c) 1990-1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)sparc_ddi.s	1.14	95/08/30 SMI"

/*
 * Assembler routines to make some DDI routines go faster.
 * These routines should ONLY be ISA-dependent.
 */

#if defined(lint)

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/file.h>
#define	SUNDDI_IMPL		/* prevent spltty -> i_ddi_spltty etc. */
#include <sys/sunddi.h>

#else	/* lint */

#include <sys/asm_linkage.h>

#include "assym.s"		/* for FKIOCTL etc. */

#endif	/* lint */

/*
 * Layered driver routines.
 *
 * At the time of writing, the compiler converts
 *
 * a() { return (b()); }
 *
 * into
 *	save, call b, restore
 *
 * Though this is sort of ok, if the called routine is leaf routine,
 * then we just burnt a register window.
 *
 * When the compiler understands this optimization, many
 * of these routines can go back to C again.
 */

#define	FLATCALL(routine)	\
	mov	%o7, %g1;	\
	call	routine;	\
	mov	%g1, %o7

#ifdef	lint

int
ddi_copyin(caddr_t buf, caddr_t kernbuf, size_t size, int flags)
{
	extern int kcopy(caddr_t, caddr_t, size_t);

	if (flags & FKIOCTL)
		return (kcopy(buf, kernbuf, size) ? -1 : 0);
	return (copyin(buf, kernbuf, size));
}

#else	/* lint */

	ENTRY(ddi_copyin)
	set	FKIOCTL, %o4
	andcc	%o3, %o4, %g0
	bne	.do_kcopy	! share code with ddi_copyout
	FLATCALL(copyin)
	/*NOTREACHED*/

.do_kcopy:
	save	%sp, -SA(MINFRAME), %sp
	mov	%i2, %o2
	mov	%i1, %o1
	call	kcopy
	mov	%i0, %o0
	orcc	%g0, %o0, %i0	! if kcopy returns EFAULT ..
	bne,a	1f
	mov	-1, %i0		! .. we return -1
1:	ret
	restore
	SET_SIZE(ddi_copyin)

#endif	/* lint */

#ifdef	lint

int
ddi_copyout(caddr_t buf, caddr_t kernbuf, size_t size, int flags)
{
	extern int kcopy(caddr_t, caddr_t, size_t);

	if (flags & FKIOCTL)
		return (kcopy(buf, kernbuf, size) ? -1 : 0);
	return (copyout(buf, kernbuf, size));
}

#else	/* lint */

	ENTRY(ddi_copyout)
	set	FKIOCTL, %o4
	andcc	%o3, %o4, %g0
	bne	.do_kcopy	! share code with ddi_copyin
	FLATCALL(copyout)
	/*NOTREACHED*/
	SET_SIZE(ddi_copyout)

#endif	/* lint */

/*
 * DDI spine wrapper routines - here so as to not have to
 * buy register windows when climbing the device tree (which cost!)
 */

#if	defined(lint)

/*ARGSUSED*/
int
ddi_ctlops(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t op, void *a, void *v)
{
	return (DDI_SUCCESS);
}

#else	/* lint */

	ENTRY(ddi_ctlops)
	tst	%o0		! dip != 0?
	be	2f		! nope
	tst	%o1		! rdip != 0?
	be	2f		! nope
	ld	[%o0 + DEVI_BUS_CTL], %o0
				! dip = (dev_info_t *) DEVI(dip)->devi_bus_ctl;
	tst	%o0
	be	2f
	nop			! Delay slot
	ld	[%o0 + DEVI_DEV_OPS ], %g1	! dip->dev_ops
	ld	[%g1 + DEVI_BUS_OPS ], %g1	! dip->dev_ops->devo_bus_ops
	ld	[%g1 + OPS_CTL ], %g1	! dip->dev_ops->devo_bus_ops->bus_ctl
	jmpl	%g1, %g0	! bop off to new routine
	nop			! as if we had never been here
2:	retl
	sub	%g0, 1, %o0	! return (DDI_FAILURE);
	SET_SIZE(ddi_ctlops)

#endif	/* lint */

#if	defined(lint)

/* ARGSUSED */
int
ddi_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareqp, ddi_dma_handle_t *handlep)
{
	return (DDI_SUCCESS);
}

#else	/* lint */

	ENTRY(ddi_dma_map)
	ld	[%o0 + DEVI_BUS_DMA_MAP], %o0
			! dip = (dev_info_t *) DEVI(dip)->devi_bus_dma_map;
	ld	[%o0 + DEVI_DEV_OPS ], %g1	! dip->dev_ops
	ld	[%g1 + DEVI_BUS_OPS ], %g1	! dip->dev_ops->devo_bus_ops
	ld	[%g1 + OPS_MAP ], %g1 ! dip->dev_ops->devo_bus_ops->bus_dma_map
	jmpl	%g1, %g0	! bop off to new routine
	nop			! as if we had never been here
	SET_SIZE(ddi_dma_map)

#endif	/* lint */

#if	defined(lint)

/* ARGSUSED */
int
ddi_dma_alloc_handle(dev_info_t *dip, ddi_dma_attr_t *attr,
	int (*waitfp)(caddr_t), caddr_t arg,
	ddi_dma_handle_t *handlep)
{
	return (DDI_SUCCESS);
}

#else	/* lint */

	ENTRY(ddi_dma_allochdl)
	ld	[%o0 + DEVI_BUS_DMA_ALLOCHDL], %o0
			! dip = (dev_info_t *) DEVI(dip)->devi_bus_dma_allochdl;
	ld	[%o0 + DEVI_DEV_OPS ], %g1	! dip->dev_ops
	ld	[%g1 + DEVI_BUS_OPS ], %g1	! dip->dev_ops->devo_bus_ops
	ld	[%g1 + OPS_ALLOCHDL ], %g1
			! dip->dev_ops->devo_bus_ops->bus_dma_allochdl
	jmpl	%g1, %g0	! bop off to new routine
	nop			! as if we had never been here
	SET_SIZE(ddi_dma_allochdl)

#endif	/* lint */

#if	defined(lint)

/* ARGSUSED */
int
ddi_dma_freehdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handlep)
{
	return (DDI_SUCCESS);
}

#else	/* lint */

	ENTRY(ddi_dma_freehdl)
	ld	[%o0 + DEVI_BUS_DMA_FREEHDL], %o0
			! dip = (dev_info_t *) DEVI(dip)->devi_bus_dma_freehdl;
	ld	[%o0 + DEVI_DEV_OPS ], %g1	! dip->dev_ops
	ld	[%g1 + DEVI_BUS_OPS ], %g1	! dip->dev_ops->devo_bus_ops
	ld	[%g1 + OPS_FREEHDL ], %g1
			! dip->dev_ops->devo_bus_ops->bus_dma_freehdl
	jmpl	%g1, %g0	! bop off to new routine
	nop			! as if we had never been here
	SET_SIZE(ddi_dma_freehdl)

#endif	/* lint */

#if	defined(lint)

/* ARGSUSED */
int
ddi_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
        ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
	ddi_dma_cookie_t *cp, u_int *ccountp)
{
	return (DDI_SUCCESS);
}

#else	/* lint */

	ENTRY(ddi_dma_bindhdl)
	ld	[%o0 + DEVI_BUS_DMA_BINDHDL], %o0
			! dip = (dev_info_t *) DEVI(dip)->devi_bus_dma_bindhdl;
	ld	[%o0 + DEVI_DEV_OPS ], %g1	! dip->dev_ops
	ld	[%g1 + DEVI_BUS_OPS ], %g1	! dip->dev_ops->devo_bus_ops
	ld	[%g1 + OPS_BINDHDL ], %g1
			! dip->dev_ops->devo_bus_ops->bus_dma_bindhdl
	jmpl	%g1, %g0	! bop off to new routine
	nop			! as if we had never been here
	SET_SIZE(ddi_dma_bindhdl)

#endif	/* lint */

#if	defined(lint)

/* ARGSUSED */
int
ddi_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle)
{
	return (DDI_SUCCESS);
}

#else	/* lint */

	ENTRY(ddi_dma_unbindhdl)
	ld	[%o0 + DEVI_BUS_DMA_UNBINDHDL], %o0
			! dip = (dev_info_t *) DEVI(dip)->devi_bus_dma_unbindhdl;
	ld	[%o0 + DEVI_DEV_OPS ], %g1	! dip->dev_ops
	ld	[%g1 + DEVI_BUS_OPS ], %g1	! dip->dev_ops->devo_bus_ops
	ld	[%g1 + OPS_UNBINDHDL ], %g1
			! dip->dev_ops->devo_bus_ops->bus_dma_unbindhdl
	jmpl	%g1, %g0	! bop off to new routine
	nop			! as if we had never been here
	SET_SIZE(ddi_dma_unbindhdl)

#endif	/* lint */

#if	defined(lint)

/* ARGSUSED */
int
ddi_dma_flush(dev_info_t *dip, dev_info_t *rdip,
        ddi_dma_handle_t handle, off_t off, u_int len,
        u_int cache_flags)
{
	return (DDI_SUCCESS);
}

#else	/* lint */

	ENTRY(ddi_dma_flush)
	ld	[%o0 + DEVI_BUS_DMA_FLUSH], %o0
			! dip = (dev_info_t *) DEVI(dip)->devi_bus_dma_flush;
	ld	[%o0 + DEVI_DEV_OPS ], %g1	! dip->dev_ops
	ld	[%g1 + DEVI_BUS_OPS ], %g1	! dip->dev_ops->devo_bus_ops
	ld	[%g1 + OPS_FLUSH ], %g1
			! dip->dev_ops->devo_bus_ops->bus_dma_flush
	jmpl	%g1, %g0	! bop off to new routine
	nop			! as if we had never been here
	SET_SIZE(ddi_dma_flush)

#endif	/* lint */

#if	defined(lint)

/* ARGSUSED */
int
ddi_dma_win(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, uint_t win, off_t *offp,
	uint_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	return (DDI_SUCCESS);
}

#else	/* lint */

	ENTRY(ddi_dma_win)
	ld	[%o0 + DEVI_BUS_DMA_WIN], %o0
			! dip = (dev_info_t *) DEVI(dip)->devi_bus_dma_win;
	ld	[%o0 + DEVI_DEV_OPS ], %g1	! dip->dev_ops
	ld	[%g1 + DEVI_BUS_OPS ], %g1	! dip->dev_ops->devo_bus_ops
	ld	[%g1 + OPS_WIN ], %g1
			! dip->dev_ops->devo_bus_ops->bus_dma_win
	jmpl	%g1, %g0	! bop off to new routine
	nop			! as if we had never been here
	SET_SIZE(ddi_dma_win)

#endif	/* lint */

#if	defined(lint)

/*ARGSUSED*/
int
ddi_dma_mctl(register dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, u_int *lenp, caddr_t *objp, u_int flags)
{
	return (DDI_SUCCESS);
}

#else	/* lint */

	ENTRY(ddi_dma_mctl)
	ld	[%o0 + DEVI_BUS_DMA_CTL], %o0
			! dip = (dev_info_t *) DEVI(dip)->devi_bus_dma_ctl;
	ld	[%o0 + DEVI_DEV_OPS ], %g1	! dip->dev_ops
	ld	[%g1 + DEVI_BUS_OPS ], %g1	! dip->dev_ops->devo_bus_ops
	ld	[%g1 + OPS_MCTL ], %g1 ! dip->dev_ops->devo_bus_ops->bus_dma_ctl
	jmpl	%g1, %g0	! bop off to new routine
	nop			! as if we had never been here
	SET_SIZE(ddi_dma_mctl)

#endif	/* lint */

/*
 * Delay by spinning.
 *
 * drv_usecwait(clock_t n)	[DDI/DKI - section 9F]
 * usec_delay(int n)		[compatibility - should go one day]
 *
 * delay for n microseconds.  numbers <= 0 delay 1 usec
 *
 * the inner loop counts for sparc v7/v8, initialized in startup():
 * inner loop is 2 cycles, the outer loop adds 3 more cycles.
 * Cpudelay*cycletime(ns)*2 + cycletime(ns)*3 >= 1000
 *
 * model	cycletime(ns)	Cpudelay
 * 110		66		7
 * 260		60		7
 * 330		40		11
 * 460		30		16
 *
 * XXX	These should probably be platform-dependent functions, since
 *	MP machines with different speed cpu's may want to do something
 *	a little more snazzy here
 *
 *  For sun4u, the inner loop only takes 1 cycle due to its 
 *  pipelining etc. Cpudelay is computed as follows:
 * 	Cpudelay*cycletime(ns)*1 + cycletime(ns)*3 > = 1000
 *  The value of Cpudelay is set differently and depends on how this code
 *  is written. 
 *	 DO NOT CHANGE THIS CODE WITHOUT AJDUSTING CPUDELAY.
 */

#if defined(lint)

/*ARGSUSED*/
void
drv_usecwait(clock_t n)
{}

/*ARGSUSED*/
void
usec_delay(int n)
{}

#else	/* lint */


/*
 * 	For sun4u machines, if the 2 instructions inside the inner loop 
 *	reside on different cache lines, the pipeliningg will make it
 *	take only 2 cycles  instead of 1 to execute, hence increasing 
 *	the time by 2x. On sun4u we calculate Cpudelay assuming the
 *	inner loop would take only 1 cycle for the 2 instr.
 *	Therefore we need to make sure they resides in
 *	the same cacheline, ie 32byte block.  Since this should not
 *	affect other archs, we have add it here. Ideally this routine
 *	should be arch-dependent.
 */
	.section	".text"
	.align	32

	ALTENTRY(drv_usecwait)
	ENTRY(usec_delay)
	sethi	%hi(Cpudelay), %o5
	ld	[%o5 + %lo(Cpudelay)], %o4 ! microsecond countdown counter
	orcc	%o4, 0, %o3		! set cc bits to nz
	
1:	bnz	1b			! microsecond countdown loop
	subcc	%o3, 1, %o3		! 2 instructions in loop

	subcc	%o0, 1, %o0		! now, for each microsecond...
	bg	1b			! go n times through above loop
	orcc    %o4, 0, %o3
	retl
	nop
	SET_SIZE(usec_delay)
	SET_SIZE(drv_usecwait)

#endif	/* lint */
