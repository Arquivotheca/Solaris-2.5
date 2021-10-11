#ident	"@(#)memlist.c	1.14	94/12/09 SMI"

/*
 * Copyright (c) 1992-1994 Sun Microsystems, Inc.  All Rights Reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/machparam.h>
#include <sys/promif.h>
#include <sys/pte.h>
#include <sys/bootconf.h>

#define	NIL	0
#define	roundup(x, y)   ((((x)+((y)-1))/(y))*(y))

extern caddr_t memlistpage;

extern struct memlist 	*vfreelistp, 	/* virtmem avail */
			*pfreelistp, 	/* physmem avail */
			*pinstalledp; 	/* physmem installed */

/* Always pts to the next free link in the headtable */
/* i.e. it is always memlistpage+tableoffset */
caddr_t tablep = (caddr_t)0;
static int table_freespace;

u_int	memory_avail = 0;

/*
 *	Function prototypes
 */
extern struct memlist *	fill_memlists(char *name, char *prop);
extern int 		insert_node(struct memlist **list,
				struct memlist *newnode);
extern caddr_t 		getlink(u_int n);
extern struct memlist *	get_min_node(struct memlist *list,
				u_longlong_t lo, u_int size);
extern struct prom_memlist *	list_invert_sense(struct prom_memlist *list);
extern void		thread_links(struct memlist *list);
extern void 		reset_alloc(void);

/*
 *  This routine just sews all of the memlist nodes
 *  together, both forw and back.
 */
void
thread_links(struct memlist *al)
{
	int i, links;
	/*
	 * We may have coalesced some nodes, so we gotta
	 * count the links again here.
	 * We rely on the fact that the first unused node
	 * is of size zero since we bzero'd the memory
	 * before we used it.
	 */
	for (i = 0; al[i].size; i++)
		;
	links = i;

	/* Now forward link all the nodes together */
	for (i = 0; i < links-1; i++)
		al[i].next = &al[i+1];

	al[i].next = NIL;

	/* Now the backward links */
	for (i = links-1; i > 0; i--)
		al[i].prev = &al[i-1];

	/* finish off the job */
	al[i].prev = NIL;

}

void
print_memlist(struct memlist *av)
{
	struct memlist *p = av;

	while (p != NIL) {
		printf("addr = 0x%x:0x%x, size = 0x%x:0x%x\n",
		    (u_int)(p->address >> 32), (u_int)p->address,
		    (u_int)(p->size >> 32), (u_int)p->size);
		p = p->next;
	}

}

/*
 *	This does a linear search thru the list until it finds
 *	a node that matches or engulfs the request addr and size.
 *	That node is returned.
 */
struct memlist *
search_list(struct memlist *list, struct memlist *request)
{
	while (list != (struct memlist *)NIL) {

		if ((list->address == request->address) &&
			(list->size >= request->size))
				break;

		if ((list->address < request->address) &&
			((list->address + list->size) >=
			(request->address + request->size)))
				break;

		list = list->next;
	}
	return	(list);
}

/*
 *	This does a linear search thru the list until it finds a node
 *	1). whose startaddr lies above lo and whose size is at least
 *		the size of the requested size, OR
 *	2). lo is between startaddr and startaddr+endaddr, but there is
 *		enough room there so that lo+size is within node's range.
 *	If node's range is completely below lo, it is no good.
 *	When such node is found, that node is returned.
 *	If no node is found, NIL is returned.
 */
struct memlist *
get_min_node(struct memlist *list, u_longlong_t lo, u_int size)
{
	struct memlist *n;

	for (n = list; n != NIL; n = n->next) {

		if ((u_int) lo <= n->address) {
			/*
			 * This node is completely above lo.
			 * Just check for size.
			 */
			if (n->size >= size)
				return (n);
		} else if ((u_int)lo < (n->address + n->size)) {
			/*
			 * lo falls within this node.
			 * Check if the end of needed size is within node.
			 */
			if (((u_int)lo + size) <= (n->address + n->size))
				return (n);
		}
		/* The node is completely below lo. Skip to next one */
	}
	return (n);
}

/*
 *	Once we know if and my what node the request can be
 *	satisfied, we use this routine to stick it in.
 *	There is NO ERROR RETURN from this routine.
 *	The caller MUST call search_list() before this
 *	to assure success.
 */
int
insert_node(struct memlist **list, struct memlist *request)
{
	struct memlist *node;
	struct memlist *np;
	u_int request_eaddr = request->address + request->size;

	/* Search for the memlist that engulfs this request */
	for (node = (*list); node != NIL; node = node->next) {
		if ((node->address <= request->address) &&
		    ((node->address + node->size) >= request_eaddr))
			break;
	}

	if (node == NIL)
		return (0);

	if (request->address == node->address) {
		/* See if we completely replace an existing node */
		/* Then delete the node from the list */

		if (request->size == node->size) {
			if (node->next != NIL) node->next->prev = node->prev;
			if (node->prev != NIL) node->prev->next = node->next;
			else *list = node->next;
			return (1);
		/* else our request is replacing the front half */
		} else {
			node->address = request->address + request->size;
			node->size -= request->size;
			return (1);
		}
	} else if ((request->address + request->size) ==
			(node->address + node->size)) {
		node->size -= request->size;
		return (1);
	}

	/* else we have to split a node */

	np = (struct memlist *)getlink(sizeof (struct memlist));

	/* setup the new data link and */
	/* point the new link at the larger node */
	np->address = request->address + request->size;
	np->size = (node->address + node->size) -
			(request->address + request->size);
	node->size = node->size - request->size - np->size;

	/* Insert the node in the chain after node */
	np->prev = node;
	np->next = node->next;

	if (node->next != NIL) node->next->prev = np;
	node->next = np;
	return (1);
}

/* allocate room for n bytes, return 8-byte aligned address */
caddr_t
getlink(u_int n)
{
	caddr_t p;
	extern int pagesize;

	if (memlistpage == (caddr_t)0) {
		reset_alloc();
	}

	if (tablep == (caddr_t)0) {

		/*
		 * Took the following 2 lines out of above test for
		 * memlistpage == null so we can initialize table_freespace
		 */

		table_freespace = pagesize - sizeof (struct bsys_mem);
		tablep = memlistpage + sizeof (struct bsys_mem);
		tablep = (caddr_t)roundup((u_int)tablep, 8);
	}

	if (n == 0)
		return ((caddr_t)0);

	n = roundup(n, 8);
	p = (caddr_t)tablep;

	table_freespace -= n;
	tablep += n;
	if (table_freespace <= 0) {
		char buf[80];

		sprintf(buf, "Boot getlink(): no memlist space (need %d)\n", n);
		prom_panic(buf);
	}

	return (p);
}

void
coalesce_list(struct memlist *list)
{
	while (list && list->next) {
		while (list->address + list->size ==
				list->next->address) {

			list->size += list->next->size;
			if (list->next->next) {
				list->next->next->prev = list;
				list->next = list->next->next;
			} else {
				list->next = NIL;
				return;
			}
		}
		list = list->next;
	}
}
