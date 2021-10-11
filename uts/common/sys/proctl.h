/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1987, 1988 Microsoft Corporation	*/
/*	  All Rights Reserved	*/

/*	This Module contains Proprietary Information of Microsoft  */
/*	Corporation and should be treated as Confidential.	   */

#ifndef	_SYS_PROCTL_H
#define	_SYS_PROCTL_H

#pragma ident	"@(#)proctl.h	1.9	92/07/14 SMI"	/* SVr4.0 1.2 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * THIS FILE CONTAINS CODE WHICH IS DESIGNED TO BE
 * PORTABLE BETWEEN DIFFERENT MACHINE ARCHITECTURES
 * AND CONFIGURATIONS. IT SHOULD NOT REQUIRE ANY
 * MODIFICATIONS WHEN ADAPTING XENIX TO NEW HARDWARE.
 */

/* proctl() requests */

#define	PRHUGEX		1	/* allow process > swapper size to execute */
#define	PRNORMEX 	2	/* remove PRHUGEX permission */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PROCTL_H */
