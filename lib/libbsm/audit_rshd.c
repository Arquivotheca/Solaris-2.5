/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */
#ident	"@(#)audit_rshd.c	1.16	95/02/23 SMI"

#include <sys/types.h>
#include <stdio.h>
#include <sys/fcntl.h>
#include <bsm/audit.h>
#include <bsm/audit_record.h>
#include <bsm/audit_uevents.h>
#include <bsm/libbsm.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <netinet/in.h>
#include <locale.h>
#include <unistd.h>
#include "generic.h"

extern int	errno;

static au_event_t	event;	/* audit event number */

static void generate_record();
static void setup_session();
static selected();

audit_rshd_setup()
{
	event = AUE_rshd;
	return (0);
}


audit_rshd_fail(msg, hostname, remuser, locuser, cmdbuf)
char	*msg;		/* message containing failure information */
char	*hostname;		/* hostname of machine requesting service */
char	*remuser;		/* username at machine requesting service */
char	*locuser;		/* username of local machine */
char	*cmdbuf;		/* command line to be executed locally */
{
	if (cannot_audit(0)) {
		return (0);
	}
	generate_record(hostname, remuser, locuser, cmdbuf, -1, msg);
	return (0);
}


audit_rshd_success(hostname, remuser, locuser, cmdbuf)
char	*hostname;		/* hostname of machine requesting service */
char	*remuser;		/* username at machine requesting service */
char	*locuser;		/* username at local machine */
char	*cmdbuf;		/* command line to be executed locally */
{
	if (cannot_audit(0)) {
		return (0);
	}
	generate_record(hostname, remuser, locuser, cmdbuf, 0, "");
	setup_session(hostname, remuser, locuser);
	return (0);
}


#include <pwd.h>

static void
generate_record(hostname, remuser, locuser, cmdbuf, sf_flag, msg)
char	*hostname;	/* hostname of machine requesting service */
char	*remuser;		/* username at machine requesting service */
char	*locuser;		/* username of local machine */
char	*cmdbuf;		/* command line to be executed locally */
int	sf_flag;		/* success (0) or failure (-1) flag */
char	*msg;		/* message containing failure information */
{
	int	rd;		/* audit record descriptor */
	char	buf[256];	/* temporary buffer */
	char	*tbuf;		/* temporary buffer */
	uid_t	uid;
	gid_t	gid;
	pid_t	pid;
	au_tid_t	tid;
	struct passwd *pwd;

	if (cannot_audit(0)) {
		return;
	}

	pwd = getpwnam(locuser);
	if (pwd == NULL) {
		uid = -1;
		gid = -1;
	} else {
		uid = pwd->pw_uid;
		gid = pwd->pw_gid;
	}

	if (!selected(uid, locuser, remuser, event, sf_flag))
		return;

	pid = getpid();
	tid.port	 = aug_get_port();
	tid.machine	 = aug_get_machine(hostname);

	rd = au_open();

	au_write(rd, au_to_subject(uid, uid, gid, uid, gid, pid, pid, &tid));

	if ((tbuf = (char *) malloc(strlen(cmdbuf)+64)) == (char *) 0) {
		au_close(rd, 0, 0);
		return;
	}
	(void) sprintf(tbuf, dgettext("SUNW_BSM_LIBBSM", "cmd %s"), cmdbuf);
	au_write(rd, au_to_text(tbuf));
	(void) free(tbuf);

	if (strcmp(remuser, locuser) != 0) {
		(void) sprintf(buf, dgettext("SUNW_BSM_LIBBSM",
			"remote user %s"), remuser);
		au_write(rd, au_to_text(buf));
	}

	if (sf_flag == -1) {
		(void) sprintf(buf, dgettext("SUNW_BSM_LIBBSM",
			"local user %s"), locuser);
		au_write(rd, au_to_text(buf));
		au_write(rd, au_to_text(msg));
	}

	au_write(rd, au_to_return (sf_flag, 0));

	if (au_close(rd, 1, event) < 0) {
		au_close(rd, 0, 0);
	}
}


static
selected(uid, locuser, remuser, event, sf)
uid_t uid;
char	*locuser, *remuser;
au_event_t	event;
int	sf;
{
	int	rc, sorf;
	char	naflags[512];
	struct au_mask mask;

	mask.am_success = mask.am_failure = 0;
	if (uid < 0) {
		rc = getacna(naflags, 256); /* get non-attrib flags */
		if (rc == 0)
			getauditflagsbin(naflags, &mask);
	} else {
		rc = au_user_mask(locuser, &mask);
	}

	if (sf == 0)
		sorf = AU_PRS_SUCCESS;
	else if (sf == -1)
		sorf = AU_PRS_FAILURE;
	else
		sorf = AU_PRS_BOTH;
	rc = au_preselect(event, &mask, sorf, AU_PRS_REREAD);
	return (rc);
}


static void
setup_session(hostname, remuser, locuser)
char	*hostname, *remuser, *locuser;
{
	int	rc;
	struct auditinfo info;
	au_mask_t		mask;
	uid_t			uid;
	struct passwd *pwd;

	pwd = getpwnam(locuser);
	if (pwd == NULL)
		uid = -1;
	else
		uid = pwd->pw_uid;

	info.ai_auid = uid;
	info.ai_asid = getpid();

	mask.am_success = 0;
	mask.am_failure = 0;
	au_user_mask(locuser, &mask);

	info.ai_mask.am_success = mask.am_success;
	info.ai_mask.am_failure = mask.am_failure;

	info.ai_termid.port	 = aug_get_port();
	info.ai_termid.machine	 = aug_get_machine(hostname);

	rc = setaudit(&info);
	if (rc < 0) {
		perror("setaudit");
	}
}
