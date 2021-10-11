/*
 * Copyright (c) 1991, 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)obpsym.c	1.17	95/03/14 SMI"

/*
 * This module supports callbacks from the firmware
 * such that address and name lookups can work and use kernel symbol names.
 * For example "ctrace" will provide symbolic names, if they are available.
 * Also, normal firmware name to address lookups should work, though be
 * careful with clashing kernel names, such as "startup" and "reset" which
 * may be the firmware names and *not* the kernel names.
 *
 * The module locks the symbol tables in memory, when it's installed,
 * and unlocks them when it is removed.  To install the module, you
 * may either add "set obpdebug=1" to /etc/system or just modload it.
 *
 * This file contains the actual code the does the lookups, and interfaces
 * with the kernel kobj stuff.  The transfer of data and control to/from
 * the firmware is handled in prom-dependent code.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/kobj.h>
#include <sys/promif.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/reboot.h>
#include <rpc/types.h>
#include <rpc/xdr.h>

int obpsym_debug = 0;
#define	DPRINTF(str)		if (obpsym_debug) prom_printf(str)
#define	DPRINTF1(str, a)	if (obpsym_debug) prom_printf(str, a);
#define	DPRINTF2(str, a, b)	if (obpsym_debug) prom_printf(str, a, b);
#define	DXPRINTF		if (obpsym_debug > 1) prom_printf

int obpheld = 1;		/* Prevents unloading when set */
extern int obpdebug;

/*
 * name_to_value - translate a string into an address
 *
 * The string may be one of two forms - 'symname' or 'modname:symname'.
 * (We also accept 'unix:symname' as a synonymn for 'symname'.)
 * The latter form causes a kobj_lookup() to occur only in the given module,
 * the former causes a kobj_getsymvalue() on the entire kernel symbol table.
 */
int
name_to_value(char *name, u_long *value)
{
	register char *symname = name;
	register char *modname = "";
	char *p;
	int retval = 0;
	u_long symvalue = 0;
	char c = (char)0;

	/*
	 * we take names of the form: "modname:symbol", "unix:symbol", "symbol"
	 */
	if (((p = strchr(name, ':')) != NULL) && (*(p+1) != (char)0))  {
		symname = p + 1;
		modname = name;
		c = *p;
		*p = (char)0;
	}

	if ((*modname == (char)0) || (strcmp(modname, "unix") == 0))  {
		symvalue = (u_long) kobj_getsymvalue(symname, 0);
	} else  {
		struct modctl *mp;

		for (mp = modules.mod_next; mp != &modules; mp = mp->mod_next)
			if (strcmp(modname, mp->mod_modname) == 0)
				break;

		if (mp != &modules)
			symvalue = ((u_long)kobj_lookup(mp->mod_mp, symname));
	}

	if (symvalue == 0)
		retval = -1;
	if (c != (char)0)		/* Restore incoming cstr */
		*p = c;

	*value = symvalue;
	return (retval);
}

/*
 * value_to_name - translate an address into a string + offset
 */
u_int
value_to_name(u_long value, char *symbol)
{
	struct modctl *modp;
	u_int offset;
	char *name;

	DPRINTF1("value_to_name: Looking for <%x>\n", value);

	/*
	 * Search the modules, then the primaries, so we can
	 * return "module:name" syntax to the caller, if the symbol
	 * appears in a module.
	 */
	for (modp = modules.mod_next; modp != &modules; modp = modp->mod_next) {
		if (modp->mod_mp == NULL) {
			continue;
		}
		if (name = kobj_searchsym(modp->mod_mp,
		    (u_int)value, &offset)) {
			(void) strcpy(symbol, modp->mod_modname);
			(void) strcat(symbol, ":");
			(void) strcat(symbol, name);
			return (offset);
		}
	}

	name = kobj_getsymname(value, &offset);
	if ((name == NULL) || (*name == (char) 0))  {
		*symbol = (char)0;
		return ((u_int)-1);
	}
	(void) strcpy(symbol, name);
	return (offset);
}

/*
 * loadable module wrapper
 */

#ifndef lint
char _depends_on[] = "strmod/rpcmod";
#endif lint

static struct modlmisc modlmisc = {
	&mod_miscops, "OBP symbol callbacks"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

int
_init(void)
{
	int i;
	int retval;
	extern int install_callbacks(void);
	extern void remove_callbacks(void);

	if (install_callbacks() != 0)
		return (ENXIO);

	obpdebug = 1;

	if ((i = kobj_lock_syms()) != 0)
		printf("obpsym: Error %d locking kernel symbol table\n", i);

	printf("obpsym: symbolic debugging is available.\n");

	if (boothowto & RB_HALT)
		debug_enter("obpsym: halt flag (-h) is set.\n");

	retval = mod_install(&modlinkage);

	/*
	 * if load fails remove callback and unlock symbols
	 */
	if (retval) {
		printf("obpsym: Error %d installing OBP syms module\n", retval);
		remove_callbacks();
		obpdebug = 0;
		if ((i = kobj_unlock_syms()) != 0)
		    printf("obpsym: Error %d unlocking kernel symbol table\n",
			i);
	}
	return (retval);
}

int
_fini(void)
{
	int i;
	int retval;
	extern void remove_callbacks(void);

	if (obpheld != 0)
		return (EBUSY);

	retval = mod_remove(&modlinkage);

	/*
	 * if unload succeeds remove callback and unlock symbols
	 */
	if (retval == 0) {
		remove_callbacks();
		obpdebug = 0;
		if ((i = kobj_unlock_syms()) != 0)
		    printf("obpsym: Error %d unlocking kernel symbol table\n",
			i);
	}
	return (retval);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
