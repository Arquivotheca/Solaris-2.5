/*
 * Copyright (c) 1990-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)sparc_subr.s	1.58	95/10/19 SMI"

/*
 * General assembly language routines.
 * It is the intent of this file to contain routines that are
 * independent of the specific kernel architecture, and those that are
 * common across kernel architectures.
 * As architectures diverge, and implementations of specific
 * architecture-dependent routines change, the routines should be moved
 * from this file into the respective ../`arch -k`/subr.s file.
 * For example, this file used to contain getidprom(), but the sun4c & sun4e
 * getidprom() diverged from the sun4 getidprom() so getidprom() was
 * moved.
 */

#if defined(lint)
#include <sys/types.h>
#include <sys/scb.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>	/* BUG: splzs is -platform- specific */
#define	SUNDDI_IMPL		/* prevent spltty -> i_ddi_spltty etc. */
#include <sys/sunddi.h>
#endif	/* lint */

#include <sys/asm_linkage.h>
#include <sys/machparam.h>	/* To get SYSBASE and PAGESIZE */
#include <sys/psr.h>
#include <sys/mmu.h>
#include <sys/machthread.h>
#include <sys/clock.h>

#if defined(sun4) || defined(sun4c) || defined(sun4m) || defined(sun4e)
#include <sys/enable.h>
#endif /* sun4 | sun4c | sun4m | sun4e */

#if !defined(lint)
#include "assym.s"

/* #define DEBUG */
	.seg	".text"
	.align	4

/*
 * Macro to raise processor priority level.
 * Avoid dropping processor priority if already at high level.
 * Also avoid going below CPU->cpu_base_spl, which could've just been set by
 * a higher-level interrupt thread that just blocked.
 */
#define	RAISE(level)	RAISE_PIL((level) << PSR_PIL_BIT)

/*
 * RAISE_PIL macro used by RAISE() and splr().
 *
 * newpil can be %o0 (not other regs used here) or a constant with
 * the new PIL in the PSR_PIL field of the level arg.
 */
#define	RAISE_PIL(newpil) \
	rd	%psr, %o1;		/* find current PIL */		\
	and	%o1, PSR_PIL, %o2;	/* mask current PIL */		\
	cmp	%o2, newpil;		/* is PIL high enough? */	\
	bge	2f;			/* yes, return */		\
	andn	%o1, PSR_PIL, %o3;	/* delay - clear old PIL */	\
	wr	%o3, PSR_PIL, %psr; 	/* freeze CPU_BASE_SPL */	\
	ld	[THREAD_REG + T_CPU], %o2; /* psr delay */		\
	nop;				/* psr delay */			\
	ld	[%o2 + CPU_BASE_SPL], %o2;/* psr delay */		\
	cmp	%o2, newpil;						\
	bl,a	1f;			/* use new priority if base is less */ \
	wr	%o3, newpil, %psr;	/* use specified new level */	\
	wr	%o3, %o2, %psr;		/* use base level */		\
1:									\
	nop;				/* psr delay */			\
2:									\
	retl;				/* psr delay */			\
	mov	%o1, %o0		/* psr delay - return old PSR */


/*
 * Macro to raise processor priority level to level >= LOCK_LEVEL..
 */
#define	RAISE_HIGH(level)	RAISE_PIL_HIGH((level) << PSR_PIL_BIT)

/*
 * RAISE_PIL_HIGH macro used by RAISE_HIGH().
 * Doesn't require comparison to CPU->cpu_base_spl.
 *
 * newpil can be %o0 (not other regs used here) or a constant with
 * the new PIL in the PSR_PIL field of the level arg.
 */
#define	RAISE_PIL_HIGH(newpil) \
	rd	%psr, %o1;		/* find current PIL */		\
	and	%o1, PSR_PIL, %o2;	/* mask current PIL */		\
	cmp	%o2, newpil;		/* is PIL high enough? */	\
	bge	2f;			/* yes, return */		\
	andn	%o1, PSR_PIL, %o3;	/* delay - clear old PIL */	\
	wr	%o3, newpil, %psr;	/* use chosen value */		\
	nop;				/* psr delay */			\
2:									\
	retl;				/* psr delay */			\
	mov	%o1, %o0		/* psr delay - return old PSR */


/*
 * Macro to set the priority to a specified level.
 * Avoid dropping the priority below CPU->cpu_base_spl.
 */
#define	SETPRI(level)	SETPRI_PIL((level) << PSR_PIL_BIT)


/*
 * SETPRI_PIL macro used by SETPRI() and splx().
 *
 * newpil can be %o0 (not other regs used here) or a constant with
 * the new PIL in the PSR_PIL field of the level arg.
 */
#define	SETPRI_PIL(newpil) \
	mov	%psr, %o1; 		/* get old PSR */		\
	andn	%o1, PSR_PIL, %o3;	/* mask out old PIL */		\
	wr	%o3, PSR_PIL, %psr;	/* protect T_CPU and CPU_BASE_SPL */ \
	ld	[THREAD_REG + T_CPU], %o2; 	/* psr delay */		\
	nop;					/* psr delay */		\
	ld	[%o2 + CPU_BASE_SPL], %o2; 	/* psr delay */		\
	cmp	%o2, newpil;		/* compare new to base */ 	\
	bl,a	1f;			/* use new pri if base is less */ \
	wr	%o3, newpil, %psr; 	/* delay - use new pri */ 	\
	wr	%o3, %o2, %psr; 	/* use base pri */		\
1:									\
	nop; 				/* psr delay */			\
	retl; 				/* psr delay */			\
	mov	%o1, %o0		/* psr delay - return old PSR */

/*
 * Macro to set the priority to a specified level at or above LOCK_LEVEL.
 * Doesn't require comparison to CPU->cpu_base_spl.
 */
#define	SETPRI_HIGH(level)	SETPRI_PIL_HIGH((level) << PSR_PIL_BIT)

/*
 * SETPRI_PIL_HIGH macro used by SETPRI_HIGH() 
 *
 * newpil can be %o0 (not other regs used here) or a constant with
 * the new PIL in the PSR_PIL field of the level arg.
 */
#define	SETPRI_PIL_HIGH(newpil) \
	mov	%psr, %o1; 		/* get old PSR */		\
	andn	%o1, PSR_PIL, %o3;	/* mask out old PIL */		\
	wr	%o3, newpil, %psr; 	/* delay - use new pri */ 	\
	nop; 				/* psr delay */			\
	retl; 				/* psr delay */			\
	mov	%o1, %o0		/* psr delay - return old PSR */

#endif	/* lint */

	/*
	 * Berkley 4.3 introduced symbolically named interrupt levels
	 * as a way deal with priority in a machine independent fashion.
	 * Numbered priorities are machine specific, and should be
	 * discouraged where possible.
	 *
	 * Note, for the machine specific priorities there are
	 * examples listed for devices that use a particular priority.
	 * It should not be construed that all devices of that
	 * type should be at that priority.  It is currently were
	 * the current devices fit into the priority scheme based
	 * upon time criticalness.
	 *
	 * The underlying assumption of these assignments is that
	 * SPARC IPL 10 is the highest level from which a device
	 * routine can call wakeup.  Devices that interrupt from higher
	 * levels are restricted in what they can do.  If they need
	 * kernels services they should schedule a routine at a lower
	 * level (via software interrupt) to do the required
	 * processing.
	 *
	 * Examples of this higher usage:
	 *	Level	Usage
	 *	15	Asynchronous memory exceptions (Non-maskable)
	 *	14	Profiling clock (and PROM uart polling clock)
	 *	13	Audio device (on sun4c)
	 *	12	Serial ports
	 *	11	Floppy controller (on sun4c)
	 *
	 * The serial ports request lower level processing on level 6.
	 * Audio and floppy request lower level processing on level 4.
	 *
	 * Also, almost all splN routines (where N is a number or a
	 * mnemonic) will do a RAISE(), on the assumption that they are
	 * never used to lower our priority.
	 * The exceptions are:
	 *	spl8()		Because you can't be above 15 to begin with!
	 *	splzs()		Because this is used at boot time to lower our
	 *			priority, to allow the PROM to poll the uart.
	 *	spl0()		Used to lower priority to 0.
	 *	splsoftclock()	Used by hardclock to lower priority.
	 */

#if defined(lint)

int spl8(void)		{ return (0); }
int splaudio(void)	{ return (0); }
int spl7(void)		{ return (0); }
int splzs(void)		{ return (0); }
int splhigh(void)	{ return (0); }
int splhi(void)		{ return (0); }
int splclock(void)	{ return (0); }
int spltty(void)	{ return (0); }
int splbio(void)	{ return (0); }
int spl6(void)		{ return (0); }
int spl5(void)		{ return (0); }
int spl4(void)		{ return (0); }
int splimp(void)	{ return (0); }
int spl3(void)		{ return (0); }
int spl2(void)		{ return (0); }
int spl1(void)		{ return (0); }
int splnet(void)	{ return (0); }
int splsoftclock(void)	{ return (0); }
int spl0(void)		{ return (0); }

#else	/* lint */

	/* locks out all interrupts, including memory errors */
	ENTRY(spl8)
	SETPRI_HIGH(15)
	SET_SIZE(spl8)

	/* just below the level that profiling runs */
	ALTENTRY(splaudio)	/* Just sun4c for now */
	ENTRY(spl7)
	RAISE_HIGH(13)
	SET_SIZE(spl7)
	SET_SIZE(splaudio)

	/* sun specific - highest priority onboard serial i/o zs ports */
	ALTENTRY(splzs)	
	SETPRI_HIGH(12)	/* Can't be a RAISE, as it's used to lower us */
	SET_SIZE(splzs)

	/*
	 * should lock out clocks and all interrupts,
	 * as you can see, there are exceptions
	 */
	ALTENTRY(splhigh)
	ALTENTRY(splhi)

	/* the standard clock interrupt priority */
	ALTENTRY(splclock)

	/* highest priority for any tty handling */
	ALTENTRY(spltty)

	/* highest priority required for protection of buffered io system */
	ALTENTRY(splbio)

	/* machine specific */ 
	ENTRY2(spl6,spl5)
	RAISE_HIGH(10)		/* raise to 10, which is LOCK_LEVEL */
	SET_SIZE(splhigh)
	SET_SIZE(splhi)
	SET_SIZE(splclock)
	SET_SIZE(spltty)
	SET_SIZE(splbio)
	SET_SIZE(spl5)
	SET_SIZE(spl6)

	/*
	 * machine specific 
	 * for sun, some frame buffers must be at this priority
	 */
	ENTRY(spl4)
	RAISE(8)
	SET_SIZE(spl4)

	/* highest level that any network device will use */
	ALTENTRY(splimp)	

	/*
	 * machine specific 
	 * for sun, devices with limited buffering: tapes, ethernet
	 */
	ENTRY(spl3)
	RAISE(6)
	SET_SIZE(splimp)
	SET_SIZE(spl3)

	/*
	 * machine specific - not as time critical as above
	 * for sun, disks
	 */
	ENTRY(spl2)
	RAISE(4)
	SET_SIZE(spl2)

	ENTRY(spl1)
	RAISE(2)
	SET_SIZE(spl1)

	/* highest level that any protocol handler will run */
	ENTRY(splnet)
	RAISE(1)
	SET_SIZE(splnet)

	/* softcall priority */
	/* used by hardclock to LOWER priority */
	ENTRY(splsoftclock)
	SETPRI(1)
	SET_SIZE(splsoftclock)

	/* allow all interrupts */
	ENTRY(spl0)
	SETPRI(0)
	SET_SIZE(spl0)

#endif	/* lint */

/*
 * splx - set PIL back to that indicated by the old %PSR passed as an argument,
 * or to the CPU's base priority, whichever is higher.
 * sys_rtt (in locore.s) relies on this not to use %g1 or %g2.
 */

#if defined(lint)

/* ARGSUSED */
int
splx(int level)
{ return (0); }

#else	/* lint */

	ENTRY(splx)
	and	%o0, PSR_PIL, %o0	/* mask argument */
	SETPRI_PIL(%o0)			/* set PIL */
	SET_SIZE(splx)

#endif	/* level */

/*
 * splr()
 *
 * splr is like splx but will only raise the priority and never drop it
 * Be careful not to set priority lower than CPU->cpu_base_pri,
 * even though it seems we're raising the priority, it could be set higher
 * at any time by an interrupt routine, so we must block interrupts and
 * look at CPU->cpu_base_pri.
 *
 */

#if defined(lint)

/* ARGSUSED */
int
splr(int level)
{ return (0); }

#else	/* lint */

/*
 * splr(psr_pri_field)
 * splr is like splx but will only raise the priority and never drop it
 */
	ENTRY(splr)
	and	%o0, PSR_PIL, %o0	! mask proposed new value
	RAISE_PIL(%o0)
	SET_SIZE(splr)

#endif	/* lint */

/*
 * spldown()
 *
 * spldown is like splx but is only called at or above LOCK_LEVEL.
 * This means we don't need to raise the priority to look at cpu_base_pri.
 */
#if defined(lint)

/* ARGSUSED */
int
spldown(int level)
{ return (0); }

#else	/* lint */

	ENTRY(spldown)
	ld	[THREAD_REG + T_CPU], %o2	! get CPU pointer
	mov	%psr, %o1	 		! get old PSR
	and	%o0, PSR_PIL, %o0		! mask proposed new value
	andn	%o1, PSR_PIL, %o3		! mask out old PIL
	ld	[%o2 + CPU_BASE_SPL], %o2
	cmp	%o2, %o0			! compare new to base
	bl,a	1f				! use new pri if base is less
	wr	%o3, %o0, %psr 			! delay - use new pri
	wr	%o3, %o2, %psr	 		! use base pri
1:
	nop 					! psr delay
	retl 					! psr delay
	mov	%o1, %o0			! psr delay - return old PSR
	SET_SIZE(spldown)

#endif	/* lint */

/*
 * on_fault()
 * Catch lofault faults. Like setjmp except it returns one
 * if code following causes uncorrectable fault. Turned off
 * by calling no_fault().
 */

#if defined(lint)

/* ARGSUSED */
int
on_fault(label_t *ljb)
{ return (0); }

#else	/* lint */

	ENTRY(on_fault)
	st      %o0, [THREAD_REG + T_ONFAULT]
	set	catch_fault, %o1
	b	setjmp			! let setjmp do the rest
	st	%o1, [THREAD_REG + T_LOFAULT]	! put catch_fault in u.u_lofault

catch_fault:
	save	%sp, -WINDOWSIZE, %sp	! goto next window so that we can rtn
	ld      [THREAD_REG + T_ONFAULT], %o0
	b	longjmp			! let longjmp do the rest
	clr	[THREAD_REG + T_LOFAULT]	! turn off lofault
	SET_SIZE(on_fault)

#endif	/* lint */

/*
 * no_fault()
 * turn off fault catching.
 */

#if defined(lint)

void
no_fault(void)
{}

#else	/* lint */

	ENTRY(no_fault)
	clr      [THREAD_REG + T_ONFAULT]
	retl
	clr	[THREAD_REG + T_LOFAULT]	! turn off lofault
	SET_SIZE(no_fault)

#endif	/* lint */

/*
 * Setjmp and longjmp implement non-local gotos using state vectors
 * type label_t.
 *
 * setjmp(lp)
 * label_t *lp;
 */

#if defined(lint)

/* ARGSUSED */
int
setjmp(label_t *lp)
{ return (0); }

#else	/* lint */

	ENTRY(setjmp)
	st	%o7, [%o0 + L_PC]	! save return address
	st	%sp, [%o0 + L_SP]	! save stack ptr
	retl
	clr	%o0			! return 0
	SET_SIZE(setjmp)

#endif	/* lint */

/*
 * longjmp(lp)
 * label_t *lp;
 */

#if defined(lint)

/* ARGSUSED */
void
longjmp(label_t *lp)
{}

#else	/* lint */

	ENTRY(longjmp)
	!
        ! The following save is required so that an extra register
        ! window is flushed.  Flush_windows flushes nwindows-2
        ! register windows.  If setjmp and longjmp are called from
        ! within the same window, that window will not get pushed
        ! out onto the stack without the extra save below.  Tail call
        ! optimization can lead to callers of longjmp executing
        ! from a window that could be the same as the setjmp,
        ! thus the need for the following save.
        !
	save    %sp, -SA(MINFRAME), %sp
	call	flush_windows		! flush all but this window
	nop
	ld	[%i0 + L_PC], %i7	! restore return addr
	ld	[%i0 + L_SP], %fp	! restore sp for dest on foreign stack
	ret				! return 1
	restore	%g0, 1, %o0		! takes underflow, switches stacks
	SET_SIZE(longjmp)

#endif	/* lint */

/*
 * Enable and disable DVMA.
 */

#if defined(lint)

#if defined(sun4c) || defined(sun4) || defined(sun4e)
void
enable_dvma(void)
{}
#endif /* sun4c || sun4 || sun4e */

/* ARGSUSED */
void
on_enablereg(u_char bit)
{}

#else	/* lint */

#if defined(sun4c) || defined(sun4) || defined(sun4e)
	ENTRY(enable_dvma)
	mov	ENA_SDVMA, %o0		! enable system DVMA
#endif /* sun4c || sun4 || sun4e */

	! fall through to on_enablereg

#if defined(sun4c) || defined(sun4) || defined(sun4m) || defined(sun4e)
/*
 * Turn on a bit in the system enable register.
 * on_enablereg((u_char)bit)
 */
	ENTRY(on_enablereg)
	mov	%psr, %o3
	or	%o3, PSR_PIL, %g1	! spl hi to lock enable reg update
	mov	%g1, %psr
	nop; nop;			! psr delay
	set	ENABLEREG, %o2		! address of real version in hardware
	lduba	[%o2]ASI_CTL, %g1
	bset	%o0, %g1		! turn on bit
	stba	%g1, [%o2]ASI_CTL	! write out new enable register
	b	splx			! restore psr (or base priority)
	mov	%o3, %o0
	SET_SIZE(on_enablereg)
#endif /* sun4c || sun4 || sun4m || sun4e */

#if defined(sun4c) || defined(sun4) || defined(sun4e)
	SET_SIZE(enable_dvma)
#endif /* sun4c || sun4 || sun4e */

#endif	/* lint */



#if defined(lint)

#if defined(sun4c) || defined(sun4) || defined(sun4e)
void
disable_dvma(void)
{}
#endif /* sun4c || sun4 || sun4e */

/* ARGSUSED */
void
off_enablereg(u_char bit)
{}

#else	/* lint */

#if defined(sun4c) || defined(sun4) || defined(sun4e)
	ENTRY(disable_dvma)
	mov	ENA_SDVMA, %o0		! disable system DVMA
#endif /* sun4c || sun4 || sun4e */

	! fall through to off_enablereg
#if defined(sun4c) || defined(sun4) || defined(sun4m) || defined(sun4e)
/*
 * Turn off a bit in the system enable register.
 * off_enablereg((u_char)bit)
 */
	ENTRY(off_enablereg)
	mov	%psr, %o3
	or	%o3, PSR_PIL, %g1	! spl hi to lock enable reg update
	mov	%g1, %psr
	nop; nop;			! psr delay
	set	ENABLEREG, %o2		! address of real version in hardware
	lduba	[%o2]ASI_CTL, %g1
	bclr	%o0, %g1		! turn off bit
	stba	%g1, [%o2]ASI_CTL	! write out new enable register
	b	splx			! restore psr (or base priority)
	mov	%o3, %o0
	SET_SIZE(off_enablereg)
#endif /* sun4c || sun4 || sun4m || sun4e */

#if defined(sun4c) || defined(sun4) || defined(sun4e)
	SET_SIZE(disable_dvma)
#endif /* sun4c || sun4 || sun4e */

#endif	/* lint */

 /*
  * safely call a C procedure from kadb.
  * save pc and npc in "save_pc" and "save_npc"
  * set a breakpoint on "lastchance", set pc
  * to "sfsx", npc to "sfsx+4", and continue.  at the
  * breakpoint, reset the pc to the routine
  * you are calling, and stuff any arguments
  * needed.  after calling your routine
  * you should resume execution where you
  * left off, with your registers intact.
  */

#ifdef NEVER

#if !defined(lint)

/*	.global save_pc, save_npc */
	.global sfsx, lastchance, onreturn, trace_print
/*	.reserve save_pc, 4, ".data", 4		! save pc */
/*	.reserve save_npc, 2, ".data", 4	! save pc */
sfsx:
	save	%sp, -WINDOWSIZE, %sp	! save ins and locals
	save	%sp, -WINDOWSIZE, %sp	! save ins and locals
	mov	%g0,%o0
	mov	%g0,%o1
	mov	%g0,%o2
	mov	%g0,%o3
	mov	%g0,%o4
	mov	%g0,%o5
	set	trace_print,%o7
lastchance:
	jmpl	%o7, %o7
	nop
onreturn:
!	set	save_pc, %o7
!	ld	[%o7], %pc
!	set	save_npc, %o7
!	ld	[%o7], %npc
	restore	
	restore

#endif	/* lint */

#endif	/* NEVER */

/*
 * movtuc(length, from, to, table)
 *
 * VAX movtuc instruction (sort of).
 */

#if defined(lint)

/*ARGSUSED*/
int
movtuc(size_t length, u_char *from, u_char *to, u_char table[])
{ return (0); }

#else	/* lint */

	ENTRY(movtuc)
	tst     %o0
	ble     2f                      ! check length
	clr     %o4
 
	ldub    [%o1 + %o4], %g1        ! get next byte in string
0:
	ldub    [%o3 + %g1], %g1        ! get corresponding table entry
	tst     %g1                     ! escape char?
	bnz     1f
	stb     %g1, [%o2 + %o4]        ! delay slot, store it
 
	retl                            ! return (bytes moved)
	mov     %o4, %o0
1:
	inc     %o4                     ! increment index
	cmp     %o4, %o0                ! index < length ?
	bl,a    0b
	ldub    [%o1 + %o4], %g1        ! delay slot, get next byte in string
2:
	retl                            ! return (bytes moved)
	mov     %o4, %o0
	SET_SIZE(movtuc)
 
#endif	/* lint */

/*
 * scanc(length, string, table, mask)
 *
 * VAX scanc instruction.
 */

#if defined(lint)

/*ARGSUSED*/
int
scanc(size_t length, u_char *string, u_char table[], u_char mask)
{ return (0); }

#else	/* lint */

	ENTRY(scanc)
	tst	%o0
	ble	1f			! check length
	clr	%o4
0:
	ldub	[%o1 + %o4], %g1	! get next byte in string
	cmp	%o4, %o0		! interlock slot, index < length ?
	ldub	[%o2 + %g1], %g1	! get corresponding table entry
	bge	1f			! interlock slot
	btst	%o3, %g1		! apply the mask
	bz,a	0b
	inc	%o4			! delay slot, increment index
1:
	retl				! return(length - index)
	sub	%o0, %o4, %o0
	SET_SIZE(scanc)

#endif	/* lint */

/*
 * if a() calls b() calls caller(),
 * caller() returns return address in a().
 */

#if defined(lint)

caddr_t
caller(void)
{ return (0); }

#else	/* lint */

	ENTRY(caller)
	retl
	mov	%i7, %o0
	SET_SIZE(caller)

#endif	/* lint */

/*
 * if a() calls callee(), callee() returns the
 * return address in a();
 */

#if defined(lint)

caddr_t
callee(void)
{ return (0); }

#else	/* lint */

	ENTRY(callee)
	retl
	mov	%o7, %o0
	SET_SIZE(callee)

#endif	/* lint */

#ifdef notdef
/*
 * if a() calls b() calls c() calls caller(),
 * caller() returns return address in a().
 */

#if defined(lint)

caddr_t
caller2(void)
{ return (0); }

#else	/* lint */

	ENTRY(caller2)
	save	%sp, -SA(WINDOWSIZE), %sp
	call	flush_windows
	nop
	ld	[%fp + (8+6)*4], %o0	! load our caller's (c's) fp
	ld	[%o0 + (8+7)*4], %i0	! load HIS caller's (b's) return address
	ret
	restore

#endif	/* lint */
#endif	/* notdef */

/*
 * Get system enable register.
 */
#if defined(lint)

u_char
get_enablereg(void)
{ return (0); }

#else	/* lint */

#if defined(sun4c) || defined(sun4) || defined(sun4m) || defined(sun4e)
	ENTRY(get_enablereg)
	save	%sp, -SA(WINDOWSIZE), %sp	! save ins and locals
	mov	%psr, %o3
	or	%o3, PSR_PIL, %g1	! spl hi to lock enable reg update
	mov	%g1, %psr
	nop; nop;			! psr delay
	set	ENABLEREG, %o2		! address of real version in hardware
	lduba	[%o2]ASI_CTL, %g1
	call	splx			! restore psr (or base priority)
	mov	%o3, %o0
	mov	%g1, %i0		! Return system enable register
	ret
	restore
	SET_SIZE(get_enablereg)
#endif /* sun4c || sun4 || sun4m || sun4e */

#endif	/* lint */

#ifdef notdef
/*
 * Get Window Invalid Mask
 */

#if defined(lint)

greg_t
getwim(void)
{ return (0); }

#else	/* lint */

        ENTRY(getwim)
        retl
        mov     %wim, %o0
	SET_SIZE(getwim)

#endif	/* lint */
#endif /* notdef */

#ifdef notdef
/* 
 * return the current stack pointer
 */

#if defined(lint)

greg_t
getsp(void)
{ return (0); }

#else	/* lint */

	ENTRY(getsp)
	retl
	mov	%sp, %o0
	SET_SIZE(getsp)

#endif	/* lint */
#endif /* notdef */

/* 
 * return the current frame pointer
 */

#if defined(lint)

greg_t
getfp(void)
{ return (0); }

#else	/* lint */

	ENTRY(getfp)
	retl
	mov	%fp, %o0
	SET_SIZE(getfp)

#endif	/* lint */

/*
 * Get vector base register
 */      

#if defined(lint)

greg_t
gettbr(void)
{ return (0); }

greg_t
getvbr(void)
{ return (0); }

#else	/* lint */

	ENTRY2(gettbr,getvbr)
	retl
	mov     %tbr, %o0
	SET_SIZE(gettbr)
	SET_SIZE(getvbr)
 
#endif	/* lint */

/*
 * Get processor state register
 */

#if defined(lint)

greg_t
getpsr(void)
{ return (0); }

#else	/* lint */

	ENTRY(getpsr)
	retl
	mov     %psr, %o0
	SET_SIZE(getpsr)

#endif	/* lint */

/*
 * Get current processor interrupt level
 */

#if defined(lint)

greg_t
getpil(void)
{ return (0); }

#else	/* lint */

	ENTRY(getpil)
	mov	%psr, %o0
	and	%o0, PSR_PIL, %o0
	retl
	srl	%o0, PSR_PIL_BIT, %o0
	SET_SIZE(getpil)

#endif	/* lint */

/*
 * _insque(entryp, predp)
 *
 * Insert entryp after predp in a doubly linked list.
 */

#if defined(lint)

/*ARGSUSED*/
void
_insque(caddr_t entryp, caddr_t predp)
{}

#else	/* lint */

	ENTRY(_insque)
	ld      [%o1], %g1              ! predp->forw
	st      %o1, [%o0 + 4]          ! entryp->back = predp
	st      %g1, [%o0]              ! entryp->forw =  predp->forw
	st      %o0, [%o1]              ! predp->forw = entryp
	retl
	st      %o0, [%g1 + 4]          ! predp->forw->back = entryp
	SET_SIZE(_insque)
 
#endif	/* lint */

/*
 * _remque(entryp)
 *       
 * Remove entryp from a doubly linked list
 */

#if defined(lint)

/*ARGSUSED*/
void
_remque(caddr_t entryp)
{}

#else	/* lint */

	ENTRY(_remque)
	ld      [%o0], %g1              ! entryp->forw
	ld      [%o0 + 4], %g2          ! entryp->back
	st      %g1, [%g2]              ! entryp->back->forw = entryp->forw
	retl
	st      %g2, [%g1 + 4]          ! entryp->forw->back = entryp->back
	SET_SIZE(_remque)

#endif	/* lint */


/*
 * strlen(str), ustrlen(str)
 *
 * Returns the number of
 * non-NULL bytes in string argument.
 *
 * XXX -  why is this here, rather than the traditional file?
 *	  why does it have local labels which don't start with a `.'?
 */

#if defined(lint)

/*ARGSUSED*/
size_t
ustrlen(const char *str)
{ return (0); }

#else	/* lint */

	ENTRY(ustrlen)
	set	USERLIMIT, %o1
	cmp	%o0, %o1
	blu	.do_strlen
	nop
	ldub	[%o1], %g0		! dereference invalid addr to force trap
	nop
	.word	0			! illegal insn if USERLIMIT is valid

#endif	/* lint */

#if defined(lint)

/*ARGSUSED*/
size_t
strlen(const char *str)
{ return (0); }

#else	/* lint */

	ENTRY(strlen)
#ifdef DEBUG
	set     KERNELBASE, %o1
	cmp     %o0, %o1
	bgeu    1f
	nop

	set     2f, %o0
	call    panic
	nop
2:
	.asciz  "strlen: Arg pointer is not in kernel space"
	.align  4
1:
#endif DEBUG
.do_strlen:	
	mov	%o0, %o1
	andcc	%o1, 3, %o3		! is src word aligned
	bz	$nowalgnd
	clr	%o0			! length of non-zero bytes
	cmp	%o3, 2			! is src half-word aligned
	be	$s2algn
	cmp	%o3, 3			! src is byte aligned
	ldub	[%o1], %o3		! move 1 or 3 bytes to align it
	inc	1, %o1			! in either case, safe to do a byte
	be	$s3algn
	tst	%o3
$s1algn:
	bnz,a	$s2algn			! now go align dest
	inc	1, %o0
	b,a	$done

$s2algn:
	lduh	[%o1], %o3		! know src is half-byte aligned
	inc	2, %o1
	srl	%o3, 8, %o4
	tst	%o4			! is the first byte zero
	bnz,a	1f
	inc	%o0
	b,a	$done
1:	andcc	%o3, 0xff, %o3		! is the second byte zero
	bnz,a	$nowalgnd
	inc	%o0
	b,a	$done
$s3algn:
	bnz,a	$nowalgnd
	inc	1, %o0
	b,a	$done

$nowalgnd:
	! use trick to check if any read bytes of a word are zero
	! the following two constants will generate "byte carries"
	! and check if any bit in a byte is set, if all characters
	! are 7bits (unsigned) this allways works, otherwise
	! there is a specil case that rarely happens, see below

	set	0x7efefeff, %o3
	set	0x81010100, %o4

3:	ld	[%o1], %o2		! main loop
	inc	4, %o1
	add	%o2, %o3, %o5		! generate byte-carries
	xor	%o5, %o2, %o5		! see if orignal bits set
	and	%o5, %o4, %o5
	cmp	%o5, %o4		! if ==,  no zero bytes
	be,a	3b
	inc	4, %o0

	! check for the zero byte and increment the count appropriately
	! some information (the carry bit) is lost if bit 31
	! was set (very rare), if this is the rare condition,
	! return to the main loop again

	sethi	%hi(0xff000000), %o5	! mask used to test for terminator
	andcc	%o2, %o5, %g0		! check if first byte was zero
	bnz	1f
	srl	%o5, 8, %o5
$done:	
	retl
	nop
1:	andcc	%o2, %o5, %g0		! check if second byte was zero
	bnz	1f
	srl	%o5, 8, %o5
$done1:	
	retl
	inc	%o0
1:	andcc 	%o2, %o5, %g0		! check if third byte was zero
	bnz	1f
	andcc	%o2, 0xff, %g0		! check if last byte is zero
$done2:	
	retl
	inc	2, %o0
1:	bnz,a	3b
	inc	4, %o0			! count of bytes
$done3:	
	retl
	inc	3, %o0
	SET_SIZE(strlen)

#endif	/* lint */


/*
 * copyinstr_noerr(s1, s2, len)
 *
 * Copy string s2 to s1.  s1 must be large enough and len contains the
 * number of bytes copied.  s1 is returned to the caller.
 * s2 is user space and s1 is in kernel space
 * 
 * copyoutstr_noerr(s1, s2, len)
 *
 * Copy string s2 to s1.  s1 must be large enough and len contains the
 * number of bytes copied.  s1 is returned to the caller.
 * s2 is kernel space and s1 is in user space
 *
 * knstrcpy(s1, s2, len)
 *
 * This routine copies a string s2 in the kernel address space to string
 * s2 which is also in the kernel address space.
 *
 * XXX so why is the third parameter *len?
 *	  why is this in this file, rather than the traditional?
 *	  why does it have local labels which don't start with a `.'?
 */

#if defined(lint)


/*ARGSUSED*/
char *
copyinstr_noerr(char *s1, const char *s2, size_t *len)
{ return ((char *)0); }

/*ARGSUSED*/
char *
copyoutstr_noerr(char *s1, const char *s2, size_t *len)
{ return ((char *)0); }

/*ARGSUSED*/
char *
knstrcpy(char *s1, const char *s2, size_t *len)
{ return ((char *)0); }


#else	/* lint */

#define	DEST	%i0
#define	SRC	%i1
#define LEN	%i2
#define DESTSV	%i5
#define ADDMSK	%l0
#define	ANDMSK	%l1
#define	MSKB0	%l2
#define	MSKB1	%l3
#define	MSKB2	%l4
#define SL	%o0
#define	SR	%o1
#define	MSKB3	0xff

	ENTRY(copyoutstr_noerr)
#ifdef DEBUG
	set	KERNELBASE, %o4
	cmp	%o1, %o4
	bgeu	1f
	nop
3:
	set	2f, %o0
	call	panic
	nop

2:	.asciz	"copyoutstr_noerr: Arg not in user address space"
	.align 4

1:
	cmp	%o0, %o4
	blu	.do_cpy	
	nop

	ba,a	3b
	.empty

#endif

	ENTRY(copyinstr_noerr)

#ifdef DEBUG
	set	KERNELBASE, %o4
	cmp	%o0, %o4
	bgeu	1f
	nop
3:
	set	2f, %o0
	call	panic
	nop

2:	.asciz	"copyinstr_noerr: Arg not in user address space"
	.align 4

1:
	cmp	%o1, %o4
	blu	.do_cpy	
	nop

	ba,a	3b
	.empty
#endif

	ALTENTRY(knstrcpy)
#ifdef DEBUG
	set	KERNELBASE, %o4
	cmp	%o0, %o4
	bgeu	1f
	nop
3:
	set	2f, %o0
	call	panic
	nop

2:	.asciz	"knstrcpy: Arg not in user address space"
	.align 4

1:
	cmp	%o1, %o4
	blu	3b	
	nop

#endif

.do_cpy:
	save	%sp, -SA(WINDOWSIZE), %sp	! get a new window
	clr	%l6
	clr	%l7
	andcc	SRC, 3, %i3		! is src word aligned
	bz	.aldest
	mov	DEST, DESTSV		! save return value
	cmp	%i3, 2			! is src half-word aligned
	be	.s2algn
	cmp	%i3, 3			! src is byte aligned
	ldub	[SRC], %i3		! move 1 or 3 bytes to align it
	inc	1, SRC
	stb	%i3, [DEST]		! move a byte to align src
	be	.s3algn
	tst	%i3
.s1algn:
	bnz	.s2algn			! now go align dest
	inc	1, DEST
	b,a	.done
.s2algn:
	lduh	[SRC], %i3		! know src is 2 byte alinged
	inc	2, SRC
	srl	%i3, 8, %i4
	tst	%i4
	stb	%i4, [DEST]
	bnz,a	1f
	stb	%i3, [DEST + 1]
	inc	1, %l6
	b,a	.done
1:	andcc	%i3, MSKB3, %i3
	bnz	.aldest
	inc	2, DEST
	b,a	.done
.s3algn:
	bnz	.aldest
	inc	1, DEST
	b,a	.done

.aldest:
	set	0xfefefeff, ADDMSK	! masks to test for terminating null
	set	0x01010100, ANDMSK
	sethi	%hi(0xff000000), MSKB0
	sethi	%hi(0x00ff0000), MSKB1

	! source address is now aligned
	andcc	DEST, 3, %i3		! is destination word aligned?
	bz	.w4str
	srl	MSKB1, 8, MSKB2		! generate 0x0000ff00 mask
	cmp	%i3, 2			! is destination halfword aligned?
	be	.w2str
	cmp	%i3, 3			! worst case, dest is byte alinged
.w1str:
	ld	[SRC], %i3		! read a word
	inc	4, SRC			! point to next one
	be	.w3str
	andcc	%i3, MSKB0, %g0		! check if first byte was zero
	bnz	1f
	andcc	%i3, MSKB1, %g0		! check if second byte was zero
	b,a	.done1
1:	bnz	1f
	andcc	%i3, MSKB2, %g0		! check if third byte was zero
	b,a	.w1done2
1:	bnz	1f
	andcc	%i3, MSKB3, %g0		! check if last byte is zero
	b,a	.w1done3
1:	bz	.w1done4
	srl	%i3, 24, %o3		! move three bytes to align dest
	stb	%o3, [DEST]
	srl	%i3, 8, %o3
	sth	%o3, [DEST + 1]
	inc	3, DEST			! destination now aligned
	mov	%i3, %o3
	mov	24, SL
	b	8f			! inner loop same for w1str and w3str
	mov	8, SR			! shift amounts are different

2:	inc	4, DEST
8:	andcc	%l7, MSKB3, %g0		! check if exit flag is set
	bnz	3f
	sll	%o3, SL, %o2		! save remaining byte
	ld	[SRC], %o3		! read a word
	inc	4, SRC			! point to next one
3:	srl	%o3, SR, %i3
	addcc	%o3, ADDMSK, %l5	! check for a zero byte
	bcc	1f			! there must be one
	xor	%l5, %o3, %l5
	and	%l5, ANDMSK, %l5
	cmp	%l5, ANDMSK
	be,a	4f			! if no zero byte in word,don't set flag
	nop
1:	inc	1, %l7			! Set exit flag
4:
	or	%i3, %o2, %i3
	addcc	%i3, ADDMSK, %l5	! check for a zero byte
	bcc	1f			! there must be one
	xor	%l5, %i3, %l5
	and	%l5, ANDMSK, %l5
	cmp	%l5, ANDMSK
	be,a	2b			! if no zero byte in word, loop
	st	%i3, [DEST]

1:
	andcc	%i3, MSKB0, %g0		! check if first byte was zero
	bnz	1f
	andcc	%i3, MSKB1, %g0		! check if second byte was zero
	b,a	.done1
1:	bnz	1f
	andcc	%i3, MSKB2, %g0		! check if third byte was zero
	b,a	.w1done2
1:	bnz	1f
	andcc	%i3, MSKB3, %g0		! check if last byte is zero
	b,a	.w1done3
1:	st	%i3, [DEST]		! it is safe to write the word now
	bnz	8b			! if not zero, go read another word
	inc	4, DEST			! else finished
	b,a	.done

.w1done4:
	stb	%i3, [DEST + 3]
	inc	1, %l6
.w1done3:
	srl	%i3, 8, %o3
	stb	%o3, [DEST + 2]
	inc	1, %l6
.w1done2:
	srl	%i3, 24, %o3
	stb	%o3, [DEST]
	srl	%i3, 16, %o3
	inc	2, %l6
	b	.done
	stb	%o3, [DEST + 1]

.w3str:
	bnz	1f
	andcc	%i3, MSKB1, %g0		! check if second byte was zero
	b,a	.done1
1:	bnz	1f
	andcc	%i3, MSKB2, %g0		! check if third byte was zero
	b,a	.w1done2
1:	bnz	1f
	andcc	%i3, MSKB3, %g0		! check if last byte is zero
	b,a	.w1done3
1:	bz	.w1done4
	srl	%i3, 24, %o3
	stb	%o3, [DEST]
	inc	1, DEST
	mov	%i3, %o3
	mov	8, SL
	b	8b			! inner loop same for w1str and w3str
	mov	24, SR			! shift amounts are different

.w2done4:
	srl	%i3, 16, %o3
	sth	%o3, [DEST]
	inc	4, %l6
	b	.done
	sth	%i3, [DEST + 2]

.w2str:
	ld	[SRC], %i3		! read a word
	inc	4, SRC			! point to next one
	andcc	%i3, MSKB0, %g0		! check if first byte was zero
	bnz	1f
	andcc	%i3, MSKB1, %g0		! check if second byte was zero
	b,a	.done1
1:	bz	.done2

	srl	%i3, 16, %o3
	sth	%o3, [DEST]
	inc	2, DEST
	b	9f
	mov	%i3, %o3

2:	inc	4, DEST
9:	sll	%o3, 16, %i3		! save rest
	andcc	%i3, MSKB0, %g0		! check if first byte was zero
	bnz	1f
	andcc	%i3, MSKB1, %g0		! check if second byte was zero
	b,a	.done1
1:	bnz,a	1f
	ld	[SRC], %o3		! read a word
	b,a	.done2
1:	inc	4, SRC			! point to next one
	srl	%o3, 16, %o2
	or	%o2, %i3, %i3

	addcc	%i3, ADDMSK, %l5	! check for a zero byte
	bcc	1f			! there must be one
	xor	%l5, %i3, %l5
	and	%l5, ANDMSK, %l5
	cmp	%l5, ANDMSK
	be,a	2b			! if no zero byte in word, loop
	st	%i3, [DEST]

1:	andcc	%i3, MSKB2, %g0		! check if third byte was zero
	bnz	1f
	andcc	%i3, MSKB3, %g0		! check if last byte is zero
	b,a	.done3
1:	st	%i3, [DEST]		! it is safe to write the word now
	bnz	9b			! if not zero, go read another word
	inc	4, DEST			! else fall through
	b,a	.done

2:	inc	4, DEST
.w4str:
	ld	[SRC], %i3		! read a word
	inc	4, %i1			! point to next one

	addcc	%i3, ADDMSK, %l5	! check for a zero byte
	bcc	1f			! there must be one
	xor	%l5, %i3, %l5
	and	%l5, ANDMSK, %l5
	cmp	%l5, ANDMSK
	be,a	2b			! if no zero byte in word, loop
	st	%i3, [DEST]

1:	andcc	%i3, MSKB0, %g0		! check if first byte was zero
	bnz	1f
	andcc	%i3, MSKB1, %g0		! check if second byte was zero
	b,a	.done1
1:	bnz	1f
	andcc	%i3, MSKB2, %g0		! check if third byte was zero
	b,a	.done2
1:	bnz	1f
	andcc	%i3, MSKB3, %g0		! check if last byte is zero
	b,a	.done3
1:	st	%i3, [DEST]		! it is safe to write the word now
	bnz	.w4str			! if not zero, go read another word
	inc	4, DEST			! else fall through

.done:
	sub	DEST, DESTSV, %l0
	add	%l0, %l6, %l0
	st	%l0, [LEN]
	ret			! last byte of word was the terminating zero
	restore	DESTSV, %g0, %o0

.done1:
	stb	%g0, [DEST]	! first byte of word was the terminating zero
	sub	DEST, DESTSV, %l0
	inc	1, %l6
	add	%l0, %l6, %l0
	st	%l0, [LEN]
	ret
	restore	DESTSV, %g0, %o0

.done2:
	srl	%i3, 16, %i4	! second byte of word was the terminating zero
	sth	%i4, [DEST]
	sub	DEST, DESTSV, %l0
	inc	2, %l6
	add	%l0, %l6, %l0
	st	%l0, [LEN]
	ret
	restore	DESTSV, %g0, %o0

.done3:
	srl	%i3, 16, %i4	! third byte of word was the terminating zero
	sth	%i4, [DEST]
	stb	%g0, [DEST + 2]
	sub	DEST, DESTSV, %l0
	inc	3, %l6
	add	%l0, %l6, %l0
	st	%l0, [LEN]
	ret
	restore	DESTSV, %g0, %o0
	SET_SIZE(copyinstr_noerr)
	SET_SIZE(copyoutstr_noerr)
	SET_SIZE(knstrcpy)

#endif	/* lint */


#if defined(sun4) || defined(sun4c) || defined(sun4m) || defined(sun4e)
#include <sys/intreg.h>

#if defined(sun4) || defined(sun4c) || defined(sun4e)
/*
 * Turn on or off bits in the interrupt register.
 * We must lock out interrupts, since we don't have an atomic or/and to mem.
 * set_intreg(bit, value)
 *	int bit;		bit mask in interrupt reg
 *	int flag;		0 = off, otherwise on
 */

#if defined(lint)

/*ARGSUSED*/
void
set_intreg(int bit, int flag)
{}

#else	/* lint */

	ENTRY_NP(set_intreg)
	mov	%psr, %g2
	or	%g2, PSR_PIL, %g1	! spl hi to protect intreg update
	mov	%g1, %psr
	nop;				! psr delay
	set	INTREG_ADDR, %o2	! interrupt register address
	ldub	[%o2], %g1		! read interrupt register
	tst	%o1
	bnz,a	1f
	bset	%o0, %g1		! on
	bclr	%o0, %g1		! off
1:
	stb	%g1, [%o2]		! request a level 1 interrupt
	!
	! Need to use an inline version of splx since an splx call will
	! fail if we are profiling.
	!
	! Have to check CPU->cpu_base_spl because we might have been
	! interrupted between reading and setting the psr.
	!
	ld	[THREAD_REG + T_CPU], %o1	! get base priority level
	ld	[%o1 + CPU_BASE_SPL], %o1
	and	%g2, PSR_PIL, %o2	! get old pri 
	andn	%g2, PSR_PIL, %o3	! clear PIL from old PSR
	cmp	%o1, %o2
	bl,a	1f
	wr	%o3, %o2, %psr		! base priority is less, use old pri
	wr	%o3, %o1, %psr		! use base priority
1:
	nop
	retl
	nop
	SET_SIZE(set_intreg)

#endif	/* lint */

#endif /* sun4 || sun4c || sun4e */


/*
 * void setsoftint(u_int pri)
 */

#if defined(lint)

/* ARGSUSED */
void
setsoftint(u_int pri)
{}

#else	/* lint */

	ENTRY(setsoftint)
	mov	1, %o1
	cmp	%o0, 4
	be,a	set_intreg
	set	IR_SOFT_INT4, %o0	! too big for mov in sun4m
	cmp	%o0, 6
	be,a	set_intreg
	set	IR_SOFT_INT6, %o0	! too big for mov in sun4m
	retl				! wrong values- just return
	nop    			! following "mov 1,%o1" ok in delay slot
	SET_SIZE(setsoftint)

#endif	/* lint */

/*
 * Turn on a software interrupt (H/W level 1).
 */

#if defined(lint)

void
siron(void)
{}

#else	/* lint */

	ENTRY(siron)
	mov	1, %o1
	b	set_intreg
	set	IR_SOFT_INT1, %o0	! too big for mov in sun4m
	SET_SIZE(siron)

#endif	/* lint */
#endif /* sun4 || sun4c || sun4m || sun4e */

/*
 * Provide a C callable interface to the trap that reads the hi-res timer.
 * Returns 64-bit nanosecond timestamp in %o0 and %o1.
 */

#if defined(lint)

hrtime_t
gethrtime(void)
{
	return ((hrtime_t)0);
}

void
gethrestime(timespec_t *tp)
{
	tp->tv_sec = 0;
	tp->tv_nsec = 0;
}

#else	/* lint */

	ENTRY_NP(gethrtime)
#ifdef sun4d
	mov	%psr, %o5
	wr	%o5, PSR_ET, %psr		! disable traps
	GET_HRTIME(%o0, %o1, %o2, %o3, %o4)	! get time into %o0:%o1
	wr	%o5, 0, %psr
	nop
	retl
	nop
#else
	GET_HRTIME(%o0, %o1, %o2, %o3, %o4)	! get time into %o0:%o1
	retl
	nop
#endif
	SET_SIZE(gethrtime)

/*
 * Fast trap to return a timestamp, uses trap window, leaves traps
 * disabled.  Returns a 64-bit nanosecond timestamp in %i0 and %i1.
 */

	ENTRY_NP(get_timestamp)
	GET_HRTIME(%i0, %i1, %l4, %l5, %l6)	! get time into %i0:%i1
	jmp	%l2
	rett	%l2+4
	SET_SIZE(get_timestamp)


	ENTRY_NP(gethrestime)
	mov	%o0, %o5
	ta	ST_GETHRESTIME
	st	%o0, [%o5]
	retl
	st	%o1, [%o5+4]
	SET_SIZE(gethrestime)

/*
 * Fast trap for gettimeofday(). This returns a timestruc_t.
 * It uses the GET_HRESTIME to get the values of hrestime and hrestime_adj.
 * GET_HRESTIME macro returns the values in following  registers:
 * %i0 = hrestime.tv_sec
 * %i1 = hrestime.tv_nsec
 * %i2 = hrestime_adj (hi)
 * %i3 = hrestime_adj (lo)
 * %l4 = nanosecs since last tick
 * %l3 = scratch
 */

	ENTRY_NP(get_hrestime)
	GET_HRESTIME(%l4, %l3, %l6, %i2, %i0)	! %l4 = nsec_since_last_tick
	orcc	%i2, %i3, %g0			! if hrestime_adj == 0
	be	3f				!		go to 3
	add	%i1, %l4, %i1			! hrestime.tv_nsec + nslt
	tst	%i2
	bl	2f				! if hrestime_adj is negative
	srl	%l4, ADJ_SHIFT, %l6		! %l6 = nslt_div_16 = nslt >> 4
	subcc	%i3, %l6, %g0			! hrestime_adj - nslt_div_16
	subxcc	%i2, %g0, %g0			! subtract any carry
	bl,a	3f				! if hrestime_adj is smaller
	add	%i1, %i3, %i1			! hrestime.nsec += hrestime_adj
	ba	3f
	add	%i1, %l6, %i1			! hrestime.nsec += nslt_div_16
2:
	addcc	%i3, %l6, %g0			! hrestime_adj + nslt_div_16
	addxcc	%i2, %g0, %g0			! add any carry
	bge,a	3f				! if hrestime_adj is smaller neg
	add	%i1, %i3, %i1			! hrestime.nsec += hrestime_adj
	sub	%i1, %l6, %i1			! hrestime.nsec -= nslt_div_16
3:	
	set	NANOSEC, %i2
	cmp	%i1, %i2
	bl	4f				! if hrestime.tv_nsec < NANOSEC
	nop					! delay slot
	add	%i0, 0x1, %i0			! hrestime.tv_sec++
	sub	%i1, %i2, %i1			! hrestime.tv_nsec - NANOSEC
4:
	jmp	%l2
	rett	%l2+4
	SET_SIZE(get_hrestime)


/*
 * Fast trap to return lwp virtual time, uses trap window, leaves traps
 * disabled.  Returns a 64-bit number in %i0 and %i1, which is the number
 * of nanoseconds consumed.
 */
	ENTRY_NP(get_virtime)
	GET_HRTIME(%i0, %i1, %l6, %l7, %l3)	! get time into %i0:%i1
	CPU_ADDR(%l3, %l4)			! CPU struct ptr to %l3
	ld	[%l3 + CPU_THREAD], %l3		! thread pointer to %l3
	lduh	[%l3 + T_PROC_FLAG], %l4
	btst	TP_MSACCT, %l4			! test for MSACCT on
	bz	1f				! not on - do estimate
	ld	[%l3 + T_LWP], %l4		! delay - lwp pointer to %l4

	/*
	 * Subtract start time of current microstate from time
	 * of day (i0,i1) to get increment for lwp virtual time.
	 */
	ldd	[%l4 + LWP_STATE_START], %l6	! ms_state_start
	subcc	%i1, %l7, %i1
	subx	%i0, %l6, %i0

	/*
	 * Add current value of ms_acct[LMS_USER]
	 */
	ldd	[%l4 + LWP_ACCT_USER], %l6	! ms_acct[LMS_USER]
	addcc	%i1, %l7, %i1
	addx	%i0, %l6, %i0

	jmp	%l2
	rett	%l2+4

	/*
	 * Microstate accounting times are not available.
	 * Estimate based on tick samples.
	 * Convert from ticks (100 Hz) to nanoseconds (mult by 10,000,000).
	 * Use step-wise multiply because it works on all Sparcs. 
	 * Would otherwise need to enable traps.
	 */
1:
	ld	[%l4 + LWP_UTIME], %l7		! utime = lwp->lwp_utime;
	mov	%y, %l1				! save Y register
	mov	%l7, %y				! product
	set	10000000, %l3			! multiplier
	andcc	%g0, %g0, %l6

	mulscc	%l6, %l3, %l6			! first iteration of 33

	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6

	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6

	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6

	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6

	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6

	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6
	mulscc	%l6, %l3, %l6

	mulscc	%l6, %l3, %l6			! 32nd iteration
	mulscc	%l6, %g0, %l6			! last iteration only shifts
	mov	%y, %l7				! low order part of product

	/*
	 * Sanity-check estimate.
	 * If elapsed real time for process is smaller, use that instead.
	 */
	ldd	[%l4 + LWP_MS_START], %l4	! starting real time for LWP
	subcc	%i1, %l5, %i1			! subtract start time
	subx	%i0, %l4, %i0
	cmp	%l6, %i0			! compare high virt - elapsed
	be,a	2f				! compare low if highs equal
	cmp	%l7, %i1			! delay - compare low
2:
	bgu	3f				! virtual greater - use elapsed
	mov	%l1, %y				! delay - restore %y reg
	mov	%l6, %i0			! move computed virtual time
	mov	%l7, %i1
3:
	jmp	%l2
	rett	%l2 + 4				! return from trap
	SET_SIZE(get_virtime)

#endif	/* lint */

#ifdef	TRACE

/*
 * Provide a C callable interface to the tracing traps.
 */

#if defined(lint)

/* ARGSUSED */
void trace_0(u_long)
{}
/* ARGSUSED */
void trace_1(u_long, u_long)
{}
/* ARGSUSED */
void trace_2(u_long, u_long, u_long)
{}
/* ARGSUSED */
void trace_3(u_long, u_long, u_long, u_long)
{}
/* ARGSUSED */
void trace_4(u_long, u_long, u_long, u_long, u_long)
{}
/* ARGSUSED */
void trace_5(u_long, u_long, u_long, u_long, u_long, u_long)
{}
/* ARGSUSED */
void trace_write_buffer(u_long *, u_long)
{}

#else	/* lint */

	ENTRY_NP(trace_0)
	ta	ST_TRACE_0; retl; nop;
	SET_SIZE(trace_0)

	ENTRY_NP(trace_1)
	ta	ST_TRACE_1; retl; nop;
	SET_SIZE(trace_1)

	ENTRY_NP(trace_2)
	ta	ST_TRACE_2; retl; nop;
	SET_SIZE(trace_2)

	ENTRY_NP(trace_3)
	ta	ST_TRACE_3; retl; nop;
	SET_SIZE(trace_3)

	ENTRY_NP(trace_4)
	ta	ST_TRACE_4; retl; nop;
	SET_SIZE(trace_4)

	ENTRY_NP(trace_5)
	ta	ST_TRACE_5; retl; nop;
	SET_SIZE(trace_5)

	ENTRY_NP(trace_write_buffer)
	ta	ST_TRACE_WRITE_BUFFER; retl; nop;
	SET_SIZE(trace_write_buffer)

/*
 * Fast traps to dump trace records.  Each uses only the trap window,
 * and leaves traps disabled.  They're all very similar, so we use
 * macros to generate the code -- see sparc/sys/asm_linkage.h.
 * All trace traps are entered with %i0 = FTT2HEAD (fac, tag, event_info).
 */

	ENTRY_NP(trace_trap_0)
	CPU_ADDR(%l3, %l4);
	TRACE_DUMP_0(%i0, %l3, %l4, %l5, %l6);
	jmp	%l2
	rett	%l2+4
	SET_SIZE(trace_trap_0)

	ENTRY_NP(trace_trap_1)
	CPU_ADDR(%l3, %l4);
	TRACE_DUMP_1(%i0, %i1, %l3, %l4, %l5, %l6);
	jmp	%l2
	rett	%l2+4
	SET_SIZE(trace_trap_1)

	ENTRY_NP(trace_trap_2)
	CPU_ADDR(%l3, %l4);
	TRACE_DUMP_2(%i0, %i1, %i2, %l3, %l4, %l5, %l6);
	jmp	%l2
	rett	%l2+4
	SET_SIZE(trace_trap_2)

	ENTRY_NP(trace_trap_3)
	CPU_ADDR(%l3, %l4);
	TRACE_DUMP_3(%i0, %i1, %i2, %i3, %l3, %l4, %l5, %l6);
	jmp	%l2
	rett	%l2+4
	SET_SIZE(trace_trap_3)

	ENTRY_NP(trace_trap_4)
	CPU_ADDR(%l3, %l4);
	TRACE_DUMP_4(%i0, %i1, %i2, %i3, %i4, %l3, %l4, %l5, %l6);
	jmp	%l2
	rett	%l2+4
	SET_SIZE(trace_trap_4)

	ENTRY_NP(trace_trap_5)
	CPU_ADDR(%l3, %l4);
	TRACE_DUMP_5(%i0, %i1, %i2, %i3, %i4, %i5, %l3, %l4, %l5, %l6);
	jmp	%l2
	rett	%l2+4
	SET_SIZE(trace_trap_5)

	ENTRY_NP(trace_trap_write_buffer)
	CPU_ADDR(%l3, %l4);			! %l3 = CPU address
	andncc	%i1, 3, %l6			! %l6 = # of bytes to copy
	bz	9f				! no data, return from trap
	ld	[%l3 + CPU_TRACE_THREAD], %l5	! last thread id
	tst	%l5				! NULL thread pointer?
	bz	9f				! if NULL, bail out
	ld	[%l3 + CPU_TRACE_HEAD], %l5	! %l5 = buffer head address
	add	%l5, %l6, %l4			! %l4 = new buffer head
1:	subcc	%l6, 4, %l6
	ld	[%i0+%l6], %l7
	bg	1b
	st	%l7, [%l5+%l6]
	TRACE_DUMP_TAIL(%l3, %l4, %l5, %l6);
9:	jmp	%l2
	rett	%l2+4
	SET_SIZE(trace_trap_write_buffer)

#endif	/* lint */

#endif	/* TRACE */

	

/*
 * Kernel probes assembly routines
 */

#if defined(lint)

/* ARGSUSED */
int tnfw_b_get_lock(u_char *lp)
{ return (0); }
/* ARGSUSED */
void tnfw_b_clear_lock(u_char *lp)
{}
/* ARGSUSED */
u_long tnfw_b_atomic_swap(u_long *wp, u_long x)
{ return (0); }

#else  /* lint */

	ENTRY(tnfw_b_get_lock)
	ldstub	[%o0], %o1
	jmpl	%o7+8, %g0
	mov	%o1, %o0
	SET_SIZE(tnfw_b_get_lock)

	ENTRY(tnfw_b_clear_lock)
	jmpl	%o7+8, %g0
	stb	%g0, [%o0]
	SET_SIZE(tnfw_b_clear_lock)

	ENTRY(tnfw_b_atomic_swap)
	swap	[%o0], %o1
	jmpl	%o7+8, %g0
	mov	%o1, %o0
	SET_SIZE(tnfw_b_atomic_swap)

#endif /* lint */

/*
 * Fetch user (long) word.
 *
 * int
 * fuword(addr)
 *	int *addr;
 */

#if defined(lint)

/* ARGSUSED */
int
fuword(int *addr)
{ return(0); }

/* ARGSUSED */
int
fuiword(int *addr)
{ return(0); }

#else	/* lint */

	ENTRY(fuiword)
	b	1f			! doing instruction fetch
	mov	ASI_UI, %o1
	ENTRY(fuword)
	mov	ASI_UD, %o1		! doing data fetch
1:
	sethi	%hi(KERNELBASE), %o3	! compare access addr to KERNELBASE
	cmp	%o0, %o3		! if (KERNELBASE >= addr) error
	bgeu	.fsuerr
	btst	0x3, %o0		! test alignment
	bne	.fsuerr
	.empty
	set	.fsuerr, %o3		! set t_lofault to catch any fault
	st	%o3, [THREAD_REG + T_LOFAULT]
	cmp     %o1, ASI_UI             ! which ASI should I use
	beq,a   1f
	lda     [%o0]ASI_UI, %o0        ! get the word on ASI_UI
	ld	[%o0], %o0		! get the word
1:
	retl
	clr	[THREAD_REG + T_LOFAULT]	! clear t_lofault
	SET_SIZE(fuword)
	SET_SIZE(fuiword)

#endif	/* lint */

/*
 * Fetch user byte.
 *
 * int
 * fubyte(addr)
 *	caddr_t addr;
 */

#if defined(lint)

/* ARGSUSED */
int
fubyte(caddr_t addr)
{ return(0); }

/* ARGSUSED */
int
fuibyte(caddr_t addr)
{ return(0); }

#else	/* lint */

	ENTRY(fuibyte)
	b       1f
	mov     ASI_UI, %o1             ! doing instruction fetch
	ENTRY(fubyte)
	mov     ASI_UD, %o1             ! doing data fetch
1:
	sethi	%hi(KERNELBASE), %o3	! compare access addr to KERNELBASE
	cmp	%o0, %o3		! if (KERNELBASE >= addr) error
	bgeu	.fsuerr
	.empty
	set	.fsuerr, %o3		! set t_lofault to catch any fault
	st	%o3, [THREAD_REG + T_LOFAULT]
	cmp     %o1, ASI_UI             ! which ASI should I use
	beq,a   1f
	lduba   [%o0]ASI_UI, %o0        ! get the byte on ASI_UI
	ldub	[%o0], %o0		! get the byte
1:
	retl
	clr	[THREAD_REG + T_LOFAULT]	! clear t_lofault
	SET_SIZE(fubyte)
	SET_SIZE(fuibyte)

#endif	/* lint */

/*
 * Set user (long) word.
 *
 * int
 * suword(addr, value)
 *	int *addr;
 *	int value;
 */

#if defined(lint)

/* ARGSUSED */
int
suword(int *addr, int value)
{ return(0); }

/* ARGSUSED */
int
suiword(int *addr, int value)
{ return(0); }

#else	/* lint */

	ENTRY2(suword,suiword)
	sethi	%hi(KERNELBASE), %o3	! compare access addr to KERNELBASE
	cmp	%o0, %o3		! if (KERNELBASE >= addr) error
	bgeu	.fsuerr
	btst	0x3, %o0		! test alignment
	bne	.fsuerr
	.empty
	set	.fsuerr, %o3		! set t_lofault to catch any fault
	st	%o3, [THREAD_REG + T_LOFAULT]
	b	.suret
	st	%o1, [%o0]		! set the word
	SET_SIZE(suword)
	SET_SIZE(suiword)

#endif	/* lint */

/*
 * Set user byte.
 *
 * int
 * subyte(addr, value)
 *	caddr_t addr;
 *	char value;
 */

#if defined(lint)

/* ARGSUSED */
int
subyte(caddr_t addr, char value)
{ return(0); }

/* ARGSUSED */
int
suibyte(caddr_t addr, char value)
{ return(0); }

#else	/* lint */

	ENTRY2(subyte,suibyte)
	sethi	%hi(KERNELBASE), %o3	! compare access addr to KERNELBASE
	cmp	%o0, %o3		! if (KERNELBASE >= addr) error
	bgeu	.fsuerr
	.empty
	set	.fsuerr, %o3		! set t_lofault to catch any fault
	st	%o3, [THREAD_REG + T_LOFAULT]
	stb	%o1, [%o0]		! set the byte
.suret:
	mov	0, %o0			! indicate success
	retl
	clr	[THREAD_REG + T_LOFAULT]	! clear t_lofault
	SET_SIZE(subyte)
	SET_SIZE(suibyte)

#endif	/* lint */

/*
 * Fetch user short (half) word.
 *
 * int
 * fusword(addr)
 *	caddr_t addr;
 */

#if defined(lint)

/* ARGSUSED */
int
fusword(caddr_t addr)
{ return(0); }

#else	/* lint */

	ENTRY(fusword)
	sethi	%hi(KERNELBASE), %o3	! compare access addr to KERNELBASE
	cmp	%o0, %o3		! if (KERNELBASE >= addr) error
	bgeu	.fsuerr
	btst	0x1, %o0		! test alignment
	bne	.fsuerr
	.empty
	set	.fsuerr, %o3		! set t_lofault to catch any fault
	st	%o3, [THREAD_REG + T_LOFAULT]
	lduh	[%o0], %o0		! get the half word
	retl
	clr	[THREAD_REG + T_LOFAULT]	! clear t_lofault
	SET_SIZE(fusword)

#endif	/* lint */

/*
 * Set user short word.
 *
 * int
 * susword(addr, value)
 *	caddr_t addr;
 *	int value;
 */

#if defined(lint)

/* ARGSUSED */
int
susword(caddr_t addr, int value)
{ return(0); }

#else	/* lint */

	ENTRY(susword)
	sethi	%hi(KERNELBASE), %o3	! compare access addr to KERNELBASE
	cmp	%o0, %o3		! if (KERNELBASE >= addr) error
	bgeu	.fsuerr
	btst	0x1, %o0		! test alignment
	bne	.fsuerr
	.empty
	set	.fsuerr, %o3		! set t_lofault to catch any fault
	st	%o3, [THREAD_REG + T_LOFAULT]
	b	.suret
	sth	%o1, [%o0]		! set the half word

.fsuerr:
	mov	-1, %o0			! return error
	retl
	clr	[THREAD_REG + T_LOFAULT]	! clear t_lofault
	SET_SIZE(susword)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
int
fuword_noerr(int *addr)
{ return(0); }


/* ARGSUSED */
int
fubyte_noerr(caddr_t addr)
{ return(0); }


/* ARGSUSED */
int
fusword_noerr(caddr_t addr)
{ return(0); }


#else	/* lint */

	ENTRY(fuword_noerr)
#ifdef DEBUG
	set	KERNELBASE, %o1
	cmp	%o0, %o1
	blu	1f
	nop

	mov	%o0, %o1		! save it for debugging purposes
	set	2f, %o0
	call	panic
	nop
	! no return

2:
	.asciz	"fuword_noerr:	 Address is above KERNELBASE "
	.align	4
1:
#endif DEBUG
	retl
	ld	[%o0], %o0
	SET_SIZE(fuword_noerr)

	ENTRY(fusword_noerr)
#ifdef DEBUG
	set	KERNELBASE, %o1
	cmp	%o0, %o1
	blu	1f
	nop

	mov	%o0, %o1		! save it for debugging purposes
	set	2f, %o0
	call	panic
	nop
	! no return

2:
	.asciz	"fusword_noerr:	 Address is above KERNELBASE "
	.align	4
1:
#endif DEBUG
	retl
	lduh	[%o0], %o0
	SET_SIZE(fusword_noerr)

	ENTRY(fubyte_noerr)
#ifdef DEBUG
	set	KERNELBASE, %o1
	cmp	%o0, %o1
	blu	1f
	nop

	mov	%o0, %o1		! save it for debugging purposes
	set	2f, %o0
	call	panic
	nop
	! no return

2:
	.asciz	"fubyte_noerr:	 Address is above KERNELBASE "
	.align	4
1:
#endif DEBUG
	retl
	ldub	[%o0], %o0
	SET_SIZE(fubyte_noerr)

#endif lint

#if defined(lint)

/* ARGSUSED */
int
suword_noerr(int *addr, int value)
{ return(0); }

/* ARGSUSED */
int
susword_noerr(int *addr, int value)
{ return(0); }

/* ARGSUSED */
int
subyte_noerr(caddr_t addr, char value)
{ return(0); }


#else

	ENTRY(suword_noerr)
#ifdef DEBUG
	set	KERNELBASE, %o2
	cmp	%o0, %o2
	blu	1f
	nop

	mov	%o0, %o2		! save it for debugging purposes
	set	2f, %o0
	call	panic
	nop
	! no return

2:
	.asciz	"suword_noerr:	 Address is above KERNELBASE "
	.align	4
1:
#endif DEBUG
	retl
	st	%o1, [%o0]
	SET_SIZE(suword_noerr)

	ENTRY(susword_noerr)
#ifdef DEBUG
	set	KERNELBASE, %o2
	cmp	%o0, %o2
	blu	1f
	nop

	mov	%o0, %o2		! save it for debugging purposes
	set	2f, %o0
	call	panic
	nop
	! no return

2:
	.asciz	"susword_noerr:	 Address is above KERNELBASE "
	.align	4
1:
#endif DEBUG
	retl
	sth	%o1, [%o0]
	SET_SIZE(susword_noerr)

	ENTRY(subyte_noerr)
#ifdef DEBUG
	set	KERNELBASE, %o2
	cmp	%o0, %o2
	blu	1f
	nop

	mov	%o0, %o2		! save it for debugging purposes
	set	2f, %o0
	call	panic
	nop
	! no return

2:
	.asciz	"subyte_noerr:	 Address is above KERNELBASE "
	.align	4
1:
#endif DEBUG
	retl
	stb	%o1, [%o0]
	SET_SIZE(subyte_noerr)

#endif lint


