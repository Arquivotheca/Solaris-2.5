/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)collate.c	1.5	95/07/11 SMI"	/* SVr4.0 1.9	*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int _collate_xpg = 0;
int _collate_category_changed = 1;

int
setup_collate(char *loc)
{
	int fd = -1;
	char buf[1024];

#ifdef DBG
printf("setup_collate called(%s)\n",
	loc);
#endif
	/*
	 * Mark that collate category channged.
	 */
	_collate_category_changed = 1;

	/*
	 * coll.so ?
	 */
	sprintf(buf, "/usr/lib/locale/%s/LC_COLLATE/", loc);
	strcat(buf, "coll.so");
	if (access(buf, R_OK|F_OK) == 0) {
		/*
		 * coll.so exists. Wants to use .so method.
		 */
		_collate_xpg = 0;
#ifdef DEBUG
printf("GOING TO USE coll.so,,,\n");
#endif
		return (0);
	}

	/*
	 * CollTable ?
	 */
	sprintf(buf, "/usr/lib/locale/%s/LC_COLLATE/", loc);
	strcat(buf, "CollTable");
	if (access(buf, R_OK|F_OK) == 0) {
		/*
		 * CollTable  exists. Don't use .so method.
		 */
		_collate_xpg = 1;
#ifdef DEBUG
printf("GOING TO USE CollTable,,,\n");
#endif
		return (0);
	}

	return (-1);
}

_is_xpg_collate()
{
	if (_collate_xpg)
		return (1);
	else
		return (0);
}
