/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ident	"@(#)fiximp.c	1.24	95/07/18 SMI" /* From SunOS 4.1.1 */

#include <sys/types.h>
#include <sys/machparam.h>
#include <sys/cpu.h>
#include <sys/idprom.h>
#include <sys/promif.h>

int debug_prop = 0;		/* Turn on to enable debugging message */

extern int cache_state;		/* 0 => leave it alone! */


void
fiximp(void)
{
	pstack_t *stk;
	dnode_t node;
	dnode_t sp[OBP_STACKDEPTH];
	struct idprom idp;
	extern use_align;

	/* Don't bother testing the IDFORM.  Who really cares? */
	if (prom_getidprom((caddr_t) &idp, sizeof (idp)) != 0)
		prom_panic("Could not read IDprom.  Exiting.");

	cputype = idp.id_machine;

	switch (cputype & CPU_ARCH) {

	case SUN4C_ARCH:
		fiximp_sun4c(cputype);
		break;
	case SUN4M_ARCH:
#ifdef sun4m
		fiximp_sun4m(cputype);
#endif

#ifndef	ALL_BROKEN_PROMS_REALLY_GONE
	{
		auto int rmap;
		prom_interpret("h# f800.0000 rmap@ swap ! ",
			(int)&rmap, 0, 0, 0, 0);
		/*
		 * If this region is mapped, it means you have a
		 * preFCS PROM in your 4/6xx machine.  You should
		 * get it updated (or we should put back the workaround
		 * from fiximp_sun4m.c)
		 */
		if (rmap)
			prom_printf("Warning: f8 region already mapped!\n");
	}
#endif
		/* FALL THROUGH */
	default:
		cache_state = 0;
		vac = 0;
		break;
	}

	/*
	 * Can we make aligned memory requests?
	 */
	use_align = 0;
	if (prom_is_openprom()) {
		stk = prom_stack_init(sp, sizeof (sp));
		node = prom_findnode_byname(prom_rootnode(), "openprom", stk);
		if (node != OBP_NONODE && node != OBP_BADNODE) {
			if (prom_getproplen(node, "aligned-allocator") == 0)
				use_align = 1;
		}
		prom_stack_fini(stk);
	}
}
