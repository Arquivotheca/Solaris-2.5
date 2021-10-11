/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)main.c	1.27	95/09/22 SMI"

/*
 * xsh4_collate -i input_fname [-v] [-f chamap]
 *			       [-o output_fname] [-s skip_count]
 *			       [-B]
 *			       [-l runtime_locale]
 */
#include "collate.h"
#include "extern.h"
#define	OPTSTRING		":Bdgi:vf:o:s:l:"
#define	DEFAULT_OUTPUT	"COLLTBL_OUTPUT"
char	*output_fname = DEFAULT_OUTPUT;
char	*run_locale = "C";
int	l_flag = 0;			/* run time locale */

static int	initialize_charmap(int, int);
static void	usage();
static void	update_lex();
#ifdef DEBUG
static void	debug_info();
#endif

extern int	skip_lines(FILE *, int);
extern int	yyparse();
extern int	output(char *, unsigned int, char *);
extern void	skip_init_line(FILE *, char, int *);
extern int	fillin_missing_weights();
extern int	yydebug;

int debug_flag = 0;

void
main(int argc, char **argv)
{
	int		f_flag = 0;		/* -f charmap */
	int		i_flag = 0;		/* -i input_fname */
	int		o_flag = 0;		/* -o output_fname */
	int		s_flag = 0;		/* -s skip_count */
	int		s_line = 0;		/* No. of lines to skip */
	unsigned int 	sort_flags = 0;		/* sortint type */
	int		charmap_fd;
	int		errorcnt = 0;
	int		c;
	extern int	opterr, optopt;
	extern char	*optarg;

	yydebug = 0;
	opterr = 0;
	program = argv[0];

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);
	/*
	 * Arguments handling
	 */
	while ((c = getopt(argc, argv, OPTSTRING)) != -1) {
		switch (c) {
		case 'B':
			sort_flags |= USE_BINARY;
			break;
		case 'd':
			debug_flag++;
			break;
		case 'l':
			l_flag++;
			run_locale = optarg;
			break;
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
		case 'v':
			v_flag++;
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
		default:
			break;
		}
	}


	if (i_flag == 0 || errorcnt) {
		usage();
	}

	/*
	 * Initialize
	 *	Open input file
	 *	Skip lines if skip line specified
	 *	Open charmap file if specified
	 */
	if ((input_file = fopen(input_fname, "r")) == NULL) {
		(void) fprintf(stderr, gettext(
			"%s: Can't open %s.\n"), program,  input_fname);
		exit(4);
	}

	if (s_flag != 0) {
		(void) skip_lines(input_file, s_line);
	}
	lineno += s_line;
	(void) skip_init_line(input_file, comment_char, &lineno);

	if (f_flag != 0) {
		if ((charmap_fd = open(charmap_fname, 0)) == -1) {
			(void) fprintf(stderr, gettext(
				"%s: Can't open %s.\n"),
				program, charmap_fname);
			exit(4);
		}
	}
	(void) initialize_charmap(f_flag, charmap_fd);
#ifdef DEBUG
	if (f_flag != 0) {
		dump_charmap();
	}
#endif

	/*
	 * Update yylex routine
	 */
	update_lex();

	/*
	 * Parse the input file
	 */
	(void) yyparse();

	/*
	 * Now go through the missing weights list and fill in the elements
	 * that are missing weights
	 */
	(void) fillin_missing_weights();

#ifdef DEBUG
	debug_info();
#endif

	/*
	 * Create Output
	 */
	if (syntax_errors || exec_errors) {
		(void) fprintf(stderr, gettext(
			"%s: No output created.\n"),
			program);
		exit(4);
	}

	/*
	 * If -l flag was not specified, then
	 * use the current running locale. This should be
	 * change.
	 */
	if (l_flag == 0)
		run_locale = (char *)strdup(setlocale(LC_CTYPE, 0));
	if (output(output_fname, sort_flags, run_locale) == ERROR) {
		exit(4);
	}
	exit(0);
}

/*
 * Set Up character mapping information
 */
static int
initialize_charmap(int flag, int fd)
{
	struct stat	stat;
	int		size;

	if (flag == 0) {
		/*
		 * No charmap file was specified.
		 * Use default mapping.
		 */
		return (0);
	} else {
		char	*a;

		if (fstat(fd, &stat) == -1) {
			(void) fprintf(stderr, gettext(
				"%s: stat error.\n"),
				program);
			return (ERROR);
		}
		size = stat.st_size;
		a =  mmap(NULL, size,
			PROT_READ, MAP_SHARED, fd, 0);
		if (a == (char *)-1) {
			(void) fprintf(stderr, gettext(
				"%s: mmap failed.\n"), program);
			return (ERROR);
		}
		charmapheader = (CharmapHeader *)a;
		charmapsymbol = (CharmapSymbol *)(a + CHARMAPHEADER);
	}
	return (0);
}

static void
usage()
{
	(void) fprintf(stderr, gettext(
		"usage: %s -i input_fname [-v] [-f charmap] "
		"[-o output_fname] [-s skip_count]\n"),
		program);
	exit(4);
}

static void
update_lex()
{
	extern int (*_isidentifier)();
	extern int is_alpha();

	_isidentifier = is_alpha;
}

#ifdef DEBUG
/*
 * Debugging
 */
void
debug_info()
{
	dump_collating_element();
	(void) printf("\n");
	dump_collating_symbol();
	dump_weights();
}
#endif
