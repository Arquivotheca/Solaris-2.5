/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _PRBUTL_H
#define	_PRBUTL_H

#pragma ident  "@(#)prbutl.h 1.54 94/10/11 SMI"

/*
 * Includes
 */

#include <sys/types.h>
#include <sys/syscall.h>

#include <tnf/probe.h>


/*
 * Defines
 */

#define	TRACE_END		"tnf_trace_end"


/*
 * Typedefs
 */

typedef enum prb_status {
	/* successful status */
	PRB_STATUS_OK = 0,	/* success			 */

	/* errors */
	/*
	 * * Status values in the range 1 to -1023 are reserved for * mapping
	 * standard errno values.
	 */
	PRB_STATUS_MINERRNO = 1,	/* minimum errno value */
	PRB_STATUS_MAXERRNO = 1023,	/* maximum errno value */

	PRB_STATUS_NOPRBOBJ = 1024,	/* can't find libprobe object */
	PRB_STATUS_BADELFVERS = 1025,	/* bad elf version */
	PRB_STATUS_BADELFOBJ = 1026,	/* bad elf object */
	PRB_STATUS_ALLOCFAIL = 1027,	/* memory allocation failed */
	PRB_STATUS_SYMMISSING = 1028,	/* couldn't find symbol */
	PRB_STATUS_BADARG = 1029,	/* bad input argument */
	PRB_STATUS_MMAPFAIL = 1030,	/* mmap (or munmap) failed */
	PRB_STATUS_BADDYN = 1031,	/* corrupted .dynamic section */
	PRB_STATUS_NOTDYNAMIC = 1032,	/* not a dynamic executable */
	PRB_STATUS_BADSYNC = 1033,	/* couldn't sync with rtld */
	PRB_STATUS_NIY = 1034,	/* not implemented yet */
	PRB_STATUS_BADLMAPSTATE = 1035,	/* inconsistent link map */
	PRB_STATUS_NOTINLMAP = 1036	/* address not in link map */

} prb_status_t;

typedef enum prb_syscall_op {
	PRB_SYS_ALL,		/* turn on all system calls	 */
	PRB_SYS_NONE,		/* clears all system calls	 */
	PRB_SYS_ADD,		/* add a system call		 */
	PRB_SYS_DEL		/* delete a system call		 */


}			   prb_syscall_op_t;

typedef enum prb_comb_op {
	PRB_COMB_CHAIN = 0,	/* call the down, then the next */
	PRB_COMB_COUNT = 1	/* how many? */


} prb_comb_op_t;

typedef struct prb_proc_state {
	boolean_t	   ps_isstopped;
	boolean_t	   ps_isinsys;
	boolean_t	   ps_isrequested;
	boolean_t	   ps_issysexit;
	boolean_t	   ps_issysentry;
	boolean_t	   ps_isbptfault;
	long			ps_syscallnum;

}			   prb_proc_state_t;

typedef struct prb_lmap_entry {
	caddr_t		 addr;
	char		   *name;
	boolean_t	   isnew;

}			   prb_lmap_entry_t;

typedef struct prbctlref prbctlref_t;
struct prbctlref {
	caddr_t			addr;
	prb_lmap_entry_t 	*lmap_p;
	int			prbk_probe_id;	/* used in kernel mode only */
	tnf_probe_control_t 	refprbctl;
	tnf_probe_control_t 	wrkprbctl;
	prbctlref_t 		*next;
};

typedef prb_status_t
(*prb_traverse_probe_func_t) (prbctlref_t * ref_p, void *calldata_p);

typedef prb_status_t
(*prb_sym_func_t) (char *name, caddr_t addr, void *calldata);

/*
 * Declarations
 */

prb_status_t	prb_status_map(int errno);
const char	 *prb_status_str(prb_status_t prbstat);

prb_status_t	prb_shmem_init(void);
prb_status_t	prb_shmem_set(void);
prb_status_t	prb_shmem_wait(void);
prb_status_t	prb_shmem_clear(void);

prb_status_t	prb_elf_isdyn(int procfd);
prb_status_t	prb_elf_dbgent(int procfd, caddr_t * entaddr_p);

prb_status_t	prb_lmap_update(int procfd);
prb_status_t	prb_lmap_find(int procfd, caddr_t addr,
	prb_lmap_entry_t ** entry_pp);
prb_status_t	prb_lmap_mark(int procfd);

prb_status_t	prb_rtld_setup(int procfd);
prb_status_t	prb_rtld_wait(int procfd);
prb_status_t	prb_rtld_stalk(int procfd);
prb_status_t	prb_rtld_unstalk(int procfd);
prb_status_t	prb_rtld_advance(int procfd);

prb_status_t	prb_proc_open(pid_t pid, int *procfd_p);
prb_status_t	prb_proc_stop(int procfd);
prb_status_t	prb_proc_prstop(int procfd);
prb_status_t	prb_proc_wait(int procfd);
prb_status_t	prb_proc_cont(int procfd);
prb_status_t	prb_proc_state(int procfd, prb_proc_state_t * state_p);
prb_status_t	prb_proc_setrlc(int procfd, boolean_t rlc);
prb_status_t	prb_proc_setklc(int procfd, boolean_t klc);
prb_status_t	prb_proc_exit(int procfd, long syscall, prb_syscall_op_t op);
prb_status_t	prb_proc_entry(int procfd, long syscall, prb_syscall_op_t op);
prb_status_t	prb_proc_read(int procfd, caddr_t addr,
	void *buf, size_t size);
prb_status_t	prb_proc_write(int procfd, caddr_t addr,
	void *buf, size_t size);
prb_status_t	prb_proc_mappings(int procfd, int *nmaps_p,
	caddr_t ** addrs_pp, size_t ** sizes_pp, long **mflags_pp);
prb_status_t	prb_proc_tracebpt(int procfd, boolean_t bpt);
prb_status_t	prb_proc_istepbpt(int procfd);
prb_status_t	prb_proc_clrbptflt(int procfd);
prb_status_t	prb_proc_readstr(int procfd, caddr_t addr, char **outstr_pp);

prb_status_t	prb_comb_build(int procfd, prb_comb_op_t op,
	caddr_t down, caddr_t next, caddr_t * comb_p);
prb_status_t	prb_comb_decode(int procfd, caddr_t addr, char **name_pp);

#if 0
prb_status_t	prb_pipe_setup(pid_t pid);
prb_status_t	prb_pipe_wait(void);
prb_status_t	prb_pipe_sync(void);
prb_status_t	prb_pipe_cleanup(void);
#endif

prb_status_t	prb_link_find(int procfd);
prb_status_t	prb_link_reset(int procfd);
prb_status_t	prb_link_flush(int procfd);
prb_status_t	prb_link_traverse(prb_traverse_probe_func_t func_p,
	void *calldata_p);
prb_status_t	prb_link_enable(int procfd);
prb_status_t	prb_link_disable(int procfd);

prb_status_t	prb_sym_find_in_obj(int procfd, caddr_t baseaddr,
	int objfd, int count, const char **symnames, caddr_t * symaddrs);
prb_status_t	prb_sym_find(int procfd, int count,
	const char **symnames, caddr_t * symaddrs);
prb_status_t	prb_sym_findname(int procfd, int count,
	caddr_t * symaddrs, char **symnames);
prb_status_t	prb_sym_callback(int procfd, const char *symname,
	prb_sym_func_t symfunc, void *calldata);

prb_status_t	prb_child_create(char *cmdname, char **cmdargs,
	char *preload, pid_t * pid_p);

void			prb_verbose_set(int verbose);

prb_status_t	prb_targmem_alloc(int procfd, size_t size, caddr_t * addr_p);

#endif				/* _PRBUTL_H */
