
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_stubs.c 1.7     93/06/03 SMI"

#include "unix_headers.h"

extern int  unix_attempt_cnt;

sa_start(iah, program, user, ttyn, rhost, ia_conv)
	char *program;
	char *user;
	char *ttyn;
	char *rhost;
	struct ia_conv *ia_conv;
	void *iah;
{

	unix_attempt_cnt = 0;
	return (0);
}


sa_end(iah)
	void *iah;
{
	return (0);
}
