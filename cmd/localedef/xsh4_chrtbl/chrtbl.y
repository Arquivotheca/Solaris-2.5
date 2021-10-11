/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

/*
 * Character Table (LC_CTYPE) Grammar
 */
%{
#ident	"@(#)chrtbl.y	1.23	95/03/07 SMI"
#include	<varargs.h>
#include	"chrtbl.h"
#include	"extern.h"
unsigned int		get_mask(int type);
static encoded_val	*get_char_mapping(char *);
encoded_val			*alloc_encoded(encoded_val *);

static int			attr_type;
static int			conv_type;
static unsigned int	attr_mask;
static int 			prev_is_ellipsis = OFF;
static unsigned int	lower = 0;
int					codeset = 0;
int					cswidth_cs = 0;
int					mixed_format = 0;
int					old_format = 0;
encoded_val			enbuf;

void			execerror(char *, ...);

extern int		_yylex_common(FILE *, int);
extern int		set_ctype(int, unsigned int, unsigned int,
					int, unsigned int);
extern int		set_conv(int, unsigned int, unsigned int);
extern void		undefined(char *, char *, int, int *);
extern void		euc_width_info(int, int, int);
extern void		setmem(int);
%}
%union {
	int					ival;
	char				*id;
	unsigned char		byte;
	unsigned char		*bytes;
	struct order		*order;
	struct encoded_val	*encoded_val;
	struct keyword		*key;
}

%token	<key>	_LC_CTYPE
%token	<key>	_LC_EUC_CTYPE
%token	<key>	END
%token	<key>	REG_TYPE
%token	<key>	CONV_TYPE
%token	<key>	_CSWIDTH

%token	<id>	ID				150
%token	<ival>	NUM				151
%token	<byte>	HEX_CHAR		152
%token	<byte>	OCTAL_CHAR		153
%token	<byte>	DECIMAL_CHAR	154
%token	ELLIPSIS				155
%token	EOL						156
%token	D_QUOTE					157
%token	COPY					158
%token	NULL_STR				159

%type	<id>	gen_id
%type	<key>	l_keywords
%type	<key>	attributes attribute
%type	<key>	regular_attr convert_attr
%type	<key>	reg_type conv_type
%type	<ival>	reg_list
%type	<ival>	num_pairs num_pair
%type	<byte>	encoded_symbol
%type	<encoded_val>	encoded_symbols from to conv_val

%%
ctype_file	: starter attributes ender
		{
			return (0);
			/* NOTREACHED */
		}
		;

starter		: _LC_CTYPE eol
		;

ender		: END _LC_CTYPE
		;

attributes	: attributes attribute
		| attribute
		;

attribute	: regular_attr
		| convert_attr
		| _CSWIDTH num_pairs eol
		{
#ifdef SJIS
			if (!m_flag) {
#endif				
				width++;
				euc_width_info(3, 0, 0);
#ifdef SJIS
			}
#endif
		}
		| _LC_EUC_CTYPE eol
		{
			if (mixed_format) {
				execerror(gettext(
					"supplementary codesets already defined.\n"));
			} else {
				old_format = 1;
			}
			if (!width) {
				execerror(gettext(
					"cswidth not pre-defined.\n"));
			}
			switch ($1->ktype) {
			case T_CODE1:
				if (codeset1) {
					execerror(gettext(
						"LC_CTYPE1 already used.\n"));
				} else {
					codeset1++;
					codeset = 1;
					setmem(1);
				}
				break;
			case T_CODE2:
				if (codeset2) {
					execerror(gettext(
						"LC_CTYPE2 already used.\n"));
				} else {
					codeset2++;
					codeset = 2;
					setmem(2);
				}
				break;
			case T_CODE3:
				if (codeset3) {
					execerror(gettext(
						"LC_CTYPE3 already used.\n"));
				} else {
					codeset3++;
					codeset = 3;
					setmem(3);
				}
				break;
			default:
				execerror("INTERNAL ERROR: Illegal k_type.\n");
				break;
			}
			$$ = $1;
		}
		;

num_pairs	: num_pairs ',' num_pair
		| num_pair
		;

num_pair	: NUM ':' NUM
		{
#ifdef SJIS
			if (!m_flag) {
#endif
				if (cswidth_cs < 3) {
					euc_width_info(cswidth_cs, $1, $3);
				}
#ifdef SJIS
			}
#endif
			cswidth_cs++;
		}
		;

regular_attr	: reg_type reg_lists eol
		;

reg_type	: REG_TYPE
		{
			attr_type = $1->ktype;
			attr_mask = get_mask(attr_type);
			$$ = $1;
		}

reg_lists	: reg_lists ';' reg_list
		| reg_list
		;

reg_list	: '<' gen_id '>'
		{
			unsigned		u_val;
			encoded_val		*e;
			int				code;
			
			if ($2 != NULL) {
				e = get_char_mapping($2);
				if (e != NULL) {
					code = encoded_value(&u_val, e);
					(void) set_ctype(code, attr_mask, u_val,
						prev_is_ellipsis, lower);
					prev_is_ellipsis = OFF;
					lower = u_val;
					free($2);
					free(e);
					$$ = 1;
				}
			}
		}
		| ELLIPSIS
		{
			prev_is_ellipsis = ON;
			$$ = 3;
		}
		| encoded_symbols
		{
			unsigned 	u_val;
			int			code;	
			if ($1 != NULL) {
				code = encoded_value(&u_val, $1);
				(void) set_ctype(code, attr_mask, u_val,
					prev_is_ellipsis, lower);
				prev_is_ellipsis = OFF;
				lower = u_val;
				free($1);
				$$ = 4;
			}
		}
		;

convert_attr	: conv_type conv_lists eol
		;

conv_type	: CONV_TYPE
		{
			conv_type = $1->ktype;
		}
		;

conv_lists	: conv_lists ';' conv_list
		| conv_list
		;

conv_list	: '(' from ',' to ')'
		{
			unsigned int	u_f, u_t;
			int				code1, code2;
			
			if ($2 != NULL) {
				code1 = encoded_value(&u_f, $2);
				if ($4 != NULL) {
					code2 = encoded_value(&u_t, $4);
				}
			}
			if ($2 != NULL && $4 != NULL) {
				if (code1 != code2) {
					execerror(gettext(
						"codeset mismatch.\n"));
				}
				if (conv_type == T_TOUPPER) {
					(void) set_conv(code1, u_f, u_t);
				} else {
					(void) set_conv(code1, u_t, u_f);
				}
			}
			if ($2) {
				free($2);
			}
			if ($4) {
				free($4);
			}
		}
		;

from	: conv_val
		;

to		: conv_val
		;

conv_val	: '<' gen_id '>'
		{
			if ($2 != NULL) {
				$$ = get_char_mapping($2);
				free($2);
			}
		}
		| encoded_symbols
		{
			$$ = alloc_encoded($1);
			if ($$ == NULL) 
				execerror(gettext(
				"Could not allocate memory for encoded_symbol.\n"));
		}
		;

encoded_symbols	: encoded_symbols encoded_symbol
		{
			enbuf.bytes[enbuf.length++] = $2;
			$$ = &enbuf;
		}
		| encoded_symbol
		{
			enbuf.length = 0;
			enbuf.bytes[enbuf.length++] = $1;
			$$ = &enbuf;
		}
		;
encoded_symbol	: HEX_CHAR
		| OCTAL_CHAR
		| DECIMAL_CHAR
		;
gen_id	: ID
		{
			$$ = $1;
		}
		| l_keywords
		{
			$$ = strdup($1->name);
		}
		;
l_keywords	: _LC_CTYPE
		| _LC_EUC_CTYPE
		| END
		| REG_TYPE
		| CONV_TYPE
		| _CSWIDTH
		;
eol		: eol EOL
		| EOL
		;
%%


/*
 * Lexical analyzer
 */
int
yylex()
{
	int x;
	x = _yylex_common(input_file, _XSH4_CHRTBL);
	return (x);
}

/*
 * Supplementary routines
 */
static encoded_val *
get_char_mapping(char *id)
{
	CharmapSymbol *cm;
	encoded_val *e;

	cm = get_charmap_sym(id);
	if (cm == NULL) {
		undefined("LC_CTYPE", id, lineno, &exec_errors);
		return (NULL);
	}
	e = alloc_encoded(&(cm->en_val));
	if (e == NULL)
		execerror(gettext(
			"Could not allocate memory for encoded_val.\n"));
	return (e);
}

#define	SS2		0x008e
#define	SS3		0x008f

#ifdef SJIS
static int
sjis_encoded_value(unsigned int *i_val, encoded_val *en)
{

	int		topb;
	int		code;
	
	topb = en->bytes[0];
	
	if ((topb >= 0x81 && topb <= 0x9f) ||
		(topb >= 0xe0 && topb <= 0xef)) {
		unsigned char	c1, c2;

		c1 = topb & 0x00ff;
		c2 = en->bytes[1] & 0x00ff;
		if ((c2 < 0x40) || (c2 == 0x7f) || (c2 > 0xfc)) {
			execerror(gettext(
				"invalid SJIS code.\n"));
		} else {
			code = 1;
			if (c1 >= 0xe0) {
				c1 -= 0xb1;
			} else {
				c1 -= 0x71;
			}
			c1 = c1 * 2 + 1;
			if (c2 >= 0x9f) {
				c1++;
				c2 -= 0x5f;
			} else if (c2 >= 0x7f) {
				c2--;
			}
			c2 = c2 - 0x1f;
			c1 = c1 | 0x80;
			c2 = c2 | 0x80;				
			*i_val = (c1 << 8) | c2;
		}
	} else if (topb >= 0xa1 && topb <= 0xdf) {
		code = 2;
		*i_val = topb;
	} else if (topb >= 0xf0 && topb <= 0xfc) {
		unsigned char	c1, c2;

		c1 = topb & 0x00ff;
		c2 = en->bytes[1] & 0x00ff;
		if ((c2 < 0x40) || (c2 == 0x7f) || (c2 > 0xfc)) {
			execerror(gettext(
				"invalid SJIS code.\n"));
		} else {
			code = 3;
			c1 -= 0xe0;
			c1 = c1 * 2 + 1;
			if (c2 >= 0x9f) {
				c1++;
				c2 -= 0x5f;
			} else if (c2 >= 0x7f) {
				c2--;
			}
			c2 = c2 - 0x1f;
			c1 = c1 | 0x80;
			c2 = c2 | 0x80;
			*i_val = (c1 << 8) | c2;
		}
	} else {
		execerror(gettext(
			"invalid code.\n"));
	}

	return (code);
}
#endif

static int
encoded_value(unsigned int *i_val, encoded_val *en)
{
	int		i, code, topb;
	int		bytew = 0;

	topb = en->bytes[0];
	if (isascii(topb)) {
		code = 0;
		*i_val = topb;
	} else {
#ifdef SJIS
		if (m_flag) {
			code = sjis_encoded_value(i_val, en);
		} else {
#endif
			if (topb >= 0x80 && topb <= 0x9f &&
				topb != SS2 && topb != SS3) {
				code = 0;
				*i_val = topb;
			} else {
				if (topb == SS2) {
					if (en->length == 1) {
						code = 0;
						*i_val = topb;
						goto out;
					} else {
						code = 2;
					}
				} else if (topb == SS3) {
					if (en->length == 1) {
						code = 0;
						*i_val = topb;
						goto out;
					} else {
						code = 3;
					}
				} else {
					code = 1;
				}
				bytew = ctype[START_CSWIDTH + (code - 1)]; 
				for(*i_val = 0, i = en->length - bytew; i < en->length; i++) {
					*i_val = (*i_val << 8) | en->bytes[i];
				}
			}
#ifdef SJIS
		}
#endif
	}

out:
	
#ifdef DEBUG
	(void) printf("encoded_value = %x, codeset = %d\n",
		   *i_val, code);
#endif

	if (old_format == 1) {
		if (codeset != code) {
			execerror(gettext(
				"codeset mismatch.\n"));
		}
	} else {
		if (code != 0) {
			if (code == 1) {
				if (!codeset1) {
					codeset1 = 1;
					setmem(1);
				}
			} else if (code == 2) {
				if (!codeset2) {
					codeset2 = 1;
					setmem(2);
				}
			} else {
				if (!codeset3) {
					codeset3 = 1;
					setmem(3);
				}
			}
			if (!width) {
				execerror(gettext(
					"cswidth not pre-defined.\n"));
			}
			if (mixed_format == 0) {
				mixed_format = 1;
			}
		}
	}

	return(code);
}
		
/*
 * Error routines
 */
static void
yyerror(char *s)
{
	if (s) {
		(void) fprintf(stderr, "%s: %s\n", program, s);
	} else {
		(void) fprintf(stderr, gettext(
			"%s: syntax error.\n"), program);
	}
	(void) fprintf(stderr, gettext(
		"\tError occurred at line %d.\n"), lineno);
	syntax_errors++;
}

void
execerror(char *s, ...)
{
	va_list		args;

	va_start(args);
	(void) fprintf(stderr, "%s: ", program);
	(void) fprintf(stderr, gettext(
		"error at line %d: "), lineno);
	(void) vfprintf(stderr, s, args);
	va_end(args);
	exec_errors++;
}

