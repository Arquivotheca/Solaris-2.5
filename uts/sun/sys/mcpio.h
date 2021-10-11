/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MCPIO_H
#define	_SYS_MCPIO_H

#pragma ident	"@(#)mcpio.h	1.5	94/01/06 SMI"

/*
 * Ioctl definitions for Sun MCP (ALM-2)
 */

#include <sys/ioccom.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	MCPIOSPR	_IOWR('M', 1, u_char)	/* Ioctl to set Printer Diag */
#define	MCPIOGPR	_IOR('M', 2, u_char)	/* Ioctl to get Printer Diag */

#define	MCPRIGNSLCT	0x02	/* set = ignore hardware slct sig */
#define	MCPRDIAG	0x04	/* set = activate diag mode */
#define	MCPRVMEINT	0x08	/* set = VME interrupts enabled */
#define	MCPRINTPE	0x10	/* set = interrupt on pe false */
#define	MCPRINTSLCT	0x20	/* set = interrupt on slct false */
#define	MCPRPE		0x40	/* set = printer ok, clr = paper out */
#define	MCPRSLCT	0x80	/* set = printer online */

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_MCPIO_H */
