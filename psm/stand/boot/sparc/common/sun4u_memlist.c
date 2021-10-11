/*
 * Copyright (c) 1992-1994 Sun Microsystems, Inc.  All Rights Reserved.
 */

#pragma ident "@(#)sun4u_memlist.c	1.6	95/02/14 SMI"

#include <sys/types.h>
#include <sys/promif.h>
#include <sys/openprom.h>	/* only for struct prom_memlist */
#include <sys/bootconf.h>

/*
 * This file defines the interface from the prom and platform-dependent
 * form of the memory lists, to boot's more generic form of the memory
 * list.  For sun4u, the memory list properties are {hi, lo, size_hi, size_lo},
 * which is similar to boot's format, except boot's format is a linked
 * list, and the prom's is an array of these structures. Note that the
 * native property on sparc machines is identical to the property encoded 
 * format, so no property decoding is required.
 *
 * Note that the format of the memory lists is really 4 encoded integers,
 * but the encoding is the same as that given in the following structure
 * on SPARC systems ...
 */

struct sun4u_prom_memlist {
	u_longlong_t	addr;
	u_longlong_t	size;
};

struct sun4u_prom_memlist scratch_memlist[200];

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
static struct memlist *reg_to_list(struct sun4u_prom_memlist *a, int size);
static void sort_reglist(struct sun4u_prom_memlist *ar, int size);
extern void thread_links(struct memlist *list);

void
init_memlists()
{
	/* this list is a map of pmem actually installed */
	pinstalledp = fill_memlists("memory", "reg");

	pfreelistp = fill_memlists("memory", "available");
	vfreelistp = fill_memlists("virtual-memory", "available");

	kmem_init();
}

struct memlist *
fill_memlists(char *name, char *prop)
{
	static dnode_t pmem = 0;
	static dnode_t pmmu = 0;
	dnode_t node;
	int links;
	struct memlist *al;
	struct sun4u_prom_memlist *pm = scratch_memlist;

	if (pmem == (dnode_t)0)  {

		/*
		 * Figure out the interesting phandles, one time
		 * only.
		 */

		ihandle_t ih;

		if ((ih = prom_mmu_ihandle()) == (ihandle_t)-1)
			prom_panic("Can't get mmu ihandle");
		pmmu = prom_getphandle(ih);

		if ((ih = prom_memory_ihandle()) == (ihandle_t)-1)
			prom_panic("Can't get memory ihandle");
		pmem = prom_getphandle(ih);
	}

	if (strcmp(name, "memory") == 0)
		node = pmem;
	else
		node = pmmu;

	/*
	 * Read memory node and calculate the number of entries
	 */
	if ((links = prom_getproplen(node, prop)) == -1)
		prom_panic("Cannot get list.\n");
	if (links > sizeof scratch_memlist) {
		prom_printf("%s list <%s> exceeds boot capabilities\n",
			name, prop);
		prom_panic("fill_memlists - memlist size");
	}
	links = links / sizeof (struct sun4u_prom_memlist);


	prom_getprop(node, prop, (caddr_t)pm);
	sort_reglist(pm, links);
	al = reg_to_list(pm, links);
	thread_links(al);
	return (al);
}

/*
 *  Simple selection sort routine.
 *  Sorts platform dependent memory lists into ascending order
 */

static void
sort_reglist(struct sun4u_prom_memlist *ar, int n)
{
	int i, j, min;
	struct sun4u_prom_memlist temp;
	u_longlong_t start1, start2;

	for (i = 0; i < n; i++) {
		min = i;

		for (j = i+1; j < n; j++)  {
			if (ar[j].addr < ar[min].addr)
				min = j;
		}

		if (i != min)  {
			/* Swap ar[i] and ar[min] */
			temp = ar[min];
			ar[min] = ar[i];
			ar[i] = temp;
		}
	}
}

/*
 *  This routine will convert our platform dependent memory list into
 *  struct memlists's.  And it will also coalesce adjacent  nodes if
 *  possible.
 */
static struct memlist *
reg_to_list(struct sun4u_prom_memlist *ar, int n)
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
		start1 = ar[i].addr;
		start2 = ar[i+1].addr;
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
