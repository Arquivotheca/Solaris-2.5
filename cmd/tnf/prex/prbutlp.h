/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _PRBUTLP_H
#define	_PRBUTLP_H

#pragma ident  "@(#)prbutlp.h 1.14 94/10/11 SMI"

/*
 * Includes
 */

#include "prbutl.h"
#include <libelf.h>

/*
 * Typedefs
 */

typedef struct prb_elf_search prb_elf_search_t;


typedef prb_status_t
(*prb_traverse_process_func_t) (int procfd, prb_elf_search_t * search_info);

typedef prb_status_t
(*prb_traverse_mapping_func_t) (int procfd, caddr_t addr, size_t size,
	long mflags, prb_elf_search_t * search_info);

typedef prb_status_t
(*prb_traverse_object_func_t) (int objfd, caddr_t addr,
	prb_elf_search_t * search_info);

typedef prb_status_t
(*prb_traverse_section_func_t) (Elf * elf, char *strs, Elf_Scn * scn,
	Elf32_Shdr * shdr, Elf_Data * data, caddr_t baseaddr,
	prb_elf_search_t * search_info);

typedef prb_status_t
(*prb_record_func_t) (char *name, caddr_t addr, void *entry,
	prb_elf_search_t * search_info);

struct prb_elf_search {
	prb_traverse_process_func_t process_func;
	void					   *process_data;
	prb_traverse_mapping_func_t mapping_func;
	void					   *mapping_data;
	prb_traverse_object_func_t object_func;
	void					   *object_data;
	prb_traverse_section_func_t section_func;
	void					   *section_data;
	prb_record_func_t			record_func;
	void					   *record_data;

};

/*
 * Declarations
 */

prb_status_t	prb_traverse_process(int procfd,
	prb_elf_search_t * search_info_p);

prb_status_t	prb_traverse_mapping(int procfd, caddr_t addr, size_t size,
	long mflags, prb_elf_search_t * search_info_p);

prb_status_t	prb_traverse_mapobj(int procfd, caddr_t addr, size_t size,
	long mflags, prb_elf_search_t * search_info_p);

prb_status_t	prb_traverse_object(int objfd, caddr_t addr,
	prb_elf_search_t * search_info_p);

prb_status_t	prb_traverse_rela(Elf * elf, char *strs, Elf_Scn * rel_scn,
	Elf32_Shdr * rel_shdr, Elf_Data * rel_data,
	caddr_t baseaddr, prb_elf_search_t * search_info_p);

prb_status_t	prb_traverse_dynsym(Elf * elf, char *elfstrs,
	Elf_Scn * scn, Elf32_Shdr * shdr, Elf_Data * data, caddr_t baseaddr,
	prb_elf_search_t * search_info_p);

prb_status_t	prb_fill_search_info(prb_elf_search_t * search_p);
#endif				/* _PRBUTLP_H */
