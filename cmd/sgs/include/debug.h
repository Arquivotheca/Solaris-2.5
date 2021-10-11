/*
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)debug.h	1.28	95/06/20 SMI"

#ifndef		DEBUG_DOT_H
#define		DEBUG_DOT_H

/*
 * Global include file for linker debugging.
 *
 * ld(1) and ld.so carry out all diagnostic debugging calls via dlopen'ing
 * the library liblddbg.so.  Thus debugging is always enabled.  The utility
 * elfdump() is explicitly dependent upon this library.  There are two
 * categories of routines defined in this library:
 *
 *  o	Debugging routines that have specific linker knowledge, and test the
 *	class of debugging allowable before proceeding, start with the `Dbg_'
 *	prefix.
 *  o	Lower level routines that provide generic ELF structure interpretation
 *	start with the `Elf_' prefix.  These latter routines are the only
 *	routines used by the elfdump() utility.
 */

#include	<libelf.h>
#include	"sgs.h"
#include	"machdep.h"

/*
 * Define any interface flags.  These flags direct the debugging routine to
 * generate different diagnostics, thus the strings themselves are maintained
 * in this library.
 */
#define	DBG_SUP_ENVIRON	1
#define	DBG_SUP_CMDLINE	2
#define	DBG_SUP_DEFAULT	3

#define	DBG_SUP_START	1
#define	DBG_SUP_ATEXIT	2
#define	DBG_SUP_FILE	3
#define	DBG_SUP_SECTION	4

/*
 * Define our (latest) library name and setup entry point.
 */
#define	DBG_LIBRARY	"liblddbg.so.3"
#define	DBG_SETUP	"Dbg_setup"

/*
 * Define a user macro to invoke debugging.  The `dbg_mask' variable acts as a
 * suitable flag, and can be set to collect the return value from Dbg_setup().
 */
extern	int		dbg_mask;

#define	DBG_CALL(func)	if (dbg_mask) func

/*
 * Print routine, this must be supplied by the application.
 */
extern	void		dbg_print(const char *, ...);

/*
 * External interface routines.  These are linker specific.
 */
extern	void		Dbg_args_files(int, char *);
extern	void		Dbg_args_flags(int, int);
extern	void		Dbg_bind_global(const char *, caddr_t, caddr_t, int,
				const char *, caddr_t, caddr_t, const char *);
extern	void		Dbg_bind_profile(int, int);
extern	void		Dbg_bind_weak(const char *, caddr_t, caddr_t,
				const char *);
extern	void		Dbg_ent_print(List *, Boolean);
extern	void		Dbg_file_analyze(const char *, int);
extern	void		Dbg_file_aout(const char *, unsigned long,
				unsigned long, unsigned long);
extern	void		Dbg_file_archive(const char *, int);
extern	void		Dbg_file_bound(const char *, const char *, int);
extern	void		Dbg_file_delete(const char *);
extern	void		Dbg_file_dlclose(const char *);
extern	void		Dbg_file_dlopen(const char *, const char *, int);
extern	void		Dbg_file_elf(const char *, unsigned long, unsigned long,
				unsigned long, unsigned long, unsigned long,
				unsigned int);
extern	void		Dbg_file_filter(const char *, const char *);
extern	void		Dbg_file_fixname(const char *, const char *);
extern	void		Dbg_file_generic(Ifl_desc *);
extern	void		Dbg_file_ldso(const char *, unsigned long,
				unsigned long, unsigned long, unsigned long);
extern	void		Dbg_file_needed(const char *, const char *);
extern	void		Dbg_file_nl(void);
extern	void		Dbg_file_output(Ofl_desc *);
extern	void		Dbg_file_preload(const char *);
extern	void		Dbg_file_prot(const char *, int);
extern	void		Dbg_file_ref(const char *, int, Permit *, int);
extern	void		Dbg_file_reuse(const char *, const char *);
extern	void		Dbg_file_skip(const char *, const char *);
extern	void		Dbg_file_unused(const char *);
extern	void		Dbg_libs_ignore(const char *);
extern	void		Dbg_libs_init(List *, List *);
extern	void		Dbg_libs_l(const char *, const char *);
extern	void		Dbg_libs_path(const char *);
extern	void		Dbg_libs_req(Sdf_desc *, const char *);
extern	void		Dbg_libs_update(List *, List *);
extern	void		Dbg_libs_yp(const char *);
extern	void		Dbg_libs_ylu(const char *, const char *, int);
extern	void		Dbg_libs_find(const char *);
extern	void		Dbg_libs_found(const char *);
extern	void		Dbg_libs_dpath(const char *);
extern	void		Dbg_libs_rpath(const char *, const char *);
extern	void		Dbg_map_atsign(Boolean);
extern	void		Dbg_map_dash(const char *, Sdf_desc *);
extern	void		Dbg_map_ent(Boolean, Ent_desc *);
extern	void		Dbg_map_equal(Boolean);
extern	void		Dbg_map_parse(const char *);
extern	void		Dbg_map_pipe(Sg_desc *, const char *, const Word);
extern	void		Dbg_map_seg(Sg_desc *);
extern	void		Dbg_map_size_new(const char *);
extern	void		Dbg_map_size_old(Sym_desc *);
extern	void		Dbg_map_sort_fini(Sg_desc *);
extern	void		Dbg_map_sort_orig(Sg_desc *);
extern	void		Dbg_map_symbol(Sym_desc *);
extern	void		Dbg_map_version(const char *, const char *, int);
extern	void		Dbg_reloc_apply(Word, Word, Os_desc *);
extern	void		Dbg_reloc_in(Rel *, const char *, Is_desc *);
extern	void		Dbg_reloc_out(Rel *, const char *, Os_desc *);
extern	void		Dbg_reloc_proc(Os_desc *, Is_desc *);
extern	void		Dbg_reloc_ars_entry(Rel_desc *);
extern	void		Dbg_reloc_ors_entry(Rel_desc *);
extern	void		Dbg_reloc_doactiverel();
extern	void		Dbg_reloc_doact(Word, Word, Word, const char *,
			    Os_desc *);
extern	void		Dbg_reloc_dooutrel();
extern	void		Dbg_reloc_run(const char *);
extern	void		Dbg_sec_added(Os_desc *, Sg_desc *);
extern	void		Dbg_sec_created(Os_desc *, Sg_desc *);
extern	void		Dbg_sec_in(Is_desc *);
extern	void		Dbg_seg_entry(int, Sg_desc *);
extern	void		Dbg_seg_list(List *);
extern	void		Dbg_seg_os(Os_desc *, int);
extern	void		Dbg_seg_title(void);
extern	void		Dbg_support_action(const char *, const char *, int,
			    const char *);
extern	void		Dbg_support_load(const char *, const char *);
extern	void		Dbg_support_req(const char *, int);
extern	void		Dbg_syms_ar_checking(int, Elf_Arsym *, const char *);
extern	void		Dbg_syms_ar_entry(int, Elf_Arsym *);
extern	void		Dbg_syms_ar_resolve(int, Elf_Arsym *, const char *);
extern	void		Dbg_syms_ar_title(const char *, int);
extern	void		Dbg_syms_created(const char *);
extern	void		Dbg_syms_entered(Sym *, Sym_desc *);
extern	void		Dbg_syms_entry(int, Sym_desc *);
extern	void		Dbg_syms_global(int, const char *);
extern	void		Dbg_syms_new(Sym *, Sym_desc *);
extern	void		Dbg_syms_nl();
extern	void		Dbg_syms_old(Sym_desc *);
extern	void		Dbg_syms_process(Ifl_desc *);
extern	void		Dbg_syms_reduce(Sym_desc *);
extern	void		Dbg_syms_reloc(Sym_desc *, Boolean);
extern	void		Dbg_syms_resolved(Sym_desc *);
extern	void		Dbg_syms_resolving1(int, const char *, int, int);
extern	void		Dbg_syms_resolving2(Sym *, Sym *, Sym_desc *,
				Ifl_desc *);
extern	void		Dbg_syms_sec_entry(int, Sg_desc *, Os_desc *);
extern	void		Dbg_syms_sec_title(void);
extern	void		Dbg_syms_spec_title(void);
extern	void		Dbg_syms_up_title(void);
extern	void		Dbg_syms_updated(Sym_desc *, const char *);
extern	void		Dbg_syms_dlsym(const char *, const char *, int next);
extern	void		Dbg_syms_lookup_aout(const char *);
extern	void		Dbg_syms_lookup(const char *, const char *,
				const char *);
extern	void		Dbg_util_call_fini(const char *);
extern	void		Dbg_util_call_init(const char *);
extern	void		Dbg_util_call_main(const char *);
extern	void		Dbg_util_nl(void);
extern	void		Dbg_util_str(const char *);
extern	void		Dbg_ver_avail_entry(Ver_index *, const char *);
extern	void		Dbg_ver_avail_title(const char *);
extern	void		Dbg_ver_desc_entry(Ver_desc *);
extern	void		Dbg_ver_def_title(const char *);
extern	void		Dbg_ver_need_title(const char *);
extern	void		Dbg_ver_need_entry(int, const char *, const char *);
extern	void		Dbg_ver_need_not(const char *);
extern	void		Dbg_ver_symbol(const char *);

/*
 * External interface routines. These are not linker specific and provide
 * generic routines for interpreting elf structures.
 */
extern	void		Elf_dyn_print(Dyn *, const char *);
extern	void		Elf_elf_data(const char *, Addr, Elf_Data *,
				const char *);
extern	void		Elf_elf_data_title();
extern	void		Elf_elf_header(Ehdr *);
extern	void		Elf_note_entry(long *);
extern	void		Elf_phdr_entry(Phdr *);
extern	void		Elf_reloc_entry(const char *, Rel *, const char *,
				const char *);
extern	void		Elf_shdr_entry(Shdr *);
extern	void		Elf_sym_table_entry(const char *, Sym *, int,
				const char *, const char *);
extern	void		Elf_sym_table_title(const char *, const char *);
extern	void		Elf_ver_def_print(Verdef *, Word, const char *);
extern	void		Elf_ver_need_print(Verneed *, Word, const char *);

#endif
