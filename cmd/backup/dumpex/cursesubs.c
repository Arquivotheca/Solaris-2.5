#ident "@(#)cursesubs.c 1.3 92/03/06"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include <curses.h>

void
nocurses()
{
	if (curseson) {
		noraw();
		echo();
		refresh();
		endwin();
	}
}
