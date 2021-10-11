/*
 * Copyright (c) 1987, 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_BUSERR_H
#define	_SYS_BUSERR_H

#pragma ident	"@(#)buserr.h	1.12	92/07/14 SMI"
/* From 4.1.1 sun4/buserr.h */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Definitions and structures related to system bus error handling for Sun4.
 */

/*
 * The System Bus Error Register captures the cause(s) of bus
 * errors given to the CPU. Because of prefetch the Bus Error
 * Register must accumulate errors. After a bus error the
 * register usually flags one error. However sometimes, because
 * of prefetch, the register contains more than one error
 * indication. In this case the software must verify the type
 * of error by reading the MMU, decoding the instruction to
 * get the access type, or by re-executing faulted instruction.
 * The Bus Error Register is read-only and is addressed in ASI_CTL
 * space. Errors are latched only for CPU cycles. They are not
 * latched for DVMA cycles.
 *
 * The be_proterr bit is set only if the valid bit was set AND the
 * page protection does not allow the kind of operation attempted.
 */
#ifndef _ASM
struct	buserrorreg {
	u_int	be_invalid	:1;	/* Page map Valid bit was off */
	u_int	be_proterr	:1;	/* Protection error */
	u_int	be_timeout	:1;	/* Bus access timed out */
	u_int	be_vmeberr	:1;	/* VMEbus bus error */
	u_int			:2;
	u_int	be_serr		:1;	/* access size error */
	u_int	be_watchdog	:1;	/* Watchdog reset */
};
#endif /* !_ASM */

/*
 * Equivalent bits.
 */
#define	BE_INVALID	0x80		/* page map was invalid */
#define	BE_PROTERR	0x40		/* protection error */
#define	BE_TIMEOUT	0x20		/* bus access timed out */
#define	BE_VMEBERR	0x10		/* VMEbus bus error */
#define	BE_SIZERR	0x02		/* size error */
#define	BE_WATCHDOG	0x01		/* Watchdog reset */

#define	BUSERRREG	0x60000000	/* addr of buserr reg in ASI_CTL */

#define	BUSERR_BITS \
	"\20\10INVALID\7PROTERR\6TIMEOUT\5VMEBERR\2SIZERR\1WATCHDOG"

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BUSERR_H */
