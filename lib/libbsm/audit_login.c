#ifndef lint
static char	sccsid[] = "@(#)audit_login.c 1.15 93/01/26 SMI";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <sys/systeminfo.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/systeminfo.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>

#include <pwd.h>
#include <shadow.h>
#include <utmpx.h>
#include <unistd.h>
#include <string.h>

#include <bsm/audit.h>
#include <bsm/libbsm.h>
#include <bsm/audit_uevents.h>
#include <bsm/audit_record.h>
#include "generic.h"

#include <locale.h>

int audit_login_save_machine();
static void audit_login_record();
static void audit_login_session_setup();

static void get_terminal_id();
static void catch_signals();
static void audit_login_logout();
static void get_terminal_id();
static get_machine();
static selected();

static char	sav_ttyn[512];
static int	sav_rflag;
static int	sav_hflag;
static char	sav_name[512];
static uid_t	sav_uid;
static gid_t	sav_gid;
static int	sav_port, sav_machine;
static char	sav_host[512];

int
audit_login_save_flags(rflag, hflag)
	int rflag, hflag;
{
	if (cannot_audit(0)) {
		return (0);
	}
	sav_rflag = rflag;
	sav_hflag = hflag;
	return (0);
}

int
audit_login_save_host(host)
	char *host;
{
	if (cannot_audit(0)) {
		return (0);
	}
	(void) strncpy(sav_host, host, 511);
	sav_host[511] = '\0';
	(void) audit_login_save_machine();
	return (0);
}

int
audit_login_save_ttyn(ttyn)
	char *ttyn;
{
	if (cannot_audit(0)) {
		return (0);
	}
	(void) strncpy(sav_ttyn, ttyn, 511);
	sav_ttyn[511] = '\0';
	return (0);
}

int
audit_login_save_port()
{
	if (cannot_audit(0)) {
		return (0);
	}
	sav_port = aug_get_port();
	return (0);
}

int
audit_login_save_machine()
{
	if (cannot_audit(0)) {
		return (0);
	}
	sav_machine = get_machine();
	return (0);
}

int
audit_login_save_pw(pwd)
	struct passwd *pwd;
{
	if (cannot_audit(0)) {
		return (0);
	}
	if (pwd == NULL) {
		sav_name[0] = '\0';
		sav_uid = -1;
		sav_gid = -1;
	} else {
		(void) strncpy(sav_name, pwd->pw_name, 511);
		sav_name[511] = '\0';
		sav_uid = pwd->pw_uid;
		sav_gid = pwd->pw_gid;
	}
	return (0);
}

int
audit_login_maxtrys()
{
	if (cannot_audit(0)) {
		return (0);
	}
	audit_login_record(1, dgettext("SUNW_BSM_LIBBSM", "maxtrys"),
		AUE_login);
	return (0);
}

int
audit_login_not_console()
{
	if (cannot_audit(0)) {
		return (0);
	}
	audit_login_record(2, dgettext("SUNW_BSM_LIBBSM", "not_console"),
		AUE_login);
	return (0);
}

int
audit_login_bad_pw()
{
	if (cannot_audit(0)) {
		return (0);
	}
	if (sav_uid == -1) {
		audit_login_record(3, dgettext("SUNW_BSM_LIBBSM",
			"invalid user name"), AUE_login);
	} else {
		audit_login_record(4, dgettext("SUNW_BSM_LIBBSM",
			"invalid password"), AUE_login);
	}
	return (0);
}

int
audit_login_bad_dialup()
{
	if (cannot_audit(0)) {
		return (0);
	}
	audit_login_record(5, dgettext("SUNW_BSM_LIBBSM",
		"invalid dialup password"), AUE_login);
	return (0);
}

int
audit_login_success()
{
	if (cannot_audit(0)) {
		return (0);
	}
	audit_login_session_setup();
	audit_login_record(0, dgettext("SUNW_BSM_LIBBSM",
		"successful login"), AUE_login);
	audit_login_logout();
	return (0);
}

static void
audit_login_record(typ, string, event_no)
int	typ;
char	*string;
au_event_t event_no;
{
	int	ad, rc;
	uid_t		uid;
	gid_t		gid;
	pid_t		pid;
	au_tid_t	tid;

	uid = sav_uid;
	gid = sav_gid;
	pid = getpid();

	get_terminal_id(&tid);

	if (typ == 0) {
		rc = 0;
	} else {
		rc = -1;
	}

	if (event_no == AUE_login) {
		if (sav_hflag)  {
			event_no = AUE_telnet;
		}
		if (sav_rflag) {
			event_no = AUE_rlogin;
		}
	}

	if (!selected(sav_name, uid, event_no, rc))
		return;

	ad = au_open();

	au_write(ad, au_to_subject(uid, uid, gid, uid, gid, pid, pid, &tid));
	au_write(ad, au_to_text(string));
	au_write(ad, au_to_return(rc, typ));

	rc = au_close(ad, AU_TO_WRITE, event_no);
	if (rc < 0) {
		perror("audit");
	}
}

static void
audit_login_session_setup()
{
	int	rc;
	struct auditinfo info;
	au_mask_t mask;

	info.ai_auid = sav_uid;
	info.ai_asid = getpid();
	mask.am_success = 0;
	mask.am_failure = 0;

	au_user_mask(sav_name, &mask);

	info.ai_mask.am_success  = mask.am_success;
	info.ai_mask.am_failure  = mask.am_failure;

	get_terminal_id(&(info.ai_termid));

	rc = setaudit(&info);
	if (rc < 0) {
		perror("setaudit");
	}
}


static void
get_terminal_id(tid)
au_tid_t *tid;
{
	tid->port = sav_port;
	tid->machine = sav_machine;
}

static void
audit_login_logout()
{
	int	ret; /* return value of wait() */
	int	status; /* wait status */
	pid_t pid; /* process id */

	if ((pid = fork()) == 0) {
		return;
	} else if (pid == -1) {
		(void) fputs(dgettext("SUNW_BSM_LIBBSM",
			"login: could not fork\n"), stderr);
		exit(1);
	} else {
		(void) sigset(SIGCHLD, catch_signals);
		while ((ret = (int)wait(&status)) != pid && ret != -1);
			/* keep waiting */
		catch_signals();
		/* NOTREACHED */
		exit(0);
	}
}

static void catch_signals()
{
	char	textbuf[BSM_TEXTBUFSZ];

	(void) sprintf(textbuf,
		dgettext("SUNW_BSM_LIBBSM", "logout %s"), sav_name);
	audit_login_record(0, textbuf, AUE_logout);
	exit(0);
}

static
get_machine()
{
	int	rc, mach;
	char	hostname[256];
	struct hostent *hostent;

	if (sav_rflag || sav_hflag) {
		mach = aug_get_machine(sav_host);
	} else {
		rc = sysinfo(SI_HOSTNAME, hostname, 256);
		if (rc < 0) {
			perror("sysinfo");
			return (0);
		}
		mach = aug_get_machine(hostname);
	}
	return (mach);
}


static
selected(nam, uid, event, sf)
char	*nam;
uid_t uid;
au_event_t event;
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
		rc = au_user_mask(nam, &mask);
	}

	if (sf == 0) {
		sorf = AU_PRS_SUCCESS;
	} else {
		sorf = AU_PRS_FAILURE;
	}
	rc = au_preselect(event, &mask, sorf, AU_PRS_REREAD);

	return (rc);
}
