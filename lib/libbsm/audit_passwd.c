#ifndef lint
static char	sccsid[] = "@(#)audit_passwd.c 1.2 93/01/26 SMI";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

/* BSM hooks for the passwd command */

#include <sys/types.h>
#include <bsm/audit.h>
#include <bsm/libbsm.h>
#include <bsm/audit_uevents.h>
#include "generic.h"

static void audit_passwd();

void
audit_passwd_sorf(retval)
int	retval;
{
	if (cannot_audit(0)) {
		return;
	}
	if (retval == 0) {
		audit_passwd(dgettext("SUNW_BSM_LIBBSM",
			"update password success"), 0);
	} else {
		audit_passwd(dgettext("SUNW_BSM_LIBBSM",
			"update password failed"), 1);
	}
}

void
audit_passwd_attributes_sorf(retval)
int	retval;
{
	if (cannot_audit(0)) {
		return;
	}
	if (retval == 0) {
		audit_passwd(dgettext("SUNW_BSM_LIBBSM",
			"update successful"), 0);
	} else {
		audit_passwd(dgettext("SUNW_BSM_LIBBSM",
			"update failed"), 2);
	}
}

void
audit_passwd_init_id()
{
	if (cannot_audit(0)) {
		return;
	}
	aug_save_me();
}

static void
audit_passwd(s, r)
char	*s;
int	r;
{
	aug_save_event(AUE_passwd);
	aug_save_text(s);
	aug_save_sorf(r);
	aug_audit();
}
