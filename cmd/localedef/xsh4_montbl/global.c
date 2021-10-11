/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)global.c	1.4	95/03/03 SMI"

#include "montbl.h"
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
 * Lexical key words.
 */
struct keyword	keywords[] = {
	"LC_MONETARY", _LC_MONETARY, 0,
	"END", END, 0,
	"int_curr_symbol", M_WORD_STR, T_INT_CURR_SYMBOL,
	"currency_symbol", M_WORD_STR, T_CURRENCY_SYMBOL,
	"mon_decimal_point", M_WORD_STR, T_MON_DECIMAL_POINT,
	"mon_thousands_sep", M_WORD_STR, T_MON_THOUSANDS_SEP,
	"mon_grouping", M_WORD_GRP, T_MON_GROUPING,
	"positive_sign", M_WORD_STR, T_POSITIVE_SIGN,
	"negative_sign", M_WORD_STR, T_NEGATIVE_SIGN,
	"int_frac_digits", M_WORD_CHAR, T_INT_FRAC_DIGITS,
	"frac_digits", M_WORD_CHAR, T_FRAC_DIGITS,
	"p_cs_precedes", M_WORD_CHAR, T_P_CS_PRECEDES,
	"p_sep_by_space", M_WORD_CHAR, T_P_SEP_BY_SPACE,
	"n_sep_by_space", M_WORD_CHAR, T_N_SEP_BY_SPACE,
	"n_cs_precedes", M_WORD_CHAR, T_N_CS_PRECEDES,
	"p_sign_posn", M_WORD_CHAR, T_P_SIGN_POSN,
	"n_sign_posn", M_WORD_CHAR, T_N_SIGN_POSN,
	"copy", COPY, 0,
	(char *)-1, -1, 0
};
