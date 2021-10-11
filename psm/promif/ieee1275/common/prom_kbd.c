/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)prom_kbd.c	1.8	94/11/12 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

/*
 * Returns true is the stdin device exports the "keyboard" property.
 * XXX: The "keyboard" property is not part of the IEEE 1275 standard.
 * XXX: Perhaps this belongs in platform dependent directories?
 */

int
prom_stdin_is_keyboard(void)
{
	int i;

	i = prom_getproplen(prom_stdin_node(), "keyboard");
	return (i == -1 ? 0 : 1);
}
