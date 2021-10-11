/*
 * Copyright 1986 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_CG4REG_H
#define	_SYS_CG4REG_H

#pragma ident	"@(#)cg4reg.h	1.15	93/02/04 SMI"
/* from cg4reg.h 1.8 88/08/19 SMI */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The cgfour frame buffer hardware definitions:
 *
 * The cg4 device has an overlay plane, enable plane and eight planes
 * of color/grayscale pixel read/write memory. There are no rasterop
 * chips or planes register. All memory is on board memory.
 *
 * There are two flavors of cg4.
 * Type A has AMD DACS in the I/O space and a status register.
 * Type B has a Brooktree BT458 DAC in the memory space.
 * Sun4 machines have only the type B.
 *
 * This port of cg4 to SVR4 supports only sun4 (type B).
 */

/* number of colormap entries */
#define	CG4_CMAP_ENTRIES	256

/* Type B (Brooktree DAC) definitions */

/*
 * For the cgfour.conf file:
 *
 * The cgfour has five registers:
 * Each will be treated as a register set,
 * thus the offsets to ddi_map_regs will always be 0.
 *
 *	CG4B_ADDR_CMAP		0xFB200000
 *	CG4B_ADDR_P4_PROBE_ID	0xFB300000
 *	CG4B_ADDR_OVERLAY	0xFB400000
 *	CG4B_ADDR_ENABLE	0xFB600000
 *	CG4B_ADDR_COLOR		0xFB800000
 *
 * Register set numbers:
 */
#define	CG4B_REGNUM_CMAP	0
#define	CG4B_REGNUM_P4_PROBE_ID	1
#define	CG4B_REGNUM_OVERLAY	2
#define	CG4B_REGNUM_ENABLE	3
#define	CG4B_REGNUM_COLOR	4

/*
 * Colormap structure
 */
struct cg4b_cmap {
	u_char	addr;		/* address register */
	char	fill0[3];
	u_char	cmap;		/* color map data register */
	char	fill1[3];
	u_char	ctrl;		/* control register */
	char	fill2[3];
	u_char	omap;		/* overlay map data register */
	char	fill3[3];
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CG4REG_H */
