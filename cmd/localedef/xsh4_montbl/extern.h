/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)extern.h	1.2	94/05/09 SMI"

extern char *program;
extern char *input_fname;
extern char *output_fname;
extern int lineno;
extern char escape_char;
extern char comment_char;
extern int errorcnt;
extern FILE *input_file;
extern int d_flag;

/*
 * defined in ../lib/charmap.c
 */
extern encoded_val enbuf;
