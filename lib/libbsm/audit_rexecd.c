
/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ident	"@(#)audit_rexecd.c	1.8	95/02/23 SMI"

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
#include "generic.h"

#ifdef C2_DEBUG
#define	dprintf(x) { printf x; }
#else
#define	dprintf(x)
#endif

extern int	errno;

audit_rexecd_setup()
{
	dprintf(("audit_rexecd_setup()\n"));

	if (cannot_audit(0)) {
		return (0);
	}

	aug_init();
	aug_save_me();
	aug_save_namask();
	aug_save_event(AUE_rexecd);

	return (0);
}


audit_rexecd_fail(msg, hostname, user, cmdbuf)
char	*msg;			/* message containing failure information */
char	*hostname;	/* hostname of machine requesting service */
char	*user;			/* username of user requesting service */
char	*cmdbuf;		/* command line to be executed locally */
{
	int	rd;			/* audit record descriptor */
	char	buf[256];	/* temporary buffer */
	char	*tbuf;		/* temporary buffer */

	dprintf(("audit_rexecd_fail()\n"));

	if (cannot_audit(0)) {
		return (0);
	}

	/* determine if we're preselected */
	if (!aug_na_selected())
		return (0);

	rd = au_open();

	/* add subject token */
	au_write(rd, au_to_me());

	/* add reason for failure */
	au_write(rd, au_to_text(msg));

	/* add hostname of machine requesting service */
	sprintf(buf, dgettext("SUNW_BSM_LIBBSM", 
		"Remote execution requested by: %s"), hostname);
	au_write(rd, au_to_text(buf));

	/* add username of user requesting service */
	sprintf(buf, dgettext("SUNW_BSM_LIBBSM",
		"Username: %s"), user);
	au_write(rd, au_to_text(buf));

	/* add command line to be executed locally */
	if ((tbuf = (char *) malloc(strlen(cmdbuf) + 64)) == (char *) 0) {
		au_close(rd, 0, 0);
		return (-1);
	}
	sprintf(tbuf, dgettext("SUNW_BSM_LIBBSM", "Command line: %s"), cmdbuf);
	au_write(rd, au_to_text(tbuf));
	(void) free(tbuf);

	/* add return token */
	au_write(rd, au_to_return (-1, errno));

	/* write audit record */
	if (au_close(rd, 1, AUE_rexecd) < 0) {
		au_close(rd, 0, 0);
		return (-1);
	}

	return (0);
}


audit_rexecd_success(hostname, user, cmdbuf)
char	*hostname;	/* hostname of machine requesting service */
char	*user;			/* username of user requesting service */
char	*cmdbuf;		/* command line to be executed locally */
{
	int	rd;			/* audit record descriptor */
	char	buf[256];	/* temporary buffer */
	char	*tbuf;		/* temporary buffer */

	dprintf(("audit_rexecd_success()\n"));

	if (cannot_audit(0)) {
		return (0);
	}

	/* determine if we're preselected */
	if (!aug_na_selected())
		return (0);

	rd = au_open();

	/* add subject token */
	au_write(rd, au_to_me());

	/* add hostname of machine requesting service */
	sprintf(buf, dgettext("SUNW_BSM_LIBBSM",
		"Remote execution requested by: %s"), hostname);
	au_write(rd, au_to_text(buf));

	/* add username at machine requesting service */
	sprintf(buf, dgettext("SUNW_BSM_LIBBSM", "Username: %s"), user);
	au_write(rd, au_to_text(buf));

	/* add command line to be executed locally */
	if ((tbuf = (char *) malloc(strlen(cmdbuf) + 64)) == (char *) 0) {
		au_close(rd, 0, 0);
		return (-1);
	}
	sprintf(tbuf, dgettext("SUNW_BSM_LIBBSM", "Command line: %s"), cmdbuf);
	au_write(rd, au_to_text(tbuf));
	(void) free(tbuf);

	/* add return token */
	au_write(rd, au_to_return (0, 0));

	/* write audit record */
	if (au_close(rd, 1, AUE_rexecd) < 0) {
		au_close(rd, 0, 0);
		return (-1);
	}

	return (0);
}
