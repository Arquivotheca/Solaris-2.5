/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

/*
 * Character Map grammar
 */
%{
#ident	"@(#)montbl.y	1.8	95/03/06 SMI"

#include "montbl.h"
#include "extern.h"
#include <varargs.h>

#define	MAX_GRP 10

int		num_list = 0;
int		num_grp[MAX_GRP];

extern int		_yylex_common(FILE *, int);
extern int		set_string(int, struct encoded_val *);
extern int		set_char(int, int);
extern int		set_group(int, int *);
extern void		undefined(char *, char *, int, int*);
%}
%union {
	int		ival;
	char	*id;
	unsigned char	byte;
	unsigned char	*bytes;
	struct order	*order;
	struct encoded_val	*encoded_val;
	struct keyword	*key;
}

%token <key> _LC_MONETARY END
%token <key> M_WORD_STR
%token <key> M_WORD_CHAR
%token <key> M_WORD_GRP
%start monetary

%token <id> ID				150
%token <ival> NUM			151
%token <byte> HEX_CHAR		152
%token <byte> OCTAL_CHAR	153
%token <byte> DECIMAL_CHAR	154
%token ELLIPSIS				155
%token EOL					156
%token D_QUOTE				157
%token COPY					158
%token NULL_STR				159

%type <id> coll_symbol
%type <byte> encoded_symbol
%type <encoded_val> coll_elements
%type <encoded_val> encoded_symbols
%type <encoded_val> quoted_string
%type <id> gen_id
%type <key> l_keywords

%%
monetary	: starter m_words ender
		{
			return (0);
			/* NOTREACHED */
		}
		| starter COPY quoted_string eol ender
		{
			return (0);
			/* NOTREACHED */
		}
		;
starter		: _LC_MONETARY eol
		;
ender		: END _LC_MONETARY
		;
m_words 	: m_words m_word
		| m_word
		;
m_word		: M_WORD_STR quoted_string eol
		{
			(void) set_string($1->ktype, $2);
			free($2);
		}
		| M_WORD_CHAR NUM eol
		{
			(void) set_char($1->ktype, $2);
		}
		| M_WORD_CHAR '\'' NUM '\'' eol
		{
			(void) set_char($1->ktype, $3);
		}
		| M_WORD_GRP m_grp_list eol
		{
			(void) set_group(num_list, num_grp);
			num_list = 0;
		}
		;
m_grp_list	: m_grp_list ';' NUM
		{
			if (num_list != MAX_GRP) {	/* silently ignore */
				num_grp[num_list++] = $3;
			}
		}
		| NUM
		{
			num_grp[num_list++] = $1;
		}
		;
eol		: eol EOL
		| EOL
		;
quoted_string	: D_QUOTE encoded_symbols D_QUOTE
		{
			$$ = alloc_encoded($2);
		}
		| D_QUOTE coll_elements D_QUOTE
		{
			$$ = alloc_encoded($2);
		}
		| D_QUOTE gen_id D_QUOTE
		{
			(void) strcpy((char *)enbuf.bytes, $2);
			enbuf.length = strlen($2);
			$$ = alloc_encoded(&enbuf);
			free($2);
		}
		| NULL_STR
		{
			(void) strcpy((char *)enbuf.bytes, "");
			enbuf.length = 1;
			$$ = alloc_encoded(&enbuf);
		}
		;
coll_elements	: coll_elements coll_symbol
		{
			if (set_enbuf($2) == ERROR) {
				undefined("LC_MONETARY", $2, lineno, &errorcnt);
			}
			$$ = &enbuf;
		}
		| coll_symbol
		{
			enbuf.length = 0;
			if (set_enbuf($1) == ERROR) {
				undefined("LC_MONETARY", $1, lineno, &errorcnt);
			}
			$$ = &enbuf;
		}
		;
coll_symbol	: '<' gen_id '>'
		{
			$$ = $2;
		}
		;
gen_id		: ID
		{
			$$ = $1;
		}
		| l_keywords
		{
			$$ = strdup($1->name);
		}
		;
l_keywords	: _LC_MONETARY
		| END
		| M_WORD_STR
		| M_WORD_CHAR
		| M_WORD_GRP
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
encoded_symbol 	: OCTAL_CHAR
		| HEX_CHAR
		| DECIMAL_CHAR
		;

%%

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
		"\tError occurred at line %d.\n"),
		lineno);
	errorcnt++;
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
	errorcnt++;
}

int
yylex()
{
	int		x;
	x = _yylex_common(input_file, _XSH4_MONTBL);
	return (x);
}
