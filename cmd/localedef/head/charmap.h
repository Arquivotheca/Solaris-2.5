/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)charmap.h	1.11	95/03/08 SMI"

/*
 * Common Header File for Localedef command
 */

#define	RANGE		1
#define	SINGLE		2

#define	DEFAULT_ESCAPE_CHAR		'\\'
#define	DEFAULT_COMMENT_CHAR	'#'

/*
 * Symbol table structure
 */
typedef struct	CharmapSymbol {
	char	type;
	char	name[MAX_ID_LENGTH];
	encoded_val	en_val;
	int		range;
} CharmapSymbol;

/*
 * Output table header
 */
typedef struct	CharmapHeader {
	char	code_set_name[MAX_ID_LENGTH];
	int		mb_cur_max;
	int		mb_cur_min;
	char	escape_char;
	char	comment_char;
	int		num_of_elements;
} CharmapHeader;

#define	CHARMAPSYMBOL	sizeof (struct CharmapSymbol)
#define	CHARMAPHEADER	sizeof (struct CharmapHeader)

CharmapSymbol	*get_charmap_sym(char *);
