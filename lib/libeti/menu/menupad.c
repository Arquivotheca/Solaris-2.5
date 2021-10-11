/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)menupad.c	1.2	92/07/14 SMI"	/* SVr4.0 1.1	*/

#include "private.h"
#include <ctype.h>

int
set_menu_pad (m, pad)
register MENU *m;
register int pad;
{
  if (!isprint(pad)) {
    return E_BAD_ARGUMENT;
  }
  if (m) {
    Pad(m) = pad;
    if (Posted(m)) {
      _draw (m);		/* Redraw menu */
      _show (m);		/* Display menu */
    }
  }
  else {
    Pad(Dfl_Menu) = pad;
  }
  return E_OK;
}

int
menu_pad(m)
register MENU *m;
{
  return Pad(m ? m : Dfl_Menu);
}
