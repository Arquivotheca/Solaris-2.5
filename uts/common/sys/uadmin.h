/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_UADMIN_H
#define	_SYS_UADMIN_H

#pragma ident	"@(#)uadmin.h	1.19	94/04/19 SMI"	/* SVr4.0 11.7 */

#ifdef	__cplusplus
extern "C" {
#endif

#define	A_REBOOT	1
#define	A_SHUTDOWN	2
#define	A_REMOUNT	4
#define	A_FREEZE	3	/* For freeze and thaw */
#define	A_SWAPCTL	16
/*			17-21	   reserved for obsolete interface */

#define	AD_HALT		0	/* halt the processor */
#define	AD_BOOT		1	/* multi-user reboot of /unix */
#define	AD_IBOOT	2	/* multi-user reboot, ask for name of file */
#define	AD_SBOOT	3	/* single-user reboot of /unix */
#define	AD_SIBOOT	4	/* single-user reboot, ask for name of file */
#define	AD_PANIC	5	/* multi-user reboot of /unix after crash */
				/* dump */
#define	AD_POWEROFF	6	/* software poweroff */

/* functions reserved for A_FREEZE (may not be available on all platforms */
#define	AD_COMPRESS	0	/* store state file compressed during CPR */
#define	AD_FORCE	1	/* force to do AD_COMPRESS */
#define	AD_CHECK	2	/* test if CPR module is available */

#if defined(_KERNEL)
extern void mdboot(int, int, char *);
#endif

#if defined(__STDC__) && !defined(_KERNEL)
int uadmin(int, int, int);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UADMIN_H */
