/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)fnunbind.cc	1.2	94/08/13 SMI"


#include <stdio.h>
#include <stdlib.h>

#include <xfn/xfn.hh>

void
usage(char *cmd)
{
	printf("Usage:\t%s composite_name\n", cmd);
	exit(1);
}


// returns 1 on failure and 0 on success
int
unbind(FN_ctx *ctx, const FN_string &name)
{
	FN_status status;

	if (ctx->unbind(name, status) == 0) {
		printf("unbind of '%s' failed: %s\n",
		    name.str(), status.description()->str());
		return (1);
	}
	return (0);
}

int
main(int argc, char **argv)
{
	if (argc != 2)
		usage(argv[0]);

	FN_status status;
	FN_ctx* ctx = FN_ctx::from_initial(status);

	if (ctx == 0) {
		printf("Unable to get initial context! %s\n",
		    status.description()->str());
		exit(1);
	}

	exit(unbind(ctx, (unsigned char *)(argv[1])));
}
