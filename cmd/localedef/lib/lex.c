/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)lex.c	1.16	95/07/17 SMI"

#include "../head/_localedef.h"
#include "../head/charmap.h"
#include "../head/_collate.h"
#include <ctype.h>

typedef union {
	int		ival;
	char	*id;
	unsigned char	byte;
	unsigned char	*bytes;
	struct order	*order;
	struct encoded_val	*encoded_val;
	struct keyword	*key;
} YYSTYPE;

extern YYSTYPE	yylval;

/*
 * Forcely defined in YACC grammars.
 */
#define	ID				150
#define	NUM				151
#define	HEX_CHAR		152
#define	OCTAL_CHAR		153
#define	DECIMAL_CHAR	154
#define	ELLIPSIS		155
#define	EOL				156
#define	D_QUOTE			157
#define	COPY			158
#define	NULL_STR		159

/*
 * Global variables
 */
extern char	escape_char;
extern char	comment_char;
extern int	lineno;
extern keyword	keywords[];

extern keyword	*lookup(char *, keyword *);
extern void		execerror(char *, ...);

unsigned int
geteval(int type, FILE *f)
{
	unsigned int	val = 0;
	int		c;
	char	buf[MAX_ID_LENGTH];
	char	*p = buf;

	while ((c = getc(f)) == ' ' || c == '\t');
	switch (type) {
	case 8:
		while (c >= '0' && c <= '8') {
			val = 8 * val + c - '0';
			c = getc(f);
		}
		break;
	case 10:
		while (isdigit(c)) {
			*p++ = c;
			c = getc(f);
		}
		*p = 0;
		(void) sscanf(buf, "%d", &val);
		break;
	case 16:
		while (isxdigit(c)) {
			*p++ = c;
			c = getc(f);
		}
		*p = 0;
		(void) sscanf(buf, "%x", &val);
		break;
	}
	(void) ungetc(c, f);
	return (val);
}

int
_is_identifier(int c, FILE *input)
{
	return (isalnum(c) || c == '_');
}

int
_is_identifier_tail(int c, FILE *input)
{
	return (isalnum(c) || c == '_' || c == '-');
}

int
_is_ident_1(int c, FILE *input)
{
	int	i = 0;
	int	valid[] = {'|', '\\',
		'!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
		'_', '-', '+', '{', '}', '[', ']', ':', ';', '\'', '"', '~',
		0};
	if (isalnum(c)) {
		return (1);
	}
	while (valid[i] != 0) {
		if (valid[i] == c) {
			return (1);
		}
		++i;
	}
	return (0);
}

int
is_alpha(int c, FILE *input)
{
	return (isalpha(c));
}

/*
 * lexical analyzer, common part
 */
int (*_isidentifier)() = _is_identifier;
int (*_isidentifier_tail)() = _is_identifier_tail;

int
_yylex_common(FILE *input_file, int flag)
{
	static int	quote_sw = 0;
	static int	in_dquote = 0;
	char	buf[MAX_ID_LENGTH];
	int		c, sign, val;

	/*
	 * Skip spaces
	 */
	while ((c = getc(input_file)) == ' ' ||
		c == '\t');

	if (c == EOF) {
		return (0);
	}

	/*
	 * Comment ?
	 */
	if (c == comment_char) {
		/*
		 * Skip comments and call yylex() recursively.
		 */
		while ((c = getc(input_file)) != '\n') {
			if (c == EOF) {
				return (EOF);
			}
		}
		lineno++;
		return (EOL);
	}

	/*
	 * Double Quote handling
	 */
	if (c == '"') {
		if ((c = getc(input_file)) == '"') {
			return (NULL_STR);
		}
		(void) ungetc(c, input_file);
		if (in_dquote == 0) {
			in_dquote = 1;
			quote_sw = 1;
		} else {
			in_dquote = 0;
			quote_sw = 0;
		}
		return (D_QUOTE);
	}

	/*
	 * The previous token was D_QUOTE
	 */
	if (in_dquote == 1 && quote_sw == 1) {
		char	*p = buf;

		quote_sw = 0;
		if (c == '<') {
			/*
			 * Do simple/naive syntax check.
			 */
			*p = c;
			do {
				if (p > buf + sizeof (buf) - 1) {
					*p = 0;
					execerror(gettext(
						"IDENTIFIER too long.\n"));
				}
				*p++ = c;
				if (c == '>') {
					p--;
					if (fseek(input_file, (long) -(p - buf), SEEK_CUR) == -1)
						execerror(gettext("Internal error.  _yylex_common: fseek failed\n"));
					return ('<');
				}
			} while ((c = getc(input_file)) != EOF && (c != '"'));
			(void) ungetc(c, input_file);
			*p = 0;
			yylval.id = strdup(buf);
			return (ID);
		}

		/*
		 * Ok, this is a double quoted string.
		 * Copy it in blindly.
		 */
		do {
			if (p > buf + sizeof (buf) - 1) {
				*p = 0;
				execerror(gettext(
					"IDENTIFIER too long.\n"));
			}
			*p++ = c;
		} while ((c = getc(input_file)) != EOF && (c != '"'));
		(void) ungetc(c, input_file);
		*p = 0;
		yylval.id = strdup(buf);
		return (ID);
	}

	/*
	 * The previous token was not D_QUOTE.
	 * But in_dquote be ON. Does not matter,,
	 */
	if ((*_isidentifier)(c, input_file)) {
		keyword	*key;
		char	*p = buf;

		do {
			if (p > buf + sizeof (buf) - 1) {
				*p = 0;
				execerror(gettext(
					"IDENTIFIER too long.\n"));
			}
			*p++ = c;
		} while ((c = getc(input_file)) != EOF &&
				(*_isidentifier_tail)(c, input_file));
		(void) ungetc(c, input_file);
		*p = 0;
		if ((key = (keyword *) lookup(buf, &keywords[0])) != NULL) {
			yylval.key = key;
			return (key->kval);
		}
		yylval.id = strdup(buf);
		return (ID);
	}

	/*
	 * Encoded value ?
	 */
	if (c == escape_char) {
		c = getc(input_file);
		if (c == 'x' || c == 'X') {
			yylval.byte = geteval(16, input_file);
			return (HEX_CHAR);
		} else if (c == 'd' || c == 'D') {
			yylval.byte = geteval(10, input_file);
			return (DECIMAL_CHAR);
		} else if (c >= '0' && c <= '8') {
			(void) ungetc(c, input_file);
			yylval.byte = geteval(8, input_file);
			return (OCTAL_CHAR);
		} else if (c == '\n') {
			(void) ungetc(c, input_file);
			/*
			 * Assume that this is a escale character for
			 * New line. Skip the rest of the line and
			 * call yylex() recursively.
			 */
			while ((c = getc(input_file)) != '\n') {
				if (c == EOF) {
					return (EOF);
				}
			}
			++lineno;
			return (_yylex_common(input_file, flag));
		}
	}

	/*
	 * Number ?
	 */
	sign = 1;
	if (isdigit(c)) {
		val = 0;
calc:
		do {
			val = 10 * val + c - '0';
			c = getc(input_file);
		} while (isdigit(c));
		(void) ungetc(c, input_file);
		yylval.ival = sign*val;
		return (NUM);
	}

	if (c == '-' || c == '+') {
		if (c == '-') {
			sign = -1;
		} else {
			sign = 1;
		}
		c = getc(input_file);
		if (isdigit(c)) {
			val = 0;
			goto calc;
		}
		(void) ungetc(c, input_file);
	}

	/*
	 * Special characters ?
	 */
	switch (c) {
	case '\n':
		lineno++;
		return (EOL);
	case '>':
	case ';':
	case ',':
	case '<':
	case ')':
	case '(':
	case ':':
		return (c);
	case '.':
		c = getc(input_file);
		if (c == '.') {
			c = getc(input_file);
			if (c == '.') {
				return (ELLIPSIS);
			}
		}
		return (c);
	default:
		/*
		 * treat this as ID.
		 */
		buf[0] = c;
		buf[1] = 0;
		yylval.id = strdup(buf);
		return (ID);
	}
}
