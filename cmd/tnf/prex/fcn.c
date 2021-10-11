/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)fcn.c 1.13 94/08/25 SMI"

/*
 * Includes
 */

#include <stdlib.h>
#include <string.h>
#include <libintl.h>
#include "prbutl.h"
#include "queue.h"
#include "set.h"
#include "fcn.h"
#include "new.h"
#include "source.h"


/*
 * Globals
 */

static queue_node_t g_fcnlist = {
	&g_fcnlist,
&g_fcnlist};

extern int	  g_procfd;


/*
 * Forward Declarations
 */

static void fcn_destroy(fcn_t * fcn_p);
static void fcn_print(FILE * stream, fcn_t * fcn_p);


/*
 * fcn() - builds a function block and inserts it on the global list.
 */

#define	NSYMS	1

void
fcn(char *name_p, char *entry_name_p)
{
	fcn_t		  *new_p;
	fcn_t		  *old_p;
	const char	 *symnames[NSYMS];
	caddr_t		 symaddrs[NSYMS];
	prb_status_t	prbstat;

	/* does this setname exist already? */
	old_p = fcn_find(name_p);
	if (old_p)
		fcn_destroy(old_p);

	/* create a new set */
	new_p = new(fcn_t);
	queue_init(&new_p->qn);
	new_p->name_p = name_p;
	new_p->entry_name_p = entry_name_p;

	/* map the initial and final functions to target addresses */
	symnames[0] = entry_name_p;
	prbstat = prb_sym_find(g_procfd, NSYMS, symnames, symaddrs);
	if (prbstat) {
		if (prbstat == PRB_STATUS_SYMMISSING) {
			semantic_err(gettext(
				"no entry named \"%s\""), symnames[0]);
			goto Error;
		}
		semantic_err(gettext("error on function named \"%s\""), name_p);
		goto Error;
	}
	new_p->entry_addr = symaddrs[0];

#ifdef OLD
	/*
	 * allocate a target function block, and stuff the init and fini
	 * addrs
	 */
	prbstat = prb_targmem_alloc(g_procfd, sizeof (probe_funcs_t),
					&new_p->funcs_p);
	if (prbstat) {
		semantic_err(gettext("problem allocating target memory"));
		goto Error;
	}
	prbstat = prb_proc_write(g_procfd, new_p->funcs_p,
		&new_p->funcs, sizeof (probe_funcs_t));
	if (prbstat) {
		semantic_err(gettext(
				"setup problem, initial/final "
				"funcs in target memory"));
		goto Error;
	}
#endif

	/* append the new set to the global list */
	(void) queue_append(&g_fcnlist, &new_p->qn);

	return;

Error:
	if (new_p)
		free(new_p);
	return;

}				/* end fcn */


/*
 * fcn_destroy() - destroys a fcn and related resources
 */

static void
fcn_destroy(fcn_t * fcn_p)
{
	if (!fcn_p)
		return;

	/* remove ourselves from any list */
	if (!queue_isempty(&fcn_p->qn))
		(void) queue_remove(&fcn_p->qn);

	if (fcn_p->name_p)
		free(fcn_p->name_p);
	if (fcn_p->entry_name_p)
		free(fcn_p->entry_name_p);

	free(fcn_p);

}				/* end fcn_destroy */


/*
 * fcn_list() - pretty prints the global fcnlist
 */

void
fcn_list(void)
{
	fcn_t		  *fcn_p;

	fcn_p = (fcn_t *) & g_fcnlist;
	while ((fcn_p = (fcn_t *) queue_next(&g_fcnlist, &fcn_p->qn))) {
		fcn_print(stdout, fcn_p);
	}

}				/* end fcn_list */


/*
 * fcn_print() - pretty prints a fcn
 */

static void
fcn_print(FILE * stream, fcn_t * fcn_p)
{
	if (!fcn_p)
		return;

	(void) fprintf(stream, "&%-8s %-24s\n",
		fcn_p->name_p, fcn_p->entry_name_p);

}				/* end fcn_print */


/*
 * fcn_findname() - find the created name, given an entry name
 */

char		   *
fcn_findname(char *entry_p)
{
	fcn_t		  *fcn_p;

	if (!entry_p)
		return (NULL);

	fcn_p = (fcn_t *) & g_fcnlist;
	while ((fcn_p = (fcn_t *) queue_next(&g_fcnlist, &fcn_p->qn)))
		if (strcmp(entry_p, fcn_p->entry_name_p) == 0)
			return (fcn_p->name_p);

	return (NULL);

}				/* end fcn_findname */


/*
 * fcn_find() - finds a fcn by name
 */

fcn_t		  *
fcn_find(char *fcnname_p)
{
	fcn_t		  *fcn_p;

	if (!fcnname_p)
		return (NULL);

	fcn_p = (fcn_t *) & g_fcnlist;
	while ((fcn_p = (fcn_t *) queue_next(&g_fcnlist, &fcn_p->qn)))
		if (strcmp(fcnname_p, fcn_p->name_p) == 0)
			return (fcn_p);

	return (NULL);

}				/* end set_find */
