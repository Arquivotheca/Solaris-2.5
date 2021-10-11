/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_DEVADDR_H
#define	_SYS_DEVADDR_H

#pragma ident	"@(#)devaddr.h	1.30	93/10/20 SMI"

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/physaddr.h>
#include <sys/cpu.h>
#include <sys/vm_machparam.h>

/*
 * Fixed virtual addresses.
 * Allocated from the top of the
 * available virtual address space
 * and working down.
 */

#define	MX_SUN4D_BRDS		(10)
#define	MX_XDBUS		(2)
#define	XDB_OFFSET		(0x100)

/*
 * number of pages needed for "well known" addresses
 *	- only segkmap.
 */
#define	NWKPGS			(mmu_btop(SEGMAPSIZE))

/*
 * The base address of well known addresses, offset from PPMAPBASE
 * and rounded down so the bits will be right for the AGETCPU
 * macro (currenlty only used by sun4m).
 */
#define	V_WKBASE_ADDR		((PPMAPBASE - (NWKPGS * MMU_PAGESIZE)))
#define	V_SEGMAP_ADDR		(PPMAPBASE - (SEGMAPSIZE))

/*
 * msgbuf is at the very beginning of kernelmap. This way it can still
 * be seen by libkvm. Note, we don't use SYSBASE page so we start with
 * SYSBASE + MMU_PAGESIZE.
 */
#define	V_MSGBUF_ADDR		(SYSBASE + MMU_PAGESIZE)

/* compatibility */
/* FIXME: these needs to be revisited */
#define	V_EEPROM_ADDR		0
#define	V_EEPROM_PGS		0x2		/* Pages needed for EEPROM */

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_DEVADDR_H */
