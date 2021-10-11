/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sm_proc.c	1.13	95/01/13 SMI"	/* SVr4.0 1.2	*/
/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 * 		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986,1987,1988,1989,1994  Sun Microsystems, Inc.
 *  	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <rpc/rpc.h>
#include <rpcsvc/sm_inter.h>
#include <memory.h>
#include "sm_statd.h"

extern int debug;
int local_state;		/* fake local sm state */
int remote_state = 3; 		/* fake remote sm state for testing */

#define	SM_TCP_TIMEOUT 0

struct mon_entry {
	mon id;
	struct mon_entry *prev;
	struct mon_entry *nxt;
};
typedef struct mon_entry mon_entry;
struct mon_entry *monitor_q;

static void delete_mon(char *mon_name, my_id *my_idp);
static void insert_mon(mon *monp);
static void pr_mon(void);
static int statd_call_lockd(mon *monp, int state);

/* ARGSUSED */
sm_stat_res *
sm_stat_1(namep, clnt)
	sm_name *namep;
	CLIENT *clnt;
{
	static sm_stat_res resp;

	if (debug)
		printf("proc sm_stat: mon_name = %s\n", namep);

	/* fake answer */
	resp.res_stat = stat_fail;
	resp.state = -1;
	return (&resp);
}

/* ARGSUSED */
sm_stat_res *
sm_mon_1(monp, clnt)
	mon *monp;
	CLIENT *clnt;
{
	static sm_stat_res resp;
	mon_id *monidp;
	monidp = &monp->mon_id;

	if (debug)
		printf("proc sm_mon: mon_name = %s, id = %d\n",
		monidp->mon_name, * ((int *)monp->priv));
	/* store monitor request into monitor_q */
	insert_mon(monp);
	pr_mon();
	resp.res_stat = stat_succ;
	resp.state = local_state;
	return (&resp);
}

/* ARGSUSED */
sm_stat *
sm_unmon_1(monidp, clnt)
	mon_id *monidp;
	CLIENT *clnt;
{
	static sm_stat resp;

	if (debug)
		printf("proc sm_unmon: mon_name = %s, [%s, %d, %d, %d]\n",
		monidp->mon_name, monidp->my_id.my_name,
		monidp->my_id.my_prog, monidp->my_id.my_vers,
		monidp->my_id.my_proc);
	delete_mon(monidp->mon_name, &monidp->my_id);
	pr_mon();
	resp.state = local_state;
	return (&resp);
}

/* ARGSUSED */
sm_stat *
sm_unmon_all_1(myidp, clnt)
	my_id *myidp;
	CLIENT *clnt;
{
	static sm_stat resp;

	if (debug)
		printf("proc sm_unmon_all: [%s, %d, %d, %d]\n",
		myidp->my_name,
		myidp->my_prog, myidp->my_vers,
		myidp->my_proc);
	/*
	 * XXX Should not be here
	 * Removing it from monitor_q will break the recovery mechanism
	 */
/*
	delete_mon((char *)NULL, myidp);
*/

	pr_mon();
	resp.state = local_state;
	return (&resp);
}

/* ARGSUSED */
void *
sm_simu_crash_1(myidp, clnt)
	void *myidp;
	CLIENT *clnt;
{
	if (debug)
		printf("proc sm_simu_crash\n");
	if (monitor_q != (struct mon_entry *)NULL)
		sm_crash();
}


/*
 * Insert an entry into the monitor_q.  Space for the entry is allocated
 * here.  It is then filled in from the information passed in.
 * N.B. The information passed in contains two character strings.  These
 *	are used without being copied.  Care should be taken not to free
 *	them.
 */
static void
insert_mon(monp)
	mon *monp;
{
	mon_entry *new, *next;
	my_id *my_idp, *nl_idp;

	if ((new = (mon_entry *) malloc(sizeof (mon_entry))) == 0) {
		syslog(LOG_ERR,
			"statd: insert_mon: malloc error on mon %s (id=%d)\n",
			monp->mon_id.mon_name, * ((int *)monp->priv));
		return;
	}
	(void) memset(new, 0, sizeof (mon_entry));
	new->id = *monp;
	/*
	 * Only the pointers to the string and not the string itself are
	 * copied from the parameter to the new data structure.  For that,
	 * reason the xdr arguments for SM_MON command are specifically not
	 * freed when this eventually returns to sm_prog_1.  Since monp is
	 * used for the next rpc, the character pointers must be set to
	 * NULL so xdr_string will allocate new data rather than
	 * overwriting the string stored here.
	 * XXX it would probably be better to do this assignment up in
	 * XXX sm_prog_1 where the special case to not free the arguments
	 * XXX for the SM_MON command is located.
	 */
	monp->mon_id.mon_name = (char *)NULL;
	monp->mon_id.my_id.my_name = (char *)NULL;

	if (debug)
		printf("add_mon(%x) %s (id=%d)\n",
		new, new->id.mon_id.mon_name, * ((int *)new->id.priv));

	record_name(new->id.mon_id.mon_name, 1);
	if (monitor_q == (struct mon_entry *)NULL) {
		new->nxt = new->prev = (mon_entry *)NULL;
		monitor_q = new;
		return;
	} else {
		next = monitor_q;
		my_idp = &monp->mon_id.my_id;
		while (next != (mon_entry *)NULL)  {
			/* look for other mon_name */
			if (strncmp(next ->id.mon_id.mon_name,
				new->id.mon_id.mon_name,
				strcspn(next->id.mon_id.mon_name,
				".")) == 0) {
				/* found */
				nl_idp = &next->id.mon_id.my_id;
				if (strncmp(new->id.mon_id.my_id.my_name,
					nl_idp->my_name,
					strcspn(nl_idp->my_name, ".")) == 0 &&
					my_idp->my_prog == nl_idp->my_prog &&
					my_idp->my_vers == nl_idp->my_vers &&
					my_idp->my_proc == nl_idp->my_proc) {
					/* already exists an identical one */
					free(new->id.mon_id.mon_name);
					free(new->id.mon_id.my_id.my_name);
					free(new);
					return;
				} else {
					new->nxt = next->nxt;
					if (next->nxt != (mon_entry *)NULL)
						next->nxt->prev = new;
					next->nxt = new;
					return;
				}
			}
			next = next->nxt;
		}
		/* not found */
		new->nxt = monitor_q;
		if (new->nxt != (mon_entry *)NULL)
			new->nxt->prev = new;
		monitor_q = new;
		return;
	}
}

static void
delete_mon(mon_name, my_idp)
	char *mon_name;
	my_id *my_idp;
{
	struct mon_entry *next, *nl;
	my_id *nl_idp;

	if (mon_name != (char *)NULL)
		record_name(mon_name, 0);

	next = monitor_q;
	while ((nl = next) != (struct mon_entry *)NULL) {
		next = next->nxt;
		if (mon_name == (char *)NULL || (mon_name != (char *)NULL &&
			strncmp(nl ->id.mon_id.mon_name, mon_name,
			strcspn(mon_name, ".")) == 0)) {
			nl_idp = &nl->id.mon_id.my_id;
			if (strncmp(my_idp->my_name, nl_idp->my_name,
				strcspn(my_idp->my_name, ".")) == 0 &&
				my_idp->my_prog == nl_idp->my_prog &&
				my_idp->my_vers == nl_idp->my_vers &&
				my_idp->my_proc == nl_idp->my_proc) {
				/* found */
				if (debug)
					printf("delete_mon(%x): %s\n",
						nl, mon_name);
				if (nl->prev != (struct mon_entry *)NULL)
					nl->prev->nxt = nl->nxt;
				else {
					monitor_q = nl->nxt;
				}
				if (nl->nxt != (struct mon_entry *)NULL)
					nl->nxt->prev = nl->prev;
				free(nl->id.mon_id.mon_name);
				free(nl_idp->my_name);
				free(nl);
			}
		} /* end of if mon */
	}
}

void
send_notice(mon_name, state)
	char *mon_name;
	int state;
{
	struct mon_entry *next;

	next = monitor_q;
	while (next != (struct mon_entry *)NULL) {
		if (strncmp(next->id.mon_id.mon_name, mon_name,
			strcspn(mon_name, ".")) == 0) {
			if (statd_call_lockd(&next->id, state) == -1) {
				if (debug && mon_name)
					printf(
				"problem with notifying %s failure, give up\n",
						mon_name);
			} else {
				if (debug)
					printf(
				"send_notice: %s, %d notified for 0x%x\n",
						mon_name, state, next);
#if 0
				/* XXX should flush this? */
				next->id.mon_id.mon_state = state;
#endif
			}
		}
		next = next->nxt;
	}
}

static int
statd_call_lockd(monp, state)
	mon *monp;
	int state;
{
	struct status stat;
	my_id *my_idp;
	char *mon_name;
	int i, err;

	mon_name = monp->mon_id.mon_name;
	my_idp = &monp->mon_id.my_id;
	(void) memset(&stat, 0, sizeof (struct status));
	stat.mon_name = mon_name; /* may be dangerous */
	stat.state = state;
	for (i = 0; i < 16; i++) {
		stat.priv[i] = monp->priv[i];
	}
	if (debug)
		printf("statd_call_lockd: %s state = %d\n",
		stat.mon_name, stat.state);
	if ((err = call_rpc(my_idp->my_name, 0, my_idp->my_prog,
		my_idp->my_vers, my_idp->my_proc, xdr_status, &stat, xdr_void,
		NULL, 1, SM_TCP_TIMEOUT)) != (int) RPC_SUCCESS &&
		err != (int) RPC_TIMEDOUT) {
		syslog(LOG_ERR,
	    "statd: cannot contact local lockd on %s status change,give up!\n",
			mon_name);
			return (-1);
		}
	else
		return (0);
}

static void
pr_mon()
{
	mon_entry *nl;

	if (!debug)
		return;
	if (monitor_q == (struct mon_entry *)NULL) {
		printf("*****monitor_q = NULL\n");
		return;
	}

	nl = monitor_q;
	printf("*****monitor_q: ");
	while (nl != (mon_entry *)NULL) {
		printf("(%x), ", nl);
		nl = nl->nxt;
	}
	printf("\n");
}
