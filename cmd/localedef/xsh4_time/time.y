/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

/*
 * Character Map grammar
 */
%{
#ident	"@(#)time.y	1.9	95/03/06 SMI"

#include "time.h"
#include "extern.h"
#include <varargs.h>

int		key_words[T_ERA + 1];
int		num_list = 0;
encoded_val	*en_list[MAX_LIST + 1];

extern int in_dquote;

extern int	_yylex_common(FILE *, int);
extern int	set_table(int, encoded_val **, int);
extern void	undefined(char *, char *, int, int *);
extern void	free_encoded_symbol(encoded_val *);
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

%token <key> _LC_TIME END
%token <key> TIME_WORD_FMT
%token <key> TIME_WORD_NAME
%token <key> TIME_WORD_OPT
%type <key> time_keyword
%start time

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

%type <id>  gen_id
%type <key> l_keywords

%type <id> coll_symbol
%type <byte> encoded_symbol
%type <encoded_val> coll_elements
%type <encoded_val> encoded_symbols
%type <encoded_val> quoted_string

%%
time		: starter t_words ender
		{
			/*
			int		i;

			for (i = 0; i < T_DATE_FMT; i++) 
				if (key_words[i] == 0) {
					yyerror(gettext(
				"One of required keywords is not specified."));
				}
			*/
			return (0);
			/* NOTREACHED */
		}
		| starter COPY quoted_string eol ender
		{
			return (0);
			/* NOTREACHED */
		}
		;
starter		: _LC_TIME eol
		;
ender		: END _LC_TIME
		;
t_words 	: t_words t_word
		| t_word
		;
t_word		: time_keyword time_list eol
		{
			/*
			 * silently ignore the repeated key words.
			 */
			if (key_words[$1->ktype] == 0) {
				key_words[$1->ktype] = 1;
				(void) set_table($1->ktype, en_list, num_list);
			} else {
				free_enlist(en_list, num_list);
			}
			num_list = 0;
		}
		;
time_keyword	: TIME_WORD_FMT
		| TIME_WORD_NAME
		| TIME_WORD_OPT
		;
time_list	: time_list ';' quoted_string
		{
			/*
			 * silently ignore the over flew list.
			 */
			if (num_list < MAX_LIST) {
				en_list[num_list++] = $3;
			}
		}
		| quoted_string
		{
			num_list = 0;
			en_list[num_list++] = $1;
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
		;
coll_elements	: coll_elements coll_symbol
		{
			if (set_enbuf($2) == ERROR) {
				undefined("LC_TIME", $2, lineno, &errorcnt);
			}
			$$ = &enbuf;
		}
		| coll_symbol
		{
			enbuf.length = 0;
			if (set_enbuf($1) == ERROR) {
				undefined("LC_TIME", $1, lineno, &errorcnt);
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
l_keywords	: _LC_TIME
		| END
		| TIME_WORD_FMT
		| TIME_WORD_NAME
		| TIME_WORD_OPT
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

static void
free_enlist(encoded_val **list, int num)
{
	int		i;
	for (i = 0; i < num; i++) {
		free_encoded_symbol(list[i]);
	}
}

int
yylex()
{
	int		x;
	x = _yylex_common(input_file, _XSH4_TIME);
#ifdef DDEBUG
printf("YYLEX returning %d\n", x);
#endif
	return (x);
}
