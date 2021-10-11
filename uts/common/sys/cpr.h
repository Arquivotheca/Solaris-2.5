/*
 * Copyright (c) 1993, by Sun Microsystems Inc.
 */

#ifndef _SYS_CPR_H
#define	_SYS_CPR_H

#pragma ident	"@(#)cpr.h	1.41	94/11/22 SMI"

#include <sys/sunddi.h>
#include <sys/promif.h>
#include <sys/vnode.h>
#include <sys/thread.h>
#include <sys/callo.h>
#include <sys/uadmin.h>
#include <sys/debug.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern int	cpr_debug;

#define	errp	prom_printf
#define	DPRINT

/*
 * DEBUG1 displays the main flow of CPR. Use it to identify which sub-module
 *	of CPR causes problems.
 * DEBUG2 displays minor stuff that normally won't matter.
 * DEBUG3 displays some big loops (i.g. cpr dump). It requires much longer
 *	running time if it's on.
 * DEBUG9 displays statistical data for CPR on console (by using printf),
 *	such as num page invalidated, etc.
 */
#define	LEVEL1		0x1
#define	LEVEL2		0x2
#define	LEVEL3		0x4
#define	LEVEL4		0x8
#define	LEVEL5		0x10

#define	DEBUG1(p)	{if (cpr_debug & LEVEL1) p; }
#define	DEBUG2(p)	{if (cpr_debug & LEVEL2) p; }
#define	DEBUG3(p)	{if (cpr_debug & LEVEL3) p; }
#define	DEBUG4(p)	{if (cpr_debug & LEVEL4) p; }
#define	DEBUG9(p)	{if (cpr_debug & LEVEL5) p; }

#define	CPR_MAXCONTIG	8
/*
 * redefinitions of uadmin subcommands for A_FREEZE
 */
#define	AD_CPR_COMPRESS		AD_COMPRESS /* store state file compressed */
#define	AD_CPR_FORCE		AD_FORCE /* force to do AD_CPR_COMPRESS */
#define	AD_CPR_CHECK		AD_CHECK /* test if CPR module is there */
#define	AD_CPR_TURBO		5	/* turbo boot support */
#define	AD_CPR_NOCOMPRESS	6	/* store state file uncompressed */
#define	AD_CPR_TESTNOZ		7	/* test mode, auto-restart uncompress */
#define	AD_CPR_TESTZ		8	/* test mode, auto-restart compress */
#define	AD_CPR_PRINT		9	/* print out stats */
#define	AD_CPR_DEBUG0		100	/* clear debug flag */
#define	AD_CPR_DEBUG1		101	/* display CPR main flow via prom */
#define	AD_CPR_DEBUG2		102	/* misc small/mid size loops */
#define	AD_CPR_DEBUG3		103	/* exhaustive big loops */
#define	AD_CPR_DEBUG4		104	/* debug cprboot */
#define	AD_CPR_DEBUG9		109	/* display stat data on console */

/*
 * cprboot related information and definitions. The statefile names are
 * hardcoded for now.
 */
#define	CPRINFO_GEN		"/.cpr_generic_info"
#define	CPRINFO_TURBO		"/.cpr_turbo_info"
#define	CPRINFO_DFLTBOOT	"/.cpr_defaultboot_info"
#define	CPR_STATE_FILE		"/.CPR"
#define	CPR_TURBO_FILE		"/.CPR_TB"

#define	CPR_PATHMAX	64

#define	CPRBOOT		"cprboot"
/*
 * information for cprboot
 */
struct cprinfo {
	int	ci_magic;		/* magic word for boot level */
	char	ci_path[CPR_PATHMAX];	/* file path for the boot level */
	char	ci_bootfile[OBP_MAXPATHLEN]; /* previous boot-file */
};

/*
 * cprinfo magic words
 */
#define	CPRINFO_GENERIC_MAGIC		'GeNc'
#define	CPRINFO_TURBO_MAGIC		'TrBc'
#define	CPRINFO_DFLTBOOT_MAGIC		'DfLt'


/*
 * CPR FILE FORMAT:
 *
 * 	ELF header: Standard header which must include the following:
 *
 *		ep->e_type = ET_CORE;
 *
 * 	Physical Dump Header: Information about the physical dump:
 *
 *		cpr_dump_desc
 *
 * 	Physical Page Map: Contains one of more bitmaps records, each
 *		records is consisted of a descriptor and data:
 *
 *		cpr_bitmap_desc
 *		(char) bitmap[cpr_bitmap_desc.cbd_size]
 *
 * 	Physical data: Contains one or more physical page records, each
 *		records is consisted of a descriptor and data:
 *
 *		cpr_page_desc
 *		(char) page_data[cpr_page_desc.cpd_offset]
 */

#define	CPR_DUMP_MAGIC		'DuMp'
#define	CPR_BITMAP_MAGIC	'BtMp'
#define	CPR_PAGE_MAGIC		'PaGe'
#define	CPR_TERM_MAGIC		'TeRm'

/*
 * header at the begining of the dump data section
 */
struct cpr_dump_desc {
	u_int	cdd_magic;	/* paranoia check */
	u_int	cdd_bitmaprec;	/* number of bitmap records */
	u_int	cdd_dumppgsize;	/* total # of frames dumped, in pages */
	caddr_t	cdd_rtnpc;	/* thawing kernel pc */
	u_int	cdd_rtnpc_pfn;	/* thawing kernel physical page no. */
	caddr_t	cdd_curthread;	/* thawing current thread ptr */
	caddr_t	cdd_qsavp;    	/* used for longjmp back to old thread */
	int	cdd_debug;	/* turn on debug on cprboot */
};
typedef struct cpr_dump_desc cdd_t;

/*
 * physical memory bitmap descriptor, preceeds the actual bitmap,
 * each bitmap covers a SIM, should correspond exactly with the
 * phys_install memlist in the kernel.
 */
struct cpr_bitmap_desc {
	u_int		cbd_magic;	/* so we can spot it better */
	u_int		cbd_spfn;   	/* starting pfn in memlist segment */
	u_int		cbd_epfn;	/* ending pfn in memlist segment */
	u_int		cbd_size;	/* size of this bitmap, in bytes */
	u_int		cbd_pagedump;	/* number of pages dumped */
	char		*cbd_bitmap;	/* bitmap of this memlist segment */
	char		*cbd_auxmap; 	/* ptr to aux bitmap used during thaw */
	struct cpr_bitmap_desc *cbd_next; /* next bitmap descriptor */
};
typedef struct cpr_bitmap_desc cbd_t;

/*
 * Describes the virtual to physical mapping for the page saved
 * preceeds the page data, the len is important only when we compress.
 * The va is the virtual frame number for the physical page as seen
 * by the kernel, we don't care too much about aliases since this
 * mapping is used only to help kernel get started.
 *
 */
struct cpr_page_desc {
	u_int cpd_magic;	/* so we can spot it better */
	u_int cpd_va;   	/* kern virtual address mapping */
	u_int cpd_pfn;   	/* kern physical address page # */
	u_int cpd_page;		/* number of contiguous frames, in pages */
	u_int cpd_length;	/* data segment size following, in bytes */
	u_int cpd_flag;		/* see below */
};
typedef struct cpr_page_desc cpd_t;

struct cpr_terminator {
	u_int	magic;			/* paranoia check */
	caddr_t	va;			/* virtuall addr of this struct */
	u_int   pfn;			/* physical page no. of this struct */
	u_int 	real_statef_size;	/* in bytes */
	timestruc_t tm_shutdown;	/* time in milisec when shutdown */
	timestruc_t tm_cprboot_start;	/* time when cprboot starts to run */
	timestruc_t tm_cprboot_end;	/* time before jumping to kernel */
};

/*
 * cpd_flag values
 */
#define	CPD_COMPRESS	0x0001	/* set if compressed */

/*
 * definitions for CPR statistics
 */
#define	CPR_E_NAMELEN		64
#define	CPR_E_MAX_EVENTNUM	64

struct cpr_time {
	time_t	mtime;		/* mean time on this event */
	time_t	stime;		/* start time on this event */
	time_t	etime;		/* end time on this event */
	time_t	ltime;		/* time duration of the last event */
};

struct cpr_event {
	struct cpr_event *ce_next;	/* next event in the list */
	long		ce_ntests;	/* num of the events since loaded */
	struct cpr_time	ce_sec;		/* cpr time in sec on this event */
	struct cpr_time	ce_msec;	/* cpr time in 100*millisec */
	char		ce_name[CPR_E_NAMELEN];
};

struct cpr_stat {
	int	cs_ntests;		/* num of cpr's since loaded */
	int	cs_mclustsz;		/* average cluster size: all in bytes */
	int	cs_nosw_pages;		/* # of pages of no backing store */
	int	cs_upage2statef;	/* actual # of upages gone to statef */
	int	cs_grs_statefsz;	/* statefile size without compression */
	int	cs_est_statefsz;	/* estimated statefile size */
	int	cs_real_statefsz;	/* real statefile size */
	int	cs_dumped_statefsz;	/* how much has been dumped out */
	int	cs_min_comprate;	/* minimum compression ratio * 100 */
	struct cpr_event *cs_event_head; /* The 1st one in stat event list */
	struct cpr_event *cs_event_tail; /* The last one in stat event list */
};

/*
 * macros for CPR statistics evaluation
 */
#define	CPR_STAT_EVENT_START(s)		cpr_stat_event_start(s, 0)
#define	CPR_STAT_EVENT_END(s)		cpr_stat_event_end(s, 0)
/*
 * use the following is other time zone is required
 */
#define	CPR_STAT_EVENT_START_TMZ(s, t)	cpr_stat_event_start(s, t)
#define	CPR_STAT_EVENT_END_TMZ(s, t)	cpr_stat_event_end(s, t)

#define	CPR_STAT_EVENT_PRINT		cpr_stat_event_print

/*
 * State Structure for CPR
 */
typedef struct cpr {
	u_int		c_flags;
	int		c_substate;	/* tracking suspend progress */
	int		c_fcn;		/* uadmin subcommand */
	struct vnode	*c_vp;		/* vnode for statefile */
	int		c_cprboot_magic;
	char		c_alloc_cnt;	/* # of statefile alloc retries */
	struct cpr_stat	c_stat;
	kmutex_t	c_dlock;	/* driver lock */
	kcondvar_t	c_holddrv_cv;	/* notify driver threads to suspend */
	caddr_t		c_mapping_area;	/* reserve for dumping kas phys pages */
	cbd_t  		*c_bitmaps_chain;
} cpr_t;

extern cpr_t cpr_state;
#define	CPR	(&cpr_state)
#define	STAT	(&(cpr_state.c_stat))

/*
 * c_flags definitions
 */
#define	C_SUSPENDING		1
#define	C_RESUMING		2
#define	C_COMPRESSING		4
#define	C_ERROR			8

/*
 * definitions for c_substate. It works together w/ c_flags to determine which
 * stages the CPR is at.
 */
#define	C_ST_USER_THREADS	0
#define	C_ST_STATEF_ALLOC	1
#define	C_ST_STATEF_ALLOC_RETRY	2
#define	C_ST_DDI		3
#define	C_ST_DRIVERS		4
#define	C_ST_DUMP		5

#define	cpr_set_substate(a)	(CPR->c_substate = (a))

#define	C_VP		(CPR->c_vp)

#define	C_MAX_ALLOC_RETRY	4

#ifndef _ASM

extern void cpr_signal_user(int sig);
extern void cpr_start_user_threads(void);
extern void cpr_stat_event_start(char *, timestruc_t *);
extern void cpr_stat_event_end(char *, timestruc_t *);
extern void cpr_stat_event_print(void);
extern void cpr_stat_record_events(void);
extern void cpr_stat_cleanup(void);
extern void cpr_restore_mmu(u_int, u_int, caddr_t, caddr_t);
extern void cpr_save_time(void);
extern void cpr_restore_time(void);
extern void cpr_hold_driver(void);  /* callback for stopping drivers */
extern void cpr_void_cprinfo(void);
extern void cpr_spinning_bar(void);
extern void cpr_set_bootinfo(char *, char *);
extern int cpr_main(void);
extern int cpr_stop_user_threads(void);
extern int cpr_suspend_devices(dev_info_t *);
extern int cpr_resume_devices(dev_info_t *);
extern int cpr_dump(vnode_t *);
extern int cpr_validate_cprinfo(char *default_bootfile);
extern int cpr_cprinfo_is_valid(char *file, int magic, struct cprinfo *);
extern int cpr_mp_online(void);
extern int cpr_mp_offline(void);
extern int cpr_change_mp(int);
extern int cpr_write(vnode_t *, caddr_t, int);
extern int pf_is_memory(u_int);
extern uint_t cpr_compress(uchar_t *, uint_t, uchar_t *);
extern timestruc_t cpr_todget(void);

extern timestruc_t wholecycle_tv;

#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CPR_H */
