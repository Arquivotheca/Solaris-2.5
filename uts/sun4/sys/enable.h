/*
 * Copyright (c) 1987, 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_ENABLE_H
#define	_SYS_ENABLE_H

#pragma ident	"@(#)enable.h	1.12	93/05/26 SMI"
/* From SunOS 4.1.1 sun4/enable.h 1.5 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The System Enable register controls overall
 * operation of the system.  When the system is
 * reset, the Enable register is cleared.  The
 * enable register is addressed as a byte in
 * ASI_CTL space.
 */

/*
 * Bits of the Enable Register
 */
#define	ENA_DIAG	0x01		/* r/o - diag switch, 1 = on */
#define	ENA_MONITOR	0x01		/* w/o - monitor bit */
#define	ENA_VMERESET	0x02		/* r/w - reset the vme */
#define	ENA_CACHERESET	0x04		/* r/w - reset the cache */
#define	ENA_VIDEO	0x08		/* r/w - enable video memory */
#define	ENA_CACHE	0x10		/* r/w - enable external cache */
#define	ENA_SDVMA	0x20		/* r/w - enable system DVMA */
#define	ENA_IOCACHE	0x40		/* r/w - enable i/o cache */
#define	ENA_NOTBOOT	0x80		/* r/w - non-boot state, 1 = normal */

#define	ENABLEREG	0x40000000	/* addr in ASI_CTL space */

#if defined(_KERNEL) && !defined(_ASM)

extern void on_enablereg(unsigned char);
extern void off_enablereg(unsigned char);
extern unsigned char get_enablereg(void);

#endif /* _KERNEL && !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ENABLE_H */
