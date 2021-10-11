/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)common.c	1.11	95/03/03 SMI"

#include "../head/_localedef.h"
#include "../head/charmap.h"

/*
 * common lookup, is from lex.c, but localedef doesn't need the specific
 * complexity of lex.c's parsing routines, so we have a local copy of
 * the simple one here. The complex xsh4_*() utilities use their own more
 * complex parsers and link with this anyway.
 */
keyword *
lookup(char *id, keyword *k)
{
	while (k->kval != -1) {
		if (strcmp(id, k->name) == 0) {
			return (k);
		}
		++k;
	}
	return ((struct keyword *)NULL);
}

/*
 * Skip lines
 */
int
skip_lines(FILE *input, int skip)
{
	char	buf[MAX_LINE_LENGTH+1];

	while (skip > 0) {
		if (fgets(buf, MAX_LINE_LENGTH, input) == NULL) {
			break;
		}
		skip--;
	}
	return (skip);
}

/*
 * Skip empty lines
 */
void
eat_line(FILE *input_file, char comment_char, int *lineno)
{
	int		x;

	while ((x = getc(input_file)) == ' ' ||
		x == '\t' || x == '\n') {
			if (x == '\n') {
				(*lineno)++;
			}
	}
	if (x == comment_char) {
		/* Skip this line */
		while ((x = getc(input_file)) != '\n')
			;
	} else if (x != EOF) {
		(void) ungetc(x, input_file);
	}
}

/*
 * Skip first space characters and comment lines
 */
void
skip_init_line(FILE *input_file, char comment_char, int *line)
{
	int		c;
eat_line:
	while ((c = getc(input_file)) == ' ' ||
		c == '\t' ||
		c == '\n') {
		if (c == '\n') {
			(*line)++;
		}
	}
	if (c == comment_char) {
		while ((c = getc(input_file)) != '\n');
		(*line)++;
		goto eat_line;
	}
	(void) ungetc(c, input_file);
}

int
bytescmp(unsigned char *s1, unsigned char *s2, int len)
{
	while (len--) {
		if (*s1 != *s2) {
			return (*s1 - *s2);
		}
		s1++;
		s2++;
	}
	return (0);
}

int
bytescopy(unsigned char *s1, unsigned char *s2, int len)
{
	if (len < 0) {
		return (ERROR);
	}
	while (len--) {
		*s1++ = *s2++;
	}
	return (1);
}

/*
 * error reporting routinnes.
 */

/*
 * report undefined symbol.
 */
void
undefined(char *cat, char *id, int line, int *err)
{
	(void) fprintf(stderr, gettext(
		"\t%s: %s used around line %d could be an undefined symbol.\n"),
		cat, id, line);
	(*err)++;
}
