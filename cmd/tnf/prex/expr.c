/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)expr.c 1.21 94/09/01 SMI"

/*
 * Includes
 */

/* we need to define this to get strtok_r from string.h */
/* SEEMS LIKE A BUG TO ME */
#define	_REENTRANT

#ifndef DEBUG
#define	NDEBUG	1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <assert.h>
#include "spec.h"
#include "expr.h"
#include "new.h"


/*
 * Typedefs
 */

typedef enum {
	MATCH_NONE = 0,
	MATCH_FALSE,
	MATCH_TRUE


}			   match_t;


/*
 * Declarations
 */

static boolean_t matchattrs(expr_t * expr_p, const char *attrs);
static void matchvals(spec_t * spec_p, char *attrstr,
	char *valstr, void *calldatap);
static void matched(spec_t * spec_p, char *valstr, void *calldatap);


/* ---------------------------------------------------------------- */
/* ----------------------- Public Functions ----------------------- */
/* ---------------------------------------------------------------- */

/*
 * expr() - builds an expr
 */

expr_t		 *
expr(spec_t * left_p,
	spec_t * right_p)
{
	expr_t		 *new_p;

	new_p = new(expr_t);
	queue_init(&new_p->qn);
	new_p->left_p = left_p;
	new_p->right_p = right_p;

	return (new_p);

}				/* end expr */


/*
 * expr_dup() - duplicates an expression list
 */

expr_t		 *
expr_dup(expr_t * list_p)
{
	expr_t		 *expr_p;
	expr_t		 *head_p;

	if (!list_p)
		return (NULL);

	/* copy the first node */
	head_p = expr(spec_dup(list_p->left_p),
		spec_dup(list_p->right_p));

	/* append each additional node */
	expr_p = list_p;
	while (expr_p = (expr_t *) queue_next(&list_p->qn, &expr_p->qn)) {
		expr_t		 *new_p;

		new_p = expr(spec_dup(expr_p->left_p),
			spec_dup(expr_p->right_p));
		(void) queue_append(&head_p->qn, &new_p->qn);
	}

	return (head_p);

}				/* end expr_dup */


/*
 * expr_destroy() - destroys an expression list
 */

void
expr_destroy(expr_t * list_p)
{
	expr_t		 *expr_p;

	while (expr_p = (expr_t *) queue_next(&list_p->qn, &list_p->qn)) {
		(void) queue_remove(&expr_p->qn);

		if (expr_p->left_p)
			spec_destroy(expr_p->left_p);
		if (expr_p->right_p)
			spec_destroy(expr_p->right_p);
		free(expr_p);
	}

	if (list_p->left_p)
		spec_destroy(list_p->left_p);
	if (list_p->right_p)
		spec_destroy(list_p->right_p);
	free(list_p);

}				/* end expr_destroy */


/*
 * expr_list() - append a expr_t to a list
 */

expr_t		 *
expr_list(expr_t * h,
	expr_t * f)
{
	/* queue append handles the NULL cases OK */
	return ((expr_t *) queue_append(&h->qn, &f->qn));

}				/* end expr_list */


/*
 * expr_print() - pretty prints an expr list
 */

void
expr_print(FILE * stream,
	expr_t * list_p)
{
	expr_t		 *expr_p = NULL;

	while ((expr_p = (expr_t *) queue_next(&list_p->qn, &expr_p->qn))) {
		spec_print(stream, expr_p->left_p);
		(void) fprintf(stream, "=");
		spec_print(stream, expr_p->right_p);
		(void) fprintf(stream, " ");
	}

}				/* end expr_print */


/*
 * expr_match() - figures out whether a probe matches in an expression list
 */

boolean_t
expr_match(expr_t * list_p,
	const char *attrs)
{
	expr_t		 *expr_p = NULL;

	while ((expr_p = (expr_t *) queue_next(&list_p->qn, &expr_p->qn))) {
		if (matchattrs(expr_p, attrs))
			return (B_TRUE);
	}

	return (B_FALSE);

}				/* end expr_match */


/* ---------------------------------------------------------------- */
/* ----------------------- Private Functions ---------------------- */
/* ---------------------------------------------------------------- */

#ifdef OLD
/*
 * matchattrs() - check for matches between in an attribute list
 */

static		  boolean_t
matchattrs(expr_t * expr_p,
	const char *attrs)
{
	boolean_t	   match = B_FALSE;
	char		   *lasts;
	char		   *ptr = NULL;
	char		   *pair;

	/* need to make a copy so strtok can write into it */
	ptr = strdup(attrs);

	/* loop over each attribute section separated by ';' */
	for (pair = strtok_r(ptr, ";", &lasts); pair;
		pair = strtok_r(NULL, ";", &lasts)) {
		char		   *a;
		char		   *v;
		char		   *ls;

		a = strtok_r(pair, " \t", &ls);

		/*
		 * #### MISSING - need to handle quoted value strings *
		 * containing whitespace.
		 */

		/* loop over space separated values */
		for (v = strtok_r(NULL, " \t", &ls); v;
			v = strtok_r(NULL, " \t", &ls)) {
			if (spec_match(expr_p->left_p, a) &&
				spec_match(expr_p->right_p, v)) {
				match = B_TRUE;
				goto alldone;
			}
		}
	}

alldone:
	if (ptr)
		free(ptr);

	return (match);

}				/* end match */
#endif


typedef struct matchargs {
	spec_t		 *spec_p;
	boolean_t	   match;

}			   matchargs_t;

static		  boolean_t
matchattrs(expr_t * expr_p,
	const char *attrs)
{
	matchargs_t	 args;

	args.spec_p = expr_p->right_p;
	args.match = B_FALSE;

	spec_attrtrav(expr_p->left_p,
		(char *) attrs, matchvals, (void *) &args);

	return (args.match);

}				/* end matchattrs */


/*ARGSUSED*/
static void
matchvals(spec_t * spec_p,
	char *attrstr,
	char *valstr,
	void *calldatap)
{
	matchargs_t	*args_p = (matchargs_t *) calldatap;

	spec_valtrav(args_p->spec_p, valstr, matched, calldatap);

}				/* matchvals */


/*ARGSUSED*/
static void
matched(spec_t * spec_p,
	char *valstr,
	void *calldatap)
{
	matchargs_t	*args_p = (matchargs_t *) calldatap;

	args_p->match = B_TRUE;

}				/* end matched */
