#ifndef lint
static char sccsid[] = "@(#)audit_cron_session.c 1.4 93/09/30 SMI";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <bsm/audit.h>
#include <bsm/libbsm.h>
#include <unistd.h>

void
audit_cron_session(nam, uid)
char	*nam;
uid_t	uid;
{
	struct auditinfo	info;
	au_mask_t		mask;

	if (cannot_audit(0)) {
		return;
	}

	info.ai_auid = uid;

	au_user_mask(nam, &mask);

	info.ai_mask.am_success  = mask.am_success;
	info.ai_mask.am_failure  = mask.am_failure;

	info.ai_termid.port	= 0;
	info.ai_termid.machine	= 0;

	info.ai_asid = getpid();

	setaudit(&info);
}
