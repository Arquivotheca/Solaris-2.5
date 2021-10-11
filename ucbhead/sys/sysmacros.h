/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sysmacros.h	1.1	90/04/27 SMI"	/* SVr4.0 1.3	*/

/*******************************************************************

		PROPRIETARY NOTICE (Combined)

This source code is unpublished proprietary information
constituting, or derived under license from AT&T's UNIX(r) System V.
In addition, portions of such source code were derived from Berkeley
4.3 BSD under license from the Regents of the University of
California.



		Copyright Notice 

Notice of copyright on this source code product does not indicate 
publication.

	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
	          All rights reserved.
********************************************************************/ 

/*
 * Major/minor device constructing/busting macros.
 */

#ifndef _SYS_SYSMACROS_H
#define _SYS_SYSMACROS_H

/* major part of a device */
#define       major(x)        ((int)(((unsigned)(x)>>8)&0377))

/* minor part of a device */
#define       minor(x)        ((int)((x)&0377))

/* make a device number */
#define       makedev(x,y)    ((dev_t)(((x)<<8) | (y)))

#endif /*!_SYS_SYSMACROS_H*/
