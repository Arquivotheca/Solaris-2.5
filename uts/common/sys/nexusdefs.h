/*
 * Copyright (c) 1992-1994, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_NEXUSDEFS_H
#define	_SYS_NEXUSDEFS_H

#pragma ident	"@(#)nexusdefs.h	1.13	94/08/09 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Bus Nexus Control Operations
 */

typedef enum {
	DDI_CTLOPS_DMAPMAPC,
	DDI_CTLOPS_INITCHILD,
	DDI_CTLOPS_UNINITCHILD,
	DDI_CTLOPS_REPORTDEV,
	DDI_CTLOPS_REPORTINT,
	DDI_CTLOPS_REGSIZE,
	DDI_CTLOPS_NREGS,
	DDI_CTLOPS_NINTRS,
	DDI_CTLOPS_SIDDEV,
	DDI_CTLOPS_SLAVEONLY,
	DDI_CTLOPS_AFFINITY,
	DDI_CTLOPS_IOMIN,
	DDI_CTLOPS_PTOB,
	DDI_CTLOPS_BTOP,
	DDI_CTLOPS_BTOPR,
	DDI_CTLOPS_POKE_INIT,
	DDI_CTLOPS_POKE_FLUSH,
	DDI_CTLOPS_POKE_FINI,
	DDI_CTLOPS_INTR_HILEVEL,
	DDI_CTLOPS_XLATE_INTRS,
	DDI_CTLOPS_DVMAPAGESIZE,
	DDI_CTLOPS_POWER
} ddi_ctl_enum_t;

/*
 * For source compatability, we define the following obsolete code:
 * Do NOT use this, use the real constant name.
 */
#define	DDI_CTLOPS_REMOVECHILD	DDI_CTLOPS_UNINITCHILD

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_NEXUSDEFS_H */
