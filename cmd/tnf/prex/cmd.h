/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _CMD_H
#define	_CMD_H

#pragma ident  "@(#)cmd.h 1.17 94/08/26 SMI"

/*
 * Includes
 */

#include <sys/types.h>
#include "prbutl.h"
#include "queue.h"
#include "expr.h"
#include "set.h"
#include "fcn.h"


/*
 * Typedefs
 */

typedef enum cmd_kind {
	CMD_ENABLE,
	CMD_DISABLE,
	CMD_CONNECT,
	CMD_CLEAR,
	CMD_TRACE,
	CMD_UNTRACE


}			   cmd_kind_t;

typedef struct cmd {
	queue_node_t	qn;
	boolean_t	   isnamed;
	boolean_t	   isnew;
	union {
#ifdef LATEBINDSETS
		char		   *setname_p;
#endif
		expr_t		 *expr_p;
	}			   expr;
	char		   *fcnname_p;
	cmd_kind_t	  kind;

}			   cmd_t;

typedef
prb_status_t(*cmd_traverse_func_t) (
					expr_t * expr_p,
					cmd_kind_t kind,
					fcn_t * fcn_p,
					boolean_t isnew,
					void *calldata_p);


/*
 * Declarations
 */

void cmd_set(char *setname_p, cmd_kind_t kind, char *fcnname_p);
void cmd_expr(expr_t * expr_p, cmd_kind_t kind, char *fcnname_p);
void cmd_list(void);
#if 0
void cmd_delete(int cmdnum);
#endif
void cmd_traverse(cmd_traverse_func_t percmdfunc, void *calldata_p);
void cmd_mark(void);

#endif				/* _CMD_H */
