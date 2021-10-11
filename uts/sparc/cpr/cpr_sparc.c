/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)cpr_sparc.c 1.33     94/02/14 SMI"

/*
 * This module contains functions that only available to sparc platform.
 */

#include <sys/types.h>
#include <sys/cpr.h>

#ifdef sparc

void
cpr_console_clear()
{
	prom_printf("\033[p");
}

void
cpr_get_bootinfo(char *default_bootfile)
{
	dnode_t node;
	dnode_t sp[OBP_STACKDEPTH];
	pstack_t *stk;
	int bootf_len;


	default_bootfile[0] = 0; /* terminate it first */
	stk = prom_stack_init(sp, sizeof (sp));
	node = prom_findnode_byname(prom_nextnode(0), "options", stk);
	prom_stack_fini(stk);

	if ((node == OBP_NONODE) || (node == OBP_BADNODE)) {
		errp("Invalid default prom node\n");
		return;
	}
	if ((bootf_len = prom_getproplen(node, "boot-file")) <= 0 ||
	    (bootf_len >= OBP_MAXPATHLEN)) {
		DEBUG2(errp("cpr_get_bootinfo: len=%d\n", bootf_len));
		return;
	}
	bzero(default_bootfile, bootf_len);
	(void) prom_getprop(node, "boot-file", default_bootfile);
	default_bootfile[bootf_len] = 0; /* terminate it */
}

void
cpr_set_bootinfo(char *boot_file, char *silent)
{
	dnode_t node;
	dnode_t sp[OBP_STACKDEPTH];
	pstack_t *stk;
	int bootf_len;

	stk = prom_stack_init(sp, sizeof (sp));
	node = prom_findnode_byname(prom_nextnode(0), "options", stk);
	prom_stack_fini(stk);

	if ((node == OBP_NONODE) || (node == OBP_BADNODE)) {
		errp("Invalid prom node; no auto resume\n");
		return;
	}
	/* +1 for '\0' */
	if ((bootf_len = strlen(boot_file) + 1) <= 0 ||
	    (bootf_len >= OBP_MAXPATHLEN)) {
		errp("Invalide prom file name len (%d); no auto resume\n",
			bootf_len);
		return;
	}
	(void) prom_setprop(node, "boot-file", boot_file, bootf_len);
	(void) prom_setprop(node, "silent-mode?", silent, (strlen(silent) + 1));
}

void
cpr_reset_bootinfo()
{
	struct cprinfo ci;

	/*
	 * if this is turbo resume or turbo statfile exists, then set
	 * boot-file (or alike) to be cprboot
	 */
	if (CPR->c_cprboot_magic == CPRINFO_TURBO_MAGIC ||
	    CPR->c_cprboot_magic == CPRINFO_GENERIC_MAGIC &&
	    cpr_cprinfo_is_valid(CPRINFO_TURBO, CPRINFO_TURBO_MAGIC, &ci))
		cpr_set_bootinfo(CPRBOOT, "true");

	/* or we put back what is saved in ci_bootfile */
	if (cpr_cprinfo_is_valid(CPRINFO_GEN, CPRINFO_GENERIC_MAGIC, &ci))
		cpr_set_bootinfo(ci.ci_bootfile, "false");
}

void
cpr_send_notice()
{
	prom_printf("\033[q");
	prom_printf("\014");
	prom_printf("\033[1P");
	prom_printf("\033[18;21H");
	prom_printf("Saving System State. Please Wait... ");
}

void
cpr_spinning_bar()
{
	static int spinix;

	switch (spinix) {
	case 0:
		prom_printf("|\b");
		break;
	case 1:
		prom_printf("/\b");
		break;
	case 2:
		prom_printf("-\b");
		break;
	case 3:
		prom_printf("\\\b");
		break;
	}
	if ((++spinix) & 0x4) spinix = 0;
}

#endif sparc
