
/*
 * Copyright (c) 1994 by Sun Microsystems Inc.
 */

#ifndef	_SYS_IVINTR_H
#define	_SYS_IVINTR_H

#pragma ident	"@(#)ivintr.h	1.11	95/08/02 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__STDC__
typedef u_int (*intrfunc)(caddr_t);
#else
typedef u_int (*intrfunc)();
#endif	/* __STDC__ */

/*
 * Interrupt Vector Table Entry
 *
 *	The interrupt vector table is statically allocated as part of
 *	the nucleus code in order to be locked in the tlb. An interrupt
 *	number is an index to the interrupt vector table representing
 *	a unique interrupt source to the system.
 */
struct intr_vector {
	intrfunc	iv_handler;	/* interrupt handler */
	caddr_t		iv_arg;		/* interrupt argument */
	u_short		iv_pil;		/* interrupt request level */
	u_short		iv_pending;	/* pending softint flag */
	kmutex_t	*iv_mutex; /* if non-zero, points to &unsave_driver */
};

extern void add_ivintr();
extern void rem_ivintr();
extern u_int add_softintr();
extern void rem_softintr();

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IVINTR_H */
