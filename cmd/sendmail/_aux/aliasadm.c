/*
 * Copyright (c) 1991, 1992, 1993 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <string.h>
#include "conf.h"
#include <stdlib.h>
#undef NIS /* symbol conflict in nis.h */
#include <rpcsvc/nis.h>
#include "nisplus.h"

# define DEFAULT_ALIAS_MAP "mail_aliases.org_dir"

FILE *fp = NULL;
char *domain, *alias_map;
char *match_arg;

struct nis_mailias alias = {NULL, NULL, NULL, NULL};


t_mode mode;

main(argc, argv)
	int argc;
	char **argv;
{
	int i;
	nis_result *res;

	print_comments = TRUE;

	alias_map = DEFAULT_ALIAS_MAP;

	if ((domain = nis_local_directory()) == NULL) {
		fprintf(stderr, "Can't get current domain\n");
		exit(-1);
	}

	argparse(argc, argv);

	switch (mode) {
	case ADD:
		nis_mailias_add(alias, alias_map, domain);
		break;
	case CHANGE:
		nis_mailias_change(alias, alias_map, domain);
		break;
	case DELETE:
		nis_mailias_delete(alias, alias_map, domain);
		break;
	case MATCH:
		res = nis_mailias_match(match_arg, 
					alias_map, domain, ALIAS_COL);
		if (res->status == SUCCESS) {
			int i;
			for (i = 0; i < res->objects.objects_len; i++)
				mailias_print(fp? fp: stdout, 
					      (&res->objects.objects_val[0])+i);
		}
		break;
	case LIST:
		nis_mailias_list(fp? fp: stdout, alias_map, domain);
		break;
	case INIT:
		nis_mailias_init(alias_map, domain);
		break;
	case EDIT:
		nis_mailias_edit(fp, alias_map, domain);
		break;
	case NONE:
	default:
		usage(argv[0]);
		exit(-1);
		break;
	}
	exit(0);
}

argparse(argc, argv)
	int argc;
	char **argv;
{
	int c;
	int narg;
	int ind;
	
	mode = NONE;

	while ((c = getopt(argc, argv, "D:M:f:a:c:d:m:leIn")) != EOF) {
		ind = (int) optind;  /* optind doesn't seem to be recognized
				      * as an extern int (which it is)
				      * for now cast it
				      */
		switch(c) {
		case 'a':
			mode = ADD;
			narg = argc - ind + 1;
			if (narg < 2) {
				usage(argv[0]);
				fprintf(stderr,"Invalid argument\n");
				exit(-1);
			}
			alias.name = strdup(optarg);
			alias.expn = strdup(argv[ind]);
			if (narg >= 3 && *argv[ind + 1] != '-')
				alias.comments = strdup(argv[ind + 1]); 
			if (narg >= 4 && *argv[ind + 1] != '-' && 
			    *argv[ind + 2] != '-') {
				alias.options = strdup(argv[ind + 2]); 
			}
			break;
		case 'c':
			mode = CHANGE;
			narg = argc - ind + 1;
			if (narg < 2) {
				usage(argv[0]);
				fprintf(stderr,"Invalid argument\n");
				exit(-1);
			}
			alias.name = optarg;
			alias.expn = strdup(argv[ind]);
			if (narg >= 3 && *argv[ind + 1] != '-')
				alias.comments = strdup(argv[ind + 1]); 
			if (narg >= 4 && *argv[ind + 1] != '-' && 
			    *argv[ind + 2] != '-') {
				alias.options = strdup(argv[ind + 2]); 
			}
			break;
		case 'D':
			domain = strdup(optarg);
			break;
		case 'd':
			mode = DELETE;
			alias.name = strdup(optarg);
			break;
		case 'M':
			alias_map = strdup(optarg);
			break;
		case 'm':

			mode = MATCH;
			match_arg = strdup(optarg);
			break;
		case 'n':
			print_comments = FALSE;
			break;
		case 'f':
			fp = fopen(optarg, "a+");
			if (fp == NULL) {
				fprintf(stderr, "%s:", optarg);
				perror("Can not open:");
				exit(-1);
			}
			break;
		case 'e':
			mode = EDIT;
			break;
		case 'l':
			mode = LIST;
			break;
		case 'I':
			mode = INIT;
			break;
		default:
			fprintf(stderr,"Invalid argument\n");
			usage(argv[0]);
			exit(-1);
			break;
		}
	}
}

usage(pname)
	char *pname;
{
	fprintf(stderr,
		"usage:\t%s -a alias expansion [comments] [options]\n", pname);
	fprintf(stderr,"\t%s -c alias expansion [comments] [options]\n", pname);
	fprintf(stderr, "\t%s -e\n", pname);
	fprintf(stderr,"\t%s -d alias\n", pname);
	fprintf(stderr, "\t%s -m alias\n", pname);
	fprintf(stderr, "\t%s -l\n", pname);
}
