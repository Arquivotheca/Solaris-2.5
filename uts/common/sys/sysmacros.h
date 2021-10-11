/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_SYSMACROS_H
#define	_SYS_SYSMACROS_H

#pragma ident	"@(#)sysmacros.h	1.22	94/05/22 SMI"	/* SVr4 11.14 */

#include <sys/param.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Some macros for units conversion
 */
/*
 * Disk blocks (sectors) and bytes.
 */
#define	dtob(DD)	((DD) << DEV_BSHIFT)
#define	btod(BB)	(((BB) + DEV_BSIZE - 1) >> DEV_BSHIFT)
#define	btodt(BB)	((BB) >> DEV_BSHIFT)
#define	lbtod(BB)	(((offset_t)(BB) + DEV_BSIZE - 1) >> DEV_BSHIFT)
/*
 * Disk blocks (sectors) and pages.
 */
/* Clicks (MMU PAGES) to disk blocks */
#define	ctod(x)		mmu_ptod(x)

/* clicks (MMU PAGES) to bytes */
#define	ctob(x)		mmu_ptob(x)

/* bytes to clicks (MMU PAGES), with rounding and without */
#define	btoc(x)		mmu_btopr(x)
#define	btoct(x)	mmu_btop(x)

/* common macros */

#define	MIN(a, b)	((a) < (b) ? (a) : (b))
#define	MAX(a, b)	((a) < (b) ? (b) : (a))

/*
 * Convert a single byte to/from binary-coded decimal (BCD).
 */
extern unsigned char byte_to_bcd[256];
extern unsigned char bcd_to_byte[256];

#define	BYTE_TO_BCD(x)	byte_to_bcd[(x) & 0xff]
#define	BCD_TO_BYTE(x)	bcd_to_byte[(x) & 0xff]

/*
 * WARNING: The device number macros defined here should not be used by device
 * drivers or user software. Device drivers should use the device functions
 * defined in the DDI/DKI interface (see also ddi.h). Application software
 * should make use of the library routines available in makedev(3). A set of
 * new device macros are provided to operate on the expanded device number
 * format supported in SVR4. Macro versions of the DDI device functions are
 * provided for use by kernel proper routines only. Macro routines bmajor(),
 * major(), minor(), emajor(), eminor(), and makedev() will be removed or
 * their definitions changed at the next major release following SVR4.
 */

#define	O_BITSMAJOR	7	/* # of SVR3 major device bits */
#define	O_BITSMINOR	8	/* # of SVR3 minor device bits */
#define	O_MAXMAJ	0x7f	/* SVR3 max major value */
#define	O_MAXMIN	0xff	/* SVR3 max major value */


#define	L_BITSMAJOR	14	/* # of SVR4 major device bits */
#define	L_BITSMINOR	18	/* # of SVR4 minor device bits */
#define	L_MAXMAJ	0x3fff	/* SVR4 max major value */
#define	L_MAXMIN	0x3ffff	/* MAX minor for 3b2 software drivers. */
				/* For 3b2 hardware devices the minor is */
				/* restricted to 256 (0-255) */

#ifdef _KERNEL

/* major part of a device internal to the kernel */

#define	major(x)	(int)((unsigned)((x)>>O_BITSMINOR) & O_MAXMAJ)
#define	bmajor(x)	(int)((unsigned)((x)>>O_BITSMINOR) & O_MAXMAJ)

/* get internal major part of expanded device number */

#define	getmajor(x)	(int)((unsigned)((x)>>L_BITSMINOR) & L_MAXMAJ)

/* minor part of a device internal to the kernel */

#define	minor(x)	(int)((x) & O_MAXMIN)

/* get internal minor part of expanded device number */

#define	getminor(x)	(int)((x) & L_MAXMIN)

#else

/* major part of a device external from the kernel (same as emajor below) */

#define	major(x)	(int)(((unsigned)(x) >> O_BITSMINOR) & O_MAXMAJ)


/* minor part of a device external from the kernel  (same as eminor below) */

#define	minor(x)	(int)((x) & O_MAXMIN)

#endif	/* _KERNEL */

/* create old device number */

#define	makedev(x, y)	(unsigned short)(((x)<<O_BITSMINOR) | ((y)&O_MAXMIN))

/* make an new device number */

#define	makedevice(x, y) (unsigned long)(((x)<<L_BITSMINOR) | ((y)&L_MAXMIN))


/*
 *   emajor() allows kernel/driver code to print external major numbers
 *   eminor() allows kernel/driver code to print external minor numbers
 */

#define	emajor(x) \
	(int)(((unsigned long)(x)>>O_BITSMINOR) > O_MAXMAJ) ? \
	    NODEV : (((unsigned long)(x)>>O_BITSMINOR)&O_MAXMAJ)

#define	eminor(x) \
	(int)((x)&O_MAXMIN)

/*
 * get external major and minor device
 * components from expanded device number
 */
#define	getemajor(x)	(int)((((unsigned long)(x)>>L_BITSMINOR) > L_MAXMAJ) ? \
			NODEV : (((unsigned long)(x)>>L_BITSMINOR)&L_MAXMAJ))
#define	geteminor(x)	(int)((x)&L_MAXMIN)


/* convert to old dev format */

#define	cmpdev(x) \
	(unsigned long)((((x)>>L_BITSMINOR) > O_MAXMAJ || \
	    ((x)&L_MAXMIN) > O_MAXMIN) ? NODEV : \
	    ((((x)>>L_BITSMINOR)<<O_BITSMINOR)|((x)&O_MAXMIN)))

/* convert to new dev format */

#define	expdev(x) \
	(unsigned long)(((((x)>>O_BITSMINOR)&O_MAXMAJ)<<L_BITSMINOR) | \
	    ((x)&O_MAXMIN))

#define	SALIGN(p)	(char *)(((int)p+(sizeof (short)-1)) & \
			    ~(sizeof (short)-1))
#define	IALIGN(p)	(char *)(((int)p+(sizeof (int)-1)) & ~(sizeof (int)-1))
#define	LALIGN(p)	(char *)(((int)p+(sizeof (long)-1)) & \
			    ~(sizeof (long)-1))

#define	SNEXT(p)		(char *)((int)p + sizeof (short))
#define	INEXT(p)		(char *)((int)p + sizeof (int))
#define	LNEXT(p)		(char *)((int)p + sizeof (long))

/*
 * Macros for counting and rounding.
 */
#define	howmany(x, y)	(((x)+((y)-1))/(y))
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))

/*
 * Macros to atomically increment/decrement a variable.  mutex and var
 * must be pointers.
 */
#define	INCR_COUNT(var, mutex) mutex_enter(mutex), (*(var))++, mutex_exit(mutex)
#define	DECR_COUNT(var, mutex) mutex_enter(mutex), (*(var))--, mutex_exit(mutex)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SYSMACROS_H */
