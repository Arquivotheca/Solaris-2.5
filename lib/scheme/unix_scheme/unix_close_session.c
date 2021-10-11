
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_close_session.c 1.7     94/05/19 SMI"

#include "unix_headers.h"

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
