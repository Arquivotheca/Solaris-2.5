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
#ifndef	_RTLD_DOT_H
#define	_RTLD_DOT_H

#pragma ident	"@(#)_rtld.h	1.41	95/07/31 SMI"


/*
 * Common header for run-time linker.
 */
#include	<link.h>
#include	<sys/types.h>
#include	<stdarg.h>
#include	<synch.h>
#include	<signal.h>
#include	<errno.h>
#include	<unistd.h>
#include	"sgs.h"
#include	"machdep.h"
#include	"dllib.h"

#define		DEBUG

/*
 * Types of directory search rules.
 */
#define	ENVDIRS 1
#define	RUNDIRS 2
#define	DEFAULT 3

/*
 * Linked list of directory names in a search path.
 */
typedef struct pnode {
	const char *	p_name;
	int		p_len;
	struct pnode *	p_next;
} Pnode;

/*
 * Runtime linker private data maintained for each shared object.  Maps are
 * connected to link map lists for `main' and possibly `rtld'.
 */
typedef struct rt_map	Rt_map;
typedef struct lm_list	Lm_list;

struct lm_list {
	Rt_map *	lm_head;
	Rt_map *	lm_tail;
};

struct rt_map {
	Link_map	rt_public;	/* public data */
	List		rt_alias;	/* list of linked file names */
	void (*		rt_init)();	/* address of _init */
	void (*		rt_fini)();	/* address of _fini */
	char *		rt_runpath;	/* LD_RUN_PATH and its equivalent */
	Pnode *		rt_runlist;	/*	Pnode structures */
	long		rt_count;	/* reference count */
	List 		rt_bound;	/* libraries this one is bound to */
	Dl_obj *	rt_dlp;		/* pointer to a dlopened object */
	Permit *	rt_permit;	/* ids permitted to access this lm */
	unsigned long	rt_msize;	/* total memory mapped */
	unsigned long	rt_etext;	/* etext address */
	struct fct *	rt_fct;		/* file class table for this object */
	struct rt_map *	rt_reflm;	/* filters reference link map */
	char *		rt_refname;	/* filters reference name */
	Sym *(*		rt_symintp)();	/* link map symbol interpreter */
	void *		rt_priv;	/* object specific private data area */
	Lm_list * 	rt_list;	/* link map list we belong to */
	unsigned long	rt_flags;	/* state flags, see FLG below */
	dev_t		rt_stdev;	/* device id and inode number for .so */
	ino_t		rt_stino;	/*	multiple inclusion checks */
};

/*
 * Link map state flags.
 */
#define	FLG_RT_ISMAIN	0x0001		/* object represents main executable */
#define	FLG_RT_ANALYZED	0x0002		/* object has been analyzed */
#define	FLG_RT_NODELETE	0x0004		/* object must never be deleted */
#define	FLG_RT_PROMISC	0x0008		/* promiscuous module, anyone can */
					/*	look for symbols in this guy */
#define	FLG_RT_OBJECT	0x0010		/* object processing (ie. .o's) */
#define	FLG_RT_BOUND	0x0020		/* bound to indicator */
#define	FLG_RT_DELETING	0x0040		/* deletion in progress */
#define	FLG_RT_PROFILE	0x0080		/* image is being profiled */
#define	FLG_RT_ALLOC	0x0100		/* image is allocated (not mmap'ed) */
#define	FLG_RT_INITDONE	0x0200		/* objects .init has be called */
#define	FLG_RT_DEBUGGER	0x0400		/* a debugger is monitoring us */
#define	FLG_RT_AUX	0x0800		/* filter is an auxiliary filter */
#define	FLG_RT_FLTALLOC	0x1000		/* alloc'd occurred for filter name */


/*
 * Data structure for file class specific functions and data.
 */
typedef struct fct {
	int (*		fct_are_u_this)();	/* determine type of object */
	unsigned long (*fct_entry_pt)();	/* get entry point */
	Rt_map * (*	fct_map_so)();		/* map in a shared object */
	Rt_map * (*	fct_new_lm)();		/* make a new link map */
	int (*		fct_unmap_so)();	/* unmap a shared object */
	int (*		fct_ld_needed)();	/* determine needed objects */
	Sym * (*	fct_lookup_sym)();	/* initialize symbol lookup */
	Sym * (*	fct_find_sym)();	/* find symbol in load map */
	int (*		fct_reloc)();		/* relocate shared object */
	int *		fct_search_rules;	/* search path rules */
	Pnode * 	fct_dflt_dirs;		/* list of default dirs to */
						/*	search */
	Pnode * 	fct_secure_dirs;	/* list of secure dirs to */
						/*	search (set[ug]id) */
	const char * (*	fct_fix_name)();	/* prepend ./ to pathname */
						/*	without a slash */
	char * (*	fct_get_so)();		/* get shared object */
	void (*		fct_dladdr)();		/* get symbolic address */
	Sym * (*	fct_dlsym)();		/* process dlsym request */
} Fct;

/*
 * Macros for getting to link_map data.
 */
#define	ADDR(X)			((X)->rt_public.l_addr)
#define	NAME(X)			((X)->rt_public.l_name)
#define	DYN(X)			((X)->rt_public.l_ld)
#define	NEXT(X)			((X)->rt_public.l_next)
#define	PREV(X)			((X)->rt_public.l_prev)

/*
 * Macros for getting to linker private data.
 */
#define	ALIAS(X)		((X)->rt_alias)
#define	INIT(X)			((X)->rt_init)
#define	FINI(X)			((X)->rt_fini)
#define	RPATH(X)		((X)->rt_runpath)
#define	RLIST(X)		((X)->rt_runlist)
#define	COUNT(X)		((X)->rt_count)
#define	BOUNDTO(X)		((X)->rt_bound)
#define	BTTAIL(X)		((X)->rt_bttail)
#define	DLP(X)			((X)->rt_dlp)
#define	PERMIT(X)		((X)->rt_permit)
#define	MSIZE(X)		((X)->rt_msize)
#define	ETEXT(X)		((X)->rt_etext)
#define	FCT(X)			((X)->rt_fct)
#define	REFLM(X)		((X)->rt_reflm)
#define	REFNAME(X)		((X)->rt_refname)
#define	SYMINTP(X)		((X)->rt_symintp)
#define	FLAGS(X)		((X)->rt_flags)
#define	LIST(X)			((X)->rt_list)

/*
 * Macros for getting to the file class table.
 */
#define	LM_ENTRY_PT(X)		((X)->rt_fct->fct_entry_pt)
#define	LM_UNMAP_SO(X)		((X)->rt_fct->fct_unmap_so)
#define	LM_NEW_LM(X)		((X)->rt_fct->fct_new_lm)
#define	LM_LD_NEEDED(X)		((X)->rt_fct->fct_ld_needed)
#define	LM_LOOKUP_SYM(X)	((X)->rt_fct->fct_lookup_sym)
#define	LM_FIND_SYM(X)		((X)->rt_fct->fct_find_sym)
#define	LM_RELOC(X)		((X)->rt_fct->fct_reloc)
#define	LM_SEARCH_RULES(X)	((X)->rt_fct->fct_search_rules)
#define	LM_DFLT_DIRS(X)		((X)->rt_fct->fct_dflt_dirs)
#define	LM_SECURE_DIRS(X)	((X)->rt_fct->fct_secure_dirs)
#define	LM_FIX_NAME(X)		((X)->rt_fct->fct_fix_name)
#define	LM_GET_SO(X)		((X)->rt_fct->fct_get_so)
#define	LM_DLADDR(X)		((X)->rt_fct->fct_dladdr)
#define	LM_DLSYM(X)		((X)->rt_fct->fct_dlsym)

/*
 * Size of buffer for building error messages.
 */
#define	ERRSIZE		1024

/*
 * Data structure to hold interpreter information.
 */
typedef struct interp {
	char *		i_name;		/* interpreter name */
	caddr_t		i_faddr;	/* address interpreter is mapped at */
} Interp;

/*
 * Data structure used to keep track of special R1_COPY relocations.
 */
typedef struct rel_copy	Rel_copy;

struct rel_copy	{
	void *		r_to;		/* copy to address */
	void *		r_from;		/* copy from address */
	unsigned long	r_size;		/* copy size bytes */
	Rel_copy *	r_next;		/* next on list */
};

/*
 * Data structure to hold initial file mapping information.  Used to
 * communicate during initial object mapping and provide for error recovery.
 */
typedef struct fil_map {
	int		fm_fd;		/* File descriptor */
	char *		fm_maddr;	/* Address of initial mapping */
	size_t		fm_msize;	/* Size of initial mapping */
	short		fm_mflags;	/* mmaping flags */
	size_t		fm_fsize;	/* Actual file size */
	unsigned long	fm_etext;	/* End of text segment */
	dev_t		fm_stdev;	/* device id and inode number for .so */
	ino_t		fm_stino;	/*	multiple inclusion checks */
} Fmap;

/*
 * /dev/zero file descriptor availability flag.
 */
#define	DZ_UNAVAIL	-1

/*
 * Flags for lookup_sym (and hence find_sym) routines.
 */
#define	LKUP_DEFT	0x0		/* Simple lookup request */
#define	LKUP_SPEC	0x1		/* Special ELF lookup (allows address */
					/*	resolutions to plt[] entries. */
#define	LKUP_LDOT	0x2		/* Indicates the original A_OUT */
					/*	symbol had a leading `.'. */
#define	LKUP_FIRST	0x4		/* Lookup symbol in first link map */
					/*	only */


/*
 * status flags for rtld_flags
 */
#define	RT_FL_THREADS	0x00000001	/* Are threads enabled */
#define	RT_FL_SEARCH	0x00000002	/* tracing search paths */
#define	RT_FL_WARN	0x00000004	/* print warnings for undefines? */
#define	RT_FL_VERBOSE	0x00000008	/* verbose (versioning) tracing */
#define	RT_FL_NOBIND	0x00000010	/* carry out plt binding? */
#define	RT_FL_NOVERSION	0x00000020	/* disable version checking? */
#define	RT_FL_SECURE	0x00000040	/* setuid/segid flag */
#define	RT_FL_APPLIC	0x00000080	/* have we started the application? */

/*
 * Data symbols.
 */
extern rwlock_t		bindlock;	/* readers/writers binding lock */
extern rwlock_t		malloclock;	/* readers/writers malloc lock */
extern rwlock_t		printlock;	/* readers/writers print lock */
extern rwlock_t		boundlock;	/* readers/writers BOUNDTO lock */
extern mutex_t *	profilelock;	/* mutex lock for profiling */
extern int		lc_version;
					/* current version of libthread int. */

/*
 * binding flags for the bindguard routines
 */
#define	THR_FLG_BIND	0x00000001	/* BINDING bindguard flag */
#define	THR_FLG_MALLOC	0x00000002	/* MALLOC bindguard flag */
#define	THR_FLG_PRINT	0x00000004	/* PRINT bindguard flag */
#define	THR_FLG_BOUND	0x00000008	/* BOUNDTO bindguard flag */
#define	THR_FLG_MASK	THR_FLG_BIND | THR_FLG_MALLOC | \
			THR_FLG_PRINT | THR_FLG_BOUND
					/* mask for all THR_FLG flags */

extern Lm_list		lml_main;	/* the `main's link map list */
extern Lm_list		lml_rtld;	/* rtld's link map list */
extern Lm_list		lml_free;	/* free link map list */

extern unsigned long	flags;		/* machine specific file flags */
extern List		preload;	/* preloadable file list */
extern const char *	pr_name;	/* file name of executing process */
extern struct r_debug	r_debug;	/* debugging information */
extern Rel_copy *	copies;		/* head of copy relocations list */
extern char *		lasterr;	/* string describing last error */
extern Interp *		interp;		/* ELF executable interpreter info */
extern Fmap *		fmap;		/* Initial file mapping info */
extern const char *	rt_name;	/* name of the dynamic linker */
extern int		bind_mode;	/* object binding mode (RTLD_LAZY?) */
extern const char *	envdirs;	/* env variable LD_LIBRARY_PATH */
extern Pnode *		envlist;	/*	and its associated Pnode list */
extern int		tracing;	/* tracing loaded objects? */
extern size_t		syspagsz;	/* system page size */
extern char *		platform; 	/* platform name */
extern int		rtld_flags;	/* status flags for RTLD */

extern Fct		elf_fct;	/* ELF file class dependent data */
extern Fct		aout_fct;	/* a.out (4.x) file class dependent */
					/*	data */
extern const char *	ldso_path;
extern const char *	ldso_name;

#ifdef	DEBUG
extern const char *	dbg_file;	/* debugging directed to a file */
#endif

/*
 * Function declarations.
 */
extern void		addfree(void *, size_t);
extern int		analyze_so(Lm_list *, Rt_map *, int, Dl_obj *);
extern Fct *		are_u_this(const char *);
extern int		bind_guard(int);
extern int		bind_clear(int);
extern int		bound_add(Rt_map *, Rt_map *);
extern void		bound_delete(Rt_map *);
extern void		call_fini(void);
extern void		call_init(Rt_map *);
extern int		caller();
extern void *		calloc(size_t, size_t);
extern int		dbg_setup(const char *);
extern char *		dlerror(void);
extern Dl_obj *		dl_old_so(Rt_map *, Permit *, int);
extern Dl_obj *		dl_new_so(const char *, Rt_map *, Rt_map **, Permit *,
				int);
extern void		dlp_delete(Dl_obj *);
extern int		dlp_listadd(Rt_map *, Dl_obj *);
extern int		doprf(const char *, va_list, char *);
extern void		dz_init(int);
extern int		dz_open();
extern void		dz_close();
extern unsigned long	elf_hash(const char *);
extern void		eprintf(Error, const char *, ...);
extern Fmap *		fm_init();
extern void		fm_cleanup(Fmap *);
extern Pnode *		get_next_dir(Pnode **, Rt_map *);
extern Rt_map *		is_so_loaded(Lm_list *, const char *);
extern Listnode *	list_append(List *, const void *);
extern void		lm_append(Lm_list *, Rt_map *);
extern Rt_map *		load_so(Lm_list *, const char *, Rt_map *);
extern Sym *		lookup_sym(const char *, Permit *, Rt_map *,
				Rt_map *, Rt_map **, int);
extern void *		malloc(size_t);
extern void		perm_free(Permit *);
extern Permit *		perm_get();
extern int		perm_test(Permit *, Permit *);
extern Permit *		perm_set(Permit *, Permit *);
extern Permit *		perm_unset(Permit *, Permit *);
extern void		r_debug_state(void);
extern int		readenv(const char **, int);
extern int		relocate_so(Rt_map *, int);
extern void		remove_so(Lm_list *);
extern int		rt_atfork(void (*)(void), void (*)(void),
				void (*)(void));
extern int		rt_mutex_lock(mutex_t *, sigset_t *);
extern int		rt_mutex_unlock(mutex_t *, sigset_t *);
extern void		security(uid_t, uid_t, gid_t, gid_t);
extern int		setup(Rt_map *);
extern int		so_find(const char *, Rt_map *, const char **);
extern const char *	so_gen_path(Pnode *, char *, Rt_map *);
extern int		do_platform_token(char *, char **);
extern int		__write(int, char *, int);
extern void		zero(caddr_t, int);

/*
 * Global error messages
 */
extern const char *	Errmsg_cmdz;		/* can't map /dev/zero */
extern const char *	Errmsg_cmfl;		/* can't map file */
extern const char *	Errmsg_cmsg;		/* can't map segment */
extern const char *	Errmsg_cofl;		/* can't open file */
extern const char *	Errmsg_cotf;		/* corrupt or truncated file */
extern const char *	Errmsg_csps;		/* can't set segment prot. */
extern const char *	Errmsg_rupp;		/* reloc: unable to proc plt */
extern const char *	Errmsg_rupr;		/* 	unidentified ref */
extern const char *	Errmsg_rbeo;		/* 	bad entry offset */
extern const char *	Errmsg_rirt;		/* reloc: invalid reloc type */
extern const char *	Errmsg_rsnf;		/* reloc: sym not found */
extern const char *	Errmsg_rvob;		/* reloc: values overflows */
extern const char *	Errmsg_unft;		/* unknown file type */

extern const char *	Intmsg_relo;		/* internal relocation error */

/*
 * Global ldd generated messages
 */
extern const char *	Lddmsg_fndl;		/* find library */
extern const char *	Lddmsg_lflp;		/* library full path */
extern const char *	Lddmsg_lequ;		/* library equivalent to */
extern const char *	Lddmsg_rsnf;		/* reloc symbol not found */

#endif
