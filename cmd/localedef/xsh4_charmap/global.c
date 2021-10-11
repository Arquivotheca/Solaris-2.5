/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)global.c	1.1	94/06/05 SMI"

#include "../head/_localedef.h"
#include "../head/charmap.h"
#include "y.tab.h"

char *program;
char *input_fname;
char *output_fname;
int lineno = 0;
char escape_char = DEFAULT_ESCAPE_CHAR;
char comment_char = DEFAULT_COMMENT_CHAR;
char code_set_name[MAX_ID_LENGTH];
int num_of_symbols = 0;
int errorcnt = 0;
int mb_cur_max = 1;
int mb_cur_min = 1;
FILE *input_file;

/*
 * Keywords definition
 */
keyword keywords[] = {
	"CHARMAP", CHARMAP, 0,
	"END", END, 0,
	"mb_cur_max", MB_CURR_MAX, 0,
	"mb_cur_min", MB_CURR_MIN, 0,
	"escape_char", ESCAPE_CHAR, 0,
	"comment_char", COMMENT_CHAR, 0,
	"code_set_name", CODE_SET_NAME, 0,
	0, -1
};
