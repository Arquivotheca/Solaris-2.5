/*
 *	Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident	"@(#)probe_cntl.c	1.24	95/03/21 SMI"

/*
 * Includes
 */

#ifndef DEBUG
#define	NDEBUG	1
#endif

#include <thread.h>
#include <sys/lwp.h>
#include <synch.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>

#include "tnf_trace.h"


/*
 * Typedefs
 */

typedef tnf_ops_t * (*tnf_context_t)(void);

typedef void * (*start_func_t)(void *			arg);

typedef int (*thr_create_func_t)(void *			stk,
				size_t			stksize,
				start_func_t		startfunc,
				void *			arg,
				long			flags,
				thread_t *		newthread);

typedef pid_t (*fork_t)(void);

typedef struct args {
	start_func_t		real_func;
	void 			*real_arg;
} args_t;

/*
 * Local Declaraions
 */

static void * tnf_threaded_test(void 			*dummy,
				tnf_probe_control_t 	*probe_p,
				tnf_probe_setup_t 	*set_p);
static void * tnf_non_threaded_test(void 	*		dummy,
					tnf_probe_control_t *	probe_p,
					tnf_probe_setup_t *	set_p);
static tnf_ops_t *tnf_probe_getfunc(void);
static void *probestart(void *arg);
static pid_t common_fork(fork_t real_fork);
static void probe_setup(void *data);

/*
 * Static Globals
 */

extern tnf_ops_t 	tnf_trace_initial_tpd;
static void 		*tpd = &tnf_trace_initial_tpd;


/*
 * Project Private interfaces:
 * 	These are interfaces between prex and libtnfw, or
 * 	between libtnfw and libtthread.
 */

/* variable indicates if libtnfw has sync'ed up with libthread or not */
long			__tnf_probe_thr_sync		= 0;

/* head of the list that is used to chain all probes */
tnf_probe_control_t *	__tnf_probe_list_head		= NULL;
int			__tnf_probe_list_valid		= 0;

/* notify function that libthread calls after primordial thread is created */
void __tnf_probe_notify(void);

tnf_probe_test_func_t tnf_threaded_test_addr = tnf_threaded_test;
tnf_probe_test_func_t tnf_non_threaded_test_addr = tnf_non_threaded_test;


/*
 * Externs
 */
#pragma weak thr_probe_getfunc_addr
extern tnf_context_t	thr_probe_getfunc_addr;

#pragma weak thr_probe_setup
extern void thr_probe_setup(void *);

/* ---------------------------------------------------------------- */
/* ----------------------- Public Functions ----------------------- */
/* ---------------------------------------------------------------- */

/*
 * probe_setup() - the thread probe setup function for the non-threaded
 * case.
 */
static void
probe_setup(void *data)
{
#ifdef DEBUG
	/* #### - TEMPORARY */
	fprintf(stderr, "probe_setup: \n");
#endif
	tpd = data;

}   /* end probe_setup */

/*
 * __tnf_probe_notify() - libthread calls this function to notify us
 * that the primordial thread has been created.
 */

void
__tnf_probe_notify(void)
{
	tnf_probe_control_t *		prbctl_p;
	tnf_probe_test_func_t		test_func;

	/* paranoia: thr_probe_setup should be defined */
	assert(thr_probe_setup != 0);
	if (thr_probe_setup != 0) thr_probe_setup(tpd);

	/*
	 * no race with prex if we set flag first
	 *		- this is an idempotent operation
	 */
	__tnf_probe_thr_sync = 1;

#ifdef DEBUG
	{
		char tmp_buf[512];
		(void) sprintf(tmp_buf, "__tnf_probe_notify: \n");
		(void) write(2, tmp_buf, strlen(tmp_buf));
	}
#endif
	/* paranoia: thr_probe_getfunc_addr should be defined */
	test_func = (&thr_probe_getfunc_addr != 0) ?
				tnf_threaded_test : 0;
	assert(test_func);

	/*
	 * I think in this case that we do not need to check the
	 * __tnf_probe_list_valid flag since __tnf_probe_notify is
	 * called very early.
	 */

	/* replace all existing test functions with libthread's test func */
	for (prbctl_p = __tnf_probe_list_head; prbctl_p;
					prbctl_p = prbctl_p->next)
		if (prbctl_p->test_func)
			prbctl_p->test_func = test_func;

	return;

}   /* end __tnf_probe_notify */

/*
 * _tnf_fork_thread_setup - function called by buffering layer
 * whenever it finds a thread in the newly forked process that
 * hasn't been re-initialized in this process.
 */
void
_tnf_fork_thread_setup(void)
{
	tnf_context_t		test_func;
	tnf_ops_t		*ops;

#ifdef DEBUGFUNCS
	{
		char tmp_buf[512];
		(void) sprintf(tmp_buf, "in _tnf_fork_thread_setup: \n");
		(void) write(2, tmp_buf, strlen(tmp_buf));
	}
#endif
	/* get the tpd */
	test_func = (&thr_probe_getfunc_addr != 0) ?
				thr_probe_getfunc_addr : tnf_probe_getfunc;
	ops = test_func();
	if (!ops)
		return;
	/* null out tag_index, so that a new one is initialized and written */
	ops->schedule.record_p = 0;
	return;

}

/* ---------------------------------------------------------------- */
/* ---------------------- Interposed Functions -------------------- */
/* ---------------------------------------------------------------- */

/*
 * thr_create() - this function is interposed in front of the
 * actual thread create function in libthread.
 */

int
thr_create(void 		*stk,
	size_t		stksize,
	void *		(*real_func)(void *),
	void		*real_arg,
	long		flags,
	thread_t	*new_thread)
{
	static thr_create_func_t real_thr_create = NULL;
	args_t *arg_p;

#ifdef VERYVERBOSE
	fprintf(stderr, "hello from the interposed thr_create parent\n");
#endif

	/* use dlsym to find the address of the "real" thr_create function */
	if (real_thr_create == NULL) {
		real_thr_create = (thr_create_func_t)
					dlsym(RTLD_NEXT, "thr_create");
	}
	assert(real_thr_create);

	/* set up the interposed argument block */
	arg_p = (args_t *) malloc(sizeof (args_t));
	assert(arg_p);
	arg_p->real_func = real_func;
	arg_p->real_arg  = real_arg;

	return ((*real_thr_create)(stk, stksize, probestart, (void *) arg_p,
					flags, new_thread));

}   /* end thr_create */


/*
 * function to be interposed in front of _resume.  We invalidate the
 * schedule record in case the lwpid changes the next time this
 * thread is scheduled.
 */

#pragma weak _resume_ret = _tnf_resume_ret
void
_tnf_resume_ret(void *arg1)
{
	static void (*real_resume_ret)(void *) = NULL;
	tnf_context_t	test_func;
	tnf_ops_t	*ops;

	if (real_resume_ret == NULL) {
		real_resume_ret = (void (*)(void *)) dlsym(RTLD_NEXT,
					"_resume_ret");
	}
	assert(real_resume_ret);

	test_func = (&thr_probe_getfunc_addr != 0) ?
			thr_probe_getfunc_addr : tnf_probe_getfunc;
	ops = test_func();
	if (ops) {
		/*
		 * invalidate the schedule record.  This forces it
		 * to get re-initialized with the new lwpid the next
		 * time this thread gets scheduled
		 */
		if (ops->schedule.lwpid != _lwp_self())
			ops->schedule.record_p = 0;
	}

	real_resume_ret(arg1);
}

/*
 * functions to be interposed in front of fork, fork1, and vfork
 */

#pragma weak fork = _tnf_fork
pid_t
_tnf_fork(void)
{
	static fork_t real_fork = NULL;

	if (real_fork == NULL) {
		real_fork = (fork_t) dlsym(RTLD_NEXT, "fork");
	}
	assert(real_fork);
	return (common_fork(real_fork));
}

#pragma weak vfork = _tnf_vfork
pid_t
_tnf_vfork(void)
{
	static fork_t real_fork = NULL;

	if (real_fork == NULL) {
		real_fork = (fork_t) dlsym(RTLD_NEXT, "vfork");
	}
	assert(real_fork);
	return (common_fork(real_fork));
}

#pragma weak fork1 = _tnf_fork1
pid_t
_tnf_fork1(void)
{
	static fork_t real_fork = NULL;

	if (real_fork == NULL) {
		real_fork = (fork_t) dlsym(RTLD_NEXT, "fork1");
	}
	assert(real_fork);
	return (common_fork(real_fork));
}

/* ---------------------------------------------------------------- */
/* ----------------------- Private Functions ---------------------- */
/* ---------------------------------------------------------------- */

/*
 * tnf_probe_getfunc() - default test function if libthread is not
 * present
 */
static tnf_ops_t *
tnf_probe_getfunc(void)
{
	/* test function to be used if libthread is not linked in */
#ifdef DEBUGFUNCS
	{
		char tmp_buf[512];
		(void) sprintf(tmp_buf, "tnf_probe_getfunc: \n");
		(void) write(2, tmp_buf, strlen(tmp_buf));
	}
#endif
	return (tpd);
}   /* end tnf_probe_getfunc */


/*
 * probestart() - this function is called as the start_func by the
 * interposed thr_create().  It calls the real start function.
 */

static void *
probestart(void * arg)
{
	args_t 		*args_p = (args_t *) arg;
	start_func_t	real_func;
	void		*real_arg;
	tnf_ops_t	ops;		/* allocated on stack */
	void		*real_retval;

#ifdef VERYVERBOSE
	fprintf(stderr, "hello from the interposed thr_create child\n");
#endif

	/* initialize ops */
	(void) memset(&ops, 0, sizeof (ops));	/* zero ops */
	ops.mode = TNF_ALLOC_REUSABLE;
	ops.alloc = tnfw_b_alloc;
	ops.commit = tnfw_b_xcommit;
	ops.rollback = tnfw_b_xabort;

	/* copy (and free) the allocated arg block */
	real_func = args_p->real_func;
	real_arg  = args_p->real_arg;
	free(args_p);

	/* paranoia: thr_probe_setup should be defined */
	assert(thr_probe_setup != 0);
	if (thr_probe_setup != 0) thr_probe_setup(&ops);

#ifdef VERYVERBOSE
	fprintf(stderr, "in middle of interposed start procedure\n");
#endif

	real_retval = (*real_func)(real_arg);

	/*
	 * we need to write a NULL into the tpd pointer to disable
	 * tracing for this thread.
	 * CAUTION: never make this function tail recursive because
	 * tpd is allocated on stack.
	 */
	if (thr_probe_setup != 0)
			thr_probe_setup(NULL);

	return (real_retval);

}   /* end probestart */


static thread_key_t tpd_key;
static int key_created = 0;
static tnf_ops_t *stashed_tpd = NULL;

/*
 * tnf_thread_disable: API to disable a thread
 */
void
tnf_thread_disable(void)
{
	tnf_ops_t		*ops;
	static mutex_t		keylock = DEFAULTMUTEX;

	if (thr_probe_setup != 0) {
		/* threaded client */

		if (!key_created) {
			(void) mutex_lock(&keylock);
			if (!key_created) {
				/* REMIND: destructor function ? */
				(void) thr_keycreate(&tpd_key, NULL);
				key_created++;
			}
			(void) mutex_unlock(&keylock);
		}

		/* get the tpd */
		ops = thr_probe_getfunc_addr();
		/* check ops to ensure function is idempotent */
		if (ops != NULL) {
			/* disable the thread */
			thr_probe_setup(NULL);
			/* stash the tpd */
			(void) thr_setspecific(tpd_key, ops);
		}
	} else {
		/* non-threaded client */

		/* get the tpd */
		ops = tnf_probe_getfunc();
		if (ops != NULL) {
			/* disable the process */
			probe_setup(NULL);
			/* stash the tpd */
			stashed_tpd = ops;
		}
	}
}

/*
 * tnf_thread_enable: API to enable a thread
 */
void
tnf_thread_enable(void)
{
	tnf_ops_t *ops;

	if (thr_probe_setup != 0) {
		/* threaded client */

		if (key_created) {
			(void) thr_getspecific(tpd_key, (void *)&ops);
			if (ops) {
				thr_probe_setup(ops);
			}
		}
	} else {
		/* non-threaded client */

		ops = stashed_tpd;
		if (ops) {
			probe_setup(ops);
		}
	}
}

/*
 * common_fork - code that is common among the interpositions of
 * fork, fork1, and vfork
 */
static pid_t
common_fork(fork_t real_fork)
{
	pid_t 		retval;
	tnf_context_t	test_func;
	tnf_ops_t	*ops;
	tnf_tag_data_t	*metatag_data;

#ifdef DEBUGFUNCS
	{
		char tmp_buf[512];
		(void) sprintf(tmp_buf, "in interposed fork: \n");
		(void) write(2, tmp_buf, strlen(tmp_buf));
	}
#endif
	if ((_tnfw_b_control->tnf_state == TNFW_B_NOBUFFER) &&
				(tnf_trace_file_name[0] != '\0')) {
		/*
		 * if no buffer has been allocated yet, and prex plugged in
		 * name...
		 */
		test_func = (&thr_probe_getfunc_addr != 0) ?
			thr_probe_getfunc_addr : tnf_probe_getfunc;
		ops = test_func();
		if (ops == NULL) {
			/*
			 * get it from stashed location
			 * don't enable thread though
			 */
			if (thr_probe_setup != 0) {
				/* threaded client */
				if (key_created) {
					(void) thr_getspecific(tpd_key,
							(void *)&ops);
				}
			} else {
				/* non-threaded client */
				ops = stashed_tpd;
			}
		}

		/*
		 * ops shouldn't be NULL.  But, if it is, then we don't
		 * initialize tracing.  In the child, tracing will be
		 * set to broken.
		 */
		if (ops) {
			/* initialize tracing */
			ops->busy = 1;
			metatag_data = TAG_DATA(tnf_struct_type);
			metatag_data->tag_desc(ops, metatag_data);
			/* commit the data */
			(void) ops->commit(&(ops->wcb));
			ops->busy = 0;
		}
	}

	retval = real_fork();
	if (retval == 0) {
		/* child process */
		_tnfw_b_control->tnf_pid = getpid();
		if ((_tnfw_b_control->tnf_state == TNFW_B_NOBUFFER) &&
				(tnf_trace_file_name[0] != '\0')) {
			/*
			 * race condition, prex attached after condition was
			 * checked in parent, so both parent and child point at
			 * the same file name and will overwrite each other.
			 * So, we set tracing to broken in child.  We could
			 * invent a new state called RACE and use prex to
			 * reset it, if needed...
			 */
			tnf_trace_file_name[0] = '\0';
			_tnfw_b_control->tnf_state = TNFW_B_BROKEN;
		} else if (_tnfw_b_control->tnf_state == TNFW_B_RUNNING) {
			/* normal expected condition */
			_tnfw_b_control->tnf_state = TNFW_B_FORKED;
		}
	}
	return (retval);
}

/*
 * tnf_threaded_test
 */
/*ARGSUSED0*/
static void *
tnf_threaded_test(void *dummy, tnf_probe_control_t *probe_p,
			tnf_probe_setup_t *set_p)
{
	tnf_ops_t *tpd_p;

	tpd_p = thr_probe_getfunc_addr();
	if (tpd_p) {
		return (probe_p->alloc_func(tpd_p, probe_p, set_p));
	}
	return (NULL);
}


/*
 * tnf_non_threaded_test
 */
/*ARGSUSED0*/
static void *
tnf_non_threaded_test(void *dummy, tnf_probe_control_t *probe_p,
				tnf_probe_setup_t *set_p)
{
	tnf_ops_t *tpd_p;

	tpd_p = tnf_probe_getfunc();
	if (tpd_p) {
		return (probe_p->alloc_func(tpd_p, probe_p, set_p));
	}
	return (NULL);
}
