#ifndef lint
static char	sccsid[] = "@(#)audit_su.c 1.9 93/11/22 SMI";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

/*
 * BSM hooks for the su command
 */

#include <sys/types.h>
#include <bsm/audit.h>
#include <bsm/libbsm.h>
#include <bsm/audit_uevents.h>
#include <limits.h>
#include <pwd.h>
#include <shadow.h>
#include <locale.h>
#include <unistd.h>
#include <string.h>

#define	SU_IS_EXPIRED	1
#define	SU_NOT_EXPIRED	0

static auditinfo_t su_ai;
static su_expired; /* boolean, are we su-ing to an expired account? */
static char *su_user;
static char *invalid_user= "invalid user";

static void audit_su_init_expired();
static void audit_su();

/*
 * Hooks that set private variables.
 */

void
audit_su_init_info(username, ttyn)
	char *username;
	char *ttyn;
{
	if (cannot_audit(0)) {
		return;
	}
	if (username != NULL) {
		su_user = strdup(username);
	} else {
		su_user = invalid_user;
	}
	audit_su_init_expired(username);
	getaudit(&su_ai);
	aug_save_me();
}

static void
audit_su_init_expired(username)
	char *username;
{
	struct spwd *p_spwd;

	if (cannot_audit(0)) {
		return;
	}

	su_expired = SU_NOT_EXPIRED;

	if ((p_spwd = getspnam(username)) == NULL) {
		return;
	}

	if (p_spwd->sp_expire > 0 && p_spwd->sp_expire < DAY_NOW) {
		su_expired = SU_IS_EXPIRED;
	}
}

audit_su_reset_ai()
{
	au_mask_t new_users_mask;

	if (cannot_audit(0)) {
		return;
	}

	new_users_mask.am_success = 0;
	new_users_mask.am_failure = 0;

	(void) au_user_mask(su_user, &new_users_mask);
	su_ai.ai_mask.am_success |= new_users_mask.am_success;
	su_ai.ai_mask.am_failure |= new_users_mask.am_failure;

	return (setaudit(&su_ai));
}

void
audit_su_success()
{
	if (cannot_audit(0)) {
		return;
	}
	audit_su(dgettext("SUNW_BSM_LIBBSM", "success"), 0);
}

void
audit_su_bad_username()
{
	if (cannot_audit(0)) {
		return;
	}
	audit_su(dgettext("SUNW_BSM_LIBBSM", "bad username"), 1);
}

void
audit_su_bad_authentication()
{
	if (cannot_audit(0)) {
		return;
	}
	audit_su(dgettext("SUNW_BSM_LIBBSM", "bad auth."), 2);
}

void
audit_su_bad_uid(uid)
uid_t uid;
{
	char    textbuf[BSM_TEXTBUFSZ];

	if (cannot_audit(0)) {
		return;
	}
	(void) sprintf(textbuf, dgettext("SUNW_BSM_LIBBSM", "bad uid %ld"),
		uid);
	audit_su(textbuf, 3);
}

void
audit_su_unknown_failure()
{
	if (cannot_audit(0)) {
		return;
	}
	audit_su(dgettext("SUNW_BSM_LIBBSM", "unknown failure"), 4);
}

/*
 * audit_su: The master hook for su.  It writes records to the audit trail.
 */

static void
audit_su(s, r)
char	*s;	/* string indication success, failure message */
int	r;	/* return code for return token */
{
	int	au_d;
	char	textbuf[BSM_TEXTBUFSZ];
	char	*exp;

	aug_save_event(AUE_su);
	aug_save_sorf(r);
	if (su_expired == SU_IS_EXPIRED) {
		exp = dgettext("SUNW_BSM_LIBBSM", " (expired)");
	} else {
		exp = "";
	}
	(void) sprintf(textbuf, dgettext("SUNW_BSM_LIBBSM",
		"%s for user %s%s"), s, su_user, exp);
	aug_save_text(textbuf);
	aug_audit();
}
