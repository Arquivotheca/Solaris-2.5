/*
 * Copyright (c) 1990-1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)subr_4c.s	1.17	94/11/21 SMI"

/*
 * General assembly language routines.
 */

#if defined(lint)
#include <sys/types.h>
#include <sys/machsystm.h>
#endif

#include <sys/asm_linkage.h>
#include <sys/psr.h>
#include <sys/mmu.h>
#include <sys/enable.h>
#include <sys/eeprom.h>
#include <sys/intreg.h>

#if !defined(lint)
#include "assym.s"
#endif	/* lint */

#ifdef notdef
#ifdef sun4c
/*
 * Enable and disable video interrupt. (sun4 only)
 * setintrenable(value)
 *	int value;		0 = off, otherwise on
 */
#if	defined(lint)

/*ARGSUSED*/
void
setintrenable(int onoff)
{}

#else	/* lint */

	ENTRY(setintrenable)
	sethi	%hi(.not_a_sun4_msg), %o0
	call	panic
	or	%o0, %lo(.not_a_sun4_msg), %o0
	SET_SIZE(setintrenable)

#endif	/* lint */

/*
 * Enable and disable video. (sun4 only; not sun4c)
 */

#if	defined(lint)

/*ARGSUSED*/
void
setvideoenable(int onoff)
{}

#else	/* lint */

	ENTRY(setvideoenable)
	sethi	%hi(.not_a_sun4_msg), %o0
	call	panic
	or	%o0, %lo(.not_a_sun4_msg), %o0
	SET_SIZE(setvideoenable)

#endif	/* lint */

/*
 * Read the state of the video. (sun4 only; not sun4c)
 */

#if	defined(lint)

int
getvideoenable(void)
{ return (0); }

#else	/* lint */

	ENTRY(getvideoenable)
	sethi	%hi(.not_a_sun4_msg), %o0
	call	panic
	or	%o0, %lo(.not_a_sun4_msg), %o0
	SET_SIZE(getvideoenable)

.not_a_sun4_msg:
	.asciz	"can't happen: no video enable, not a sun4"
	.align	4

#endif	/* lint */
#endif	/* sun4c */
#endif	/* notdef */

#ifdef sun4c

/*
 * Flush any write buffers between the CPU and the device at address v.
 * This will force any pending stores to complete, and any exceptions
 * they cause to occur before this routine returns.
 *
 * void
 * flush_writebuffers_to(caddr_t v)
 *
 * We implement this by reading the context register; this will stall
 * until the store buffer(s) are empty, on both 4/60's and 4/70's (and
 * clones).  Note that we ignore v on Sun4c.
 */
#if defined(lint)

/*ARGSUSED*/
void
flush_writebuffers_to(caddr_t v)
{}

#else	/* lint */

	ENTRY(flush_writebuffers_to)
	set	CONTEXT_REG, %o1
	lduba	[%o1]ASI_CTL, %g0	! read the context register
	nop; nop			! two delays for synchronization
	nop; nop			! two more to strobe the trap address
	nop				! and three to empty the pipleine
	retl				!  so that any generated interrupt
	nop				!  will occur before we return
	SET_SIZE(flush_writebuffers_to)

#endif	/* lint */

#endif	/* sun4c */

#if defined(lint)

unsigned int
stub_install_common(void)
{ return (0); }

#else	/* lint */

	ENTRY_NP(stub_install_common)
	save	%sp, -96, %sp
	call	install_stub,1
	mov	%g1, %o0
	jmpl	%o0, %g0
	restore

#endif	/* lint */

#if defined(lint)

int
impl_setintreg_on(void)
{ return (0); }

#else   /* lint */

	DGDEF(fd_softintr_cookie)		.word IR_SOFT_INT4

	ENTRY_NP(impl_setintreg_on)

	/*
	 * set a pending software interrupt
	 * This code is tricky since is is called in a trap
	 * window and only uses %l5 and %l6.
	 * %l6 starts as the interrupt bit to or into the intreg.
	 * %l5 is used as both the pointer to intreg and the content.
	 * finally %l6 is used as the pointer to intreg.
	 * %l7 is the return address.
	 */
	sethi	%hi(INTREG_ADDR), %l5
	ldub	[%l5 + %lo(INTREG_ADDR)], %l5
	or	%l5, %l6, %l5
	sethi	%hi(INTREG_ADDR), %l6
	jmp	%l7 + 8
	stb	%l5, [%l6 + %lo(INTREG_ADDR)]
	SET_SIZE(impl_setintreg_on)

#endif  /* lint */

#include <sys/archsystm.h>

/*
 * Answer questions about any extended SPARC hardware capabilities.
 * On this platform, the answer is uniformly "None".
 */

#if	defined(lint)

/*ARGSUSED*/
int
get_hwcap_flags(int inkernel)
{ return (0); }

#else	/* lint */

	ENTRY(get_hwcap_flags)
	retl
	mov	0, %o0
	SET_SIZE(get_hwcap_flags)

#endif	/* lint */
