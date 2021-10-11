/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)nscd_nischeck.c	1.2	94/12/09 SMI"

/*
 * Check permissions on NIS+ tables for security
 *
 * Usage: /usr/lib/nscd_nischeck <table>
 *
 * The speficied table is cacheable iff the exit code is zero.
 */

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>
#include <unistd.h>
extern int 	optind;
extern char	*optarg;

int
check_col(struct nis_object *table, int col)
{
  	struct table_col *c;
	c = table->zo_data.objdata_u.ta_data.ta_cols.ta_cols_val + col;
	return (NOBODY(c->tc_rights, NIS_READ_ACC));
}

int
main(int argc, char **argv)
{
	nis_result *tab;
	nis_object *obj;
	char namebuf[64];

	if (argc != 2) {
		(void)fprintf(stderr, "usage: %s cache_name\n", argv[0]);
		exit(1);
	}

	sprintf(namebuf, "%s.org_dir", argv[1]);
	tab = nis_lookup(namebuf, EXPAND_NAME);
	if (tab->status != NIS_SUCCESS) {
		nis_perror(tab->status, namebuf);
		exit(2);
	}

	obj = tab->objects.objects_val;
	if (NOBODY(obj->zo_access, NIS_READ_ACC))
		exit(0);

	if (strcmp(argv[1], "passwd") == 0 &&
	    check_col(obj, 0) &&
	    check_col(obj, 2) &&
	    check_col(obj, 3) &&
	    check_col(obj, 4) &&
	    check_col(obj, 5) &&
	    check_col(obj, 6))
		exit(0);

	if (strcmp(argv[1], "group") == 0 &&
	    check_col(obj, 0) &&
	    check_col(obj, 2) &&
	    check_col(obj, 3))
		exit(0);

	exit(1);
}
