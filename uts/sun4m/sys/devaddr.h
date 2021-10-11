/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

/*
 * This file contains device addresses for the architecture.
 *
 * XXX	The very existence of this file is a bug.
 */

#ifndef _SYS_DEVADDR_H
#define	_SYS_DEVADDR_H

#pragma ident	"@(#)devaddr.h	1.29	95/03/07 SMI"

#include <sys/mmu.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The base address of well known addresses, offset from PPMAPBASE
 * and rounded down so the bits will be right for the CPU_INDEX
 * macro.
 */
#define	BIG_OBP_MAP		(0xfe400000)
#define	BIG_OBP_MAP_END		(0xfef00000)
#define	DEBUGADDR		(0xfbd00000)
#define	DEBUGADDR_END		(0xfbe00000)
#define	MONSTART		(0xffd00000)

#define	V_WKBASE_ADDR		(BIG_OBP_MAP_END)

#define	V_TBR_ADDR_BASE		(V_WKBASE_ADDR + (0 * MMU_PAGESIZE))
#define	V_TBR_ADDR_CPU1		(V_WKBASE_ADDR + (1 * MMU_PAGESIZE))
#define	V_TBR_ADDR_CPU2		(V_WKBASE_ADDR + (2 * MMU_PAGESIZE))
#define	V_TBR_ADDR_CPU3		(V_WKBASE_ADDR + (3 * MMU_PAGESIZE))
#define	V_TBR_WR_ADDR		(V_WKBASE_ADDR + (4 * MMU_PAGESIZE))

#define	V_MX_SEGKP		(BIG_OBP_MAP - DEBUGADDR_END)

/*
 * msgbuf is logically at the beginning of kernelmap; residing in the
 * kernelmap segment allows it to be seen by libkvm and thus adb.  Note,
 * we don't use SYSBASE page zero so we start with SYSBASE + MMU_PAGESIZE.
 */
#define	V_MSGBUF_ADDR		(SYSBASE + MMU_PAGESIZE)

#ifndef _ASM
extern caddr_t v_eeprom_addr;
#define	V_CLK1ADDR		(v_eeprom_addr+0x1FF8)
extern caddr_t v_vmectl_addr[];
#define	VMEBUS_ICR		((u_int *) v_vmectl_addr[0])
/* XXX depend on the order the prom maps the two VME registers */
#endif

/*
 * specify the per-cpu i/o mapping layout
 */
#define	PC_intregs	0x1000
#define	PC_utimers	0x1000

/* VMEbus Interface control register mask defines */
#define	VMEICP_IOC	0x80000000	/* IOCache enable */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DEVADDR_H */
