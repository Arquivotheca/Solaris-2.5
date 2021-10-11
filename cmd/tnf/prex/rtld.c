/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)rtld.c 1.26 94/08/25 SMI"

/*
 * Includes
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/procfs.h>
#include <link.h>

#include "prbutlp.h"
#include "dbg.h"


/*
 * Globals
 */

caddr_t		 g_bptaddr = 0;


/*
 * Declarations
 */

static prb_status_t bpt(int procfd, caddr_t addr);
static prb_status_t unbpt(int procfd, caddr_t addr);


/* ---------------------------------------------------------------- */
/* ----------------------- Public Functions ----------------------- */
/* ---------------------------------------------------------------- */


/*
 * prb_rtld_stalk() - setup for a breakpoint when rtld has opened or closed a
 * shared object.
 */

prb_status_t
prb_rtld_stalk(int procfd)
{
	prb_status_t	prbstat = PRB_STATUS_OK;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_rtld_stalk:\n");
#endif

	if (!g_bptaddr) {
		caddr_t		 dentaddr;
		Elf32_Dyn	   dentry;
		struct r_debug  r_dbg;

		prbstat = prb_elf_dbgent(procfd, &dentaddr);
		if (prbstat)
			return (prbstat);

		prbstat = prb_proc_read(procfd, dentaddr,
			&dentry, sizeof (dentry));
		if (prbstat || !dentry.d_un.d_ptr) {
#ifdef DEBUG
			(void) fprintf(stderr,
				"prb_rtld_stalk: error in d_un.d_ptr\n");
#endif
			return (prbstat);
		}
		/* read in the debug struct that it points to */
		prbstat = prb_proc_read(procfd, (caddr_t) dentry.d_un.d_ptr,
			&r_dbg, sizeof (r_dbg));
		if (prbstat)
			return (prbstat);

		g_bptaddr = (caddr_t) r_dbg.r_brk;
	}
	/* plant a breakpoint trap in the pointed to function */
	prbstat = bpt(procfd, g_bptaddr);
	if (prbstat)
		return (prbstat);

	/* setup process to stop when breakpoint encountered */
	prbstat = prb_proc_tracebpt(procfd, B_TRUE);

	return (prbstat);

}				/* end prb_rtld_stalk */


/*
 * prb_rtld_unstalk() - remove rtld breakpoint
 */

prb_status_t
prb_rtld_unstalk(int procfd)
{
	prb_status_t	prbstat;

	/* turn off BPT tracing while out of the water ... */
	prbstat = prb_proc_tracebpt(procfd, B_FALSE);

	prbstat = unbpt(procfd, g_bptaddr);

	return (prbstat);

}				/* end prb_rtld_unstalk */


/*
 * prb_rtld_advance() - we've hit a brekpoint, replace the original
 * instruction, istep, put the breakpoint back ...
 */

prb_status_t
prb_rtld_advance(int procfd)
{
	prb_status_t	prbstat;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_rtld_advance:\n");
#endif

	prbstat = unbpt(procfd, g_bptaddr);
	if (prbstat)
		return (prbstat);

	prbstat = prb_proc_istepbpt(procfd);
	if (prbstat)
		return (prbstat);

	prbstat = bpt(procfd, g_bptaddr);
	if (prbstat)
		return (prbstat);

	return (PRB_STATUS_OK);

}				/* end prb_rtld_advance */


/*
 * prb_rtld_setup() - turns on the flag in the rtld structure so that rtld
 * executes a getpid() stystem call after it done mapping all shared objects
 * but before it executes and init code.
 */

prb_status_t
prb_rtld_setup(int procfd)
{
	prb_status_t	prbstat = PRB_STATUS_OK;
	caddr_t		 dentaddr;
	Elf32_Dyn	   dentry;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_rtld_setup:\n");
#endif

	prbstat = prb_elf_dbgent(procfd, &dentaddr);
	if (prbstat)
		return (prbstat);

	prbstat = prb_proc_read(procfd, dentaddr,
		&dentry, sizeof (dentry));
	if (prbstat) {
#ifdef DEBUG
		(void) fprintf(stderr, "prb_rtld_setup: error in d_un.d_ptr\n");
#endif
		return (prbstat);
	}
	/* modify it  - i.e. request rtld to do getpid() */
	dentry.d_un.d_ptr = 1;
	prbstat = prb_proc_write(procfd, dentaddr,
		&dentry, sizeof (dentry));

	return (prbstat);

}				/* end prb_rtld_setup */


/*
 * prb_rtld_wait() - waits on target to execute getpid()
 */

prb_status_t
prb_rtld_wait(int procfd)
{
	prb_proc_state_t pstate;
	prb_status_t	prbstat;

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "prb_rtld_wait:\n");
#endif
	/* stop on exit of getpid() */
	prbstat = prb_proc_exit(procfd, SYS_getpid, PRB_SYS_ADD);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"prb_rtld_wait: couldn't set up child to stop on "
			"exit of getpid(): %s\n", prb_status_str(prbstat)));
		return (prbstat);
	}
	/* stop on entry of exit() - i.e. exec failed */
	prbstat = prb_proc_entry(procfd, SYS_exit, PRB_SYS_ADD);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"prb_rtld_wait: couldn't set up child to stop on "
			"entry of exit(): %s\n", prb_status_str(prbstat)));
		return (prbstat);
	}
	/* continue target and wait for it to stop */
	prbstat = prb_proc_cont(procfd);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"prb_rtld_wait: couldn't continue target process: %s\n",
				prb_status_str(prbstat)));
		return (prbstat);
	}
	/* wait for target to stop */
	prbstat = prb_proc_wait(procfd);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"prb_rtld_wait: couldn't wait on target process: %s\n",
			prb_status_str(prbstat)));
		return (prbstat);
	}
	/* make sure it did stop on getpid() */
	prbstat = prb_proc_state(procfd, &pstate);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"prb_rtld_wait: couldn't get state of target: %s\n",
				prb_status_str(prbstat)));
		return (prbstat);
	}
	if (pstate.ps_issysentry && (pstate.ps_syscallnum == SYS_exit)) {
		DBG((void) fprintf(stderr, "prb_rtld_wait: target exited\n"));
		return (prb_status_map(EACCES));
	}
	/* catch any other errors */
	if (!(pstate.ps_issysexit && (pstate.ps_syscallnum == SYS_getpid))) {
		DBG((void) fprintf(stderr,
			"prb_rtld_wait: target didn't stop on getpid\n"));
		return (PRB_STATUS_BADSYNC);
	}
	/* clear wait on getpid */
	prbstat = prb_proc_exit(procfd, SYS_getpid, PRB_SYS_DEL);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"prb_rtld_wait: couldn't clear child to stop on "
			"exit of getpid(): %s\n", prb_status_str(prbstat)));
		return (prbstat);
	}
	/* clear wait on exit */
	prbstat = prb_proc_entry(procfd, SYS_exit, PRB_SYS_DEL);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"prb_rtld_wait: couldn't clear child to stop on "
			"entry of exit(): %s\n", prb_status_str(prbstat)));
		return (prbstat);
	}
	/* start-stop the process to clear it out of the system call */
	prbstat = prb_proc_prstop(procfd);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"prb_rtld_wait: couldn't prstop child: %s\n",
			prb_status_str(prbstat)));
		return (prbstat);
	}
	return (PRB_STATUS_OK);


}				/* end prb_rtld_wait */


/* ---------------------------------------------------------------- */
/* ----------------------- Private Functions ---------------------- */
/* ---------------------------------------------------------------- */

/*
 * bpt(), unbpt - plants/removes  a breakpoint at the specified location in
 * the target process, and saves/replaces the previous instruction.
 */

#ifdef sparc
#define	INS_BPT 0x91d02001
#elif defined(i386)
#define	INS_BPT 0xcc
#endif
#ifdef sparc
typedef unsigned long bptsave_t;
#elif defined(i386)
typedef unsigned char bptsave_t;
#endif

static bptsave_t	   g_saveinstr;
static boolean_t	   g_inserted = B_FALSE;

static prb_status_t
bpt(int procfd,
	caddr_t addr)
{
	prb_status_t	prbstat;
	bptsave_t	   instr;

	if (!g_inserted) {
#ifdef DEBUG
		if (__prb_verbose >= 2)
			(void) fprintf(stderr, "bpt: planting at 0x%x\n", addr);
#endif

		prbstat = prb_proc_read(procfd, addr,
			&g_saveinstr, sizeof (bptsave_t));
		if (prbstat)
			return (prbstat);

#ifdef DEBUG
		if (__prb_verbose >= 2)
			(void) fprintf(stderr,
				"bpt: saving 0x%x\n", (unsigned) g_saveinstr);
#endif

		/*LINTED constant truncated by assignment*/
		instr = INS_BPT;

		prbstat = prb_proc_write(procfd, addr,
			&instr, sizeof (bptsave_t));
		if (prbstat)
			return (prbstat);

		g_inserted = B_TRUE;
	}
	return (PRB_STATUS_OK);

}				/* end bpt */


prb_status_t
unbpt(int procfd,
	caddr_t addr)
{
	prb_status_t	prbstat;

	if (g_inserted) {
#ifdef DEBUG
		if (__prb_verbose >= 2)
			(void) fprintf(stderr,
				"unbpt: unplanting at 0x%x, saved instr 0x%x\n",
				addr, (unsigned) g_saveinstr);
#endif

		prbstat = prb_proc_write(procfd, addr, &g_saveinstr,
			sizeof (g_saveinstr));
		if (prbstat)
			return (prbstat);

		g_inserted = B_FALSE;
	}
	return (PRB_STATUS_OK);

}				/* end unbpt */
