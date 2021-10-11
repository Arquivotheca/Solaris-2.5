/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)sym.c 1.22 94/09/08 SMI"

/*
 * Includes
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/procfs.h>
#include <sys/stat.h>

#include "prbutlp.h"
#include "dbg.h"


/*
 * Typedefs
 */

typedef struct sym_args {
	/* pointers to arrays of names and places to store addrs */
	int			 sa_count;
	char		  **sa_names;
	caddr_t		*sa_addrs;

}			   sym_args_t;

typedef struct sym_callback {
	const char	 *sc_name;
	prb_sym_func_t  sc_func;
	void		   *sc_calldata;
}			   sym_callback_t;

/*
 * Declarations
 */

static prb_status_t
sym_match(char *name,
	caddr_t addr,
	void *sym_entry,
	prb_elf_search_t * search_info_p);

static prb_status_t
sym_matchname(char *name,
	caddr_t addr,
	void *sym_entry,
	prb_elf_search_t * search_info_p);

static prb_status_t
sym_link(char *name,
	caddr_t addr,
	void *sym_entry,
	prb_elf_search_t * search_info_p);


/* ---------------------------------------------------------------- */
/* ----------------------- Public Functions ----------------------- */
/* ---------------------------------------------------------------- */

/*
 * prb_sym_find_in_obj() - determines the virtual address of the supplied
 * symbols in the specified object file.
 */

/*ARGSUSED*/
prb_status_t
prb_sym_find_in_obj(int procfd,
			caddr_t baseaddr,
			int objfd,
			int count,
			const char **symnames,
			caddr_t * symaddrs)
{
	prb_status_t	prbstat = PRB_STATUS_OK;
	sym_args_t	  symargs;
	int			 i;
	prb_elf_search_t search_info;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_sym_find_in_obj: looking for %d symbols\n", count);
#endif

	symargs.sa_count = count;
	symargs.sa_names = (char **) symnames;
	symargs.sa_addrs = symaddrs;

	/* clear the output args in advance */
	for (i = 0; i < count; i++) {
		symaddrs[i] = NULL;
	}

	prbstat = prb_fill_search_info(&search_info);
	if (prbstat) {
		goto Cleanup;
	}
	search_info.object_data = (void *) SHT_DYNSYM;
	search_info.section_func = prb_traverse_dynsym;
	search_info.record_func = sym_match;
	search_info.record_data = &symargs;

	prbstat = search_info.object_func(objfd, baseaddr, &search_info);
	/* its OK if that wasn't an elf object, just go on ... */
	if (prbstat == PRB_STATUS_BADELFOBJ)
		prbstat = PRB_STATUS_OK;
	/* If there was a problem below, report it */
	if (prbstat) {
		goto Cleanup;
	}
	/* make sure we mapped all the symbols */
	for (i = 0; i < count; i++) {
		if (symaddrs[i] == NULL) {
			prbstat = PRB_STATUS_SYMMISSING;
			goto Cleanup;
		}
#ifdef DEBUG
		if (__prb_verbose >= 2)
			(void) fprintf(stderr,
				"prb_sym_find_in_obj: \"%s\" = 0x%x\n",
				symnames[i], (unsigned) symaddrs[i]);
#endif
	}

Cleanup:
	return (prbstat);

}				/* end prb_sym_find_in_obj */


/*
 * prb_sym_find() - determines the virtual address of the supplied symbols
 */

prb_status_t
prb_sym_find(int procfd,
	int count,
	const char **symnames,
	caddr_t * symaddrs)
{
	prb_status_t	prbstat = PRB_STATUS_OK;
	sym_args_t	  symargs;
	int			 i;
	prb_elf_search_t search_info;

#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"prb_sym_find: looking for %d symbols\n", count);
#endif

	symargs.sa_count = count;
	symargs.sa_names = (char **) symnames;
	symargs.sa_addrs = symaddrs;

	/* clear the output args in advance */
	for (i = 0; i < count; i++) {
		symaddrs[i] = NULL;
	}

	prbstat = prb_fill_search_info(&search_info);
	if (prbstat) {
		goto Cleanup2;
	}
	search_info.object_data = (void *) SHT_DYNSYM;
	search_info.section_func = prb_traverse_dynsym;
	search_info.record_func = sym_match;
	search_info.record_data = &symargs;

	prbstat = search_info.process_func(procfd, &search_info);
	/* If there was a problem below, report it */
	if (prbstat) {
		goto Cleanup2;
	}
	/* make sure we mapped all the symbols */
	for (i = 0; i < count; i++) {
		if (symaddrs[i] == NULL) {
			prbstat = PRB_STATUS_SYMMISSING;
			goto Cleanup2;
		}
#ifdef DEBUG
		if (__prb_verbose >= 3)
			(void) fprintf(stderr, "prb_sym_find: \"%s\" = 0x%x\n",
				symnames[i], (unsigned) symaddrs[i]);
#endif
	}

Cleanup2:
	return (prbstat);

}				/* end prb_sym_find */


/*
 * prb_sym_findname() - determines the name of a function, from its address.
 */

prb_status_t
prb_sym_findname(int procfd,
	int count,
	caddr_t * symaddrs,
	char **symnames)
{
	prb_status_t	prbstat = PRB_STATUS_OK;
	sym_args_t	  symargs;
	int			 i;
	prb_elf_search_t search_info;

#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"prb_sym_findname: looking for %d symbols\n", count);
#endif

	symargs.sa_count = count;
	symargs.sa_names = (char **) symnames;
	symargs.sa_addrs = symaddrs;

	/* clear the output args in advance */
	for (i = 0; i < count; i++) {
		symnames[i] = NULL;
	}

	prbstat = prb_fill_search_info(&search_info);
	if (prbstat) {
		goto Cleanup2;
	}
	search_info.object_data = (void *) SHT_DYNSYM;
	search_info.section_func = prb_traverse_dynsym;
	search_info.record_func = sym_matchname;
	search_info.record_data = &symargs;

	prbstat = search_info.process_func(procfd, &search_info);
	/* If there was a problem below, report it */
	if (prbstat) {
		goto Cleanup2;
	}
	/* make sure we mapped all the symbols */
	for (i = 0; i < count; i++) {
		if (symaddrs[i] == NULL) {
			prbstat = PRB_STATUS_SYMMISSING;
			goto Cleanup2;
		}
#ifdef DEBUG
		if (__prb_verbose >= 3)
			(void) fprintf(stderr,
				"prb_sym_findname: \"%s\" = 0x%x\n",
				symnames[i], (unsigned) symaddrs[i]);
#endif
	}

Cleanup2:
	return (prbstat);


}				/* end prb_sym_findname */


/*
 * prb_sym_callback() - calls the supplied function for every symbol found
 */

prb_status_t
prb_sym_callback(int procfd,
	const char *symname,
	prb_sym_func_t symfunc,
	void *calldata)
{
	prb_status_t	prbstat = PRB_STATUS_OK;
	sym_callback_t  symargs;
	prb_elf_search_t search_info;

#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"prb_sym_callback: looking for 1 symbol\n");
#endif

	symargs.sc_name = symname;
	symargs.sc_func = symfunc;
	symargs.sc_calldata = calldata;

	prbstat = prb_fill_search_info(&search_info);
	if (prbstat)
		return (prbstat);

	search_info.object_data = (void *) SHT_DYNSYM;
	search_info.section_func = prb_traverse_dynsym;
	search_info.record_func = sym_link;
	search_info.record_data = &symargs;

	prbstat = search_info.process_func(procfd, &search_info);
	/* If there was a problem below, report it */
	return (prbstat);

}				/* end prb_sym_callback */

/* ---------------------------------------------------------------- */
/* ----------------------- Private Functions ---------------------- */
/* ---------------------------------------------------------------- */


/*
 * sym_match() - function to be called on each symbol in a dynsym section.
 */

static		  prb_status_t
sym_match(char *name,
	caddr_t addr,
	void *sym_entry,
	prb_elf_search_t * search_info_p)
{
	int			 i;
	sym_args_t	 *symargs_p = (sym_args_t *) search_info_p->record_data;
	Elf32_Sym	  *sym = (Elf32_Sym *) sym_entry;


#ifdef VERYVERBOSE
#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "sym_match: checking \"%s\"\n", name);
#endif
#endif

	for (i = 0; i < symargs_p->sa_count; i++) {
		if ((sym->st_shndx != SHN_UNDEF) &&
			(strcmp(name, symargs_p->sa_names[i]) == 0)) {
#ifdef DEBUG
			if (__prb_verbose >= 2)
				(void) fprintf(stderr,
					"		MATCHED \"%s\" "
					"at 0x%08x\n",
					name, (unsigned) addr);
#endif
			symargs_p->sa_addrs[i] = addr;
		}
	}

	return (PRB_STATUS_OK);

}				/* end sym_match */


/*
 * sym_matchname() - function to be called on each symbol in a dynsym
 * section.
 */

static		  prb_status_t
sym_matchname(char *name,
	caddr_t addr,
	void *sym_entry,
	prb_elf_search_t * search_info_p)
{
	int			 i;
	sym_args_t	 *symargs_p = (sym_args_t *) search_info_p->record_data;
	Elf32_Sym	  *sym = (Elf32_Sym *) sym_entry;


#ifdef VERYVERBOSE
#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"sym_matchname: checking \"%s\"\n", name);
#endif
#endif

	for (i = 0; i < symargs_p->sa_count; i++) {
		if ((sym->st_shndx != SHN_UNDEF) &&
			symargs_p->sa_addrs[i] == addr) {
#ifdef DEBUG
			if (__prb_verbose >= 2)
				(void) fprintf(stderr,
					"		MATCHEDNAME \"%s\" "
					"at 0x%08x\n",
					name, (unsigned) addr);
#endif
			symargs_p->sa_names[i] = strdup(name);
		}
	}

	return (PRB_STATUS_OK);

}				/* end sym_matchname */


/*
 * sym_link() - function to be called on each symbol in a dynsym section.
 */

static		  prb_status_t
sym_link(char *name,
	caddr_t addr,
	void *sym_entry,
	prb_elf_search_t * search_info_p)
{
	sym_callback_t *symargs_p =
		(sym_callback_t *) search_info_p->record_data;
	Elf32_Sym	  *sym = (Elf32_Sym *) sym_entry;
	prb_status_t	prbstat;


#ifdef VERYVERBOSE
#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "sym_link: checking \"%s\"\n", name);
#endif
#endif

	if ((sym->st_shndx != SHN_UNDEF) &&
		(strcmp(name, symargs_p->sc_name) == 0)) {
		prbstat = symargs_p->sc_func(name, addr,
			symargs_p->sc_calldata);
		if (prbstat)
			return (prbstat);
	}
	return (PRB_STATUS_OK);

}				/* end sym_link */
