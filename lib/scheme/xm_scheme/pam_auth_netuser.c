
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)pam_auth_netuser.c 1.12     94/09/16 SMI"

#include "pam_headers.h"

/*
 * sa_auth_netuser	- Checks if the user is allowed remote access
 */

int
sa_auth_netuser(iah, rusername, ia_status)
	void	*iah;
	char	*rusername;
	struct ia_status *ia_status;
{
	char *host = NULL, *lusername = NULL;
	struct passwd *pwd;
	struct spwd *shpwd;
	int	is_superuser;
	char    *program = NULL, *ttyn = NULL;
	struct  ia_conv *ia_convp;
	int	err;

	if ((err = sa_getall(iah,
	    &program, &lusername, &ttyn, &host, &ia_convp)) != IA_SUCCESS)
		return (err);

	if (get_pwd(&pwd, &shpwd, lusername))
		return (IA_SCHERROR);

	if (pwd->pw_uid == 0)
		is_superuser = 1;
	else
		is_superuser = 0;

	return (ruserok(host, is_superuser, rusername, lusername)
		== -1 ? IA_AUTHTEST_FAIL : 0);

}
