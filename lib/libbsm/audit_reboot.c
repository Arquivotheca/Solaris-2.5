#ifndef lint
static char	sccsid[] = "@(#)audit_reboot.c 1.11 93/11/19 SMI";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <bsm/audit.h>
#include <bsm/audit_record.h>
#include <bsm/audit_uevents.h>
#include <bsm/libbsm.h>
#include "generic.h"

#ifdef C2_DEBUG
#define	dprintf(x) { printf x; }
#else
#define	dprintf(x)
#endif

audit_reboot_setup()
{
	dprintf(("audit_reboot_setup()\n"));

	if (cannot_audit(0)) {
		return (0);
	}

	aug_init();
	aug_save_event(AUE_reboot_solaris);
	aug_save_me();
	return (0);
}

audit_reboot_fail()
{
	return (audit_reboot_generic(-1));
}

audit_reboot_success()
{
	return (audit_reboot_generic(0));
}

audit_reboot_generic(int sorf)
{
	int r;

	dprintf(("audit_reboot_generic(%d)\n", sorf));

	if (cannot_audit(0)) {
		return (0);
	}

	aug_save_sorf(sorf);
	aug_audit();

	return (0);
}
