/*
 * Copyright (c) 1992-1994 Sun Microsystems, Inc.  All Rights Reserved.
 */

#pragma ident "@(#)sun4x_memlist.c	1.5	94/12/09 SMI"

#include <sys/types.h>
#include <sys/promif.h>
#include <sys/openprom.h>
#include <sys/bootconf.h>

/*
 * This file contains the memlist/prom interfaces for sun4x machines.
 * There's a similar file for sun5x (and newer machines.)
 */

#define	NIL	((u_int)0)

#ifdef DEBUG
static int memlistdebug = 1;
#else DEBUG
static	int memlistdebug = 0;
#endif DEBUG
#define	dprintf		if (memlistdebug) printf

struct memlist *fill_memlists(char *name, char *prop);
extern struct memlist *pfreelistp, *vfreelistp, *pinstalledp;

extern caddr_t getlink(u_int n);
static struct memlist *reg_to_list(struct avreg *a, int size);
static void sort_reglist(struct avreg *ar, int size);
extern void thread_links(struct memlist *list);
static struct prom_memlist *prom_memory_available(void);
static struct prom_memlist *prom_memory_physical(void);
static struct prom_memlist *prom_memory_virtual(void);

void
init_memlists()
{
	/* this list is a map of pmem actually installed */
	pinstalledp = fill_memlists("memory", "reg");

	pfreelistp = fill_memlists("memory", "available");
	vfreelistp = fill_memlists("virtual-memory", "available");

	kmem_init();
}

static struct prom_memlist *
list_invert_sense(struct prom_memlist *list)
{
	caddr_t va;
	struct prom_memlist *inv_list, *mylist, *ret_list;
	extern caddr_t hole_start, hole_end;

	/* Splatt */
	/* XXX */
	inv_list = ret_list = mylist = (struct prom_memlist *)
	    getlink(sizeof (struct prom_memlist));
	inv_list->address = 0;
	inv_list->size = (u_int)hole_start;

	va = hole_end;

	while (va != (caddr_t)0 && list) {
		inv_list = (struct prom_memlist *)
			getlink(sizeof (struct prom_memlist));
		mylist->next = inv_list;
		mylist = mylist->next;
		inv_list->address = (u_int)va;
		inv_list->size = list->address - (u_int)va;
		va = (caddr_t)(list->address + list->size);
		if ((list = list->next) == NIL) {
			inv_list = (struct prom_memlist *)
				getlink(sizeof (struct prom_memlist));
			mylist->next = inv_list;
			mylist = mylist->next;
			inv_list->address = (u_int)va;
			inv_list->size = (u_int)((caddr_t)0 - va);
			break;
		}
	}

	mylist->next = NIL;

	return (ret_list);
}

struct memlist *
fill_memlists(char *name, char *prop)
{
	dnode_t	node;
	struct memlist *al;
	struct avreg *ar;
	int len = 0;
	int links = 0;
	dnode_t sp[OBP_STACKDEPTH];
	pstack_t *stk;

	stk = prom_stack_init(sp, sizeof (sp));
	node = prom_findnode_byname(prom_rootnode(), name, stk);
	prom_stack_fini(stk);

	if (node == OBP_NONODE) {
		if (prom_getversion() > 0) {
			prom_panic("Cannot find node.\n");

		} else if (strcmp(prop, "available") == 0 &&
		    strcmp(name, "memory") == 0) {

			struct prom_memlist *list, *dbug, *tmp;
			struct avreg *ap;

			dbug = list = prom_memory_available();

			if (memlistdebug) {
				printf("physmem-avail:\n");
				do {
					printf("start = 0x%x, size = 0x%x, "
					    "list = 0x%x\n", dbug->address,
					    dbug->size, dbug);
				} while ((dbug = dbug->next) != NIL);
			}

			for (tmp = list; tmp; tmp = tmp->next)
				links++;
			ar = ap = (struct avreg *)getlink(links *
			    sizeof (struct avreg));
			do {
				ap->type = 0;
				ap->start = list->address;
				ap->size = list->size;
				ap++;
			} while ((list = list->next) != NIL);

		} else if (strcmp(prop, "available") == 0 &&
				strcmp(name, "virtual-memory") == 0) {

			struct prom_memlist *list, *dbug, *tmp;
			struct avreg *ap;

			dbug = list = prom_memory_virtual();

			if (memlistdebug) {
				printf("virtmem-avail: \n");
				do {
					printf("start = 0x%x, size = 0x%x, "
					    "list = 0x%x\n", dbug->address,
					    dbug->size, dbug);
				} while ((dbug = dbug->next) != NIL);
			}

			if (prom_getversion() == 0)
				list = list_invert_sense(list);

			for (tmp = list; tmp; tmp = tmp->next)
				links++;
			ar = ap = (struct avreg *)getlink(links *
			    sizeof (struct avreg));
			do {
				ap->type = 0;
				ap->start = list->address;
				ap->size = list->size;
				ap++;
			} while ((list = list->next) != NIL);

		} else if (strcmp(prop, "reg") == 0 &&
				strcmp(name, "memory") == 0) {
			struct prom_memlist *list, *dbug, *tmp;
			struct avreg *ap;

			dbug = list = prom_memory_physical();

			if (memlistdebug) {

				printf("physmem-installed: \n");
				do {
					printf("start = 0x%x, size = 0x%x, "
					    "list = 0x%x\n", dbug->address,
					    dbug->size, dbug);
				} while ((dbug = dbug->next) != NIL);
			}

			for (tmp = list; tmp; tmp = tmp->next)
				links++;
			ar = ap = (struct avreg *)getlink(links *
			    sizeof (struct avreg));
			do {
				ap->type = 0;
				ap->start = list->address;
				ap->size = list->size;
				ap++;
			} while ((list = list->next) != NIL);
		}
	} else {
		/*
		 * XXX  I know, I know.
		 * But this will get fixed when boot uses arrays, not lists.
		 */
		u_int buf[200/sizeof (u_int)];	/* u_int aligned buffer */

		/* Read memory node, if it is there */
		if ((len = prom_getproplen(node, prop)) ==
		    (int)OBP_BADNODE)
			prom_panic("Cannot get list.\n");

		/* Oh, malloc, where art thou? */
		ar = (struct avreg *)buf;
		prom_getprop(node, prop, (caddr_t)ar);

		/* calculate the number of entries we will have */
		links = len / sizeof (struct avreg);
	}

	sort_reglist(ar, links);

	al = reg_to_list(ar, links);

	thread_links(al);

	return (al);
}

/*
 *  These routines are for use by /boot only.  The kernel
 *  is not to call them because they return a foreign memory
 *  list data type.  If it were to try to copy them, the kernel
 *  would have to dynamically allocate space for each node.
 *  And these promif functions are specifically prohibited from
 *  such activity.
 */

/*
 * This routine will return a list of the physical
 * memory available to standalone programs for
 * further allocation.
 */
static struct prom_memlist *
prom_memory_available(void)
{
	switch (prom_getversion()) {

	case OBP_V0_ROMVEC_VERSION:
		return ((struct prom_memlist *)OBP_V0_AVAILMEMORY);

	default:
		return ((struct prom_memlist *)0);
	}
}

/*
 * This is a description of the number of memory cells actually installed
 * in the system and where they reside.
 */
static struct prom_memlist *
prom_memory_physical(void)
{
	switch (prom_getversion()) {

	case OBP_V0_ROMVEC_VERSION:
		return ((struct prom_memlist *)OBP_V0_PHYSMEMORY);

	default:
		return ((struct prom_memlist *)0);
	}
}

/*
 * This returns a list of virtual memory available
 * for further allocation by the standalone.
 */
static struct prom_memlist *
prom_memory_virtual(void)
{
	switch (prom_getversion()) {

	case OBP_V0_ROMVEC_VERSION:
		return ((struct prom_memlist *)OBP_V0_VIRTMEMORY);

	default:
		return ((struct prom_memlist *)0);
	}
}

/*
 *  Simple selection sort routine.
 *  Sorts avreg's into ascending order
 */

static void
sort_reglist(struct avreg *ar, int n)
{
	int i, j, min;
	struct avreg temp;
	u_longlong_t start1, start2;

	for (i = 0; i < n; i++) {
		min = i;

		for (j = i+1; j < n; j++) {
			start1 = (((u_longlong_t)ar[j].type) << 32) +
			    ar[j].start;
			start2 = (((u_longlong_t)ar[min].type) << 32) +
			    ar[min].start;
			if (start1 < start2)
				min = j;
		}

		/* Swap ar[i] and ar[min] */
		temp = ar[min];
		ar[min] = ar[i];
		ar[i] = temp;
	}
}

/*
 *  This routine will convert our struct avreg's into
 *  struct memlists's.  And it will also coalesce adjacent
 *  nodes if possible.
 */
static struct memlist *
reg_to_list(struct avreg *ar, int n)
{
	struct memlist *al;
	int i;
	u_longlong_t size = 0;
	u_longlong_t addr = 0;
	u_longlong_t start1, start2;
	int flag = 0, j = 0;

	if (n == 0)
		return ((struct memlist *)0);

	if ((al = (struct memlist *)getlink(n*sizeof (struct memlist))) ==
				(struct memlist *)0)
			return ((struct memlist *)0);
	else
		bzero(al, n*sizeof (struct memlist));

	for (i = 0; i < n; i++) {
		start1 = (((u_longlong_t)ar[i].type) << 32) +
		    ar[i].start;
		start2 = (((u_longlong_t)ar[i+1].type) << 32) +
		    ar[i+1].start;
		if (i < n-1 && (start1 + ar[i].size == start2)) {
			size += ar[i].size;
			if (!flag) {
				addr = start1;
				flag++;
			}
			continue;
		} else if (flag) {
			/*
			 * catch the last one on the way out of
			 * this iteration
			 */
			size += ar[i].size;
		}

		al[j].address = flag ? addr : start1;
		al[j].size = size ? size : ar[i].size;
		al[j].next = NIL;
		al[j].prev = NIL;
		j++;
		size = 0;
		flag = 0;
		addr = 0;
	}
	return (al);
}
