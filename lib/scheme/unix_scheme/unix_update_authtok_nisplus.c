
/*
 * Copyright (c) 1985 by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_update_authtok_nisplus.c 1.12     94/08/29 SMI"

#include "unix_headers.h"

extern struct nis_result	*passwd_res;
extern struct nis_result	*cred_res;
extern char			curcryptsecret[];
extern int			privileged;
extern mutex_t			_priv_lock;

static int		update_attr();
static char		*reencrypt_secret();
static nis_error	revert2oldpasswd();
static int		talk_to_npd();

static char		*gecos = NULL;
static char		*shell = NULL;
static int		failover = FALSE;

int
update_authtok_nisplus(
	char *prognamep,
	char *usrname,
	char *nisdomain,
	char *field,
	char *data[],			/* Depending on field: it can store */
					/* encrypted new passwd or new */
					/* attributes */
	struct ia_conv *ia_convp,
	char *old,			/* old passwd: clear version */
	char *oldrpc,			/* old rpc passwd: clear version */
	char *new,			/* new passwd: clear version */
	int  opwcmd)			/* old passwd cmd: nispasswd */
{
	char tmpcryptsecret[HEXKEYBYTES+KEYCHECKSUMSIZE+1];
	char *newcryptsecret = NULL;
	entry_col	ecol[8];
	nis_object	*eobj;
	nis_result	*mres;
	char		mname[NIS_MAXNAMELEN];
	nis_error 	niserr;
	struct spwd	sp;
	char		shadow[80];
	int		rc;
	char 		messages[MAX_NUM_MSG][MAX_MSG_SIZE];

	/*
	 * Passwd is setuid program. We want the real user to send out
	 * any nis+ requests. The correct identity should have been set
	 * in ck_perm() when checking privilege.
	 */
	dprintf1("the effective uid is %d\n", geteuid());

	if (opwcmd == FALSE) {
		rc = talk_to_npd(field, data, nisdomain, usrname, old, new,
		    ia_convp);
		if (rc == IA_SUCCESS || rc == NPD_PARTIALSUCCESS) {
			sprintf(messages[0],
			    "\tNIS+ password information changed for %s\n",
			    usrname);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
			if (rc == IA_SUCCESS) {
				sprintf(messages[0],
			"\tNIS+ credential information changed for %s\n",
				    usrname);
				(void) display_errmsg(ia_convp->start_conv,
				    0, 1, messages, NULL);
			}
			return (IA_SUCCESS);
		}
		/* failover to use old protocol */
		dprintf("Failed to use new passwd update protocol\n");

		/*
		 * There are two reasons we will get here:
		 * 1. passwd, shell, gecos update failed (true failover)
		 * 2. we are updating passwd attrs other than the above
		 *    three attrs. In this case, rc is equal to IA_NOPERM
		 *    (i.e. attrs not supported by new protocol)
		 */
		failover = TRUE;
	}

	if (strcmp(field, "passwd") == 0) {
		/*
		 * Obtain the old aging information. And modify, if need be,
		 * on top. At least the latchg field needs to be changed.
		 */
		/* old protocol requires user credential info */
		if (cred_res == NULL || cred_res->status != NIS_SUCCESS) {
			sprintf(messages[0], dgettext(PAMTXD, "%s: %s"),
			prognamep, "Failover: user credential is required.");
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
			return (IA_FATAL);
		}

		populate_age(NIS_RES_OBJECT(passwd_res), &sp);

		(void) memcpy(tmpcryptsecret, curcryptsecret,
		    HEXKEYBYTES + KEYCHECKSUMSIZE + 1);

		/* same user check? */
		mutex_lock(&_priv_lock);
		if ((!privileged) && (newcryptsecret = reencrypt_secret
		    (tmpcryptsecret, oldrpc, new)) == NULL) {
			sprintf(messages[0],
			    "\nUnable to reencrypt credentials for %s;\n",
			    usrname);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
			mutex_unlock(&_priv_lock);
			return (IA_FATAL);
		}
		mutex_unlock(&_priv_lock);

		/* update passwd at server */
		(void) memset((char *)ecol, 0, sizeof (ecol));
		ecol[1].ec_value.ec_value_val = *data;
		ecol[1].ec_value.ec_value_len = strlen(*data) + 1;
		ecol[1].ec_flags = EN_CRYPT|EN_MODIFIED;

		/* update last change field */
		sp.sp_lstchg = DAY_NOW;
		if (sp.sp_max == 0) {
			/* passwd was forced to changed: turn off aging */
			sp.sp_max = -1;
			sp.sp_min = -1;
		}

		/* prepare shadow column */
		if (sp.sp_expire == -1) {
			sprintf(shadow, "%ld:%ld:%ld:%ld:%ld::%lu",
				sp.sp_lstchg,
				sp.sp_min,
				sp.sp_max,
				sp.sp_warn,
				sp.sp_inact,
				sp.sp_flag);
		} else {
			sprintf(shadow, "%ld:%ld:%ld:%ld:%ld:%ld:%lu",
				sp.sp_lstchg,
				sp.sp_min,
				sp.sp_max,
				sp.sp_warn,
				sp.sp_inact,
				sp.sp_expire,
				sp.sp_flag);
		}
		ecol[7].ec_value.ec_value_val = shadow;
		ecol[7].ec_value.ec_value_len = strlen(shadow) + 1;
		ecol[7].ec_flags = EN_CRYPT|EN_MODIFIED;

		/*
		 * build entry based on the one we got back from the server
		 */
		eobj = nis_clone_object(NIS_RES_OBJECT(passwd_res), NULL);
		if (eobj == NULL) {
			sprintf(messages[0], dgettext(PAMTXD,
			    "%s: %s"), prognamep, "clone object failed");
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
			return (IA_FATAL);
		}
		eobj->EN_data.en_cols.en_cols_val = ecol;
		eobj->EN_data.en_cols.en_cols_len = 8;

		/* strlen("[name=],passwd.") + null + "." = 17 */
		if ((strlen(usrname) +
		    strlen(NIS_RES_OBJECT(passwd_res)->zo_domain) + 17) >
			(size_t) NIS_MAXNAMELEN) {
			sprintf(messages[0], dgettext(PAMTXD,
			    "%s: %s"), prognamep, "not enough buffer space");
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
			return (IA_FATAL);
		}
		sprintf(mname, "[name=%s],passwd.%s", usrname,
		    NIS_RES_OBJECT(passwd_res)->zo_domain);
		if (mname[strlen(mname) - 1] != '.')
			(void) strcat(mname, ".");
		mres = nis_modify_entry(mname, eobj, 0);

		/*
		 * It is possible that we have permission to modify the
		 * encrypted password but not the shadow column in the
		 * NIS+ table. In this case, we should try updating only
		 * the password field and not the aging stuff (lstchg).
		 * With the current NIS+ passwd table format, this would
		 * be the case most of the times.
		 */
		if (mres->status == NIS_PERMISSION) {
			ecol[7].ec_flags = 0;
			mres = nis_modify_entry(mname, eobj, 0);
			if (mres->status != NIS_SUCCESS) {
				sprintf(messages[0],
				    "Password information update failed\n");
				(void) display_errmsg(ia_convp->start_conv,
				    0, 1, messages, NULL);
				(void) nis_freeresult(mres);
				return (IA_FATAL);
			}
		}
		/* set column stuff to NULL so that we can free eobj */
		eobj->EN_data.en_cols.en_cols_val = NULL;
		eobj->EN_data.en_cols.en_cols_len = 0;
		(void) nis_destroy_object(eobj);
		(void) nis_freeresult(mres);


		sprintf(messages[0],
		    "\tNIS+ password information changed for %s\n", usrname);
		(void) display_errmsg(ia_convp->start_conv,
		    0, 1, messages, NULL);


		mutex_lock(&_priv_lock);
		if (privileged) {
			sprintf(messages[0],
	"\nThe credential information for %s will not be changed.\n", usrname);
			(void) display_errmsg(ia_convp->start_conv,
			    0, 1, messages, NULL);
			sprintf(messages[0],
	"\tUser %s must do the following to update his/her\n", usrname);
			(void) display_errmsg(ia_convp->start_conv,
			    0, 1, messages, NULL);
			sprintf(messages[0], "\tcredential information:\n");
			(void) display_errmsg(ia_convp->start_conv,
			    0, 1, messages, NULL);
			sprintf(messages[0],
	"\tUse NEW passwd for login and OLD passwd for keylogin.\n");
			(void) display_errmsg(ia_convp->start_conv,
			    0, 1, messages, NULL);
			sprintf(messages[0],
	"\tUse \"chkey -p\" to reencrypt the credentials with the\n");
			(void) display_errmsg(ia_convp->start_conv,
			    0, 1, messages, NULL);
			sprintf(messages[0], "\tnew login passwd.\n");
			(void) display_errmsg(ia_convp->start_conv,
			    0, 1, messages, NULL);
			sprintf(messages[0],
	"\tHe/she must keylogin explicitly after his/her next login.\n\n",
				usrname);
			(void) display_errmsg(ia_convp->start_conv,
			    0, 1, messages, NULL);
			mutex_unlock(&_priv_lock);
			return (IA_SUCCESS);
		}
		mutex_unlock(&_priv_lock);

		/* update cred at server */
		(void) memset((char *)ecol, 0, sizeof (ecol));
		ecol[4].ec_value.ec_value_val = newcryptsecret;
		ecol[4].ec_value.ec_value_len = strlen(newcryptsecret) + 1;
		ecol[4].ec_flags = EN_CRYPT|EN_MODIFIED;
		eobj = nis_clone_object(NIS_RES_OBJECT(cred_res), NULL);
		if (eobj == NULL) {
			sprintf(messages[0], dgettext(PAMTXD,
			    "%s: %s"), prognamep, "clone object failed");
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
			return (IA_FATAL);
		}
		eobj->EN_data.en_cols.en_cols_val = ecol;
		eobj->EN_data.en_cols.en_cols_len = 5;

		/*
		 * Now, if one were stupid enough to run nispasswd as/for root
		 * on some machine, it would have looked up and modified
		 * the password entry for "root" in passwd.org_dir. Now,
		 * should we really apply this new password to the cred
		 * entry for "<machinename>.<domainname>" ?
		 *
		 * POLICY: NO. We have no way of identifying a root user in
		 * NIS+ passwd table for each root@machinename. We do not
		 * allow the one password for [name=root], passwd.org_dir
		 * to apply to all "<machinename>.<domainname>" principals.
		 * If somebody let a root entry in passwd table, it probably
		 * has modify permissions for a distinguished NIS+ principal
		 * which we let be associated only with NIS+ principal
		 * root.<domainname>. Does this make any sense ?
		 */

		/* strlen("[cname=.,auth_type=DES],cred.") + null + "." = 31 */
		if ((strlen(usrname) + strlen(nisdomain) +
		    strlen(NIS_RES_OBJECT(passwd_res)->zo_domain) + 31) >
			(size_t) NIS_MAXNAMELEN) {
			sprintf(messages[0], dgettext(PAMTXD,
			    "%s: %s"), prognamep, "not enough buffer space");
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
			return (IA_FATAL);
		}
		sprintf(mname, "[cname=%s.%s,auth_type=DES],cred.%s",
		    usrname, nisdomain, NIS_RES_OBJECT(passwd_res)->zo_domain);
		if (mname[strlen(mname) - 1] != '.')
			(void) strcat(mname, ".");
		mres = nis_modify_entry(mname, eobj, 0);
		if (mres->status != NIS_SUCCESS) {

			/* attempt to revert back to the old passwd */
			niserr = revert2oldpasswd(usrname);

			if (niserr != NIS_SUCCESS) {
				sprintf(messages[0],
		"\nWARNING: Could not reencrypt credentials for %s;\n",
				    usrname);
				(void) display_errmsg(ia_convp->start_conv,
				    0, 1, messages, NULL);
				sprintf(messages[0],
			"\tlogin and keylogin passwords differ.\n");
				(void) display_errmsg(ia_convp->start_conv,
				    0, 1, messages, NULL);
				sprintf(messages[0],
		"\tUse NEW passwd for login and OLD passwd for keylogin.\n");
				(void) display_errmsg(ia_convp->start_conv,
				    0, 1, messages, NULL);
				sprintf(messages[0],
		"\tUse \"chkey -p\" to reencrypt the credentials with the\n");
				(void) display_errmsg(ia_convp->start_conv,
				    0, 1, messages, NULL);
				sprintf(messages[0], "\tnew login passwd.\n");
				(void) display_errmsg(ia_convp->start_conv,
				    0, 1, messages, NULL);
				sprintf(messages[0],
		"\tYou must keylogin explicitly after your next login.\n\n");
				(void) display_errmsg(ia_convp->start_conv,
				    0, 1, messages, NULL);
				return (IA_FATAL);
			}
			sprintf(messages[0],
				"\t%s: couldn't change password for %s.\n",
				prognamep, usrname);
			(void) display_errmsg(ia_convp->start_conv,
			    0, 1, messages, NULL);
			sprintf(messages[0],
"\tReason: failed to update the cred table with reencrypted credentials.\n");
			(void) display_errmsg(ia_convp->start_conv,
			    0, 1, messages, NULL);
			sprintf(messages[0],
				"\tPlease notify your System Administrator.\n");
			(void) display_errmsg(ia_convp->start_conv,
			    0, 1, messages, NULL);
			(void) nis_freeresult(mres);
			return (IA_FATAL);
		}
		/* set column stuff to NULL so that we can free eobj */
		eobj->EN_data.en_cols.en_cols_val = NULL;
		eobj->EN_data.en_cols.en_cols_len = 0;
		(void) nis_destroy_object(eobj);
		(void) nis_freeresult(mres);

		sprintf(messages[0],
		    "\tNIS+ credential information changed for %s\n",
		    usrname);
		(void) display_errmsg(ia_convp->start_conv,
		    0, 1, messages, NULL);
	} else
		return (update_attr(field, data, usrname, 1,
		    NULL, NULL, ia_convp));
	return (0);
}

/*
 * The function uses the new protocol to update passwd attributes via
 * passwd daemon.
 */
static int
talk_to_npd(char *field, char **data, char *domain, char *user,
	char *oldpass, char *newpass, struct ia_conv *ia_convp)
{
	CLIENT		*clnt = NULL;
	char		srv_pubkey[HEXKEYBYTES + 1];
	char		u_pubkey[HEXKEYBYTES + 1];
	char		u_seckey[HEXKEYBYTES + 1];
	des_block	deskey;
	unsigned long 	ident = 0, randval = 0;
	int 		error = 0, status, srv_keysize = HEXKEYBYTES + 1;
	int		retcode;
	char		messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	nispasswd_error	*errlist = NULL;
	nispasswd_error	*p = NULL;
	static struct ia_response	*ret_resp;

	if (user == NULL || domain == NULL || *domain == '\0')
		return (IA_FATAL);

	dprintf2("domain=%s, user=%s\n", domain, user);

	/*
	 * Let's do a quick check whether the attrs are really of interest.
	 * We don't want to prompt for user passwd which is sure not to be
	 * used.
	 */
	retcode = update_attr(field, data, NULL, 0, NULL, NULL, NULL);
	if (retcode != IA_SUCCESS)
		return (retcode);

	if (oldpass == NULL) {
		/*
		 * This is possible from unix_set_authtokattr().
		 * Old passwd is required to change any attributes.
		 * This is imposed by new protocol to support users
		 * without credentials.
		 */
		sprintf(messages[0], dgettext
		    (PAMTXD, "Enter login(NIS+) password: "));

		retcode = get_authtok(ia_convp->start_conv, 0,
		    1, messages, NULL, &ret_resp);
		if (retcode != IA_SUCCESS)
			return (retcode);
		oldpass = ret_resp->resp;
	}

	dprintf1("oldpass=%s\n", oldpass);

	/* get gecos, shell and other */
	retcode = update_attr(field, data, user, 0, &shell, &gecos, ia_convp);
	if (retcode != IA_SUCCESS)
		return (retcode);

	if (npd_makeclnthandle(domain, &clnt, srv_pubkey, srv_keysize) ==
								FALSE) {
		syslog(LOG_ALERT, "Couldn't make a client handle\n");
		return (IA_FATAL);
	}

/* again: doesn't need to generate a new pair of keys */
	/* generate a key-pair for this user */
	(void) __gen_dhkeys(u_pubkey, u_seckey, oldpass);

	/*
	 * get the common des key from the servers' pubkey and
	 * the users secret key
	 */
	if (__get_cmnkey(srv_pubkey, u_seckey, &deskey) == FALSE) {
		syslog(LOG_ALERT, "Couldn't get a common DES key\n");
		return (IA_FATAL);
	}
again:
	status = nispasswd_auth(user, domain, oldpass, u_pubkey, &deskey, clnt,
			&ident, &randval, &error);
	if (status == NPD_FAILED) {
		dprintf1("error=%d\n", error);
		switch (error) {
		case NPD_NOTMASTER:
			syslog(LOG_ALERT,
	"Password update daemon is not running with NIS+ master server\n");
			return (IA_FATAL);
		case NPD_SYSTEMERR:
			syslog(LOG_ALERT, "System error\n");
			return (IA_FATAL);
		case NPD_IDENTINVALID:
			syslog(LOG_ALERT, "Identifier invalid\n");
			return (IA_FATAL);
		case NPD_PASSINVALID:
			syslog(LOG_ALERT, "Password invalid\n");
			return (IA_FATAL);
		case NPD_NOSUCHENTRY:
			syslog(LOG_ALERT, "No password entry for %s\n", user);
			return (IA_FATAL);
		case NPD_NISERROR:
			syslog(LOG_ALERT, "NIS+ error\n");
			return (IA_FATAL);
		case NPD_CKGENFAILED:
			syslog(LOG_ALERT,
			    "Couldn't generate a common DES key\n");
			return (IA_FATAL);
		case NPD_NOPASSWD:
			syslog(LOG_ALERT, "No password for %s\n", user);
			return (IA_FATAL);
		case NPD_NOTAGED:
			syslog(LOG_ALERT, "Passwd has not aged enough\n");
			return (IA_FATAL);
		case NPD_NOSHDWINFO:
			syslog(LOG_ALERT, "No shadow password information\n");
			return (IA_FATAL);
		default:
			syslog(LOG_ALERT, "Fatal error: %d\n", error);
			return (IA_FATAL);
		}
	}
	if (status == NPD_TRYAGAIN) {
		/*
		 * call nispasswd_auth() again after getting another
		 * passwd. Note that ident is now non-zero.
		 */
		dprintf2("status=tryagain; ident=%ld, randval=%ld\n",
			ident, randval);

		/* wrong passwd: get auth token again */
		sprintf(messages[0], "Password incorrect: try again\n");
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
		sprintf(messages[0], dgettext(PAMTXD,
		    "Enter login(NIS+) password: "));
		retcode = get_authtok(ia_convp->start_conv, 0,
		    1, messages, NULL, &ret_resp);
		if (retcode != IA_SUCCESS)
			return (retcode);
		oldpass = ret_resp->resp;
		goto again;
	}
	if (status == NPD_SUCCESS) {
		/* send the new passwd & other changes */
		dprintf2("status=success; ident=%ld, randval=%ld\n",
		    ident, randval);
		if (newpass == NULL) {
			/*
			 * This is possible from unix_set_authtokattr().
			 * Just use the same passwd so that we have a
			 * meaningful passwd field.
			 */
			newpass = oldpass;
		}
		dprintf1("newpass=%s\n", newpass);

		/* gecos and shell could be NULL if we just change passwd */
		status = nispasswd_pass(clnt, ident, randval, &deskey,
				newpass, gecos, shell, &error, &errlist);

		if (status == NPD_FAILED) {
			sprintf(messages[0],
			"Password information update failed(talking to NPD)\n");
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
			    messages, NULL);
			dprintf1("error=%d\n", error);
			return (IA_FATAL);
		}
		/*
		 * WHAT SHOULD BE DONE FOR THE PARTIAL SUCCESS CASE ??
		 * I'll just print out some messages
		 */
		if (status == NPD_PARTIALSUCCESS) {
			syslog(LOG_ALERT,
			"Password information is partially updated.\n");
			for (p = errlist; p != NULL; p = p->next) {
				if (p->npd_field == NPD_GECOS) {
					sprintf(messages[0],
		"GECOS information was not updated: check permission.\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
				} else if (p->npd_field == NPD_SHELL) {
					sprintf(messages[0],
		"SHELL information was not updated: check permission.\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
				} else if (p->npd_field == NPD_SECRETKEY) {
					sprintf(messages[0],
			"Credential information was not updated.\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
				}
			}
			/* check for collision with IA_* return code */
			(void) __npd_free_errlist(errlist);
			return (NPD_PARTIALSUCCESS);
		}
		(void) __npd_free_errlist(errlist);
	}
	return (IA_SUCCESS);
}


static int
update_attr(char *field, char **data, char *usrname, int opwcmd, char **sh_p,
	char **gecos_p, struct ia_conv *ia_convp)
{
	entry_col	ecol[8];
	nis_object	*eobj;
	nis_result	*mres;
	char		mname[NIS_MAXNAMELEN];
	struct spwd	sp;		/* new attr values in here */
	char		*value;
	int		maxdate;
	int		mindate;
	int		warndate;
	static char	lkstring[] = "*LK*"; /* ??? in header */
	int		flag = 0;	/* any change in shadow column */
	static char	**data_p;
	char		shadow[80];
	char		*newhome;
	char		*newgecos;
	char		*newsh;
	char 		messages[MAX_NUM_MSG][MAX_MSG_SIZE];

	data_p = data;
	if (strcmp(field, "attr") == 0) {
		/*
		 * Obtain the old aging information. And modify, if need be,
		 * on top.
		 */
		if (opwcmd)
			populate_age(NIS_RES_OBJECT(passwd_res), &sp);

		(void) memset((char *)ecol, 0, sizeof (ecol));
		while (*data != NULL) {
			/* AUTHTOK_DEL: not applicable */

			/* check attribute: AUTHTOK_LK */
			if ((value = attr_match("AUTHTOK_LK", *data)) != NULL) {
				/* new update protocol doesn't support this */
				if (opwcmd == FALSE)
					return (IA_NOPERM);

				if (strcmp(value, "1") == 0) {
					/* lock password */
					ecol[1].ec_value.ec_value_val =
					    &lkstring[0];
					ecol[1].ec_value.ec_value_len =
					    strlen(&lkstring[0]) + 1;
					ecol[1].ec_flags = EN_CRYPT|EN_MODIFIED;

					if (!(attr_find
					    ("AUTHTOK_EXP", data_p))) {
						sp.sp_lstchg = DAY_NOW;
						flag = 1;
					}
				}
				data++;
				continue;
			}

			/* check attribute: AUTHTOK_EXP */
			if ((value = attr_match("AUTHTOK_EXP", *data))
			    != NULL) {
				/* new update protocol doesn't support this */
				if (opwcmd == FALSE)
					return (IA_NOPERM);

				if (strcmp(value, "1") == 0) {
					/* expire password */
					sp.sp_lstchg = (long) 0;
					flag = 1;
				}
				data++;
				continue;
			}

			/* check attribute: AUTHTOK_MAXAGE */
			if ((value = attr_match("AUTHTOK_MAXAGE", *data))
			    != NULL) {
				/* new update protocol doesn't support this */
				if (opwcmd == FALSE)
					return (IA_NOPERM);

				/* set max field */
				maxdate = (int)atol(value);
				if (!(attr_find("AUTHTOK_MINAGE", data_p)) &&
					sp.sp_min == -1)
					sp.sp_min = 0;
				if (maxdate == -1) {	/* turn off aging */
					sp.sp_min = -1;
					sp.sp_warn = -1;
				} else if (sp.sp_max == -1)
					sp.sp_lstchg = DAY_NOW;

				sp.sp_max = maxdate;
				flag = 1;
				data++;
				continue;
			}

			/* check attribute: AUTHTOK_MINAGE */
			if ((value = attr_match("AUTHTOK_MINAGE", *data))
			    != NULL) {
				/* new update protocol doesn't support this */
				if (opwcmd == FALSE)
					return (IA_NOPERM);

				/* set min field */
				mindate = (int)atol(value);
				if (!(attr_find("AUTHTOK_MAXAGE", data_p)) &&
				    sp.sp_max == -1 && mindate != -1)
					return (IA_BADAGE);
				sp.sp_min = mindate;
				flag = 1;
				data++;
				continue;
			}

			/* check attribute: AUTHTOK_WARNDATE */
			if ((value = attr_match("AUTHTOK_WARNDATE", *data))
			    != NULL) {
				/* new update protocol doesn't support this */
				if (opwcmd == FALSE)
					return (IA_NOPERM);

				/* set warn field */
				warndate = (int)atol(value);
				if (sp.sp_max == -1 && warndate != -1)
					return (IA_BADAGE);
				sp.sp_warn = warndate;
				flag = 1;
				data++;
				continue;
			}

			if ((value = attr_match("AUTHTOK_SHELL", *data))
			    != NULL) {
				/* see if quick check */
				if (usrname == NULL && ia_convp == NULL)
					return (IA_SUCCESS);

				if (nisplus_pwd == NULL && opwcmd) {
					sprintf(messages[0],
					    "No NIS+ record\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}

				/*
				 * If failover, we already got the shell info
				 * in "shell". Don't ask again.
				 */
				if (failover)
					newsh = shell;
				else
					newsh = getloginshell(
					    nisplus_pwd->pw_shell);

				if (newsh == NULL)
					return (IA_FATAL);

				if (opwcmd || failover) {
					ecol[6].ec_value.ec_value_val = newsh;
					ecol[6].ec_value.ec_value_len =
					    strlen(newsh) + 1;
					ecol[6].ec_flags = EN_MODIFIED;
				} else
					*sh_p = newsh;
				data++;
				continue;
			}

			if ((value = attr_match("AUTHTOK_HOMEDIR", *data))
			    != NULL) {
				/* new update protocol doesn't support this */
				if (opwcmd == FALSE)
					return (IA_NOPERM);

				/* home directory */
				if (nisplus_pwd == NULL) {
					sprintf(messages[0],
					    "No NIS+ record\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				newhome = gethomedir(nisplus_pwd->pw_dir);
				if (newhome == NULL)
					return (IA_FATAL);
				ecol[5].ec_value.ec_value_val = newhome;
				ecol[5].ec_value.ec_value_len =
				    strlen(newhome) + 1;
				ecol[5].ec_flags = EN_MODIFIED;
				data++;
				continue;
			}

			if ((value = attr_match("AUTHTOK_GECOS", *data))
			    != NULL) {
				/* see if quick check */
				if (usrname == NULL && ia_convp == NULL)
					return (IA_SUCCESS);

				/* finger information */
				if (nisplus_pwd == NULL && opwcmd) {
					sprintf(messages[0],
					    "No NIS+ record\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				if (failover)
					newgecos = gecos;
				else
					newgecos = getfingerinfo(
					    nisplus_pwd->pw_gecos);

				if (newgecos == NULL)
					return (IA_FATAL);

				if (opwcmd || failover) {
					ecol[4].ec_value.ec_value_val =
					    newgecos;
					ecol[4].ec_value.ec_value_len =
					    strlen(newgecos) + 1;
					ecol[4].ec_flags = EN_MODIFIED;
				} else
					*gecos_p = newgecos;
				data++;
				continue;
			}
		} /* while */

		if (usrname == NULL && ia_convp == NULL)
			return (IA_SUCCESS);

		if (flag && opwcmd) {
			/* prepare shadow column */
			if (sp.sp_expire == -1) {
				sprintf(shadow, "%ld:%ld:%ld:%ld:%ld::%lu",
				    sp.sp_lstchg,
				    sp.sp_min,
				    sp.sp_max,
				    sp.sp_warn,
				    sp.sp_inact,
				    sp.sp_flag);
			} else {
				sprintf(shadow, "%ld:%ld:%ld:%ld:%ld:%ld:%lu",
				    sp.sp_lstchg,
				    sp.sp_min,
				    sp.sp_max,
				    sp.sp_warn,
				    sp.sp_inact,
				    sp.sp_expire,
				    sp.sp_flag);
			}
			ecol[7].ec_value.ec_value_val = shadow;
			ecol[7].ec_value.ec_value_len = strlen(shadow) + 1;
			ecol[7].ec_flags = EN_CRYPT|EN_MODIFIED;
		}

		if (opwcmd || failover) {
			eobj = nis_clone_object(NIS_RES_OBJECT(passwd_res),
			    NULL);
			if (eobj == NULL) {
				sprintf(messages[0], "clone object failed");
				(void) display_errmsg(ia_convp->start_conv, 0,
				    1, messages, NULL);
				return (IA_FATAL);
			}
			eobj->EN_data.en_cols.en_cols_val = ecol;
			eobj->EN_data.en_cols.en_cols_len = 8;
			/* strlen("[name=],passwd.") + null + "." = 17 */
			if ((strlen(usrname) +
			strlen(NIS_RES_OBJECT(passwd_res)->zo_domain) + 17) >
				(size_t) NIS_MAXNAMELEN) {
				sprintf(messages[0], "not enough buffer space");
				(void) display_errmsg(ia_convp->start_conv, 0,
				    1, messages, NULL);
				return (IA_FATAL);
			}
			sprintf(mname, "[name=%s],passwd.%s", usrname,
			    NIS_RES_OBJECT(passwd_res)->zo_domain);
			if (mname[strlen(mname) - 1] != '.')
				(void) strcat(mname, ".");
			mres = nis_modify_entry(mname, eobj, 0);
			if (mres->status != NIS_SUCCESS) {
				sprintf(messages[0],
			"Password information update failed (update_attr)\n");
				(void) display_errmsg(
				    ia_convp->start_conv, 0, 1, messages, NULL);
				return (IA_FATAL);
			}

			sprintf(messages[0],
			    "\tNIS+ password information changed for %s\n",
			    usrname);
			(void) display_errmsg(
			    ia_convp->start_conv, 0, 1, messages, NULL);
		}
	}
	return (IA_SUCCESS);
}

/*
 * Return reencrypted secret key.
 * The first two if statements should always succeed as these tests
 * are also carried out in getnewpasswd().
 */
static char *
reencrypt_secret(char *oldsecret, char *oldpass, char *newpass)
{
	static char crypt[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];

	if (xdecrypt(oldsecret, oldpass) == 0)
		return (NULL); /* cbc_crypt failed */

	if (memcmp(oldsecret, &(oldsecret[HEXKEYBYTES]), KEYCHECKSUMSIZE) != 0)
		return (NULL); /* didn't really decrypt */

	(void) memcpy(crypt, oldsecret, HEXKEYBYTES);
	(void) memcpy(crypt + HEXKEYBYTES, oldsecret, KEYCHECKSUMSIZE);
	crypt[HEXKEYBYTES + KEYCHECKSUMSIZE] = 0;

	if (xencrypt(crypt, newpass) == 0)
		return (NULL); /* cbc_crypt encryption failed */

	return (crypt);
}


/*
 * Revert back to the old passwd
 */
static nis_error
revert2oldpasswd(char *usrname)
{
	entry_col ecol[8];
	nis_object *eobj;
	nis_result *mres;
	char mname[NIS_MAXNAMELEN];

	/*
	 * clear column data
	 */
	(void) memset((char *) ecol, 0, sizeof (ecol));

	/*
	 * passwd (col 1)
	 */
	ecol[1].ec_value.ec_value_val =
		ENTRY_VAL(NIS_RES_OBJECT(passwd_res), 1);
	ecol[1].ec_value.ec_value_len =
		ENTRY_LEN(NIS_RES_OBJECT(passwd_res), 1);
	ecol[1].ec_flags = EN_CRYPT|EN_MODIFIED;

	/*
	 * build entry based on the global "passwd_res"
	 */
	eobj = nis_clone_object(NIS_RES_OBJECT(passwd_res), NULL);
	if (eobj == NULL)
		return (NIS_SYSTEMERROR);
	eobj->EN_data.en_cols.en_cols_val = ecol;
	eobj->EN_data.en_cols.en_cols_len = 8;

	sprintf(mname, "[name=%s],passwd.%s", usrname,
		NIS_RES_OBJECT(passwd_res)->zo_domain);

	mres = nis_modify_entry(mname, eobj, 0);
	return (mres->status);
}
