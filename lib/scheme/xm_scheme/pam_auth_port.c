
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)pam_auth_port.c 1.13     94/11/01 SMI"

#include "pam_headers.h"

/*
 * sa_dialpass		- Check the dial password for the specified line
 *			  Returns: 0 on success, 1 on failure.
 */

int
sa_dialpass(ttyn, ia_convp, u_name)
	char 	*ttyn;
	char 	*u_name;
	struct 	ia_conv	*ia_convp;
{
	register FILE 	*fp;
	char 		defpass[30];
	char 		line[80];
	char 		pwdbuf[80];
	register char 	*p1, *p2;
	struct passwd 	*pwd;
	struct spwd 	*shpwd;
	char		*password;
	static struct ia_response *ret_resp;
	int		retcode;
	char 		messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	int 		num_msg;

	get_pwd(&pwd, &shpwd, u_name);

	if ((fp = fopen(DIAL_FILE, "r")) == NULL)
		return (0);

	while ((p1 = fgets(line, sizeof (line), fp)) != NULL) {
		while (*p1 != '\n' && *p1 != ' ' && *p1 != '\t')
			p1++;
		*p1 = '\0';
		if (strcmp(line, ttyn) == 0)
			break;
	}

	(void) fclose(fp);

	if (p1 == NULL || (fp = fopen(DPASS_FILE, "r")) == NULL)
		return (0);

	defpass[0] = '\0';
	p2 = 0;

	while ((p1 = fgets(line, sizeof (line)-1, fp)) != NULL) {
		while (*p1 && *p1 != ':')
			p1++;
		*p1++ = '\0';
		p2 = p1;
		while (*p1 && *p1 != ':')
			p1++;
		*p1 = '\0';
		if (pwd->pw_shell != NULL && strcmp(pwd->pw_shell, line) == 0)
			break;

		if (strcmp(SHELL, line) == 0)
			SCPYN(defpass, p2);
		p2 = 0;
	}

	(void) fclose(fp);

	if (!p2)
		p2 = defpass;

	if (*p2 != '\0') {
		strcpy(messages[0], dgettext(PAMTXD, DIALUP_PASSWD_MSG));
		num_msg = 1;
		retcode = get_authtok(ia_convp->start_conv, 0,
				num_msg, messages, NULL, &ret_resp);
		if (retcode != IA_SUCCESS)
			return (retcode);
		password = ret_resp->resp;
		if (strcmp(crypt(password, p2), p2)) {
			free_resp(num_msg, ret_resp);
			(void) sleep(SLEEPTIME);
			return (ERROR);
		}
		free_resp(num_msg, ret_resp);
	}

	return (OK);
}


/*
 * sa_auth_port		- This is the top level function in the scheme called
 *			  by ia_auth_port in the framework
 *			  Returns: IA_SCHERROR on failure, 0 on success
 */

static int  port_attempt_cnt;

int
sa_auth_port(iah, flags, ia_status)
	void	*iah;
	int	flags;
	struct	ia_status *ia_status;
{
	int	return_value;
	char	*ttyn, *user;
	struct	ia_conv	*ia_convp;

	if ((return_value = sa_getall(iah, NULL, &user, &ttyn, NULL, &ia_convp))
	    != IA_SUCCESS)
		return (return_value);

	if (sa_dialpass(ttyn, ia_convp, user)) {
		if (flags != AP_NOCNT) {
			if (++port_attempt_cnt >= MAXTRYS) {
				/*
				 * reset count now that we have overflowed.
				 * It is up to the utility to abort.
				 */
				port_attempt_cnt = 0;
				return_value = IA_MAXTRYS;
			} else
				return_value = IA_SCHERROR;
		}
		else
			return_value = IA_SCHERROR;
	} else
		return_value = IA_SUCCESS;

	return (return_value);
}
