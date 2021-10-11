/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)extern.h	1.7	95/02/28 SMI"

extern char		*program;
extern char		charobj_fname[];

extern int	lineno;
extern int	lc_ctype;
extern int	lc_collate;
extern int	lc_time;
extern int	lc_numeric;
extern int	lc_monetary;
extern int	lc_message;
extern int	errorcnt;

extern int	v_flag;
extern int	i_flag;		/* -i input_fname */
extern int	f_flag;		/* -f charmap */
extern int	c_flag;		/* -c option */
extern int	e_flag;		/* -e option */
extern int	lf_flag;	/* -F charmap_obj */

extern char	*cmd_charmap;
extern char	*cmd_chrtbl;
extern char	*cmd_collate;
extern char	*cmd_time;
extern char	*cmd_numeric;
extern char	*cmd_monetary;
extern char	*cmd_message;
