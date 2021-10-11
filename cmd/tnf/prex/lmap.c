/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)lmap.c 1.11 94/09/08 SMI"

/*
 * Includes
 */

#include <stdlib.h>
#include <stdio.h>
#include <link.h>

#include "prbutl.h"
#include "dbg.h"


/*
 * Globals
 */

static int	  g_linkcnt = 0;
static prb_lmap_entry_t *g_linkmap = NULL;


/*
 * Declarations
 */

static int
addr_compare(const void *elem1,
	const void *elem2);


/*
 * prb_lmap_update() - updates the internal copy of the link map.
 */

prb_status_t
prb_lmap_update(int procfd)
{
	prb_status_t	prbstat;
	caddr_t		 dentaddr;
	Elf32_Dyn	   dentry;
	struct r_debug  r_dbg;
	caddr_t		 lmapaddr;
	struct link_map lmap;
	int			 i;
	prb_lmap_entry_t *new_linkmap;
	int			 new_linkcnt;

#ifdef DEBUG
	if (__prb_verbose) {
		(void) fprintf(stderr, "checking link map\n");
	}
#endif

	prbstat = prb_elf_dbgent(procfd, &dentaddr);
	if (prbstat)
		return (prbstat);

	prbstat = prb_proc_read(procfd, dentaddr,
		&dentry, sizeof (dentry));
	if (prbstat || !dentry.d_un.d_ptr) {
#ifdef DEBUG
		(void) fprintf(stderr,
			"prb_lmap_update: error in d_un.d_ptr\n");
#endif
		return (prbstat);
	}
	/* read in the debug struct that it points to */
	prbstat = prb_proc_read(procfd, (caddr_t) dentry.d_un.d_ptr,
		&r_dbg, sizeof (r_dbg));
	if (prbstat)
		return (prbstat);

#ifdef DEBUG
	if (__prb_verbose) {
		(void) fprintf(stderr, "  link map state = %s\n",
			(r_dbg.r_state == RT_CONSISTENT) ? "RT_CONSISTENT" :
			(r_dbg.r_state == RT_ADD) ? "RT_ADD" :
			"RT_DELETE");
	}
#endif

	/* if the link map is not consistent, bail now */
	if (r_dbg.r_state != RT_CONSISTENT)
		return (PRB_STATUS_BADLMAPSTATE);

	new_linkmap = NULL;
	new_linkcnt = 0;

	lmap.l_next = NULL;			/* makes lint happy */

	for (lmapaddr = (caddr_t) r_dbg.r_map; lmapaddr;
		lmapaddr = (caddr_t) lmap.l_next) {
		prb_lmap_entry_t *entry_p;

		prbstat = prb_proc_read(procfd, lmapaddr,
			&lmap, sizeof (lmap));
		if (prbstat)
			return (prbstat);

		/* resize the link map memory */
		new_linkmap = (prb_lmap_entry_t *) realloc(new_linkmap,
			(new_linkcnt + 1) * sizeof (prb_lmap_entry_t));
		entry_p = &new_linkmap[new_linkcnt];
		new_linkcnt += 1;

		entry_p->addr = (caddr_t) lmap.l_addr;
		entry_p->name = NULL;

		(void) prb_proc_readstr(procfd, lmap.l_name, &entry_p->name);
	}

	/* sort the link map by address */
	qsort(new_linkmap, new_linkcnt,
		sizeof (prb_lmap_entry_t), addr_compare);

	/* check the new link map against the old and identify new LO's */
	for (i = 0; i < new_linkcnt; i++) {
		if (g_linkcnt) {
			new_linkmap[i].isnew =
				(bsearch(&new_linkmap[i], g_linkmap, g_linkcnt,
					sizeof (prb_lmap_entry_t),
					addr_compare))
				? B_FALSE : B_TRUE;
		} else
			new_linkmap[i].isnew = B_TRUE;
	}

	/* free the old link map */
	if (g_linkmap) {
		for (i = 0; i < g_linkcnt; i++)
			if (g_linkmap[i].name)
				free(g_linkmap[i].name);
		free(g_linkmap);
	}

	/* transfer the new linkmap into the official place */
	g_linkmap = new_linkmap;
	g_linkcnt = new_linkcnt;

#ifdef DEBUG
	for (i = 0; i < g_linkcnt; i++) {
		prb_lmap_entry_t *entry_p = &g_linkmap[i];

		if (__prb_verbose) {
			(void) fprintf(stderr, "%s 0x%08x %s\n",
				(entry_p->isnew) ? "*" : " ",
				entry_p->addr, entry_p->name);
		}
	}
#endif

	return (PRB_STATUS_OK);

}				/* end prb_lmap_update */


#ifdef OLD
/*
 * prb_lmap_check() - checks to see if an address is in the link map
 */

prb_status_t
prb_lmap_check(int procfd,
	caddr_t addr)
{
	int			 i;

	/* procfd is unused, retained for consistency */

	if (!g_linkmap)
		return (PRB_STATUS_BADLMAPSTATE);

	for (i = 0; i < g_linkcnt; i++)
		if (g_linkmap[i].addr == addr)
			return (PRB_STATUS_OK);

	return (PRB_STATUS_NOTINLMAP);

}				/* end prb_lmap_check */
#endif


/*
 * prb_lmap_find() - finds a given entry in the link map
 */

/*ARGSUSED*/
prb_status_t
prb_lmap_find(int procfd,
	caddr_t addr,
	prb_lmap_entry_t ** entry_pp)
{
	prb_lmap_entry_t entry;
	prb_lmap_entry_t *entry_p = NULL;

	/* procfd is unused, retained for consistency */

	*entry_pp = NULL;

	if (!g_linkmap)
		return (PRB_STATUS_BADLMAPSTATE);

	entry.addr = addr;

	entry_p = (prb_lmap_entry_t *) bsearch(&entry, g_linkmap, g_linkcnt,
		sizeof (prb_lmap_entry_t),
		addr_compare);

	if (entry_p) {
		*entry_pp = entry_p;
		return (PRB_STATUS_OK);
	} else {
		return (PRB_STATUS_NOTINLMAP);
	}

}				/* end prb_lmap_find */


/*
 * prb_lmap_mark() - mark all of the link map entries as old
 */

/*ARGSUSED*/
prb_status_t
prb_lmap_mark(int procfd)
{
	int			 i;

	for (i = 0; i < g_linkcnt; i++)
		g_linkmap[i].isnew = B_FALSE;

	return (PRB_STATUS_OK);

}				/* end prb_lmap_mark */


/*
 * addr_compare() - comparison functions for lmap entries
 */

static int
addr_compare(const void *elem1,
	const void *elem2)
{
	prb_lmap_entry_t *p1 = (prb_lmap_entry_t *) elem1;
	prb_lmap_entry_t *p2 = (prb_lmap_entry_t *) elem2;

	if ((unsigned) p1->addr > ((unsigned) p2->addr))
		return (1);
	else if ((unsigned) p1->addr < ((unsigned) p2->addr))
		return (-1);
	else
		return (0);

}				/* end addr_compare */
