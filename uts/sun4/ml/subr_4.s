/*
 * Copyright (c) 1990-1994, by Sun Microsystems, Inc.
 */

#ident	"@(#)subr_4.s	1.9	94/10/26 SMI" /* From 4.1.1 sun4/subr.s 1.16 */

/*
 * General assembly language routines.
 */

#if defined(lint)
#include <sys/types.h>
#include <sys/machsystm.h>
#endif

#include <sys/asm_linkage.h>
#include <sys/psw.h>
#include <sys/mmu.h>
#include <sys/enable.h>
#include <sys/intreg.h>
#include <sys/eeprom.h>

#if !defined(lint)
#include "assym.s"
#endif	/* lint */
	
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

	.global	set_intreg
	ENTRY(setintrenable)
	mov	%o0, %o1
	b	set_intreg
	mov	IR_ENA_VID8, %o0
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

	.global	on_enablereg, off_enablereg
	ENTRY(setvideoenable)
	tst	%o0
	bnz	on_enablereg
	mov	ENA_VIDEO, %o0
	b,a	off_enablereg
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

	! XXX	The enable register needs at least a mutex to
	!	protect it from the unwary :-)

	ENTRY(getvideoenable)
	set	ENABLEREG, %o0
	lduba	[%o0]ASI_CTL, %o0
	retl
	and	%o0, ENA_VIDEO, %o0
	SET_SIZE(getvideoenable)

#endif	/* lint */

/*
 * read sun4_330 conf register, Memory configuration.
 */

#if	defined(lint)

int
read_confreg(void)
{ return (0); }

#else	/* lint */

	ENTRY(read_confreg)
	mov	0x2, %o0
	retl
	lduha	[%o0]ASI_SM, %o0
	SET_SIZE(read_confreg)

#endif	/* lint */

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

#include <sys/archsystm.h>

/*
 * Answer questions about extended SPARC hardware capabilities.
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
