/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988,1989,1990  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

#pragma ident   "@(#)vm_pageout.c 1.83     94/12/02 SMI"
/*	From:	SVr4.0	"kernel:os/vm_pageout.c	1.19"		*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/mman.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/vm.h>
#include <sys/vmparam.h>
#include <sys/vtrace.h>
#include <sys/cmn_err.h>
#include <sys/cpuvar.h>
#include <sys/user.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/strsubr.h>
#include <sys/callb.h>
#include <sys/tnf_probe.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg_kmem.h>

static int checkpage(page_t *, int);

/*
 * The following parameters control operation of the page replacement
 * algorithm.  They are initialized to 0, and then computed at boot time
 * based on the size of the system.  If they are patched non-zero in
 * a loaded vmunix they are left alone and may thus be changed per system
 * using adb on the loaded system.
 */

int		slowscan = 0;
int		fastscan = 0;
static int	handspreadpages = 0;
static int	loopfraction = 2;
static u_int	looppages;
static int	percent_cpu = 4;
static int	maxfastscan = 0;
static int	maxslowscan = 100;

int	maxpgio = 0;
int	minfree = 0;
int	desfree = 0;
int	lotsfree = 0;
int	needfree = 0;

int	deficit;
int	nscan;
int	desscan;

/*
 * The size of the clock loop.
 */
#define	LOOPPAGES	(epages - pages)

/* insure non-zero */
#define	nz(x)	((x) != 0 ? (x) : 1)


/*
 * hooks for bufcall wakeups
 */
extern struct bclist strbcalls;
extern char strbcflag;
extern int run_queues;
extern kmutex_t service_queue;
extern kcondvar_t services_to_run;
extern void queuerun(void);

/*
 * Set up the paging constants for the clock algorithm.
 * Called after the system is initialized and the amount of memory
 * and number of paging devices is known.
 *
 * Threshold constants are defined in vm_machparam.h.
 */
void
setupclock()
{
	/*
	 * Set up thresholds for paging:
	 */
	looppages = LOOPPAGES;

	/*
	 * Lotsfree is threshold where paging daemon turns on.
	 */
	if (lotsfree == 0)
		lotsfree = MIN(looppages / LOTSFREEFRACT, LOTSFREE / NBPG);
	if (lotsfree > looppages / LOTSFREEFRACT)
		lotsfree = looppages / LOTSFREEFRACT;

	/*
	 * Desfree is amount of memory desired free.
	 * If less than this for extended period, start swapping.
	 */
	if (desfree == 0)
		desfree = MIN(looppages / DESFREEFRACT, DESFREE / NBPG);
	if (desfree > looppages / DESFREEFRACT)
		desfree = looppages / DESFREEFRACT;

	/*
	 * Minfree is minimal amount of free memory which is tolerable.
	 */
	if (minfree == 0)
		minfree = MIN(desfree / MINFREEFRACT, MINFREE / NBPG);
	if (minfree > desfree / MINFREEFRACT)
		minfree = desfree / MINFREEFRACT;

	/*
	 * Maxpgio thresholds how much paging is acceptable.
	 * This figures that 2/3 busy on an arm is all that is
	 * tolerable for paging.  We assume one operation per disk rev.
	 *
	 * XXX - Does not account for multiple swap devices.
	 */
	if (maxpgio == 0)
		maxpgio = (DISKRPM * 2) / 3;

	/*
	 * The clock scan rate varies between fastscan and slowscan
	 * based on the amount of free memory available.  Fastscan
	 * rate should be set based on the number pages that can be
	 * scanned per sec using ~10% of processor time.  Since this
	 * value depends on the processor, MMU, Mhz etc., it is
	 * difficult to determine it in a generic manner for all
	 * architectures.
	 *
	 * Instead of trying to determine the number of pages scanned
	 * per sec for every processor, fastscan is set to be the smaller
	 * of 1/2 of memory or MAXHANDSPREADPAGES and the sampling
	 * time is limited to ~4% of processor time.
	 *
	 * Setting fastscan to be 1/2 of memory allows pageout to scan
	 * all of memory in ~2 secs.  This implies that user pages not
	 * accessed within 1 sec (assuming, handspreadpages == fastscan)
	 * can be reclaimed when free memory is very low.  Stealing pages
	 * not accessed within 1 sec seems reasonable and ensures that
	 * active user processes don't thrash.
	 *
	 * Smaller values of fastscan result in scanning fewer pages
	 * every second and consequently pageout may not be able to free
	 * sufficient memory to maintain the minimum threshold.  Larger
	 * values of fastscan result in scanning a lot more pages which
	 * could lead to thrashing and higher CPU usage.
	 *
	 * Fastscan needs to be limited to a maximum value and should not
	 * scale with memory to prevent pageout from consuming too much
	 * time for scanning on slow CPU's and avoid thrashing, as a
	 * result of scanning too many pages, on faster CPU's.
	 * The value of 64 Meg was chosen for MAXHANDSPREADPAGES
	 * (the upper bound for fastscan) based on the average number
	 * of pages that can potentially be scanned in ~1 sec (using ~4%
	 * of the CPU) on some of the following machines that currently
	 * run Solaris 2.x:
	 *
	 *			average memory scanned in ~1 sec
	 *
	 *	25 Mhz SS1+:		23 Meg
	 *	LX:			37 Meg
	 *	50 Mhz SC2000:		68 Meg
	 *
	 *	40 Mhz 486:		26 Meg
	 *	66 Mhz 486:		42 Meg
	 *
	 * When free memory falls just below lotsfree, the scan rate
	 * goes from 0 to slowscan (i.e., pageout starts running).  This
	 * transition needs to be smooth and is achieved by ensuring that
	 * pageout scans a small number of pages to satisfy the transient
	 * memory demand.  This is set to not exceed 100 pages/sec (25 per
	 * wakeup) since scanning that many pages has no noticible impact
	 * on system performance.
	 *
	 * In addition to setting fastscan and slowscan, pageout is
	 * limited to using ~4% of the CPU.  This results in increasing
	 * the time taken to scan all of memory, which in turn means that
	 * user processes have a better opportunity of preventing their
	 * pages from being stolen.  This has a positive effect on
	 * interactive and overall system performance when memory demand
	 * is high.
	 *
	 * Thus, the rate at which pages are scanned for replacement will
	 * vary linearly between slowscan and the number of pages that
	 * can be scanned using ~4% of processor time instead of varying
	 * linearly between slowscan and fastscan.
	 *
	 * Also, the processor time used by pageout will vary from ~1%
	 * at slowscan to ~4% at fastscan instead of varying between
	 * ~1% at slowscan and ~10% at fastscan.
	 *
	 * The values chosen for the various VM parameters (fastscan,
	 * handspreadpages, etc) are not universally true for all machines,
	 * but appear to be a good rule of thumb for the machines we've
	 * tested.  They have the following ranges:
	 *
	 *	cpu speed:	20 to 70 Mhz
	 *	page size:	4K to 8K
	 *	memory size:	16M to 5G
	 *	page scan rate:	4000 - 17400 4K pages per sec
	 *
	 * The values need to be re-examined for machines which don't
	 * fall into the various ranges (e.g., slower or faster CPUs,
	 * smaller or larger pagesizes etc) shown above.
	 *
	 * On an MP machine, pageout is often unable to maintain the
	 * minimum paging thresholds under heavy load.  This is due to
	 * the fact that user processes running on other CPU's can be
	 * dirtying memory at a much faster pace than pageout can find
	 * pages to free.  The memory demands could be met by enabling
	 * more than one CPU to run the clock algorithm in such a manner
	 * that the various clock hands don't overlap.  This also makes
	 * it more difficult to determine the values for fastscan, slowscan
	 * and handspreadpages.
	 *
	 * The swapper is currently used to free up memory when pageout
	 * is unable to meet memory demands by swapping out processes.
	 * In addition to freeing up memory, swapping also reduces the
	 * demand for memory by preventing user processes from running
	 * and thereby consuming memory.
	 */
	if (maxfastscan == 0)
		maxfastscan = MAXHANDSPREADPAGES;
	if (fastscan == 0)
		fastscan = MIN(looppages / loopfraction, maxfastscan);
	if (fastscan > looppages / loopfraction)
		fastscan = looppages / loopfraction;

	/*
	 * Set slow scan time to 1/10 the fast scan time, but
	 * not to exceed maxslowscan.
	 */
	if (slowscan == 0)
		slowscan = MIN(fastscan / 10, maxslowscan);
	if (slowscan > fastscan / 2)
		slowscan = fastscan / 2;

	/*
	 * Handspreadpages is distance (in pages) between front and back
	 * pageout daemon hands.  The amount of time to reclaim a page
	 * once pageout examines it increases with this distance and
	 * decreases as the scan rate rises. It must be < the amount
	 * of pageable memory.
	 *
	 * Since pageout is limited to ~4% of the CPU, setting handspreadpages
	 * to be "fastscan" results in the front hand being a few secs
	 * (varies based on the processor speed) ahead of the back hand
	 * at fastscan rates.  This distance can be further reduced, if
	 * necessary, by increasing the processor time used by pageout
	 * to be more than ~4% and preferrably not more than ~10%.
	 *
	 * As a result, user processes have a much better chance of
	 * referencing their pages before the back hand examines them.
	 * This also significantly lowers the number of reclaims from
	 * the freelist since pageout does not end up freeing pages which
	 * may be referenced a sec later.
	 */
	if (handspreadpages == 0)
		handspreadpages = fastscan;

	/*
	 * Make sure that back hand follows front hand by at least
	 * 1/RATETOSCHEDPAGING seconds.  Without this test, it is possible
	 * for the back hand to look at a page during the same wakeup of
	 * the pageout daemon in which the front hand cleared its ref bit.
	 */
	if (handspreadpages >= looppages)
		handspreadpages = looppages - 1;
}

/*
 * Pageout scheduling.
 *
 * Schedpaging controls the rate at which the page out daemon runs by
 * setting the global variables nscan and desscan RATETOSCHEDPAGING
 * times a second.  Nscan records the number of pages pageout has examined
 * in its current pass; schedpaging resets this value to zero each time
 * it runs.  Desscan records the number of pages pageout should examine
 * in its next pass; schedpaging sets this value based on the amount of
 * currently available memory.
 */

#define	RATETOSCHEDPAGING	4		/* HZ that is */

static kmutex_t	pageout_mutex;	/* held while pageout or schedpaging running */

/*
 * Pool of available async pageout putpage requests.
 */
static struct async_reqs *push_req;
static struct async_reqs *req_freelist;	/* available req structs */
static struct async_reqs *push_list;	/* pending reqs */
static kmutex_t push_lock;		/* protects req pool */
static kcondvar_t push_cv;

static async_list_size = 256;		/* number of async request structs */

void pageout_scanner();
void cv_signal_pageout();

/*
 * Schedule rate for paging.
 * Rate is linear interpolation between
 * slowscan with lotsfree and fastscan when out of memory.
 */
void
schedpaging()
{
	register int vavail;

	if (freemem < lotsfree + needfree + kmem_reapahead)
		kmem_reap();

	if (mutex_tryenter(&pageout_mutex)) {
		/* pageout() not running */
		nscan = 0;
		vavail = freemem - deficit;
		if (vavail < 0)
			vavail = 0;
		if (vavail > lotsfree)
			vavail = lotsfree;
		desscan = needfree ? fastscan / RATETOSCHEDPAGING :
			(slowscan * vavail + fastscan * (lotsfree - vavail)) /
			nz(lotsfree) / RATETOSCHEDPAGING;
		if (freemem < lotsfree + needfree) {
			TRACE_1(TR_FAC_VM, TR_PAGEOUT_CV_SIGNAL,
				"pageout_cv_signal:freemem %d", freemem);
			cv_signal(&proc_pageout->p_cv);
		} else {
			cv_signal_pageout();
		}
		mutex_exit(&pageout_mutex);
	}

	/*
	 * run streams bufcalls, if necessary
	 */

	if ((strbcalls.bc_head || run_queues) && !strbcflag &&
		kmem_avail() > 0 &&
		mutex_tryenter(&service_queue)) {
			strbcflag = 1;
			cv_signal(&services_to_run);
			mutex_exit(&service_queue);
	}
	(void) timeout(schedpaging, (caddr_t)0, HZ / RATETOSCHEDPAGING);
}

void pageout_scanner(void);
int		pushes;
u_int		push_list_size;		/* # of requests on pageout queue */

#define	FRONT	1
#define	BACK	2

int dopageout = 1;	/* must be non-zero to turn page stealing on */

/*
 * The page out daemon, which runs as process 2.
 *
 * As long as there are at least lotsfree pages,
 * this process is not run.  When the number of free
 * pages stays in the range desfree to lotsfree,
 * this daemon runs through the pages in the loop
 * at a rate determined in schedpaging().  Pageout manages
 * two hands on the clock.  The front hand moves through
 * memory, clearing the reference bit,
 * and stealing pages from procs that are over maxrss.
 * The back hand travels a distance behind the front hand,
 * freeing the pages that have not been referenced in the time
 * since the front hand passed.  If modified, they are pushed to
 * swap before being freed.
 *
 * There are 2 threads that act on behalf of the pageout process.
 * One thread scans pages (pageout_scanner) and frees them up if
 * they don't require any VOP_PUTPAGE operation. If a page must be
 * written back to its backing store, the request is put on a list
 * and the other (pageout) thread is signaled. The pageout thread
 * grabs VOP_PUTPAGE requests from the list, and processes them.
 * Some filesystems may require resources for the VOP_PUTPAGE
 * operations (like memory) and hence can block the pageout
 * thread, but the scanner thread can still operate. There is still
 * no gaurentee that memory deadlocks cannot occur.
 *
 * For now, this thing is in very rough form.
 */
void
pageout()
{
	struct async_reqs *arg;
	pri_t pageout_pri;
	int i;
	u_int max_pushes;
	callb_cpr_t cprinfo;

	proc_pageout = ttoproc(curthread);
	proc_pageout->p_cstime = 0;
	proc_pageout->p_stime =  0;
	proc_pageout->p_cutime =  0;
	proc_pageout->p_utime = 0;
	bcopy("pageout", u.u_psargs, 8);
	bcopy("pageout", u.u_comm, 7);

	/*
	 * Create pageout scanner thread
	 */
	mutex_init(&pageout_mutex, "pageout", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&push_lock, "pageout req", MUTEX_DEFAULT, DEFAULT_WT);

	/*
	 * Allocate and initialize the async request structures
	 * for pageout.
	 */
	push_req = (struct async_reqs *)
	    kmem_zalloc(async_list_size * sizeof (struct async_reqs), KM_SLEEP);

	req_freelist = push_req;
	for (i = 0; i < async_list_size - 1; i++)
		push_req[i].a_next = &push_req[i + 1];

	pageout_pri = curthread->t_pri;
	if (thread_create(NULL, PAGESIZE, pageout_scanner, 0, 0,
	    proc_pageout, TS_RUN, pageout_pri - 1) == NULL)
		cmn_err(CE_PANIC, "Can't create pageout thread");

	/*
	 * kick off pageout scheduler.
	 */
	schedpaging();

	/*
	 * Limit pushes to avoid saturating pageout devices.
	 */
	max_pushes = maxpgio / RATETOSCHEDPAGING;
	CALLB_CPR_INIT(&cprinfo, &push_lock, callb_generic_cpr, "pageout");

	for (;;) {
		mutex_enter(&push_lock);
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		while ((arg = push_list) == NULL || pushes > max_pushes) {
			cv_wait(&push_cv, &push_lock);
			pushes = 0;
		}
		CALLB_CPR_SAFE_END(&cprinfo, &push_lock);
		push_list = arg->a_next;
		arg->a_next = NULL;
		mutex_exit(&push_lock);

		if (VOP_PUTPAGE(arg->a_vp, arg->a_off, arg->a_len, arg->a_flags,
			    arg->a_cred) == 0) {
			pushes++;
		}

		/* vp held by check_page */
		VN_RELE(arg->a_vp);

		mutex_enter(&push_lock);
		arg->a_next = req_freelist;	/* back on freelist */
		req_freelist = arg;
		push_list_size--;
		mutex_exit(&push_lock);
	}
}

clock_t pageout_lbolt;
int pageout_ticks;		/* number of ticks to be used by pageout */

/*
 * Kernel thread that scans pages looking for ones to free
 */
void
pageout_scanner(void)
{
	register struct page *fronthand, *backhand;
	register int count;
	callb_cpr_t cprinfo;

	CALLB_CPR_INIT(&cprinfo, &pageout_mutex, callb_generic_cpr, "poscan");
	mutex_enter(&pageout_mutex);

	/*
	 * Set the two clock hands to be separated by a reasonable amount,
	 * but no more than 360 degrees apart.
	 *
	 * N.B.: assumes pages are all contiguous.
	 */
	backhand = pages;
	fronthand = pages + handspreadpages;
	if (fronthand >= epages)
		fronthand = epages - 1;

	pageout_ticks = ((HZ * percent_cpu) / 100) / RATETOSCHEDPAGING;
	if (pageout_ticks == 0)
		cmn_err(CE_PANIC, "pageout_scanner: pageout_ticks is: %d",
		    pageout_ticks);
loop:
	cv_signal_pageout();

	CALLB_CPR_SAFE_BEGIN(&cprinfo);
	cv_wait(&proc_pageout->p_cv, &pageout_mutex);
	CALLB_CPR_SAFE_END(&cprinfo, &pageout_mutex);

	if (!dopageout)
		goto loop;

	CPU_STAT_ADDQ(CPU, cpu_vminfo.pgrrun, 1);
	count = 0;

	TRACE_4(TR_FAC_VM, TR_PAGEOUT_START,
		"pageout_start:freemem %d lotsfree %d nscan %d desscan %d",
		freemem, lotsfree, nscan, desscan);

	/* Kernel probe */
	TNF_PROBE_2(pageout_scan_start, "vm pagedaemon", /* CSTYLED */,
		tnf_ulong, pages_free, freemem,
		tnf_ulong, pages_needed, needfree);

	pageout_lbolt = lbolt + pageout_ticks;

	while (lbolt < pageout_lbolt &&
	    nscan < desscan && freemem < lotsfree + needfree) {
		register int rvfront, rvback;

		/*
		 * If checkpage manages to add a page to the free list,
		 * we give ourselves another couple of trips around the loop.
		 */
		if ((rvfront = checkpage(fronthand, FRONT)) == 1)
			count = 0;
		if ((rvback = checkpage(backhand, BACK)) == 1)
			count = 0;

		/*
		 * protected by pageout_mutex instead of cpu_stat_lock
		 */
		CPU_STAT_ADDQ(CPU, cpu_vminfo.scan, 1);

		/*
		 * Don't include ineligible pages in the number scanned.
		 */
		if (rvfront != -1 || rvback != -1)
			nscan++;

		if (++backhand >= epages) {
			TRACE_2(TR_FAC_VM, TR_PAGEOUT_HAND_WRAP,
				"pageout_hand_wrap:freemem %d whichhand %d",
				freemem, BACK);
			backhand = pages;
		}
		if (++fronthand >= epages) {
			TRACE_2(TR_FAC_VM, TR_PAGEOUT_HAND_WRAP,
				"pageout_hand_wrap:freemem %d whichhand %d",
				freemem, FRONT);
			fronthand = pages;
			/*
			 * protected by pageout_mutex instead of cpu_stat_lock
			 */
			CPU_STAT_ADDQ(CPU, cpu_vminfo.rev, 1);
			if (++count > 2) {
				/*
				 * Extremely unlikely, but we went around the
				 * loop at least twice and didn't get anywhere.
				 * Don't cycle, stop till the next clock tick.
				 */
				goto loop;
			}
		}
	}

	TRACE_4(TR_FAC_VM, TR_PAGEOUT_END,
		"pageout_end:freemem %d lotsfree %d nscan %d desscan %d",
		freemem, lotsfree, nscan, desscan);

	/* Kernel probe */
	TNF_PROBE_2(pageout_scan_end, "vm pagedaemon", /* CSTYLED */,
		tnf_ulong, pages_scanned, nscan,
		tnf_ulong, pages_free, freemem);

	goto loop;
}

/*
 * If the page is being shared more than "share_trigger" times
 * then leave it alone.
 */
u_int	share_trigger = 8;


/*
 * An iteration of the clock pointer (hand) around the loop.
 * Look at the page at hand.  If it is locked (e.g., for physical i/o),
 * system (u., page table) or free, then leave it alone.  Otherwise,
 * if we are running the front hand, turn off the page's reference bit.
 * If the proc is over maxrss, we take it.  If running the back hand,
 * check whether the page has been reclaimed.  If not, free the page,
 * pushing it to disk first if necessary.
 *
 * Return values:
 *	-1 if the page was locked and therefore not a candidate at all,
 *	 0 if not freed, or
 *	 1 if we freed it.
 */
static int
checkpage(pp, whichhand)
	register struct page *pp;
	int whichhand;
{

	/*
	 * Skip pages associated with the kernel vnode since
	 * they are always "exclusively" locked.
	 *
	 * NOTE:  These optimizations assume that reads are atomic.
	 */
	if (pp->p_vnode == &kvp || pp->p_share > share_trigger ||
	    pp->p_free || !se_nolock(&pp->p_selock))
		return (-1);

	if (!page_trylock(pp, SE_EXCL)) {
		/*
		 * Skip the page if we can't acquire the "exclusive" lock.
		 */
		return (-1);
	} else if (pp->p_free) {
		/*
		 * It became free between the above check and our actually
		 * locking the page.  Oh, well there will be other pages.
		 */
		page_unlock(pp);
		return (-1);
	}

	/*
	 * Reject pages that cannot be freed. The page_struct_lock
	 * need not be acquired to examine these
	 * fields since the page has an "exclusive" lock.
	 */
	if (pp->p_lckcnt != 0 || pp->p_cowcnt != 0) {
		page_unlock(pp);
		return (-1);
	}

	/*
	 * XXX - Where do we simulate reference bits for
	 * stupid machines (like the vax) that don't have them?
	 *
	 * hat_pagesync will turn off ref and mod bits loaded
	 * into the hardware (front hand only).
	 */
	if (whichhand == FRONT)
		hat_pagesync(pp, HAT_ZERORM);
	else if (!PP_ISREF(pp))
		hat_pagesync(pp, HAT_DONTZERO | HAT_STOPON_REF);

recheck:
	/*
	 * If page is referenced; make unreferenced but reclaimable.
	 * If this page is not referenced, then it must be reclaimable
	 * and we can add it to the free list.
	 */
	if (PP_ISREF(pp)) {
		TRACE_4(TR_FAC_VM, TR_PAGEOUT_ISREF,
			"pageout_isref:pp %x vp %x offset %x whichhand %d",
			pp, pp->p_vnode, pp->p_offset, whichhand);
		if (whichhand == FRONT) {
			/*
			 * Checking of rss or madvise flags needed here...
			 *
			 * If not "well-behaved", fall through into the code
			 * for not referenced.
			 */
			hat_clrref(pp);
		}
		/*
		 * Somebody referenced the page since the front
		 * hand went by, so it's not a candidate for
		 * freeing up.
		 */
		page_unlock(pp);
		return (0);
	}

	/*
	 * If the page is currently dirty, we have to arrange
	 * to have it cleaned before it can be freed.
	 *
	 * XXX - ASSERT(pp->p_vnode != NULL);
	 */
	if (PP_ISMOD(pp) && pp->p_vnode) {
		struct vnode *vp = pp->p_vnode;
		u_int offset = pp->p_offset;

		/*
		 * XXX - Test for process being swapped out or about to exit?
		 * [Can't get back to process(es) using the page.]
		 */

		/*
		 * Hold the vnode before releasing the page lock to
		 * prevent it from being freed and re-used by some
		 * other thread.
		 */
		VN_HOLD(vp);
		page_unlock(pp);

		/*
		 * Queue i/o request for the pageout thread.
		 */
		if (!queue_io_request(vp, offset)) {
			VN_RELE(vp);
			return (0);
		}
		return (1);
	}

	/*
	 * Now we unload all the translations,
	 * and put the page back on to the free list.
	 * If the page was used (referenced or modified) after
	 * the pagesync but before it was unloaded we catch it
	 * and handle the page properly.
	 */
	TRACE_5(TR_FAC_VM, TR_PAGEOUT_FREE,
		"pageout_free:pp %x vp %x offset %x whichhand %d freemem %d",
		pp, pp->p_vnode, pp->p_offset, whichhand, freemem);
	hat_pageunload(pp);
	if (PP_ISREF(pp) || (PP_ISMOD(pp) && pp->p_vnode))
		goto recheck;

	/*LINTED: constant in conditional context*/
	VN_DISPOSE(pp, B_FREE, 0, kcred);

	CPU_STAT_ADD_K(cpu_vminfo.dfree, 1);
	return (1);		/* freed a page! */
}

/*
 * Queue async i/o request from pageout_scanner and segment swapout
 * routines on one common list.  This ensures that pageout devices (swap)
 * are not saturated by pageout_scanner or swapout requests.
 * The pageout thread empties this list by initiating i/o operations.
 */
int
queue_io_request(vp, off)
	struct vnode *vp;
	u_int off;
{
	struct async_reqs *arg;

	/*
	 * If we cannot allocate an async request struct,
	 * skip this page.
	 */
	mutex_enter(&push_lock);
	if ((arg = req_freelist) == NULL) {
		mutex_exit(&push_lock);
		return (0);
	}
	req_freelist = arg->a_next;		/* adjust freelist */
	push_list_size++;

	arg->a_vp = vp;
	arg->a_off = off;
	arg->a_len = PAGESIZE;
	arg->a_flags = B_ASYNC | B_FREE;
	arg->a_cred = kcred;		/* always held */

	/*
	 * Add to list of pending write requests.
	 */
	arg->a_next = push_list;
	push_list = arg;
	mutex_exit(&push_lock);
	return (1);
}

/*
 * Wakeup pageout to initiate i/o if push_list is not empty.
 */
void
cv_signal_pageout()
{
	if (push_list != NULL) {
		mutex_enter(&push_lock);
		cv_signal(&push_cv);
		mutex_exit(&push_lock);
	}
}
