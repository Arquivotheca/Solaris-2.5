/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)elf.c 1.20 94/10/14 SMI"

/*
 * Includes
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <link.h>
#include <sys/procfs.h>

#include "prbutlp.h"
#include "dbg.h"


/*
 * Declarations
 */

static prb_status_t prb_elf_dynsec_num(int procfd, caddr_t baseaddr,
					int objfd, int *num_dyn);
static prb_status_t elf_dynmatch(Elf *elf, char *strs, Elf_Scn *dyn_scn,
	Elf32_Shdr *dyn_shdr, Elf_Data *dyn_data,
	caddr_t baseaddr, prb_elf_search_t * search_info_p);
static prb_status_t dyn_findtag(
	Elf32_Dyn 	*start,		/* start of dynam table read in */
	Elf32_Sword 	tag, 		/* tag to search for */
	caddr_t 	dynam_addr,	/* address of _DYNAMIC in target */
	int 		limit, 		/* number of entries in table */
	caddr_t 	*dentry_address);	/* return value */


/* ---------------------------------------------------------------- */
/* ----------------------- Public Functions ----------------------- */
/* ---------------------------------------------------------------- */

/*
 * prb_elf_isdyn() - returns PRB_STATUS_OK if executable is a dynamic
 * executable.
 */

prb_status_t
prb_elf_isdyn(int procfd)
{
	int			 num_ent = 0;
	int			 objfd;
	prb_status_t	prbstat;
	caddr_t		 baseaddr = 0;
	prb_elf_search_t search_info;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_elf_isdyn: \n");
#endif

again:
	objfd = ioctl(procfd, PIOCOPENM, 0);
	if (objfd < 0) {
		if (errno == EINTR)
			goto again;
		DBG((void) fprintf(stderr,
			"prb_elf_isdyn: PIOCOPENM failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	prbstat = prb_fill_search_info(&search_info);
	if (prbstat)
		return (prbstat);

	search_info.object_data = (void *) SHT_DYNAMIC;
	search_info.section_func = elf_dynmatch;
	search_info.section_data = &num_ent;

	prbstat = search_info.object_func(objfd, baseaddr, &search_info);

	if (prbstat != PRB_STATUS_OK)
		return (prbstat);

	if (num_ent == 0)
		return (PRB_STATUS_NOTDYNAMIC);

	return (PRB_STATUS_OK);

}				/* end prb_elf_isdyn */


/*
 * prb_elf_finddbg() - this function finds the address of the debug struct in
 * the target process.
 */

prb_status_t
prb_elf_dbgent(int procfd, caddr_t * entaddr_p)
{
	prb_status_t	prbstat;
	int			 objfd;
	int			 num_dynentries = 0;
	const char	 *symnames[1] = {
	"_DYNAMIC"};
	caddr_t		 symaddrs[1];
	caddr_t		 baseaddr = 0;
	caddr_t		 dentry_addr;
	Elf32_Dyn	  *dynam_tab = NULL;
	int			 dynam_tab_size;

	*entaddr_p = NULL;

again1:
	objfd = ioctl(procfd, PIOCOPENM, 0);
	if (objfd < 0) {
		if (errno == EINTR)
			goto again1;
		DBG((void) fprintf(stderr,
			"prb_elf_finddbg: PIOCOPENM failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	/* find the address of the symbol _DYNAMIC */
	prbstat = prb_sym_find_in_obj(procfd, baseaddr, objfd, 1,
		symnames, symaddrs);
	if (prbstat)
		return (PRB_STATUS_NOTDYNAMIC);

	/* find the number of entries in the .dynamic section */
	prbstat = prb_elf_dynsec_num(procfd, baseaddr, objfd, &num_dynentries);
	if (prbstat)
		return (prbstat);

#ifdef DEBUG
	if (__prb_verbose >= 2) {
		(void) fprintf(stderr,
			"prb_elf_finddbg: number of dynentries=%d\n",
			num_dynentries);
		(void) fprintf(stderr,
			"prb_elf_finddbg: _DYNAMIC address=0x%x\n",
			(unsigned) symaddrs[0]);
	}
#endif

	/* read in the dynamic table from the image of the process */
	dynam_tab_size = num_dynentries * sizeof (Elf32_Dyn);
	dynam_tab = malloc(dynam_tab_size);
	if (!dynam_tab) {
		return (PRB_STATUS_ALLOCFAIL);
	}
	prbstat = prb_proc_read(procfd, symaddrs[0], dynam_tab, dynam_tab_size);
	if (prbstat)
		goto Cleanup;

	prbstat = dyn_findtag(dynam_tab, DT_DEBUG, symaddrs[0], dynam_tab_size,
		&dentry_addr);
	if (prbstat) {
		goto Cleanup;
	}
	*entaddr_p = (caddr_t) dentry_addr;

Cleanup:
	if (dynam_tab)
		free(dynam_tab);
	return (prbstat);

}				/* end prb_elf_finddbg */


/* ---------------------------------------------------------------- */
/* ----------------------- Private Functions ---------------------- */
/* ---------------------------------------------------------------- */

/*
 * dyn_findtag() - searches for tag in _DYNAMIC table
 */

static		  prb_status_t
dyn_findtag(Elf32_Dyn * start,	/* start of dynam table read in */
		Elf32_Sword tag,	/* tag to search for */
		caddr_t dynam_addr,	/* base address of _DYNAMIC in target */
		int limit, /* number of entries in table */
		caddr_t * dentry_address)
{				/* return value */
	Elf32_Dyn	  *dp;

	for (dp = start; dp->d_tag != DT_NULL; dp++) {
#ifdef DEBUG
		if (__prb_verbose >= 3)
			(void) fprintf(stderr,
				"dyn_findtag: in loop, tag=%d\n",
				(int) dp->d_tag);
#endif

		if (dp->d_tag == tag) {
			*dentry_address = dynam_addr +
				(dp - start) * sizeof (Elf32_Dyn);
			return (PRB_STATUS_OK);
		}
		if (--limit <= 0) {
			DBG((void) fprintf(stderr,
				"dyn_findtag: exceeded limit of table\n"));
			return (PRB_STATUS_BADDYN);
		}
	}

	DBG((void) fprintf(stderr,
		"dyn_findtag: couldn't find tag, last tag=%d\n",
		(int) dp->d_tag));
	return (PRB_STATUS_BADDYN);
}


/*
 * prb_elf_dynsec_num() - returns the number of dynamic sections in the
 * executable file.
 */

/*ARGSUSED*/
static prb_status_t
prb_elf_dynsec_num(int procfd, caddr_t baseaddr,
	int objfd, int *num_dyn)
{
	int			 num_ent = 0;
	prb_status_t	prbstat;
	prb_elf_search_t search_info;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_elf_dynsec_num: looking for number of entries in "
			".dynamic section\n");
#endif

	prbstat = prb_fill_search_info(&search_info);
	if (prbstat)
		return (prbstat);

	search_info.object_data = (void *) SHT_DYNAMIC;
	search_info.section_func = elf_dynmatch;
	search_info.section_data = &num_ent;

	prbstat = search_info.object_func(objfd, baseaddr, &search_info);
	if (prbstat != PRB_STATUS_OK)
		return (prbstat);

	if (num_ent == 0)
		return (PRB_STATUS_NOTDYNAMIC);

	*num_dyn = num_ent;

	return (PRB_STATUS_OK);

}				/* end prb_elf_dynsec_num */


/*
 * elf_dynmatch() - this function searches for the .dynamic section and
 * returns the number of entries in it.
 */

/*ARGSUSED*/
static		  prb_status_t
elf_dynmatch(Elf * elf,
	char *strs,
	Elf_Scn * dyn_scn,
	Elf32_Shdr * dyn_shdr,
	Elf_Data * dyn_data,
	caddr_t baseaddr,
	prb_elf_search_t * search_info_p)
{
	char		   *scn_name;
	int			*ret = (int *) search_info_p->section_data;

	/* bail if this isn't a .dynamic section */
	scn_name = strs + dyn_shdr->sh_name;
	if (strcmp(scn_name, ".dynamic") != 0)
		return (PRB_STATUS_OK);

	if (dyn_shdr->sh_entsize == 0) {	/* no dynamic section */
		*ret = 0;
	} else {
		*ret = (int) (dyn_shdr->sh_size / dyn_shdr->sh_entsize);
	}
	return (PRB_STATUS_OK);
}
