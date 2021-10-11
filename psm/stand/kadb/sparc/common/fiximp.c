/*
 * Copyright (c) 1991-1995, by Sun Microsystems, Inc.
 */

#pragma ident "@(#)fiximp.c	1.21	95/06/12 SMI" /* From SunOS 4.1.1 */

#include <sys/types.h>
#include <sys/machparam.h>
#include <sys/cpu.h>
#include <sys/mmu.h>
#include <sys/idprom.h>
#include <sys/promif.h>

short cputype;
u_int npmgrps;
u_int segmask;
int vac;
int vac_size;
int vac_nlines;
int vac_linesize;
int pagesize;
int use_align;
int icache_flush;
#ifdef __sparcv9
int v9flag = 1;
#else
int v9flag = 0;
#endif

extern int nwindows;
extern void mach_fiximp(void);

/*
 * Look up a property by name, and fetch its value.
 * This version of getprop fetches an int-length property
 * into the space at *ip. It returns the length of the
 * requested property; the caller should test that the
 * return value == sizeof(int) for success.
 * A non-int-length property can be fetched by getlongprop(), q.v.
 */
int
getprop(dnode_t id, char *name, caddr_t ip)
{
	int len;

	len = prom_getproplen((dnode_t)id, (caddr_t)name);
	if (len != (int)OBP_BADNODE && len != (int)OBP_NONODE)
		prom_getprop((dnode_t)id, (caddr_t)name, (caddr_t)ip);
	return (len);
}

void
fiximp(void)
{
	pstack_t *stk;
	dnode_t node;
	dnode_t sp[OBP_STACKDEPTH];
	struct idprom idp;

	if (prom_getidprom((caddr_t) &idp, sizeof (idp)) != 0)
		prom_panic("Could not read IDprom.");

	cputype = idp.id_machine;

	mach_fiximp();

	if ((cputype & CPU_ARCH) != SUN4C_ARCH) {
		/*
		 * We're either:
		 *
		 * -	an early sun4m (SUN4M_ARCH)
		 * -	a later sun4m (OBP_ARCH)
		 * -	a sun4d (OBP_ARCH)
		 * -	a sun4u (OBP_ARCH)
		 * -	some other OBP_ARCH machine
		 *
		 * OR	something completely different that'll
		 *	probably not work at all..
		 */
		if ((cputype & CPU_ARCH) != SUN4M_ARCH &&
		    (cputype & OBP_ARCH) != OBP_ARCH)
			prom_printf("Warning: unknown, non-OBP platform");

		/*
		 * What's our pagesize?
		 */
		pagesize = PAGESIZE;	/* default */
		if (prom_is_openprom()) {
			stk = prom_stack_init(sp, sizeof (sp));
			node = prom_findnode_bydevtype(prom_rootnode(),
			    OBP_CPU, stk);
			if (node != OBP_NONODE && node != OBP_BADNODE) {
				(void) getprop(node, "page-size",
				    (caddr_t)&pagesize);
			}
			prom_stack_fini(stk);
		}
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
