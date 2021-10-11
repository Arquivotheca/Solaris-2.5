/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_MODCTL_H
#define	_SYS_MODCTL_H

#pragma ident	"@(#)modctl.h	1.36	95/09/13 SMI"	/* SunOS-4.1 1.9 */

/*
 * loadable module support.
 */

#include <sys/types.h>
#include <sys/ioccom.h>
#include <sys/nexusdefs.h>
#include <sys/thread.h>
#include <sys/t_lock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The following structure defines the operations used by modctl
 * to load and unload modules.  Each supported loadable module type
 * requires a set of mod_ops.
 */
struct mod_ops {
	int		(*modm_install)();	/* install module in kernel */
	int		(*modm_remove)();	/* remove from kernel */
	int		(*modm_info)();		/* module info */
};

#ifdef _KERNEL

/*
 * The defined set of mod_ops structures for each loadable module type
 * Defined in modctl.c
 */
extern struct mod_ops mod_driverops;
extern struct mod_ops mod_execops;
extern struct mod_ops mod_fsops;
extern struct mod_ops mod_miscops;
extern struct mod_ops mod_schedops;
extern struct mod_ops mod_strmodops;
extern struct mod_ops mod_syscallops;

#endif /* _KERNEL */

/*
 * Definitions for the module specific linkage structures.
 * The first two fields are the same in all of the structures.
 * The linkinfo is for informational purposes only and is returned by
 * modctl with the MODINFO cmd.
 */

/* For drivers */
struct modldrv {
	struct mod_ops		*drv_modops;
	char			*drv_linkinfo;
	struct dev_ops		*drv_dev_ops;
};

/* For system calls */
struct modlsys {
	struct mod_ops		*sys_modops;
	char			*sys_linkinfo;
	struct sysent		*sys_sysent;
};

/* For filesystems */
struct modlfs {
	struct mod_ops		*fs_modops;
	char			*fs_linkinfo;
	struct vfssw		*fs_vfssw;
};

/* For misc */
struct modlmisc {
	struct mod_ops		*misc_modops;
	char			*misc_linkinfo;
};

/* For Streams Modules. */
struct modlstrmod {
	struct mod_ops		*strmod_modops;
	char			*strmod_linkinfo;
	struct fmodsw		*strmod_fmodsw;
};

/* For Scheduling classes */
struct modlsched {
	struct mod_ops		*sched_modops;
	char			*sched_linkinfo;
	struct sclass		*sched_class;
};

/* For Exec file type (like COFF, ...) */
struct modlexec {
	struct mod_ops		*exec_modops;
	char			*exec_linkinfo;
	struct execsw		*exec_execsw;
};

/*
 * Revision number of loadable modules support.  This is the value
 * that must be used in the modlinkage structure.
 */
#define	MODREV_1		1

/*
 * The modlinkage structure is the structure that the module writer
 * provides to the routines to install, remove, and stat a module.
 * The ml_linkage element is an array of pointers to linkage structures.
 * For most modules there is only one linkage structure.  We allocate
 * enough space for 3 linkage structures which happens to be the most
 * we have in any sun supplied module.  For those modules with more
 * than 3 linkage structures (which is very unlikely), a modlinkage
 * structure must be kmem_alloc'd in the module wrapper to be big enough
 * for all of the linkage structures.
 */
struct modlinkage {
	int		ml_rev;		/* rev of loadable modules system */
	void		*ml_linkage[4];	/* NULL terminated list of */
					/* linkage structures */
};

/*
 * commands.  These are the commands supported by the modctl system call.
 */
#define	MODLOAD		0
#define	MODUNLOAD	1
#define	MODINFO		2
#define	MODRESERVED	3
#define	MODCONFIG	4
#define	MODADDMAJBIND	5
#define	MODGETPATH	6
#define	MODREADSYSBIND  7
#define	MODGETMAJBIND	8
#define	MODGETNAME	9

/*
 * Data structure passed to modconfig command in kernel to build devfs tree
 */

struct aliases {
	struct aliases *a_next;
	char *a_name;
	int a_len;
};

struct modconfig {
	char rootdir[256];
	char drvname[256];
	char drvclass[256];
	int major;
	int num_aliases;
	struct aliases *ap;
	int debugflag;
};

/*
 * Max module path length
 */
#define	MOD_MAXPATH	256

/*
 * Default search path for modules ADDITIONAL to the directory
 * where the kernel components we booted from are.
 *
 * Most often, this will be "/platform/{platform}/kernel /kernel /usr/kernel",
 * but we don't wire it down here.
 */
#define	MOD_DEFPATH	"/kernel /usr/kernel"

/*
 * Default file name extension for autoloading modules.
 */
#define	MOD_DEFEXT	""

/*
 * Parameters for modinfo
 */
#define	MODMAXNAMELEN 32		/* max module name length */
#define	MODMAXLINKINFOLEN 32		/* max link info length */

/*
 * Module specific information.
 */
struct modspecific_info {
	char	msi_linkinfo[MODMAXLINKINFOLEN]; /* name in linkage struct */
	int	msi_p0;			/* module specific information */
};

/*
 * Structure returned by modctl with MODINFO command.
 */
#define	MODMAXLINK 10			/* max linkages modinfo can handle */

struct modinfo {
	int		   mi_info;		/* Flags for info wanted */
	int		   mi_state;		/* Flags for module state */
	int		   mi_id;		/* id of this loaded module */
	int		   mi_nextid;		/* id of next module or -1 */
	caddr_t		   mi_base;		/* virtual addr of text */
	u_int		   mi_size;		/* size of module in bytes */
	int		   mi_rev;		/* loadable modules rev */
	int		   mi_loadcnt;		/* # of times loaded */
	char		   mi_name[MODMAXNAMELEN]; /* name of module */
	struct modspecific_info mi_msinfo[MODMAXLINK];
						/* mod specific info */
};

/* Values for mi_info flags */
#define	MI_INFO_ONE	1
#define	MI_INFO_ALL	2
#define	MI_INFO_CNT	4

/* Values for mi_state */
#define	MI_LOADED	1
#define	MI_INSTALLED	2

/*
 * Macros to vector to the appropriate module specific routine.
 */
#define	MODL_INSTALL(MODL, MODLP) \
	(*(MODL)->misc_modops->modm_install)(MODL, MODLP)
#define	MODL_REMOVE(MODL, MODLP) \
	(*(MODL)->misc_modops->modm_remove)(MODL, MODLP)
#define	MODL_INFO(MODL, MODLP, P0) \
	(*(MODL)->misc_modops->modm_info)(MODL, MODLP, P0)

/*
 * Definitions for stubs
 */
struct mod_stub_info {
	unsigned int mods_func_adr;
	struct mod_modinfo *mods_modinfo;
	unsigned int mods_stub_adr;
	int (*mods_errfcn)();
	int mods_weak;
};

struct mod_modinfo {
	char *modm_module_name;
	struct modctl *mp;
	struct mod_stub_info modm_stubs[1];
};

struct modctl_list {
	struct modctl_list *modl_next;
	struct modctl *modl_modp;
};

extern unsigned int stub_install_common();

/*
 * Structure to manage a loadable module.
 */
struct modctl {
	struct modctl *mod_next;
	struct modctl *mod_prev;
	int mod_id;
	void *mod_mp;
	kthread_t *mod_inprogress_thread;
	struct mod_modinfo *mod_modinfo;
	struct modlinkage *mod_linkage;
	char *mod_filename;
	char *mod_modname;
	int mod_busy;
	int mod_stub;			/* currently executing via a stub */
	char mod_loaded;
	char mod_installed;
	char mod_loadflags;
	char mod_want;
	struct modctl_list *mod_requisites; /* Modules this one depends on. */
	struct modctl_list *mod_dependents; /* Modules depending on this one. */
	int mod_loadcnt;
};

/*
 * mod_loadflags
 */

#define	MOD_NOAUTOUNLOAD	0x1
#define	MOD_PACKED		0x2

#ifdef _KERNEL

typedef int modid_t;

/*
 * global function and data declarations
 */
extern kmutex_t mod_unload_lock;
extern kmutex_t mod_lock;
extern kmutex_t instub_lock;

extern char *systemfile;
extern int mod_mix_changed;
extern int moddebug;
extern int in_modprintf;
extern int instubs;

/*
 * this is the head of a doubly linked list.  Only the next and prev
 * pointers are used
 */
extern struct modctl modules;

/*
 * This enum type controls the autounloading of modules.
 */
enum mod_unload_ctrl {
	MOD_AUTOUNLOAD_OFF,	/* Turn autounloading off, please */
	MOD_AUTOUNLOAD_ON,	/* Turn it back on */
	MOD_AUTOUNLOAD_STATUS	/* What is the current status? */
};

extern void	mod_setup(void);
extern int	modload(char *, char *);
extern int	modloadonly(char *, char *);
extern int	modunload(int);
extern int	mod_hold_stub(struct mod_stub_info *);
extern int	mod_install(struct modlinkage *);
extern int	mod_remove(struct modlinkage *);
extern int	mod_info(struct modlinkage *, struct modinfo *);
extern int	modunloadctrl(enum mod_unload_ctrl);
extern int	mod_remove_by_name(char *);
extern int	mod_sysctl(int, void *);
struct sysparam;
extern int	mod_sysctl_type(int, int (*)(struct sysparam *, void *),
    void *);
extern void	mod_read_system_file(int);
extern void	mod_release_stub(struct mod_stub_info *, int);
extern void	mod_askparams(void);
extern void	mod_uninstall_daemon(void);
extern void	modreap(void);
extern int	mod_hold_by_modctl(struct modctl *);
extern void	mod_release_mod(struct modctl *);
extern u_long	modlookup(char *, char *);
extern char	*modgetsymname(unsigned int, unsigned int *);
extern struct modctl *mod_load_requisite(struct modctl *, char *);
extern struct modctl *mod_find_by_filename(char *, char *);
extern unsigned int	modgetsymvalue(char *, int);
extern void	mod_rele_dev_by_major(major_t);
extern struct dev_ops *mod_hold_dev_by_major(major_t);

#include <sys/varargs.h>

extern void modprintf(char *, ...);
extern void modvprintf(char *, va_list);

extern int make_devname(char *, int);
extern void make_syscallname(char *, int);

struct bind;
extern void make_aliases(struct bind **);
extern int read_binding_file(char *, struct bind **);
extern void read_class_file(void);

extern unsigned int install_stub(struct mod_stub_info *);
extern void install_stubs_by_name(struct modctl *, char *);
extern void reset_stubs(struct modctl *);
extern struct modctl *mod_getctl(struct modlinkage *);
extern major_t mod_name_to_major(char *);
extern void init_devnamesp(int);
extern void init_syscallnames(int);

extern char *mod_getsysname(int);
extern int mod_getsysnum(char *);

extern void make_all_nodes(struct modconfig *);
extern int make_one_node(major_t, struct modconfig *);

extern int mod_containing_pc(caddr_t, char *);


/*
 * Only these three are part of the DDI
 */
extern int _init(void);
extern int _fini(void);
extern int _info(struct modinfo *);

#endif	/* _KERNEL */

/*
 * bit definitions for moddebug.
 *
 * Normally if KADB is present we lock down all symbol tables
 * and if KADB is not present we unlock all symbol tables.
 *
 * There are reasons why users may want to override the default.
 * One reason is that if the system is not boot with KADB and
 * keeps crashing for some reason, symbols may not be in
 * the crash dump unless the symbols are locked down.
 * Another reason is that locking down symbols may make the
 * system behave differently than if they are not locked down.
 * So you may want to boot with KADB but not lock down symbols.
 *
 * There are two flags in moddebug to control the locking of symbols.
 * MODDEBUG_LOCKSYMBOLS forces symbols to be locked down regardless
 * of the presence of KADB.  If this flag is not set, then
 * MODDEBUG_UNLOCKSYMBOLS can be set to unlock symbols even when
 * KADB is present.
 */
#define	MODDEBUG_LOADMSG	0x80000000	/* print "[un]loading..." msg */
#define	MODDEBUG_ERRMSG		0x40000000	/* print detailed error msgs */
#define	MODDEBUG_LOADMSG2	0x20000000	/* print 2nd level msgs */
#define	MODDEBUG_NOPACK		0x00002000	/* unloading OK, no packing */
#define	MODDEBUG_NOAUL_DRV	0x00001000	/* no Autounloading Drivers */
#define	MODDEBUG_NOAUL_EXEC	0x00000800	/* no Autounloading Execs */
#define	MODDEBUG_NOAUL_FS	0x00000400	/* no Autounloading File sys */
#define	MODDEBUG_NOAUL_MISC	0x00000200	/* no Autounloading misc */
#define	MODDEBUG_NOAUL_SCHED	0x00000100	/* no Autounloading scheds */
#define	MODDEBUG_NOAUL_STR	0x00000080	/* no Autounloading streams */
#define	MODDEBUG_NOAUL_SYS	0x00000040	/* no Autounloading syscalls */
#define	MODDEBUG_NOSYMS		0x00000020	/* no sorted symbols for kadb */
#define	MODDEBUG_NOAUTOUNLOAD	0x00000010	/* no autounloading at all */
#define	MODDEBUG_UNLOCKSYMBOLS	0x00000008	/* ok to page symbols */
#define	MODDEBUG_LOCKSYMBOLS	0x00000004	/* not ok to page symbols */
#define	MODDEBUG_STUBBPT	0x00000002	/* bpt after stubs installed */
#define	MODDEBUG_USERDEBUG	0x00000001	/* bpt after init_module() */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MODCTL_H */
