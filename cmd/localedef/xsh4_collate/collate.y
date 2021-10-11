/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
/*
 * Collation Grammar
 */
%{
#ident	"@(#)collate.y	1.34	95/04/07 SMI"
#include "collate.h"
#include "extern.h"
#include <varargs.h>

static union args	args;
static int			cur_level = 0;	/* current weight level */
struct order	*tmporder;
void			do_order_opt();

extern keyword	keywords[];
extern char	*get_keyword(int, keyword *);
extern int	_yylex_common(FILE *, int);
extern int	set_collating_elms(char *, encoded_val *);
extern int	set_collating_syms(char *);
extern int	adjust_weight(order *, int);
extern int	set_coll_identifier(order *);
extern int	set_weight(char, int, union args *, order *, int);
extern void	free_encoded_symbol(encoded_val *);
extern void	undefined(char *, char *, int, int*);
extern void	eat_line(FILE *, char, int *);
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

%token <key> L_COLLATE		/* keyword: LC_COLLATE */
%token <key> COLLATING_ELE	/* keyword: collating_element */
%token <key> FROM 			/* keyword: from */
%token <key> COLLATING_SYM	/* keyword: collating_symbol */
%token <key> ORDER_START	/* keyword: order_start */
%token <key> ORDER_END		/* keyword: order_end */
%token <key> UNDEFINED		/* keyword: UNDEFINED */
%token <key> IGNORE			/* keyword: IGNORE */
%token <key> FORWARD		/* keyword: forward */
%token <key> BACKWARD		/* keyword: backward */
%token <key> POSITION		/* keyword: position */
%token <key> END			/* keyword: END */

%type <id> coll_symbol
%type <id> gen_id
%type <key> l_keywords
%type <byte> encoded_symbol
%type <encoded_val> coll_elements
%type <encoded_val> encoded_symbols
%type <encoded_val> quoted_string
%type <order> coll_identifier coll_ident
%type <order> coll_entry
%type <order> coll_order

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

%start collation

%%

/****************
 *Grammar start *
 ****************/
collation	: coll_hdr coll_keywords coll_tlr
		{
			return (0);
			/* NOTREACHED */
		}
		;
coll_hdr 	: L_COLLATE eol
		;
coll_keywords	: order_stats
		| opt_stats order_stats
		;
opt_stats	: opt_stats collating_syms
		| opt_stats collating_elms
		| collating_syms
		| collating_elms
		;

/*********************
 * Collating_element *
 *********************/
collating_elms	: COLLATING_ELE coll_symbol
			FROM quoted_string eol
		{
			(void) set_collating_elms($2, $4);
			enbuf.length = 0;
			free($2);
			free_encoded_symbol($4);
		}
		;
quoted_string	: D_QUOTE encoded_symbols D_QUOTE
		{
			$$ = alloc_encoded($2);
		}
		| D_QUOTE coll_elements D_QUOTE
		{
			$$ = alloc_encoded($2);
		}
		| D_QUOTE ID D_QUOTE
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
				undefined("LC_COLLATE", $2, lineno, &exec_errors);
			}
			$$ = &enbuf;
		}
		| coll_symbol
		{
			enbuf.length = 0;
			if (set_enbuf($1) == ERROR) {
				undefined("LC_COLLATE", $1, lineno, &exec_errors);
			}
			$$ = &enbuf;
		}
		;

/********************
 * Collating_symbol *
 ********************/
collating_syms	: COLLATING_SYM coll_symbol eol
		{
			(void) set_collating_syms($2);
			free($2);
		}
		;


/**************************
 * Order statment handling*
 **************************/

order_stats	: order_start coll_order order_end
		;
order_start	: ORDER_START eol
		{
			/* set the default weight order. */
			do_order_opt();
		}
		| ORDER_START order_opts eol
		;
order_opts	: order_opts ';' order_opt
		{
			do_order_opt();
		}
		| order_opt
		{
			do_order_opt();
		}
		;
order_opt	: order_opt ',' coll_word
		| coll_word
		;
coll_word	: FORWARD
		{
			if (weight_types[weight_level]&T_BACKWARD) {
				execerror(gettext(
				"FORWARD&BACKWARD specified.\n"));
			} else {
				weight_types[weight_level] |= T_FORWARD;
			}
		}
		| BACKWARD
		{
			if (weight_types[weight_level]&T_FORWARD) {
				execerror(gettext(
				"FORWARD&BACKWARD specified.\n"));
			} else {
				weight_types[weight_level] |= T_BACKWARD;
			}
		}
		| POSITION
		{
			weight_types[weight_level] |= T_POSITION;
		}
		;
coll_order	: coll_order coll_entry
		| coll_entry
		;
coll_entry	: coll_ident eol
		{
			(void) adjust_weight($1, cur_level);
		}
		| coll_ident weight_list eol
		{
			(void) adjust_weight($1, cur_level);
		}
		;
coll_ident	: coll_identifier
		{
			if (set_coll_identifier($1) == NULL) {
				execerror(gettext(
				"coll_entry(1): coll_identifier.\n"));
			}
			cur_level = 0;
			$$ = tmporder = $1;
		}
		;
coll_identifier	: encoded_symbols
		{
			args.en = $1;
			$$ = alloc_order(T_CHAR_ENCODED, &args);
		}
		| ID
		{
			args.id = $1;
			$$ = alloc_order(T_CHAR_ID, &args);
			free($1);
		}
		| coll_symbol
		{
			args.id = $1;
			$$ = alloc_order(T_CHAR_CHARMAP, &args);
			free($1);
		}
		| ELLIPSIS
		{
			$$ = alloc_order(T_ELLIPSIS, (union args *)NULL);
		}
		| UNDEFINED
		{
			$$ = alloc_order(T_UNDEFINED, (union args *)NULL);
		}
		;
weight_list	: weight_list ';' weight_symbol
		{
			cur_level++;
		}
		| weight_symbol
		{
			cur_level++;
		}
		;
weight_symbol	: encoded_symbols
		{
			args.en = $1;
			(void) set_weight(WT_RELATIVE, T_CHAR_ENCODED,
				&args, tmporder, cur_level);
		}
		| ID
		{
			args.id = $1;
			(void) set_weight(WT_RELATIVE, T_CHAR_ID,
				&args, tmporder, cur_level);
		}
		| coll_symbol
		{
			args.id = $1;
			(void) set_weight(WT_RELATIVE, T_CHAR_CHARMAP,
				&args, tmporder, cur_level);
		}
		| quoted_string
		{
			args.en = $1;
			(void) set_weight(WT_ONE_TO_MANY, T_CHAR_ENCODED,
				&args, tmporder, cur_level);
		}
		| ELLIPSIS
		{
			(void) set_weight(WT_ELLIPSIS, 0, (union args *)NULL,
				tmporder, cur_level);
		}
		| IGNORE
		{
			(void) set_weight(WT_IGNORE, 0, (union args *)NULL,
				tmporder, cur_level);
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
l_keywords	: L_COLLATE	
		| COLLATING_ELE
		| FROM 	
		| COLLATING_SYM
		| ORDER_START
		| ORDER_END
		| UNDEFINED
		| IGNORE	
		| FORWARD
		| BACKWARD	
		| POSITION
		| END
		;
order_end	: ORDER_END eol
		{
#ifdef DEBUG
			dump_order();
#endif
		}
		;
coll_tlr	: END L_COLLATE eol
		;
eol		: eol EOL
		{
			eat_line(input_file, comment_char, &lineno);
		}
		| EOL
		{
			eat_line(input_file, comment_char, &lineno);
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
encoded_symbol 	: OCTAL_CHAR
		| HEX_CHAR
		| DECIMAL_CHAR
		;
%%

/*
 * Lexical analyzer
 */
int
yylex()
{
	int	x;
	x = _yylex_common(input_file, _XSH4_COLLATE);
	return (x);
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
