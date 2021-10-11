/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)global.c	1.3	95/03/03 SMI"

#include "numeric.h"
#include "y.tab.h"

char	*program;
char	*input_fname;
char	*output_fname;
char	escape_char = DEFAULT_ESCAPE_CHAR;
char	comment_char = DEFAULT_COMMENT_CHAR;
FILE	*input_file;
int		lineno = 0;
int		errorcnt = 0;
int		d_flag = 0;

/*
 * Lexical keywords
 */
keyword	keywords[] = {
	"LC_NUMERIC", _LC_NUMERIC, 0,
	"END", END, 0,
	"decimal_point", N_WORD_STR, T_DECIMAL_POINT,
	"thousands_sep", N_WORD_STR, T_THOUSANDS_SEP,
	"grouping", N_WORD_GRP, T_GROUPING,
	"copy", COPY, 0,
	(char *)-1, -1, 0
};
