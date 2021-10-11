/*
 * Copyright (c) 1985-1991, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MEMERR_H
#define	_SYS_MEMERR_H

#pragma ident	"@(#)memerr.h	1.11	93/05/26 SMI"
/* From SunOS 4.1.1 sun4/memerr.h 1.6 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * All Sun-4 implementations have either memory parity error detection
 * or memory equipped with error correction (ECC). The memory error
 * register consists of a control and an address register.  If an error
 * occurs, the control register stores information relevant to the error.
 * The memory address error register stores the virtual address, the
 * context number, and the CPU/DVMA bit of the memory cycle at which
 * the error was detected.  Errors are reported via a non-maskable
 * level 15 interrupt.  In case of multiple (stacked) memory errors,
 * the information relation to the first error is latched in the
 * memory error register.  The interrupt is held pending and the error
 * information in the memory error register is latched (frozen) until
 * it is cleared (unfrozen) by a write to bits <31..24> of the memory
 * error address register.
 */

#define	OBIO_MEMERR_ADDR 0xF4000000	/* address of memerr in obio space */

#define	MEMERR_ADDR	(MDEVBASE + 0x6000)	/* virtual address mapped to */

#ifdef _ASM
#define	MEMERR MEMERR_ADDR	/* virtual address we map memerr to be at */
#else	/* _ASM */

#if defined(_KERNEL)

struct regs;			/* foward declaration, keeps lint happy */

#include <sys/pte.h>

struct memerr {
	u_int	me_err;		/* memory error register */
#define	me_per	me_err		/* parity error register */
#define	me_eer	me_err		/* ECC error register */
	u_int	me_vaddr;	/* virtual address of error */
};
#define	MEMERR ((struct memerr *)(MEMERR_ADDR))


void memerr_init(void);
void memerr_signal(caddr_t);
void memerr_330(u_int, u_int, caddr_t, unsigned, struct regs *);
int parerr_reset(caddr_t, struct pte *);

#endif /* defined(_KERNEL) */
#endif /* _ASM */

/*
 *  Bits for the memory error register when used as parity error register
 */
#define	PER_CTX		0x1fe00	/* r/o - context number mask */
#define	PER_DVMA	0x100	/* r/o - DVMA access indicator */
#define	PER_INTR	0x80	/* r/o - 1 = parity interrupt pending */
#define	PER_INTENA	0x40	/* r/w - 1 = enable interrupt on parity error */
#define	PER_TEST	0x20	/* r/w - 1 = write inverse parity */
#define	PER_CHECK	0x10	/* r/w - 1 = enable parity checking */
#define	PER_ERR24	0x08	/* r/o - 1 = parity error <24..31> */
#define	PER_ERR16	0x04	/* r/o - 1 = parity error <16..23> */
#define	PER_ERR08	0x02	/* r/o - 1 = parity error <8..15> */
#define	PER_ERR00	0x01	/* r/o - 1 = parity error <0..7> */
#define	PER_ERR		0x0F	/* r/o - mask for some parity error occuring */
#define	PARERR_BITS	\
	"\20\11DVMA\10INTR\7INTENA\6TEST\5CHECK\4ERR24\3ERR16\2ERR08\1ERR00"

#define	PER_DVMA_SHIFT	8
#define	PER_CTX_SHIFT	9

/*
 *  Bits for the memory error register when used as ECC error register
 */
#define	EER_CTX		0x1fe00	/* r/o - context number mask */
#define	EER_DVMA	0x100	/* r/o - DVMA access indicator */
#define	EER_INTR	0x80	/* r/o - ECC memory interrupt pending */
#define	EER_INTENA	0x40	/* r/w - enable interrupts on errors */
#define	EER_BUSHOLD	0x20	/* r/w - hold memory bus mastership */
#define	EER_CE_ENA	0x10	/* r/w - enable CE recording */
#define	EER_TIMEOUT	0x08	/* r/o - Sirius bus time out */
#define	EER_WBACKERR	0x04	/* r/o - write back error */
#define	EER_UE		0x02	/* r/o - UE, uncorrectable error  */
#define	EER_CE		0x01	/* r/o - CE, correctable (single bit) error */
#define	EER_ERR		0x0F	/* r/o - mask for some ECC error occuring */
#define	ECCERR_BITS	\
	"\20\11DVMA\10INTR\7INTENA\6BUSHOLD\5CE_ENA\4TIMEOUT\3WBACKERR\2UE\1CE"

#define	EER_DVMA_SHIFT	8	/* shift for DVMA bit */
#define	EER_CTX_SHIFT	9	/* shift for context number */

#define	ER_INTR		0x80	/* mask for ECC/parity interrupt pending */
#define	ER_INTENA	0x40	/* mask for ECC/parity enable interrupt */


#define	MEMINTVL	60	/* sixty second delay for softecc */
/*
 * stingray parity error SIMM ID print defines
 */
#define	CONF    0xE
#define	CONF_POS	1
#define	SIMMADDR	0x0FC00000
#define	SIMMADDR_POS    22

/*
 * Flags to define type of memory error.
 */
#define	MERR_SYNC	0x0
#define	MERR_ASYNC	0x1

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MEMERR_H */
