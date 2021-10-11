/*
 * Copyright (c) 1991-1994 Sun Microsystems, Inc.
 */

#ident	"@(#)standalloc.c	1.40	95/09/25 SMI"

#include <sys/types.h>
#include <sys/saio.h>
#include <sys/sysmacros.h>
#include <sys/promif.h>
#include <sys/bootconf.h>

#define	NIL		0

#ifdef DEBUG
static int	resalloc_debug = 1;
#else DEBUG
static int	resalloc_debug = 0;
#endif DEBUG
#define	dprintf	if (resalloc_debug) printf

extern struct memlist	*vfreelistp, *pfreelistp;
extern int 		insert_node(struct memlist **list,
					struct memlist *request);
extern	struct memlist	*get_min_node(struct memlist *l, u_longlong_t lo,
				u_int size);
extern	void		reset_alloc(void);
extern	caddr_t		get_low_vpage(int n);
extern	void		alloc_segment(caddr_t v);
extern	caddr_t		get_hi_ppage(void);
extern	void 		map_child(caddr_t v, caddr_t p);
extern	caddr_t		resalloc(enum RESOURCES type,
				unsigned bytes, caddr_t virthint, int align);
extern	void		print_memlist(struct memlist *av);
extern	caddr_t		top_bootmem;
extern	caddr_t		magic_phys;

caddr_t		memlistpage;
caddr_t		le_page;
caddr_t		ie_page;
caddr_t 	scratchmemp;
extern int	pagesize;

/*
 *  This routine should be called get_a_page().
 *  It allocates from the appropriate entity one or
 *  more pages and maps them in.
 *  Note that there is no resfree() function.
 *  This then assumes that the standalones cannot free
 *  any pages which have been allocated and mapped.
 */

caddr_t
kern_resalloc(caddr_t virthint, u_int size, int align)
{
	if (virthint != 0)
		return (resalloc(RES_CHILDVIRT, size, virthint, align));
	else {
		return (resalloc(RES_BOOTSCRATCH, size, NULL, NULL));
	}
}

caddr_t
resalloc(enum RESOURCES type, unsigned bytes, caddr_t virthint, int align)
{
	caddr_t	vaddr;
	long pmap = 0;

	if (memlistpage == (caddr_t)0)
		reset_alloc();

	if (bytes == 0)
		return ((caddr_t)0);

	/* extend request to fill a page */
	bytes = roundup(bytes, pagesize);

	dprintf("resalloc:  bytes = %x\n", bytes);

	switch (type) {

	/*
	 * even V2 PROMs never bother to indicate whether the
	 * first MAGIC_PHYS is taken or not.  So we do it all here.
	 * Smart PROM or no smart PROM.
	 */
	case RES_BOOTSCRATCH:
		vaddr = get_low_vpage(bytes/pagesize);

		if (resalloc_debug) {
			dprintf("vaddr = %x, paddr = %x\n", vaddr, ptob(pmap));
			print_memlist(vfreelistp);
			print_memlist(pfreelistp);
		}
		return (vaddr);
		/*NOTREACHED*/
		break;

	case RES_CHILDVIRT:
		vaddr = (caddr_t) prom_alloc(virthint, bytes, align);

		if (vaddr == (caddr_t)virthint)
			return (vaddr);
#ifdef sparc
		if (prom_getversion() <= 0) {
			struct memlist a;
			caddr_t	v;
			/*
			 * First we need to find, allocate, and map
			 * the segments corresponding to the
			 * virthint+size.  We also map the
			 * pages within that range.
			 * We don't really care if the pages
			 * were previously mapped or not because
			 * we rely upon the integrity of our own
			 * memlists.
			 */
			for (v = virthint; v < virthint + bytes;
					v += pagesize) {
				caddr_t p;

				/*
				 *  we put the virtual
				 *  addr on the list
				 */
				a.address = (u_int)v;
				a.size = pagesize;
				a.next = a.prev = NIL;
				if (insert_node(&vfreelistp, &a) == 0)
					prom_panic("Cannot fill request.\n");

				/*
				 *  ...and the physical
				 */
				p = get_hi_ppage();
				a.address = (u_int)p;
				a.size = pagesize;
				a.next = a.prev = NIL;
				if (insert_node(&pfreelistp, &a) == 0)
					prom_panic("Cannot fill request.\n");

				/*  Now map them together */
				map_child(v, p);
			}
			if (resalloc_debug) {
				dprintf("virthint = %x\n", virthint);
				print_memlist(vfreelistp);
				print_memlist(pfreelistp);
			}
			return (virthint);
		} else {
#endif
			printf("Alloc of 0x%x bytes at 0x%x refused.\n",
				bytes, virthint);
			return ((caddr_t)0);
#ifdef sparc
		}
#endif
		/*NOTREACHED*/
		break;

	default:
		printf("Bad resurce type\n");
		return ((caddr_t)0);
	}
}

void
reset_alloc()
{
	extern char _end[];

	/* Cannot be called multiple times */
	if (memlistpage != (caddr_t)0)
		return;

	/*
	 *  Due to kernel history and ease of programming, we
	 *  want to keep everything private to /boot BELOW MAGIC_PHYS.
	 *  In this way, the kernel can just snarf it all when
	 *  when it is ready, and not worry about snarfing lists.
	 */
	memlistpage = (caddr_t)roundup((u_int)_end, pagesize);

	/*
	 *  This next is for scratch memory only
	 *  We only need 1 page in memlistpage for now
	 */
	scratchmemp = (caddr_t)(memlistpage + pagesize);
	le_page = (caddr_t)(scratchmemp + pagesize);
	ie_page = (caddr_t)(le_page + pagesize);

	bzero(memlistpage, pagesize);
	bzero(scratchmemp, pagesize);
	dprintf("memlistpage = %x\n", memlistpage);
	dprintf("le_page = %x\n", le_page);
}

/*
 *	This routine will find the next PAGESIZE chunk in the
 *	low MAGIC_PHYS.  It is analogous to valloc(). It is only for boot
 *	scratch memory, because child scratch memory goes up in
 *	the the high memory.  We just need to verify that the
 *	pages are on the list.  The calling routine will actually
 *	remove them.
 */
caddr_t
get_low_vpage(int numpages)
{
	caddr_t v;
#ifdef i386
	extern int	expand_scratch(int size);

	expand_scratch(numpages * pagesize);	/* bootscratch: switch arenas? */
#endif

	v = scratchmemp;

	if (!numpages)
		return (0);

	/* We know the page is mapped because the 1st MAGIC_PHYS is 1:1 */
	scratchmemp += (numpages * pagesize);

	/* keep things from getting out of hand */
	if (scratchmemp >= top_bootmem)
		prom_panic("Boot:  scratch memory overflow.\n");

	return (v);
}

/*
 *  This routine will allocate a phys page from our
 *  list of ones known to be unallocated.  Death and
 *  destruction shall befall those programs which do not
 *  register their memory usage.  They will most likely
 *  obtain double mappings.
 */
caddr_t
get_hi_ppage(void)
{
	struct memlist *node;

	node = NIL;

	if (pfreelistp == NIL)
		prom_panic("No physmemory list.\n");

/*
 *	Since physmem gets crowded below MAGIC_PHYS, we start
 *	searching above that magic spot all the time for
 *	phys pages for a child.
 */
	node = get_min_node(pfreelistp, (u_longlong_t)magic_phys, pagesize);
	if (node == NIL)
		prom_panic("get_hi_ppage():  Cannot find link in range.\n");
	/*
	 * It is best to let the calling routine take
	 * the node off the freelist.
	 */

	return ((caddr_t)MAX(node->address, (int) magic_phys));
}

int
get_progmemory(caddr_t vaddr, u_int size, int align)
{
	u_int n;

	/*
	 * if the vaddr given is not a mult of PAGESIZE,
	 * then we rounddown to a page, but keep the same
	 * ending addr.
	 */
	n = (u_int)vaddr & (pagesize - 1);
	if (n) {
		vaddr -= n;
		size += n;
	}

	dprintf("get_progmem: requesting %x bytes at %x\n", size, vaddr);
	if (resalloc(RES_CHILDVIRT, size, vaddr, align) != vaddr)
		return (-1);
	return (0);
}
