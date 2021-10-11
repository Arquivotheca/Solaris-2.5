
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)pam_close_session.c 1.8     94/09/16 SMI"

#include "pam_headers.h"

/*
 * sa_close_session	- Update utmp entry for process terminated with
 *			  a PAM authenticated session
 */

int
sa_close_session(iah, flags, pid, status, id, out)
	void	*iah;
	int	flags;
	pid_t	pid;
	int	status;
	char	id[];
	struct ia_status	*out;
{
	return (0);
}
