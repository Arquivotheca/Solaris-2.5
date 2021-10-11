!
! @(#)_Q_get_rp_rd.S 1.3 91/06/11 SMI
!
! Copyright (c) 1989 by Sun Microsystems, Inc.
!

#include "SYS.h"

        ENTRY(_Q_get_rp_rd)
	.global fp_precision, fp_direction
__Q_get_rp_rd:
#ifdef PIC
	PIC_SETUP(o5)
	ld     [%o5+fp_direction],%o3
#else
	set     fp_direction,%o3
#endif
	set     0xc0000000,%o4          ! mask of rounding direction bits
        st      %fsr,[%sp+0x44]
        ld      [%sp+0x44],%o0          ! o0 = fsr
        and     %o0,%o4,%o1
        srl     %o1,30,%o1
	st	%o1,[%o3]
#ifdef PIC
	ld     [%o5+fp_precision],%o3
#else
	set     fp_precision,%o3
#endif
	set     0x30000000,%o4
	and     %o0,%o4,%o1
	srl     %o1,28,%o1
        retl
	st	%o1,[%o3]
        SET_SIZE(_Q_get_rp_rd)
