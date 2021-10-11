/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)main.c	1.19	95/03/06 SMI"

/*
 *  Character Mapping Handler, main routine
 *	charmap -i input_fname [-o output_fname] [-s skip_count]
 */
#include "../head/_localedef.h"
#include "../head/charmap.h"
#include <unistd.h>
#include "extern.h"
#define	DEFAULT_OUTPUT	"CharmapOutput"
#define	OPTSTRING ":i:o:s:"

char	*input_fname;
char	*output_fname;
static void update_lex();
static void	usage();

extern FILE		*input_file;
extern void		output(int);
extern void		skip_init_line(FILE *, char, int *);
extern int		yyparse(void);
extern int		skip_lines(FILE *, int);

int
main(int argc, char **argv)
{
	int		i_flag = 0;		/* -i input_fname */
	int		o_flag = 0;		/* -o output_fname */
	int		s_flag = 0;		/* -s skip_count */
	int		s_line = 0;		/* No. of lines to skip */
	char	*output_fname = DEFAULT_OUTPUT;
	int		charmap_fd;
	int		c;
	extern int	optind, opterr, optopt;
	extern char	*optarg;

	opterr = 0;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

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
		case 'o':
			o_flag++;
			output_fname = optarg;
			break;
		case 's':
			s_flag++;
			s_line = atoi(optarg);
			if (s_line < 0) {
				s_line = 0;
				s_flag = 0;
			}
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
			"%s: Can't open %s.\n"), program,  input_fname);
		exit(1);
	}

	if (s_flag != 0) {
		(void) skip_lines(input_file, s_line);
	}
	lineno += s_line;

	skip_init_line(input_file, comment_char, &lineno);

	/*
	 * Update yylex routines
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
		exit(1);
	}
	if ((charmap_fd = creat(output_fname, 0777)) == -1) {
		(void) fprintf(stderr, gettext(
			"%s: Can't create %s.\n"),
			program, output_fname);
		exit(1);
	}
	output(charmap_fd);
	(void) close(charmap_fd);
	exit(0);
}

static void
usage()
{
	(void) fprintf(stderr, gettext(
		"usage: %s -i input_fname [-o output_fname] [-s skip_count]\n"),
		program);
	exit(1);
}

static void
update_lex()
{
	extern int (*_isidentifier)();
	extern int is_alpha();

	_isidentifier = is_alpha;
}
