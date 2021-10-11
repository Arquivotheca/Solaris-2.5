/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)main.c	1.13	95/03/08 SMI"

/*
 *  XPG4, chrtbl command
 *  colltbl -i input_fname [-f charmap] [-o output_fname] [-s skip_count]
 */


#include "chrtbl.h"
#include "extern.h"

#define	OPTSTRING ":i:f:o:s:"

#define	DEFAULT_OUTPUT	"CHRTBL_OUT"

#ifdef SJIS
#define	S_SHIFTJIS	"shiftjis"
#define	S_SJIS		"sjis"
#define	S_MSK		"msk"
#define	S_MSKANJI	"ms-kanji"
#endif

static void init();
static void	usage();
static void update_lex();

extern int	skip_lines(FILE *, int);
extern int	initialize_charmap(int, int);
extern int	output(char *);
extern int	yyparse();
extern void	init_add_conv();
extern void	execerror(char *, ...);
extern void	euc_width_info(int, int, int);

int
main(int argc, char **argv)
{
	int		f_flag = 0;		/* -f charmap */
	int		i_flag = 0;		/* -i input_fname */
	int		o_flag = 0;		/* -o output_fname */
	int		s_flag = 0;		/* -s skip_count */
	int		s_line = 0;		/* No. of lines to skip */
	char	*output_fname = DEFAULT_OUTPUT;
	int		charmap_fd;
	int		c;
	int		errorcnt = 0;
#ifdef SJIS
	char	*codesetname;
#endif

	extern int	optind, opterr, optopt;
	extern char	*optarg;

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
		case 'f':
			f_flag++;
			charmap_fname = optarg;
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
		if (skip_lines(input_file, s_line) != 0) {
			execerror(gettext(
				"invalid skip_count.\n"));
		}
	}
	lineno += s_line;

	if (f_flag != 0) {
		if ((charmap_fd = open(charmap_fname, 0)) == -1) {
			(void) fprintf(stderr, gettext(
				"%s: Can't open %s.\n"),
				program, charmap_fname);
			exit(4);
		}
	}

	if (initialize_charmap(f_flag, charmap_fd) == ERROR) {
		exit(4);
	}
#ifdef SJIS
	codesetname = charmapheader->code_set_name;
	if ((strcasecmp(codesetname, S_SHIFTJIS) == 0) ||
		(strcasecmp(codesetname, S_SJIS) == 0) ||
		(strcasecmp(codesetname, S_MSK) == 0) ||
		(strcasecmp(codesetname, S_MSKANJI) == 0)) {
		m_flag = 1;
	}
#endif
	init();
	init_add_conv();

#ifdef DDEBUG
	if (f_flag != 0)
		dump_charmap();
#endif

	/*
	 * Update yylex() routines.
	 */
	update_lex();

	/*
	 * Parse the input file
	 */
	(void) yyparse();


	/*
	 * Create Output
	 */
	if (syntax_errors || exec_errors) {
		(void) fprintf(stderr, gettext(
			"%s: No output created.\n"), program);
		exit(2);
	}
	if (output(output_fname) == ERROR) {
		exit(4);
	}
	exit(0);
}

/* Initialize the ctype array */
static void
init()
{
	int		i;

	for (i = 0; i < 256; i++) {
		(ctype + 1)[257 + i] = i;
	}
#ifdef SJIS
	if (m_flag) {
		width++;
		euc_width_info(0, 2, 2); /* KANJI */
		euc_width_info(1, 1, 1); /* HANKAKU-KANA */
		euc_width_info(2, 2, 2); /* GAIJI */
		euc_width_info(3, 0, 0); /* maximum byte width */
	} else {
#endif
		euc_width_info(0, 1, 1);	/* for codeset 1 */
		euc_width_info(1, 0, 0);	/* for codeset 2 */
		euc_width_info(2, 0, 0);	/* for codeset 3 */
		euc_width_info(3, 0, 0);	/* maximum byte width */
#ifdef SJIS
	}
#endif
	for (i = 0; i < 3; i++) {
		wcptr[i] = 0;
		cnt_index[i] = 0;
		cnt_type[i] = 0;
		cnt_code[i] = 0;
	}
}

static void
usage()
{
	char	*p;

	if ((p = strrchr(program, '/')) == (char *) NULL) {
		p = program;
	} else {
		p++;
	}
	(void) fprintf(stderr, gettext(
		"usage: %s -i input_fname [-f charmap] "
		"[-o output_fname] [-s skip_count]\n"), p);
	exit(4);
}

static void
update_lex()
{
	extern int	(*_isidentifier)();
	extern int	is_alpha();

	_isidentifier = is_alpha;
}
