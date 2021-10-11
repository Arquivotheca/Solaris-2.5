/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)extern.h	1.12	95/04/09 SMI"

extern char	*program;
extern char	*charmap_fname;
extern char	*output_fname;
extern char	*input_fname;
extern int	syntax_errors;
extern int	exec_errors;
extern int	lineno;
extern char	comment_char;
extern char	escape_char;
extern FILE	*input_file;
extern int	v_flag;
extern int	debug_flag;

extern char	weight_level;
extern char	weight_types[];
extern encoded_val	enbuf;

extern CharmapHeader	*charmapheader;
extern CharmapSymbol	*charmapsymbol;

extern collating_symbol	*head_collating_symbol;
