%{
/*
 *	Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)prexlex.l 1.16 95/08/17 SMI"
%}

%a 10000
%o 10000

%{
#include "spec.h"
#include "expr.h"
#include "y.tab.h"
#include <stdlib.h>
#include <string.h>

char *	qtstr		(char *		instr);
char *	rgstr		(char *		instr);

/*
** we substitute i/o routines defined in main.c for the
** standard fare.  This allows us to support the "source"
** function by redirecting the input stream from different
** places
*/
#include "source.h"
#undef input
#undef unput
#undef output
#define	input()		source_input()
#define	unput(c)	source_unput(c)
#define	output(c)	source_output(c)
%}

IDFIRST			[a-zA-Z_\.%]
IDCHAR			({IDFIRST}|[0-9])
ID			{IDFIRST}{IDCHAR}*

%%

#.*			;		/* eat comments */
[ \t]+			;		/* eats whitespace */

\n			{ source_nl(); return NL; }
\\\n			{ source_nl(); } /* escaped newline */
=			return (EQ);
\,			return (COMMA);

add			return (ADD);
alloc			return (ALLOC);
buffer			return (BUFFER);
clear			return (CLEAR);
connect			return (CONNECT);
continue		return (CONTINUE);
create			return (CREATE);
dealloc			return (DEALLOC);
delete			return (DELETE);
disable			return (DISABLE);
enable			return (ENABLE);
fcns			return (FCNS);
filter			return (FILTER);
help			return (HELP);
history			return (HISTORY);
kill			return (KILL);
ktrace			return (KTRACE);
list			return (LIST);
off			return (OFF);
on			return (ON);
pfilter			return (PFILTER);
probes			return (PROBES);
quit			return (QUIT);
resume			return (RESUME);
sets			return (SETS);
source			return (SOURCE);
suspend			return (SUSPEND);
trace			return (TRACE);
untrace			return (UNTRACE);
values			return (VALUES);

${ID}			{ yylval.strval = strdup(&yytext[1]); return SETNAME; }
&{ID}			{ yylval.strval = strdup(&yytext[1]); return FCNNAME; }
{ID}			{ yylval.strval = strdup(yytext); return IDENT; }
\'[^'\n]*\'		{ yylval.strval = qtstr(yytext); return VALSTR; }

\/([^/\\\n]|\\.)*\/	{ yylval.strval = rgstr(yytext); return REGEXP; }

[0-9]+[KkMm]?		{
				char scale = yytext[yyleng - 1];
				yylval.intval = atoi(yytext);
				if (scale == 'k' || scale == 'K')
					yylval.intval *= 1024;
				else if (scale == 'm' || scale == 'M')
					yylval.intval *= 1024 * 1024;
				return (SCALED_INT);
			}

.			return (INVAL);	/* barf on anything else */

%%

/****************************************************************
qtstr() - shucks a quoted str, and copies it into new memory
****************************************************************/

char *
qtstr		(char *		instr)
{
	char	*ptr;
	int	indx;

	/* skip the leading quote in the copy */
	ptr = strdup(&instr[1]);

	/* null out the trailing quote */
	indx = strlen(ptr) - 1;
	indx = (indx < 0) ? 0 : indx;
	ptr[indx] = '\0';

	return ptr;
}   /* end qtstr */


/****************************************************************
rgstr() - shucks a decorated regular expression, and copies it
into new memory
****************************************************************/

char *
rgstr		(char *		instr)
{
	char	*ptr;
	int	indx;

	/* skip the leading slash in the copy */
	ptr = strdup(&instr[1]);

	/* null out the trailing slash */
	indx = strlen(ptr) - 1;
	indx = (indx < 0) ? 0 : indx;
	ptr[indx] = '\0';

	return (ptr);

}   /* end rgstr */


