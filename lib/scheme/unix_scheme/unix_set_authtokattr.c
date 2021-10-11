
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_set_authtokattr.c 1.10     94/06/10 SMI"

#include	"unix_headers.h"

/*
 * sa_set_authtokattr():
 *	To set authentication token attribute values.
 *
 * 	This function calls ck_perm() to check the caller's
 *	permission.  If the check succeeds, It will
 *	call update_authentok_file() and passes attributes/value
 *	pairs pointed by "sa_setattr" to set the authentication
 *	token attribute values of the user specified by the
 *	authentication handle "iah".
 */

int
sa_set_authtokattr(iah, sa_setattr, ia_statusp, repository, nisdomain)
	void			*iah;
	char **			sa_setattr;
	struct	ia_status	*ia_statusp;
	int			repository;
	char			*nisdomain;
{
	register int	i;
	int		retcode;
	char 		*usrname;
	char 		*prognamep;
	struct ia_conv	*ia_convp;

	if ((retcode = sa_getall(iah, &prognamep,
	    &usrname, NULL, NULL, &ia_convp)) != IA_SUCCESS)
		return (retcode);

	dprintf1("set_authtokattr(): repository is %x\n", repository);
	/* repository must be specified in the command line. */
	if (repository == R_DEFAULT) {
		(void) fprintf(stderr,
	"You must specify repository when displaying passwd attributes\n");
		return (IA_FATAL);
	}

	retcode = ck_perm(prognamep, usrname, ia_convp, repository, nisdomain);
	if (retcode != 0)
		return (retcode);

	/* ignore all the signals */
	for (i = 1; i < NSIG; i++)
		(void) sigset(i, SIG_IGN);

	/* Clear the errno. It is set because SIGKILL can not be ignored. */
	errno = 0;

	/* update authentication token file */
	/* make sure the user exists before we update the repository */
	if (IS_NIS(repository) && (nis_pwd != NULL)) {
		retcode = update_authtok_nis(prognamep, usrname, "attr",
		    sa_setattr, ia_convp, NULL, NULL);
		return (retcode);
	}
	if (IS_NISPLUS(repository) && (nisplus_pwd != NULL)) {
		/* nis+ needs clear versions of old and new passwds */
		retcode = update_authtok_nisplus(prognamep, usrname, nisdomain,
		    "attr", sa_setattr, ia_convp, NULL, NULL, NULL,
		    IS_OPWCMD(repository) ? 1 : 0);
		return (retcode);
	}
	if (IS_FILES(repository) && (unix_pwd != NULL)) {
		retcode = update_authtok_file(prognamep, usrname, "attr",
		    sa_setattr, ia_convp);
		return (retcode);
	}
	return (0);
}
