/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)global.c	1.12	95/03/02 SMI"

#include "collate.h"
#include "y.tab.h"

char	*program;
char	*input_fname = "stdin";
char	*charmap_fname = NULL;
int		syntax_errors = 0;
int		exec_errors = 0;
int		lineno = 0;
char	comment_char = DEFAULT_COMMENT_CHAR;
char	escape_char = DEFAULT_ESCAPE_CHAR;
FILE	*input_file;
int		v_flag = 0;

char	weight_level = 0;
char	weight_types[MAX_WEIGHTS];
encoded_val	enbuf;

/*
 * The following to pointers contains info.
 *	about character mapping.
 */
CharmapHeader	*charmapheader = NULL;
CharmapSymbol	*charmapsymbol = NULL;

/*
 * Colltbl info.
 */
collating_symbol	*head_collating_symbol = NULL;

/*
 * keywords definition
 */
keyword keywords[] = {
	"LC_COLLATE", L_COLLATE, 0,
	"from", FROM, 0,
	"collating_element", COLLATING_ELE, 0,
	"collating_symbol", COLLATING_SYM, 0,
	"order_start", ORDER_START, 0,
	"order_end", ORDER_END, 0,
	"UNDEFINED", UNDEFINED, 0,
	"forward", FORWARD, 0,
	"backward", BACKWARD, 0,
	"position", POSITION, 0,
	"IGNORE", IGNORE, 0,
	"END", END, 0,
	0, -1, 0
};
