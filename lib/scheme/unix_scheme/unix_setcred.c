
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_setcred.c 1.13     93/06/02 SMI"

#include "unix_headers.h"


/*
 * sa_setcred		- Set the process credentials
 */
int
sa_setcred(iah, flags, uid, gid, ngroups, grouplist, ia_status)
	void *iah;
	int   flags;
	uid_t uid;
	gid_t gid;
	int   ngroups;
	char *grouplist;
	struct ia_status *ia_status;
{
	char    *program, *user, *ttyn, *rhost;
	struct ia_conv	*ia_convp;
	char	*password;
	int	err;
	char	messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	int	estkey_stat;
	char	netname[MAXNETNAMELEN+1];

	if ((err = sa_getall(iah, &program, &user, &ttyn, &rhost, &ia_convp))
		    != IA_SUCCESS)
		return (err);

	/*
	 * Set the credentials
	 */

		/* set the effective GID */
	if (flags & SC_SETEGID) {
		if (setegid(gid) == -1) {
			sprintf(messages[0], dgettext(PAMTXD,
				"Bad group id.\n"));
			display_errmsg(ia_convp->cont_conv, 0, 1, messages,
					    NULL);
			return (IA_BAD_GID);
		}
	}

	/* set the real (and effective) GID */
	if (flags & SC_SETGID) {
		if (setgid(gid) == -1) {
			sprintf(messages[0], dgettext(PAMTXD,
				"Bad group id.\n"));
			display_errmsg(ia_convp->cont_conv, 0, 1, messages,
					    NULL);
			return (IA_BAD_GID);
		}
	}

	/*
	 * Initialize the supplementary group access list.
	 */
	if (flags & SC_INITGPS) {
		if (initgroups(user, gid) == -1) {
			sprintf(messages[0], dgettext(PAMTXD,
				"Could not initialize groups\n"));
			display_errmsg(ia_convp->cont_conv, 0, 1, messages,
					    NULL);
			return (IA_INITGP_FAIL);
		}
	}

	/*
	 * Set the supplementary group access list.
	 */
	if (flags & SC_SETGPS) {
		if (setgroups(ngroups, (gid_t *)grouplist) == -1) {
			sprintf(messages[0], dgettext(PAMTXD,
				"Could not set groups\n"));
			display_errmsg(ia_convp->cont_conv, 0, 1, messages,
					    NULL);
			return (IA_SETGP_FAIL);
		}
	}

	/*
	 * Set the user id
	 */

		/* set the effective UID */
	if (flags & SC_SETEUID) {
		if (seteuid(uid) == -1) {
			sprintf(messages[0], dgettext(PAMTXD,
				"Bad user id.\n"));
			display_errmsg(ia_convp->cont_conv, 0, 1, messages,
					    NULL);
			return (IA_BAD_UID);
		}
	}

		/* set the real (and effective) UID */
	if (flags & SC_SETUID) {
		if (setuid(uid) == -1) {
			sprintf(messages[0], dgettext(PAMTXD,
				"Bad user id.\n"));
			display_errmsg(ia_convp->cont_conv, 0, 1, messages,
					    NULL);
			return (IA_BAD_UID);
		}
	}

	if ((err = ia_get_item(iah, IA_AUTHTOK, (void **) &password)) < 0)
		return (-err);

	/*
	 * Do a keylogin if the password is
	 * not null and its not a root login
	 */
	if (password != NULL &&
	    password[0] != '\0' &&
	    uid != 0) {
		estkey_stat = sa_establish_key(uid, password, 0, netname);
		switch (estkey_stat) {
		    case SA_ESTKEY_SUCCESS:
		    case SA_ESTKEY_ALREADY:
		    case SA_ESTKEY_NOCREDENTIALS:
			break;
		    case SA_ESTKEY_BADPASSWD:
			sprintf(messages[0], dgettext(PAMTXD,
			"Password does not decrypt secret key for %s.\r\n"),
			netname);
			display_errmsg(ia_convp->cont_conv, 0, 1, messages,
					NULL);
			break;
		    case SA_ESTKEY_CANTSETKEY:
			sprintf(messages[0],
			dgettext(PAMTXD, "Could not set secret key for %s. \
The key server may be down.\n"),
				netname);
			display_errmsg(ia_convp->cont_conv, 0, 1, messages,
					NULL);
			break;
		}
	}

	return (IA_SUCCESS);
}
