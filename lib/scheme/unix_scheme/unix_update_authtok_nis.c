
/*
 * Copyright (c) 1985 by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_update_authtok_nis.c 1.4     94/05/19 SMI"

#include "unix_headers.h"

struct yppasswd yppasswd;

static void	reencrypt_secret();
static int	update_nisattr();

int
update_authtok_nis(
	char		*prognamep,
	char		*usrname,
	char		*field,
	char		*data[],	/* encrypted new passwd */
					/* or new attribute info */
	struct ia_conv *ia_convp,
	char		*old,		/* old passwd: clear */
	char		*new)		/* new passwd: clear */
					/* no compat mode: npd and yp server */
					/* take the same protocol */
{
	int 				ok;
	enum clnt_stat 			ans;
	char 				*domain;
	char 				*master;
	CLIENT 				*client;
	struct timeval 			timeout;
	static struct ia_response	*ret_resp;
	char				messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	int				retcode;

	if (strcmp(field, "passwd") == 0) {
		/*
		 * ck_passwd() already checked the old passwd. It won't get here
		 * if the old passwd is not matched.
		 * We are just preparing the passwd update packet here.
		 */

		yppasswd.oldpass = old;
		nis_pwd->pw_passwd = *data;	/* encrypted new passwd */
	} else {
		/*
		 * prompt for passwd: required for egh options
		 * nis_pwd struct will be modified by update_nisattr().
		 * The encrypted passwd remains the same because we are not
		 * changing passwd here.
		 */
		sprintf(messages[0], dgettext
		    (PAMTXD, "Enter login(NIS) password: "));

		retcode = get_authtok(ia_convp->start_conv, 0,
		    1, messages, NULL, &ret_resp);
		if (retcode != IA_SUCCESS)
			return (retcode);
		yppasswd.oldpass = ret_resp->resp;

		if (update_nisattr(field, data, ia_convp) != 0)
			return (IA_FATAL);
	}

	yppasswd.newpw = *nis_pwd;
	if (yp_get_default_domain(&domain) != 0) {
		sprintf(messages[0],
		    "%s: can't get domain\n", prognamep);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		return (IA_FATAL);
	}

	if (yp_master(domain, "passwd.byname", &master) != 0) {
		sprintf(messages[0],
		    "%s: can't get master for passwd file\n", prognamep);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		return (IA_FATAL);
	}
	client = clnt_create(master, YPPASSWDPROG, YPPASSWDVERS, "udp");
	if (client == NULL) {
		sprintf(messages[0],
		    "%s: couldn't create client\n", prognamep);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
		return (IA_FATAL);
	}

	timeout.tv_usec = 0;
	timeout.tv_sec = 55;	/* npd uses 55 seconds */

	ans = CLNT_CALL(client, YPPASSWDPROC_UPDATE, xdr_yppasswd,
		(char *)&yppasswd, xdr_int, (char *)&ok, timeout);
	if (ans != RPC_SUCCESS) {
		sprintf(messages[0],
		    "%s: couldn't change passwd/attributes\n", prognamep);
		sprintf(messages[1],
		    "Client may have failed because of timeout(55 sec)\n");
		(void) display_errmsg(ia_convp->start_conv, 0, 2,
		    messages, NULL);
		return (IA_FATAL);
	}
	(void) clnt_destroy(client);

	if (ok != 0) {
		sprintf(messages[0],
		    "%s: Couldn't change passwd/attributes for %s \n",
		    prognamep, usrname);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
		return (IA_FATAL);
	}
	sprintf(messages[0],
	    "NIS(YP) passwd/attributes changed on %s\n", master);
	(void) display_errmsg(ia_convp->start_conv, 0, 1,
	    messages, NULL);
	reencrypt_secret(domain, old, new);
	return (IA_SUCCESS);
}


static int
update_nisattr(char *field, char **data, struct ia_conv *ia_convp)
{
	char		*value;
	char 		messages[MAX_NUM_MSG][MAX_MSG_SIZE];

	if (strcmp(field, "attr") == 0) {

		while (*data != NULL) {
			/* AUTHTOK_DEL: not applicable */

			if ((value = attr_match("AUTHTOK_SHELL", *data))
			    != NULL) {
				if (strcmp(value, "1") != 0) {
					sprintf(messages[0],
		    "%s: System error: shell is set illegally\n", value);
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				if (nis_pwd == NULL) {
					sprintf(messages[0],
				"System error: no nis passwd record\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				nis_pwd->pw_shell =
				    getloginshell(nis_pwd->pw_shell);
				if (nis_pwd->pw_shell == NULL)
					return (IA_FATAL);
				data++;
				continue;
			}

			if ((value = attr_match("AUTHTOK_HOMEDIR", *data))
			    != NULL) {
				/* home directory */
				if (strcmp(value, "1") != 0) {
					sprintf(messages[0],
				"System error: homedir is set illegally.\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				if (nis_pwd == NULL) {
					sprintf(messages[0],
				"System error: no nis passwd record\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				nis_pwd->pw_dir =
				    gethomedir(nis_pwd->pw_dir);
				if (nis_pwd->pw_dir == NULL)
					return (IA_FATAL);
				data++;
				continue;
			}

			if ((value = attr_match("AUTHTOK_GECOS", *data))
			    != NULL) {
				/* finger information */
				if (strcmp(value, "1") != 0) {
					sprintf(messages[0],
				"System error: gecos is set illegally.\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				if (nis_pwd == NULL) {
					sprintf(messages[0],
				"System error: no nis passwd record\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				nis_pwd->pw_gecos =
				    getfingerinfo(nis_pwd->pw_gecos);
				if (nis_pwd->pw_gecos == NULL)
					return (IA_FATAL);
				data++;
				continue;
			}
		} /* while */
		return (IA_SUCCESS);
	}
	return (IA_FATAL);
	/* NOTREACHED */
}

/*
 * If the user has a secret key, reencrypt it.
 * Otherwise, be quiet.
 */
static void
reencrypt_secret(char *domain, char *oldpass, char *newpass)
{
	char who[MAXNETNAMELEN+1];
	char secret[HEXKEYBYTES+1];
	char public[HEXKEYBYTES+1];
	char crypt[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];
	char pkent[sizeof (crypt) + sizeof (public) + 1];
	char *master;

	getnetname(who);
	if (!getsecretkey(who, secret, oldpass)) {
		/*
		 * Quiet: net is not running secure RPC
		 */
		return;
	}
	if (secret[0] == 0) {
		/*
		 * Quiet: user has no secret key
		 */
		return;
	}
	if (getpublickey(who, public) == FALSE) {
		(void) fprintf(stderr,
		    "Warning: can't find public key for %s.\n", who);
		return;
	}
	(void) memcpy(crypt, secret, HEXKEYBYTES);
	(void) memcpy(crypt + HEXKEYBYTES, secret, KEYCHECKSUMSIZE);
	crypt[HEXKEYBYTES + KEYCHECKSUMSIZE] = 0;
	(void) xencrypt(crypt, newpass);
	(void) sprintf(pkent, "%s:%s", public, crypt);
	if (yp_update(domain, PKMAP, YPOP_STORE,
	    who, strlen(who), pkent, strlen(pkent)) != 0) {

		(void) fprintf(stderr,
		    "Warning: couldn't reencrypt secret key for %s\n", who);
		return;
	}
	if (yp_master(domain, PKMAP, &master) != 0) {
		master = "yp master";	/* should never happen */
	}
	(void) printf("secret key reencrypted for %s on %s\n", who, master);
}
