/*
 * Copyright (c) 1989-1991, 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_OPENPROMIO_H
#define	_SYS_OPENPROMIO_H

#pragma ident	"@(#)openpromio.h	1.9	95/05/25 SMI"
/* From SunOS 4.1.1 <sundev/openpromio.h> */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * XXX HACK ALERT
 *
 * You might think that this interface could support setting non-ASCII
 * property values.  Unfortunately the 4.0.3c openprom driver SETOPT
 * code ignores oprom_size and uses strlen() to compute the length of
 * the value.  The 4.0.3c openprom eeprom command makes its contribution
 * by not setting oprom_size to anything meaningful.  So, if we want the
 * driver to trust oprom_size we have to use SETOPT2.  XXX.
 */
struct openpromio {
	u_int	oprom_size;		/* real size of following array */
	char	oprom_array[1];		/* For property names and values */
					/* NB: Adjacent, Null terminated */
};

/*
 * OPROMMAXPARAM is used as a limit by the driver, and it has been
 * increased to be 4 times the largest possible size of a property,
 * which is 8K (nvramrc property).
 */
#define	OPROMMAXPARAM	32768		/* max size of array */

/*
 * Note that all OPROM ioctl codes are type void. Since the amount
 * of data copied in/out may (and does) vary, the openprom driver
 * handles the copyin/copyout itself.
 */
#define	OIOC	('O'<<8)
#define	OPROMGETOPT	(OIOC | 1)
#define	OPROMSETOPT	(OIOC | 2)
#define	OPROMNXTOPT	(OIOC | 3)
#define	OPROMSETOPT2	(OIOC | 4)	/* working OPROMSETOPT */
#define	OPROMNEXT	(OIOC | 5)	/* interface to raw config_ops */
#define	OPROMCHILD	(OIOC | 6)	/* interface to raw config_ops */
#define	OPROMGETPROP	(OIOC | 7)	/* interface to raw config_ops */
#define	OPROMNXTPROP	(OIOC | 8)	/* interface to raw config_ops */
#define	OPROMU2P	(OIOC | 9)	/* NOT SUPPORTED after 4.x */
#define	OPROMGETCONS	(OIOC | 10)	/* enquire which console device */
#define	OPROMGETFBNAME	(OIOC | 11)	/* Frame buffer OBP pathname */
#define	OPROMGETBOOTARGS (OIOC | 12)	/* Get boot arguments */
#define	OPROMGETVERSION	(OIOC | 13)	/* Get OpenProm Version string */

/*
 * Return values from OPROMGETCONS:
 */

#define	OPROMCONS_NOT_WSCONS	0
#define	OPROMCONS_STDIN_IS_KBD	0x1	/* stdin device is kbd */
#define	OPROMCONS_STDOUT_IS_FB	0x2	/* stdout is a framebuffer */
#define	OPROMCONS_OPENPROM	0x4	/* supports openboot */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_OPENPROMIO_H */
