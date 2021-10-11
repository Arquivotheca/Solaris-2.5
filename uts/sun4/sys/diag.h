/*
 * Copyright (c) 1985-1990 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_DIAG_H
#define	_SYS_DIAG_H

#pragma ident	"@(#)diag.h	1.10	93/02/04 SMI"
/* From SunOS 4.1.1 sun4/diag.h 1.3 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The diagnostic register drvies an 8-bit LED display.
 * This register is addressed in ASI_CTL space and is write only.
 * A "0" bit written will cause the corresponding LED to light up,
 * a "1" bit to be dark.
 */
#define	DIAGREG		0x70000000	/* addr of diag reg in ASI_CTL space */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DIAG_H */
