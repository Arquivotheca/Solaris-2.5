/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)proc.c 1.32 94/09/08 SMI"

/*
 * Includes
 */

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/fault.h>
#include <sys/procfs.h>

#include "prbutlp.h"
#include "dbg.h"


/*
 * prb_proc_open() - opens the process file system entry for the supplied
 * process.
 */

#define	PROCFORMAT	"/proc/%05d"

prb_status_t
prb_proc_open(pid_t pid, int *fd_p)
{
	char			path[MAXPATHLEN];
	int			 retval;

	(void) sprintf(path, PROCFORMAT, (int) pid);

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_proc_open: opening \"%s\"\n", path);
#endif

	retval = open(path, O_RDWR | O_EXCL);
	if (retval == -1) {
		DBG((void) fprintf(stderr,
			"proc_open: open of \"%s\" failed: %s\n",
			path, strerror(errno)));
		return (prb_status_map(errno));
	}
	*fd_p = retval;
	return (PRB_STATUS_OK);

}				/* end prb_proc_open */


/*
 * prb_proc_stop() - stops the target process
 */

prb_status_t
prb_proc_stop(int procfd)
{
	int			 retval;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_proc_stop: stopping the target process\n");
#endif

again:
	retval = ioctl(procfd, PIOCSTOP, NULL);
	if (retval == -1) {
		if (errno == EINTR)
			goto again;
		DBG((void) fprintf(stderr,
			"prb_proc_stop: PIOCSTOP failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	return (PRB_STATUS_OK);

}				/* end prb_proc_stop */


/*
 * prb_proc_prstop() - runs and stops the process, used to clear a target
 * process out of a system call state.
 */

prb_status_t
prb_proc_prstop(int procfd)
{
	int			 retval;
	prrun_t		 prrun;
	prstatus_t	  prstat;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_proc_prstop: stepping the target process\n");
#endif

	(void) memset((char *) &prrun, 0, sizeof (prrun));
	(void) memset((char *) &prstat, 0, sizeof (prstat));

again1:
	prrun.pr_flags = PRSTOP;
	retval = ioctl(procfd, PIOCRUN, &prrun);
	if (retval == -1) {
		if (errno == EINTR)
			goto again1;
		DBG((void) fprintf(stderr,
			"prb_proc_prstop: PIOCRUN failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
again2:
	retval = ioctl(procfd, PIOCWSTOP, &prstat);
	if (retval == -1) {
		if (errno == EINTR)
			goto again2;
		DBG((void) fprintf(stderr,
			"prb_proc_prstop: PIOCWSTOP failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	/* if there was a signal involved, we might need to try again */
	if (prstat.pr_why != PR_REQUESTED)
		goto again1;

	return (PRB_STATUS_OK);

}				/* end prb_proc_prstop */


/*
 * prb_proc_state() - returns the status pf the process
 */

prb_status_t
prb_proc_state(int procfd, prb_proc_state_t * state_p)
{
	int			 retval;
	prstatus_t	  prstatus;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_proc_state: getting the status\n");
#endif

	(void) memset(&prstatus, 0, sizeof (prstatus));

again:
	retval = ioctl(procfd, PIOCSTATUS, &prstatus);
	if (retval == -1) {
		if (errno == EINTR)
			goto again;
		DBG((void) fprintf(stderr,
			"prb_proc_status: PIOCSTATUS failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	state_p->ps_isbptfault = (prstatus.pr_flags & PR_FAULTED &&
		prstatus.pr_what == FLTBPT);
	state_p->ps_isstopped = ((prstatus.pr_flags & PR_STOPPED) != 0);
	state_p->ps_isinsys = ((prstatus.pr_flags & PR_ASLEEP) != 0);
	state_p->ps_isrequested = ((prstatus.pr_why & PR_REQUESTED) != 0);
	state_p->ps_issysexit = ((prstatus.pr_why & PR_SYSEXIT) != 0);
	state_p->ps_issysentry = ((prstatus.pr_why & PR_SYSENTRY) != 0);
	state_p->ps_syscallnum = prstatus.pr_what;
	return (PRB_STATUS_OK);

}				/* end prb_proc_status */


/*
 * prb_proc_wait() - waits for the target process to stop
 */

prb_status_t
prb_proc_wait(int procfd)
{
	int			 retval;
	prstatus_t	  prstat;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_proc_wait: waiting for the target process"
			" to stop\n");
#endif

	/*
	 * * This one of the places where we do not resubmit the ioctl if *
	 * if it is terminated by an EINTR (interrupted system call). * In
	 * this case, the caller knows best ...
	 */
	(void) memset(&prstat, 0, sizeof (prstat));
	retval = ioctl(procfd, PIOCWSTOP, &prstat);
	if (retval == -1) {
#ifdef DEBUG
		if (errno != EINTR && errno != ENOENT)
			(void) fprintf(stderr,
				"prb_proc_wait: PIOCWSTOP failed: %s\n",
				strerror(errno));

		if (__prb_verbose >= 2)
			(void) fprintf(stderr,
				"prb_proc_wait: pc=0x%x instr=0x%x\n",
				prstat.pr_reg[R_PC], prstat.pr_instr);
#endif

		return (prb_status_map(errno));
	}
#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_proc_wait: pc=0x%x instr=0x%x\n",
			prstat.pr_reg[R_PC], prstat.pr_instr);
#endif

	return (PRB_STATUS_OK);

}				/* end prb_proc_wait */


/*
 * prb_proc_cont() - start the target process
 */

prb_status_t
prb_proc_cont(int procfd)
{
	int			 retval;
	prrun_t		 prrun;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_proc_cont: starting the target process\n");
#endif

	(void) memset((char *) &prrun, 0, sizeof (prrun));

again:
	prrun.pr_flags = PRCFAULT;
	retval = ioctl(procfd, PIOCRUN, &prrun);
	if (retval == -1) {
		if (errno == EINTR)
			goto again;
		DBG((void) fprintf(stderr,
			"prb_proc_cont: PIOCRUN failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	return (PRB_STATUS_OK);

}				/* end prb_proc_cont */


/*
 * prb_proc_istepbpt() - step the target process one instruction
 *
 * CAUTION!!!! - this routine is specialized to only be able to single step over
 * the breakpoint location.
 */

/* need to know the location of the breakpoint */
extern caddr_t  g_bptaddr;

prb_status_t
prb_proc_istepbpt(int procfd)
{
	int			 retval;
	prrun_t		 run;
	fltset_t		faults;
	prstatus_t	  prstat;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_proc_istepbpt\n");
#endif

	(void) memset((char *) &run, 0, sizeof (run));

	/* add trace fault to the list of current traced faults */
again1:
	retval = ioctl(procfd, PIOCGFAULT, &faults);
	if (retval == -1) {
		if (errno == EINTR)
			goto again1;
		DBG((void) fprintf(stderr,
			"prb_proc_istepbpt: PIOCGFAULT failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	praddset(&faults, FLTTRACE);

	/* issue the run command with the single-step option */
	run.pr_flags = PRCFAULT | PRSFAULT | PRSTEP;
	run.pr_fault = faults;

	/* load the location of the breakpoint */
	run.pr_vaddr = g_bptaddr;
	run.pr_flags |= PRSVADDR;

again2:
	retval = ioctl(procfd, PIOCRUN, &run);
	if (retval == -1) {
		if (errno == EINTR)
			goto again2;
		DBG((void) fprintf(stderr,
			"prb_proc_istepbpt: PIOCRUN failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
again3:
	retval = ioctl(procfd, PIOCWSTOP, &prstat);
	if (retval == -1) {
		if (errno == EINTR)
			goto again3;
#ifdef DEBUG
		if (errno != EINTR)
			(void) fprintf(stderr,
				"prb_proc_istepbpt: PIOCWSTOP failed: %s\n",
				strerror(errno));
#endif
		return (prb_status_map(errno));
	}
#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_proc_istepbpt: pc=0x%x instr=0x%x\n",
			prstat.pr_reg[R_PC], prstat.pr_instr);
#endif


	/* clear any current faults */
again4:
	retval = ioctl(procfd, PIOCCFAULT, NULL);
	if (retval == -1) {
		if (errno == EINTR)
			goto again4;
		DBG((void) fprintf(stderr,
			"prb_proc_clrbptflt: PIOCCFAULT failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	/* remove the trace fault from the current traced faults */
	prdelset(&faults, FLTTRACE);
again5:
	retval = ioctl(procfd, PIOCSFAULT, &faults);
	if (retval == -1) {
		if (errno == EINTR)
			goto again5;
		DBG((void) fprintf(stderr,
			"prb_proc_istepbpt: PIOCSFAULT failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	return (PRB_STATUS_OK);

}				/* end prb_proc_istepbpt */


/*
 * prb_proc_clrbptflt() - clear an encountered breakpoint fault
 */

prb_status_t
prb_proc_clrbptflt(int procfd)
{
	int			 retval;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_proc_clrbptflt\n");
#endif

	/* clear any current faults */
again:
	retval = ioctl(procfd, PIOCCFAULT, NULL);
	if (retval == -1) {
		if (errno == EINTR)
			goto again;
		DBG((void) fprintf(stderr,
			"prb_proc_clrbptflt: PIOCCFAULT failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	return (PRB_STATUS_OK);

}				/* end prb_proc_clrbptflt */


/*
 * prb_proc_tracebpt() - sets the bpt tracing state.
 */

prb_status_t
prb_proc_tracebpt(int procfd,
	boolean_t bpt)
{
	int			 retval;
	fltset_t		faults;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_proc_tracebpt: %s\n",
			(bpt) ? "enabled" : "disabled");
#endif

	/* get the current set of traced faults */
again1:
	retval = ioctl(procfd, PIOCGFAULT, &faults);
	if (retval == -1) {
		if (errno == EINTR)
			goto again1;
		DBG((void) fprintf(stderr,
			"prb_proc_tracebpt: PIOCGFAULT failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	/* set or clear the breakpoint flag */
	if (bpt)
		praddset(&faults, FLTBPT);
	else
		prdelset(&faults, FLTBPT);

	/* write the fault set back */
again2:
	retval = ioctl(procfd, PIOCSFAULT, &faults);
	if (retval == -1) {
		if (errno == EINTR)
			goto again2;
		DBG((void) fprintf(stderr,
			"prb_proc_tracebpt: PIOCSFAULT failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	return (PRB_STATUS_OK);

}				/* end prb_proc_tracebpt */


/*
 * prb_proc_setrlc() - sets or clears the run-on-last-close flag.
 */

prb_status_t
prb_proc_setrlc(int procfd, boolean_t rlc)
{
	int			 mode;
	int			 retval;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_proc_setrlc: %s run-on-last-close\n",
			(rlc) ? "setting" : "clearing");
#endif

	mode = PR_RLC;

	if (rlc) {
again1:
		retval = ioctl(procfd, PIOCSET, &mode);
		if (retval == -1) {
			if (errno == EINTR)
				goto again1;
			DBG((void) fprintf(stderr,
				"prb_proc_setrlc: PIOCSET failed: %s\n",
				strerror(errno)));
			return (prb_status_map(errno));
		}
	} else {
again2:
		retval = ioctl(procfd, PIOCRESET, &mode);
		if (retval == -1) {
			if (errno == EINTR)
				goto again2;
			DBG((void) fprintf(stderr,
				"prb_proc_setrlc: PIOCRESET failed: %s\n",
				strerror(errno)));
			return (prb_status_map(errno));
		}
	}

	return (PRB_STATUS_OK);


}				/* end prb_proc_setrlc */


/*
 * prb_proc_setklc() - sets or clears the kill-on-last-close flag.
 */

prb_status_t
prb_proc_setklc(int procfd, boolean_t klc)
{
	int			 mode;
	int			 retval;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_proc_setklc: %s kill-on-last-close\n",
			(klc) ? "setting" : "clearing");
#endif

	mode = PR_KLC;

	if (klc) {
again1:
		retval = ioctl(procfd, PIOCSET, &mode);
		if (retval == -1) {
			if (errno == EINTR)
				goto again1;
			DBG((void) fprintf(stderr,
				"prb_proc_setklc: PIOCSET failed: %s\n",
				strerror(errno)));
			return (prb_status_map(errno));
		}
	} else {
again2:
		retval = ioctl(procfd, PIOCRESET, &mode);
		if (retval == -1) {
			if (errno == EINTR)
				goto again2;
			DBG((void) fprintf(stderr,
				"prb_proc_setklc: PIOCRESET failed: %s\n",
				strerror(errno)));
			return (prb_status_map(errno));
		}
	}

	return (PRB_STATUS_OK);

}				/* end prb_proc_setklc */


/*
 * prb_proc_mappings() - returns arrays of information about the mappings in
 * a process.
 */

prb_status_t
prb_proc_mappings(int procfd,
	int *nmaps_p,
	caddr_t ** addrs_pp,
	size_t ** sizes_pp,
	long **mflags_pp)
{
	int			 retval;
	int			 nmap = 0;
	size_t		  size;
	prmap_t		*prmap_p;
	int			 i;

	/* preassert some nothingness */
	*nmaps_p = 0;
	*addrs_pp = NULL;
	*sizes_pp = NULL;
	*mflags_pp = NULL;

	/* fetch the number of mappings */
again1:
	retval = ioctl(procfd, PIOCNMAP, &nmap);
	if (retval == -1) {
		if (errno == EINTR)
			goto again1;
		DBG((void) fprintf(stderr,
			"prb_proc_mappings: PIOCNMAP failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"prb_proc_mappings: found %d mappings\n", nmap);
#endif

	/* allocate enough memory for the mapping table */
	prmap_p = NULL;
	size = sizeof (prmap_t) * (nmap + 1);
	prmap_p = (prmap_t *) malloc(size);
	if (!prmap_p) {
		DBG((void) fprintf(stderr,
			"prb_proc_mappings: malloc of %d bytes failed\n",
			size));
		return (PRB_STATUS_ALLOCFAIL);
	}
	/* read the mapping table from /proc */
again2:
	retval = ioctl(procfd, PIOCMAP, prmap_p);
	if (retval == -1) {
		if (errno == EINTR)
			goto again2;
		DBG((void) fprintf(stderr,
			"prb_proc_mappings: PIOCMAP failed: %s\n",
			strerror(errno)));
		if (prmap_p)
			free(prmap_p);
		return (prb_status_map(errno));
	}
	*addrs_pp = (caddr_t *) calloc(1, sizeof (caddr_t) * nmap);
	*sizes_pp = (size_t *) calloc(1, sizeof (size_t) * nmap);
	*mflags_pp = (long *) calloc(1, sizeof (long) * nmap);
	if (!addrs_pp || !sizes_pp || !mflags_pp) {
		if (*addrs_pp)
			free(*addrs_pp);
		if (*sizes_pp)
			free(*sizes_pp);
		if (*mflags_pp)
			free(*mflags_pp);
		if (prmap_p)
			free(prmap_p);
		return (PRB_STATUS_ALLOCFAIL);
	}
	for (i = 0; i < nmap; i++) {
		(*addrs_pp)[i] = prmap_p[i].pr_vaddr;
		(*sizes_pp)[i] = (size_t) prmap_p[i].pr_size;
		(*mflags_pp)[i] = prmap_p[i].pr_mflags;
	}
	*nmaps_p = nmap;

	if (prmap_p) free(prmap_p);

	return (PRB_STATUS_OK);

}				/* end prb_proc_mappings */


/*
 * prb_proc_exit() - if op is PRB_SYS_ALL, sets up the target process to stop
 * on exit from all system calls.  If op is PRB_SYS_NONE, sets up the target
 * process so that it will not stop on exit from any system call.
 * PRB_SYS_ADD and PRB_SYS_DEL adds or deletes a particular system call from
 * the mask of "interested" system calls respectively. This function can be
 * called multiple times to build up the mask.
 */

prb_status_t
prb_proc_exit(int procfd,
	long syscall,
	prb_syscall_op_t op)
{
	int			 retval;
	sysset_t		sysmask;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_proc_exit: setting up the target process to "
			"stop on exit of syscall\n");
#endif

	switch (op) {
	case PRB_SYS_ALL:
		prfillset(&sysmask);
		break;
	case PRB_SYS_NONE:
		premptyset(&sysmask);
		break;
	case PRB_SYS_ADD:
again1:
		retval = ioctl(procfd, PIOCGEXIT, &sysmask);
		if (retval == -1) {
			if (errno == EINTR)
				goto again1;
			DBG((void) fprintf(stderr,
				"prb_proc_exit: PIOCGEXIT failed: %s\n",
				strerror(errno)));
			return (prb_status_map(errno));
		}
		praddset(&sysmask, syscall);
		break;
	case PRB_SYS_DEL:
again2:
		retval = ioctl(procfd, PIOCGEXIT, &sysmask);
		if (retval == -1) {
			if (errno == EINTR)
				goto again2;
			DBG((void) fprintf(stderr,
				"prb_proc_exit: PIOCGEXIT failed: %s\n",
				strerror(errno)));
			return (prb_status_map(errno));
		}
		prdelset(&sysmask, syscall);
		break;
	default:
		DBG((void) fprintf(stderr, "prb_proc_exit: bad input arg\n"));
		return (PRB_STATUS_BADARG);
	}
again3:
	retval = ioctl(procfd, PIOCSEXIT, &sysmask);
	if (retval == -1) {
		if (errno == EINTR)
			goto again3;
		DBG((void) fprintf(stderr,
			"prb_proc_exit: PIOCSEXIT failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	return (PRB_STATUS_OK);

}				/* end prb_proc_exit */

/*
 * prb_proc_entry() - if op is PRB_SYS_ALL, sets up the target process to
 * stop on entry from all system calls.  If op is PRB_SYS_NONE, sets up the
 * target process so that it will not stop on entry from any system call.
 * PRB_SYS_ADD and PRB_SYS_DEL adds or deletes a particular system call from
 * the mask of "interested" system calls respectively. This function can be
 * called multiple times to build up the mask.
 */

prb_status_t
prb_proc_entry(int procfd,
	long syscall,
	prb_syscall_op_t op)
{
	int			 retval;
	sysset_t		sysmask;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr,
			"prb_proc_entry: setting up the target process to "
			"stop on entry of syscall\n");
#endif

	switch (op) {
	case PRB_SYS_ALL:
		prfillset(&sysmask);
		break;
	case PRB_SYS_NONE:
		premptyset(&sysmask);
		break;
	case PRB_SYS_ADD:
again1:
		retval = ioctl(procfd, PIOCGENTRY, &sysmask);
		if (retval == -1) {
			if (errno == EINTR)
				goto again1;
			DBG((void) fprintf(stderr,
				"prb_proc_entry: PIOCGENTRY failed: %s\n",
				strerror(errno)));
			return (prb_status_map(errno));
		}
		praddset(&sysmask, syscall);
		break;
	case PRB_SYS_DEL:
again2:
		retval = ioctl(procfd, PIOCGENTRY, &sysmask);
		if (retval == -1) {
			if (errno == EINTR)
				goto again2;
			DBG((void) fprintf(stderr,
				"prb_proc_entry: PIOCGENTRY failed: %s\n",
				strerror(errno)));
			return (prb_status_map(errno));
		}
		prdelset(&sysmask, syscall);
		break;
	default:
		DBG((void) fprintf(stderr, "prb_proc_entry: bad input arg\n"));
		return (PRB_STATUS_BADARG);
	}
again3:
	retval = ioctl(procfd, PIOCSENTRY, &sysmask);
	if (retval == -1) {
		if (errno == EINTR)
			goto again3;
		DBG((void) fprintf(stderr,
			"prb_proc_entry: PIOCSENTRY failed: %s\n",
			strerror(errno)));
		return (prb_status_map(errno));
	}
	return (PRB_STATUS_OK);

}				/* end prb_proc_entry */

/*
 * prb_proc_read() - reads a block of memory from a processes address space.
 */

prb_status_t
prb_proc_read(int procfd, caddr_t addr, void *buf, size_t size)
{
	ssize_t		 sz;
	off_t		   offset;

#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"prb_proc_read: reading %d bytes from 0x%x\n",
			size, (unsigned long) addr);
#endif

	offset = lseek(procfd, (off_t) addr, SEEK_SET);
	if (offset != (off_t) addr) {
		DBG(perror("prb_proc_read: lseek failed"));
		return (prb_status_map(errno));
	}
	sz = read(procfd, buf, size);
	if (sz < 0) {
		DBG(perror("prb_proc_read: read failed"));
		return (prb_status_map(errno));
	}
	return (PRB_STATUS_OK);

}				/* prb_proc_read */


/*
 * prb_proc_write() - writes a block of memory from a processes address
 * space.
 */

prb_status_t
prb_proc_write(int procfd, caddr_t addr, void *buf, size_t size)
{
	ssize_t		 sz;
	off_t		   offset;

#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"prb_proc_write: writing %d bytes to 0x%x\n",
			size, (unsigned long) addr);
#endif

	offset = lseek(procfd, (off_t) addr, SEEK_SET);
	if (offset != (off_t) addr) {
		DBG(perror("prb_proc_write: lseek failed"));
		return (prb_status_map(errno));
	}
	sz = write(procfd, buf, size);
	if (sz < 0) {
		DBG(perror("prb_proc_write: write failed"));
		return (prb_status_map(errno));
	}
	return (PRB_STATUS_OK);

}				/* prb_proc_write */


/*
 * prb_proc_readstr() - dereferences a string in the target
 */

#define	BUFSZ	256

prb_status_t
prb_proc_readstr(int procfd, caddr_t addr, char **outstr_pp)
{
	prb_status_t	prbstat;
	int				bufsz = BUFSZ;
	char			buffer[BUFSZ + 1];
	offset_t		offset;
	char		   *ptr;

	*outstr_pp = NULL;
	buffer[BUFSZ] = '\0';
	offset = 0;

	/* allocate an inital return buffer */
	ptr = (char *) malloc(BUFSZ);
	if (!ptr) {
		DBG((void) fprintf(stderr,
			"prb_proc_readstr: malloc failed\n"));
		return (PRB_STATUS_ALLOCFAIL);
	}
	/*LINTED constant in conditional context*/
	while (1) {
		int			 i;

		top_o_the_loop:

		/* read a chunk into our buffer */
		prbstat = prb_proc_read(procfd, addr + offset, buffer, bufsz);
		if (prbstat) {

			/*
			 * if we get into trouble with a large read, try again
			 * with a single byte.  Subsequent failiure is real ...
			 */
			if (bufsz > 1) {
				bufsz = 1;
				goto top_o_the_loop;
			}

			DBG((void) fprintf(stderr,
				"prb_proc_readstr: prb_proc_read failed: %s\n",
				prb_status_str(prbstat)));
			return (prbstat);
		}
		/* copy the chracters into the return buffer */
		for (i = 0; i < bufsz; i++) {
			char			c = buffer[i];

			ptr[offset + i] = c;
			if (c == '\0') {
				/* hooray! we saw the end of the string */
				*outstr_pp = ptr;
				return (PRB_STATUS_OK);
			}
		}

		/* bummer, need to grab another bufsz characters */
		offset += bufsz;
		ptr = (char *) realloc(ptr, offset + bufsz);
		if (!ptr) {
			DBG((void) fprintf(stderr,
				"prb_proc_readstr: realloc failed\n"));
			return (PRB_STATUS_ALLOCFAIL);
		}
	}

#if defined(lint)
	return (PRB_STATUS_OK);
#endif

}				/* end prb_proc_readstr */
