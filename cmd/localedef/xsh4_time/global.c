/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)global.c	1.3	95/03/03 SMI"

#include "time.h"
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

keyword	keywords[] = {
	"LC_TIME", _LC_TIME, 0,
	"END", END, 0,
	"abday", TIME_WORD_NAME, T_ABDAY,
	"day", TIME_WORD_NAME, T_DAY,
	"abmon", TIME_WORD_NAME, T_ABMON,
	"mon", TIME_WORD_NAME, T_MON,
	"d_t_fmt", TIME_WORD_FMT, T_D_T_FMT,
	"d_fmt", TIME_WORD_FMT, T_D_FMT,
	"t_fmt", TIME_WORD_FMT, T_T_FMT,
	"date_fmt", TIME_WORD_FMT, T_DATE_FMT,
	"am_pm", TIME_WORD_FMT, T_AM_PM,
	"t_fmt_ampm", TIME_WORD_FMT, T_T_FMT_AMPM,
	"era", TIME_WORD_OPT, T_ERA,
	"era_d_fmt", TIME_WORD_OPT, T_ERA_D_FMT,
	"era_t_fmt", TIME_WORD_OPT, T_ERA_T_FMT,
	"era_d_t_fmt", TIME_WORD_OPT, T_ERA_D_T_FMT,
	"alt_digits", TIME_WORD_OPT, T_ALT_DIGITS,
	"copy", COPY, 0,
	(char *)-1, -1, 0
};
