/*
 * Copyright (c) 1988-1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_MEMERR_H
#define	_SYS_MEMERR_H

#pragma ident	"@(#)memerr.h	1.10	94/03/22 SMI"

/* From: SunOS-4.1 1.9	*/

#include <sys/devaddr.h>

/*
 * All Sun-4c implementations have memory parity error detection.
 * The memory error register consists of a control register.  If an error
 * occurs, the control register stores information relevant to the error.
 * The address of the word in error is stored in one of the error
 * address registers; the SEVAR for synchronous errors and the ASEVAR for
 * asynchronous errors.  The byte(s) in error are identified by the
 * memory error register.
 * Errors are reported either by a trap (for synchronous errors) or a
 * non-maskable level 15 interrupt (for asynchronous errors).
 * If a second error occurs before the first one has been processed,
 * the MULTI bit will be on in the memory error register, and the
 * address of the first (for asynchronous errors) or last (for
 * synchronous errors) error will be stored in the appropriate error
 * address register.
 * The interrupt is cleared by toggling the "enable all interrupts" bit
 * in the interrupt control register.
 * The information bits in the memory error register are cleared by
 * reading it.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#define	MEMEXP_START	0X40000000	/* Off-board memory starts at 1GB */
#ifndef _ASM
struct memerr {
	u_int	me_err;		/* memory error register */
#define	me_per	me_err		/* parity error register */
};
extern caddr_t v_memerr_addr;
#define	MEMERR ((struct memerr *)(v_memerr_addr))

extern void memerr_init(void);
#endif /* ! _ASM */

/*
 *  Bits for the memory error register when used as parity error register
 */
#define	PER_ERROR	0x80	/* r/o - 1 = parity error detected */
#define	PER_MULTI	0x40	/* r/o - 1 = second error detected */
#define	PER_TEST	0x20	/* r/w - 1 = write inverse parity */
#define	PER_CHECK	0x10	/* r/w - 1 = enable parity checking */
#define	PER_ERR00	0x08	/* r/o - 1 = parity error <0..7> */
#define	PER_ERR08	0x04	/* r/o - 1 = parity error <8..15> */
#define	PER_ERR16	0x02	/* r/o - 1 = parity error <16..23> */
#define	PER_ERR24	0x01	/* r/o - 1 = parity error <24..31> */
#define	PER_ERRS	0x0F	/* r/o - mask for specific error bits */
#define	PARERR_BITS	\
	"\20\10ERROR\7MULTI\6TEST\5CHECK\4ERR00\3ERR08\2ERR16\1ERR24"

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MEMERR_H */
