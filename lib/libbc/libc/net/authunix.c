#pragma ident	"@(#)authunix.c	1.2	92/07/20 SMI" 

/* 
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */


#include <rpc/types.h>
#include <rpc/auth.h>

#undef authunix_create
#undef authunix_create_default

AUTH *
authunix_create(machname, uid, gid, len, aup_gids)
	char *machname;
	uid_t uid;
	gid_t gid;
	register int len;
	gid_t *aup_gids;
{
	return(authsys_create(machname, uid, gid, len, aup_gids));
}



AUTH *
authunix_create_default()
{
	return(authsys_create_default());
}
