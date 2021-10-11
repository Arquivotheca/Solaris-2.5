/*
 *	nistest.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nistest.c	1.6	94/10/14 SMI"

/*
 * nistest.c
 *
 * nis+ object test utility
 */

#include <stdio.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>

extern int 	optind;
extern char	*optarg;


#define	EXIT_TRUE 0
#define	EXIT_FALSE 1
#define	EXIT_ERROR 2

void
usage()
{
	fprintf(stderr,
	    "usage: nistest [-LPAM] [-t G|D|L|T|E|P] [-a mode] name\n");
	exit(EXIT_ERROR);
}

#define	TEST_TYPE 1
#define	TEST_ACCESS 2

unsigned int flags = 0;
zotypes	otype;
u_long oaccess;
int dotest_called = 0;

int
dotest (tab, obj, udata)
	char		*tab;
	nis_object	*obj;
	void		*udata;
{
	if ((flags & TEST_TYPE) &&
	    (obj->zo_data.zo_type != otype))
		exit(EXIT_FALSE);

	if ((flags & TEST_ACCESS) &&
	    ((obj->zo_access & oaccess) != oaccess))
		exit(EXIT_FALSE);

	dotest_called = 1;
}


main(argc, argv)
	int argc;
	char *argv[];
{
	int c;
	u_long flinks = 0, fpath = 0, allres = 0, master = 0;
	char *name;
	nis_result *ores;

	while ((c = getopt(argc, argv, "LPAMt:a:")) != -1)
		switch (c) {
		case 'L':
			flinks = FOLLOW_LINKS;
			break;
		case 'P':
			fpath = FOLLOW_PATH;
			break;
		case 'A':
			allres = ALL_RESULTS;
			break;
		case 'M':
			master = MASTER_ONLY;
			break;
		case 't':
			flags |= TEST_TYPE;
			switch (*optarg) {
				case 'G':
					otype = GROUP_OBJ;
					break;
				case 'D':
					otype = DIRECTORY_OBJ;
					break;
				case 'T':
					otype = TABLE_OBJ;
					break;
				case 'L':
					otype = LINK_OBJ;
					break;
				case 'E':
					otype = ENTRY_OBJ;
					break;
				case 'P':
					otype = PRIVATE_OBJ;
					break;
				default:
					usage();
					break;
			}
			break;
		case 'a':
			flags |= TEST_ACCESS;
			oaccess = 0;
			if (!parse_rights(&oaccess, optarg))
				usage();
			break;
		default:
			usage();
		}

	if (argc - optind != 1)
		usage();

	name = argv[optind];

	/*
	 * Get the object using expand name magic, and test it.
	 */
	if (*name == '[') {
		ores = nis_list(name,
		    fpath|allres|master|FOLLOW_LINKS|EXPAND_NAME, dotest, 0);
		if (ores->status != NIS_CBRESULTS)
			exit(EXIT_FALSE);
	} else {
		ores = nis_lookup(name, flinks|master|EXPAND_NAME);
		if (ores->status != NIS_SUCCESS)
			exit(EXIT_FALSE);
		dotest(0, ores->objects.objects_val, 0);
	}

	if (dotest_called)
		exit(EXIT_TRUE);
	
	exit(EXIT_FALSE);
}
