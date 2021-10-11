/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ypsym.h	1.7	92/07/14 SMI"        /* SVr4.0 1.1   */

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*	PROPRIETARY NOTICE (Combined)
*
* This source code is unpublished proprietary information
* constituting, or derived under license from AT&T's UNIX(r) System V.
* In addition, portions of such source code were derived from Berkeley
* 4.3 BSD under license from the Regents of the University of
* California.
*
*
*
*	Copyright Notice 
*
* Notice of copyright on this source code product does not indicate 
*  publication.
*
*	(c) 1986,1987,1988,1989,1990  Sun Microsystems, Inc
*	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
*          All rights reserved.
*/ 

/*
 * This contains symbol and structure definitions for modules in the YP server 
 */

#include <ndbm.h>			/* Pull this in first */
#define DATUM
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <rpc/rpc.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>

typedef void (*PFV)();
typedef int (*PFI)();
typedef unsigned int (*PFU)();
typedef long int (*PFLI)();
typedef unsigned long int (*PFULI)();
typedef short int (*PFSI)();
typedef unsigned short int (*PFUSI)();

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifdef NULL
#undef NULL
#endif
#define NULL 0

/* Maximum length of a yp map name in the system v filesystem */
#define MAXALIASLEN 8

#define YPINTERTRY_TIME 10		/* Secs between tries for peer bind */
#define YPTOTAL_TIME 30			/* Total secs until timeout */
#define YPNOPORT ((unsigned short) 0)	/* Out-of-range port value */

/* External refs to yp server data structures */

extern bool ypinitialization_done;
extern struct timeval ypintertry;
extern struct timeval yptimeout;
extern char myhostname[];

/* External refs to yp server-only functions */

extern bool ypcheck_map_existence();
extern DBM *ypset_current_map();
extern void ypclr_current_map();
extern bool ypbind_to_named_server();
extern void ypmkfilename();
extern int yplist_maps();


/* definitions for reading files of lists */

struct listofnames
{
	struct listofnames *nextname;
	char *name;
};
typedef struct listofnames listofnames;

/* XXX- NAME_MAX can't be defined in <limits.h> in a POSIX conformant system
 *	(under conditions which apply to Sun systems). Removal of this define
 *	caused yp to break (and only yp!). Hence, NAME_MAX is defined here
 *	*exactly* as it was in <limits.h>. I suspect this may not be the
 *	desired value. I suspect the desired value is either:
 *		- the maxumum name length for any file system type, or
 *		- should be _POSIX_NAME_MAX which is the minimum-maximum name
 *		  length in a POSIX conformant system (which just happens to
 *		  be 14), or
 *		- should be gotten by pathconf() or fpathconf().
 * XXX- I leave this to the owners of yp!
 */
#define	NAME_MAX	14	/* s5 file system maximum name length */

