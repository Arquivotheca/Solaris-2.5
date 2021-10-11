/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)targmem.c 1.11 94/10/03 SMI"

/*
 * Includes
 */

#ifndef DEBUG
#define	NDEBUG	1
#endif

#include <assert.h>
#include "prbutl.h"
#include <prb_internals.h>
#include "dbg.h"


/*
 * Defines Project Private Interface
 */

#define	MEMSEG_PTR	"__tnf_probe_memseg_p"

/*
 * Globals
 */

static caddr_t  targ_memseg_p = NULL;


/*
 * prb_targmem_alloc() - allocates memory in the target process.
 */

prb_status_t
prb_targmem_alloc(int procfd,
	size_t size,
	caddr_t * addr_p)
{
	prb_status_t	prbstat;
	caddr_t		memseg_p;
	tnf_memseg_t	memseg;

	*addr_p = NULL;

	/* if the address of the memory segment head is missing, find it */
	if (!targ_memseg_p) {
		const char	 *symnames[1] = { MEMSEG_PTR };
		caddr_t		 symaddrs[1];

		prbstat = prb_sym_find(procfd, 1, symnames, symaddrs);
		if (prbstat)
			return (prbstat);

		targ_memseg_p = symaddrs[0];
		assert(targ_memseg_p);
	}
	/* read the address of the memseg block from the target process */
	prbstat = prb_proc_read(procfd, targ_memseg_p,
		&memseg_p, sizeof (tnf_memseg_t *));
	if (prbstat)
		return (prbstat);

	/* read the memseg block from the target process */
	prbstat = prb_proc_read(procfd, memseg_p, &memseg, sizeof (memseg));
	if (prbstat)
		return (prbstat);

	/* if there is memory left, allocate it */
	if ((memseg.min_p + memseg.i_reqsz) <= (memseg.max_p - size)) {
		memseg.max_p -= size;

		prbstat = prb_proc_write(procfd, memseg_p,
			&memseg, sizeof (memseg));
		if (prbstat)
			return (prbstat);

		*addr_p = memseg.max_p;

#ifdef DEBUG
		if (__prb_verbose >= 3)
			(void) fprintf(stderr,
				"prb_targmem_alloc: allocating %d "
				"bytes at 0x%x\n",
				size, (unsigned long) *addr_p);
#endif
		return (PRB_STATUS_OK);
	} else
		return (PRB_STATUS_ALLOCFAIL);

}				/* end prb_targmem_alloc */
