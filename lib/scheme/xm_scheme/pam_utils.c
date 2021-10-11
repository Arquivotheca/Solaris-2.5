
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)pam_utils.c 1.20     94/11/01 SMI"

#include "pam_headers.h"

static	struct	passwd nouser = { "", "no:password", ~ROOTUID };
static	struct	spwd noupass = { "", "no:password" };

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
	 * Call conv function to display the prompt.
	 */
	retcode = conv_funp(conv_id, num_msg, &msg, ret_respp, conv_apdp);
	free_msg(num_msg, msg);
	return (retcode);
}
