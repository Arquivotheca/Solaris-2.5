/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)sparcv9_subr.s	1.48	95/08/02 SMI"

/*
 * General assembly language routines.
 * It is the intent of this file to contain routines that are
 * independent of the specific kernel architecture, and those that are
 * common across kernel architectures.
 * As architectures diverge, and implementations of specific
 * architecture-dependent routines change, the routines should be moved
 * from this file into the respective ../`arch -k`/subr.s file.
 * Or, if you want to be really nice, move them to a file whose
 * name has something to do with the routine you are moving.
 */

#if defined(lint)
#include <sys/types.h>
#include <sys/scb.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>	/* BUG: splzs is -platform- specific */
#define	SUNDDI_IMPL		/* prevent spltty -> i_ddi_spltty etc. */
#include <sys/sunddi.h>
#endif	/* lint */

#include <sys/atomic_prim.h>
#include <sys/asm_linkage.h>
#include <sys/privregs.h>
#include <sys/machparam.h>	/* To get SYSBASE and PAGESIZE */
#include <sys/machthread.h>
#include <sys/clock.h>
#include <sys/psr_compat.h>

#if !defined(lint)
#include "assym.s"

	.seg	".text"
	.align	4

/*
 * Macro to raise processor priority level.
 * Avoid dropping processor priority if already at high level.
 * Also avoid going below CPU->cpu_base_spl, which could've just been set by
 * a higher-level interrupt thread that just blocked.
 *
 * level can be %o0 (not other regs used here) or a constant.
 */
#define	RAISE(level) \
	rdpr	%pil, %o1;		/* get current PIL */		\
	cmp	%o1, level;		/* is PIL high enough? */	\
	bge	1f;			/* yes, return */		\
	nop;								\
	wrpr	%g0, PIL_MAX, %pil;	/* freeze CPU_BASE_SPL */	\
	ld	[THREAD_REG + T_CPU], %o2;				\
	ld	[%o2 + CPU_BASE_SPL], %o2;				\
	cmp	%o2, level;		/* compare new to base */	\
	movl	%xcc, level, %o2;	/* use new if base lower */	\
	wrpr	%g0, %o2, %pil;						\
1:									\
	retl;								\
	mov	%o1, %o0		/* return old PIL */

/*
 * Macro to raise processor priority level to level >= LOCK_LEVEL..
 * Doesn't require comparison to CPU->cpu_base_spl.
 *
 * newpil can be %o0 (not other regs used here) or a constant.
 */
#define	RAISE_HIGH(level) \
	rdpr	%pil, %o1;		/* get current PIL */		\
	cmp	%o1, level;		/* is PIL high enough? */	\
	bge	1f;			/* yes, return */		\
	nop;								\
	wrpr	%g0, level, %pil;	/* use chose value */		\
1:									\
	retl;								\
	mov	%o1, %o0		/* return old PIL */
	
/*
 * Macro to set the priority to a specified level.
 * Avoid dropping the priority below CPU->cpu_base_spl.
 *
 * newpil can be %o0 (not other regs used here) or a constant with
 * the new PIL in the PSR_PIL field of the level arg.
 */
#define SETPRI(level) \
	rdpr	%pil, %o1;		/* get current PIL */		\
	wrpr	%g0, PIL_MAX, %pil;	/* freeze CPU_BASE_SPL */	\
	ld	[THREAD_REG + T_CPU], %o2;				\
	ld	[%o2 + CPU_BASE_SPL], %o2;				\
	cmp	%o2, level;		/* compare new to base */	\
	movl	%xcc, level, %o2;	/* use new if base lower */	\
	wrpr	%g0, %o2, %pil;						\
	retl;								\
	mov	%o1, %o0		/* return old PIL */

/*
 * Macro to set the priority to a specified level at or above LOCK_LEVEL.
 * Doesn't require comparison to CPU->cpu_base_spl.
 *
 * newpil can be %o0 (not other regs used here) or a constant with
 * the new PIL in the PSR_PIL field of the level arg.
 */
#define	SETPRI_HIGH(level) \
	rdpr	%pil, %o1;		/* get current PIL */		\
	wrpr	%g0, level, %pil;					\
	retl;								\
	mov	%o1, %o0		/* return old PIL */

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
	 * SPARC9 IPL 10 is the highest level from which a device
	 * routine can call wakeup.  Devices that interrupt from higher
	 * levels are restricted in what they can do.  If they need
	 * kernels services they should schedule a routine at a lower
	 * level (via software interrupt) to do the required
	 * processing.
	 *
	 * Examples of this higher usage:
	 *	Level	Usage
	 *	15	Asynchronous memory exceptions
	 *	14	Profiling clock (and PROM uart polling clock)
	 *	13	Audio device
	 *	12	Serial ports
	 *	11	Floppy controller
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
	ALTENTRY(splaudio)
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
	RAISE_HIGH(10)
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
 * splx - set PIL back to that indicated by the old %pil passed as an argument,
 * or to the CPU's base priority, whichever is higher.
 */

#if defined(lint)

/* ARGSUSED */
int
splx(int level)
{ return (0); }

#else	/* lint */

	ENTRY(splx)
	SETPRI(%o0)		/* set PIL */
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
	ENTRY(splr)
	RAISE(%o0)
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
	rdpr	%pil, %o1			! get current PIL
	ld	[THREAD_REG + T_CPU], %o2	! get CPU pointer
	ld	[%o2 + CPU_BASE_SPL], %o2
	cmp	%o2, %o0			! compare new to base
	movl	%xcc, %o0, %o2			! use new pri if base is less
	wrpr	%g0, %o2, %pil
	retl
	mov	%o1, %o0			! return old PIL
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
        ! window is flushed.  Flushw flushes nwindows-2
        ! register windows.  If setjmp and longjmp are called from
        ! within the same window, that window will not get pushed
        ! out onto the stack without the extra save below.  Tail call
        ! optimization can lead to callers of longjmp executing
        ! from a window that could be the same as the setjmp,
        ! thus the need for the following save.
        !
	save    %sp, -SA(MINFRAME), %sp
	flushw				! flush all but this window
	ld	[%i0 + L_PC], %i7	! restore return addr
	ld	[%i0 + L_SP], %fp	! restore sp for dest on foreign stack
	ret				! return 1
	restore	%g0, 1, %o0		! takes underflow, switches stacks
	SET_SIZE(longjmp)

#endif	/* lint */

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
 * Get processor state register, V9 faked to look like V8.
 * Note: does not provide ccr.xcc and provides FPRS.FEF instead of
 * PSTATE.PEF, because PSTATE.PEF is always on in order to allow the
 * libc_psr memcpy routines to run without hitting the fp_disabled trap.
 */

#if defined(lint)

greg_t
getpsr(void)
{ return (0); }

#else	/* lint */

	ENTRY(getpsr)
	rd	%ccr, %o1			! get ccr
        sll	%o1, PSR_ICC_SHIFT, %o0		! move icc to V8 psr.icc
	rd	%fprs, %o1			! get fprs
	and	%o1, FPRS_FEF, %o1		! mask out dirty upper/lower
	sllx	%o1, PSR_FPRS_FEF_SHIFT, %o1	! shift fef to V8 psr.ef
        or	%o0, %o1, %o0			! or into psr.ef
        set	V9_PSR_IMPLVER, %o1		! SI assigned impl/ver: 0xef
        retl
        or	%o0, %o1, %o0			! or into psr.impl/ver
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
	retl
	rdpr	%pil, %o0
	SET_SIZE(getpil)

#endif	/* lint */

#if defined(lint)

/*ARGSUSED*/
void
setpil(greg_t pil)
{}

#else	/* lint */

	ENTRY(setpil)
	retl
	wrpr	%g0, %o0, %pil
	SET_SIZE(setpil)

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
 * Returns the number of non-NULL bytes in string argument.
 *
 * ustrlen is to accesss user address space. It assumes it is being called
 * in the context on an onfault setjmp.
 *
 * XXX -  why is this here, rather than the traditional file?
 *	  why does it have local labels which don't start with a `.'?
 */

#if defined(lint)

/*ARGSUSED*/
size_t
strlen(const char *str)
{ return (0); }

/*ARGSUSED*/
size_t
ustrlen(const char *str)
{ return (0); }

#else	/* lint */

	ENTRY(ustrlen)
	ba	.genstrlen
	wr	%g0, ASI_USER, %asi
	
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
	wr	%g0, ASI_P, %asi
.genstrlen:
	mov	%o0, %o1
	andcc	%o1, 3, %o3		! is src word aligned
	bz	$nowalgnd
	clr	%o0			! length of non-zero bytes
	cmp	%o3, 2			! is src half-word aligned
	be	$s2algn
	cmp	%o3, 3			! src is byte aligned
	lduba	[%o1]%asi, %o3		! move 1 or 3 bytes to align it
	inc	1, %o1			! in either case, safe to do a byte
	be	$s3algn
	tst	%o3
$s1algn:
	bnz,a	$s2algn			! now go align dest
	inc	1, %o0
	b,a	$done

$s2algn:
	lduha	[%o1]%asi, %o3		! know src is half-byte aligned
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

3:	lda	[%o1]%asi, %o2		! main loop
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
	SET_SIZE(ustrlen)

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
copyinstr_noerr(char *s1, char *s2, size_t *len)
{ return ((char *)0); }

/*ARGSUSED*/
char *
copyoutstr_noerr(char *s1, char *s2, size_t *len)
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

	ba,pt	%icc, .do_cpy
	  wr	%g0, ASI_USER, %asi


	ENTRY(copyinstr_noerr)

	ba,pt	%icc, .do_cpy
	  wr	%g0, ASI_USER, %asi

	ALTENTRY(knstrcpy)
	wr	%g0, ASI_P, %asi

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
	lduba	[SRC]%asi, %i3		! move 1 or 3 bytes to align it
	inc	1, SRC
	stb	%i3, [DEST]		! move a byte to align src
	be	.s3algn
	tst	%i3
.s1algn:
	bnz	.s2algn			! now go align dest
	inc	1, DEST
	b,a	.done
.s2algn:
	lduha	[SRC]%asi, %i3		! know src is 2 byte alinged
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
	lda	[SRC]%asi, %i3		! read a word
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
	lda	[SRC]%asi, %o3		! read a word
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
	addcc	%i3, ADDMSK, %l5	! check for a zero byte
	bcc	1f			! there must be one
	xor	%l5, %i3, %l5
	and	%l5, ANDMSK, %l5
	cmp	%l5, ANDMSK
	be,a	2b			! if no zero byte in word, loop
	st	%i3, [DEST]

1:
	or	%i3, %o2, %i3
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
	lda	[SRC]%asi, %i3		! read a word
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
	lda	[SRC]%asi, %o3		! read a word
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
	lda	[SRC]%asi, %i3		! read a word
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
	GET_HRTIME(%o0, %o1, %g5, %g1)		! get time into %o0:%o1
	retl
	nop
	SET_SIZE(gethrtime)

/*
 * Fast trap to return a timestamp, uses trap window, leaves traps
 * disabled.  Returns a 64-bit nanosecond timestamp in %o0 and %o1.
 */

	ENTRY_NP(get_timestamp)
	GET_HRTIME(%o0, %o1, %g5, %g1)		! get time into %o0:%o1
	done
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
 *	%g4 = nanosecs since last tick
 *	%o5 = scratch
 *	%o2 = scratch
 *	%g5 = hrestime_adj
 *	%g1 = hrestime
 */

	ENTRY_NP(get_hrestime)
	GET_HRESTIME(%g4, %g3, %g2, %g5, %g1) 	! %g4 = nsec_since_last_tick
	brz,pt	%g5, 3f
	add	%g1, %g4, %g1			! hrestime.tv_nsec + nslt
	brlz,pn	%g5, 2f				! if hrestime_adj is negative
	srl	%g4, ADJ_SHIFT, %g2		! %g2 = nslt_div_16 = nslt >> 4
	subcc	%g5, %g2, %g0			! hrestime_adj - nslt_div_16
	movl	%xcc, %g5, %g2
	ba	3f
	add	%g1, %g2, %g1
2:
	addcc	%g5, %g2, %g0			! hrestime_adj + nslt_div_16
	bge,a,pt %xcc, 3f			! if hrestime_adj is smaller neg
	add	%g1, %g5, %g1			! hrestime.nsec += hrestime_adj
	sub	%g1, %g2, %g1			! hrestime.nsec -= nslt_div_16
3:
	set	NANOSEC, %g4
	srl	%g1, 0, %o1
	cmp	%o1, %g4
	bl,pt	%xcc, 4f			! if hrestime.tv_nsec < NANOSEC
	srlx	%g1, 32, %o0
	add	%o0, 0x1, %o0			! hrestime.tv_sec++
	sub	%o1, %g4, %o1			! hrestime.tv_nsec - NANOSEC
4:
	done
	SET_SIZE(get_hrestime)
/*
 * Fast trap to return lwp virtual time, uses trap window, leaves traps
 * disabled.  Returns a 64-bit number in %o0, which is the number
 * of nanoseconds consumed.
 * Register usage:
 *	%o0 = return lwp virtual time
 * 	%o2 = CPU/thread
 * 	%o3 = lwp
 * 	%g1 = scratch
 * 	%g5 = scratch
 */
	ENTRY_NP(get_virtime)
	GET_NSEC(%g5, %g1)			! get time into %g5
	CPU_ADDR(%g2, %g3)			! CPU struct ptr to %g2
	ld	[%g2 + CPU_THREAD], %g2		! thread pointer to %g2
	lduh	[%g2 + T_PROC_FLAG], %g3
	btst	TP_MSACCT, %g3			! test for MSACCT on
	bz	1f				! not on - do estimate
	ld	[%g2 + T_LWP], %g3		! lwp pointer to %g3

	/*
	 * Subtract start time of current microstate from time
	 * of day to get increment for lwp virtual time.
	 */
	ldx	[%g3 + LWP_STATE_START], %g1	! ms_state_start
	sub	%g5, %g1, %g5

	/*
	 * Add current value of ms_acct[LMS_USER]
	 */
	ldx	[%g3 + LWP_ACCT_USER], %g1	! ms_acct[LMS_USER]
	add	%g5, %g1, %g5

	srl	%g5, 0, %o1
	srlx	%g5, 32, %o0

	done

	/*
	 * Microstate accounting times are not available.
	 * Estimate based on tick samples.
	 * Convert from ticks (100 Hz) to nanoseconds (mult by 10,000,000).
	 */
1:
	ld	[%g3 + LWP_UTIME], %g4		! utime = lwp->lwp_utime;
	set	10000000, %g1			! multiplier
	mulx	%g4, %g1, %g1

	/*
	 * Sanity-check estimate.
	 * If elapsed real time for process is smaller, use that instead.
	 */
	ldx	[%g3 + LWP_MS_START], %g4	! starting real time for LWP
	sub	%g5, %g4, %g5			! subtract start time
	cmp	%g1, %g5			! compare to estimate
	movleu	%xcc, %g1, %g5			! use estimate

	srl	%g5, 0, %o1
	srlx	%g5, 32, %o0

	done
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
 * macros to generate the code -- see sparc9/sys/asm_linkage.h.
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
 * Provide a C callable interface to the membar instruction.
 */

#if defined(lint)

void
membar_ldld()
{}

void
membar_stld()
{}

void
membar_ldst()
{}

void
membar_stst()
{}

void
membar_ldld_ldst()
{}

void
membar_ldld_stld()
{}

void
membar_ldld_stst()
{}


void
membar_stld_ldld()
{}

void
membar_stld_ldst()
{}

void
membar_stld_stst()
{}

void
membar_ldst_ldld()
{}

void
membar_ldst_stld()
{}

void
membar_ldst_stst()
{}

void
membar_stst_ldld()
{}

void
membar_stst_stld()
{}

void
membar_stst_ldst()
{}

void
membar_lookaside()
{}

void
membar_memissue()
{}

void
membar_sync()
{}

#else
	ENTRY(membar_ldld)
	retl
	membar	#LoadLoad
	SET_SIZE(membar_ldld)

	ENTRY(membar_stld)
	retl
	membar	#StoreLoad
	SET_SIZE(membar_stld)

	ENTRY(membar_ldst)
	retl
	membar	#LoadStore
	SET_SIZE(membar_ldst)

	ENTRY(membar_stst)
	retl
	membar	#StoreStore
	SET_SIZE(membar_stst)

	ENTRY(membar_ldld_stld)
	ALTENTRY(membar_stld_ldld)
	retl
	membar	#LoadLoad|#StoreLoad
	SET_SIZE(membar_stld_ldld)
	SET_SIZE(membar_ldld_stld)

	ENTRY(membar_ldld_ldst)
	ALTENTRY(membar_ldst_ldld)
	retl
	membar	#LoadLoad|#LoadStore
	SET_SIZE(membar_ldst_ldld)
	SET_SIZE(membar_ldld_ldst)

	ENTRY(membar_ldld_stst)
	ALTENTRY(membar_stst_ldld)
	retl
	membar	#LoadLoad|#StoreStore
	SET_SIZE(membar_stst_ldld)
	SET_SIZE(membar_ldld_stst)

	ENTRY(membar_stld_ldst)
	ALTENTRY(membar_ldst_stld)
	retl
	membar	#StoreLoad|#LoadStore
	SET_SIZE(membar_ldst_stld)
	SET_SIZE(membar_stld_ldst)

	ENTRY(membar_stld_stst)
	ALTENTRY(membar_stst_stld)
	retl
	membar	#StoreLoad|#StoreStore
	SET_SIZE(membar_stst_stld)
	SET_SIZE(membar_stld_stst)

	ENTRY(membar_ldst_stst)
	ALTENTRY(membar_stst_ldst)
	retl
	membar	#LoadStore|#StoreStore
	SET_SIZE(membar_stst_ldst)
	SET_SIZE(membar_ldst_stst)

	ENTRY(membar_lookaside)
	retl
	membar	#Lookaside
	SET_SIZE(membar_lookaside)

	ENTRY(membar_memissue)
	retl
	membar	#MemIssue
	SET_SIZE(membar_memissue)

	ENTRY(membar_sync)
	retl
	membar	#Sync
	SET_SIZE(membar_sync)

#endif	/* lint */

/*
 * Primitives for atomic increments/decrements of a counter.
 * all routines expect an address and a positive or negative count.
 * all routines assume that the the result of the operation is always
 * positive and that overflow should not occur.  ifdef
 * DEBUG panics incorporate checks.
 *
 * atadd_ext(u_longlong_t *, int cnt)
 *	atomically adds cnt (positive or negative) to the extended word pointed
 *	to by addr.
 * atadd_word(u_int *, int cnt)
 *	atomically adds cnt (positive or negative) to the word pointed to
 *	by addr.
 * atadd_hword(u_short *, int cnt)
 *	atomically adds cnt (positive or negative) to the half word pointed to
 *	by addr.
 */

#ifdef lint

/* ARGSUSED */
void
atadd_ext(u_longlong_t *arg1, int arg2)
{}

/* ARGSUSED */
void
atadd_word(u_int *arg1, int arg2)
{}

/* ARGSUSED */
void
atadd_hword(u_short *arg1, int arg2)
{}

#else /* lint */

/*
 * panic strings for DEBUG kernel
 */
#ifdef DEBUG
	.seg	".data"
atadd_panic1:
	.ascii	"atadd: result regative"
	.byte	0
atadd_panic2:
	.ascii	"atadd: result overflowed"
	.byte	0
	.align 4
	.seg	".text"
#endif /* DEBUG */

	ENTRY(atadd_ext)
	ldx	[%o0], %g2
1:
	addcc	%g2, %o1, %g3
#ifdef DEBUG
	bvc,a,pt %xcc, 2f
	  nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(atadd_panic2), %o0
	call	panic
	  or	%o0, %lo(atadd_panic2), %o0
2:
	brgez,a,pt %g3, 3f
	  nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(atadd_panic1), %o0
	call	panic
	  or	%o0, %lo(atadd_panic1), %o0
3:
#endif /* DEBUG */
#ifndef	ATOMICS_WORK
	membar	#Sync
#endif
	casx	[%o0], %g2, %g3
	cmp	%g2, %g3
	bne,a,pn %xcc, 1b
	  ldx	[%o0], %g2
	retl
	nop
	SET_SIZE(atadd_ext)

	ENTRY(atadd_word)
	ld	[%o0], %o2
1:
	addcc	%o2, %o1, %o3
#ifdef DEBUG
	bvc,a,pt %icc, 2f
	  nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(atadd_panic2), %o0
	call	panic
	  or	%o0, %lo(atadd_panic2), %o0
2:
	cmp	%o3, %g0
	bge,a,pt %o3, 3f
	  nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(atadd_panic1), %o0
	call	panic
	  or	%o0, %lo(atadd_panic1), %o0
3:
#endif /* DEBUG */
#ifndef	ATOMICS_WORK
	membar	#Sync
#endif
	cas	[%o0], %o2, %o3
	cmp	%o2, %o3
	bne,a,pn %icc, 1b
	  ld	[%o0], %o2
	retl
	nop
	SET_SIZE(atadd_word)

/*
 * atomic inc/dec of a halfword is a pain because cas does not support
 * a halfword.  We load the halfword besides the other halfword and
 * we shift the odd one to come up with a word quantity.
 */
	ENTRY(atadd_hword)
	xor	%o0, 2, %o4
	lduh	[%o0], %o2
1:
	lduh	[%o4], %o3
	addcc	%o2, %o1, %o5
#ifdef DEBUG
	bvc,a,pt %icc, 2f
	  nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(atadd_panic2), %o0
	call	panic
	  or	%o0, %lo(atadd_panic2), %o0
2:
	cmp	%o5, %g0
	bge,a,pt %icc, 3f
	  nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(atadd_panic1), %o0
	call	panic
	  or	%o0, %lo(atadd_panic1), %o0
3:
#endif /* DEBUG */
	btst	2, %o0
	bnz,a,pn %icc, 4f
	  sll	%o3, 16, %o3
/*
 * counter is higher halfword
 */
	sll	%o5, 16, %o5
	sll	%o2, 16, %o2
	or	%o5, %o3, %o5
	or	%o2, %o3, %g2
#ifndef	ATOMICS_WORK
	membar	#Sync
#endif
	cas	[%o0], %g2, %o5
	cmp	%g2, %o5
	bne,a,pn %icc, 1b
	  lduh	[%o0], %o2
	retl
	  nop
4:
/*
 * counter is lower halfword
 */
	or	%o5, %o3, %o5
	or	%o2, %o3, %g2
#ifndef	ATOMICS_WORK
	membar	#Sync
#endif
	cas	[%o4], %g2, %o5
	cmp	%g2, %o5
	bne,a,pn %icc, 1b
	  lduh	[%o0], %o2
	retl
	  nop
	SET_SIZE(atadd_hword)

#endif /* lint */

#ifdef lint
/*
 * These routines are for readers-writer lock where the lock is an u_int.
 * Note: Caller should not loop trying to grab the writer lock, since
 * these routines don't give any priority to a writer over readers.
 */

/* ARGSUSED */
void
rwlock_word_init(u_int *lock)
{
}

/* ARGSUSED */
int
rwlock_word_enter(u_int *lock, int flag)
{
return(0);
}

/* ARGSUSED */
void
rwlock_word_exit(u_int *lock, int flag)
{
}

/*
 * These routines are for readers-writer lock where the lock is a u_short.
 * Note: Caller should not loop trying to grab the writer lock, since
 * these routines don't give any priority to a writer over readers.
 */

/* ARGSUSED */
void
rwlock_hword_init(u_short *lock)
{
}

/* ARGSUSED */
int
rwlock_hword_enter(u_short *lock, int flag)
{
return(0);
}

/* ARGSUSED */
void
rwlock_hword_exit(u_short *lock, int flag)
{
}

#else /* lint */

#ifdef DEBUG
	.seg	".data"
rw_panic1:
	.ascii	"rwlock_hword_exit: write lock held"
	.byte	0
rw_panic2:
	.ascii	"rwlock_hword_exit: reader lock not held"
	.byte	0
rw_panic3:
	.ascii	"rwlock_hword_exit: write lock not held"
	.byte	0
#endif /* DEBUG */

	.seg	".data"
rw_panic4:
	.ascii	"rwlock_hword_enter: # of reader locks equal WLOCK"
	.byte	0
	.seg	".text"
	.align	4

	ENTRY(rwlock_word_init)
	st	%g0, [%o0]		/* set rwlock to 0 */
	retl
	nop
	SET_SIZE(rwlock_word_init)


	ENTRY(rwlock_word_enter)

	cmp	%o1, READER_LOCK
	bne	%icc, 4f
	ld	[%o0], %o3		/* o3 = rwlock */
	/*
	 * get reader lock
	 */
	set	WORD_WLOCK, %o5
0:
	cmp	%o3, %o5
	be,a,pn	%icc, 3f		/* return failure */
	sub	%g0, 1, %o0

	mov	%o3, %o1		/* o1 = old value */
	inc	%o3			/* o3 = new value */

	cmp	%o3, %o5		/* # of readers == WLOCK */
	bne,pt	%icc, 1f
	nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(rw_panic4), %o0
	call	panic
	or	%o0, %lo(rw_panic4), %o0

1:
	cas	[%o0], %o1, %o3
	cmp	%o1, %o3
	be,a	%icc, 3f		/* return success */
	mov	0, %o0

	ba,pt	%icc, 0b		/* try again */
	ld	[%o0], %o3

	/*
	 * get writer lock
	 */
2:
	cmp	%o3, %g0
	bne,a,pn %icc, 3f		/* return failure */
	sub	%g0, 1, %o0

	mov	%o3, %o1		/* o1 = old value */
	set	WORD_WLOCK, %o3		/* o3 = new value */

	cas	[%o0], %o1, %o3
	cmp	%o1, %o3
	be,a	%icc, 3f		/* return success */
	mov	0, %o0

	ba,pt	%icc, 2b		/* try again */
	ld	[%o0], %o3
		
	/*
	 * return status
	 */
3:
	retl
	nop
	SET_SIZE(rwlock_word_enter)


	ENTRY(rwlock_word_exit)

	cmp	%o1, READER_LOCK
	bne	%icc, 4f
	ld	[%o0], %o3		/* o3 = rwlock */
	/*
	 * exit reader lock
	 */
1:
#ifdef	DEBUG
	set	WORD_WLOCK, %o5
	cmp	%o3, %o5
	bne,a,pt %icc, 2f
	nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(rw_panic1), %o0
	call	panic
	or	%o0, %lo(rw_panic1), %o0
2:
	cmp	%o3, %g0		/* reader lock is held */
	bg,a,pt %icc, 3f
	nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(rw_panic2), %o0
	call	panic
	or	%o0, %lo(rw_panic2), %o0
3:
#endif	DEBUG

	mov	%o3, %o1		/* o1 = old value */
	dec	%o3			/* o3 = new value */

	cas	[%o0], %o1, %o3
	cmp	%o1, %o3
	be,a	%icc, 6f		/* return success */
	nop

	ba,pt	%icc, 1b		/* try again */
	ld	[%o0], %o3

	/*
	 * exit writer lock
	 */
4:
#ifdef	DEBUG
	set	WORD_WLOCK, %o5
	cmp	%o3, %o5
	be,a,pt %icc, 5f			/* return failure */
	nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(rw_panic3), %o0
	call	panic
	or	%o0, %lo(rw_panic3), %o0
5:
#endif	DEBUG

	mov	%o3, %o1		/* o1 = old value */
	mov	%g0, %o3		/* o3 = new value */

	cas	[%o0], %o1, %o3
	cmp	%o1, %o3
	be,a	%icc, 6f		/* return success */
	mov	0, %o0

	ba,pt	%icc, 4b		/* try again */
	ld	[%o0], %o3
		
6:
	retl
	nop
	SET_SIZE(rwlock_word_exit)



	ENTRY(rwlock_hword_init)
	sth	%g0, [%o0]		/* set rwlock to 0 */
	retl
	nop
	SET_SIZE(rwlock_hword_init)


	ENTRY(rwlock_hword_enter)

	xor	%o0, 2, %o4
	cmp	%o1, READER_LOCK
	bne	%icc, 4f
	lduh	[%o0], %o2		/* o2 = rwlock */
	/*
	 * get reader lock
	 */
0:
	lduh	[%o4], %o3		/* o3 = other half */

	set	HWORD_WLOCK, %o5
	cmp	%o2, %o5
	be,a,pn	%icc, 7f		/* return failure */
	sub	%g0, 1, %o0

	dec	%o5
	cmp	%o2, %o5		/* # of readers == WLOCK-1 */
	bne,pt	%icc, 1f
	nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(rw_panic4), %o0
	call	panic
	or	%o0, %lo(rw_panic4), %o0
1:
	btst	2, %o0			/* which halfword */
	bz	%icc,2f
	mov	%o2, %o5
/*
 * rwlock is lower halfword
 */
	inc	%o5
	sll	%o3, 16, %o3
	or	%o2, %o3, %o1		/* o1 = old value */
	ba,pt	%icc, 3f
	or	%o5, %o3, %o3		/* o3 = new value */

/*
 * rwlock is upper halfword
 */
2:
	inc	%o5
	sll	%o2, 16, %o2
	or	%o2, %o3, %o1		/* o1 = old value */
	sll	%o5, 16, %o5
	or	%o5, %o3, %o3		/* o3 = new value */

/*
 * Now compare and swap
 */
3:
	cas	[%o4], %o1, %o3
	cmp	%o1, %o3
	be,a	%icc, 7f		/* return success */
	mov	0, %o0

	ba,pt	%icc, 0b		/* try again */
	lduh	[%o0], %o2

	/*
	 * get writer lock
	 */
4:
	lduh	[%o4], %o3		/* o3 = other half */
	cmp	%o2, %g0
	bne,a,pn %icc, 7f		/* return failure */
	sub	%g0, 1, %o0

	set	HWORD_WLOCK, %o5
	btst	2, %o0			/* which halfword */
	bz,a	%icc,5f
	sll	%o2, 16, %o2
/*
 * rwlock is lower halfword
 */
	sll	%o3, 16, %o3
	or	%o2, %o3, %o1		/* o1 = old value */
	ba,pt	%icc, 6f
	or	%o5, %o3, %o3		/* o3 = new value */

/*
 * rwlock is upper halfword
 */
5:
	or	%o2, %o3, %o1		/* o1 = old value */
	sll	%o5, 16, %o5
	or	%o5, %o3, %o3		/* o3 = new value */
/*
 * Now compare and swap
 */
6:
	cas	[%o4], %o1, %o3
	cmp	%o1, %o3
	be,a	%icc, 7f		/* return success */
	mov	0, %o0

	ba,pt	%icc, 4b		/* try again */
	lduh	[%o0], %o2
		
	/*
	 * return status
	 */
7:
	retl
	nop
	SET_SIZE(rwlock_hword_enter)


	ENTRY(rwlock_hword_exit)

	xor	%o0, 2, %o4
	cmp	%o1, READER_LOCK
	bne	%icc, 6f
	lduh	[%o0], %o2		/* o2 = rwlock */
	/*
	 * exit reader lock
	 */
1:
	lduh	[%o4], %o3		/* o3 = other half */

#ifdef	DEBUG
	set	HWORD_WLOCK, %o5	/* check for writer lock */
	cmp	%o2, %o5
	bne,a,pt %icc, 2f
	nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(rw_panic1), %o0
	call	panic
	or	%o0, %lo(rw_panic1), %o0
2:
	cmp	%o2, %g0		/* check for reader lock */
	bg,a,pt %icc, 3f
	nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(rw_panic2), %o0
	call	panic
	or	%o0, %lo(rw_panic2), %o0
3:
#endif	DEBUG

	btst	2, %o0			/* which halfword */
	bz	%icc,4f
	mov	%o2, %o5
/*
 * rwlock is lower halfword
 */
	dec	%o5
	sll	%o3, 16, %o3
	or	%o2, %o3, %o1		/* o1 = old value */
	ba,pt	%icc, 5f
	or	%o5, %o3, %o3		/* o3 = new value */

/*
 * rwlock is upper halfword
 */
4:
	dec	%o5
	sll	%o2, 16, %o2
	or	%o2, %o3, %o1		/* o1 = old value */
	sll	%o5, 16, %o5
	or	%o5, %o3, %o3		/* o3 = new value */

/*
 * Now compare and swap
 */
5:
	cas	[%o4], %o1, %o3
	cmp	%o1, %o3
	be,a	%icc, 0f		/* return success */
	nop

	ba,pt	%icc, 1b		/* try again */
	lduh	[%o0], %o2

	/*
	 * exit writer lock
	 */
6:
	lduh	[%o4], %o3		/* o3 = other half */
	andn	%o0, 3, %o5
	ld	[%o5], %o1		/* o1 = old value */
#ifdef	DEBUG
	set	HWORD_WLOCK, %o5
	cmp	%o2, %o5
	be,a,pt %icc, 7f			/* return failure */
	nop
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(rw_panic3), %o0
	call	panic
	or	%o0, %lo(rw_panic3), %o0
7:
#endif	DEBUG

	btst	2, %o0			/* which halfword */
	bz	%icc,8f
	mov	%g0, %o5
/*
 * rwlock is lower halfword
 */
	sll	%o3, 16, %o3
	or	%o2, %o3, %o1		/* o1 = old value */
	ba,pt	%icc, 9f
	or	%o5, %o3, %o3		/* o3 = new value */
/*
 * rwlock is upper halfword
 */
8:
	sll	%o2, 16, %o2
	or	%o2, %o3, %o1		/* o1 = old value */
/*
 * Now compare and swap
 */
9:
	cas	[%o4], %o1, %o3
	cmp	%o1, %o3
	be,a	%icc, 0f		/* return success */
	mov	0, %o0

	ba,pt	%icc, 6b		/* try again */
	lduh	[%o0], %o2
		
0:
	retl
	nop
	SET_SIZE(rwlock_hword_exit)

#endif /* lint */

#ifdef lint
/*
 * Primitives for atomic increments of a pointer to a circular array.
 * atomically adds 1 to the word pointed to by *curidx, unless it's reached
 * the maxidx, in which case it atomically returns *pidx to zero.
 * Rules for use: 1. maxidx must be > 0
 *		  2. *never* write *pidx after initialization
 *		  3. always use the returned or *newidx value as the
 *			index to your array
 *
 * 
 * void atinc_cidx_extword(longlong_t *curidx, longlong_t *newidx,
 *			longlong_t *maxidx)
 * int atinc_cidx_word(int *curidx, int maxidx)
 * short atinc_cidx_hword(short *curidx, int maxidx)
 * {
 *	curidx = *pidx;
 *      if (curidx < maxidx)
 *              *pidx = curidx++;
 *      else
 *              *pidx = curidx = 0;
 *	return(curidx);			! word, hword
 *	    OR	
 *	*newidx = curidx;		! extword
 * }
 */

/* ARGSUSED */
void
atinc_cidx_extword(longlong_t *curidx, longlong_t *newidx, longlong_t maxidx)
{}

/* ARGSUSED */
int
atinc_cidx_word(int *curidx, int maxidx)
{return 0;}

/* ARGSUSED */
short
atinc_cidx_hword(short *curidx, short maxidx)
{return 0;}

#else /* lint */

	ENTRY(atinc_cidx_extword)
1:
	ldx	[%o0], %o3			! load current idx to curidx
	subcc	%o2, %o3, %g0			! compare maxidx to curidx
	be,a,pn	%icc, 2f			! if maxidx == curidx
	clr	%o4				! then move 0 to newidx
	add	%o3, 1, %o4			! else newidx = curidx++
2:
	or	%o4, %g0, %o5			! save copy of newidx in %o5
	casx	[%o0], %o3, %o4	
	cmp	%o3, %o4			! if curidx != current idx
	bne,a,pn %icc, 1b			! someone else got there first
	nop 
	retl					! put saved copy of
	stx	%o5, [%o1] 			! newidx into [%o1]
	SET_SIZE(atinc_cidx_extword)

	ENTRY(atinc_cidx_word)
1:
	ld	[%o0], %o2			! load current idx to curidx
	subcc	%o1, %o2, %g0			! compare maxidx to curidx
	be,a,pn	%icc, 2f			! if maxidx == curidx
	clr	%o3				! then move 0 to newidx
	add	%o2, 1, %o3			! else newidx = curidx++
2:
	or	%o3, %g0, %o4			! save copy of newidx in %o4
	cas	[%o0], %o2, %o3	
	cmp	%o2, %o3			! if curidx != current idx
	bne,a,pn %icc, 1b			! someone else got there first
	nop 
	retl					! put saved copy of
	or	%o4, %g0, %o0 			! newidx into %o0
	SET_SIZE(atinc_cidx_word)

	ENTRY(atinc_cidx_hword)
	xor	%o0, 2, %o4
1:
	lduh	[%o0], %o2			! load current idx to curidx
	lduh	[%o4], %o3			! load the other half word
	subcc	%o1, %o2, %g0			! compare maxidx to curidx
	be,a,pn	%icc, 2f			! if maxidx == curidx
	clr	%o5				! then move 0 to newidx
	add	%o2, 1, %o5			! else newidx = curidx++
2:
	btst	2, %o0
	bnz,a,pn %icc, 3f			! counter is lower half
	sll	%o3, 16, %o3

	sll	%o5, 16, %o5			! counter is upper half
	sll	%o2, 16, %o2
3:
	or	%o5, %g0, %o4			! save copy of newidx in %o4
	or	%o5, %o3, %o5			! or newidx w/other half word
	or	%o2, %o3, %g2			! or curidx w/other half word
	cas	[%o0], %g2, %o5
	cmp	%g2, %o5			! if curidx != current idx
	bne,a,pn %icc, 1b			! someone else got there first
	nop 
	retl					! put saved copy of
	or	%o4, %g0, %o0			! newidx into %o0
	SET_SIZE(atinc_cidx_hword)

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

	ENTRY2(fuword,fuiword)
        sethi   %hi(USERLIMIT), %o3    ! compare access addr to USERLIMIT
        cmp     %o0, %o3                ! if (USERLIMIT >= addr) error
        bgeu    .fsubad
        btst    0x3, %o0                ! test alignment
        bne     .fsubad
        .empty
	set	.fsuerr, %o3
	swap	[THREAD_REG + T_LOFAULT], %o3
	lda	[%o0]ASI_USER, %o0		! get the word
	retl

.fsuerr:
	st	%o3, [THREAD_REG + T_LOFAULT]
.fsubad:
	retl
	mov	-1, %o0			! return error
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

	ENTRY2(fubyte,fuibyte)
        sethi   %hi(USERLIMIT), %o3    ! compare access addr to USERLIMIT
        cmp     %o0, %o3                ! if (USERLIMIT >= addr) error
        bgeu    .fsubad
        .empty
	set	.fsuerr, %o3
	swap	[THREAD_REG + T_LOFAULT], %o3
	lduba	[%o0]ASI_USER, %o0
	retl
	st	%o3, [THREAD_REG + T_LOFAULT]
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
        sethi   %hi(USERLIMIT), %o3    ! compare access addr to USERLIMIT
        cmp     %o0, %o3                ! if (USERLIMIT >= addr) error
        bgeu    .fsubad
        btst    0x3, %o0                ! test alignment
        bne     .fsubad
        .empty
	set	.fsuerr, %o3
	swap	[THREAD_REG + T_LOFAULT], %o3
	sta	%o1, [%o0]ASI_USER
	clr	%o0
	retl
	st	%o3, [THREAD_REG + T_LOFAULT]
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
        sethi   %hi(USERLIMIT), %o3    ! compare access addr to USERLIMIT
        cmp     %o0, %o3                ! if (USERLIMIT >= addr) error
        bgeu    .fsubad
        .empty
	set	.fsuerr, %o3
	swap	[THREAD_REG + T_LOFAULT], %o3
	stba	%o1, [%o0]ASI_USER
	clr	%o0
	retl
	st	%o3, [THREAD_REG + T_LOFAULT]
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
        sethi   %hi(USERLIMIT), %o3    ! compare access addr to USERLIMIT
        cmp     %o0, %o3                ! if (USERLIMIT >= addr) error
        bgeu    .fsubad
        btst    0x1, %o0                ! test alignment
        bne     .fsubad
        .empty
	set	.fsuerr, %o3
	swap	[THREAD_REG + T_LOFAULT], %o3
	lduha	[%o0]ASI_USER, %o0
	retl
	st	%o3, [THREAD_REG + T_LOFAULT]
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
        sethi   %hi(USERLIMIT), %o3    ! compare access addr to USERLIMIT
        cmp     %o0, %o3                ! if (USERLIMIT >= addr) error
        bgeu    .fsubad
        btst    0x1, %o0                ! test alignment
        bne     .fsubad
        .empty
	set	.fsuerr, %o3
	swap	[THREAD_REG + T_LOFAULT], %o3
	stha	%o1, [%o0]ASI_USER
	clr	%o0
	retl
	st	%o3, [THREAD_REG + T_LOFAULT]
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
	retl
	lda	[%o0]ASI_USER, %o0
	SET_SIZE(fuword_noerr)

	ENTRY(fusword_noerr)
	retl
	lduha	[%o0]ASI_USER, %o0
	SET_SIZE(fusword_noerr)

	ENTRY(fubyte_noerr)
	retl
	lduba	[%o0]ASI_USER, %o0
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
	retl
	sta	%o1, [%o0]ASI_USER
	SET_SIZE(suword_noerr)

	ENTRY(susword_noerr)
	retl
	stha	%o1, [%o0]ASI_USER
	SET_SIZE(susword_noerr)

	ENTRY(subyte_noerr)
	retl
	stba	%o1, [%o0]ASI_USER
	SET_SIZE(subyte_noerr)

#endif lint


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
 * Set tba to given address, no side effects.
 * This entry point is used by callback handlers.
 */
#if defined (lint)
/* ARGSUSED */
void *set_tba(void *new_tba)
{
	return ((void *)0);
}
#else /* lint */

	ENTRY(set_tba)
	mov	%o0, %o1
	rdpr	%tba, %o0
	wrpr	%o1, %tba
	retl
	nop
#endif /* lint */

/*
 * Set PSTATE_IE, regardless of current value, no side effects.
 * Returns previous contents of %pstate.
 * This entry point is used by callback handlers.
 */
#if defined (lint)
int enable_interrupts(void)
{
	return (0);
}
#else /* lint */

	ENTRY(enable_interrupts)
	rdpr	%pstate, %o0
	or	%o0, PSTATE_IE, %o1
	wrpr	%o1, %g0, %pstate
	retl
	nop
#endif /* lint */

#if defined(lint) || defined(__lint)

/* ARGSUSED */
void
setpstate(u_int pstate)
{}

#else	/* lint */

	ENTRY_NP(setpstate)
	retl
	wrpr	%g0, %o0, %pstate
	SET_SIZE(setpstate)

#endif	/* lint */

#if defined(lint) || defined(__lint)

u_int
getpstate(void)
{ return(0); }

#else	/* lint */

	ENTRY_NP(getpstate)
	retl
	rdpr	%pstate, %o0
	SET_SIZE(getpstate)

#endif	/* lint */

#if defined(lint) || defined(__lint)

/* ARGSUSED */
void
setwstate(u_int wstate)
{}

#else	/* lint */

	ENTRY_NP(setwstate)
	retl
	wrpr	%g0, %o0, %wstate
	SET_SIZE(setwstate)

#endif	/* lint */


#if defined(lint) || defined(__lint)

u_int
getwstate(void)
{ return(0); }

#else	/* lint */

	ENTRY_NP(getwstate)
	retl
	rdpr	%wstate, %o0
	SET_SIZE(getwstate)

#endif	/* lint */
