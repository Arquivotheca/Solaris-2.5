/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)pam_auth_user.c 1.15     94/11/01 SMI"

#include "pam_headers.h"
int unix_attempt_cnt = 0;



/*
 * sa_auth_user		- Authenticate user
 */

int
sa_auth_user(iah, flags, pwd, ia_status)
	void	*iah;
	struct 	passwd **pwd;
	int 	flags;
	struct 	ia_status 	*ia_status;
{
	struct 		spwd *shpwd; /* Shadow password structure */
	char 		*password;
	char		*program, *user, *ttyn, *rhost;
	struct ia_conv 	*ia_convp;
	int		err = 0;
	struct ia_response *ret_resp = (struct ia_response *)0;
	int		retcode;
	char 		messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	int 		num_msg = 0;
	int		estkey_stat;


	if ((err = sa_getall(iah, &program, &user, &ttyn, &rhost, &ia_convp))
	    != IA_SUCCESS)
		return (err);

	/*
	 * Get the password and shadow password entry
	 */

	if (get_pwd(pwd, &shpwd, user) == ERROR) {
		err = IA_NO_PWDENT;
		if ((flags & AU_CONTINUE) == 0)
			goto out;
	}

	/*
	 * Is a password check required?
	 */
	if (!((flags & AU_CHECK_PASSWD) || (flags & AU_PASSWD_REQ)))
		/* No password check is required */
		goto out;


	/* Yes, we're supposed to check the password. */

	/* Is there anything there to check? */
	if ((shpwd->sp_pwdp == 0) || (*shpwd->sp_pwdp == '\0')) {
		/*
		 * No. There is definitely nothing there
		 * to check. If a password is required, then
		 * we signal an error.
		 */
		if (flags & AU_PASSWD_REQ)
			err = IA_SCHERROR;
		goto out;
	}


	/*
	 * Yes, there is some string in the sp_pwdp field.
	 * We have one of the following two situations:
	 *    1) the sp_pwdp string is actually the encrypted
	 *	 user's password,
	 * or 2) the sp_pwdp string is "*NP*", which means we
	 *	 didn't actually have permission to read the
	 *	 password field in the name service.
	 *
	 * In either case, we must obtain the password from the user.
	 * In situation 2, we can't actually tell yet whether the unix
	 * password is present or not.  We must get the password from
	 * the user, just to establish the user's secure RPC
	 * credentials.  Then, having established the user's Secure
	 * RPC credentials, we need to re-obtain the shpwd structure.
	 * At that point, if the unix password is present there, we
	 * check it against that too.
	 */


	/*
	 * Get the password from the user
	 */

	sprintf(messages[0], dgettext(PAMTXD, "Password: "));
	num_msg = 1;
	retcode = get_authtok(ia_convp->start_conv, 0,
				num_msg, messages, NULL,
				&ret_resp);
	if (retcode != IA_SUCCESS) {
		if (err != IA_NO_PWDENT)
			err = retcode;
		goto out;
	}

	password = ret_resp->resp;

	if (password == NULL) {
		/* Need a password to proceed */
		(void) sleep(SLEEPTIME);
		if (err != IA_NO_PWDENT)
			err = IA_AUTHTEST_FAIL;
		goto out;
	}


	/*
	 * If the password string in the shpwd structure is
	 * "*NP*", try to establish Secure RPC
	 * credentials for the user and re-obtain
	 * the password entry.
	 */

	if (strcmp(shpwd->sp_pwdp, "*NP*") == 0) {
		/*
		 * Attempt to establish Secure RPC credentials with the
		 * password that the user has typed.
		 */
		estkey_stat = sa_establish_key((*pwd)->pw_uid, password,
						1, NULL);
		if (estkey_stat != SA_ESTKEY_SUCCESS) {
			/* Failed to establish secret key. */
			if (err != IA_NO_PWDENT) {
				switch (estkey_stat) {
				    case SA_ESTKEY_BADPASSWD:
					(void) sleep(SLEEPTIME);
					err = IA_AUTHTEST_FAIL;
					break;
				    case SA_ESTKEY_NOCREDENTIALS:
					/*
					 * user requires credentials to read
					 * passwd field but doesn't have any
					 * should syslog() a message for admin
					 */
					syslog(LOG_ALERT,
						"User %s needs Secure RPC \
credentials to login, but has none.", user);
					err = IA_SCHERROR;
					break;
				    default:
					err = IA_SCHERROR;
				}
			}
			goto out;
		}

		/*
		 * Get the password and shadow password entry
		 * (this time having established the user's key)
		 */

		get_pwd(pwd, &shpwd, user);

		/*
		 * If at this point, the shadow password field
		 * is still"*NP*", it means the user
		 * even with credentials, is unable to
		 * read his own password entry.   We can't continue,
		 * and should return a configuration error.
		 */
		if ((shpwd->sp_pwdp != 0) &&
		    (strcmp(shpwd->sp_pwdp, "*NP*") == 0)) {
			syslog(LOG_ALERT,
				"Permissions on the password database may \
be too restrictive.");
			err = IA_SCHERROR;
			goto out;
		}
	}

	/*
	 * At this point, shpwd->sp_pwdp has the actual
	 * encrypted password, if present.
	 * /

	/* Is it present? */
	if ((shpwd->sp_pwdp == 0) || (*shpwd->sp_pwdp == '\0')) {
		/*
		 * No. There is no password
		 * to check. If one is required then
		 * we signal an error.
		 */
		if (flags & AU_PASSWD_REQ)
			err = IA_SCHERROR;
		goto out;
	}

	/* Yes, check it */
	if (strcmp(crypt(password, shpwd->sp_pwdp),
		    shpwd->sp_pwdp)) {
		(void) sleep(SLEEPTIME);
		if (err != IA_NO_PWDENT)
			err = IA_AUTHTEST_FAIL;
		goto out;
	}


	/*
	 * Save the clear text password.
	 */

	ia_set_item(iah, IA_AUTHTOK, password);


out:

	/*
	 * XXX - ia_status is supposed to return AU_PWD_ENTERED if a password
	 * was entered.
	 */
	if (num_msg > 0) {
		if (ret_resp != 0) {
			if (ret_resp->resp != 0) {
				/* avoid leaving password cleartext around */
				memset(ret_resp->resp, 0,  ret_resp->resp_len);
			}
			free_resp(num_msg, ret_resp);
			ret_resp = 0;
		}
	}

	if (err == 0)
		return (IA_SUCCESS);

	if (++unix_attempt_cnt >=  MAXTRYS)
	{
		return (IA_MAXTRYS);
	}
	else
		return (err);

}
