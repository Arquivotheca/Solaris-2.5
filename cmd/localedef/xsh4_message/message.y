/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

/*
 * Character Map grammar
 */
%{
#ident	"@(#)message.y	1.10	95/03/06 SMI"

#include "message.h"
#include "extern.h"
#include <varargs.h>

int		k_count[4];

extern int	_yylex_common(FILE *, int);
extern int	set_table(struct keyword *, encoded_val *);
extern void	free_encoded_symbol(encoded_val *);
extern void	undefined(char *, char *, int, int *);
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

%token <key> _LC_MESSAGE END
%token <key> YN_REG_EXP
%token <key> YN_STR
%type <key> m_word
%type <key> msg_keyword
%start message

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

%type <id> gen_id
%type <key> l_keywords

%type <id> coll_symbol
%type <byte> encoded_symbol
%type <encoded_val> coll_elements
%type <encoded_val> encoded_symbols
%type <encoded_val> quoted_string

%%
message		: starter m_words ender
		{
			return (0);
			/* NOTREACHED */
		}
		| starter COPY quoted_string eol ender
		{
			/*
			 * print warning message that
			 * COPY is a no-op.
			 */
			return (0);
			/* NOTREACHED */
		}
		;
starter		: _LC_MESSAGE eol
		;
ender		: END _LC_MESSAGE
		;
m_words 	: m_words m_word
		| m_word
		;
m_word		: msg_keyword quoted_string eol
		{
			/*
			 * Silently ignore the second and the later
			 */
			if (k_count[$1->ktype]++ == 0) {
				(void) set_table($1, $2);
			}
			free_encoded_symbol($2);
		}
		;
msg_keyword	: YN_REG_EXP
		| YN_STR
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
coll_elements	: coll_elements coll_symbol
		{
			if (set_enbuf($2) == ERROR) {
				undefined("LC_MESSAGE", $2, lineno, &errorcnt);
			}
			$$ = &enbuf;
		}
		| coll_symbol
		{
			enbuf.length = 0;
			if (set_enbuf($1) == ERROR) {
				undefined("LC_MESSAGE", $1, lineno, &errorcnt);
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
l_keywords	: _LC_MESSAGE
		| END
		| YN_REG_EXP
		| YN_STR
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
	x = _yylex_common(input_file, _XSH4_MESSAGE);
	return (x);
}
