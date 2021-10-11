/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)link.c 1.58 94/11/02 SMI"

/*
 * Includes
 */

#ifndef DEBUG
#define	NDEBUG	1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/procfs.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <assert.h>
#include <sys/tnf.h>

#include "prbutlp.h"
#include "dbg.h"
#include "prbk.h"

/*
 * Defines - Project private interfaces
 */

#define	PROBE_SYMBOL	"__tnf_probe_version_1"

/*
 * Typedefs
 */

typedef struct link_args {
	boolean_t	   la_linkem;
	int			 la_procfd;
	char		   *la_probename;
	caddr_t		 la_head_p;
#ifdef MAPTARGMEM
	off_t		   la_offset;
#endif
}			   link_args_t;



/*
 * Globals
 */

prbctlref_t *g_head_p = NULL;
extern boolean_t g_kernelmode;


/*
 * Declarations
 */

static prb_status_t
link_rela(char *name,
	caddr_t addr,
	void *rel_entry,
	prb_elf_search_t * search_info_p);
static prb_status_t
link_register(
	int procfd,
	caddr_t addr,
	tnf_probe_control_t * prbctl_p,
	prb_lmap_entry_t * lmap_p);
static prb_status_t link_destroy(prbctlref_t * prb_p);
static prb_status_t
link_probe(
	int procfd,
	caddr_t addr,
	tnf_probe_control_t * prbctl_p,
	link_args_t * largs_p,
	prb_lmap_entry_t * lmap_p);


/* ---------------------------------------------------------------- */
/* ----------------------- Public Functions ----------------------- */
/* ---------------------------------------------------------------- */

/*
 * prb_link_find() - given a process file descriptor, find and link all of
 * the probes into a global probe list in the process.
 */

prb_status_t
prb_link_find(int procfd)
{
	prb_status_t	prbstat;
	const char	 *symnames[1] = {
	"__tnf_probe_list_head"};
	caddr_t		 symaddrs[1];
	link_args_t	 largs;
	prb_elf_search_t search_info;

#ifdef DEBUG
	if (__prb_verbose)
		(void) fprintf(stderr, "finding probes in target\n");
#endif

	prbstat = prb_sym_find(procfd, 1, symnames, symaddrs);
	if (prbstat)
		return (prbstat);

	largs.la_linkem = B_TRUE;
	largs.la_procfd = procfd;
	largs.la_probename = PROBE_SYMBOL;
	largs.la_head_p = symaddrs[0];
#ifdef MAPTARGMEM
	largs.la_offset = 0;
#endif
	prbstat = prb_fill_search_info(&search_info);
	if (prbstat)
		return (prbstat);

	search_info.mapping_func = prb_traverse_mapping;
	/*
	 * this optimization is not possible w/o revision since intel uses
	 * REL
	 */
#if 0
	search_info.object_data = (void *) SHT_RELA;
#endif
	search_info.section_func = prb_traverse_rela;
	search_info.record_func = link_rela;
	search_info.record_data = &largs;

	prbstat = search_info.process_func(procfd, &search_info);

	/* If there was a problem below, report it */
	if (prbstat)
		return (prbstat);

	return (PRB_STATUS_OK);

}				/* end prb_link_find */


/*
 * prb_link_reset() - frees the current probe list
 */

prb_status_t
prb_link_reset(int procfd)
{
	prb_status_t	prbstat;
	const char	 *symnames[1] = {
	"__tnf_probe_list_head"};
	caddr_t		 symaddrs[1];

	tnf_probe_control_t *h;
	link_args_t	 largs;
	prb_elf_search_t search_info;

	/* tear down the working copies of the probes */
	while (g_head_p) {
		prbctlref_t	*ref_p;

		ref_p = g_head_p;
		g_head_p = ref_p->next;

		/* destroy the copy of the probe, but not the strings */
		(void) link_destroy(ref_p);
	}

	/* unlink the probes in the target object */
	(void) memset(symaddrs, 0, sizeof (symaddrs));
	prbstat = prb_sym_find(procfd, 1, symnames, symaddrs);
	if (prbstat)
		return (prbstat);

	/* write a NULL into the head pointer */
	h = NULL;
	prbstat = prb_proc_write(procfd, symaddrs[0], &h, sizeof (h));

	largs.la_linkem = B_FALSE;
	largs.la_procfd = procfd;
	largs.la_probename = PROBE_SYMBOL;
	largs.la_head_p = symaddrs[0];
#ifdef MAPTARGMEM
	largs.la_offset = 0;
#endif
	prbstat = prb_fill_search_info(&search_info);
	if (prbstat)
		return (prbstat);

	search_info.mapping_func = prb_traverse_mapping;
	/* need RELA for sparc and REL for i386, no longer optimize ... */
#if 0
	search_info.object_data = (void *) SHT_RELA;
#endif
	search_info.section_func = prb_traverse_rela;
	search_info.record_func = link_rela;
	search_info.record_data = &largs;

	prbstat = search_info.process_func(procfd, &search_info);

	/* If there was a problem below, report it */
	if (prbstat)
		return (prbstat);


	return (PRB_STATUS_OK);

}				/* end prb_link_reset */

/*
 * prb_link_disable() - disable the probe list in the target by
 * storing a 0 in the __tnf_probe_list_valid flag. This is done to
 * prevent internal probe control from finding the list until we
 * enable it with prb_link_enable().
 */

static caddr_t  flagaddr = NULL;

prb_status_t
prb_link_disable(int procfd)
{
	prb_status_t	prbstat;
	const char	 *symnames[1] = {
	"__tnf_probe_list_valid"};
	caddr_t		 symaddrs[1];
	int				flag;

	if (!flagaddr) {
		/* find the probe list head */
		prbstat = prb_sym_find(procfd, 1, symnames, symaddrs);
		if (prbstat)
			return (prbstat);

		flagaddr = symaddrs[0];
	}

	/* write a 0 into the flag */
	flag = 0;
	prbstat = prb_proc_write(procfd, flagaddr, &flag, sizeof (flag));

	return (prbstat);

}				/* end prb_link_disable */


prb_status_t
prb_link_enable(int procfd)
{
	prb_status_t	prbstat;
	const char	 *symnames[1] = {
	"__tnf_probe_list_valid"};
	caddr_t		 symaddrs[1];
	int				flag;

	if (!flagaddr) {
		/* find the probe list head */
		prbstat = prb_sym_find(procfd, 1, symnames, symaddrs);
		if (prbstat)
			return (prbstat);

		flagaddr = symaddrs[0];
	}

	/* write a 1 into the flag */
	flag = 1;
	prbstat = prb_proc_write(procfd, flagaddr, &flag, sizeof (flag));

	return (prbstat);

}				/* end prb_link_enable */


/*
 * prb_link_flush() - write all changed probes in the target processes
 * address space.
 */

static prb_status_t
perref(prbctlref_t * ref_p, void *calldata_p)
{
	prb_status_t	prbstat;
	int			 procfd = (int) calldata_p;


	/*
	 * * Check for changes in certain fields.  If there have been changes *
	 * update the fields in the reference copy and write the probe * to
	 * target memory.
	 */
	if (ref_p->refprbctl.alloc_func != ref_p->wrkprbctl.alloc_func ||
		ref_p->refprbctl.probe_func != ref_p->wrkprbctl.probe_func ||
		ref_p->refprbctl.test_func != ref_p->wrkprbctl.test_func ||
		ref_p->refprbctl.commit_func != ref_p->wrkprbctl.commit_func) {
		ref_p->refprbctl.alloc_func = ref_p->wrkprbctl.alloc_func;
		ref_p->refprbctl.probe_func = ref_p->wrkprbctl.probe_func;
		ref_p->refprbctl.test_func = ref_p->wrkprbctl.test_func;
		ref_p->refprbctl.commit_func = ref_p->wrkprbctl.commit_func;

		if (g_kernelmode) {
			prbstat = prbk_flush(ref_p);
		} else {
			prbstat = prb_proc_write(procfd, (caddr_t) ref_p->addr,
			    &ref_p->refprbctl, sizeof (tnf_probe_control_t));
		}

		if (prbstat) {
			DBG((void) fprintf(stderr,
				"prb_link_flush: perref: "
				"prb_proc_write failed: %s\n",
					prb_status_str(prbstat)));
			return (prbstat);
		}
	}
	return (PRB_STATUS_OK);

}				/* end perref */

prb_status_t
prb_link_flush(int procfd)
{
	(void) prb_link_traverse(perref, (void *) procfd);

	return (PRB_STATUS_OK);

}				/* end prb_link_flush */


/*
 * prb_link_traverse() - calls the traversal function on each probe
 */

prb_status_t
prb_link_traverse(prb_traverse_probe_func_t fun_p,
	void *calldata_p)
{
	prbctlref_t	*ref_p;

	for (ref_p = g_head_p; ref_p; ref_p = ref_p->next)
		(*fun_p) (ref_p, calldata_p);

	return (PRB_STATUS_OK);

}				/* end prb_link_traverse */


/* ---------------------------------------------------------------- */
/* ----------------------- Private Functions ---------------------- */
/* ---------------------------------------------------------------- */

/*
 * link_rela() - function called on each relocation record
 */

/*ARGSUSED*/
static		  prb_status_t
link_rela(char *name,
	caddr_t addr,
	void *rel_entry,
	prb_elf_search_t * search_info_p)
{
	link_args_t	*largs_p = (link_args_t *)
	search_info_p->record_data;
	int			 procfd = largs_p->la_procfd;

	/* bail happily on null names */
	if (!name)
		return (PRB_STATUS_OK);

#ifdef DEBUG
	if (__prb_verbose >= 5)
		(void) fprintf(stderr,
			"		link_rela: symbol name \"%s\"\n", name);
#endif

	if (strcmp(name, largs_p->la_probename) == 0) {
		prb_status_t	prbstat;
		tnf_probe_control_t prbctl;

		/* read in the probe control block */
		prbstat = prb_proc_read(procfd, addr, &prbctl,
					sizeof (tnf_probe_control_t));
		if (prbstat) {
			DBG((void) fprintf(stderr,
				"link_rela: prb_proc_read failed: %s\n",
				prb_status_str(prbstat)));
			return (prbstat);
		}
		if (largs_p->la_linkem) {
			/*
			 * * It is important to link the probes before
			 * registering * them.  Otherwise they get unlinked
			 * when the refcopy * gets written out.
			 */

			/* perform linking operations */
			prbstat = link_probe(procfd, addr, &prbctl, largs_p,
				(prb_lmap_entry_t *)
				search_info_p->mapping_data);
			if (prbstat) {
				DBG((void) fprintf(stderr,
					"link_rela: trouble linking "
					"probe: %s\n",
						prb_status_str(prbstat)));
				return (prbstat);
			}
			/* insert the probe on our reference list */
			prbstat = link_register(procfd, addr, &prbctl,
				(prb_lmap_entry_t *)
				search_info_p->mapping_data);
			if (prbstat) {
				DBG((void) fprintf(stderr,
					"link_register: trouble registering "
					"new probe: %s\n",
						prb_status_str(prbstat)));
				return (prbstat);
			}
		} else {
			/* unlink the probes */
			prbctl.next = (struct tnf_probe_control *) - 1;

			prbstat = prb_proc_write(procfd, addr, &prbctl,
				sizeof (tnf_probe_control_t));
			if (prbstat) {
				DBG((void) fprintf(stderr,
					"link_rela: prb_proc_write "
					"failed: %s\n",
						prb_status_str(prbstat)));
				return (prbstat);
			}
		}
	}
	return (PRB_STATUS_OK);

}				/* end link_rela */


/*
 * link_register() - registers the internal copy of a probe
 */

static		  prb_status_t
link_register(int procfd, caddr_t addr, tnf_probe_control_t * prbctl_p,
	prb_lmap_entry_t * lmap_p)
{
	prb_status_t	prbstat;
	prbctlref_t	*ref_p;

	/* ---------------------------------------------------------------- */
	/* ---------------- probe_control transition plan ----------------- */
	/* ---------------------------------------------------------------- */

	/*
	 * Currently the probe macro creates old-style probe sites.  We *
	 * want to advance libprbutl and prex to the new-style before * we
	 * convert the probe macro definition. We convert old-style * to
	 * new-style in this routine.  Once the probe macro is converted, *
	 * this routine can get simpler again. *
	 *
	 * 	tnf_probe_control_t	old-style *  new_probe_control_t
	 * new-style
	 */

	/*
	 * * allocate and construct a prbctlref structure in our * address
	 * space.  The strings get dereferenced so they * can be used
	 * plainly.
	 */
	ref_p = (prbctlref_t *) malloc(sizeof (prbctlref_t));
	if (!ref_p) {
		DBG((void) fprintf(stderr, "link_register: malloc failed\n"));
		return (PRB_STATUS_ALLOCFAIL);
	}
	ref_p->addr = addr;
	ref_p->lmap_p = lmap_p;

	/* copy the probe itself into the reference block */
	(void) memcpy(&ref_p->refprbctl, prbctl_p,
				sizeof (tnf_probe_control_t));
	(void) memcpy(&ref_p->wrkprbctl, prbctl_p,
				sizeof (tnf_probe_control_t));

	/* dereference the attrs (read it into our address space) */
	prbstat = prb_proc_readstr(procfd, (caddr_t) prbctl_p->attrs,
		(char **) &ref_p->wrkprbctl.attrs);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"link_register: prb_proc_readstr (attrs) failed: %s\n",
				prb_status_str(prbstat)));
		return (prbstat);
	}

	/* print the name of the probe if we are verbose */
#ifdef DEBUG
	if (__prb_verbose) {
#define	LINESIZE	60
		char			buffer[LINESIZE];

		(void) strncpy(buffer, ref_p->wrkprbctl.attrs, LINESIZE);
		buffer[LINESIZE - 1] = '\0';
		(void) fprintf(stderr, "%s ...\n", buffer);
	}
#endif

	/* put the working probe on the other list */
	ref_p->next = g_head_p;
	g_head_p = ref_p;

	return (PRB_STATUS_OK);

}				/* end link_register */


/*
 * link_destroy() - tears down an internal probe representation
 */

static		  prb_status_t
link_destroy(prbctlref_t * ref_p)
{
	if (ref_p) {
		if (ref_p->wrkprbctl.attrs)
			free((void *) ref_p->wrkprbctl.attrs);

		free((void *) ref_p);
	}
	return (PRB_STATUS_OK);

}				/* end link_destroy */


/*
 * link_probe() - link the probes together from the global head
 */

/*ARGSUSED*/
static		  prb_status_t
link_probe(int procfd, caddr_t addr, tnf_probe_control_t * prbctl_p,
	link_args_t * largs_p, prb_lmap_entry_t * lmap_p)
{
	prb_status_t	prbstat;
	tnf_probe_control_t *head_p;

#ifdef DEBUG
	if (__prb_verbose)
		(void) fprintf(stderr,
			"%s 0x%08x: ", (lmap_p->isnew) ? "*" : " ", addr);
#endif

	/* make sure the probe is in the global probe list */
	if (prbctl_p->next == (tnf_probe_control_t *) - 1) {
		prb_status_t	prbstat1, prbstat2;

		/* read the value of the head pointer */
		prbstat = prb_proc_read(procfd, (caddr_t) largs_p->la_head_p,
					&head_p, sizeof (head_p));
		if (prbstat) {
			DBG((void) fprintf(stderr,
				"link_probe: prb_proc_read (head) failed: %s\n",
					prb_status_str(prbstat)));
			return (prbstat);
		}
		if (head_p) {
			/* add ourselves to the head of the existing list */
			prbctl_p->next = head_p;
#ifdef DEBUG
			if (__prb_verbose >= 2)
				(void) fprintf(stderr, "ADDED	", addr);
#endif
		} else {
			prbctl_p->next = NULL;
#ifdef DEBUG
			if (__prb_verbose >= 2)
				(void) fprintf(stderr, "STARTED  ");
#endif
		}

		/* set the head pointer to point to the new record */
		/*LINTED pointer cast may result in improper alignment*/
		head_p = (tnf_probe_control_t *) addr;

		/* write the probe control block and head pointer back */
		prbstat1 = prb_proc_write(procfd, (caddr_t) largs_p->la_head_p,
			&head_p, sizeof (head_p));
		prbstat2 = prb_proc_write(procfd, addr, prbctl_p,
			sizeof (tnf_probe_control_t));
		if (prbstat1 || prbstat2) {
			DBG((void) fprintf(stderr,
			"link_probe: prb_proc_write (head|prb) failed: %s\n",
				prb_status_str((prbstat1) ?
					prbstat1 : prbstat2)));
			return ((prbstat1) ? prbstat1 : prbstat2);
		}
	} else {
#if defined(DEBUG) || defined(lint)
		if (__prb_verbose >= 2)
			(void) fprintf(stderr, "REVISITED");
#endif
	}

	return (PRB_STATUS_OK);

}				/* end link_probe */
