/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_CG8_P4REG_H
#define	_SYS_CG8_P4REG_H

#pragma ident	"@(#)cg8-p4reg.h	1.6	93/02/04 SMI"
/* from cg8reg.h	1.9 of 7/15/91, SMI */

#include <sys/param.h>
#include <sys/p4reg.h>
#include <sys/ramdac.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * For the cgeight.conf file:
 *
 * (Making the assumption that the cg8 is addressed like a cg4)
 *
 * The cgeight has five registers:
 * Each will be treated as a register set,
 * thus the offsets to ddi_map_regs will always be 0.
 *
 * 	CG8_ADDR_CMAP		0xFB200000
 * 	CG8_ADDR_P4_PROBE_ID	0xFB300000
 * 	CG8_ADDR_OVERLAY	0xFB400000
 * 	CG8_ADDR_ENABLE		0xFB600000
 * 	CG8_ADDR_COLOR		0xFB800000
 *
 * Register set numbers:
 */
#define	CG8_REGNUM_CMAP		0
#define	CG8_REGNUM_P4_PROBE_ID	1
#define	CG8_REGNUM_OVERLAY	2
#define	CG8_REGNUM_ENABLE	3
#define	CG8_REGNUM_COLOR	4

/*
 * These are the physical offset from the beginning of the P4.
 */
#define	P4IDREG		0x300000
/* constants define in p4reg.h */
#define	DACBASE		(P4IDREG + P4_COLOR_OFF_LUT)
#define	OVLBASE		(P4IDREG + P4_COLOR_OFF_OVERLAY)
#define	ENABASE		(P4IDREG + P4_COLOR_OFF_ENABLE)
#define	FBMEMBASE	(P4IDREG + P4_COLOR_OFF_COLOR)
#define	PROMBASE	(P4IDREG + 0x8000)

/*
 * The device as presented by the "mmap" system call.  It seems to the mmap
 * user that the board begins, at its 0 offset, with the overlay plane,
 * followed by the enable plane and the color framebuffer.  At 8MB, there
 * is the ramdac followed by the p4 register and the boot prom.
 */
#define	CG8_VADDR_FB	0
#define	CG8_VADDR_DAC	0x800000
#define	CG8_VADDR_P4REG	(CG8_VADDR_DAC + ctob(1))
#define	CG8_VADDR_PROM	(CG8_VADDR_P4REG + ctob(1))
#define	PROMSIZE	0x40000

/*
 * Some sizes constants for reference only.  No one actually use them.
 */
#define	CG8_WIDTH	1152	/* default width */
#define	CG8_HEIGHT	900	/* default height */
#define	PIXEL_SIZE	4	/* # of bytes per pixel in frame buffer */
#define	BITPERBYTE	8
#define	FBSCAN_SIZE	(CG8_WIDTH * PIXEL_SIZE)
#define	OVLSCAN_SIZE	(CG8_WIDTH / BITPERBYTE)

/* screen size in bytes */
#define	FBMEM_SIZE	(FBSCAN_SIZE * CG8_HEIGHT)
#define	OVL_SIZE	(OVLSCAN_SIZE * CG8_HEIGHT)
#define	CG8_RAMDAC_OMAPSIZE	4
#define	CG8_RAMDAC_CMAPSIZE	256

/*
 * Constants from <sundev/ramdac.h> which define the structure of 3
 * Brooktree 458 ramdac packed into one 32-bit register.
*/
#define	CG8_RAMDAC_READMASK	RAMDAC_READMASK
#define	CG8_RAMDAC_BLINKMASK	RAMDAC_BLINKMASK
#define	CG8_RAMDAC_COMMAND	RAMDAC_COMMAND
#define	CG8_RAMDAC_CTRLTEST	RAMDAC_CTRLTEST

/*
 * The following sessions describe the physical device.  No software
 * actually uses this model which for initial board bring-up and debugging
 * only.  Since the definitions of the structure take no space, we leave
 * them here for future references.
 */
union ovlplane {
    u_short	pixel[OVLSCAN_SIZE / sizeof (u_short)][CG8_HEIGHT];
    u_short	bitplane[OVL_SIZE / sizeof (u_short)];
};


struct overlay {
    union ovlplane	color;
    u_char		pad[ENABASE - OVLBASE - OVL_SIZE];
    union ovlplane	enable;
};


/*
	The whole board.  We defined fb to be linearly addressable,
	instead of a two dimensional array.  Maybe we should use union?
*/
struct cg8_board {
    struct ramdac	lut;		/* start at P8ASE + DACBASE */
    u_char		pad1[P4IDREG - DACBASE - sizeof (struct ramdac)];
    u_int		p4reg;		/* p4 bus register */
    u_char		pad2[OVLBASE - P4IDREG - sizeof (u_int)];
    struct overlay	ovl;		/* overlay planes */
    u_char		pad3[FBMEMBASE - OVLBASE - sizeof (struct overlay)];
    union fbunit	fb[FBMEM_SIZE / sizeof (union fbunit)];
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CG8_P4REG_H */
