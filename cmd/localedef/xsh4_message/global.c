/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)global.c	1.4	95/03/03 SMI"

#include "message.h"
#include "y.tab.h"

char	*program;
char	*input_fname;
char	*output_fname;
char	escape_char = DEFAULT_ESCAPE_CHAR;
char	comment_char = DEFAULT_COMMENT_CHAR;
FILE	*input_file;
int		errorcnt = 0;
int		d_flag = 0;
int		lineno = 0;

keyword	keywords[] = {
	"LC_MESSAGES", _LC_MESSAGE, 0,
	"END", END, 0,
	"yesexpr", YN_REG_EXP, T_YES_EXP,
	"noexpr", YN_REG_EXP, T_NO_EXP,
	"yesstr", YN_STR, T_YES_STR,
	"nostr", YN_STR, T_NO_STR,
	(char *)-1, -1, 0
};
