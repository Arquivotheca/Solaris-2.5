/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_MKDEV_H
#define	_SYS_MKDEV_H

#pragma ident	"@(#)mkdev.h	1.14	93/10/13 SMI"	/* SVr4.0 1.6	*/

#ifdef	__cplusplus
extern "C" {
#endif

/* SVR3 device number constants */

#define	ONBITSMAJOR	7	/* # of SVR3 major device bits */
#define	ONBITSMINOR	8	/* # of SVR3 minor device bits */
#define	OMAXMAJ		0x7f	/* SVR3 max major value */
#define	OMAXMIN		0xff	/* SVR3 max major value */


#define	NBITSMAJOR	14	/* # of SVR4 major device bits */
#define	NBITSMINOR	18	/* # of SVR4 minor device bits */
#define	MAXMAJ		0x3fff	/* SVR4 max major value */
#define	MAXMIN		0x3ffff	/* SVR4 max minor value */

#if !defined(_KERNEL)

/* undefine sysmacros.h device macros */

#undef makedev
#undef major
#undef minor

#if defined(__STDC__)

dev_t makedev(const major_t, const minor_t);
major_t major(const dev_t);
minor_t minor(const dev_t);
dev_t __makedev(const int, const major_t, const minor_t);
major_t __major(const int, const dev_t);
minor_t __minor(const int, const dev_t);

#else

dev_t makedev();
major_t major();
minor_t minor();
dev_t __makedev();
major_t __major();
minor_t __minor();

#endif	/* defined(_STDC_) */

#define	OLDDEV 0	/* old device format */
#define	NEWDEV 1	/* new device format */

#define	makedev(maj, min)	(__makedev(NEWDEV, maj, min))
#define	major(dev)		(__major(NEWDEV, dev))
#define	minor(dev)		(__minor(NEWDEV, dev))

#endif	/* !defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MKDEV_H */
