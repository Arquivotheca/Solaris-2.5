/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)prom_fb.c	1.11	94/11/12 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

int
prom_stdout_is_framebuffer(void)
{
	return (prom_devicetype((dnode_t) prom_stdout_node(), OBP_DISPLAY));
}
