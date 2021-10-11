/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)ddi.c	1.72	94/12/05 SMI"	/* from SVr4.0 1.39 */

/*
 * UNIX Device Driver Interface functions
 *
 * This file contains functions that are to be added to the kernel
 * to put the interface presented to drivers in conformance with
 * the DDI standard. Of the functions added to the kernel, 17 are
 * function equivalents of existing macros in sysmacros.h, map.h,
 * stream.h, and param.h
 *
 * 17 additional functions --  drv_getparm(), drv_setparm(), setmapwant(),
 * getrbuf(), freerbuf(),
 * getemajor(), geteminor(), etoimajor(), itoemajor(), drv_usectohz(),
 * drv_hztousec(), drv_usecwait(), drv_priv(), kvtoppid() and pollwakeup()
 * -- are specified by DDI to exist in the kernel and are implemented here.
 *
 * Note that putnext() and put() are not in this file. The C version of
 * these routines are in uts/common/os/putnext.c and assembly versions
 * might exist for some architectures.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/map.h>
#include <sys/signal.h>
#include <sys/pcb.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/cmn_err.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/uio.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/poll.h>
#include <sys/session.h>
#include <sys/ddi.h>
#include <sys/mkdev.h>
#include <sys/debug.h>
#include <sys/vtrace.h>

/*
 * local version of RD(), WR(), SAMESTR()
 */
#define	_RD(qp)	((qp)-1)
#define	_WR(qp)	((qp)+1)
#define	_SAMESTR(q)	(!((q)->q_flag & QEND))

/*
 * function: getmajor()
 * macro in: sysmacros.h
 * purpose:  return internal major number corresponding to device
 *	number (new format) argument
 */

major_t
getmajor(register dev_t dev)
{
	return ((major_t)((dev>>NBITSMINOR) & MAXMAJ));
}




/*
 * function: getemajor()
 * macro in: sysmacros.h
 * purpose:  return external major number corresponding to device
 *	number (new format) argument
 */

major_t
getemajor(register dev_t dev)
{
	return ((major_t)((dev>>NBITSMINOR) & MAXMAJ));
}



/*
 * function: getminor()
 * macro in: sysmacros.h
 * purpose:  return internal minor number corresponding to device
 *	number (new format) argument
 */

minor_t
getminor(register dev_t dev)
{
	return ((minor_t)(dev & MAXMIN));
}



/*
 * function: geteminor()
 * macro in: sysmacros.h
 * purpose:  return external minor number corresponding to device
 *	number (new format) argument
 */

minor_t
geteminor(register dev_t dev)
{
	return ((minor_t)(dev & MAXMIN));
}


/*
 * function: etoimajor()
 * purpose:  return internal major number corresponding to external
 *	major number argument or -1 if the external major number
 *	exceeds the bdevsw and cdevsw count
 */

int
etoimajor(register major_t emajnum)
{
	if ((int)emajnum > MAXMAJ || (int)emajnum >= devcnt)
		return (-1); /* invalid external major */

	return ((int)emajnum);
}



/*
 * function: itoemajor()
 * purpose:  return external major number corresponding to internal
 *	major number argument or -1 if no external major number
 *	can be found after lastemaj that maps to the internal
 *	major number. Pass a lastemaj val of -1 to start
 *	the search initially. (Typical use of this function is
 *	of the form:
 *
 *		lastemaj = -1;
 *		while ((lastemaj = itoemajor(imag, lastemaj)) != -1)
 *			{ process major number }
 */

int
itoemajor(register major_t imajnum, int lastemaj)
{
	if (imajnum >= devcnt)
		return (-1);

	/*
	 * if lastemaj == -1 then start from beginning of
	 * the (imaginary) MAJOR table
	 */
	if (lastemaj < -1)
		return (-1);

#ifdef notdef
	/*
	 * increment lastemaj and search for MAJOR table entry that
	 * equals imajnum. return -1 if none can be found.
	 */
	while (++lastemaj < NMAJORENTRY)
	    if (imajnum == (int)MAJOR[lastemaj])
		return (lastemaj);
#else
	/*
	 * given that there's a 1-1 mapping of internal to external
	 * major numbers, searching is somewhat pointless ... let's
	 * just go there directly.
	 */
	if (++lastemaj < devcnt && imajnum < devcnt)
		return (imajnum);
#endif
	return (-1);
}



/*
 * function: makedevice()
 * macro in: sysmacros.h
 * purpose:  encode external major and minor number arguments into a
 *	new format device number
 */

dev_t
makedevice(register major_t maj, register minor_t minor)
{
	return ((dev_t)((maj<<NBITSMINOR) | (minor&MAXMIN)));
}

/* cmpdev - compress new device format to old device format */

o_dev_t
cmpdev(register dev_t dev)
{
	major_t major_d;
	minor_t minor_d;

	major_d = (dev >> NBITSMINOR);
	minor_d = (dev & MAXMIN);
	if (major_d > OMAXMAJ || minor_d > OMAXMIN)
		return (NODEV);
	return (((major_d << ONBITSMINOR) | minor_d));
}


dev_t
expdev(register dev_t dev)
{
	major_t major_d;
	minor_t minor_d;

	major_d = ((dev >> ONBITSMINOR) & OMAXMAJ);
	minor_d = (dev & OMAXMIN);
	return (((major_d << NBITSMINOR) | minor_d));
}


/*
 * function: datamsg()
 * macro in: stream.h
 * purpose:  return true (1) if the message type input is a data
 *	message type, 0 otherwise
 */
#undef datamsg
int
datamsg(register unsigned char db_type)
{
	return (db_type == M_DATA || db_type == M_PROTO ||
		db_type == M_PCPROTO || db_type == M_DELAY);
}


/*
 * function: OTHERQ()
 * macro in: stream.h
 * purpose:  return a pointer to the other queue in the queue pair
 *	of qp
 */

queue_t *
OTHERQ(register queue_t *q)
{
	return ((q)->q_flag&QREADR? (q)+1: (q)-1);
}




/*
 * function: RD()
 * macro in: stream.h
 * purpose:  return a pointer to the read queue in the queue pair
 *	of qp.
 */

queue_t *
RD(register queue_t *q)
{
	if (q->q_flag & QREADR)
		return (q);
	else
		return (q-1);
}


/*
 * function: SAMESTR()
 * macro in: strsubr.h
 * purpose:  return a pointer to the write queue in the queue pair
 *	of qp.
 */

int
SAMESTR(register queue_t *q)
{
	return (_SAMESTR(q));
}


/*
 * function: splstr()
 * macro in: stream.h
 * purpose:  set spl to protect critical regions of streams code
 */

int
splstr(void)
{
	/* declare in some header file ??? */
	extern int spltty(void);

	return (spltty());
}


/*
 * function: WR()
 * macro in: stream.h
 * purpose:  return a pointer to the write queue in the queue pair
 *	of qp.
 */

queue_t *
WR(register queue_t *q)
{
	if (q->q_flag & QREADR)
		return (q + 1);
	else
		return (q);
}

/*
 * function: drv_getparm()
 * purpose:  store value of kernel parameter associated with parm in
 *	valuep and return 0 if parm is UPROCP, PPGRP,
 *	LBOLT, PPID, PSID, TIME, UCRED; return -1 otherwise
 */

int
drv_getparm(register unsigned long parm, register unsigned long *valuep)
{
	switch (parm) {
	case UPROCP:
		*valuep = (unsigned long)ttoproc(curthread);
		break;
	case PPGRP:
		*valuep = (unsigned long)ttoproc(curthread)->p_pgrp;
		break;
	case LBOLT:
		*valuep = (unsigned long)lbolt;
		break;
	case TIME:
		if (hrestime.tv_sec == 0) {
			timestruc_t ts;
			mutex_enter(&tod_lock);
			ts = tod_get();
			mutex_exit(&tod_lock);
			*valuep = (unsigned long)ts.tv_sec;
		} else {
			*valuep = (unsigned long)hrestime.tv_sec;
		}
		break;
	case PPID:
		*valuep = (unsigned long)ttoproc(curthread)->p_pid;
		break;
	case PSID:
		*valuep = (unsigned long)ttoproc(curthread)->p_sessp->s_sid;
		break;
	case UCRED:
		*valuep = (unsigned long)CRED();
		break;
	default:
		return (-1);
	}

	return (0);
}



/*
 * function: drv_setparm()
 * purpose:  set value of kernel parameter associated with parm to
 *	value and return 0 if parm is SYSRINT, SYSXINT,
 *	SYSMINT, SYSRAWC, SYSCANC, SYSOUTC; return -1 otherwise;
 *	we splhi()/splx() to make this operation atomic
 */


int
drv_setparm(register unsigned long parm, register unsigned long value)
{
	switch (parm) {
	case SYSRINT:
		CPU_STAT_ADD(CPU, cpu_sysinfo.rcvint, value);
		break;
	case SYSXINT:
		CPU_STAT_ADD(CPU, cpu_sysinfo.xmtint, value);
		break;
	case SYSMINT:
		CPU_STAT_ADD(CPU, cpu_sysinfo.mdmint, value);
		break;
	case SYSRAWC:
		CPU_STAT_ADD(CPU, cpu_sysinfo.rawch, value);
		break;
	case SYSCANC:
		CPU_STAT_ADD(CPU, cpu_sysinfo.canch, value);
		break;
	case SYSOUTC:
		CPU_STAT_ADD(CPU, cpu_sysinfo.outch, value);
		break;
	default:
		return (-1);
	}

	return (0);
}

/*
 * function: rmsetwant()
 * purpose:  increment the count on the wait flag (mp->m_want) of
 *	map pointed to by mp
 *
 * XXX	See 1095029 - This routine is on the demolition list.
 *	Use rmalloc_wait(9F) instead.
 */

void
rmsetwant(register struct map *mp)
{
	mutex_enter(&maplock(mp));
	mapwant(mp)++;
	mutex_exit(&maplock(mp));
}


/*
 * function: getrbuf
 * purpose:  allocate space for buffer header and return pointer to it.
 *	preferred means of obtaining space for a local buf header.
 *	returns pointer to buf upon success, NULL for failure
 */

struct buf *
getrbuf(register long sleep)
{

	register struct buf *bp;

	bp = kmem_alloc(sizeof (struct buf), sleep);
	if (bp == NULL)
		return (NULL);

	/* zero out buffer header storage allocated */
	struct_zero((caddr_t)bp, sizeof (struct buf));

	/* Initialize semaphores */
	sema_init(&bp->b_sem, 0, "bp semaphore",
				SEMA_DEFAULT, NULL);
	sema_init(&bp->b_io, 0, "bp io semaphore",
				SEMA_DEFAULT, NULL);
	return (bp);
}


/*
 * function: freerbuf
 * purpose:  free up space allocated by getrbuf()
 */

void
freerbuf(struct buf *bp)
{
	kmem_free((void *)bp, sizeof (struct buf));
}

/*
 * function: btop
 * macro in: param.h
 * purpose:  convert byte count input to logical page units
 *	(byte counts that are not a page-size multiple
 *	are rounded down)
 */

unsigned long
btop(register unsigned long numbytes)
{
	return (numbytes >> PAGESHIFT);
}



/*
 * function: btopr
 * macro in: param.h
 * purpose:  convert byte count input to logical page units
 *	(byte counts that are not a page-size multiple
 *	are rounded up)
 */

unsigned long
btopr(register unsigned long numbytes)
{
	return ((numbytes+PAGEOFFSET) >> PAGESHIFT);
}



/*
 * function: ptob
 * macro in: param.h
 * purpose:  convert size in pages to to bytes.
 */

unsigned long
ptob(register unsigned long numpages)
{
	return (numpages << PAGESHIFT);
}



#define	MAXCLOCK_T 0x7FFFFFFF

/*
 * function: drv_hztousec
 * purpose:  convert from system time units (given by parameter HZ)
 *	to microseconds. This code makes no assumptions about the
 *	relative values of HZ and ticks and is intended to be
 *	portable.
 *
 *	A zero or lower input returns 0, otherwise we use the formula
 *	microseconds = (hz/HZ) * 1000000. To minimize overflow
 *	we divide first and then multiply. Note that we want
 *	upward rounding, so if there is any fractional part,
 *	we increment the return value by one. If an overflow is
 *	detected (i.e.  resulting value exceeds the
 *	maximum possible clock_t, then truncate
 *	the return value to MAXCLOCK_T.
 *
 *	No error value is returned.
 *
 *	This function's intended use is to remove driver object
 *	file dependencies on the kernel parameter HZ.
 *	many drivers may include special diagnostics for
 *	measuring device performance, etc., in their ioctl()
 *	interface or in real-time operation. This function
 *	can express time deltas (i.e. lbolt - savelbolt)
 *	in microsecond units.
 *
 */

clock_t
drv_hztousec(register clock_t ticks)
{
	clock_t quo, rem;
	register clock_t remusec, quousec;

	if (ticks <= 0)
		return (0);

	quo = ticks / HZ; /* number of seconds */
	rem = ticks % HZ; /* fraction of a second */
	quousec = 1000000 * quo; /* quo in microseconds */
	remusec = 1000000 * rem; /* remainder in millionths of HZ units */

	/* check for overflow */
	if (quo != quousec / 1000000)
		return (MAXCLOCK_T);
	if (rem != remusec / 1000000) remusec = MAXCLOCK_T;

	/* adjust remusec since it was in millionths of HZ units */
	remusec = (remusec % HZ) ? remusec/HZ + 1 : remusec/HZ;

	/*
	 * check for overflow again. If sum of quousec and remusec
	 * would exceed MAXCLOCK_T then return MAXCLOCK_T
	 */

	if ((MAXCLOCK_T - quousec) < remusec)
		return (MAXCLOCK_T);

	return (quousec + remusec);
}


/*
 * function: drv_usectohz
 * purpose:  convert from microsecond time units to system time units
 *	(given by parameter HZ) This code also makes no assumptions
 *	about the relative values of HZ and ticks and is intended to
 *	be portable.
 *
 *	A zero or lower input returns 0, otherwise we use the formula
 *	hz = (microsec/1000000) * HZ. Note that we want
 *	upward rounding, so if there is any fractional part,
 *	we increment by one. If an overflow is detected, then
 *	the maximum clock_t value is returned. No error value
 * 	is returned.
 *
 *	The purpose of this function is to allow driver objects to
 *	become independent of system parameters such as HZ, which
 *	may change in a future release or vary from one machine
 *	family member to another.
 */

clock_t
drv_usectohz(register clock_t microsecs)
{
	clock_t quo, rem;
	register clock_t remhz, quohz;

	if (microsecs <= 0)
		return (0);

	quo = microsecs / 1000000; /* number of seconds */
	rem = microsecs % 1000000; /* fraction of a second */
	quohz = HZ * quo; /* quo in HZ units */
	remhz = HZ * rem; /* remainder in millionths of HZ units */

	/* check for overflow */
	if (quo != quohz / HZ)
		return (MAXCLOCK_T);
	if (rem != remhz / HZ) remhz = MAXCLOCK_T;

	/* adjust remhz since it was in millionths of HZ units */
	remhz = (remhz % 1000000) ? remhz/1000000 + 1 : remhz/1000000;


	if ((MAXCLOCK_T - quohz) < remhz)
		return (MAXCLOCK_T);
	return (quohz + remhz);
}


/*
 * pollwakeup() - poke threads waiting in poll() for some event
 * on a particular object.
 *
 * The threads hanging off of the specified pollhead structure are scanned.
 * If their event mask matches the specified event(s), then pollrun() is
 * called to poke the thread.
 *
 * Multiple events may be specified.  When POLLHUP or POLLERR are specified,
 * all waiting threads are poked.
 *
 * It is important that pollrun() not drop the lock protecting the list
 * of threads.
 */

void
pollwakeup(pollhead_t *php, short events_arg)
{
	polldat_t	*pdp;
	uint		unsafe;
	int		events = (ushort_t)events_arg;

	unsafe = UNSAFE_DRIVER_LOCK_HELD();

	if (unsafe)
		mutex_exit(&unsafe_driver);

retry:
	pplock(php);

	/*
	 * About half of all pollwakeups don't do anything, because the
	 * pollhead list is empty (i.e, nobody was waiting for the event).
	 * For this common case, we can optimize out locking overhead.
	 * (This may cease to be a win when we finally get rid of the
	 * unsafe_driver lock.)
	 */
	if (php->ph_list == NULL) {
		ppunlock(php);
		if (unsafe)
			mutex_enter(&unsafe_driver);
		return;
	}

	if (events & (POLLHUP | POLLERR)) {
		/*
		 * poke everyone
		 */
		for (pdp = php->ph_list; pdp; pdp = pdp->pd_next) {

			kthread_t	*t = pdp->pd_thread;
			pollstate_t 	*ps = t->t_pollstate;

			/*
			 * Try to grab the lock for this thread. If
			 * we don't get it then we may deadlock so
			 * back out and restart all over again. Note
			 * that the failure rate is very very low.
			 */
			if (mutex_tryenter(&ps->ps_lock)) {
				pollrun(t);
				mutex_exit(&ps->ps_lock);
			} else {
				/*
				 * We are here because:
				 *	1) This thread has been waked up
				 *	   and is trying to get out of poll().
				 *	2) Some other thread is also here
				 *	   but with a different pollhead lock.
				 *
				 * So, we need to drop the lock on pollhead
				 * because of (1) but we want to prevent
				 * that thread from doing thread_exit(). If
				 * it did thread_exit() then the ps pointer
				 * would be invalid.
				 *
				 * Solution: Grab the ps->ps_no_exit lock,
				 * increment the ps_busy counter, drop every
				 * lock in sight. Get out of the way and wait
				 * for type (2) threads to finish.
				 */

				mutex_enter(&ps->ps_no_exit);
				ps->ps_busy++;	/* prevents exit()'s */
				mutex_exit(&ps->ps_no_exit);

				ppunlock(php);
				mutex_enter(&ps->ps_lock);
				mutex_exit(&ps->ps_lock);
				mutex_enter(&ps->ps_no_exit);
				ps->ps_busy--;
				if (ps->ps_busy == 0) {
					/*
					 * Wakeup the thread waiting in
					 * thread_exit().
					 */
					cv_signal(&ps->ps_busy_cv);
				}
				mutex_exit(&ps->ps_no_exit);
				goto retry;
			}
		}
	} else {
		for (pdp = php->ph_list; pdp; pdp = pdp->pd_next) {
			if (pdp->pd_events & events) {

				kthread_t	*t = pdp->pd_thread;
				pollstate_t	*ps = t->t_pollstate;

				if (mutex_tryenter(&ps->ps_lock)) {
					pollrun(t);
					mutex_exit(&ps->ps_lock);
				} else {
					/*
					 * See the above comment.
					 */
					mutex_enter(&ps->ps_no_exit);
					ps->ps_busy++;	/* prevents exit()'s */
					mutex_exit(&ps->ps_no_exit);

					ppunlock(php);
					mutex_enter(&ps->ps_lock);
					mutex_exit(&ps->ps_lock);
					mutex_enter(&ps->ps_no_exit);
					ps->ps_busy--;
					if (ps->ps_busy == 0) {
						/*
						 * Wakeup the thread waiting in
						 * thread_exit().
						 */
						cv_signal(&ps->ps_busy_cv);
					}
					mutex_exit(&ps->ps_no_exit);
					goto retry;
				}
			}
		}
	}

	ppunlock(php);

	if (unsafe)
		mutex_enter(&unsafe_driver);
}

/*
 * XXX safe version of pollwakeup().  Once unsafe support
 * is removed so should be pollwkeup and replaced by this.
 */
void
pollwakeup_safe(pollhead_t *php, short events_arg)
{
	polldat_t	*pdp;
	int		events = (ushort_t)events_arg;


retry:
	pplock(php);

	/*
	 * About half of all pollwakeups don't do anything, because the
	 * pollhead list is empty (i.e, nobody was waiting for the event).
	 * For this common case, we can optimize out locking overhead.
	 * (This may cease to be a win when we finally get rid of the
	 * unsafe_driver lock.)
	 */
	if (php->ph_list == NULL) {
		ppunlock(php);
		return;
	}

	if (events & (POLLHUP | POLLERR)) {
		/*
		 * poke everyone
		 */
		for (pdp = php->ph_list; pdp; pdp = pdp->pd_next) {

			kthread_t	*t = pdp->pd_thread;
			pollstate_t 	*ps = t->t_pollstate;

			/*
			 * Try to grab the lock for this thread. If
			 * we don't get it then we may deadlock so
			 * back out and restart all over again. Note
			 * that the failure rate is very very low.
			 */
			if (mutex_tryenter(&ps->ps_lock)) {
				pollrun(t);
				mutex_exit(&ps->ps_lock);
			} else {
				/*
				 * We are here because:
				 *	1) This thread has been waked up
				 *	   and is trying to get out of poll().
				 *	2) Some other thread is also here
				 *	   but with a different pollhead lock.
				 *
				 * So, we need to drop the lock on pollhead
				 * because of (1) but we want to prevent
				 * that thread from doing thread_exit(). If
				 * it did thread_exit() then the ps pointer
				 * would be invalid.
				 *
				 * Solution: Grab the ps->ps_no_exit lock,
				 * increment the ps_busy counter, drop every
				 * lock in sight. Get out of the way and wait
				 * for type (2) threads to finish.
				 */

				mutex_enter(&ps->ps_no_exit);
				ps->ps_busy++;	/* prevents exit()'s */
				mutex_exit(&ps->ps_no_exit);

				ppunlock(php);
				mutex_enter(&ps->ps_lock);
				mutex_exit(&ps->ps_lock);
				mutex_enter(&ps->ps_no_exit);
				ps->ps_busy--;
				if (ps->ps_busy == 0) {
					/*
					 * Wakeup the thread waiting in
					 * thread_exit().
					 */
					cv_signal(&ps->ps_busy_cv);
				}
				mutex_exit(&ps->ps_no_exit);
				goto retry;
			}
		}
	} else {
		for (pdp = php->ph_list; pdp; pdp = pdp->pd_next) {
			if (pdp->pd_events & events) {

				kthread_t	*t = pdp->pd_thread;
				pollstate_t	*ps = t->t_pollstate;

				if (mutex_tryenter(&ps->ps_lock)) {
					pollrun(t);
					mutex_exit(&ps->ps_lock);
				} else {
					/*
					 * See the above comment.
					 */
					mutex_enter(&ps->ps_no_exit);
					ps->ps_busy++;	/* prevents exit()'s */
					mutex_exit(&ps->ps_no_exit);

					ppunlock(php);
					mutex_enter(&ps->ps_lock);
					mutex_exit(&ps->ps_lock);
					mutex_enter(&ps->ps_no_exit);
					ps->ps_busy--;
					if (ps->ps_busy == 0) {
						/*
						 * Wakeup the thread waiting in
						 * thread_exit().
						 */
						cv_signal(&ps->ps_busy_cv);
					}
					mutex_exit(&ps->ps_no_exit);
					goto retry;
				}
			}
		}
	}

	ppunlock(php);

}

/*
 * function: drv_priv()
 * purpose:  determine if the supplied credentials identify a privileged
 *	process.  To be used only when file access modes and
 *	special minor device numbers are insufficient to provide
 *	protection for the requested driver function.  Returns 0
 *	if the privilege is granted, otherwise EPERM.
 */

int
drv_priv(cred_t *cr)
{
	return ((cr->cr_uid == 0) ? 0 : EPERM);
}

/* XXX - drv_usecwait() moved to uts/m32/ml/wait.c */

#ifdef	sun
/*
 * drv_usecwait implemented in each architecture's machine
 * specific code somewhere. For sparc, it is the alternate entry
 * to usec_delay (eventually usec_delay goes away). See
 * sparc/os/ml/sparc_subr.s
 */
#endif

/*
 * bcanputnext, canputnext assume called from timeout, bufcall,
 * or esballoc free routines.  since these are driven by
 * clock interrupts, instead of system calls the appropriate plumbing
 * locks have not been acquired.
 */
int
bcanputnext(queue_t *q, unsigned char band)
{
	int	ret;

	claimstr(q);
	ret = bcanput(q->q_next, band);
	releasestr(q);
	return (ret);
}


int
canputnext(register queue_t *q)
{
	queue_t	*qofsq = q;
	struct stdata *stp = STREAM(q);

	TRACE_2(TR_FAC_STREAMS_FR, TR_CANPUT_IN,
	    "canputnext?:%s(%X)\n", QNAME(q), q);

	mutex_enter(&stp->sd_reflock);

	/* get next module forward with a service queue */
	q = q->q_next->q_nfsrv;
	ASSERT(q != NULL);

	/* this is for loopback transports, they should not do a canputnext */
	ASSERT(STRMATED(q->q_stream) || STREAM(q) == STREAM(qofsq));

	if (!(q->q_flag & QFULL)) {
		mutex_exit(&stp->sd_reflock);
		TRACE_2(TR_FAC_STREAMS_FR, TR_CANPUT_OUT, "canput:%X %d", q, 1);
		return (1);
	}

	/* the above is the most frequently used path */
	stp->sd_refcnt++;
	ASSERT(stp->sd_refcnt != 0);	/* Wraparound */
	mutex_exit(&stp->sd_reflock);

	mutex_enter(QLOCK(q));
	if (q->q_flag & QFULL) {
		q->q_flag |= QWANTW;
		mutex_exit(QLOCK(q));
		TRACE_2(TR_FAC_STREAMS_FR, TR_CANPUT_OUT, "canput:%X %d", q, 0);
		releasestr(qofsq);

		return (0);
	}
	mutex_exit(QLOCK(q));
	TRACE_2(TR_FAC_STREAMS_FR, TR_CANPUT_OUT, "canput:%X %d", q, 1);
	releasestr(qofsq);

	return (1);
}


int
putnextctl1(queue_t *q, int type, int param)
{
	int	ret;

	claimstr(q);
	if (q->q_flag & QUNSAFE) {
		mutex_exit(&unsafe_driver);
	} else
		ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	ret = putctl1(q->q_next, type, param);
	if (q->q_flag & QUNSAFE) {
		mutex_enter(&unsafe_driver);
	} else
		ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	releasestr(q);
	return (ret);
}

int
putnextctl(queue_t *q, int type)
{
	int	ret;

	claimstr(q);
	if (q->q_flag & QUNSAFE) {
		mutex_exit(&unsafe_driver);
	} else
		ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	ret = putctl(q->q_next, type);
	if (q->q_flag & QUNSAFE) {
		mutex_enter(&unsafe_driver);
	} else
		ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	releasestr(q);
	return (ret);
}

/*
 * mt-safe open has progressed to the point where it is
 * safe to send/receive messages.
 *
 * "qprocson enables the put and service routines of the driver
 * or module... Prior to the call to qprocson, the put and service
 * routines of a newly pushed module or newly opened driver are
 * disabled.  For the module, messages flow around it as if it
 * were not present in the stream... qprocson must be called by
 * the first open of a module or driver after allocation and
 * initialization of any resource on which the put and service
 * routines depend."
 */
void
qprocson(queue_t *q)
{
	ASSERT(q->q_flag & QREADR);
	/*
	 * could be re-open
	 */
	if (q->q_next == NULL && WR(q)->q_next == NULL) {
		unblockq(q);
		insertq(STREAM(q), q, 0);
	}
}

/*
 * mt-safe close has reached a point where it can no longer
 * allow put/service into the queue.
 *
 * "qprocsoff disables the put and service routines of the driver
 * or module... When the routines are disabled in a module, messages
 * flow around the module as if it were not present in the stream.
 * qprocsoff must be called by the close routine of a driver or module
 * before deallocating any resources on which the driver/module's
 * put and service routines depend.  qprocsoff will remove the
 * queue's service routines from the list of service routines to be
 * run and waits until any concurrent put or service routines are
 * finished."
 */
void
qprocsoff(queue_t *q)
{
	ASSERT(q->q_flag & QREADR);
	if (q->q_flag & QWCLOSE) {
		/* Called more than once */
		return;
	}
	disable_svc(q);
	removeq(q, 0);
	blockq(q);		/* Prevent drain_syncq from running */
}

/*
 * "freezestr() freezes the state of the entire STREAM  containing
 *  the  queue  pair  q.  A frozen STREAM blocks any thread
 *  attempting to enter any open, close, put or service  routine
 *  belonging  to  any  queue instance in the STREAM, and blocks
 *  any thread currently within the STREAM if it attempts to put
 *  messages  onto  or take messages off of any queue within the
 *  STREAM (with the sole exception  of  the  caller).   Threads
 *  blocked  by  this  mechanism  remain  so until the STREAM is
 *  thawed by a call to unfreezestr().
 *
 * Use strblock to set SQ_BLOCKED in all syncqs in the stream (prevents
 * further entry into put, service, open, and close procedures) and
 * grab (and hold) all the QLOCKs in the stream (to block putq, getq etc.)
 *
 * Note: this has to be the only code that acquires one QLOCK while holding
 * another QLOCK (otherwise we would have locking hirarchy/ordering violations.)
 */
void
freezestr(queue_t *q)
{
	struct stdata *stp = STREAM(q);

	/* Should never be used by unsafe drivers */
	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());

	/*
	 * Increment refcnt to prevent q_next from changing during the strblock
	 * as well as while the stream is frozen.
	 */
	claimstr(RD(q));

	strblock(q);
	ASSERT(stp->sd_freezer == NULL);
	stp->sd_freezer = curthread;
	for (q = stp->sd_wrq; q != NULL; q = SAMESTR(q) ? q->q_next : NULL) {
		mutex_enter(QLOCK(q));
		mutex_enter(QLOCK(RD(q)));
	}
}

/*
 * Undo what freezestr did.
 * Have to drop the QLOCKs before the strunblock since strunblock will
 * potentially call other put procedures.
 */
void
unfreezestr(queue_t *q)
{
	struct stdata *stp = STREAM(q);
	queue_t	*q1;

	for (q1 = stp->sd_wrq; q1 != NULL;
	    q1 = SAMESTR(q1) ? q1->q_next : NULL) {
		mutex_exit(QLOCK(q1));
		mutex_exit(QLOCK(RD(q1)));
	}
	ASSERT(stp->sd_freezer == curthread);
	stp->sd_freezer = NULL;
	strunblock(q);
	releasestr(RD(q));
}

/*
 * Used by open and close procedures to "sleep" waiting for messages to
 * arrive. Note: can only be used in open and close procedures.
 *
 * Lower the gate and let in either messages on the syncq (if there are
 * any) or put/service procedures.
 *
 * If the queue has an outer perimeter this will not prevent entry into this
 * syncq (since outer_enter does not set SQ_WRITER on the syncq that gets the
 * exclusive access to the outer perimeter.)
 *
 * Return 0 is the cv_wait_sig was interrupted; otherwise 1.
 */
int
qwait_sig(queue_t *q)
{
	syncq_t		*sq, *outer;
	u_long		flags;
	int		ret = 1;

	/*
	 * Drain the syncq if possible. Otherwise reset SQ_EXCL and
	 * wait for a thread to leave the syncq.
	 */
	sq = q->q_syncq;
	ASSERT(sq);
	ASSERT(sq->sq_outer == NULL || sq->sq_outer->sq_flags & SQ_WRITER);
	ASSERT(!(sq->sq_type & SQ_UNSAFE));
	outer = sq->sq_outer;
	/*
	 * XXX this does not work if there is only an outer perimeter.
	 * The semantics of qwait/qwait_sig are undefined in this case.
	 */
	if (outer)
		outer_exit(outer);

	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	/*
	 * Drop SQ_EXCL and sq_count but hold the SQLOCK until to prevent any
	 * undetected entry and exit into the perimeter.
	 */
	ASSERT(sq->sq_count > 0);
	sq->sq_count--;
	if (!(sq->sq_type & SQ_CIOC)) {
		ASSERT(flags & SQ_EXCL);
		flags &= ~SQ_EXCL;
	}
	/*
	 * Unblock any thread blocked in an entersq or outer_enter.
	 * Note: we do not unblock a thread waiting in qwait/qwait_sig,
	 * since that could lead to livelock with two threads in
	 * qwait for the same (per module) inner perimeter.
	 */
	if (flags & SQ_WANTWAKEUP) {
		cv_broadcast(&sq->sq_wait);
		flags &= ~SQ_WANTWAKEUP;
	}
	sq->sq_flags = flags;
	if ((flags & SQ_QUEUED) && !(flags & SQ_STAYAWAY)) {
		drain_syncq(sq);
		mutex_exit(SQLOCK(sq));
		entersq(sq, SQ_OPENCLOSE);
		return (1);
	}
	/*
	 * Sleep on sq_exitwait to only be woken up when threads leave the
	 * put or service procedures. We can not sleep on sq_wait since an
	 * outer_exit in a qwait running in the same outer perimeter would
	 * cause a livelock "ping-pong" between two or more qwait'ers.
	 */
	do {
		sq->sq_flags |= SQ_WANTEXWAKEUP;
		ret = cv_wait_sig(&sq->sq_exitwait, SQLOCK(sq));
	} while (ret && (sq->sq_flags & SQ_WANTEXWAKEUP));
	mutex_exit(SQLOCK(sq));

	/*
	 * Re-enter the perimeters again
	 */
	entersq(sq, SQ_OPENCLOSE);
	return (ret);
}

/*
 * Used by open and close procedures to "sleep" waiting for messages to
 * arrive. Note: can only be used in open and close procedures.
 *
 * Lower the gate and let in either messages on the syncq (if there are
 * any) or put/service procedures.
 *
 * If the queue has an outer perimeter this will not prevent entry into this
 * syncq (since outer_enter does not set SQ_WRITER on the syncq that gets the
 * exclusive access to the outer perimeter.)
 */
void
qwait(queue_t *q)
{
	syncq_t		*sq, *outer;
	u_long		flags;

	/*
	 * Drain the syncq if possible. Otherwise reset SQ_EXCL and
	 * wait for a thread to leave the syncq.
	 */
	sq = q->q_syncq;
	ASSERT(sq);
	ASSERT(sq->sq_outer == NULL || sq->sq_outer->sq_flags & SQ_WRITER);
	ASSERT(!(sq->sq_type & SQ_UNSAFE));
	outer = sq->sq_outer;
	/*
	 * XXX this does not work if there is only an outer perimeter.
	 * The semantics of qwait/qwait_sig are undefined in this case.
	 */
	if (outer)
		outer_exit(outer);

	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	/*
	 * Drop SQ_EXCL and sq_count but hold the SQLOCK until to prevent any
	 * undetected entry and exit into the perimeter.
	 */
	ASSERT(sq->sq_count > 0);
	sq->sq_count--;
	if (!(sq->sq_type & SQ_CIOC)) {
		ASSERT(flags & SQ_EXCL);
		flags &= ~SQ_EXCL;
	}
	/*
	 * Unblock any thread blocked in an entersq or outer_enter.
	 * Note: we do not unblock a thread waiting in qwait/qwait_sig,
	 * since that could lead to livelock with two threads in
	 * qwait for the same (per module) inner perimeter.
	 */
	if (flags & SQ_WANTWAKEUP) {
		cv_broadcast(&sq->sq_wait);
		flags &= ~SQ_WANTWAKEUP;
	}
	sq->sq_flags = flags;
	if ((flags & SQ_QUEUED) && !(flags & SQ_STAYAWAY)) {
		drain_syncq(sq);
		mutex_exit(SQLOCK(sq));
		entersq(sq, SQ_OPENCLOSE);
		return;
	}
	/*
	 * Sleep on sq_exitwait to only be woken up when threads leave the
	 * put or service procedures. We can not sleep on sq_wait since an
	 * outer_exit in a qwait running in the same outer perimeter would
	 * cause a livelock "ping-pong" between two or more qwait'ers.
	 */
	do {
		sq->sq_flags |= SQ_WANTEXWAKEUP;
		cv_wait(&sq->sq_exitwait, SQLOCK(sq));
	} while (sq->sq_flags & SQ_WANTEXWAKEUP);
	mutex_exit(SQLOCK(sq));

	/*
	 * Re-enter the perimeters again
	 */
	entersq(sq, SQ_OPENCLOSE);
}

/*
 * Asynchronously upgrade to exclusive access at either the inner or
 * outer perimeter.
 */
void
qwriter(q, mp, func, perim)
	queue_t	*q;
	mblk_t	*mp;
	void	(*func)();
	int perim;
{
	if (perim == PERIM_INNER)
		qwriter_inner(q, mp, func);
	else if (perim == PERIM_OUTER)
		qwriter_outer(q, mp, func);
	else
		panic("qwriter: wrong \"perimeter\" parameter\n");
}

/*
 * Schedule a synchronous streams timeout
 */
int
qtimeout(q, fun, arg, tim)
	queue_t *q;
	void (*fun)();
	caddr_t arg;
	long tim;
{
	syncq_t		*sq;
	callbparams_t	*params;
	int		id;

	sq = q->q_syncq;
	/* you don't want the timeout firing before its params are set up */
	mutex_enter(SQLOCK(sq));
	params = callbparams_alloc(sq, fun, arg);
	/*
	 * the callbflags in the sq use the same flags. They get anded
	 * in the callbwrapper to determine if a qun* of this callback type
	 * is required. This is not a request to cancel.
	 */
	params->flags = SQ_CANCEL_TOUT;
	/* check new timeout version return codes */
	id = params->id = timeout(qcallbwrapper, (caddr_t)params, tim);
	mutex_exit(SQLOCK(sq));
	/* use local id because the params memory could be free by now */
	return (id);
}


int
qbufcall(q, size, pri, fun, arg)
	queue_t *q;
	uint size;
	int pri;
	void (*fun)();
	long arg;
{
	syncq_t		*sq;
	callbparams_t	*params;
	int		id;

	sq = q->q_syncq;
	/* you don't want the timeout firing before its params are set up */
	mutex_enter(SQLOCK(sq));
	params = callbparams_alloc(sq, fun, (caddr_t)arg);
	/*
	 * the callbflags in the sq use the same flags. They get anded
	 * in the callbwrapper to determine if a qun* of this callback type
	 * is required. This is not a request to cancel.
	 */
	params->flags = SQ_CANCEL_BUFCALL;
	/* check new timeout version return codes */
	id = params->id = bufcall(size, pri, qcallbwrapper, (long)params);
	if (id == 0) {
		callbparams_free(sq, params);
	}
	mutex_exit(SQLOCK(sq));
	/* use local id because the params memory could be free by now */
	return (id);
}


/*
 * calcelling of a timeout callback which enters the inner perimeter.
 * cancelling of all callback types on a given syncq is serialized.
 * the SQ_CALLB_BYPASSED flag indicates that the callback fn did
 * not execute. The quntimeout return value needs to reflect this.
 * As with out existing callback programming model - callbacks must
 * be cancelled before a close completes - so ensuring that the sq
 * is valid when the callback wrapper is executed.
 */
int
quntimeout(q, id)
	queue_t *q;
	int id;
{
	syncq_t *sq = q->q_syncq;
	int ret;

	mutex_enter(SQLOCK(sq));
	/* callbacks are processed serially on each syncq */
	while (sq->sq_callbflags & SQ_CALLB_CANCEL_MASK) {
		sq->sq_flags |= SQ_WANTWAKEUP;
		cv_wait(&sq->sq_wait, SQLOCK(sq));
	}
	sq->sq_cancelid = id;
	sq->sq_callbflags = SQ_CANCEL_TOUT;
	if (sq->sq_flags & SQ_WANTWAKEUP) {
		cv_broadcast(&sq->sq_wait);
		sq->sq_flags &= ~SQ_WANTWAKEUP;
	}
	mutex_exit(SQLOCK(sq));
	ret = untimeout(id);
	mutex_enter(SQLOCK(sq));
	if (ret != -1) {
		/* The wrapper was never called - need to free based on id */
		callbparams_free_id(sq, id, SQ_CANCEL_TOUT);
	}
	if (sq->sq_callbflags & SQ_CALLB_BYPASSED) {
		ret = 0;	/* this was how much time left */
	}
	sq->sq_callbflags = 0;
	if (sq->sq_flags & SQ_WANTWAKEUP) {
		cv_broadcast(&sq->sq_wait);
		sq->sq_flags &= ~SQ_WANTWAKEUP;
	}
	mutex_exit(SQLOCK(sq));
	return (ret);
}


void
qunbufcall(q, id)
	queue_t *q;
	int id;
{
	syncq_t *sq = q->q_syncq;

	mutex_enter(SQLOCK(sq));
	/* callbacks are processed serially on each syncq */
	while (sq->sq_callbflags & SQ_CALLB_CANCEL_MASK) {
		sq->sq_flags |= SQ_WANTWAKEUP;
		cv_wait(&sq->sq_wait, SQLOCK(sq));
	}
	sq->sq_cancelid = id;
	sq->sq_callbflags = SQ_CANCEL_BUFCALL;
	if (sq->sq_flags & SQ_WANTWAKEUP) {
		cv_broadcast(&sq->sq_wait);
		sq->sq_flags &= ~SQ_WANTWAKEUP;
	}
	mutex_exit(SQLOCK(sq));
	unbufcall(id);
	mutex_enter(SQLOCK(sq));
	/*
	 * No indication from unbufcall if the callback has already run.
	 * Always attempt to free it.
	 */
	callbparams_free_id(sq, id, SQ_CANCEL_BUFCALL);
	sq->sq_callbflags = 0;
	if (sq->sq_flags & SQ_WANTWAKEUP) {
		cv_broadcast(&sq->sq_wait);
		sq->sq_flags &= ~SQ_WANTWAKEUP;
	}
	mutex_exit(SQLOCK(sq));
}

/*
 * This routine is the SVR4MP 'replacement' for
 * hat_getkpfnum.  The only major difference is
 * the return value for illegal addresses - since
 * sunm_getkpfnum() and srmmu_getkpfnum() both
 * return '-1' for bogus mappings, we can (more or
 * less) return the value directly.
 */
ppid_t
kvtoppid(caddr_t addr)
{
	return ((ppid_t)hat_getkpfnum(addr));
}


/*
 * This is used to set the timeout value for cv_timed_wait() or
 * cv_timedwait_sig().  XXX good for 248 days....
 */
void
time_to_wait(unsigned long *now, long time)
{
	drv_getparm(LBOLT, now);
	*now += time;
}
