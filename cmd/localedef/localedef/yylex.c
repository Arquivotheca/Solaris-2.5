/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)yylex.c	1.10	95/03/03 SMI"

/*
 * Localde Definition File Controler, lexical analyzer
 */
#include "y.tab.h"
#include "../head/_localedef.h"
#include "../head/charmap.h"
#include "extern.h"
#include <ctype.h>

/*
 * Input file
 */
extern FILE	*input_file;
keyword		*lookup();

/*
 * Keywords list
 */
static
keyword keywords[] = {
	"CHARMAP", CHARMAP_LINE, 0,
	"LC_CTYPE", LC_CTYPE_LINE, 0,
	"LC_COLLATE", LC_COLLATE_LINE, 0,
	"LC_TIME", LC_TIME_LINE, 0,
	"LC_NUMERIC", LC_NUMERIC_LINE, 0,
	"LC_MONETARY", LC_MONETARY_LINE, 0,
	"LC_MESSAGES", LC_MESSAGE_LINE, 0,
	"END", END_CHARMAP, 0,
	0, -1, 0
};

static int
isidentifier(int c)
{
	return (isalnum(c) || c == '_');
}

/*
 * Skip Spaces
 */
static char *
skip_space(char *line)
{
	while (isspace(*line)) {
		++line;
	}
	return (line);
}

/*
 * Lexical analysis routine
 */
int
_yylex()
{
	char	buf[MAX_ID_LENGTH + 1];
	char	id[MAX_ID_LENGTH + 1];
	char	*line = buf;
	char	*word = id;
	struct keyword	*key;

	if (fgets(buf, MAX_ID_LENGTH, input_file) == NULL) {
		return (EOF);
	}
	lineno++;

	/*
	 * Skip space characters
	 */
	line = skip_space(line);
	if (*line == '\0') {
		return (REGULAR_LINE);
	}

	/*
	 * Get the first word
	 */
	while (isidentifier(*line)) {
		*word++ = *line++;
	}
	*word = '\0';

	/*
	 * Is this a key word ?
	 */
#ifdef DDEBUG
	(void) printf("YYLEX: word(1) = '%s'\n", id);
#endif
	key = lookup(id, &keywords[0]);
	if (key == NULL) {
		return (REGULAR_LINE);
	}
	if (key->kval != END_CHARMAP) {
		yylval.lineno = lineno;
		return (key->kval);
	}

	/*
	 * This was "END". What is the next word ?
	 */
	line = skip_space(line);
	if (*line == '\0') {
		return (REGULAR_LINE);
	}
	word = id;
	while (isidentifier(*line)) {
		*word++ = *line++;
	}
	*word = '\0';
#ifdef DDEBUG
	(void) printf("YYLEX: word(2) = '%s'\n", id);
#endif
	key = lookup(id, &keywords[0]);
	if (key == NULL) {
		return (REGULAR_LINE);
	}
	switch (key->kval) {
	case CHARMAP_LINE:
		return (END_CHARMAP);
		/* NOTREACHED */
		break;
	case LC_CTYPE_LINE:
		return (END_LC_CTYPE);
		/* NOTREACHED */
		break;
	case LC_COLLATE_LINE:
		return (END_LC_COLLATE);
		/* NOTREACHED */
		break;
	case LC_TIME_LINE:
		return (END_LC_TIME);
		/* NOTREACHED */
		break;
	case LC_NUMERIC_LINE:
		return (END_LC_NUMERIC);
		/* NOTREACHED */
		break;
	case LC_MONETARY_LINE:
		return (END_LC_MONETARY);
		/* NOTREACHED */
		break;
	case LC_MESSAGE_LINE:
		return (END_LC_MESSAGE);
		/* NOTREACHED */
		break;
	default:
		return (REGULAR_LINE);
		/* NOTREACHED */
		break;
	}
}

int
yylex()
{
	int		x;

	x = _yylex();
#ifdef DDEBUG
	(void) printf("\tYYLEX returning (%d)\n", x);
#endif
	return (x);
}
