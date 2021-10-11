/*
 * Copyright (c) 1987-1991 by Sun Microsystems, Inc.
 */

#ifndef _SYS_XYCOM_H
#define	_SYS_XYCOM_H

#pragma ident	"@(#)xycom.h	1.17	92/08/07 SMI"	/* SunOS 4.1.1 1.14 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Common definitions for Xylogics disk drivers.  Names are prefixed
 * with 'xy', but these definitions also apply to the xd driver.
 */

/*
 * States for the command block flags
 */
#define	XY_FBSY		0x0001		/* cmdblock in use */
#define	XY_FRDY		0x0002		/* cmdblock ready for execution */
#define	XY_DONE		0x0004		/* operation completed */
#define	XY_FAILED	0x0008		/* command failed */
#define	XY_WANTED	0x0010		/* process waiting for iopb */
#define	XY_WAIT		0x0020		/* process waiting for completion */
#define	XY_INFRD	0x0040		/* in bad block forwarding */
#define	XY_INRST	0x0080		/* in a restore */
#define	XY_FNLRST	0x0100		/* in final restore (cmd failed) */
#define	XY_NOMSG	0x0200		/* suppress error messages */
#define	XY_DIAG		0x0400		/* diagnostic mode */
#define	XY_NOCHN	0x0800		/* don't chain this command */

/*
 * States for the ctlr structure flags
 */
#define	XY_C_PRESENT	0x01		/* ctlr exists */
#define	XY_C_24BIT	0x02		/* 24 bit addressing mode */
#define	XY_C_NOCHN	0x04		/* no chaining of iopbs */

/*
 * States for the unit structure flags
 */
#define	XY_UN_PRESENT	0x01		/* unit is online */
#define	XY_UN_ATTACHED	0x02		/* unit has been attached */


/*
 * Modes to execute a command
 */
#define	XY_SYNCH	0		/* synchronous */
#define	XY_ASYNCH	1		/* interrupt, no wait on iopb */
#define	XY_ASYNCHWAIT	2		/* interrupt, wait on iopb */

/*
 * Error message control -- if a given bit is set, those errors are
 * printed. All others are suppressed.
 */
#define	EL_FORWARD	0x0001		/* block forwarding message */
#define	EL_FIXED	0x0002		/* fixed error message */
#define	EL_RETRY	0x0004		/* retry message */
#define	EL_RESTR	0x0008		/* restore message */
#define	EL_RESET	0x0010		/* reset message */
#define	EL_FAIL		0x0020		/* failure message */

/*
 * Miscellaneous defines.
 */
#define	b_cylin b_resid			/* used for disksort */
#define	XYNUNIT		32		/* max # of units on system */
#define	XYNLPART	NDKMAP		/* # of logical partitions (8) */
#define	INSTANCE(dev)	((getminor(dev)>>3) % XYNUNIT)
#define	LPART(dev)	(getminor(dev) % XYNLPART)
#define	NOLPART		(-1)		/* used for 'non-partition commands */
#define	SECSIZE		512
#define	XYWATCHTIMO	20		/* seconds till disk check */
#define	XYLOSTINTTIMO	4		/* seconds till lost interrupt */
#define	XY_IN		0		/* command reads data */
#define	XY_OUT		1		/* command writes data */

/*
 * Structure definition and macros for manufacturer's list.
 */
#define	XY_MANDEFSIZE	24

struct xydefinfo {
	u_char	info[XY_MANDEFSIZE];
};

#define	XY_MAN_SYNC(x)		(x[0])
#define	XY_MAN_CYL(x)		(((x[1] & 0x7f) << 8) + x[2])
#define	XY_MAN_HEAD(x)		(x[3])
#define	XY_MAN_BFI(x, y)	((x[5 + 4 * y] << 8) + x[6 + 4 * y])
#define	XY_MAN_LEN(x, y)	((x[7 + 4 * y] << 8) + x[8 + 4 * y])
#define	XY_MAN_LAST(x)		(x[21])

#define	XY_TRK_BAD(x)		(x[1] & 0x80)

#define	XY_SYNCBYTE		0x19
#define	XY_LASTBYTE		0xf0

#define	XY_MAN_CYL_HI(x)	(x[1])
#define	XY_MAN_CYL_LO(x)	(x[2])
#define	XY_MAN_ZERO(x)		(x[4])
#define	XY_MAN_WR_BFI(x, y, z)	x[5 + 4*y] = z>>8; x[6 + 4*y] = z;
#define	XY_MAN_WR_LEN(x, y, z)	x[7 + 4*y] = z>>8; x[8 + 4*y] = z;
#define	XY_MAN_MRK_TRK_BAD(x)	x[1] |= 0x80
#define	XY_MAN_DEFECT_BEGIN	10
#define	XY_MAN_DEFECT_END	55


/*
 * Macros for sector headers that don't hold data.
 */
#define	XY_HDR_SPARE	0xdddddddd		/* header for spare sector */
#define	XY_HDR_RUNT	0xeeeeeeee		/* header for runt sector */
#define	XY_HDR_SLIP	0xfefefefe		/* header for slipped sector */
#define	XY_HDR_ZAP	0xffffffff		/* header for zapped sector */

#if	defined(_KERNEL) || defined(_KMEMUSER)
/*
 * Stucture for recording whether a device is fully open or closed.
 *
 * Assumptions:
 *
 *	+ There are only 8 partitions possible.
 *	+ BLK, MNT, CHR, SWP don't change in some future release!
 */

struct ocinfo {
	/*
	 * Types BLK, MNT, CHR, SWP,
	 * assumed to be types 0-3.
	 */
	u_long	lyr_open[NDKMAP];
	u_char	reg_open[OTYPCNT - 1];
};
#define	OCSIZE	sizeof (struct ocinfo)
union ocmap {
	u_char chkd[OCSIZE];
	struct ocinfo rinfo;
};
#define	lyropen	rinfo.lyr_open
#define	regopen	rinfo.reg_open
#endif	/* defined(_KERNEL) || defined(_KMEMUSER) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_XYCOM_H */
