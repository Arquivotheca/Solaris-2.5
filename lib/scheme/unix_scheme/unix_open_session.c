
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_open_session.c 1.10     94/05/19 SMI"

/*
 * sa_open_session 	- Updates the utmp structure with the user name
 *			  tty line and remote host, if applicable
 */

#include "unix_headers.h"


int
sa_open_session(iah, flags, type, id, out)
	void	*iah;
	int	flags;
	int	type;
	char 	id[];
	struct 	ia_status	*out;
{
	return (0);
}
