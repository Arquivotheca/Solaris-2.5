
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)pam_utils.c 1.21     95/01/27 SMI"

#include "pam_headers.h"


/* ******************************************************************** */
/*									*/
/* 		Utilities Functions					*/
/*									*/
/* ******************************************************************** */

/*
 * get_pwd():
 *	To get the passwd and shadow entry for specified user
 *	It returns ERROR if the user can't be found, else it
 *	returns OK.
 */

int
get_pwd(pwd, shpwd, user)
	struct 	passwd **pwd;
	struct 	spwd **shpwd;	/* Shadow password structure */
	char 	*user;
{
	static struct passwd lpwd;
	static struct spwd   lspwd;
	struct passwd *p;
	struct spwd   *sp;

	setpwent();
	if ((p = getpwnam(user)) == NULL) {
		endpwent();
		goto err;
	}
	endpwent();

		/* make private copy of passwd entry */
	if (lpwd.pw_name) {
		free(lpwd.pw_name);
		free(lpwd.pw_passwd);
		free(lpwd.pw_age);
		free(lpwd.pw_comment);
		free(lpwd.pw_gecos);
		free(lpwd.pw_dir);
		free(lpwd.pw_shell);
	}
	lpwd = *p;

	if ((lpwd.pw_name = strdup(p->pw_name)) == (char *)0)
		goto err;

	if (p->pw_passwd != NULL) {
		if ((lpwd.pw_passwd = strdup(p->pw_passwd)) == (char *)0)
			goto err;
	} else {
		if ((lpwd.pw_passwd = strdup("")) == (char *)0)
			goto err;
	}

	if ((lpwd.pw_age = strdup(p->pw_age)) == (char *)0)
		goto err;
	if ((lpwd.pw_comment = strdup(p->pw_comment)) == (char *)0)
		goto err;
	if ((lpwd.pw_gecos = strdup(p->pw_gecos)) == (char *)0)
		goto err;
	if ((lpwd.pw_dir = strdup(p->pw_dir)) == (char *)0)
		goto err;
	if ((lpwd.pw_shell = strdup(p->pw_shell)) == (char *)0)
		goto err;
	*pwd = &lpwd;

	(void) setspent();	/* Setting the shadow password file */
	if ((sp = getspnam(user)) == NULL) {
		(void) endspent();	/* Closing the shadow password file */
		goto err;
	}
	(void) endspent();	/* Closing the shadow password file */

		/* make private copy of shadow entry */
	if (lspwd.sp_namp) {
		free(lspwd.sp_namp);
		free(lspwd.sp_pwdp);
	}
	lspwd = *sp;

	if ((lspwd.sp_namp = strdup(sp->sp_namp)) == (char *)0)
		goto err;
	if (sp->sp_pwdp != NULL) {
		if ((lspwd.sp_pwdp = strdup(sp->sp_pwdp)) == (char *)0)
			goto err;
	} else {
		if ((lspwd.sp_pwdp = strdup("")) == (char *)0)
			goto err;
	}

	*shpwd = &lspwd;

	return (OK);

err:
	free(lpwd.pw_name);
	free(lpwd.pw_passwd);
	free(lpwd.pw_age);
	free(lpwd.pw_comment);
	free(lpwd.pw_gecos);
	free(lpwd.pw_dir);
	free(lpwd.pw_shell);
	memset(&lpwd, 0, sizeof (lpwd));

	free(lspwd.sp_namp);
	free(lspwd.sp_pwdp);
	memset(&lspwd, 0, sizeof (lspwd));

	*pwd = &nouser;
	*shpwd = &noupass;
	return (ERROR);
}

/*
 * Establish the Secure RPC secret key for the given uid using
 * the given password to decrypt the secret key, and store it with
 * the key service.
 *
 * If called with a nonzero 'reestablish' parameter, the key
 * is obtained from the name service, decrypted, and stored
 * even if the keyserver already has a key stored for the uid.
 * If the 'reestablish' parameter is zero, the function will not
 * try to reset the key.  It will return immediately with
 * SA_ESTKEY_ALREADY.
 *
 * Returns one of the following codes:
 *   SA_ESTKEY_ALREADY - reestablish flag was zero, and key was already set.
 *   SA_ESTKEY_SUCCESS - successfully obtained, decrypted, and set the key
 *   SA_ESTKEY_NOCREDENTIALS - the user has no credentials.
 *   SA_ESTKEY_BADPASSWD - the password supplied didn't decrypt the key
 *   SA_ESTKEY_CANTSETKEY - decrypted the key, but couldn't store key with
 *			    the key service.
 *
 * If netnamebuf is a non-NULL pointer, the netname will be returned in
 * netnamebuf, provided that the return status is not
 * SA_ESTKEY_NOCREDENTIALS or SA_ESTKEY_ALREADY.  If non-NULL, the
 * netnamebuf pointer must point to a buffer of length at least
 * MAXNETNAMELEN+1 characters.
 */

int
sa_establish_key(uid, password, reestablish, netnamebuf)
	uid_t   uid;
	int	reestablish;
	char 	*password;
	char    *netnamebuf;
{
	char    netname[MAXNETNAMELEN+1];
	struct 	key_netstarg netst;
	uid_t   orig_uid;

	orig_uid = geteuid();
	if (seteuid(uid) == -1)
		/* can't set uid */
		return (SA_ESTKEY_NOCREDENTIALS);


	if (!reestablish && key_secretkey_is_set()) {
		/* key is already established and we are not to reestablish */
		(void) seteuid(orig_uid);
		return (SA_ESTKEY_ALREADY);
	}


	if (!getnetname(netname)) {
		/* can't construct netname */
		(void) seteuid(orig_uid);
		return (SA_ESTKEY_NOCREDENTIALS);
	}

	if (!getsecretkey(netname, (char *) &(netst.st_priv_key), password)) {
		/* no secret key */
		(void) seteuid(orig_uid);
		return (SA_ESTKEY_NOCREDENTIALS);
	}

	if (netnamebuf) {
		/* return copy of netname in caller's buffer */
		(void) strcpy(netnamebuf, netname);
	}

	if (netst.st_priv_key[0] == 0) {
		/* password does not decrypt secret key */
		(void) seteuid(orig_uid);
		return (SA_ESTKEY_BADPASSWD);
	}


	/* secret key successfully decrypted at this point */

	/* store with key service */

	netst.st_netname = strdup(netname);
	(void) memset(netst.st_pub_key, 0, HEXKEYBYTES);

	if (key_setnet(&netst) < 0) {
		free(netst.st_netname);
		(void) seteuid(orig_uid);
		return (SA_ESTKEY_CANTSETKEY);
	}

	free(netst.st_netname);
	(void) seteuid(orig_uid);
	return (SA_ESTKEY_SUCCESS);
}

/*
 * free_msg():
 *	free storage for messages used in the call back "ia_conv" functions
 */

void
free_msg(num_msg, msg)
	int num_msg;
	struct ia_message *msg;
{
	int 			i;
	struct ia_message 	*m;

	m = msg;
	for (i = 0; i < num_msg; i++, m++)
		free(m->msg);
	free(msg);
}

/*
 * free_resp():
 *	free storage for responses used in the call back "ia_conv" functions
 */

void
free_resp(num_msg, resp)
	int num_msg;
	struct ia_response *resp;
{
	int			i;
	struct ia_response	*r;

	r = resp;
	for (i = 0; i < num_msg; i++, r++)
		free(r->resp);
	free(resp);
}

/*
 * display_errmsg():
 *	display error message by calling the call back functions
 *	provided by the application through "ia_conv" structure
 */

int
display_errmsg(conv_funp, conv_id, num_msg, messages, conv_apdp)
	int (*conv_funp)();
	int conv_id;
	int num_msg;
	char messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	void *conv_apdp;
{
	struct ia_message	*msg;
	struct ia_message	*m;
	struct ia_response	*resp;
	int			i;
	int			k;
	int			retcode;

	msg = (struct ia_message *)calloc(num_msg, sizeof (struct ia_message));
	if (msg == NULL) {
		return (IA_CONV_FAILURE);
	}
	m = msg;

	i = 0;
	k = num_msg;
	resp = NULL;
	while (k--) {
		/*
		 * fill out the ia_message structure to display error message
		 */
		m->msg_style = IA_ERROR_MSG;
		m->msg = (char *)malloc(MAX_MSG_SIZE);
		if (m->msg != NULL)
			(void) strcpy(m->msg, (const char *)messages[i]);
		else
			continue;
		m->msg_len = strlen(m->msg);
		m++;
		i++;
	}

	/*
	 * Call conv function to display the message,
	 * ignoring return value for now
	 */
	retcode = conv_funp(conv_id, num_msg, &msg, &resp, conv_apdp);
	free_msg(num_msg, msg);
	free_resp(num_msg, resp);
	return (retcode);
}

/*
 * get_authtok():
 *	get authentication token by calling the call back functions
 *	provided by the application through "ia_conv" structure
 */

int
get_authtok(conv_funp, conv_id, num_msg, messages, conv_apdp, ret_respp)
	int (*conv_funp)();
	int conv_id;
	int num_msg;
	char messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	void *conv_apdp;
	struct ia_response	**ret_respp;
{
	struct ia_message	*msg;
	struct ia_message	*m;
	int			i;
	int			k;
	int			retcode;

	i = 0;
	k = num_msg;

	msg = (struct ia_message *)calloc(num_msg, sizeof (struct ia_message));
	if (msg == NULL) {
		return (IA_CONV_FAILURE);
	}
	m = msg;

	while (k--) {
		/*
		 * fill out the message structure to display error message
		 */
		m->msg_style = IA_PROMPT_ECHO_OFF;
		m->msg = (char *)malloc(MAX_MSG_SIZE);
		if (m->msg != NULL)
			(void) strcpy(m->msg, (char *)messages[i]);
		else
			continue;
		m->msg_len = strlen(m->msg);
		m++;
		i++;
	}

	/*
	 * Call conv function to display the prompt,
	 * ignoring return value for now
	 */
	retcode = conv_funp(conv_id, num_msg, &msg, ret_respp, conv_apdp);
	free_msg(num_msg, msg);
	return (retcode);
}
