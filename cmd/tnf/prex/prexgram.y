%{
/*
 *	Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)prexgram.y 1.20 95/08/17 SMI"
%}

%token ADD
%token ALLOC
%token BUFFER
%token CLEAR
%token COMMA
%token CONNECT
%token DEALLOC
%token DELETE
%token FILTER
%token CONTINUE
%token CREATE
%token DISABLE
%token ENABLE
%token EQ
%token FCNNAME
%token FCNS
%token ON
%token OFF
%token HELP
%token KTRACE
%token HISTORY
%token IDENT
%token INVAL
%token KILL
%token LIST
%token NL
%token PFILTER
%token PROBES
%token QUIT
%token REGEXP
%token RESUME
%token SCALED_INT
%token SETNAME
%token SETS
%token SOURCE
%token SUSPEND
%token TRACE
%token UNTRACE
%token VALSTR
%token VALUES

%{
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/tnf.h>

#include "set.h"
#include "cmd.h"
#include "fcn.h"
#include "list.h"
#include "expr.h"
#include "spec.h"
#include "source.h"
#include "prbk.h"

extern int	yylex();

void		quit(boolean_t	killtarget, boolean_t  runtarget);
extern void	stmt(void);
extern void	help(void);
extern boolean_t g_kernelmode;
%}

%union
{
	char *	strval;
	expr_t *	exprval;
	spec_t *	specval;
	void *	pidlistval;
	int		intval;
}

%type <strval>	SETNAME FCNNAME IDENT VALSTR REGEXP
%type <exprval>	expr exprlist
%type <specval> spec speclist
%type <pidlistval> pidlist
%type <intval>	SCALED_INT singlepid

%%

file			: statement_list
			;

statement_list		: /* empty */			{ stmt(); prompt(); }
			| statement_list statement	{ stmt(); prompt(); }
			;

statement		: empty_statement
			| help_statement
			| continue_statement
			| quit_statement
			| enable_statement
			| disable_statement
			| trace_statement
			| untrace_statement
			| connect_statement
			| clear_statement
			| pfilter_statement
			| ktrace_statement
			| buffer_statement
			| create_statement
			| source_statement
			| listsets_statement
			| listhistory_statement
			| listfcns_statement
			| listprobes_statement
			| listvalues_statement
			| error NL		{ yyerrok; }
			;

empty_statement		: NL
			;

help_statement		: HELP NL		{ help(); }
			;

continue_statement	: CONTINUE NL
				{
					stmt();
					if (!g_kernelmode) YYACCEPT;
				}
			;

quit_statement		: QUIT NL		{ quit(B_TRUE, B_TRUE); }
			| QUIT KILL NL		{ quit(B_TRUE, B_FALSE); }
			| QUIT RESUME NL	{ quit(B_FALSE, B_TRUE); }
			| QUIT SUSPEND NL	{ quit(B_FALSE, B_FALSE); }
			;

enable_statement	: ENABLE SETNAME NL
					{ cmd_set($2, CMD_ENABLE, NULL); }
			| ENABLE exprlist NL
					{ cmd_expr($2, CMD_ENABLE, NULL); }
			;

disable_statement	: DISABLE SETNAME NL
					{ cmd_set($2, CMD_DISABLE, NULL); }
			| DISABLE exprlist NL
					{ cmd_expr($2, CMD_DISABLE, NULL); }
			;

trace_statement		: TRACE SETNAME NL
					{ cmd_set($2, CMD_TRACE, NULL); }
			| TRACE exprlist NL
					{ cmd_expr($2, CMD_TRACE, NULL); }
			;

untrace_statement	: UNTRACE SETNAME NL
					{ cmd_set($2, CMD_UNTRACE, NULL); }
			| UNTRACE exprlist NL
					{ cmd_expr($2, CMD_UNTRACE, NULL); }
			;

connect_statement	: CONNECT FCNNAME SETNAME NL
					{ cmd_set($3, CMD_CONNECT, $2); }
			| CONNECT FCNNAME exprlist NL
					{ cmd_expr($3, CMD_CONNECT, $2); }
			;

clear_statement		: CLEAR SETNAME NL
					{ cmd_set($2, CMD_CLEAR, NULL); }
			| CLEAR exprlist NL
					{ cmd_expr($2, CMD_CLEAR, NULL); }
			;

create_statement	: CREATE SETNAME exprlist NL	{ (void) set($2, $3); }
			| CREATE FCNNAME IDENT NL	{ fcn($2, $3); }
			;

source_statement	: SOURCE VALSTR NL	{ source_file($2); }
			| SOURCE IDENT NL	{ source_file($2); }
			;

listsets_statement	: LIST SETS NL		{ set_list(); }
			;

listhistory_statement	: LIST HISTORY NL	{ cmd_list(); }
			;

listfcns_statement	: LIST FCNS NL		{ fcn_list(); }
			;

			;

pfilter_statement	: PFILTER ON NL
					 { prbk_set_pfilter_mode(B_TRUE); }
			| PFILTER OFF NL
					 { prbk_set_pfilter_mode(B_FALSE); }
			| PFILTER ADD pidlist NL
					{ prbk_pfilter_add($3); }
			| PFILTER DELETE pidlist NL
					{ prbk_pfilter_drop($3); }
			| PFILTER NL
					{ prbk_show_pfilter_mode(); }
			;

ktrace_statement	: KTRACE ON NL
					{ prbk_set_tracing(B_TRUE); }
			| KTRACE OFF NL
					{ prbk_set_tracing(B_FALSE); }
			| KTRACE NL
					{ prbk_show_tracing(); }
			;

listprobes_statement	: LIST speclist PROBES SETNAME NL
						{ list_set($2, $4); }
			| LIST speclist PROBES exprlist NL
						{ list_expr($2, $4); }
			;

listvalues_statement	: LIST VALUES speclist NL { list_values($3); }
			;

exprlist		: /* empty */		{ $$ = NULL; }
			| exprlist expr		{ $$ = expr_list($1, $2); }
			;

speclist		: /* empty */		{ $$ = NULL; }
			| speclist spec		{ $$ = spec_list($1, $2); }
			;

expr			: spec EQ spec		{ $$ = expr($1, $3); }
			| spec			{ $$ = expr(spec(strdup("keys"),
							    SPEC_EXACT),
							  $1); }
			;

spec			: IDENT			{ $$ = spec($1, SPEC_EXACT); }
			| VALSTR		{ $$ = spec($1, SPEC_EXACT); }
			| REGEXP		{ $$ = spec($1, SPEC_REGEXP); }
			;

pidlist			: pidlist COMMA singlepid
				{ $$ = prbk_pidlist_add($1, $3); }
					
			| singlepid
				{ $$ = prbk_pidlist_add(NULL, $1); }
			;

singlepid		: SCALED_INT
			;

buffer_statement	: BUFFER NL
				{
				    prbk_buffer_list();
				}
			| BUFFER ALLOC NL
				{
				    extern int g_outsize;
				    prbk_buffer_alloc(g_outsize);
				}
			| BUFFER ALLOC SCALED_INT NL
				{
				    prbk_buffer_alloc($3);
				}
			| BUFFER DEALLOC NL
				{
				    prbk_buffer_dealloc();
				}
			;


%%
