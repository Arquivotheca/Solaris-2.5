/*	@(#)eccreg.h	1.6 92/06/4 SMI; SunOS 1E */

#ifndef	_SYS_ECCREG_H
#define	_SYS_ECCREG_H

#pragma ident	"@(#)eccreg.h	1.4	93/02/04 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Copyright (c) 1992-93 by Sun Microsystems, Inc.
 */

#define	OBIO_ECCREG_ADDR 0xFC000000	/* address in obio space */

#define	MAX_ECC	4			/* max number of ECC memory boards */

#define	ECCREG_ADDR 0xFFFFA000		/* virtual address we map it to */

#ifdef LOCORE

#define	ECCREG ECCREG_ADDR

#else

struct eccreg
	{
    u_int	ecc_enable;		/* ECC enable register (r/w) */
    u_int	ecc_paddr;		/* ECC physical address register (ro) */
    u_int	ecc_synd_status;	/* ECC syndrome/status register (ro) */
    u_int	ecc_diagnostic;		/* ECC diagnostic register (wo) */
    u_char	eccreg_pad[64 - (4 * sizeof (u_int))];
	};

#define	ECCREG ((struct eccreg *)ECCREG_ADDR)

/*
 * ecc_enable for SPARCengine 1E memory boards
 */

/*
 * ECC Enable Register masks
 */
#define	ECC_BOARDENA_	0x00000001	/* <0>, r/w - board enable */
#define	ECC_INTENABLE	0x00000002	/* <1>, r/w - interrupt enable */
#define	ECC_CORRECT_	0x00000004	/* <2>, r/w - error correct. enable */
#define	ECC_DM_MASK	0x00000018	/* <3:4>, r/w - diag mode control */
#define	ECC_CHECK	0x00000020	/* <5>, r/w - check/diag */
#define	ECC_CID0	0x00000040	/* <6>, r/w - internal control mode */
#define	ECC_BOARDID_	0x00000600	/* <9:10>, r/o - board ID */
#define	ECC_BOARDSIZE_	0x00001800	/* <11:12>, r/o - board size */
#define	ECC_RAMSIZE	0x00002000	/* <13>, r/o - RAM size */
/* <14>, r/o - 0 = mapped high, 1 = mapped low */
#define	ECC_HILOMEM	0x00004000

#define	ECC_BOARDID_SHIFT	15

/*
 * Physical base addresses for boards,
 * controlled by the ECC_HILOMEM jumper.
 */
#define	ECC_HIMEM	0x10000000
#define	ECC_LOMEM	0x00000000


/*
 * ECC syndrome + status register
 */
#define	SY_SYND_MASK	0x000000FF	/* <7:0>, r/w - UE|CE|syndrome */
#define	SY_CE_MASK	0x00000100	/* <8>, r/w - correctable error */
#define	SY_UE_MASK	0x00000200	/* <9>, r/w uncorrectable error */


#endif

#define	SYNDERR_BITS	"\20\10S32\7S16\6S8\5S4\4S2\3S1\2S0\1SX"

#define	MEMINTVL		60	/* sixty second delay for softecc */

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_ECCREG_H */
