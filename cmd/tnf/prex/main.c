/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)main.c 1.105 95/07/25 SMI"

/*
 * Includes
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <locale.h>
#include <libintl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/tnf.h>

#include "prbutl.h"
#include "set.h"
#include "cmd.h"
#include "spec.h"
#include "expr.h"
#include "source.h"
#include "list.h"
#include "prbk.h"

#include "tnf_buf.h"

/*
 * Defines - Project private interfaces
 */

#define	LIBTHREAD_PRESENT	"thr_probe_getfunc_addr"
#define	PROBE_THR_SYNC 		"__tnf_probe_thr_sync"
#define	THREAD_TEST		"tnf_threaded_test_addr"
#define	NONTHREAD_TEST		"tnf_non_threaded_test_addr"

#define	TRACEFILE_NAME		"tnf_trace_file_name"
#define	TRACEFILE_SIZE		"tnf_trace_file_size"
#define	TRACEFILE_MIN		"tnf_trace_file_min"
#define	TRACE_ERROR		"_tnfw_b_control"

#define	TRACE_ALLOC		"tnf_trace_alloc"
#define	TRACE_END		"tnf_trace_end"
#define	TRACE_COMMIT		"tnf_trace_commit"
#define	TRACE_ROLLBACK		"tnf_trace_rollback"
#define	DEBUG_ENTRY		"tnf_probe_debug"
#ifdef TESTING
#define	EMPTY_ENTRY		"tnf_probe_empty"
#endif

#define	USER_OUTSIZE		(4*1024*1024)
#define	KERNEL_OUTSIZE		(384*1024)

/*
 * Globals
 */

char		**g_argv; /* copy of argv pointer */
int		g_procfd = -1; /* target process /proc fd */

static int			g_verbose; /* debugging to stderr */
static char		   *g_cmdname;		/* target command name */
static char		  **g_cmdargs;		/* target command args */
static pid_t		g_targetpid;	/* target process id */
static volatile boolean_t g_getcmds;	/* accept input flag */
static boolean_t	g_sigflag;		/* we were signalled */
static boolean_t	g_stalkflag;	/* stalk the target process */
static boolean_t	g_lmapok;		/* link map consistent */
static boolean_t	g_testflag;		/* asserted in test mode */
static char		   *g_preload;		/* objects to preload */
static char		   *g_outname;		/* tracefile name */
int			g_outsize;		/* tracefile size */
boolean_t		g_kernelmode;		/* -k flag: kernel mode */

/* #### TEMPORARY - default test, intial and final functions */
caddr_t				g_commitfunc;

static caddr_t		g_testfunc;
static caddr_t		g_allocfunc;
static caddr_t		g_endfunc;
static caddr_t		g_rollbackfunc;


/*
 * Local Declarations
 */

static void
usage(char **argv,
	const char *msg);
static void
scanargs(int argc,
	char **argv);
static prb_status_t set_signal(void);

/* #### - FIXME - need to put this in a private header file */
extern void err_fatal(char *s, ...);

extern int	  yyparse(void);

static prb_status_t find_test_func(int procfd);
static prb_status_t find_target_funcs(int procfd);
static prb_status_t check_trace_error(int procfd);
static prb_status_t set_default_cmd(int procfd);
static prb_status_t attach(pid_t pid, int *procfd_p);
static prb_status_t create(int *procfd_p);
static prb_status_t refresh(int procfd);
static prb_status_t commands(void);
static prb_status_t process(int procfd);
static prb_status_t resume(int procfd);
static prb_status_t standby(int procfd);
static prb_status_t tracefile(int procfd);

void quit(boolean_t killtarget, boolean_t runtarget);
void stmt(void);


/*
 * usage() - gives a description of the arguments, and exits
 */

static void
usage(char *argv[], const char *msg)
{
	if (msg)
		(void) fprintf(stderr,
			gettext("%s: %s\n"), argv[0], msg);

	(void) fprintf(stderr, gettext(
		"usage: %s [options] <cmd> [cmd-args...]\n"), argv[0]);
	(void) fprintf(stderr, gettext(
		"usage: %s [options] -p <pid>\n"), argv[0]);
	(void) fprintf(stderr, gettext(
		"usage: %s -s <kbytes-size> -k\n"), argv[0]);
	(void) fprintf(stderr, gettext(
		"options:\n"));
	(void) fprintf(stderr, gettext(
		"	-o <outfilename>   set trace output file name\n"));
	(void) fprintf(stderr, gettext(
		"	-s <kbytes-size>   set trace file size\n"));
	(void) fprintf(stderr, gettext(
		"	-l <sharedobjs>    shared objects to "
		"be preloaded (cmd only)\n"));

	exit(1);

}				/* end usage */


/*
 * main() -
 */

int
main(int argc, char **argv)
{
	prb_status_t	prbstat;
	int			 procfd;

	/* internationalization stuff */
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	g_argv = argv;

	scanargs(argc, argv);

	if (g_kernelmode) {
		/* prexing the kernel */
		prbstat = prbk_init();
		switch (prbstat) {
		case PRB_STATUS_OK:
			break;
		default:
			err_fatal(gettext(
				"%s: trouble attaching to the kernel: %s\n"),
				argv[0], prb_status_str(prbstat));
		}
	} else if (g_targetpid != 0) {
		prbstat = attach(g_targetpid, &procfd);
		switch (prbstat) {
		case PRB_STATUS_OK:
			break;
		default:
			err_fatal(gettext(
				"%s: trouble attaching to target "
				"process: %s\n"),
				argv[0], prb_status_str(prbstat));
		}
	} else {
		prbstat = create(&procfd);
		if (prbstat) {
			err_fatal(gettext(
				"%s: trouble creating target process: %s\n"),
				argv[0], prb_status_str(prbstat));
		}
	}

	/* initialize the source stack for the parser */
	source_init();

	if (g_kernelmode) {
		prbk_set_other_funcs(&g_allocfunc, &g_commitfunc,
			&g_rollbackfunc, &g_endfunc);
	} else {
		/* initialize the link map table */
		prbstat = prb_lmap_update(procfd);
		if (prbstat) {
			(void) fprintf(stderr, gettext(
				"%s: trouble updating the link map: %s\n"),
				argv[0], prb_status_str(prbstat));
			goto Cleanup;
		}
		/* find the needed target functions */
		prbstat = find_target_funcs(procfd);
		if (prbstat) {
			(void) fprintf(stderr, gettext(
				"%s: missing symbols, is libtnfprobe.so "
				"loaded in target?\n"),
				argv[0]);
			goto Cleanup;
		}
		/* set the tracefile name and size */
		prbstat = tracefile(procfd);
		if (prbstat) {
			(void) fprintf(stderr, gettext(
				"%s: trouble initializing tracefile: %s\n"),
				argv[0], prb_status_str(prbstat));
			goto Cleanup;
		}
		prbstat = check_trace_error(procfd);
		if (prbstat) {
			(void) fprintf(stderr, gettext(
				"%s: cannot read tracing status : %s\n"),
				argv[0], prb_status_str(prbstat));
			goto Cleanup;
		}
		/* needed by routines that parser calls */
		g_procfd = procfd;
	}

	/* accept commands from stdin the first time through */
	g_getcmds = B_TRUE;

	/* find default probe functions */
	prbstat = set_default_cmd(procfd);
	if (prbstat) {
		(void) fprintf(stderr, gettext(
		"%s: trouble finding default probe functions: %s\n"),
			argv[0], prb_status_str(prbstat));
	}
	while (!prbstat) {
		prb_proc_state_t state;

		if (!g_kernelmode) {
			if (prb_proc_state(procfd, &state))
				goto Cleanup;
			if (state.ps_isbptfault) {
				(void) prb_proc_clrbptflt(procfd);
			} else if (state.ps_issysentry &&
				(state.ps_syscallnum == SYS_exec ||
					state.ps_syscallnum == SYS_execve)) {
				(void) printf(gettext(
					"Target process exec'd\n"));
				quit(B_FALSE, B_TRUE);	/* quit resume */
			} else {
				if (g_testflag)
					(void) printf(
						"prex(%ld), target(%ld): ",
						getpid(), g_targetpid);
				(void) printf(gettext(
					"Target process stopped\n"));
			}

			/* update the link map */
			prbstat = prb_lmap_update(procfd);
			if (prbstat && prbstat != PRB_STATUS_BADLMAPSTATE) {
				(void) fprintf(stderr,	gettext(
					"%s: trouble updating the "
					"link map: %s\n"),
					argv[0], prb_status_str(prbstat));
				goto Cleanup;
			}
			g_lmapok = (prbstat != PRB_STATUS_BADLMAPSTATE);
			prbstat = PRB_STATUS_OK;

			if (g_sigflag && !g_lmapok)
				(void) printf(gettext(
					"%s: link map inconsistent, "
					"cannot stop here\n"),
					argv[0]);

		}
		/* if the mapping state is consistent, we are in business */
		if (g_kernelmode || g_lmapok) {
			g_sigflag = B_FALSE;

			/*
			 * test function that should be used can change
			 * depending on whether libthread/libtnfw have
			 * sync'ed up or not.
			 */
			if (g_kernelmode
				? prbk_test_func(&g_testfunc)
				: find_test_func(procfd))
				goto Cleanup;

			if (!g_kernelmode && refresh(procfd))
				goto Cleanup;

			if (!g_kernelmode && process(procfd))
				goto Cleanup;

			if (g_kernelmode || g_getcmds) {
				g_getcmds = B_FALSE;
				if (commands())
					goto Cleanup;
			}
			/* flush probe updates into the target */
			if (!g_kernelmode && prb_link_flush(procfd))
				goto Cleanup;
		}
		/*
		 * if we stopped on a breakpoint, advance beyond it before
		 * resuming
		 */
		if (state.ps_isbptfault)
			if (prb_rtld_advance(procfd))
				goto Cleanup;

		if (!g_kernelmode && (!g_getcmds || !g_lmapok)) {
			if (resume(procfd))
				goto Cleanup;

			if (standby(procfd))
				goto Cleanup;
		}
	}

Cleanup:
	(void) close(procfd);

	exit(0);

	return (0);

}				/* end main */


/*
 * attach() - attaches to a running process.
 */

static		  prb_status_t
attach(pid_t pid, int *procfd_p)
{
	prb_status_t	prbstat;
	int			 procfd;

	/* check if pid is valid */
	if ((kill(pid, 0) == -1) && errno == ESRCH) {
		return (prb_status_map(errno));
	}
	/* open up /proc fd */
	prbstat = prb_proc_open(pid, &procfd);
	if (prbstat)
		return (prbstat);

	/*
	 * default is to run-on-last-close.  In case we cannot sync with
	 * target, we don't want to kill the target.
	 */
	prbstat = prb_proc_setrlc(procfd, B_TRUE);
	if (prbstat)
		return (prbstat);
	prbstat = prb_proc_setklc(procfd, B_FALSE);
	if (prbstat)
		return (prbstat);

	/*
	 * REMIND: check if rtld has already been loaded - can check r_debug.
	 * If 0 or 1, rtld has not been loaded.  If not loaded, we do rtld
	 * sync
	 */

	/* stop process */
	prbstat = prb_proc_stop(procfd);
	if (prbstat)
		return (prbstat);

	prbstat = prb_elf_isdyn(procfd);
	if (prbstat)
		return (prbstat);

	prbstat = set_signal();
	if (prbstat)
		return (prbstat);

	*procfd_p = procfd;
	return (PRB_STATUS_OK);

}				/* end attach */


/*
 * create() - invokes the target program and executes it just till its gotten
 * its mappings (but before any .init sections
 */

static		  prb_status_t
create(int *procfd_p)
{
	prb_status_t	prbstat;
	prb_proc_state_t pstate;
	pid_t		 childpid;
	int procfd, oldfd;
	prb_status_t tempstat;

	*procfd_p = -1;

	prbstat = prb_shmem_init();
	if (prbstat)
		return (prbstat);

	prbstat = set_signal();
	if (prbstat)
		return (prbstat);

	prbstat = prb_child_create(g_cmdname, g_cmdargs, g_preload, &childpid);
	if (prbstat)
		return (prbstat);

	g_targetpid = childpid;

	prbstat = prb_proc_open(childpid, &procfd);
	if (prbstat)
		return (prbstat);

	prbstat = prb_proc_stop(procfd);
	if (prbstat)
		return (prbstat);

	/*
	 * default is to kill-on-last-close.  In case we cannot sync with
	 * target, we don't want the target to continue.
	 */
	prbstat = prb_proc_setrlc(procfd, B_FALSE);
	if (prbstat)
		return (prbstat);

	prbstat = prb_proc_setklc(procfd, B_TRUE);
	if (prbstat)
		return (prbstat);

	/* REMIND: do we have to wait on SYS_exec also ? */
	prbstat = prb_proc_exit(procfd, SYS_execve, PRB_SYS_ADD);
	if (prbstat)
		return (prbstat);

	prbstat = prb_proc_entry(procfd, SYS_exit, PRB_SYS_ADD);
	if (prbstat)
		return (prbstat);

#ifdef PIPE_SYNC
	prbstat = prb_pipe_setup(childpid);
	if (prbstat)
		return (prbstat);
#endif

	prbstat = prb_shmem_clear();
	if (prbstat)
		return (prbstat);

	prbstat = prb_proc_cont(procfd);
	if (prbstat)
		return (prbstat);

	prbstat = prb_proc_wait(procfd);
	switch (prbstat) {
	case PRB_STATUS_OK:
		break;
	case EAGAIN:
		/*
		 * If we had exec'ed a setuid/setgid program PIOCWSTOP
		 * will return EAGAIN.  Reopen the 'fd' and try again.
		 * Read the last section of /proc man page - we reopen first
		 * and then close the old fd.
		 */
		oldfd = procfd;
		tempstat = prb_proc_open(childpid, &procfd);
		switch (tempstat) {
		case PRB_STATUS_OK:
			break;
		case EACCES:
			err_fatal(gettext(
				"%s: can't operate on setuid programs\n"),
				g_argv[0]);
			break;
		default:
			return (tempstat);
			/*LINTED: statement not reached*/
			break;
		}

		close(oldfd);
		break;
	default:
		return (prbstat);
		/*LINTED: statement not reached*/
		break;
	}

	prbstat = prb_proc_state(procfd, &pstate);
	if (prbstat)
		return (prbstat);

	if (pstate.ps_issysexit && (pstate.ps_syscallnum == SYS_execve)) {
		/* expected condition */
		prbstat = prb_rtld_setup(procfd);
		if (prbstat)
			return (prbstat);
	} else {
		return (prb_status_map(ENOENT));
	}

	/* clear old interest mask */
	prbstat = prb_proc_exit(procfd, 0, PRB_SYS_NONE);
	if (prbstat)
		return (prbstat);

	prbstat = prb_proc_entry(procfd, 0, PRB_SYS_NONE);
	if (prbstat)
		return (prbstat);

	/* wait on target to sync up after rtld maps in all .so's */
	prbstat = prb_rtld_wait(procfd);
	if (prbstat)
		return (prbstat);

#ifdef PIPE_SYNC
	prbstat = prb_pipe_wait();
	if (prbstat)
		return (prbstat);

	prbstat = prb_proc_stop(procfd);
	if (prbstat)
		return (prbstat);
#endif

	*procfd_p = procfd;

	return (PRB_STATUS_OK);

}				/* end create */


/*
 * refresh() - link any new probes together in the target process, and
 * refresh our internal data structures.
 */

static		  prb_status_t
refresh(int procfd)
{
	prb_status_t	prbstat;

	/* reset the current probe data */
	prbstat = prb_link_reset(procfd);
	if (prbstat)
		return (prbstat);

	/* find and link probes */
	prbstat = prb_link_find(procfd);
	if (prbstat)
		return (prbstat);

	/*
	 * disable the head pointer, we will restore it right before we
	 * start the process
	 */
	prbstat = prb_link_disable(procfd);
	if (prbstat)
		return (prbstat);

	return (PRB_STATUS_OK);

}				/* end refresh */


/*
 * find_test_func() - finds the default test function.
 */

static		  prb_status_t
find_test_func(int procfd)
{
#undef NSYMS
#undef NFUNCS
#define	NSYMS 1
#define	NFUNCS 3
	const char	 *symnames[NSYMS] = {
	LIBTHREAD_PRESENT};
	const char	 *funcname[NFUNCS] = {
		NONTHREAD_TEST, THREAD_TEST,
	PROBE_THR_SYNC};
	caddr_t		 symaddrs[NSYMS];
	caddr_t		 funcloc[NFUNCS];

	caddr_t		 func_addr;
	long			thr_sync;
	prb_status_t	prbstat;
	unsigned int	test_addr = 0;

	/* find actual functions first */
	prbstat = prb_sym_find(procfd, NFUNCS, funcname, funcloc);
	if (prbstat)
		return (prbstat);

	prbstat = prb_sym_find(procfd, NSYMS, symnames, symaddrs);
	if (prbstat) {
		if (prbstat == PRB_STATUS_SYMMISSING) {
			/* no libthread linked in */
			func_addr = funcloc[0];
		} else {
			return (prbstat);
		}
	} else {
		/*
		 * no error, check whether libthread/libtnfw have synced up.
		 * If not yet synced up, use non-threaded test function
		 */
		func_addr = funcloc[1];
		/* assume we are going to use threaded test */
		prbstat = prb_proc_read(procfd, funcloc[2], &thr_sync,
			sizeof (thr_sync));
		if (prbstat)
			return (prbstat);
		/* if not yet synced up, change test func to non-threaded one */
		if (thr_sync == 0) {
			func_addr = funcloc[0];
		}
	}

	prbstat = prb_proc_read(procfd, func_addr,
		&test_addr, sizeof (test_addr));
	if (prbstat)
		return (prbstat);

	g_testfunc = (caddr_t) test_addr;

	return (PRB_STATUS_OK);

}				/* end find_test_func */


/*
 * find_target_funcs() - finds needed target functions
 */

static		  prb_status_t
find_target_funcs(int procfd)
{
#undef NSYMS
#define	NSYMS 4
	const char	 *symnames[NSYMS] = {
		TRACE_ALLOC,
		TRACE_COMMIT,
		TRACE_END,
	TRACE_ROLLBACK};
	caddr_t		 symaddrs[NSYMS];
	prb_status_t	prbstat;

	prbstat = prb_sym_find(procfd, NSYMS, symnames, symaddrs);
	if (prbstat)
		return (prbstat);

	g_allocfunc = symaddrs[0];
	g_commitfunc = symaddrs[1];
	g_endfunc = symaddrs[2];
	g_rollbackfunc = symaddrs[3];

	return (PRB_STATUS_OK);

}				/* end find_alloc_func */


/*
 * check_trace_error() - checks whether there was an error in tracing
 */

static		  prb_status_t
check_trace_error(int procfd)
{
#undef NSYMS
#define	NSYMS 1
	const char	 *symnames[NSYMS] = {TRACE_ERROR};
	caddr_t		 symaddrs[NSYMS];
	prb_status_t	prbstat;
	caddr_t		trace_error_ptr;
	TNFW_B_CONTROL	trace_error_rec;

	prbstat = prb_sym_find(procfd, NSYMS, symnames, symaddrs);
	if (prbstat)
		return (prbstat);

	/* read in the value of the control structure pointer */
	prbstat = prb_proc_read(procfd, symaddrs[0], &trace_error_ptr,
		sizeof (trace_error_ptr));
	if (prbstat)
		return (prbstat);

	/* read in the value of the control structure */
	prbstat = prb_proc_read(procfd, trace_error_ptr, &trace_error_rec,
		sizeof (trace_error_rec));
	if (prbstat)
		return (prbstat);

	if (trace_error_rec.tnf_state == TNFW_B_BROKEN) {
		(void) printf(gettext("Tracing shut down in target program "
			"due to an internal error - Please restart prex "
			"and target\n"));
	}

	return (PRB_STATUS_OK);

}				/* end find_alloc_func */

/*
 * set_default_cmd() - set the default initial and final function.
 */

/*ARGSUSED*/
static		  prb_status_t
set_default_cmd(int procfd)
{
	if (!g_kernelmode)
		fcn(strdup("debug"), DEBUG_ENTRY);
#ifdef TESTING
	fcn(strdup("empty"), EMPTY_ENTRY);
#endif
	(void) set(strdup("all"), expr(spec(strdup("keys"), SPEC_EXACT),
				spec(strdup(".*"), SPEC_REGEXP)));

	/* set("all", expr(".*", EXPR_REGEXP)); */
	/* cmd_set("all", CMD_INSTRUMENT, "debug"); */

	return (PRB_STATUS_OK);

}				/* end set_default_cmd */

/*
 * process() - enable and disable selected probes
 */

typedef struct {
	prbctlref_t	*ref_p;
	int			 procfd;

}			   process_args_t;

static		  prb_status_t
percmd(expr_t * expr_p,
	cmd_kind_t kind,
	fcn_t * fcn_p,
	boolean_t isnew,
	void *calldata_p)
{
	process_args_t *args_p = (process_args_t *) calldata_p;
	int			 procfd = args_p->procfd;
	prbctlref_t	*ref_p = args_p->ref_p;
	tnf_probe_control_t *prbctl_p = &ref_p->wrkprbctl;
	prb_status_t	prbstat;
	caddr_t		 comb;
	char		   *attrs;

	/* If this is an old command and an old probe, bail */
	if (!isnew && !g_kernelmode && !ref_p->lmap_p->isnew)
		return (PRB_STATUS_OK);

	attrs = list_getattrs(ref_p);

	if (expr_match(expr_p, attrs)) {
#if defined(DEBUG) || defined(lint)
		if (g_verbose) {
			char		   *cmdstr[] = {
				"enable", "disable",
				"connect", "clear",
				"trace", "untrace"};

			(void) fprintf(stderr, "%s 0x%08x: %s command: %s ",
				(ref_p->lmap_p->isnew) ? "*" : " ",
				(unsigned) ref_p->addr,
				(isnew) ? "new" : "old", cmdstr[kind]);
			expr_print(stderr, expr_p);
		}
#endif

		switch (kind) {
		case CMD_ENABLE:
			prbctl_p->test_func =
					(tnf_probe_test_func_t) g_testfunc;
			break;
		case CMD_DISABLE:
			prbctl_p->test_func = (tnf_probe_test_func_t) NULL;
			break;
		case CMD_TRACE:
			prbctl_p->commit_func = (tnf_probe_func_t) g_commitfunc;
			break;
		case CMD_UNTRACE:
			prbctl_p->commit_func =
					(tnf_probe_func_t) g_rollbackfunc;
			break;
		case CMD_CONNECT:
			prbstat = prb_comb_build(procfd,
				PRB_COMB_CHAIN,
				(caddr_t) fcn_p->entry_addr,
				(caddr_t) prbctl_p->probe_func,
				&comb);
			if (!prbstat)
				prbctl_p->probe_func = (tnf_probe_func_t) comb;
			break;
		case CMD_CLEAR:
			prbctl_p->probe_func = (tnf_probe_func_t) g_endfunc;
			break;
		}

#if defined(DEBUG) || defined(lint)
		if (g_verbose)
			(void) fprintf(stderr, "\n");
#endif

	}
	if (attrs)
		free(attrs);

	return (PRB_STATUS_OK);

}				/* end percmd */


static		  prb_status_t
perprobe(prbctlref_t * ref_p, void *calldata_p)
{
	int			 procfd = (int) calldata_p;
	process_args_t  args;

	/* if this is a "virgin" probe, set up its initial state */
	if (!ref_p->wrkprbctl.commit_func) {
		ref_p->wrkprbctl.probe_func = (tnf_probe_func_t) g_endfunc;
		ref_p->wrkprbctl.commit_func = (tnf_probe_func_t) g_commitfunc;
		ref_p->wrkprbctl.alloc_func =
					(tnf_probe_alloc_func_t) g_allocfunc;
	}
	args.ref_p = ref_p;
	args.procfd = procfd;
	cmd_traverse(percmd, &args);

	return (PRB_STATUS_OK);

}				/* end perprobe */

static		  prb_status_t
process(int procfd)
{
#if defined(DEBUG) || defined(lint)
	if (g_verbose)
		(void) fprintf(stderr, "processing commands\n");
#endif
	(void) prb_link_traverse(perprobe, (void *) procfd);

	if (g_kernelmode) {
		(void) prbk_tracing_sync();
		(void) prbk_pfilter_sync();
		(void) prb_link_flush(procfd);
	}
	/* mark all of the commands as executed */
	cmd_mark();

	/* mark all of the probes as old */
	(void) prb_lmap_mark(procfd);

	return (PRB_STATUS_OK);

}				/* end process */


/*
 * commands() - process commands from stdin
 */

static		  prb_status_t
commands(void)
{
	/* Read commands from STDIN */
	if (g_kernelmode)
		(void) printf(gettext("Type \"help\" for help ...\n"));
	else
		(void) printf(gettext(
		"Type \"continue\" to resume the target, "
		"\"help\" for help ...\n"));

	while (yyparse());

	return (PRB_STATUS_OK);

}				/* end commands */


/*
 * stmt() - called after each statement has been reduced by the parser.
 */

void
stmt(void)
{
	if (g_kernelmode)
		(void) prbk_refresh();
	(void) process(g_procfd);
}				/* end stmt */


/*
 * resume() - resumes execution of the target process
 */

static		  prb_status_t
resume(int procfd)
{
	prb_status_t	prbstat;

	/* restore the targets probe linked list */
	prbstat = prb_link_enable(procfd);
	if (prbstat)
		return (prbstat);

	/* trace exec */
	prbstat = prb_proc_entry(procfd, SYS_execve, PRB_SYS_ADD);
	if (prbstat)
		return (prbstat);
	prbstat = prb_proc_entry(procfd, SYS_exec, PRB_SYS_ADD);
	if (prbstat)
		return (prbstat);

	/* setup process to stop during dl_open */
	if (g_stalkflag) {
		prbstat = prb_rtld_stalk(procfd);
		if (prbstat)
			return (prbstat);
	}
	prbstat = prb_proc_cont(procfd);

	return (prbstat);

}				/* end resume */


/*
 * standby() - tracks the progress of the target process. If the "stalk"
 * arguement is asserted, the dl_opens, forks, and execs in the target
 * process will be trapped and processed.
 */

static		  prb_status_t
standby(int procfd)
{
	prb_status_t	prbstat = PRB_STATUS_OK;

	while (!g_sigflag) {
		prbstat = prb_proc_wait(procfd);
		if (prbstat != EINTR) {
			/* target terminated, or RTLD BPT hit */

#if defined(DEBUG) || defined(lint)
			if (prbstat && prbstat != ENOENT) {
				perror("standby: trouble in prb_proc_wait");
			}
#endif

			if (prbstat == ENOENT) {
				/* target terminated */
				(void) fprintf(stderr, gettext(
					"%s: target process finished\n"),
					g_argv[0]);
			} else {
				/* RTLD BPT case */
				/*
				 * remove the stalking breakpoint while
				 * stopped
				 */
				(void) prb_rtld_unstalk(procfd);

				/* remove the exec tracing while stopped */
				prbstat = prb_proc_entry(procfd,
					SYS_execve, PRB_SYS_DEL);
				if (prbstat)
					return (prbstat);
				prbstat = prb_proc_entry(procfd,
					SYS_exec, PRB_SYS_DEL);
				if (prbstat)
					return (prbstat);
			}

			return (prbstat);
		}
		/* signal encountered, if ^C will fall out ... */
	}

	/* stop the target process */
	prbstat = prb_proc_stop(procfd);

	/* remove the stalking breakpoint while the process is stopped */
	(void) prb_rtld_unstalk(procfd);

	/* remove the exec tracing while stopped */
	prbstat = prb_proc_entry(procfd, SYS_execve, PRB_SYS_DEL);
	if (prbstat)
		return (prbstat);
	prbstat = prb_proc_entry(procfd, SYS_exec, PRB_SYS_DEL);
	if (prbstat)
		return (prbstat);

	prbstat = check_trace_error(procfd);
	if (prbstat)
		return (prbstat);

	return (prbstat);

}				/* end standby */


/*
 * quit() - called to quit the controlling process. The boolean argument
 * specifies whether to terminate the target as well.
 */

void
quit(boolean_t killtarget, boolean_t runtarget)
{
	/* what if you type "quit resume"? You need to flush ... */
	(void) prb_link_flush(g_procfd);

	/*
	 * setting both arguments to B_TRUE is a out-of-band signal * which
	 * means quit without changing either klc or rlc
	 */
	if (killtarget && runtarget)
		exit(0);

	(void) prb_proc_setklc(g_procfd, killtarget);
	(void) prb_proc_setrlc(g_procfd, runtarget);

	exit(0);

}				/* end quit */


/*
 * scanargs() - processes the command line arguments
 */

#define	strneq(s1, s2, n) 	(strncmp(s1, s2, n) == 0)

static void
scanargs(int argc,
	char **argv)
{
	int			 c;
#if defined(DEBUG) || defined(lint)
	char		   *optstr = "l:o:p:s:tkv:";	/* debugging options */
#else
	char		   *optstr = "l:o:p:s:tk";	/* production options */
#endif

	/* set up some defaults */
	prb_verbose_set(0);
	g_stalkflag = B_TRUE;
	g_targetpid = 0;
	g_cmdname = NULL;
	g_cmdargs = NULL;
	g_preload = NULL;
	g_outname = NULL;
	g_outsize = -1;

	while ((c = getopt(argc, argv, optstr)) != EOF) {
		switch (c) {
		case 'l':	/* preload objects */
			g_preload = optarg;
			break;
		case 'o':	/* tracefile name */
			g_outname = optarg;
			break;
		case 'p':	/* target pid (attach case) */
			g_targetpid = atoi(optarg);
			break;
		case 's':	/* tracefile size */
			g_outsize = atoi(optarg) * 1024;
			break;
		case 't':	/* test flag */
			g_testflag = B_TRUE;
			(void) setvbuf(stdout, NULL, _IOLBF, 0);
			break;
		case 'k':	/* kernel mode */
			g_kernelmode = B_TRUE;
			break;
#if defined(DEBUG) || defined(lint)
		case 'v':	/* verbose flag */
			g_verbose = atoi(optarg);
			prb_verbose_set(g_verbose);
			break;
#endif
		case '?':	/* error case */
			usage(argv, gettext("unrecognized argument"));
		}
	}

	if (optind < argc) {
		g_cmdname = strdup(argv[optind]);
		g_cmdargs = &argv[optind];
	}
	/* sanity clause */
	if (!g_kernelmode && (g_cmdname == NULL && g_targetpid == 0))
		usage(argv, gettext("need to specify cmd or pid"));
	if (g_cmdname != NULL && g_targetpid != 0)
		usage(argv, gettext("can't specify both cmd and pid"));
	if (g_targetpid && g_preload)
		usage(argv, gettext("can't use preload option with attach"));
	if (g_kernelmode) {
		if (g_outname)
			usage(argv, "can't specify a filename in kernel mode");
		if (g_cmdname)
			usage(argv, "can't specify a command in kernel mode");
		if (g_targetpid)
			usage(argv, "can't specify pid in kernel mode");
		if (g_preload)
			usage(argv, "can't use preload option in kernel mode");
	}
	/* default output size */
	if (g_outsize == -1)
		g_outsize = g_kernelmode ? KERNEL_OUTSIZE : USER_OUTSIZE;

#ifdef OLD
	int			 i;

	for (i = 1; i < argc; i++) {
		if (strneq(argv[i], "-v", 2)) {
			int			 vlevel;

			vlevel = (strlen(argv[i]) >
				(size_t) 2) ? atoi(&argv[i][2]) : 1;
			g_verbose = B_TRUE;
			prb_verbose_set(vlevel);
		} else if (strneq(argv[i], "-pid", 2)) {
			if (++i >= argc)
				usage(argv, gettext("missing pid argument"));
			g_targetpid = atoi(argv[i]);
		} else if (strneq(argv[i], "-t", 2)) {
			g_testflag = B_TRUE;
			(void) setvbuf(stdout, NULL, _IOLBF, 0);
		} else if (argv[i][0] != '-') {
			g_cmdname = strdup(argv[i]);
			if (!g_cmdname) {
				err_fatal(gettext(
					"%s: out of memory"), argv[0]);
			}
			if (g_verbose >= 2) {
				(void) fprintf(stderr,
					"cmdname=%s\n", g_cmdname);
			}
			/*
			 * rest of arguments are the args to the executable -
			 * by convention argv[0] should be name of
			 * executable, so we don't increment i
			 */
			g_cmdargs = &argv[i];
			break;
		} else {
			usage(argv, gettext("unrecognized argument"));
		}
	}
#endif

}				/* end scanargs */


/*
 * sig_handler() - cleans up if a signal is received
 */

/*ARGSUSED*/
static void
sig_handler(int signo)
{
	g_sigflag = B_TRUE;
	g_getcmds = B_TRUE;

}				/* end sig_handler */


/*
 * set_signal() -  sets up function to call for clean up
 */

static		  prb_status_t
set_signal(void)
{
	struct sigaction newact;

	newact.sa_handler = sig_handler;
	(void) sigemptyset(&newact.sa_mask);
	newact.sa_flags = 0;
	if (sigaction(SIGINT, &newact, NULL) < 0) {
		return (prb_status_map(errno));
	}
	return (PRB_STATUS_OK);

}				/* end set_signal */


#ifdef STALE
/*
 * stalk() - turns stalking on
 */

void
stalk(boolean_t state)
{
	g_stalkflag = state;

}				/* end stalk_on */
#endif


/*
 * tracefile() - initializes tracefile, sets the tracefile name and size
 */

#define	NTFSYM		3
#define	ZBUFSZ		(64 * 1024)

static		  prb_status_t
tracefile(int procfd)
{
	char		   *preexisting;
	prb_status_t	prbstat;
	char			path[MAXPATHLEN];
	char		   *tmpdir;
	size_t		  outsize;
	const char	 *symnames[NTFSYM] = {
		TRACEFILE_NAME,
		TRACEFILE_SIZE,
		TRACEFILE_MIN};
	caddr_t		 symaddrs[NTFSYM];
	caddr_t		 name_addr;
	caddr_t		 size_addr;
	caddr_t			min_addr;
	size_t			minoutsize;
	char			zerobuf[ZBUFSZ];
	int				fd;
	int				sz;
	int 			i;

	/* find the neccessary symbols in the target */
	prbstat = prb_sym_find(procfd, NTFSYM, symnames, symaddrs);
	if (prbstat)
		return (prbstat);
	name_addr = symaddrs[0];
	size_addr = symaddrs[1];
	min_addr = symaddrs[2];

	/* if the outname has already been set, leave it alone */
	preexisting = NULL;
	prbstat = prb_proc_readstr(procfd, name_addr, &preexisting);
	if (prbstat) {
		if (preexisting)
			free(preexisting);
		return (prbstat);
	}
	if (preexisting[0] != '\0') {
#if defined(DEBUG) || defined(lint)
		if (g_verbose)
			(void) fprintf(stderr,
				"saw preexisting tracefile name \"%s\"\n",
				preexisting);
#endif
		free(preexisting);
		return (PRB_STATUS_OK);
	}
	if (preexisting)
		free(preexisting);

	/* read the minimum file size from the target */
	prbstat = prb_proc_read(procfd, min_addr,
		&minoutsize, sizeof (size_t));
	if (prbstat)
		return (prbstat);
	if (g_outsize < minoutsize)	{
		(void) fprintf(stderr,
			gettext("specified tracefile size smaller then "
				"minimum; setting to %d kbytes\n"),
				minoutsize / 1024);
		g_outsize = minoutsize;
	}

	/* where is $TMPDIR? */
	tmpdir = getenv("TMPDIR");
	if (!tmpdir || *tmpdir == '\0') {
		tmpdir = "/tmp";
	}
	/* do we have an absolute, relative or no pathname specified? */
	if (g_outname == NULL) {
		/* default, no tracefile specified */
		if ((strlen(tmpdir) + 1 + 20) > (size_t) MAXPATHLEN) {
			(void) fprintf(stderr, gettext(
				"%s: $TMPDIR too long\n"), g_argv[0]);
			exit(1);
		}
		(void) sprintf(path, "%s/trace-%ld", tmpdir, g_targetpid);
	} else if (g_outname[0] == '/') {
		/* absolute path to tracefile specified */
		if ((strlen(g_outname) + 1) > (size_t) MAXPATHLEN) {
			(void) fprintf(stderr, gettext(
				"%s: directory specification too long\n"),
				g_argv[0]);
			exit(1);
		}
		(void) strcpy(path, g_outname);
	} else {
		char		   *cwd;

		/* relative path to tracefile specified */
		cwd = getcwd(NULL, MAXPATHLEN);
		if (!cwd) {
			(void) fprintf(stderr,	gettext(
				"%s: trouble getting current directory: %s\n"),
				g_argv[0], strerror(errno));
			exit(1);
		}
		if ((strlen(cwd) + 1 + strlen(g_outname) + 1) >
			(size_t) MAXPATHLEN) {
			(void) fprintf(stderr, gettext(
				"%s: current directory path too long\n"),
				g_argv[0]);
			exit(1);
		}
		(void) sprintf(path, "%s/%s", cwd, g_outname);

		free(cwd);
	}

	outsize = g_outsize;

#if defined(DEBUG) || defined(lint)
	if (g_verbose)
		(void) fprintf(stderr,
			"setting tracefile name=\"%s\", size=%d\n",
			path, outsize);
#endif

	/* unlink a previous tracefile (if one exists) */
	(void) unlink(path);

	/* create the new tracefile */
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0)	{
		(void) fprintf(stderr,
			gettext("trouble creating tracefile\n"));
		return (prb_status_map(errno));
	}

	/* zero fill the file */
	(void) memset(zerobuf, 0, ZBUFSZ);
	sz = ZBUFSZ;
	for (i = 0; i < outsize; i += sz) {
		int				retval;

		sz = ((outsize - i) > ZBUFSZ) ? ZBUFSZ : (outsize - i);
		retval = write(fd, zerobuf, sz);
		if (retval == -1) {
			(void) fprintf(stderr,
				gettext("trouble zeroing tracefile\n"));
			return (prb_status_map(errno));
		}
	}

	/* close the file */
	(void) close(fd);

	/* write the tracefile name and size into the target process */
	prbstat = prb_proc_write(procfd, name_addr, path, strlen(path) + 1);
	if (prbstat)
		return (prbstat);
	prbstat = prb_proc_write(procfd, size_addr,
		&outsize, sizeof (size_t));
	if (prbstat)
		return (prbstat);

	return (PRB_STATUS_OK);

}				/* end tracefile */
