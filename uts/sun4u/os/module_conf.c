/*
 * Copyright (c) 1990-1993 by Sun Microsystems, Inc.
 */

#ident	"@(#)module_conf.c	1.1	93/07/29 SMI"

#include <sys/machparam.h>
#include <sys/module.h>

/*
 * add extern declarations for module drivers here
 */

extern int  	spitfire_module_identify();
extern void	spitfire_module_setup();

/*
 * module driver table
 */

struct module_linkage module_info[] = {
	{ spitfire_module_identify, spitfire_module_setup },

/*
 * add module driver entries here
 */
};

int	module_info_size = sizeof (module_info) / sizeof (module_info[0]);
