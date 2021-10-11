/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)fnrename.cc	1.3 94/12/09 SMI"


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <xfn/xfn.hh>

static int	arg_bind_supercede = 0;
static int	arg_verbose = 0;
static char	*arg_existing_name = 0;
static char	*arg_new_name = 0;
static char	*arg_target_context_name = 0;

void
usage(char *cmd, char *msg = 0)
{
	if (msg)
		fprintf(stderr, "%s: %s\n", cmd, msg);
	fprintf(stderr,
"Usage:\t%s [-sv]target_context_name existing_atomic_name  new_atomic_name\n",
	    cmd);
	exit(1);
}

void
process_cmd_line(int argc, char **argv)
{
	int c;
	while ((c = getopt(argc, argv, "sv")) != -1) {
		switch (c) {
		case 's' :
			arg_bind_supercede = 1;
			break;
		case 'v' :
			arg_verbose = 1;
			break;
		case '?':
			default :
			usage(argv[0], "invalid option");
		}
	}

	if (optind < argc)
		arg_target_context_name = argv[optind++];
	else
		usage(argv[0], "missing target context name argument");

	if (optind < argc)
		arg_existing_name = argv[optind++];
	else
		usage(argv[0], "missing existing name argument");

	if (optind < argc)
		arg_new_name = argv[optind++];
	else
		usage(argv[0], "missing new name argument");

	if (optind < argc)
		usage(argv[0], "too many arguments");
}

// returns 1 on failure and 0 on success

int
rename(FN_ctx *ctx,
    const FN_string &target_context_name,
    const FN_string &existing_name,
    const FN_string &new_name)
{
	FN_status status;
	FN_string *desc = 0;
	unsigned bind_flags = (arg_bind_supercede? 0: FN_OP_EXCLUSIVE);
	FN_ref *target_ref;
	FN_ctx *target_ctx;

	if (arg_verbose) {
		printf("renaming '%s' to '%s' in context of '%s':\n",
		    (char *)existing_name.str(),
		    (char *)new_name.str(),
		    (char *)target_context_name.str());
	}

	if ((target_ref = ctx->lookup(target_context_name, status)) == 0) {
		desc = status.description();
		fprintf(stderr, "lookup of '%s' failed: %s\n",
		    (char *)(target_context_name.str()),
		    (desc? (char *)desc->str() : "No status description"));
		delete desc;
		return (1);
	}

	if ((target_ctx = FN_ctx::from_ref(*target_ref, status)) == 0) {
		desc = status.description();
		fprintf(stderr,
		    "rename failed; cannot get context handle to '%s': %s\n",
		    (char *)(target_context_name.str()),
		    (desc? (char *)desc->str() : "No status description"));
		delete desc;
		return (1);
	}

	if (target_ctx->rename(existing_name, new_name, bind_flags,
	    status) == 0) {
		desc = status.description();
		fprintf(stderr,
		    "rename of '%s' to '%s' in context of '%s' failed: %s\n",
		    (char *)(existing_name.str()),
		    (char *)(new_name.str()),
		    (char *)(target_context_name.str()),
		    (desc? (char *)desc->str() : "No status description"));
		delete desc;
		return (1);
	}
	return (0);
}


int
main(int argc, char **argv)
{
	process_cmd_line(argc, argv);

	FN_status status;
	FN_ctx* ctx = FN_ctx::from_initial(status);

	if (ctx == 0) {
		FN_string *desc = status.description();
		printf("Unable to get initial context! %s\n",
		    desc? (char *)(desc->str()): "No status description");
		if (desc)
			delete desc;
		exit(1);
	}

	exit(rename(ctx,
	    (unsigned char *)arg_target_context_name,
	    (unsigned char *)arg_existing_name,
	    (unsigned char *)arg_new_name));
}
