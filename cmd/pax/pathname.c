/*
 *		    (c) 1994  Sun Microsystems, Inc.
 *			   All Rights Reserved
 */

#ident	"@(#)pathname.c	1.3	94/12/06 SMI"

/*
 * COPYRIGHT NOTICE
 * 
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 * 
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED 
 */
/*
 * OSF/1 1.2
 */
#if !defined(lint) && !defined(_NOIDENT)
static char rcsid[] = "@(#)$RCSfile: pathname.c,v $ $Revision: 1.2.2.2 $ (OSF) $Date: 1991/10/01 15:55:03 $";
#endif
/* 
 * pathname.c - directory/pathname support functions 
 *
 * DESCRIPTION
 *
 *	These functions provide directory/pathname support for PAX
 *
 * AUTHOR
 *
 *	Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution. 
 *
 * Copyright (c) 1989 Mark H. Colburn.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice is duplicated in all such 
 * forms and that any documentation, advertising materials, and other 
 * materials related to such distribution and use acknowledge that the 
 * software was developed * by Mark H. Colburn and sponsored by The 
 * USENIX Association. 
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Revision 1.2  89/02/12  10:05:13  mark
 * 1.2 release fixes
 * 
 * Revision 1.1  88/12/23  18:02:21  mark
 * Initial revision
 * 
 */

/* Headers */

#include "pax.h"
#include <libgen.h>

/* Messages */

#define	PN_TOOMANY	 "The pathname contains too many elements."

/* dirneed  - checks for the existance of directories and possibly create
 *
 * DESCRIPTION
 *
 *	Dirneed checks to see if a directory of the name pointed to by name
 *	exists.  If the directory does exist, then dirneed returns 0.  If
 *	the directory does not exist and the f_dir_create flag is set,
 *	then dirneed will create the needed directory, recursively creating
 *	any needed intermediate directory.
 *
 *	If f_dir_create is not set, then no directories will be created
 *	and a value of -1 will be returned if the directory does not
 *	exist.
 *
 * PARAMETERS
 *
 *	name		- name of the directory to create
 *
 * RETURNS
 *
 *	Returns a 0 if the creation of the directory succeeded or if the
 *	directory already existed.  If the f_dir_create flag was not set
 *	and the named directory does not exist, or the directory creation 
 *	failed, a -1 will be returned to the calling routine.
 */


int dirneed(char *name)

{
    char           *cp;
    char           *last;
    int             ok;
    static Stat     sb;

    last = (char *)NULL;
    for (cp = name; *cp;) {
	if (*cp++ == '/') {
	    last = cp;
	}
    }
    if (last == (char *)NULL) {
	return (STAT(".", &sb));
    }
    *--last = '\0';
    ok = STAT(*name ? name : ".", &sb) == 0
	? ((sb.sb_mode & S_IFMT) == S_IFDIR)
	: (f_dir_create && mkdirp(name, 0777) == 0);
    *last = '/';
    return (ok ? 0 : -1);
}


/* nameopt - optimize a pathname
 *
 * DESCRIPTION
 *
 * 	Confused by "<symlink>/.." twistiness. Returns the number of final 
 * 	pathname elements (zero for "/" or ".") or -1 if unsuccessful. 
 *
 * PARAMETERS
 *
 *	char	*begin	- name of the path to optimize
 *
 * RETURNS
 *
 *	Returns 0 if successful, non-zero otherwise.
 *
 */


int nameopt(char *begin)

{
    char           *name;
    char           *item;
    int             idx;
    int             absolute;
    char           *element[PATHELEM];

    absolute = (*(name = begin) == '/');
    idx = 0;
    for (;;) {
	if (idx == PATHELEM) {
	    warn(begin, MSGSTR(PN_TOOMANY, "Too many elements"));
	    return (-1);
	}
	while (*name == '/') {
	    ++name;
	}
	if (*name == '\0') {
	    break;
	}
	element[idx] = item = name;
	while (*name && *name != '/') {
	    ++name;
	}
	if (*name) {
	    *name++ = '\0';
	}
	if (strcmp(item, "..") == 0) {
	    if (idx == 0) {
		if (!absolute) {
		    ++idx;
		}
	    } else if (strcmp(element[idx - 1], "..") == 0) {
		++idx;
	    } else {
		--idx;
	    }
	} else if (strcmp(item, ".") != 0) {
	    ++idx;
	}
    }
    if (idx == 0) {
	element[idx++] = absolute ? "" : "."; 
    }
    element[idx] = (char *)NULL;
    name = begin;
    if (absolute) {
	*name++ = '/';
    }
    for (idx = 0; item = element[idx]; ++idx, *name++ = '/') {
	while (*item) {
	    *name++ = *item++;
	}
    }
    *--name = '\0';
    return (idx);
}


/* dirmake - make a directory  
 *
 * DESCRIPTION
 *
 *	Dirmake makes a directory with the appropritate permissions.
 *
 * PARAMETERS
 *
 *	char 	*name	- Name of directory make
 *	Stat	*asb	- Stat structure of directory to make
 *
 * RETURNS
 *
 * 	Returns zero if successful, -1 otherwise. 
 *
 */


int dirmake(char *name, Stat *asb)

{
    if (mkdir(name, (int) (asb->sb_mode & S_IPOPN)) < 0) {
	return (-1);
    }
    chmod(name, ((asb->sb_mode & S_IPERM) | S_IWUSR));
    return (0);
}
