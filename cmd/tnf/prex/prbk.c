/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)prbk.c 1.15 95/08/17 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>	/* for strerror() */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/tnf.h>
#include <fcntl.h>
#include <errno.h>

#include "prbutl.h"
#include "prbk.h"

/* The TNF pseudo-device */
#define	TNFDRIVER	"/dev/tnfctl"

/* Dummy "test" function  -- just used to flag enabled probes */
#define	PRBK_DUMMY_TEST	((tnf_probe_test_func_t) 4)

/* Dummy "commit" function -- just used to flag trace enabled */
#define	PRBK_DUMMY_COMMIT ((tnf_probe_func_t) 8)

/* Dummy "rollback" function -- just used to flag trace disabled */
#define	PRBK_DUMMY_ROLLBACK ((tnf_probe_func_t) 12)

/* Dummy "end" function */
#define	PRBK_DUMMY_END ((caddr_t) 16)

/* Dummy "alloc" function */
#define	PRBK_DUMMY_ALLOC ((caddr_t) 20)

/* Minimum and maximum allowed buffer sizes. */
/* XXX -- maximum should be some function of physmem. */
#define	KERNEL_MINBUF_SIZE	(128 * 1024)
#define	KERNEL_MAXBUF_SIZE	(128 * 1024 * 1024)

typedef struct _pidlist {
	pid_t pid;
	struct _pidlist *next;
} pidlist_t;

static struct {
	pidlist_t *pidfilterlist;
	int pidfiltermode;
	int tracingmode;
	int saved_tracingmode;
	int kfd;
} g_kstate;

static boolean_t
check_kernelmode()

{
	extern int g_kernelmode;
	if (!g_kernelmode) {
		(void) fprintf(stderr, "This command is only available in "
			"kernel mode (prex invoked with the -k flag\n");
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Initialize the kernel interface:  Open the TNF control device,
 * and determine the current kernel probes state, including the
 * current pidfilter list.
 */
prb_status_t
prbk_init()

{
	tifiocstate_t kstate;
	int *filterset;
	int i;

	g_kstate.kfd = open(TNFDRIVER, O_RDWR);
	if (g_kstate.kfd < 0)
		return (prb_status_map(errno));
	if (ioctl(g_kstate.kfd, TIFIOCGSTATE, &kstate) < 0)
		return (prb_status_map(errno));
	g_kstate.pidfiltermode = kstate.pidfilter_mode;
	g_kstate.tracingmode = !kstate.trace_stopped;
	g_kstate.saved_tracingmode = !kstate.trace_stopped;
	if (kstate.pidfilter_size == 0)
		return (PRB_STATUS_OK);
	filterset = (int *) malloc((kstate.pidfilter_size + 1) * sizeof (int));
	if (filterset == NULL)
		return (PRB_STATUS_ALLOCFAIL);
	if (ioctl(g_kstate.kfd, TIFIOCPIDFILTERGET, filterset) < 0)
		return (prb_status_map(errno));
	for (i = 1; i <= filterset[0]; ++i)
		g_kstate.pidfilterlist =
		    prbk_pidlist_add(g_kstate.pidfilterlist, filterset[i]);
	(void) free(filterset);
	return (PRB_STATUS_OK);
}


/*
 * Refresh our understanding of the existing probes in the kernel.
 */
prb_status_t
prbk_refresh()

{
	int maxprobe;
	static int old_maxprobe = -1;
	int i;
	prbctlref_t *p, *prev, *new_p;
	tnf_probevals_t probebuf;
	boolean_t new_block;
	extern prbctlref_t *g_head_p;

	if (ioctl(g_kstate.kfd, TIFIOCGMAXPROBE, &maxprobe) < 0)
		return (prb_status_map(errno));
	if (maxprobe == old_maxprobe) {
		/* XXX Inadequate in the presence of module unloading */
		return (PRB_STATUS_OK);
	}

	p = g_head_p;
	prev = NULL;
	for (i = 1; i <= maxprobe; ++i) {
		new_block = B_FALSE;
		probebuf.probenum = i;
		if (ioctl(g_kstate.kfd, TIFIOCGPROBEVALS, &probebuf) < 0) {
			if (errno == ENOENT) {
				if (p != NULL && p->prbk_probe_id == i) {
					/*
					 * This probe has vanished due to a
					 * module unload.  Ditch the control
					 * block.
					 */
					if (prev != NULL)
						prev->next = p->next;
					else
						g_head_p = p->next;
					free(p);
				}
			} else {
				return (prb_status_map(errno));
			}
		} else {
			if (p == NULL || p->prbk_probe_id != i) {
				new_p = (prbctlref_t *)
					calloc(1, sizeof (prbctlref_t));
				if (new_p == NULL)
					return (PRB_STATUS_ALLOCFAIL);
				new_p->next = p;
				if (prev != NULL)
					prev->next = new_p;
				else
					g_head_p = new_p;
				p = new_p;
				new_block = B_TRUE;
			}

			/* Update our info this probe */
			if (probebuf.enabled) {
				p->refprbctl.test_func =
					p->wrkprbctl.test_func =
					PRBK_DUMMY_TEST;
			} else {
				p->refprbctl.test_func =
					p->wrkprbctl.test_func = NULL;
			}
			if (probebuf.traced) {
				p->refprbctl.commit_func =
					p->wrkprbctl.commit_func =
					PRBK_DUMMY_COMMIT;
			} else {
				p->refprbctl.commit_func =
					p->wrkprbctl.commit_func =
					PRBK_DUMMY_ROLLBACK;
			}
			if (new_block) {
				if (probebuf.attrsize < sizeof (probebuf))
					probebuf.attrsize = sizeof (probebuf);
				p->refprbctl.attrs = (char *)
					malloc(probebuf.attrsize);
				if (p->refprbctl.attrs == NULL)
					return (PRB_STATUS_ALLOCFAIL);
/* LINTED pointer cast may result in improper alignment */
				*(tnf_probevals_t *) p->refprbctl.attrs =
				    probebuf;
				if (ioctl(g_kstate.kfd, TIFIOCGPROBESTRING,
						p->refprbctl.attrs) < 0)
					return (prb_status_map(errno));
				p->wrkprbctl.attrs = p->refprbctl.attrs;
				p->prbk_probe_id = i;

			}
		}
		prev = p;
		p = p->next;
	}
	old_maxprobe = maxprobe;
	return (PRB_STATUS_OK);
}


/*
 * "Flush" a probe:  i.e., sync up the kernel state with the
 * (desired) state stored in our data structure.
 */
prb_status_t
prbk_flush(prbctlref_t *p)

{
	tnf_probevals_t probebuf;

	probebuf.probenum = p->prbk_probe_id;
	probebuf.enabled = (p->refprbctl.test_func != NULL);
	probebuf.traced = (p->refprbctl.commit_func == PRBK_DUMMY_COMMIT);
	if (ioctl(g_kstate.kfd, TIFIOCSPROBEVALS, &probebuf) < 0)
		return (prb_status_map(errno));
	return (PRB_STATUS_OK);
}


/*
 * Fill in g_testfunc with a dummy "test" function.
 */
prb_status_t
prbk_test_func(caddr_t *outp)

{
	*outp = (caddr_t) PRBK_DUMMY_TEST;
	return (PRB_STATUS_OK);
}

void
prbk_set_other_funcs(caddr_t *allocp, caddr_t *commitp,
	caddr_t *rollbackp, caddr_t *endp)

{
	*allocp = PRBK_DUMMY_ALLOC;
	*commitp = (caddr_t) PRBK_DUMMY_COMMIT;
	*rollbackp = (caddr_t) PRBK_DUMMY_ROLLBACK;
	*endp = (caddr_t) PRBK_DUMMY_END;
}


/*
 * Print trace buffer status (is one allocated, and if so, how big is it.
 */
void
prbk_buffer_list()

{
	tifiocstate_t bufstat;

	if (check_kernelmode())
		return;
	if (ioctl(g_kstate.kfd, TIFIOCGSTATE, &bufstat) < 0) {
		/* XXX */
		(void) fprintf(stderr, "TIFIOCGSTATE: errno = %d\n", errno);
	}
	if (bufstat.buffer_state == TIFIOCBUF_NONE)
		(void) printf("No trace buffer allocated\n");
	else {
		(void) printf("Trace buffer size is %d bytes\n",
			bufstat.buffer_size);
		if (bufstat.buffer_state == TIFIOCBUF_UNINIT) {
			(void) printf(
				"(No probes ever written to this buffer)\n");
		} else if (bufstat.buffer_state == TIFIOCBUF_BROKEN) {
			(void) printf("Tracing system has failed -- "
				"tracing suspended\n");
		}
	}
}


/*
 * Allocate a trace buffer.  Check for reasonable size; reject if there's
 * already a buffer.
 */
void
prbk_buffer_alloc(int size)

{
	tifiocstate_t bufstat;

	if (check_kernelmode())
		return;
	if (ioctl(g_kstate.kfd, TIFIOCGSTATE, &bufstat) < 0) {
		/* XXX */
		(void) fprintf(stderr, "TIFIOCGSTATE: errno = %d\n", errno);
	}
	if (bufstat.buffer_state != TIFIOCBUF_NONE) {
		(void) fprintf(stderr, "There is already a buffer allocated\n");
		return;
	}
	if (size < KERNEL_MINBUF_SIZE) {
		(void) fprintf(stderr,
			"Size %d is less than the minimum buffer size of %d -- "
			"buffer size set to %d bytes\n",
			size, KERNEL_MINBUF_SIZE, KERNEL_MINBUF_SIZE);
		size = KERNEL_MINBUF_SIZE;
	} else if (size > KERNEL_MAXBUF_SIZE) {
		(void) fprintf(stderr,
			"Size %d is greater than the maximum buffer size of %d"
			" -- buffer size set to %d bytes\n",
			size, KERNEL_MAXBUF_SIZE, KERNEL_MAXBUF_SIZE);
		size = KERNEL_MAXBUF_SIZE;
	}
	if (ioctl(g_kstate.kfd, TIFIOCALLOCBUF, size) < 0) {
		perror("TIFIOCALLOCBUF");
	} else {
		(void) printf("Buffer of size %d bytes allocated\n", size);
	}
}


/*
 * Deallocate the kernel's trace buffer.
 */
void
prbk_buffer_dealloc()

{
	tifiocstate_t bufstat;

	if (check_kernelmode())
		return;
	if (ioctl(g_kstate.kfd, TIFIOCGSTATE, &bufstat) < 0) {
		perror("TIFIOCGSTATE");
		return;
	}
	if (bufstat.buffer_state == TIFIOCBUF_NONE) {
		(void) fprintf(stderr, "There is no buffer to deallocate\n");
		return;
	}
	if (bufstat.buffer_state == TIFIOCBUF_OK &&
	    !bufstat.trace_stopped) {
		(void) fprintf(stderr, "Can't deallocate the buffer when "
			"tracing is active\n");
		return;
	}
	if (ioctl(g_kstate.kfd, TIFIOCDEALLOCBUF) < 0) {
		perror("TIFIOCDEALLOCBUF");
	} else {
		(void) printf("buffer deallocated\n");
	}
}


/*
 * Process filter routines.
 *
 * Process id sets are encoded as "pidlists":  a linked list of pids.
 * In a feeble attempt at encapsulation, the pidlist_t type is private
 * to this file; prexgram.y manipulates pidlists only as opaque handles.
 */

/*
 * Add the given pid (new) to the pidlist (pl).
 */
void *
prbk_pidlist_add(void *pl, int new)

{
	pidlist_t *npl = (pidlist_t *) malloc(sizeof (*npl));
	if (npl == NULL) {
		(void) fprintf(stderr,
			"Out of memory -- can't process pid %d\n",
			new);
		return (pl);
	}
	npl->next = pl;
	npl->pid = new;
	return (npl);
}

/*
 * Add the pids in the given pidlist to the process filter list.
 * For each pid, check whether it's already in the filter list,
 * and whether the process exists.
 */
void
prbk_pfilter_add(void *pl)

{
	pidlist_t *ppl = (pidlist_t *) pl;
	pidlist_t *cur;
	pidlist_t *prev;
	pidlist_t *tmp;
	if (check_kernelmode())
		return;
	while (ppl != NULL) {
		cur = g_kstate.pidfilterlist;
		prev = NULL;
		while (cur != NULL && cur->pid < ppl->pid) {
			prev = cur;
			cur = cur->next;
		}
		tmp = ppl;
		ppl = ppl->next;
		if (cur != NULL && cur->pid == tmp->pid) {
			(void) fprintf(stderr,
				"Process %ld is already being traced\n",
				tmp->pid);
			free(tmp);
		} else if (ioctl(g_kstate.kfd, TIFIOCSPIDON, tmp->pid) < 0) {
			(void) fprintf(stderr, "Process %ld: %s\n",
				tmp->pid, strerror(errno));
			free(tmp);
		} else {
			tmp->next = cur;
			if (prev != NULL)
				prev->next = tmp;
			else
				g_kstate.pidfilterlist = tmp;
		}
	}
}

/*
 * Drop the pids in the given pidlist from the process filter list.
 * For each pid, complain if it's not in the process filter list;
 * and if the process no longer exists (and hence has already implicitly
 * been dropped from the process filter list), say so.
 */
void
prbk_pfilter_drop(void *pl)

{
	pidlist_t *ppl = (pidlist_t *) pl;
	pidlist_t *cur;
	pidlist_t *prev;
	pidlist_t *tmp;
	if (check_kernelmode())
		return;
	while (ppl != NULL) {
		cur = g_kstate.pidfilterlist;
		prev = NULL;
		while (cur != NULL && cur->pid < ppl->pid) {
			prev = cur;
			cur = cur->next;
		}
		tmp = ppl;
		ppl = ppl->next;
		if (cur == NULL || cur->pid > tmp->pid) {
			(void) fprintf(stderr,
				"Process %ld is not being traced\n",
				tmp->pid);
			free(tmp);
			continue;
		} else if (ioctl(g_kstate.kfd, TIFIOCSPIDOFF, tmp->pid) < 0) {
			if (errno == ESRCH)
				(void) printf("Process %ld has exited\n",
					tmp->pid);
			else
				(void) fprintf(stderr, "Process %ld: %s\n",
					tmp->pid, strerror(errno));
		}
		free(tmp);
		if (prev != NULL)
			prev->next = cur->next;
		else
			g_kstate.pidfilterlist = cur->next;
		free(cur);
	}
}

/*
 * Sync up prex's notion of the process filter list with the kernel's.
 * Currently, this means finding out about processes that were deleted
 * from the kernel's process filter list automatically because the process
 * exited.
 */
void
prbk_pfilter_sync()

{
	pidlist_t *pl = g_kstate.pidfilterlist;
	pidlist_t *prev = NULL;
	pidlist_t *tmp;
	int pid_and_reslt;

	while (pl != NULL) {
		pid_and_reslt = pl->pid;
		if (ioctl(g_kstate.kfd, TIFIOCGPIDSTATE, &pid_and_reslt) < 0) {
			(void) printf("Process %ld has exited, deleted "
				"from process filter\n",
				pl->pid);
			if (prev != NULL)
				prev->next = pl->next;
			else
				g_kstate.pidfilterlist = pl->next;
			tmp = pl;
			pl = pl->next;
			free(tmp);
		} else {
			prev = pl;
			pl = pl->next;
			/* Assert(pid_and_reslt) != 0; */
		}
	}
}

/*
 * Turn process filter mode on or off.  The process filter is maintained
 * even when process filtering is off, but has no effect:  all processes
 * are traced.
 */
void
prbk_set_pfilter_mode(int onoff)

{
	if (check_kernelmode())
		return;
	if (ioctl(g_kstate.kfd, TIFIOCSPIDFILTER, onoff) < 0) {
		(void) fprintf(stderr, "pfilter: %s\n",
			strerror(errno));
	}
	g_kstate.pidfiltermode = onoff;
}


/*
 * Report whether process filter mode is currently on or off, and
 * dump the current process filter set.
 */
void
prbk_show_pfilter_mode()

{
	pidlist_t *pl;

	if (check_kernelmode())
		return;
	prbk_pfilter_sync();
	(void) printf("Process filtering is %s\n",
		g_kstate.pidfiltermode ? "on" : "off");
	(void) printf("Process filter set is ");
	pl = g_kstate.pidfilterlist;
	if (pl == NULL)
		(void) printf("empty.\n");
	else {
		(void) printf("{");
		while (pl != NULL) {
			(void) printf("%ld%s", pl->pid,
			    pl->next != NULL ? ", " : "}\n");
			pl = pl->next;
		}
	}
}


/*
 * Turn kernel tracing on or off.  Defer the actual operation until
 * we have re-sync'ed with the kernel.  That way, we don't turn
 * tracing on until our list of known (and possibly enabled) probes
 * is up to date.
 */
void
prbk_set_tracing(int onoff)

{
	if (check_kernelmode())
		return;
	g_kstate.saved_tracingmode = g_kstate.tracingmode;
	g_kstate.tracingmode = onoff;
}

/*
 * Show whether kernel tracing is currently on or off.
 */
void
prbk_show_tracing()

{
	if (check_kernelmode())
		return;
	(void) printf("Tracing is %s\n", g_kstate.tracingmode ? "on" : "off");
}


/*
 * Sync up the tracing state in the kernel with that specified by
 * the user; see the comment for prbk_set_tracing().
 */
prb_status_t
prbk_tracing_sync()

{
	if (g_kstate.saved_tracingmode != g_kstate.tracingmode &&
	    ioctl(g_kstate.kfd, TIFIOCSTRACING, g_kstate.tracingmode) < 0) {
		if (errno == ENOMEM && g_kstate.tracingmode)
			(void) fprintf(stderr,
				"Error:  cannot enable tracing without "
				"allocating a trace buffer.\n");
		else
			perror("TIFIOCSTRACING");
		g_kstate.tracingmode = g_kstate.saved_tracingmode;
		return (prb_status_map(errno));
	}
	g_kstate.saved_tracingmode = g_kstate.tracingmode;
	return (PRB_STATUS_OK);
}
