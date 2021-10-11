/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)global.c	1.9	95/03/01 SMI"

#include "chrtbl.h"
#include "y.tab.h"

char	*program;
char	*input_fname = "stdin";
char	*charmap_fname = NULL;
char	comment_char = DEFAULT_COMMENT_CHAR;
char	escape_char = DEFAULT_ESCAPE_CHAR;

int		syntax_errors = 0;
int		exec_errors = 0;
int		lineno = 0;

#ifdef SJIS
int		m_flag = 0;
#endif

FILE	*input_file;

/*
 * The following to pointers contains info.
 *	about character mapping.
 */
CharmapHeader	*charmapheader = NULL;
CharmapSymbol	*charmapsymbol = NULL;

/*
 * The followings from Solaris 2.3 chrtbl
 */
struct classname	cln[]  =  {
	"isupper",	ISUPPER,	"_U",
	"islower",	ISLOWER,	"_L",
	"isdigit",	ISDIGIT,	"_N",
	"isspace",	ISSPACE,	"_S",
	"ispunct",	ISPUNCT,	"_P",
	"iscntrl",	ISCNTRL,	"_C",
	"isblank",	ISBLANK,	"_B",
	"isxdigit",	ISXDIGIT,	"_X",
	"iswchar1",	ISWCHAR1,	"_E1",
	"isphonogram",	ISWCHAR1,	"_E1",
	"iswchar2",	ISWCHAR2,	"_E2",
	"isideogram",	ISWCHAR2,	"_E2",
	"iswchar3",	ISWCHAR3,	"_E3",
	"isenglish",	ISWCHAR3,	"_E3",
	"iswchar4",	ISWCHAR4,	"_E4",
	"isnumber",	ISWCHAR4,	"_E4",
	"iswchar5",	ISWCHAR5,	"_E5",
	"isspecial",	ISWCHAR5,	"_E5",
	"iswchar6",	ISWCHAR6,	"_E6",
	"iswchar7",	ISWCHAR7,	"_E7",
	"iswchar8",	ISWCHAR8,	"_E8",
	"iswchar9",	ISWCHAR9,	"_E9",
	"iswchar10",	ISWCHAR10,	"_E10",
	"iswchar11",	ISWCHAR11,	"_E11",
	"iswchar12",	ISWCHAR12,	"_E12",
	"iswchar13",	ISWCHAR13,	"_E13",
	"iswchar14",	ISWCHAR14,	"_E14",
	"iswchar15",	ISWCHAR15,	"_E15",
	"iswchar16",	ISWCHAR16,	"_E16",
	"iswchar17",	ISWCHAR17,	"_E17",
	"iswchar18",	ISWCHAR18,	"_E18",
	"iswchar19",	ISWCHAR19,	"_E19",
	"iswchar20",	ISWCHAR20,	"_E20",
	"iswchar21",	ISWCHAR21,	"_E21",
	"iswchar22",	ISWCHAR22,	"_E22",
	"iswchar23",	ISWCHAR23,	"_E23",
	"iswchar24",	ISWCHAR24,	"_E24",
	"ul",		UL,		NULL,
	"LC_CTYPE",	LC_CTYPE,	NULL,
	"cswidth",	CSWIDTH,	NULL,
	"LC_NUMERIC",	LC_NUMERIC,	NULL,
	"decimal_point", DECIMAL_POINT,	NULL,
	"thousands_sep", THOUSANDS_SEP,	NULL,
	"LC_CTYPE1",	LC_CTYPE1,	NULL,
	"LC_CTYPE2",	LC_CTYPE2,	NULL,
	"LC_CTYPE3",	LC_CTYPE3,	NULL,
	NULL,		NULL,		NULL
};

int		chrclass = 0;	/* set if LC_CTYPE is specified */
int		lc_numeric;		/* set if LC_NUMERIC is specified */
int		lc_ctype;
char	chrclass_name[20];	/* save current chrclass name */
int		chrclass_num;	/* save current chrclass number */
int		ul_conv = 0;	/* set when left angle bracket */
					/* is encountered. */
					/* cleared when right angle bracket */
					/* is encountered */
int		cont = 0;	/* set if the description continues */
					/* on another line */
int		action = 0;	/*  action = RANGE when the range */
					/* character '-' is ncountered. */
					/*  action = UL_CONV when it creates */
					/* the conversion tables.  */
int		in_range = 0;	/* the first number is found */
					/* make sure that the lower limit */
					/* is set  */
int		ctype[SIZE];	/* character class and character */
					/* conversion table */
int		range = 0;	/* set when the range character '-' */
					/* follows the string */
int		width;		/* set when cswidth is specified */
int		numeric;	/* set when numeric is specified */
char	tokens[] = ",:\0";
int		codeset1 = 0;	/* set when charclass1 found */
int		codeset2 = 0;	/* set when charclass2 found */
int		codeset3 = 0;	/* set when charclass3 found */
unsigned		*wctyp[3];	/* tmp table for wctype */
unsigned int	*wconv[3];	/* tmp table for conversion */
struct _wctype	*wcptr[3];	/* pointer to ctype table */
struct _wctype	wctbl[3];	/* table for wctype */
unsigned char	*index[3];	/* code index	*/
unsigned		*type[3];		/* code type	*/
unsigned int	*code[3];	/* conversion code	*/
int		cnt_index[3];		/* number of index	*/
int		cnt_type[3];		/* number of type	*/
int		cnt_code[3];		/* number conversion code */
int		num_banks[3] = {0, 0, 0};	/* number of banks used */

/*
 * Keywords definition
 */
keyword		keywords[] = {
	/*
	 * XPG4 keyword
	 */
	"LC_CTYPE", _LC_CTYPE, 0,
	"upper", REG_TYPE, T_UPPER,
	"lower", REG_TYPE, T_LOWER,
	"alpha", REG_TYPE, T_ALPHA,
	"digit", REG_TYPE, T_DIGIT,
	"space", REG_TYPE, T_SPACE,
	"cntrl", REG_TYPE, T_CNTRL,
	"punct", REG_TYPE, T_PUNCT,
	"graph", REG_TYPE, T_GRAPH,
	"print", REG_TYPE, T_PRINT,
	"xdigit", REG_TYPE, T_XDIGIT,
	"blank", REG_TYPE, T_BLANK,
	"toupper", CONV_TYPE, T_TOUPPER,
	"tolower", CONV_TYPE, T_TOLOWER,
	"END", END, 0,
	/*
	 * EUC specific keywords
	 */
	"LC_CTYPE1", _LC_EUC_CTYPE, T_CODE1,
	"LC_CTYPE2", _LC_EUC_CTYPE, T_CODE2,
	"LC_CTYPE3", _LC_EUC_CTYPE, T_CODE3,
	"cswidth", _CSWIDTH, 0,
	"iswchar1",	REG_TYPE, T_ISWCHAR1,
	"isphonogram",	REG_TYPE, T_ISWCHAR1,
	"iswchar2",	REG_TYPE, T_ISWCHAR2,
	"isideogram",	REG_TYPE, T_ISWCHAR2,
	"iswchar3",	REG_TYPE, T_ISWCHAR3,
	"isenglish",	REG_TYPE, T_ISWCHAR3,
	"iswchar4",	REG_TYPE, T_ISWCHAR4,
	"isnumber",	REG_TYPE, T_ISWCHAR4,
	"iswchar5",	REG_TYPE, T_ISWCHAR5,
	"isspecial",	REG_TYPE, T_ISWCHAR5,
	"iswchar6",	REG_TYPE, T_ISWCHAR6,
	"iswchar7",	REG_TYPE, T_ISWCHAR7,
	"iswchar8",	REG_TYPE, T_ISWCHAR8,
	"iswchar9",	REG_TYPE, T_ISWCHAR9,
	"iswchar10",	REG_TYPE, T_ISWCHAR10,
	"iswchar11",	REG_TYPE, T_ISWCHAR11,
	"iswchar12",	REG_TYPE, T_ISWCHAR12,
	"iswchar13",	REG_TYPE, T_ISWCHAR13,
	"iswchar14",	REG_TYPE, T_ISWCHAR14,
	"iswchar15",	REG_TYPE, T_ISWCHAR15,
	"iswchar16",	REG_TYPE, T_ISWCHAR16,
	"iswchar17",	REG_TYPE, T_ISWCHAR17,
	"iswchar18",	REG_TYPE, T_ISWCHAR18,
	"iswchar19",	REG_TYPE, T_ISWCHAR19,
	"iswchar20",	REG_TYPE, T_ISWCHAR20,
	"iswchar21",	REG_TYPE, T_ISWCHAR21,
	"iswchar22",	REG_TYPE, T_ISWCHAR22,
	"iswchar23",	REG_TYPE, T_ISWCHAR23,
	"iswchar24",	REG_TYPE, T_ISWCHAR24,
	0, -1, 0
};
