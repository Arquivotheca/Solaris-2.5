/*
 * Copyright (c) 1992-1993 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)fixmmu_sun4m.s 1.8     94/11/20 SMI"

#include <sys/asm_linkage.h>
#include <sys/machparam.h>
#include <sys/mmu.h>
#include <v7/sys/psr.h>

#if defined(lint)

/* ARGSUSED */
void
srmmu_mmu_setctp(int v)
{}

/* ARGUSED */
void
srmmu_mmu_setctxreg(int v)
{}

int
srmmu_mmu_getctp(void)
{ return (0); }

/* ARGSUSED */
int
srmmu_mmu_probe(int pa)
{ return (0); }

void  
srmmu_mmu_flushall(void)
{}

/* ARGSUSED */
void 
srmmu_mmu_flushpage(caddr_t base)
{}

/* ARGSUSED */
void
stphys(int physaddr, int value)
{}

/* ARGSUSED */
int
ldphys(int physaddr)
{ return(0); }

/* ARGSUSED */
int
move_page(u_int src_va, u_int dest_va)
{ return (0); }

void
vik_turn_cache_on(void)
{}

/* ARGSUSED */
void
tsu_pac_flushall()
{}

#else	/* lint */

	!
	! misc srmmu manipulation routines
	!
	! copied from ../sun4m/ml/module_srmmu_asm.s
	!
	ENTRY(srmmu_mmu_setctp)
	set     RMMU_CTP_REG, %o1       ! set srmmu context table pointer
	retl
	sta     %o0, [%o1]ASI_MOD
	SET_SIZE(srmmu_mmu_setctp)
	 
	ENTRY(srmmu_mmu_setctxreg)
	set     RMMU_CTX_REG, %o5       ! set srmmu context number
	retl
	sta     %o0, [%o5]ASI_MOD
	SET_SIZE(srmmu_mmu_setctxreg)

        ENTRY(srmmu_mmu_getctp)
        set     RMMU_CTP_REG, %o1       ! get srmmu context table ptr
        retl
        lda     [%o1]ASI_MOD, %o0
        SET_SIZE(srmmu_mmu_getctp)

        ENTRY(srmmu_mmu_probe)
        and     %o0, MMU_PAGEMASK, %o0  ! virtual page number
        or      %o0, FT_ALL<<8, %o0     ! match criteria
        retl
        lda     [%o0]ASI_FLPR, %o0
        SET_SIZE(srmmu_mmu_probe)

        ENTRY(srmmu_mmu_flushall)       
        or      %g0, FT_ALL<<8, %o0     ! flush entire mmu
        sta     %g0, [%o0]ASI_FLPR      ! do the flush
	nop				! XXXXXX: kludge for Tsunami
	nop				! XXXXXX: kludge for Tsunami
	nop				! XXXXXX: kludge for Tsunami
	nop				! XXXXXX: kludge for Tsunami
	nop				! XXXXXX: kludge for Tsunami
        retl
        nop                             ! MMU delay
        SET_SIZE(srmmu_mmu_flushall)       
 
        ENTRY(srmmu_mmu_flushpage)      
        or      %o0, FT_PAGE<<8, %o0
        sta     %g0, [%o0]ASI_FLPR      ! do the flush
        retl
        nop                             ! PSR or MMU delay
        SET_SIZE(srmmu_mmu_flushpage)      
 


	!
	! load value at physical address
	!
	! copied from ../sun4m/ml/subr_4m.s
	!
	ENTRY(ldphys)
	sethi	%hi(mxcc), %o4
	ld	[%o4+%lo(mxcc)], %o5
	tst	%o5
	bz,a	1f
	lda     [%o0]ASI_MEM, %o0

	sethi	%hi(use_table_walk), %o4
	ld	[%o4+%lo(use_table_walk)], %o5
	tst	%o5			! use_cache off in CC mode means
	bz,a	1f			! don't cache the srmmu tables.
	lda     [%o0]ASI_MEM, %o0

	! For Viking E-$, it is necessary to set the AC bit of the
	! module control register to indicate that this access
	! is cacheable.
	mov     %psr, %o4               ! get psr
	or      %o4, PSR_PIL, %o5       ! block traps
	mov     %o5, %psr               ! new psr
	nop; nop; nop                   ! PSR delay
	lda     [%g0]ASI_MOD, %o3       ! get MMU CSR
	set     CPU_VIK_AC, %o5         ! AC bit
	or      %o3, %o5, %o5           ! or in AC bit
	sta     %o5, [%g0]ASI_MOD       ! store new CSR
	lda     [%o0]ASI_MEM, %o0
	sta     %o3, [%g0]ASI_MOD       ! restore CSR
	mov     %o4, %psr               ! restore psr
	nop; nop; nop                   ! PSR delay
1:	retl
	nop
	SET_SIZE(ldphys)



	!
	! Store value at physical address
	!
	! void	stphys(physaddr, value)
	!
	ENTRY(stphys)
	sethi	%hi(mxcc), %o4
	ld	[%o4+%lo(mxcc)], %o5
	tst	%o5
	bz,a	1f
	sta     %o1, [%o0]ASI_MEM

	sethi	%hi(use_table_walk), %o4
	ld	[%o4+%lo(use_table_walk)], %o5
	tst	%o5			! use_cache off in CC mode means
	bz,a	1f			! don't cache the srmmu tables.
	sta     %o1, [%o0]ASI_MEM

	! For Viking E-$, it is necessary to set the AC bit of the
	! module control register to indicate that this access
	! is cacheable.
	mov     %psr, %o4               ! get psr
	or      %o4, PSR_PIL, %o5       ! block traps
	mov     %o5, %psr               ! new psr
	nop; nop; nop                   ! PSR delay
	lda     [%g0]ASI_MOD, %o3       ! get MMU CSR
	set     CPU_VIK_AC, %o5         ! AC bit
	or      %o3, %o5, %o5           ! or in AC bit
	sta     %o5, [%g0]ASI_MOD       ! store new CSR
	sta     %o1, [%o0]ASI_MEM       ! the actual stphys
	sta     %o3, [%g0]ASI_MOD       ! restore CSR
	mov     %o4, %psr               ! restore psr
	nop; nop; nop                   ! PSR delay
1:	retl
	nop
	SET_SIZE(stphys)



	!
	! move_page: used to relocate a boot page
	!
	! copy a page worth of data from src_va=%o0 to tmp_va=%o1
	!
	! assume all addreses are aligned
	!
	ENTRY(move_page)
	set     MMU_PAGESIZE, %o2	! do a whole page

.copy_loop:
	ldd [%o0+0x8], %l0		! copy 16 bytes at a time
	std %l0, [%o1+0x8]
	ldd [%o0+0x0], %l0
	std %l0, [%o1+0x0]

	add %o0, 0x10, %o0		! incr src addr
	subcc %o2, 0x10, %o2
	bg,a .copy_loop
	add %o1, 0x10, %o1		! incr dest addr

	retl
	nop
	SET_SIZE(move_page)

        ENTRY(tsu_turn_cache_on)
        sta     %g0, [%g0]ASI_ICFCLR    ! flash clear icache
        sta     %g0, [%g0]ASI_DCFCLR    ! flash clear dcache
        set     0x300, %o2      	! i & d cache enable
        set     RMMU_CTL_REG, %o0
        lda     [%o0]ASI_MOD, %o1
        or      %o1, %o2, %o1
        sta     %o1, [%o0]ASI_MOD
        retl
        nop
        SET_SIZE(tsu_turn_cache_on)

        ENTRY(tsu_pac_flushall)
        sta     %g0, [%g0]ASI_ICFCLR    ! flash clear icache
        sta     %g0, [%g0]ASI_DCFCLR    ! flash clear dcache
        retl
        nop
        SET_SIZE(tsu_pac_flushall)

#endif		/* lint */


! The following routines will be deleted after the code gets stable

#ifdef OBSOLETE

#define	CACHE_VIK_OFF		(CACHE_VIK_ON & ~(CPU_VIK_IE|CPU_VIK_DE))
#define	CACHE_VIK_OFF_E		((CACHE_VIK_ON_E & ~(CPU_VIK_TC|CPU_VIK_PF)) \
				& CACHE_VIK_OFF)
#define	MXCC_CE_OFF		(~MXCC_CE)

#if defined(lint)

/* ARGSUSED */
void
tsu_turn_cache_on()
{}

/* ARGSUSED */
void
tsu_turn_cache_off()
{}

void
vik_turn_cache_off(void)
{}

void
vik_turn_mxcc_on(void)
{}

void
vik_turn_mxcc_off(void)
{}

#else	/* lint */

        ENTRY(tsu_turn_cache_off)
        sta     %g0, [%g0]ASI_ICFCLR    ! flash clear icache
        sta     %g0, [%g0]ASI_DCFCLR    ! flash clear dcache
        set     0xfffffcff, %o2      	! i & d cache disable
        set     RMMU_CTL_REG, %o0
        lda     [%o0]ASI_MOD, %o1
        and      %o1, %o2, %o1
        sta     %o1, [%o0]ASI_MOD
        retl
        nop
        SET_SIZE(tsu_turn_cache_off)

	ENTRY(vik_turn_cache_off)
	set	CACHE_VIK_OFF, %o2		!MBus mode
	set	RMMU_CTL_REG, %o0
	lda	[%o0]ASI_MOD, %o1
	andcc	%o1, CPU_VIK_MB, %g0
	bnz	1f				!MBus mode
	nop
	set	CACHE_VIK_OFF_E, %o2		!CC mode
1:	or	%o1, %o2, %o1
	retl
	sta	%o1, [%o0]ASI_MOD
	SET_SIZE(vik_turn_cache_off)

	ENTRY(vik_turn_mxcc_off)
	set	MXCC_CNTL, %o4
	lda	[%o4]ASI_MXCC, %o5	! read mxcc control reg
	set	MXCC_CE_OFF, %o1	! E-$ bit off
	andn	%o5, %o1, %o5		! turn off E-$ 
	retl
	sta	%o5, [%o4]ASI_MXCC	! update mxcc control reg
	SET_SIZE(vik_turn_mxcc_off)

	ENTRY(vik_turn_mxcc_on)
	set	MXCC_CNTL, %o4
	lda	[%o4]ASI_MXCC, %o5	! read mxcc control reg
	set	MXCC_CE, %o1		! E-$ bit 
	or	%o5, %o1, %o5		! turn on E-$
	retl
	sta	%o5, [%o4]ASI_MXCC	! update mxcc control reg
	SET_SIZE(vik_turn_mxcc_on)

	ENTRY(vik_turn_cache_on)
! %%% what do we stuff in here?
!     do we need to turn on MXCC here?
	set	CACHE_VIK_ON, %o2		!MBus mode
	set	RMMU_CTL_REG, %o0
	lda	[%o0]ASI_MOD, %o1
	andcc	%o1, CPU_VIK_MB, %g0
	bnz	1f				!MBus mode
	nop
	set	CACHE_VIK_ON_E, %o2		!CC mode
1:	or	%o1, %o2, %o1
	retl
	sta	%o1, [%o0]ASI_MOD
	SET_SIZE(vik_turn_cache_on)

#endif		/* lint */
#endif OBSOLETE
