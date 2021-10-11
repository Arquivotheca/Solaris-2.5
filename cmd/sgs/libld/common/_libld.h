/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)_libld.h	1.39	95/08/01 SMI"

/*
 * Local include file for ld library.
 */
#ifndef	_LIBLD_DOT_H
#define	_LIBLD_DOT_H

#include	"libld.h"
#include	"libconv.h"

#define	DEBUG

/*
 * Types of segment index.
 */
typedef enum {
	LD_PHDR,	LD_INTERP,	LD_TEXT,	LD_DATA,
	LD_DYN,		LD_NOTE,	LD_EXTRA
} Segment_ndx;


/*
 * Structure to manage archive member caching.  Each archive has an archive
 * descriptor (Ar_desc) associated with it.  This contains pointers to the
 * archive symbol table (obtained by elf_getarsyms(3e)) and an auxiliary
 * structure (Ar_uax[]) that parallels this symbol table.  The member element
 * of this auxiliary table indicates whether the archive member associated with
 * the symbol offset has already been extracted (AREXTRACTED) or partially
 * processed (refer process_member()).
 */
typedef struct ar_mem {
	Elf *		am_elf;		/* elf descriptor for this member */
	char *		am_name;	/* members name */
	char *		am_path;	/* path (ie. lib(foo.o)) */
	Sym *		am_syms;	/* start of global symbols */
	char *		am_strs;	/* associated string table start */
	int		am_symn;	/* no. of global symbols */
} Ar_mem;

typedef struct ar_aux {
	Sym_desc *	au_syms;	/* internal symbol descriptor */
	Ar_mem *	au_mem;		/* associated member */
} Ar_aux;

#define	AREXTRACTED	(Ar_mem *)-1

typedef struct ar_desc {
	const char *	ad_name;	/* archive file name */
	Elf *		ad_elf;		/* elf descriptor for the archive */
	Elf_Arsym *	ad_start;	/* archive symbol table start */
	Ar_aux *	ad_aux;		/* auxiliary symbol information */
	int		ad_cnt;		/* no. of command line occurrences */
	dev_t		ad_stdev;	/* device id and inode number for */
	ino_t		ad_stino;	/*	multiple inclusion checks */
} Ar_desc;


/*
 * Structure to manage the update of weak symbols from their associated alias.
 */
typedef	struct wk_desc {
	Sym *		wk_symtab;	/* the .symtab entry */
	Sym *		wk_dynsym;	/* the .dynsym entry */
	Sym_desc *	wk_weak;	/* the original weak symbol */
	Sym_desc *	wk_alias;	/* the real symbol */
} Wk_desc;

/*
 * Structure to manage the support library interfaces.
 */
typedef struct func_list {
	const char *	fl_obj;		/* name of support object */
					/*	function is from */
	void (*		fl_fptr)();	/* function pointer */
} Func_list;

typedef	struct support_list {
	const char *	sup_name;	/* ld_support function name */
	List		sup_funcs;	/* list of support functions */
} Support_list;

/*
 * Structure to manage a sorted output relocation list
 */
typedef struct reloc_list {
	Sym_desc *	rl_key;
	Rel_desc *	rl_rsp;
} Reloc_list;

/*
 * Indexes into the ld_support_funcs[] table.
 */
#define	LD_START	0
#define	LD_ATEXIT	1
#define	LD_FILE		2
#define	LD_SECTION	3


#ifndef	FILENAME_MAX
#define	FILENAME_MAX	BUFSIZ		/* maximum length of a path name */
#endif

#define	REL_AIDESCNO	500		/* size of Active relocation buckets */
#define	REL_OIDESCNO	50		/* size of output relocation buckets */
#define	SYM_IDESCNO	10
#define	SYM_DICTSZ	0x4000

/*
 * The dynamic entrance criteria may differ for different machine architectures
 * so is maintained in the machine directory.
 */
extern const Ent_desc	dyn_ent_desc[];
extern const int	dyn_ent_size;

/*
 * Local strings.
 */
extern const char *	Errmsg_ebgn;
extern const char *	Errmsg_egdt;
extern const char *	Errmsg_egeh;
extern const char *	Errmsg_egsc;
extern const char *	Errmsg_egsh;
extern const char *	Errmsg_endt;
extern const char *	Errmsg_rnas;
extern const char *	Errmsg_urty;
extern const char *	Errmsg_ares;
extern const char *	Errmsg_ufte;

extern const char *	Fini_sym;
extern const char *	Init_sym;
extern const char *	Dyn_sym;
extern const char *	Dyn_usym;
extern const char *	Edata_sym;
extern const char *	Edata_usym;
extern const char *	End_sym;
extern const char *	End_usym;
extern const char *	Etext_sym;
extern const char *	Etext_usym;
extern const char *	Etls_sym;
extern const char *	Etls_usym;
extern const char *	Got_sym;
extern const char *	Got_usym;
extern const char *	Plt_sym;
extern const char *	Plt_usym;

/*
 * Local functions.
 */
extern int		add_actrel(Half, Rel_desc *, Rel *, Ofl_desc *);
extern int		add_outrel(Half, Rel_desc *, Rel *, Ofl_desc *);
extern int		allocate_got(Ofl_desc *);
extern Ar_desc *	ar_setup(const char *, Elf *, int, Ofl_desc *);
extern int		assign_got(Sym_desc *);
extern int		assign_got_ndx(Word, int, Ofl_desc *);
extern void		assign_plt_ndx(Sym_desc *, Ofl_desc *);
extern int		do_activerelocs(Ofl_desc *);
extern void		eprintf(Error, const char *, ...);
extern void		fillin_gotplt1(Ofl_desc *);
extern Addr		fillin_gotplt2(Ofl_desc *);
extern Word		lcm(Word, Word);
extern Listnode *	list_append(List *, const void *);
extern Listnode *	list_where(List *, Word);
extern int		make_bss(Ofl_desc *, size_t, Half);
extern int		make_got(Ofl_desc *);
extern int		make_reloc(Ofl_desc *, Os_desc *);
extern void		perform_outreloc(Rel_desc *, Ofl_desc *);
extern Os_desc *	place_section(Ofl_desc *, Is_desc *, int);
extern int		process_archive(const char *, int, Ar_desc *,
				Ofl_desc *);
extern Ifl_desc *	process_ifl(const char *, const char *, int, Elf *,
				Half, Ofl_desc *);
extern int		reloc_local(Rel_desc *, Rel *, Ofl_desc *);
extern int		reloc_relobj(Boolean, Rel_desc *, Rel *, Ofl_desc *);
extern void		reloc_remain_entry(Rel_desc *, Os_desc *, Ofl_desc *);
extern int		sym_copy(Sym_desc *);
extern int		sym_process(Is_desc *, Ifl_desc *, Ofl_desc *);
extern int		sym_resolve(Sym_desc *, Sym *, Ifl_desc *, Ofl_desc *,
				int);
extern int		sym_spec(Ofl_desc *);
extern int		vers_def_process(Is_desc *, Ifl_desc *, Ofl_desc *);
extern int		vers_need_process(Is_desc *, Ifl_desc *, Ofl_desc *);
extern int		vers_sym_process(Is_desc *, Ifl_desc *);
extern int		vers_check_need(Ofl_desc *);
extern void		vers_promote(Sym_desc *, int, Ifl_desc *, Ofl_desc *);
extern int		vers_verify(Ofl_desc *);

#endif
