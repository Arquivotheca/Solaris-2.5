/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)cmd.c 1.24 94/09/30 SMI"

/*
 * Includes
 */

#ifndef DEBUG
#define	NDEBUG	1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <libintl.h>
#include "cmd.h"
#include "set.h"
#include "fcn.h"
#include "new.h"
#include "source.h"


/*
 * Globals
 */

static queue_node_t g_cmdlist = {
	&g_cmdlist,
&g_cmdlist};


/*
 * cmd_set() - creates a cmd using a named set and adds it to the global list
 */

void
cmd_set(char *setname_p, cmd_kind_t kind, char *fcnname_p)
{
	cmd_t		  *new_p;
	set_t		  *set_p;

	set_p = set_find(setname_p);
	if (!set_p) {
		semantic_err(gettext("no set named \"$%s\""), setname_p);
		return;
	}
	if (kind == CMD_CONNECT && !fcn_find(fcnname_p)) {
		semantic_err(gettext("no function named \"&%s\""), fcnname_p);
		return;
	}
	new_p = new(cmd_t);
	queue_init(&new_p->qn);
#ifdef LATEBINDSETS
	new_p->isnamed = B_TRUE;
	new_p->expr.setname_p = setname_p;
#else
	new_p->isnamed = B_FALSE;
	new_p->expr.expr_p = expr_dup(set_p->exprlist_p);
#endif
	new_p->isnew = B_TRUE;
	new_p->kind = kind;
	new_p->fcnname_p = fcnname_p;

	(void) queue_append(&g_cmdlist, &new_p->qn);

}				/* end cmd_set */


/*
 * cmd_expr() - creates a cmd using a set and adds it to the global list
 */

void
cmd_expr(expr_t * expr_p, cmd_kind_t kind, char *fcnname_p)
{
	cmd_t		  *new_p;

	if (kind == CMD_CONNECT && !fcn_find(fcnname_p)) {
		semantic_err(gettext("no function named \"&%s\""), fcnname_p);
		return;
	}
	new_p = new(cmd_t);
	queue_init(&new_p->qn);
	new_p->isnamed = B_FALSE;
	new_p->expr.expr_p = expr_p;
	new_p->isnew = B_TRUE;
	new_p->kind = kind;
	new_p->fcnname_p = fcnname_p;

	(void) queue_append(&g_cmdlist, &new_p->qn);

}				/* end cmd */


#if 0
/*
 * cmd_destroy()
 */

static void
cmd_destroy(cmd_t * cmd_p)
{
	if (!cmd_p)
		return;

	if (!queue_isempty(&cmd_p->qn))
		(void) queue_remove(&cmd_p->qn);

	if (!cmd_p->isnamed)
		expr_destroy(cmd_p->expr.expr_p);

	free(cmd_p);

}				/* end cmd_destroy */
#endif


/*
 * cmd_list() - pretty prints the global cmdlist
 */

void
cmd_list(void)
{
	cmd_t		  *cmd_p;
	int			 i = 0;
	char		   *str_p;

	cmd_p = (cmd_t *) & g_cmdlist;
	while ((cmd_p = (cmd_t *) queue_next(&g_cmdlist, &cmd_p->qn))) {
		switch (cmd_p->kind) {
		case CMD_ENABLE:
			str_p = "enable ";
			break;
		case CMD_DISABLE:
			str_p = "disable";
			break;
		case CMD_CONNECT:
			str_p = "connect";
			break;
		case CMD_CLEAR:
			str_p = "clear  ";
			break;
		case CMD_TRACE:
			str_p = "trace  ";
			break;
		case CMD_UNTRACE:
			str_p = "untrace";
			break;
		default:
			str_p = "???????";
			break;
		}
		(void) printf("[%d] %s ", i++, str_p);

		if (cmd_p->kind == CMD_CONNECT) {
			(void) printf("&%s ", cmd_p->fcnname_p);
		}
		if (!cmd_p->isnamed) {
			expr_print(stdout, cmd_p->expr.expr_p);
		}

		(void) printf("\n");
	}

}				/* end cmd_list */


/*
 * cmd_traverse() - calls the suppied traversal function on each command.
 */

void
cmd_traverse(cmd_traverse_func_t percmdfunc, void *calldata_p)
{
	cmd_t		  *cmd_p;

	cmd_p = (cmd_t *) & g_cmdlist;
	while ((cmd_p = (cmd_t *) queue_next(&g_cmdlist, &cmd_p->qn))) {
		expr_t		 *expr_p;
		fcn_t		  *fcn_p;

		if (!cmd_p->isnamed) {
			expr_p = cmd_p->expr.expr_p;
		}

		if (cmd_p->kind == CMD_CONNECT) {
			fcn_p = fcn_find(cmd_p->fcnname_p);
			assert(fcn_p);
		}
		else
			fcn_p = NULL;

		(*percmdfunc) (expr_p,
			cmd_p->kind,
			fcn_p, cmd_p->isnew, calldata_p);
	}

}				/* end cmd_traverse */


/*
 * cmd_mark() - mark all of the commands in the global list as old
 */

void
cmd_mark(void)
{
	cmd_t		  *cmd_p;

	cmd_p = (cmd_t *) & g_cmdlist;
	while ((cmd_p = (cmd_t *) queue_next(&g_cmdlist, &cmd_p->qn))) {
		cmd_p->isnew = B_FALSE;
	}

}				/* end cmd_mark */


#if 0
/*
 * cmd_delete() -
 */

void
cmd_delete(int cmdnum)
{
	cmd_t		  *cmd_p;
	int			 i = 0;

	cmd_p = (cmd_t *) & g_cmdlist;
	while ((cmd_p = (cmd_t *) queue_next(&g_cmdlist, &cmd_p->qn))) {
		if (cmdnum == i) {
			cmd_destroy(cmd_p);
			return;
		}
		i++;
	}

}				/* end cmd_delete */
#endif
