
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_auth_acctmg.c 1.16     94/07/05 SMI"

#include "unix_headers.h"

/*
 * sa_check_for_login_inactivity	- Check for login inactivity
 *
 */

static int
sa_check_for_login_inactivity(pwd, shpwd, remote_host, ia_convp, ttyn)
	struct 	passwd 	*pwd;
	struct 	spwd 	*shpwd;
	char 	*remote_host;
	struct	ia_conv	*ia_convp;
	char	*ttyn;
{
	int		fdl;
	struct lastlog	ll;
	struct lastlog	newll;
	int		retval;

	if ((fdl = open(LASTLOG, O_RDWR|O_CREAT, 0444)) >= 0) {
		/*
		 * Read the last login (ll) time
		 */
		(void) lseek(fdl,
		    (long)pwd->pw_uid * sizeof (struct lastlog), 0);

		retval =  read(fdl, (char *)&ll, sizeof (ll));

		(void) lseek(fdl,
			(long)pwd->pw_uid * sizeof (struct lastlog), 0);

		(void) time(&newll.ll_time);
		SCPYN(newll.ll_line, (ttyn+sizeof ("/dev/")-1));
		SCPYN(newll.ll_host, remote_host);

		/* Check for login inactivity */

		if ((shpwd->sp_inact > 0) && ll.ll_time &&
					(retval = sizeof (ll)))
			if (((ll.ll_time / DAY) + shpwd->sp_inact) < DAY_NOW) {
				/*
				 * Account inactive for too long
				 */
				(void) close(fdl);
				/* audit_login_main13(); */
				return (1);
			}

		(void) write(fdl, (char *)&newll, sizeof (newll));
		(void) close(fdl);
	}
	return (0);
}


/*
 * sa_do_new_passwd_if_needed()
 *			- Get a new password if the password has exipired
 *			  Returns: 0 on sucess, exlse an exec error.
 */

static int
sa_do_new_passwd_if_needed(pwd, shpwd, passwd_req, ia_convp)
	struct 	passwd 	*pwd;
	struct 	spwd 	*shpwd;
	struct	ia_conv	*ia_convp;
	int 	passwd_req;
{
	int 	n = 0;
	int	now  = DAY_NOW;

	/*
	 * We want to make sure that we only kick off /usr/bin/passwd if
	 * passwords are required for the system, the user does not
	 * have a password, AND the user's NULL password can be changed
	 * according to its password aging information
	 */

	if ((passwd_req != 0) && (shpwd->sp_pwdp[0] == '\0')) {
		if ((pwd->pw_uid != 0) &&
		    ((shpwd->sp_max == -1) || (shpwd->sp_lstchg > now) ||
		    ((now >= shpwd->sp_lstchg + shpwd->sp_min) &&
		    (shpwd->sp_max >= shpwd->sp_min)))) {

			return (IA_NEWTOK_REQD);
		}
	}
	return (IA_SUCCESS);
}



/*
 * sa_perform_passwd_aging_check
 *		- Check for password exipration.
 *		  Returns: 0 on success, else error
 */

static	int
sa_perform_passwd_aging_check(pwd, shpwd, ia_convp)
	struct 	passwd 	*pwd;
	struct 	spwd 	*shpwd;
	struct	ia_conv	*ia_convp;
{
	int 	now	= DAY_NOW;
	int	Idleweeks	= -1;
	char	*ptr;
	int 	n = 0;
	char	messages[MAX_NUM_MSG][MAX_MSG_SIZE];


	if (defopen(LOGINADMIN) == 0) {
		if ((ptr = defread("IDLEWEEKS=")) != NULL)
			Idleweeks = atoi(ptr);
		(void) defopen(NULL);
	}

	if ((shpwd->sp_lstchg == 0) ||
	    (shpwd->sp_lstchg > now) ||
	    ((shpwd->sp_max >= 0) &&
	    (now > (shpwd->sp_lstchg + shpwd->sp_max)) &&
	    (shpwd->sp_max >= shpwd->sp_min))) {
		if ((Idleweeks == 0) ||
		    ((Idleweeks > 0) &&
		    (now > (shpwd->sp_lstchg + (7 * Idleweeks))))) {
			sprintf(messages[0], dgettext(PAMTXD,
			"Your password has been expired for too long; \
			please contact the system administrator\n"));
			display_errmsg(ia_convp->cont_conv, 0, 1, messages,
			NULL);
			/* audit_login_main18(); */
			return (IA_SCHERROR);
		} else {
			return (IA_NEWTOK_REQD);
		}
	}
	/* audit_login_main20(); */
	return (IA_SUCCESS);
}

/*
 * sa_warn_user_passwd_will_expire	- warn the user when the password will
 *					  expire.
 */

static void
sa_warn_user_passwd_will_expire(pwd, shpwd, ia_convp)
	struct 	passwd 	*pwd;
	struct 	spwd 	*shpwd;
	struct	ia_conv	*ia_convp;
{
	int 	now	= DAY_NOW;
	char	messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	int	days;

	/*
	 * XXX - replace error message with call to prompt function
	 */
	if ((shpwd->sp_warn > 0) && (shpwd->sp_max > 0) &&
	    (now + shpwd->sp_warn) >= (shpwd->sp_lstchg + shpwd->sp_max)) {
		days = (shpwd->sp_lstchg + shpwd->sp_max) - now;
		if (days <= 0)
			sprintf(messages[0], dgettext(PAMTXD,
			    "Your password will expire within 24 hours.\n"));
		else if (days == 1)
			sprintf(messages[0], dgettext(PAMTXD,
			    "Your password will expire in %d day.\n"), days);
		else
			sprintf(messages[0], dgettext(PAMTXD,
			    "Your password will expire in %d days.\n"), days);
		display_errmsg(ia_convp->cont_conv, 0, 1, messages, NULL);
	}
}


/*
 * sa_auth_acctmg	- main account managment routine.
 *			  Returns: scheme error or specific error on failure
 */

int
sa_auth_acctmg(iah, flags, pwd, ia_status)
	void	*iah;
	int	flags;
	struct passwd **pwd;
	struct ia_status *ia_status;
{
	struct 	spwd *shpwd;
	int 	error;
	char    *program, *user, *ttyn, *rhost;
	struct  ia_conv *ia_convp;
	int	err;

	if ((err = sa_getall(iah, &program, &user, &ttyn, &rhost, &ia_convp))
	    != IA_SUCCESS)
		return (err);

	/*
	 * Get the password and shadow password entries
	 */
	if (sa_get_pwd(pwd, &shpwd, user) == ERROR) {
		return (IA_SCHERROR);
	}

	/*
	 * Check for account expiration
	 */
	if (shpwd->sp_expire > 0) {
		if (shpwd->sp_expire < DAY_NOW) {
			return (IA_SCHERROR);
		}
	}

	/*
	 * Check for excessive login account inactivity
	 */
	if (error = sa_check_for_login_inactivity(*pwd, shpwd,
			rhost, ia_convp, ttyn)) {
		return (IA_SCHERROR);
	}

	/*
	 * If a password is required prompt the user now.
	 */
	if (error = sa_do_new_passwd_if_needed(*pwd, shpwd,
			flags, ia_convp)) {
		return (error);
	}

	/*
	 * Check to make sure password aging information is okay
	 */
	if (error = sa_perform_passwd_aging_check(*pwd, shpwd, ia_convp)) {
		return (error);
	}

	/*
	 * Finally, warn the user that their password is about to expire.
	 */
	sa_warn_user_passwd_will_expire(*pwd, shpwd, ia_convp);

	/*
	 * All done, return Success
	 */
	return (IA_SUCCESS);
}
