/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_ISA_DEFS_H
#define	_SYS_ISA_DEFS_H

#pragma ident	"@(#)isa_defs.h	1.1	93/07/01 SMI"

/*
 * This header file serves to group a set of well known defines and to
 * set these for each instruction set architecture.  These defines may
 * be divided into two groups;  characteristics of the processor and
 * implementation choices for Solaris on a processor.
 *
 * Processor Characteristics:
 *
 * _LITTLE_ENDIAN / _BIG_ENDIAN:
 *	The natural byte order of the processor.  A pointer to an int points
 *	to the least/most significant byte of that int.
 *
 * _STACK_GROWS_UPWARD / _STACK_GROWS_DOWNWARD:
 *	The processor specific direction of stack growth.  A push onto the
 *	stack increases/decreases the stack pointer, so it stores data at
 *	successively higher/lower addresses.  (Stackless machines ignored
 *	without regrets).
 *
 * _LONG_LONG_HTOL / _LONG_LONG_LTOH:
 *	A pointer to a long long points to the most/least significant long
 *	within that long long.
 *
 * _BIT_FIELDS_HTOL / _BIT_FIELDS_LTOH:
 *	The C compiler assigns bit fields from the high/low to the low/high end
 *	of an int (most to least significant vs. least to most significant).
 *
 * _IEEE_754:
 *	The processor (or supported implementations of the processor)
 *	supports the ieee-754 floating point standard.  No other floating
 *	point standards are supported (or significant).  Any other supported
 *	floating point formats are expected to be cased on the ISA processor
 *	symbol.
 *
 * _CHAR_IS_UNSIGNED / _CHAR_IS_SIGNED:
 *	The C Compiler implements objects of type `char' as `unsigned' or
 *	`signed' respectively.  This is really an implementation choice of
 *	the compiler writer, but it is specified in the ABI and tends to
 *	be uniform across compilers for an instruction set architecture.
 *	Hence, it has the properties of a processor characteristic.
 *
 * Implementation Choices:
 *
 * _SUNOS_VTOC_8 / _SUNOS_VTOC_16 / _SVR4_VTOC_16:
 *	This specifies the form of the disk VTOC (or label):
 *
 *	_SUNOS_VTOC_8:
 *		This is a VTOC form which is upwardly compatible with the
 *		SunOS 4.x disk label and allows 8 partitions per disk.
 *
 *	_SUNOS_VTOC_16:
 *		This VTOC form is virtually identical to _SUNOS_VTOC_8
 *		except that it allows 16 partitions per disk.  It isn't
 *		compatible with the SunOS 4.x disk label.
 *
 *	Note that these are not the only two VTOC forms possible and
 *	additional forms may be added.  One possible form would be the
 *	SVr4 VTOC form.  The symbol for that is reserved now, although
 *	it is not implemented.
 *
 *	_SVR4_VTOC_16:
 *		This VTOC form is compatible with the System V Release 4
 *		VTOC (as implemented on the SVr4 Intel and 3b ports) with
 *		16 partitions per disk.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The following set of definitions characterize the Solaris on Intel systems.
 *
 * The feature test macro __i386 is generic for all processors implementing
 * the Intel 386 instruction set or a superset of it.  Specifically, this
 * includes all members of the 386, 486, and Pentium family of processors.
 */
#if defined(__i386) || defined(i386)

/*
 * Make sure that the ANSI-C "politically correct" symbol is defined.
 */
#if !defined(__i386)
#define	__i386
#endif

/*
 * Define the appropriate "processor characteristics"
 */
#define	_LITTLE_ENDIAN
#define	_STACK_GROWS_DOWNWARD
#define	_LONG_LONG_LTOH
#define	_BIT_FIELDS_LTOH
#define	_IEEE_754
#define	_CHAR_IS_SIGNED

/*
 * Define the appropriate "implementation choices".
 */
#define	_SUNOS_VTOC_16


/*
 * The following set of definitions characterize the Solaris on SPARC system.
 *
 * The flag __sparc is only guaranteed to indicate SPARC processors version 8
 * or earlier.
 */
#elif defined(__sparc) || defined(sparc)

/*
 * Make sure that the ANSI-C "politically correct" symbol is defined.
 */
#if !defined(__sparc)
#define	__sparc
#endif

/*
 * Define the appropriate "processor characteristics"
 */
#define	_BIG_ENDIAN
#define	_STACK_GROWS_DOWNWARD
#define	_LONG_LONG_HTOL
#define	_BIT_FIELDS_HTOL
#define	_IEEE_754
#define	_CHAR_IS_SIGNED

/*
 * Define the appropriate "implementation choices".
 */
#define	_SUNOS_VTOC_8


/*
 * #error is strictly ansi-C, but works as well as anything for K&R systems.
 */
#else
#error ISA not supported
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ISA_DEFS_H */
