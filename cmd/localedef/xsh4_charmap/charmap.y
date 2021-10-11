/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

/*
 * Character Map grammar
 */
%{
#ident	"@(#)charmap.y	1.20	95/03/06 SMI"
#include "../head/_localedef.h"
#include "../head/charmap.h"
#include "../head/_collate.h"
#include <varargs.h>
#include "extern.h"

extern FILE		*input_file;

unsigned char	tmp_value[MAX_BYTES];
unsigned char	*tmp_p;
int		byte_cnt;

extern int		install_symbol(int, char *, unsigned char *, int, int);
extern int		check_id(char *, char *);
extern int		_yylex_common(FILE *, int);
extern void		eat_line(FILE *, char, int *l);
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

%token CHARMAP END
%token <id> MB_CURR_MAX MB_CURR_MIN ESCAPE_CHAR
%token <id> COMMENT_CHAR CODE_SET_NAME
%type <byte> encoded_symbol
%type <bytes> bytes

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
%start charmap

%%
charmap	: starter header mapping ender
	{
		return (0);
		/* NOTREACHED */
	}
	;
starter	: CHARMAP eol
	;
header	:
	| header head_statement
	;
head_statement:	'<' CODE_SET_NAME '>' ID eol
	{
		(void) strcpy(code_set_name, $4);
		free($4);
	}
	| '<' MB_CURR_MAX '>'	NUM eol
	{
		mb_cur_max = $4;
	}
	| '<' MB_CURR_MIN '>'	NUM eol
	{
		mb_cur_min = $4;
	}
	| '<' ESCAPE_CHAR {_switch_id(0); } '>'
		ID { _switch_id(1); } eol
	{
		char *p;
		p = $5;
		escape_char = *p;
		free($5);
	}
	| '<' COMMENT_CHAR '>'  ID eol
	{
		char *p;
		p = $4;
		comment_char = *p;
		free($4);
	}
	;
mapping : mapping map_stat
	| map_stat
	;
map_stat: '<' ID '>' bytes eol
	{
		int ret;
		ret = install_symbol(SINGLE, $2, tmp_value,
				byte_cnt, 0);
		if (ret == ERROR)
			errorcnt++;
		free($2);
	}
	| '<' ID '>' ELLIPSIS '<' ID '>' bytes eol
	{
		int ret;
		ret = check_id($2, $6);
		if (ret != ERROR) {
			ret = install_symbol(RANGE, $2, tmp_value,
					byte_cnt, ret);
			if (ret == ERROR)
				errorcnt++;
		}
		else
			errorcnt++;
		free($2);
		free($6);
	}
	;
bytes	: bytes encoded_symbol
	{
		if (byte_cnt < MAX_BYTES) {
			byte_cnt++;
			*tmp_p++ = $2;
		}
		$$ = tmp_value;
	}
	| encoded_symbol
	{
		byte_cnt = 1;
		tmp_p = tmp_value;
		*tmp_p++ = $1;
		$$ = tmp_value;
	}
	;
encoded_symbol: HEX_CHAR
	| OCTAL_CHAR
	| DECIMAL_CHAR
	;
eol	:	eol EOL
	{
		eat_line(input_file, comment_char, &lineno);
	}
	| EOL
	{
		eat_line(input_file, comment_char, &lineno);
	}
	;
ender	: END CHARMAP
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
	va_list         args;

	va_start(args);
	(void) fprintf(stderr, "%s: ", program);
	(void) fprintf(stderr, gettext(
		"error at line %d: "), lineno);
	(void) vfprintf(stderr, s, args);
	va_end(args);
	errorcnt++;
}


/*
 *  Lexical analyzer
 */
int
yylex()
{
	int		x;
	x = _yylex_common(input_file, _CHARMAP);
	return (x);
}

extern int	(*_isidentifier)();
extern int	(*_isidentifier_tail)();
extern int	_is_identifier();
extern int	_is_ident_1();

int
_switch_id(int flag)
{
	if (flag == 0) {
		_isidentifier = _is_ident_1;
		_isidentifier_tail = _is_ident_1;
	} else {
		_isidentifier = _is_identifier;
		_isidentifier_tail = _is_identifier;
	}
	return (0);
}
		
