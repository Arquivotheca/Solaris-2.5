/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)extern.h	1.6	95/02/25 SMI"

extern char	*program;
extern char	*input_fname;
extern char	*charmap_fname;
extern int	syntax_errors;
extern int	exec_errors;
extern int	lineno;
extern char	comment_char;
extern char	escape_char;
extern FILE	*input_file;

#ifdef SJIS
extern int	m_flag;
#endif

/*
 * The following to pointers contains info.
 *	about character mapping.
 */
extern CharmapHeader	*charmapheader;
extern CharmapSymbol	*charmapsymbol;

/*
 * The followings from Solaris 2.3 chrtbl
 */
extern struct classname	cln[];
extern int	chrclass;
extern int	lc_numeric;
extern int	lc_ctype;
extern char	chrclass_name[];
extern int	chrclass_num;
extern int	ul_conv;
extern int	cont;
extern int	action;
extern int	in_range;
extern int	ctype[];
extern int	range;
extern int	width;
extern int	numeric;
extern char	tokens[];
extern int	codeset1;
extern int	codeset2;
extern int	codeset3;
extern unsigned			*wctyp[];
extern unsigned int		*wconv[];
extern struct _wctype	*wcptr[];
extern struct _wctype	wctbl[];
extern unsigned char	*index[];
extern unsigned			*type[];
extern unsigned int		*code[];
extern int	cnt_index[];
extern int	cnt_type[];
extern int	cnt_code[];
extern int	num_banks[];
