/*
 * Copyright (c) 1987-1991 by Sun Microsystems, Inc.
 */

#ifndef _SYS_XDCREG_H
#define	_SYS_XDCREG_H

#pragma ident	"@(#)xdcreg.h	1.4	92/07/14 SMI"	/* SunOS 4.1.1 1.10 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Common Xylogics 751/772/7053 declarations
 */

/*
 * I/O space registers - byte accesses only, but on word boundaries.
 */
struct xddevice {
	u_char :8;
	u_char xd_iopbaddr1;	/* 1,3,5,7 - iopb address */
	u_char :8;
	u_char xd_iopbaddr2;
	u_char :8;
	u_char xd_iopbaddr3;
	u_char :8;
	u_char xd_iopbaddr4;
	u_char :8;
	u_char xd_modifier;	/* 9 - iopb address modifier */
	u_char :8;
	u_char xd_csr;		/* b - controller status register */
	u_char :8;
	u_char xd_fatal;	/* d - fatal error register */
};

#define	XDHWSIZE	sizeof (struct xddevice)
/*
 * Macro to massage an address into byte sized chunks.
 */
#define	XDOFF(a, byte)	(((int)(a) >> (8 * byte)) & 0xff)

/*
 * xd_csr bits
 */
#define	XD_BUSY		0x80	/* r - operation in progress */
#define	XD_FERR		0x40	/* r - fatal error encountered */
#define	XD_ENAM		0x20	/* w - enable maintanence mode */
#define	XD_MACT		0x20	/* r - maintanence mode active */
#define	XD_RST		0x08	/* w - controller reset */
#define	XD_RACT		0x08	/* r - controller reset active */
#define	XD_AIO		0x04	/* w - add iopb to active list */
#define	XD_AIOP		0x04	/* r - iopb addition pending */
#define	XD_CLRIO	0x02	/* w - clear registers active */
#define	XD_RIO		0x02	/* r - registers are active */
#define	XD_CLRBS	0x01	/* w - clear registers busy */
#define	XD_RBS		0x01	/* r - registers are busy */

/*
 * Miscellaneous defines
 */
#define	XD_ADDRMOD24	0x3d	/* standard supervisory data modifier */
#define	XD_ADDRMOD32	0x0d	/* extended supervisory data modifier */

/*
 * Controller types
 */
#define	XDC_751		0x51
#define	XDC_772		0x72
#define	XDC_7053	0x53

/*
 * Miscellaneous macros
 */

#define	XDC_GET_IOPBADDR(ctlr)	\
	(((u_int) ctlr->c_io->xd_iopbaddr1) | \
	(((u_int) ctlr->c_io->xd_iopbaddr2) << 8) | \
	(((u_int) ctlr->c_io->xd_iopbaddr3) << 16) | \
	(((u_int) ctlr->c_io->xd_iopbaddr4) << 24))

#define	XDC_SET_IOPBADDR(c, val)	\
	(c)->c_io->xd_iopbaddr1 = XDOFF(val, 0), \
	(c)->c_io->xd_iopbaddr2 = XDOFF(val, 1), \
	(c)->c_io->xd_iopbaddr3 = XDOFF(val, 2), \
	(c)->c_io->xd_iopbaddr4 = XDOFF(val, 3)

#define	XDCDELAY(c, n)	\
{ \
	register int N = (n); \
	while (--N > 0) { \
		if ((c)) \
			break; \
		drv_usecwait(1); \
	} \
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_XDCREG_H */
