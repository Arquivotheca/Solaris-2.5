#ident	"@(#)lock.c	1.10	95/02/12 SMI"

/*
 *		Copyright (C) 1991  Sun Microsystems, Inc
 *			All rights reserved.
 *		Notice of copyright on this source code
 *		product does not indicate publication.
 *
 *		RESTRICTED RIGHTS LEGEND:
 *   Use, duplication, or disclosure by the Government is subject
 *   to restrictions as set forth in subparagraph (c)(1)(ii) of
 *   the Rights in Technical Data and Computer Software clause at
 *   DFARS 52.227-7013 and in similar clauses in the FAR and NASA
 *   FAR Supplement.
 */

/*
 * This file contains code for the crash functions: mutex, sema, rwlock,
 * and lock.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#ifdef sparc
#include <v7/sys/mutex_impl.h>
#else
#include <sys/mutex_impl.h>
#endif
#include <sys/sema_impl.h>
#include <sys/rwlock_impl.h>
#include <sys/condvar_impl.h>
#include "crash.h"

int
getmutexinfo()
{
	long addr;
	kmutex_t mb;
	int c;

	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}
	if (args[optind]) {
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				continue;
			readbuf(addr, 0, 0, -1, (char *)&mb, sizeof (mb),
								"mutex");
			prmutex(&mb);
		} while (args[optind]);
	} else longjmp(syn, 0);
	return (0);
}

int
prmutex(kmutex_t *m)
{
	unsigned int	type;
	kthread_id_t	owner;
	struct mutex_stats *stats;
	mutex_impl_t	*mp = (mutex_impl_t *)m;

	type = mp->m_generic.m_type / 32;	/* kernel internal conversion */

	switch (type) {
	case	MUTEX_ADAPTIVE:
		fprintf(fp, " waiters %x   lock %x\n",
#if	defined(__ppc)
			mp->m_adaptive.m_waiters, mp->m_adaptive.m_wlock);
#else	/* !__ppc */
			mp->m_adaptive.m_waiters, mp->m_adaptive.m_lock);
#endif
		owner = MUTEX_OWNER(mp);
		if (owner == MUTEX_NO_OWNER)
			owner = NULL;
		fprintf(fp, "\ttype: MUTEX_ADAPTIVE\towner %x\n", owner);
		break;
	case	MUTEX_SPIN:
		fprintf(fp, " minspl %x oldspl %x lock %x\n",
			mp->m_spin.m_minspl, mp->m_spin.m_oldspl,
			mp->m_spin.m_spinlock);
		fprintf(fp, "\ttype: MUTEX_SPIN\n");
		break;
	case	MUTEX_ADAPTIVE_STAT:
		stats = MUTEX_STATS(mp);
		if (stats == MUTEX_NO_STATS)
			stats = NULL;
		fprintf(fp, " type: MUTEX_ADAPTIVE_STAT\tstats %x\n", stats);
		break;
	case	MUTEX_SPIN_STAT:
		stats = MUTEX_STATS(mp);
		if (stats == MUTEX_NO_STATS)
			stats = NULL;
		fprintf(fp, " type: MUTEX_SPIN_STAT\tstats %x\n", stats);
		break;
	}
	return (0);
}


getsemainfo()
{
	long addr;
	int c;
	struct _ksema sb;

	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}
	if (args[optind]) {
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				continue;
			readbuf(addr, 0, 0, -1, (char *)&sb, sizeof (sb),
								"semaphore");
			prsema(&sb);
		} while (args[optind]);
	} else longjmp(syn, 0);
	return (0);
}

int
prsema(struct _ksema *sp)
{
	sema_impl_t *s	= (sema_impl_t *)sp;

	fprintf(fp, "\tcount %d waiting: 0x%x\n",
		s->s_count, s->s_slpq);
	return (0);
}

int
getrwlockinfo()
{
	long addr;
	struct _krwlock rwb;
	int c;

	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}
	if (args[optind]) {
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				continue;
			readbuf(addr, 0, 0, -1, (char *)&rwb, sizeof (rwb),
								"rwlock");
			prrwlock(&rwb);
		} while (args[optind]);
	} else longjmp(syn, 0);
	return (0);
}

int
prrwlock(struct _krwlock *rwp)
{
	rwlock_impl_t	*rp = (rwlock_impl_t *)rwp;

	fprintf(fp, "\ttype %d   waiters %d  owner %8x\n",
		rp->type,
		rp->waiters,
		rp->owner);
	fprintf(fp, "\twritewanted %d   holdcnt %d\n",
		rp->un.rw.writewanted,
		rp->un.rw.holdcnt);
	return (0);
}

int
prcondvar(struct _kcondvar *cvp, char *cv_name)
{
	condvar_impl_t *cv = (condvar_impl_t *)cvp;

	fprintf(fp, "Condition variable %s: %d\n", cv_name, cv->cv_waiters);
	return (0);
}
