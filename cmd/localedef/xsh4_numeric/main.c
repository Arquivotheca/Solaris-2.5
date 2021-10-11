/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)main.c	1.8	95/03/06 SMI"

/*
 * Numeric table
 * xsh4_numeric -i input_fname [-f charmap] [-o output_fname] [-s skip_count]
 */
#include "numeric.h"
#include "extern.h"
#include <unistd.h>

#define	DEFAULT_OUTPUT	"numeric"
#define	OPTSTRING		":i:f:o:s:d"

static void		usage();
static void		update_lex();

extern int		output(int);
extern int		yyparse();
extern int		skip_lines(FILE *, int);
extern int		initialize_charmap(int, int);
extern void		skip_init_line(FILE *, char, int *);

int
main(int argc, char **argv)
{
	int		i_flag = 0;		/* -i input_fname */
	int		o_flag = 0;		/* -o output_fname */
	int		s_flag = 0;		/* -s skip_count */
	int		f_flag = 0;		/* -f charmap */
	int		s_line = 0;		/* No. of lines to skip */
	char	*output_fname = DEFAULT_OUTPUT;
	char	*charmap_fname;
	int		fd;
	int		charmap_fd;
	int		ret_val = 0;
	int		c;
	extern int	optind, opterr, optopt;
	extern char	*optarg;

	opterr = 0;

	program = argv[0];

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, OPTSTRING)) != -1) {
		switch (c) {
		case 'f':
			f_flag++;
			charmap_fname = optarg;
			break;
		case 'i':
			i_flag++;
			input_fname = optarg;
			break;
		case 'o':
			o_flag++;
			output_fname = optarg;
			break;
		case 's':
			s_line = atoi(optarg);
			s_flag++;
			if (s_line <= 0) {
				s_line = 0;
				s_flag = 0;
			}
			break;
		case 'd':
			d_flag++;
			break;
		case ':':
			(void) fprintf(stderr, gettext(
				"%s: option '%c' requires an argument.\n"),
				program, optopt);
			errorcnt++;
			break;
		case '?':
			(void) fprintf(stderr, gettext(
				"%s: illegal option '%c'.\n"),
				program, optopt);
			errorcnt++;
			break;
		}
	}

	if (i_flag == 0 || errorcnt) {
		usage();
	}

	/*
	 * Initialise
	 */
	if ((input_file = fopen(input_fname, "r")) == NULL) {
		(void) fprintf(stderr, gettext(
			"%s: Can't open %s.\n"), program, input_fname);
		exit(4);
	}

	if (s_flag != 0) {
		(void) skip_lines(input_file, s_line);
	}
	lineno += s_line;
	skip_init_line(input_file, comment_char, &lineno);

	if (f_flag != 0) {
		if ((charmap_fd = open(charmap_fname, 0)) == -1) {
			(void) fprintf(stderr, gettext(
				"%s: Can't open %s.\n"),
				program, charmap_fname);
			exit(4);
		}
	}
	(void) initialize_charmap(f_flag, charmap_fd);

	/*
	 * update lex
	 */
	update_lex();

	/*
	 * Parse Input File
	 */
	(void) yyparse();

	/*
	 * Create Output File
	 */
	if (errorcnt != 0) {
		(void) fprintf(stderr, gettext(
			"%d error(s) found. No output produced.\n"),
			errorcnt);
		exit(4);
	}
	if ((fd = creat(output_fname, 0777)) == -1) {
		(void) fprintf(stderr, gettext(
			"%s: Can't create %s.\n"),
			program, output_fname);
		exit(4);
	}
	if (output(fd) != 0) {
		ret_val = 1;
	}
	(void) close(fd);
	exit(ret_val);
}

static void
usage()
{
	(void) fprintf(stderr, gettext(
		"usage: %s -i input_fname [-f charmap] "
		"[-o output_fname] [-s skip_count]\n"),
		program);
	exit(4);
}

static void
update_lex()
{
	extern int	(*_isidentifier)();
	extern int	is_alpha();

	_isidentifier = is_alpha;
}
