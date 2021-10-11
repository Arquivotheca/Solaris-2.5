/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)fnlookup.cc	1.4	94/11/08 SMI"


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <xfn/xfn.hh>

static char	*target_name = 0;
static unsigned	target_detail = 0;
static unsigned follow_link = 0;

// Options:
// -L	If the composite name is bound to an XFN link, lookup the
//	reference that the link references, rather than the link itself.
// -v	run in verbose mode
//
void
usage(char *cmd, char *msg = 0)
{
	if (msg)
		fprintf(stderr, "%s: %s\n", cmd, msg);
	fprintf(stderr, "Usage:\t%s [-vL] composite_name\n", cmd);
	exit(1);
}

void
process_cmd_line(int argc, char **argv)
{
	int c;
	while ((c = getopt(argc, argv, "Lv")) != -1) {
		switch (c) {
		case 'v' :
			target_detail = 2;
			break;
		case 'L':
			follow_link = 1;
			break;
		case '?':
			default :
			usage(argv[0], "invalid option");
		}
	}
	if (optind < argc)
		target_name = argv[optind++];
	else
		usage(argv[0], "missing composite name");

	if (optind < argc)
		usage(argv[0], "too many arguments");
}


// returns 1 on failure and 0 on success

int
lookup_and_print(FN_ctx *ctx, const FN_string &name)
{
	FN_status status;
	FN_ref* ref = 0;
	FN_string *desc;

	if (follow_link)
		ref = ctx->lookup(name, status);
	else {
		// Try link first
		const FN_ref *real_ref;
		ref = ctx->lookup_link(name, status);
		// If terminal name was not a link, return it as answer
		if (status.code() == FN_E_MALFORMED_LINK &&
		    status.remaining_name() == 0 &&
		    (real_ref = status.resolved_ref())) {
			ref = new FN_ref(*real_ref);
			status.set_success();
	    }
	}
	if (ref) {
		desc = ref->description(target_detail);
		printf("%s", desc? (char *)(desc->str()) : "No description");
		delete ref;
		if (desc)
			delete desc;
		return (0);
	} else {
		desc = status.description();
		printf("lookup of '%s' failed: %s\n",
		    (char *)(name.str()),
		    (char *)(desc->str()));
		delete desc;
		return (1);
	}
}

int
main(int argc, char **argv)
{
	process_cmd_line(argc, argv);

	FN_status status;
	FN_ctx* ctx = FN_ctx::from_initial(status);

	if (ctx == 0) {
		printf("Unable to get initial context! %s\n",
		    (char *)(status.description()->str()));
		exit(1);
	}

	exit(lookup_and_print(ctx, (unsigned char *)target_name));
}
