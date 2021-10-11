/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)list.c 1.36 95/07/20 SMI"

/*
 * Includes
 */

#ifndef DEBUG
#define	NDEBUG	1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libintl.h>
#include <search.h>

#include "prbutl.h"
#include "source.h"
#include "queue.h"
#include "list.h"
#include "spec.h"
#include "new.h"
#include "fcn.h"

extern caddr_t  g_commitfunc;


/*
 * Typedefs
 */

typedef struct list_probe_args {
	spec_t		 *speclist_p;
	expr_t		 *exprlist_p;

}			   list_probe_args_t;

typedef struct list_attrs_args {
	spec_t		 *speclist_p;
	void		   *attrroot_p;

}			   list_attrs_args_t;

typedef struct attr_node {
	char		   *name;
	void		   *valsroot_p;

}			   attr_node_t;

typedef struct vals_node {
	char		   *name;

}			   vals_node_t;


/*
 * Globals
 */


/*
 * Declarations
 */

static prb_status_t
listprobe(prbctlref_t * ref_p,
	void *calldata_p);
static prb_status_t
probescan(prbctlref_t * ref_p,
	void *calldata_p);
static void
printattrval(spec_t * spec_p,
	char *attr,
	char *value,
	void *pdata);
static void
attrscan(spec_t * spec_p,
	char *attr,
	char *values,
	void *pdata);
static int
attrcompare(const void *node1,
		const void *node2);
static int
valscompare(const void *node1,
		const void *node2);
static void
printattrs(const void *node,
	VISIT order,
	int level);
static void
printvals(const void *node,
	VISIT order,
	int level);
#if 0
static void	 attrnodedel(attr_node_t * an_p);
#endif
static void
valadd(spec_t * spec_p,
	char *val,
	void *calldata_p);


/* ---------------------------------------------------------------- */
/* ----------------------- Public Functions ----------------------- */
/* ---------------------------------------------------------------- */

/*
 * list_set() - lists all of the current probes in a target process
 */

void
list_set(spec_t * speclist_p,
	char *setname_p)
{
	set_t		  *set_p;
	list_probe_args_t args;

	set_p = set_find(setname_p);
	if (!set_p) {
		semantic_err(gettext("missing or invalid set"));
		return;
	}
	args.speclist_p = speclist_p;
	args.exprlist_p = set_p->exprlist_p;
	(void) prb_link_traverse(listprobe, (void *) &args);

}				/* end list_set */


/*
 * list_expr() - lists all of the current probes in an expression list
 */

void
list_expr(spec_t * speclist_p,
	expr_t * expr_p)
{
	list_probe_args_t args;

	args.speclist_p = speclist_p;
	args.exprlist_p = expr_p;
	(void) prb_link_traverse(listprobe, (void *) &args);

}				/* end list_expr */


/*
 * list_values() - list all the values for a supplied spec
 */

void
list_values(spec_t * speclist_p)
{
	list_attrs_args_t args;

	/* setup argument block */
	args.speclist_p = speclist_p;
	args.attrroot_p = NULL;

	/* traverse the probes, recording attributes that match */
	(void) prb_link_traverse(probescan, (void *) &args);

	/* pretty print the results */
	twalk(args.attrroot_p, printattrs);

	/* destroy the attribute tree */
	while (args.attrroot_p) {
		attr_node_t   **aptr;
		char			*anameptr;

		aptr = (attr_node_t **) args.attrroot_p;

		/* destroy the value tree */
		while ((*aptr)->valsroot_p) {
			vals_node_t   **vptr;
			char			*vnameptr;

			vptr = (vals_node_t **) (*aptr)->valsroot_p;
			vnameptr = (*vptr)->name;
#ifdef LEAKCHK
			(void) fprintf(stderr, "freeing value \"%s\"\n",
				vnameptr);
#endif
			(void) tdelete((void *) *vptr, &(*aptr)->valsroot_p,
				valscompare);
			if (vnameptr) free(vnameptr);
		}

		anameptr = (*aptr)->name;
#ifdef LEAKCHK
		(void) fprintf(stderr, "freeing attr \"%s\"\n", anameptr);
#endif
		(void) tdelete((void *) *aptr, &args.attrroot_p, attrcompare);
		if (anameptr) free(anameptr);
	}

}				/* end list_values */


/*
 * list_getattrs() - build an attribute string for this probe.
 */

extern int	  g_procfd;
extern boolean_t  g_kernelmode;

char		   *
list_getattrs(prbctlref_t * prbctlref_p)
{
	tnf_probe_control_t *prbctl_p = &prbctlref_p->wrkprbctl;
	char			buffer[2048];
	size_t		  len;
	char		   *attrs;
	char		   *funcnames;
	char		   *functmp;
	char		   *entry;

	if (g_kernelmode) {
		(void) sprintf(buffer, "enable %s; trace %s; funcs",
			(prbctl_p->test_func) ? "on" : "off",
			(prbctl_p->commit_func ==
				(tnf_probe_func_t) g_commitfunc) ?
				"on" : "off");
	} else {
		(void) sprintf(buffer, "enable %s; trace %s; object %s; funcs",
			(prbctl_p->test_func) ? "on" : "off",
			(prbctl_p->commit_func ==
				(tnf_probe_func_t) g_commitfunc) ?
					"on" : "off",
					prbctlref_p->lmap_p->name);
	}

	if (g_kernelmode) {
		/* No function combinations in the kernel as yet. */
		funcnames = "";
	} else {
		(void) prb_comb_decode(g_procfd,
			(caddr_t) prbctl_p->probe_func, &funcnames);
	}
	functmp = strdup(funcnames);

	for (entry = strtok(functmp, " "); entry; entry = strtok(NULL, " ")) {
		char		   *fcnname;

		(void) strcat(buffer, " ");

		fcnname = fcn_findname(entry);
		if (fcnname) {
			(void) strcat(buffer, "&");
			(void) strcat(buffer, fcnname);
		} else
			(void) strcat(buffer, entry);
	}

	(void) strcat(buffer, ";");

	len = strlen(buffer) + strlen((char *) prbctl_p->attrs) + 1;
	attrs = (char *) malloc(len);

	if (attrs) {
		(void) strcpy(attrs, buffer);
		(void) strcat(attrs, (char *) prbctl_p->attrs);
	}
	if (functmp)
		free(functmp);

	return (attrs);

}				/* end list_getattrs */


/* ---------------------------------------------------------------- */
/* ----------------------- Private Functions ---------------------- */
/* ---------------------------------------------------------------- */

/*
 * probescan() - function used as a callback, gathers probe attributes and
 * values
 */

static		  prb_status_t
probescan(prbctlref_t * ref_p,
	void *calldata_p)
{
	list_attrs_args_t *args_p = (list_attrs_args_t *) calldata_p;
	spec_t		 *speclist_p;
	spec_t		 *spec_p;
	char		   *attrs;

	speclist_p = args_p->speclist_p;
	spec_p = NULL;

	attrs = list_getattrs(ref_p);

	while (spec_p = (spec_t *) queue_next(&speclist_p->qn, &spec_p->qn)) {
		spec_attrtrav(spec_p, attrs, attrscan, calldata_p);
	}

	if (attrs)
		free(attrs);

	return (PRB_STATUS_OK);

}				/* end probescan */


/*
 * attrscan() - called on each matching attr/values component
 */

/*ARGSUSED*/
static void
attrscan(spec_t * spec_p,
	char *attr,
	char *values,
	void *pdata)
{
	list_attrs_args_t *args_p = (list_attrs_args_t *) pdata;
	attr_node_t	*an_p;
	attr_node_t   **ret_pp;
	static spec_t  *allspec = NULL;

	if (!allspec)
		allspec = spec(".*", SPEC_REGEXP);

	an_p = new(attr_node_t);

#ifdef LEAKCHK
	(void) fprintf(stderr, "creating attr \"%s\"\n", attr);
#endif
	an_p->name = strdup(attr);
	an_p->valsroot_p = NULL;

	ret_pp = tfind((void *) an_p, &args_p->attrroot_p, attrcompare);

	if (ret_pp) {
		/*
		 * we already had a node for this attribute; delete ours *
		 * and point at the original instead.
		 */
#ifdef LEAKCHK
		(void) fprintf(stderr, "attr already there \"%s\"\n", attr);
#endif
		if (an_p->name)
			free(an_p->name);
		free(an_p);

		an_p = *ret_pp;
	} else {
		(void) tsearch((void *) an_p, &args_p->attrroot_p, attrcompare);
	}

	spec_valtrav(allspec, values, valadd, (void *) an_p);

}				/* end attrscan */


/*
 * valadd() - add vals to an attributes tree
 */

/*ARGSUSED*/
static void
valadd(spec_t * spec_p,
	char *val,
	void *calldata_p)
{
	attr_node_t	*an_p = (attr_node_t *) calldata_p;

	vals_node_t	*vn_p;
	vals_node_t   **ret_pp;

	vn_p = new(vals_node_t);
#ifdef LEAKCHK
	(void) fprintf(stderr, "creating value \"%s\"\n", val);
#endif
	vn_p->name = strdup(val);

	ret_pp = tfind((void *) vn_p, &an_p->valsroot_p, valscompare);

	if (ret_pp) {
		/* we already had a node for this value */
#ifdef LEAKCHK
		(void) fprintf(stderr, "value already there \"%s\"\n", val);
#endif
		if (vn_p->name)
			free(vn_p->name);
		free(vn_p);
	} else {
		(void) tsearch((void *) vn_p, &an_p->valsroot_p, valscompare);
	}


}				/* end valadd */


/*
 * attrcompare() - compares attribute nodes, alphabetically
 */

static int
attrcompare(const void *node1,
		const void *node2)
{
	return strcmp(((attr_node_t *) node1)->name,
		((attr_node_t *) node2)->name);

}				/* end attrcompare */


/*
 * valscompare() - compares attribute nodes, alphabetically
 */

static int
valscompare(const void *node1,
		const void *node2)
{
	return strcmp(((vals_node_t *) node1)->name,
		((vals_node_t *) node2)->name);

}				/* end valscompare */


/*
 * printattrs() - prints attributes from the attr tree
 */

/*ARGSUSED*/
static void
printattrs(const void *node,
	VISIT order,
	int level)
{
	attr_node_t	*an_p = (*(attr_node_t **) node);

	if (order == postorder || order == leaf) {
		(void) printf("%s =\n", an_p->name);
		twalk(an_p->valsroot_p, printvals);
	}
}				/* end printattrs */


/*
 * printvals() - prints values from a value tree
 */

/*ARGSUSED*/
static void
printvals(const void *node,
	VISIT order,
	int level)
{
	vals_node_t	*vn_p = (*(vals_node_t **) node);

	if (order == postorder || order == leaf)
		(void) printf("	   %s\n", vn_p->name);

}				/* end printvals */


#if 0
/*
 * attrnodedel() - deletes an attr_node_t after the action
 */

static void
attrnodedel(attr_node_t * an_p)
{
	if (an_p->name)
		free(an_p->name);

	/* destroy the value tree */
	while (an_p->valsroot_p) {
		vals_node_t   **ptr;

		ptr = (vals_node_t **) an_p->valsroot_p;
		(void) tdelete((void *) *ptr, &an_p->valsroot_p, valscompare);
	}

	/* We don't need to free this object, since tdelete() appears to */
	/* free(an_p); */

}				/* end attrnodedel */
#endif


/*
 * listprobe() - function used as a callback, pretty prints a probe
 */

static		  prb_status_t
listprobe(prbctlref_t * ref_p,
	void *calldata_p)
{
	static spec_t  *default_speclist = NULL;
	list_probe_args_t *args_p = (list_probe_args_t *) calldata_p;
	spec_t		 *speclist_p;
	spec_t		 *spec_p;
	boolean_t	   sawattr;
	char		   *attrs;

	/* build a default speclist if there is not one built already */
	if (!default_speclist) {
		default_speclist = spec_list(
			spec_list(
				spec_list(
					spec_list(
						spec_list(
							spec("name",
								SPEC_EXACT),
							spec("enable",
								SPEC_EXACT)),
						spec("trace", SPEC_EXACT)),
					spec("file", SPEC_EXACT)),
				spec("line", SPEC_EXACT)),
			spec("funcs", SPEC_EXACT));
	}
	attrs = list_getattrs(ref_p);

	if (expr_match(args_p->exprlist_p, attrs)) {
		speclist_p = args_p->speclist_p;
		speclist_p = (speclist_p) ? speclist_p : default_speclist;

#ifdef OLDLIST
		(void) printf("%c ", (prbctl_p->test_func) ? '+' : '-');
		(void) printf("%c ", (prbctl_p->commit_func ==
			(tnf_probe_func_t) g_commitfunc) ?
			'T' : 'U');
#endif
		spec_p = NULL;
		while (spec_p = (spec_t *)
			queue_next(&speclist_p->qn, &spec_p->qn)) {
			sawattr = B_FALSE;
			spec_attrtrav(spec_p, attrs, printattrval, &sawattr);
			if (!sawattr)
				(void) printf("<no attr> ");
		}
		(void) printf("\n");
	}
	if (attrs)
		free(attrs);

	return (PRB_STATUS_OK);

}				/* end listprobe */


/*ARGSUSED*/
static void
printattrval(spec_t * spec_p,
	char *attr,
	char *value,
	void *pdata)
{
	boolean_t	  *bptr = (boolean_t *) pdata;

	*bptr = B_TRUE;

	(void) printf("%s=%s ", attr, (value && *value) ? value : "<no value>");

}				/* end printattrval */
