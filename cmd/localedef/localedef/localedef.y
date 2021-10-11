/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

/*
 * Locale Definition File Controller, Grammar
 */
%{
#ident	"@(#)localedef.y	1.14	95/08/07 SMI"
#include "../head/_localedef.h"
#include "../head/charmap.h"
#include "extern.h"

static int		ret;
static int		yacc_ret = 0;
static void		LdefError(char *);

static int		seen_lc_ctype = 0;
static int		seen_lc_collate = 0;
static int		seen_lc_time = 0;
static int		seen_lc_numeric = 0;
static int		seen_lc_monetary = 0;
static int		seen_lc_message = 0;

extern char		*input_fname;
extern FILE		*input_file;
extern char		*localename;

extern int		execute(int, char *, int, char *);
extern int		yylex();
%}

%union {
	int		lineno;
	int		type;
}
%token <lineno>	CHARMAP_LINE
%token <lineno> LC_CTYPE_LINE
%token <lineno> LC_COLLATE_LINE
%token <lineno> LC_TIME_LINE
%token <lineno> LC_NUMERIC_LINE
%token <lineno> LC_MONETARY_LINE
%token <lineno> LC_MESSAGE_LINE
%token END_CHARMAP
%token END_LC_CTYPE
%token END_LC_COLLATE
%token END_LC_TIME
%token END_LC_NUMERIC
%token END_LC_MONETARY
%token END_LC_MESSAGE
%token REGULAR_LINE
%type <type> locale_category
%start locale_definition

%%
locale_definition: charmap locale_categories comments
		{
			return (yacc_ret);
			/* NOTREACHED */
		}
		|  charmap comments
		{
			return (yacc_ret);
			/* NOTREACHED */
		}
		|  locale_categories comments
		{
			return (yacc_ret);
			/* NOTREACHED */
		}
		;
charmap		: comments CHARMAP_LINE lines END_CHARMAP
		{
			ret = execute(CHARMAP_LINE,
				input_fname, $2, localename);
			if (ret != 0) {
				LdefError("CHARMAP");
			} else {
				if (f_flag == 0 && lf_flag == 0) {
					lf_flag++;
					(void) strcpy(charobj_fname, localename);
					(void) strcat(charobj_fname, CHARMAP_TRAIL);
				}
			}
		}
		;
locale_categories: locale_categories locale_category
		| locale_category
		;
locale_category	: comments LC_CTYPE_LINE lines END_LC_CTYPE
		{
			seen_lc_ctype++;
			if (seen_lc_ctype == 1) {
				$$ = LC_CTYPE_LINE;
				ret = execute(LC_CTYPE_LINE,
					input_fname, $2, localename);
				if (ret != 0) {
					LdefError("LC_CTYPE");
					/*
					 * LC_CTYPE is needed later so if this
					 * fails then quit.
					 */
					exit(ret);
				} else
					lc_ctype++;

				(void) rewind(input_file);
				lineno = 0;
			}
		}
		| comments LC_COLLATE_LINE lines END_LC_COLLATE
		{
			seen_lc_collate++;
			if (seen_lc_collate == 1) {
				$$ = LC_COLLATE_LINE;
				ret = execute(LC_COLLATE_LINE,
					input_fname, $2, localename);
				if (ret != 0) {
					LdefError("LC_COLLATE");
				} else
					lc_collate++;
			}
		}
		| comments LC_TIME_LINE lines END_LC_TIME
		{
			seen_lc_time++;
			if (seen_lc_time == 1) {
				$$ = LC_TIME_LINE;
				ret = execute(LC_TIME_LINE,
					input_fname, $2, localename);
				if (ret != 0) {
					LdefError("LC_TIME");
				} else
					lc_time++;
			}
		}
		| comments LC_NUMERIC_LINE lines END_LC_NUMERIC
		{
			seen_lc_numeric++;
			if (seen_lc_numeric == 1) {
				$$ = LC_NUMERIC_LINE;
				ret = execute(LC_NUMERIC_LINE,
					input_fname, $2, localename);
				if (ret != 0) {
					LdefError("LC_NUMERIC");
				} else
					lc_numeric++;
			}
		}
		| comments LC_MONETARY_LINE lines END_LC_MONETARY
		{
			seen_lc_monetary++;
			if (seen_lc_monetary == 1) {
				$$ = LC_MONETARY_LINE;
				ret = execute(LC_MONETARY_LINE,
					input_fname, $2, localename);
				if (ret != 0) {
					LdefError("LC_MONETARY");
				} else
					lc_monetary++;
			}
		}
		| comments LC_MESSAGE_LINE lines END_LC_MESSAGE
		{
			seen_lc_message++;
			if (seen_lc_message == 1) {
				$$ = LC_MESSAGE_LINE;
				ret = execute(LC_MESSAGE_LINE,
					input_fname, $2, localename);
				if (ret != 0) {
					LdefError("LC_MESSAGE");
				} else
					lc_message++;
			}
		}
		;
comments	:
		| lines
		;
lines		: lines REGULAR_LINE
		| REGULAR_LINE
		;
%%

void
yyerror(char *s)
{
	(void) fprintf(stderr, "%s: %s.\n", program, s);
	errorcnt++;
}

static void
LdefError(char *s)
{
	yacc_ret++;
	(void) fprintf(stderr, gettext(
		"Warning: %s category was not generated.\n"), s);
}
