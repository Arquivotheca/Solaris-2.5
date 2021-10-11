/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)dump_collate.c	1.2	95/04/07 SMI"

/*
 * dump_collate -i input_fname
 */
#include "collate.h"
#define	OPTSTRING ":i:"
char *program;

main(int argc, char **argv)
{
	int i_flag = 0;		/* -i input_fname */
	int errorcnt = 0;
	int c;
	char *input_fname;
	int input_fd;
	extern int optind, opterr, optopt;
	extern char *optarg;
	opterr = 0;
	program = argv[0];

	/*
	 * Arguments handling
	 */
	while ((c = getopt(argc, argv, OPTSTRING)) != -1) {
		switch (c) {
		case 'i':
			i_flag++;
			input_fname = optarg;
			break;
		case ':':
			fprintf(stderr,
				"%s: option '%c' requires an argument.\n",
				program, optopt);
			errorcnt++;
			break;
		case '?':
			fprintf(stderr,
				"%s: illegal option '%c'.\n",
				program, optopt);
			errorcnt++;
			break;
		}
	}
	if (i_flag == 0 || errorcnt)
		usage();

	if ((input_fd = open(input_fname, O_RDONLY)) == -1) {
		fprintf(stderr,
		"%s: Can't open %s.\n", program,  input_fname);
		exit(1);
	}
	map_in_collate(input_fd);

	/*
	 * Dump collation file
	 */
	dump_header();
	dump_otm_section();
	dump_clm_section();
	dump_order_section();

	exit(0);
}

usage()
{
	fprintf(stderr, "usage: %s -i input_fname\n", program);
	exit(1);
}
