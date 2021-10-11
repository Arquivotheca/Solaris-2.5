/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)stream.c	1.100	95/10/13 SMI"
/*	From:	SVr4.0	"kernel:io/stream.c	1.47"		*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/thread.h>
#include <sys/sysmacros.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/conf.h>
#include <sys/var.h>
#include <sys/debug.h>
#include <sys/inline.h>
#include <sys/tuneable.h>
#include <sys/map.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <sys/vtrace.h>

/*
 * This file contains all the STREAMS utility routines that may
 * be used by modules and drivers.
 */

extern int 		run_queues;
extern queue_t *qhead;
extern kcondvar_t services_to_run;
extern queue_t *qtail;
extern char qrunflag;
extern struct bclist strbcalls;
extern kcondvar_t free_external_buf;
extern kmutex_t callback_frees;
extern struct free_rtn	*frees;
#ifdef TRACE
extern int enqueued;
#endif /* TRACE */
static kmutex_t		strreflock;

static int mh_sizes[] = {
	112,	160,	224,	384,
	672,	1152,	1632,	2048,
	2720,	4096,	MH_MAX_CACHE,
	0
};

struct kmem_cache *mh_table[MH_MAX_CACHE / MH_ALIGN];
static struct kmem_cache *esb_header_cache;
static struct kmem_cache *dupb_header_cache;

void
mh_constructor(void *buf, size_t total_size)
{
	MHP mh = buf;
	int msg_size = total_size - sizeof (MH);

	mh->mh_mblk.b_datap = &mh->mh_dblk;
	mh->mh_dblk.db_frtnp = NULL;
	mh->mh_dblk.db_base = (unsigned char *)&mh[1];
	mh->mh_dblk.db_lim = ((unsigned char *)&mh[1]) + msg_size;
	if (total_size > MH_MAX_CACHE)
		mh->mh_dblk.db_cache = NULL;
	else
		mh->mh_dblk.db_cache = mh_table[(total_size - 1) >>
			MH_ALIGN_SHIFT];
}

/*ARGSUSED1*/
static void
esb_header_constructor(void *buf, size_t total_size)
{
	MHP mh = buf;

	mh->mh_mblk.b_datap = &mh->mh_dblk;
	mh->mh_dblk.db_cache = esb_header_cache;
}

static void
dupb_header_constructor(void *buf, size_t total_size)
{
	MHP mh = buf;
	int msg_size = total_size - sizeof (MH);

	mh->mh_mblk.b_datap = &mh->mh_dblk;
	mh->mh_dblk.db_frtnp = NULL;
	mh->mh_dblk.db_base = (unsigned char *)&mh[1];
	mh->mh_dblk.db_lim = ((unsigned char *)&mh[1]) + msg_size;
	mh->mh_dblk.db_cache = dupb_header_cache;
}

void
mhinit()
{
	char name[40];
	int size, lastsize = MH_ALIGN, *mh_sizep;
	struct kmem_cache *mh_cache;

	for (mh_sizep = mh_sizes; (size = *mh_sizep) != 0; mh_sizep++) {
		(void) sprintf(name, "streams_msg_%d", size - sizeof (MH));
		mh_cache = kmem_cache_create(name, size, 0,
			mh_constructor, NULL, NULL);
		while (lastsize <= size) {
			mh_table[(lastsize - 1) >> MH_ALIGN_SHIFT] = mh_cache;
			lastsize += MH_ALIGN;
		}
	}

	dupb_header_cache = kmem_cache_create("streams_msg_dup",
		sizeof (MH), 0, dupb_header_constructor, NULL, NULL);

	esb_header_cache = kmem_cache_create("streams_msg_esb",
		sizeof (MH), 0, esb_header_constructor, NULL, NULL);

	mutex_init(&strreflock, "db_ref lock", MUTEX_DEFAULT, NULL);
}

/*
 * Pass off the free routine to an async free callback thread
 * to avoid single-party deadlock on callback.
 */
static void
freecallback(frtn_t *frp)
{
	if (frp->free_flags == 0)
		(*frp->free_func)(frp->free_arg);
	else {
		mutex_enter(&callback_frees);
		frp->free_next = frees;
		frees = frp;
		cv_signal(&free_external_buf);
		mutex_exit(&callback_frees);
	}
}

void
freeb(mblk_t *bp)
{
	MHP	mh;
	frtn_t	*frp;
	struct kmem_cache *mh_cache;

	ASSERT(bp);
	ASSERT(bp->b_datap->db_ref > 0);
	ASSERT(bp->b_next == NULL && bp->b_prev == NULL);

	mh = (MHP)&((mblk_t *)((MHP)bp)->mh_mblk.b_datap)[-1];
	if (mh == (MHP)bp) {
		if (mh->mh_dblk.db_ref != 1 &&
		    mh->mh_dblk.db_ref != mh->mh_dblk.db_refmin) {
			mutex_enter(&strreflock);
			if (--mh->mh_dblk.db_ref >= mh->mh_dblk.db_refmin) {
				mutex_exit(&strreflock);
				return;
			}
			mutex_exit(&strreflock);
		}
		goto reclaim;
	}
	bp->b_datap = &((MHP)bp)->mh_dblk;
	if (mh->mh_dblk.db_ref >= 1 &&
	    mh->mh_dblk.db_ref >= mh->mh_dblk.db_refmin) {
		mutex_enter(&strreflock);
		if (--mh->mh_dblk.db_ref >= mh->mh_dblk.db_refmin) {
			mutex_exit(&strreflock);
			goto next_one;
		}
		mutex_exit(&strreflock);
	}

	if (mh->mh_mblk.b_datap != &mh->mh_dblk) {
		/*
		 * We have the case of dup combined with a pullupmsg
		 * thus we cannot free the original mblk/dblk pair since
		 * that mblk has been pulled up (hence refers to another dblk)
		 * and the mblk has not yet been freed. (If it had been freed
		 * then b_datap would point to the attached dblk.)
		 */
		goto next_one;
	}

	frp = mh->mh_dblk.db_frtnp;
	mh_cache = mh->mh_dblk.db_cache;
	if (frp) {
		freecallback(frp);
		kmem_cache_free(mh_cache, mh);
	} else {
		if (mh_cache == NULL) {
			kmem_free(mh, mh->mh_dblk.db_lim - (u_char *)mh);
		} else {
			kmem_cache_free(mh_cache, mh);
		}
	}

next_one:
	mh = (MHP)bp;
	if (mh->mh_dblk.db_ref != 0 &&
	    mh->mh_dblk.db_ref != mh->mh_dblk.db_refmin - 1) {
		/*
		 * Case when dup and pullupmsg has been used and we are
		 * freeing the pulled up message. We have to keep this
		 * mblk/dblk since the dblk is referenced by some other mblk.
		 */
		return;
	}

reclaim:
	frp = mh->mh_dblk.db_frtnp;
	mh_cache = mh->mh_dblk.db_cache;
	if (frp) {
		freecallback(frp);
		kmem_cache_free(mh_cache, mh);
	} else {
		if (mh_cache == NULL) {
			kmem_free(mh, mh->mh_dblk.db_lim - (u_char *)mh);
		} else {
			kmem_cache_free(mh_cache, mh);
		}
	}
}

/*ARGSUSED1*/
mblk_t *
allocb(int size, uint pri)
{
	MHP	mh;

	if (size > MH_MAX_CACHE - sizeof (MH)) {
		mh = kmem_alloc(size + sizeof (MH), KM_NOSLEEP);
		if (mh == NULL)
			return (NULL);
		mh_constructor(mh, size + sizeof (MH));
	} else {
		mh = kmem_cache_alloc(mh_table[(size + (sizeof (MH) - 1)) >>
			MH_ALIGN_SHIFT], KM_NOSLEEP);
		if (mh == NULL)
			return (NULL);
	}

	mh->mh_mblk.b_next = NULL;
	mh->mh_mblk.b_prev = NULL;
	mh->mh_mblk.b_cont = NULL;
	mh->mh_mblk.b_rptr = (unsigned char *)&mh[1];
	mh->mh_mblk.b_wptr = (unsigned char *)&mh[1];
	MH_SET_BAND_FLAG(&mh->mh_mblk.b_band, 0);
	MH_SET_REF_TYPE_REFMIN_UIOFLAG(&mh->mh_dblk.db_ref, 1, M_DATA, 1, 0);

	return (&mh->mh_mblk);
}

int allocb_tryhard_fails;

/*ARGSUSED1*/
static mblk_t *
allocb_tryhard(int target_size, uint pri)
{
	int size;
	mblk_t *bp;

	for (size = target_size; size < target_size + 256; size += MH_ALIGN)
		if ((bp = allocb(size, pri)) != NULL)
			return (bp);
	allocb_tryhard_fails++;
	return (NULL);
}

mblk_t *
dupb(mblk_t *bp)
{
	MHP mh;
	dblk_t *dbp;

	dbp = bp->b_datap;

	mh = kmem_cache_alloc(dupb_header_cache, KM_NOSLEEP);
	if (mh == NULL)
		return (NULL);

	mutex_enter(&strreflock);
	ASSERT(dbp->db_ref > 0);
	if (dbp->db_ref == 255) {
		mutex_exit(&strreflock);
		kmem_cache_free(dupb_header_cache, mh);
		return (NULL);
	}
	/* paranoid */
	ASSERT(dbp->db_ref < 255);
	dbp->db_ref++;
	mutex_exit(&strreflock);

	mh->mh_mblk.b_next = NULL;
	mh->mh_mblk.b_prev = NULL;
	mh->mh_mblk.b_cont = NULL;
	mh->mh_mblk.b_rptr = bp->b_rptr;
	mh->mh_mblk.b_wptr = bp->b_wptr;
	mh->mh_mblk.b_datap = dbp;
	MH_SET_BAND_FLAG(&mh->mh_mblk.b_band, MH_GET_BAND_FLAG(&bp->b_band));
	MH_SET_REF_TYPE_REFMIN_UIOFLAG(&mh->mh_dblk.db_ref, 0, M_DATA, 1, 0);

	return (&mh->mh_mblk);
}

/*
 * XXX unexposed experimental direct-callback version of esballoc
 * Never called by unsafe drivers.
 */
/*ARGSUSED1*/
mblk_t *
desballoc(unsigned char *base, int size, int pri, frtn_t *fr_rtn)
{
	MHP mh;

	ASSERT(base != NULL && fr_rtn != NULL);

	mh = kmem_cache_alloc(esb_header_cache, KM_NOSLEEP);
	if (mh == NULL)
		return (NULL);

	mh->mh_mblk.b_next = NULL;
	mh->mh_mblk.b_prev = NULL;
	mh->mh_mblk.b_cont = NULL;
	mh->mh_mblk.b_rptr = base;
	mh->mh_mblk.b_wptr = base;
	MH_SET_BAND_FLAG(&mh->mh_mblk.b_band, 0);

	mh->mh_dblk.db_frtnp = fr_rtn;
	mh->mh_dblk.db_base = base;
	mh->mh_dblk.db_lim = base + size;
	MH_SET_REF_TYPE_REFMIN_UIOFLAG(&mh->mh_dblk.db_ref, 1, M_DATA, 1, 0);

	fr_rtn->free_flags = 0;

	return (&mh->mh_mblk);
}

mblk_t *
esballoc(unsigned char *base, int size, int pri, frtn_t *fr_rtn)
{
	mblk_t *mp;

	if (mp = desballoc(base, size, pri, fr_rtn))
		if (UNSAFE_DRIVER_LOCK_HELD())
			fr_rtn->free_flags = STRFREE_DEFCALLBACK |
				STRFREE_UNSAFE;
		else
			fr_rtn->free_flags = STRFREE_DEFCALLBACK;
	return (mp);
}

/*
 * XXX unexposed "read-only" version of esballoc for NFS,
 * hopefully going away soon.
 */
/*ARGSUSED1*/
mblk_t *
esballoca(unsigned char *base, int size, int pri, frtn_t *fr_rtn)
{
	MHP mh;

	ASSERT(base != NULL && fr_rtn != NULL);

	mh = kmem_cache_alloc(esb_header_cache, KM_NOSLEEP);
	if (mh == NULL)
		return (NULL);

	mh->mh_mblk.b_next = NULL;
	mh->mh_mblk.b_prev = NULL;
	mh->mh_mblk.b_cont = NULL;
	mh->mh_mblk.b_rptr = base;
	mh->mh_mblk.b_wptr = base;
	MH_SET_BAND_FLAG(&mh->mh_mblk.b_band, 0);

	mh->mh_dblk.db_frtnp = fr_rtn;
	mh->mh_dblk.db_base = base;
	mh->mh_dblk.db_lim = base + size;
	MH_SET_REF_TYPE_REFMIN_UIOFLAG(&mh->mh_dblk.db_ref, 2, M_DATA, 2, 0);

	fr_rtn->free_flags = STRFREE_DEFCALLBACK;

	return (&mh->mh_mblk);
}

/*
 * Call function 'func' with 'arg' when a class zero block can
 * be allocated with priority 'pri'.
 */
/* ARGSUSED */
int
esbbcall(int pri, void (*func)(), long arg)
{
	return (bufcall(1, pri, func, arg));
}

/*
 * test if block of given size can be allocated with a request of
 * the given priority.
 * 'pri' is no longer used, but is retained for compatibility.
 */
/* ARGSUSED */
int
testb(int size, u_int pri)
{
	return ((size + sizeof (MH)) <= (int)kmem_avail());
}

/*
 * Call function 'func' with argument 'arg' when there is a reasonably
 * good chance that a block of size 'size' can be allocated.
 * 'pri' is no longer used, but is retained for compatibility.
 */
/* ARGSUSED */
int
bufcall(u_int size, int pri, void (*func)(), long arg)
{
	static u_short bid = 0;
	struct strbufcall *bcp;

	if ((bcp = kmem_cache_alloc(bufcall_cache, KM_NOSLEEP)) == NULL) {
		cmn_err(CE_WARN, "bufcall: could not allocate stream event\n");
		return (0);
	}

	bcp->bc_func = func;
	bcp->bc_arg = arg;
	bcp->bc_size = size;
	bcp->bc_unsafe = UNSAFE_DRIVER_LOCK_HELD() ? 1 : 0;
	bcp->bc_next = NULL;
	bcp->bc_executor = NULL;

	mutex_enter(&service_queue);
	if ((bcp->bc_id = ++bid) == 0)
		bcp->bc_id = bid = 1;
	/*
	 * add newly allocated stream event to existing
	 * linked list of events.
	 */
	if (strbcalls.bc_head == NULL) {
		strbcalls.bc_head = strbcalls.bc_tail = bcp;
	} else {
		strbcalls.bc_tail->bc_next = bcp;
		strbcalls.bc_tail = bcp;
	}

	mutex_exit(&service_queue);
	return ((int)bcp->bc_id);
}

/*
 * Cancel a bufcall request.
 */
void
unbufcall(int id)
{
	extern kcondvar_t bcall_cv;
	strbufcall_t *bcp, *pbcp;
	int unsafe = 0;

	mutex_enter(&service_queue);
again:
	pbcp = NULL;
	for (bcp = strbcalls.bc_head; bcp; bcp = bcp->bc_next) {
		if (id == (int)bcp->bc_id)
			break;
		pbcp = bcp;
	}
	if (bcp) {
		if (bcp->bc_executor != NULL) {
			if (bcp->bc_executor != curthread) {
				if (bcp->bc_unsafe) {
					unsafe = 1;
					ASSERT(UNSAFE_DRIVER_LOCK_HELD());
					mutex_exit(&unsafe_driver);
				}
				cv_wait(&bcall_cv, &service_queue);
				if (unsafe) {
					mutex_exit(&service_queue);
					mutex_enter(&unsafe_driver);
					mutex_enter(&service_queue);
				}
				goto again;
			}
		} else {
			if (pbcp)
				pbcp->bc_next = bcp->bc_next;
			else
				strbcalls.bc_head = bcp->bc_next;
			if (bcp == strbcalls.bc_tail)
				strbcalls.bc_tail = pbcp;
			kmem_cache_free(bufcall_cache, bcp);
		}
	}
	mutex_exit(&service_queue);
}


/*
 * Free all message blocks in a message using freeb().
 * The message may be NULL.
 */
void
freemsg(mblk_t *bp)
{
	register mblk_t *tp;

	while (bp) {
		tp = bp->b_cont;
		freeb(bp);
		bp = tp;
	}
}

/*
 * Duplicate a message block by block (uses dupb), returning
 * a pointer to the duplicate message.
 * Returns a non-NULL value only if the entire message
 * was dup'd.
 */
mblk_t *
dupmsg(mblk_t *bp)
{
	register mblk_t *head, *nbp;

	if (!bp || !(nbp = head = dupb(bp)))
		return (NULL);

	while (bp->b_cont) {
		if (!(nbp->b_cont = dupb(bp->b_cont))) {
			freemsg(head);
			return (NULL);
		}
		nbp = nbp->b_cont;
		bp = bp->b_cont;
	}
	return (head);
}

/*
 * Copy data from message block to newly allocated message block and
 * data block.  The copy is rounded out to full word boundaries so that
 * the (usually) more efficient word copy can be done.
 * Returns new message block pointer, or NULL if error.
 * The allignment of rptr (w.r.t. word allignment) will be the same in the copy
 * as in the original even when db_base is not word alligned. (bug 1052877)
 */
mblk_t *
copyb(mblk_t *bp)
{
	register mblk_t *nbp;
	register dblk_t *dp, *ndp;
	caddr_t base;
	int	unalligned;

	ASSERT(bp);
	ASSERT(bp->b_wptr >= bp->b_rptr);

	dp = bp->b_datap;
	unalligned = (int)dp->db_base & (sizeof (int) - 1);
	if (!(nbp = allocb(dp->db_lim - dp->db_base + unalligned, BPRI_MED)))
		return (NULL);
	nbp->b_flag = bp->b_flag;
	nbp->b_band = bp->b_band;
	ndp = nbp->b_datap;
	ndp->db_type = dp->db_type;
	nbp->b_rptr = ndp->db_base + (bp->b_rptr - dp->db_base) + unalligned;
	nbp->b_wptr = nbp->b_rptr + (bp->b_wptr - bp->b_rptr);
	base = straln(nbp->b_rptr);
	strbcpy(straln(bp->b_rptr), base,
	    straln(nbp->b_wptr + (sizeof (int)-1)) - base);
	return (nbp);
}


/*
 * Copy data from message to newly allocated message using new
 * data blocks.  Returns a pointer to the new message, or NULL if error.
 */
mblk_t *
copymsg(mblk_t *bp)
{
	register mblk_t *head, *nbp;

	if (!bp || !(nbp = head = copyb(bp)))
		return (NULL);

	while (bp->b_cont) {
		if (!(nbp->b_cont = copyb(bp->b_cont))) {
			freemsg(head);
			return (NULL);
		}
		nbp = nbp->b_cont;
		bp = bp->b_cont;
	}
	return (head);
}

/*
 * link a message block to tail of message
 */
void
linkb(mblk_t *mp, mblk_t *bp)
{
	ASSERT(mp && bp);

	for (; mp->b_cont; mp = mp->b_cont)
		;
	mp->b_cont = bp;
}

/*
 * unlink a message block from head of message
 * return pointer to new message.
 * NULL if message becomes empty.
 */
mblk_t *
unlinkb(mblk_t *bp)
{
	register mblk_t *bp1;

	ASSERT(bp);

	bp1 = bp->b_cont;
	bp->b_cont = NULL;
	return (bp1);
}

/*
 * remove a message block "bp" from message "mp"
 *
 * Return pointer to new message or NULL if no message remains.
 * Return -1 if bp is not found in message.
 */
mblk_t *
rmvb(mblk_t *mp, mblk_t *bp)
{
	register mblk_t *tmp;
	register mblk_t *lastp = NULL;

	ASSERT(mp && bp);
	for (tmp = mp; tmp; tmp = tmp->b_cont) {
		if (tmp == bp) {
			if (lastp)
				lastp->b_cont = tmp->b_cont;
			else
				mp = tmp->b_cont;
			tmp->b_cont = NULL;
			return (mp);
		}
		lastp = tmp;
	}
	return ((mblk_t *)-1);
}

/*
 * Concatenate and align first len bytes of common
 * message type.  Len == -1, means concat everything.
 * Returns 1 on success, 0 on failure
 * After the pullup, mp points to the pulled up data.
 * This is convenient but messy to implement.
 */
int
pullupmsg(mblk_t *mp, int len)
{
	register mblk_t *bp;
	register mblk_t *new_bp;
	register n;
	MHP mh;

	ASSERT(mp != NULL);

	/*
	 * Quick checks for success or failure:
	 */
	if (len == -1) {
		if (mp->b_cont == NULL && str_aligned(mp->b_rptr))
			return (1);
		len = xmsgsize(mp);
	} else {
		ASSERT(mp->b_wptr >= mp->b_rptr);
		if (mp->b_wptr - mp->b_rptr >= len && str_aligned(mp->b_rptr))
			return (1);
		if (xmsgsize(mp) < len)
			return (0);
	}

	/*
	 * Allocate the new mblk.  We might be able to use the existing
	 * mblk, but we don't want to modify it in case its shared.
	 * The new dblk takes on the type of the old dblk
	 * If the length is less then that of the first mblk, then
	 * we want to pull up the message into an aligned mblk
	 */
	if (len < (mp->b_wptr - mp->b_rptr))
		len = mp->b_wptr - mp->b_rptr;
	if ((new_bp = allocb(len, BPRI_MED)) == NULL)
		return (0);
	new_bp->b_datap->db_type = mp->b_datap->db_type;

	/*
	 * Scan mblks and copy over data into the new mblk.
	 * Two ways to fall out: exact count match: while (len)
	 * Bp points to the next mblk containing data or is null.
	 * Inexact match: if (bp->b_rptr != ...)  In this case,
	 * bp points to an mblk that still has data in it.
	 */
	bp = mp;
	do {
		mblk_t *b_cont;

		ASSERT(bp->b_datap->db_ref > 0);
		ASSERT(bp->b_wptr >= bp->b_rptr);
		n = MIN(bp->b_wptr - bp->b_rptr, len);
		bcopy((caddr_t)bp->b_rptr, (caddr_t)new_bp->b_wptr, (u_int)n);
		new_bp->b_wptr += n;
		bp->b_rptr += n;
		len -= n;
		if (bp->b_rptr != bp->b_wptr)
			break;
		b_cont = bp->b_cont;
		if (bp != mp)	/* don't free the head mblk */
			freeb(bp);
		bp = b_cont;
	} while (len && bp);

	ASSERT(bp != mp);

	new_bp->b_flag = mp->b_flag;
	new_bp->b_band = mp->b_band;

	/*
	 * At this point:  new_bp points to a dblk that
	 * contains the pulled up data.  The head mblk, mp, is
	 * preserved but does not have any data in it.  The
	 * intermediate mblks are freed, and bp points to the
	 * last mblk that was pulled-up or is null.
	 * Note: the head mblk could be pointing to
	 * some other data block and may need to be freed.
	 *
	 * Now the tricky bit.  After this, mp points to the new dblk
	 * and tmp points to the old dblk.  New_bp points nowhere
	 */

	mutex_enter(&strreflock);
	mh = (MHP)&((mblk_t *)((MHP)mp)->mh_mblk.b_datap)[-1];
	if (mp->b_datap->db_ref == 1 && mh != (MHP)mp &&
	    ((mblk_t *)mh)->b_datap == &mh->mh_dblk) {
		mutex_exit(&strreflock);
		/*
		 * must be freeb.. we don't want to free the b_cont
		 * associated with the dblk/mblk because they
		 * are going to be reconnected to new_bp
		 */
		freeb((mblk_t *)mh);
	} else {
		mp->b_datap->db_ref--;
		mutex_exit(&strreflock);
	}

	*mp = *new_bp;
	mp->b_cont = bp;

	return (1);
}

/*
 * Concatenate and align first len byte of common
 * message type.  Len == -1 means concat everything.
 * The original message is unaltered.
 * Returns a pointer to a new message on success,
 * otherwise returns NULL.
 */
mblk_t *
msgpullup(mblk_t *mp, int len)
{
	mblk_t	*new_bp;
	int	totlen;
	int	n;

	ASSERT(mp);

	totlen = xmsgsize(mp);

	if ((len > 0) && (len > totlen))
		return (NULL);

	/*
	 * Copy all of the first msg type into one new mblk
	 * and dupmsg and link the rest onto this.
	 */

	len = totlen;

	if ((new_bp = allocb(len, BPRI_LO)) == NULL)
		return (NULL);

	new_bp->b_datap->db_type = mp->b_datap->db_type;
	new_bp->b_flag = mp->b_flag;
	new_bp->b_band = mp->b_band;

	while (len > 0) {
		n = mp->b_wptr - mp->b_rptr;
		bcopy((caddr_t)mp->b_rptr, (caddr_t)new_bp->b_wptr, n);
		new_bp->b_wptr += n;
		len -= n;
		mp = mp->b_cont;
	}

	if (mp)
		new_bp->b_cont = dupmsg(mp);

	return (new_bp);
}

/*
 * Trim bytes from message
 *  len > 0, trim from head
 *  len < 0, trim from tail
 * Returns 1 on success, 0 on failure.
 */
int
adjmsg(mblk_t *mp, int len)
{
	register mblk_t *bp;
	register n;
	int fromhead;

	ASSERT(mp != NULL);

	fromhead = 1;
	if (len < 0) {
		fromhead = 0;
		len = -len;
	}
	if (xmsgsize(mp) < len)
		return (0);

	if (fromhead) {
		bp = mp;
		while (len) {
			ASSERT(bp->b_wptr >= bp->b_rptr);
			n = min(bp->b_wptr - bp->b_rptr, len);
			bp->b_rptr += n;
			len -= n;
			bp = bp->b_cont;
		}
	} else {
		register mblk_t *save_bp;
		register unsigned char type;

		type = mp->b_datap->db_type;
		while (len) {
			bp = mp;
			save_bp = NULL;
			while (bp && bp->b_datap->db_type == type) {
				ASSERT(bp->b_wptr >= bp->b_rptr);
				if (bp->b_wptr - bp->b_rptr > 0)
					save_bp = bp;
				bp = bp->b_cont;
			}
			if (!save_bp)
				break;
			n = min(save_bp->b_wptr - save_bp->b_rptr, len);
			save_bp->b_wptr -= n;
			len -= n;
		}
	}
	return (1);
}

/*
 * get number of data bytes in message
 */
int
msgdsize(mblk_t *bp)
{
	register int count = 0;

	for (; bp; bp = bp->b_cont)
		if (bp->b_datap->db_type == M_DATA) {
			ASSERT(bp->b_wptr >= bp->b_rptr);
			count += bp->b_wptr - bp->b_rptr;
		}
	return (count);
}

/*
 * Get a message off head of queue
 *
 * If queue has no buffers then mark queue
 * with QWANTR. (queue wants to be read by
 * someone when data becomes available)
 *
 * If there is something to take off then do so.
 * If queue falls below hi water mark turn off QFULL
 * flag.  Decrement weighted count of queue.
 * Also turn off QWANTR because queue is being read.
 *
 * The queue count is maintained on a per-band basis.
 * Priority band 0 (normal messages) uses q_count,
 * q_lowat, etc.  Non-zero priority bands use the
 * fields in their respective qband structures
 * (qb_count, qb_lowat, etc.)  All messages appear
 * on the same list, linked via their b_next pointers.
 * q_first is the head of the list.  q_count does
 * not reflect the size of all the messages on the
 * queue.  It only reflects those messages in the
 * normal band of flow.  The one exception to this
 * deals with high priority messages.  They are in
 * their own conceptual "band", but are accounted
 * against q_count.
 *
 * If queue count is below the lo water mark and QWANTW
 * is set, enable the closest backq which has a service
 * procedure and turn off the QWANTW flag.
 *
 * getq could be built on top of rmvq, but isn't because
 * of performance considerations.
 */
mblk_t *
getq(queue_t *q)
{
	register mblk_t *bp;
	register mblk_t *tmp;
	register qband_t *qbp;
	int backenab = 0;
	int wantw = 0;
	int wake = 0;
	kthread_id_t freezer;

	ASSERT(q);
	/* freezestr should allow its caller to call getq/putq */
	freezer = STREAM(q)->sd_freezer;
	if (freezer == curthread) {
		ASSERT(frozenstr(q));
		ASSERT(MUTEX_HELD(QLOCK(q)));
	} else
		mutex_enter(QLOCK(q));

	if ((bp = q->q_first) == 0) {
		q->q_flag |= QWANTR;
		if (q->q_count < q->q_lowat || q->q_lowat == 0)
			backenab = 1;		/* we might backenable */
		wantw = q->q_flag & (QWANTW|QWANTWSYNC);
		wake = 1;
	} else {
		if (bp->b_flag & MSGNOGET) {	/* hack hack hack */
			while (bp && (bp->b_flag & MSGNOGET))
				bp = bp->b_next;
			if (bp)
				rmvq(q, bp);
			if (freezer != curthread)
				mutex_exit(QLOCK(q));
			return (bp);
		}
		if ((q->q_first = bp->b_next) == 0)
			q->q_last = NULL;
		else
			q->q_first->b_prev = NULL;

		if (bp->b_band == 0) {
			wantw = q->q_flag & (QWANTW|QWANTWSYNC);
			for (tmp = bp; tmp; tmp = tmp->b_cont)
				q->q_count -= (tmp->b_wptr - tmp->b_rptr);
			if (q->q_count < q->q_hiwat) {
				q->q_flag &= ~QFULL;
				wake = 1;
			}
			if (q->q_count < q->q_lowat || q->q_lowat == 0)
				backenab = 1;
		} else {
			register int i;

			ASSERT(bp->b_band <= q->q_nband);
			ASSERT(q->q_bandp != NULL);
			ASSERT(MUTEX_HELD(QLOCK(q)));
			qbp = q->q_bandp;
			i = bp->b_band;
			while (--i > 0)
				qbp = qbp->qb_next;
			if (qbp->qb_first == qbp->qb_last) {
				qbp->qb_first = NULL;
				qbp->qb_last = NULL;
			} else {
				qbp->qb_first = bp->b_next;
			}
			wantw = qbp->qb_flag & QB_WANTW;
			for (tmp = bp; tmp; tmp = tmp->b_cont)
				qbp->qb_count -= (tmp->b_wptr - tmp->b_rptr);
			if (qbp->qb_count < qbp->qb_hiwat)
				qbp->qb_flag &= ~QB_FULL;
			if (qbp->qb_count < qbp->qb_lowat || qbp->qb_lowat == 0)
				backenab = 1;
		}
		q->q_flag &= ~QWANTR;
		bp->b_next = NULL;
		bp->b_prev = NULL;
	}
	if (wake && (wantw & QWANTWSYNC)) {
		q->q_flag &= ~QWANTWSYNC;
		strwakeq(q, QWANTWSYNC);
	}
	if (backenab && (wantw & (QWANTW|QB_WANTW))) {
		if (bp && bp->b_band != 0)
			qbp->qb_flag &= ~QB_WANTW;
		else {
			q->q_flag &= ~QWANTW;
		}
		if (freezer != curthread)
			mutex_exit(QLOCK(q));
		backenable(q, (int)(bp ? bp->b_band : 0));
	} else {
		if (freezer != curthread)
			mutex_exit(QLOCK(q));
	}
	return (bp);
}


/*
 * Remove a message from a queue.  The queue count and other
 * flow control parameters are adjusted and the back queue
 * enabled if necessary.
 *
 * rmvq can be called with the stream frozen, but other utility functions
 * holding QLOCK, and by streams modules without any locks/frozen.
 */
void
rmvq(queue_t *q, mblk_t *mp)
{
	register mblk_t *tmp;
	register int i;
	register qband_t *qbp = NULL;
	int backenab = 0;
	int wantw = 0;
	int wake = 0;
	kthread_id_t freezer;

	ASSERT(q);
	ASSERT(mp);
	freezer = STREAM(q)->sd_freezer;
	if (freezer == curthread) {
		ASSERT(frozenstr(q));
		ASSERT(MUTEX_HELD(QLOCK(q)));
	} else if (MUTEX_HELD(QLOCK(q))) {
		/* Don't drop lock on exit */
		freezer = curthread;
	} else
		mutex_enter(QLOCK(q));

	ASSERT(mp->b_band <= q->q_nband);
	if (mp->b_band != 0) {		/* Adjust band pointers */
		ASSERT(q->q_bandp != NULL);
		qbp = q->q_bandp;
		i = mp->b_band;
		while (--i > 0)
			qbp = qbp->qb_next;
		if (mp == qbp->qb_first) {
			if (mp->b_next && mp->b_band == mp->b_next->b_band)
				qbp->qb_first = mp->b_next;
			else
				qbp->qb_first = NULL;
		}
		if (mp == qbp->qb_last) {
			if (mp->b_prev && mp->b_band == mp->b_prev->b_band)
				qbp->qb_last = mp->b_prev;
			else
				qbp->qb_last = NULL;
		}
	}

	/*
	 * Remove the message from the list.
	 */
	if (mp->b_prev)
		mp->b_prev->b_next = mp->b_next;
	else
		q->q_first = mp->b_next;
	if (mp->b_next)
		mp->b_next->b_prev = mp->b_prev;
	else
		q->q_last = mp->b_prev;
	mp->b_next = NULL;
	mp->b_prev = NULL;

	if (mp->b_band == 0) {		/* Perform q_count accounting */
		wantw = q->q_flag & (QWANTW|QWANTWSYNC);
		for (tmp = mp; tmp; tmp = tmp->b_cont)
			q->q_count -= (tmp->b_wptr - tmp->b_rptr);
		if (q->q_count < q->q_hiwat) {
			q->q_flag &= ~QFULL;
			wake = 1;
		}
		if (q->q_count < q->q_lowat || q->q_lowat == 0)
			backenab = 1;
	} else {			/* Perform qb_count accounting */
		wantw = qbp->qb_flag & QB_WANTW;
		for (tmp = mp; tmp; tmp = tmp->b_cont)
			qbp->qb_count -= (tmp->b_wptr - tmp->b_rptr);
		if (qbp->qb_count < qbp->qb_hiwat)
			qbp->qb_flag &= ~QB_FULL;
		if (qbp->qb_count < qbp->qb_lowat || qbp->qb_lowat == 0)
			backenab = 1;
	}

	if (wake && (wantw & QWANTWSYNC)) {
		q->q_flag &= ~QWANTWSYNC;
		strwakeq(q, QWANTWSYNC);
	}
	if (backenab && (wantw & (QWANTW|QB_WANTW))) {

		if (mp->b_band != 0)
			qbp->qb_flag &= ~QB_WANTW;
		else {
			q->q_flag &= ~QWANTW;
		}
		mutex_exit(QLOCK(q));
		backenable(q, (int)mp->b_band);
		mutex_enter(QLOCK(q));
	}
	ASSERT(MUTEX_HELD(QLOCK(q)));
	if (freezer != curthread)
		mutex_exit(QLOCK(q));
}

/*
 * Empty a queue.
 * If flag is set, remove all messages.  Otherwise, remove
 * only non-control messages.  If queue falls below its low
 * water mark, and QWANTW is set, enable the nearest upstream
 * service procedure.
 *
 * Historical note: when merging the M_FLUSH code in strrput with this
 * code one difference was discovered. flushq did not have a check
 * for q_lowat == 0 in the backenabling test.
 */
void
flushq(queue_t *q, int flag)
{
	register mblk_t *mp, *nmp;
	register qband_t *qbp;
	int backenab = 0;
	unsigned char bpri;
	unsigned char	qbf[NBAND];	/* band flushing backenable flags */

	ASSERT(q);
	if (q->q_first == NULL)
		return;

	mutex_enter(QLOCK(q));
	mp = q->q_first;
	q->q_first = NULL;
	q->q_last = NULL;
	q->q_count = 0;
	for (qbp = q->q_bandp; qbp; qbp = qbp->qb_next) {
		qbp->qb_first = NULL;
		qbp->qb_last = NULL;
		qbp->qb_count = 0;
		qbp->qb_flag &= ~QB_FULL;
	}
	q->q_flag &= ~QFULL;
	mutex_exit(QLOCK(q));
	while (mp) {
		nmp = mp->b_next;
		mp->b_next = mp->b_prev = NULL;
		if (flag || datamsg(mp->b_datap->db_type))
			freemsg_flush(mp);
		else
			(void) putq(q, mp);
		mp = nmp;
	}
	bpri = 1;
	mutex_enter(QLOCK(q));
	for (qbp = q->q_bandp; qbp; qbp = qbp->qb_next) {
		if ((qbp->qb_flag & QB_WANTW) &&
		    (qbp->qb_count < qbp->qb_lowat || qbp->qb_lowat == 0)) {
			qbp->qb_flag &= ~QB_WANTW;
			backenab = 1;
			qbf[bpri] = 1;
		} else
			qbf[bpri] = 0;
		bpri++;
	}
	ASSERT(bpri == (unsigned char)(q->q_nband + 1));
	if ((q->q_flag & QWANTW) &&
	    (q->q_lowat == 0 || q->q_count < q->q_lowat)) {
		q->q_flag &= ~QWANTW;
		backenab = 1;
		qbf[0] = 1;
	} else
		qbf[0] = 0;

	/*
	 * If any band can now be written to, and there is a writer
	 * for that band, then backenable the closest service procedure.
	 */
	if (backenab) {
		mutex_exit(QLOCK(q));
		for (bpri = q->q_nband; bpri != 0; bpri--)
			if (qbf[bpri])
				backenable(q, (int)bpri);
		if (qbf[0])
			backenable(q, 0);
	} else
		mutex_exit(QLOCK(q));
}

/*
 * Flush the queue of messages of the given priority band.
 * There is some duplication of code between flushq and flushband.
 * This is because we want to optimize the code as much as possible.
 * The assumption is that there will be more messages in the normal
 * (priority 0) band than in any other.
 *
 * Historical note: when merging the M_FLUSH code in strrput with this
 * code one difference was discovered. flushband had an extra check for
 * did not have a check for (mp->b_datap->db_type < QPCTL) in the band 0
 * case. That check does not match the man page for flushband and was not
 * in the strrput flush code hence it was removed.
 */
void
flushband(queue_t *q, unsigned char pri, int flag)
{
	register mblk_t *mp;
	register mblk_t *nmp;
	register mblk_t *last;
	register qband_t *qbp;
	int band;

	ASSERT(q);
	if (pri > q->q_nband) {
		return;
	}
	mutex_enter(QLOCK(q));
	if (pri == 0) {
		mp = q->q_first;
		q->q_first = NULL;
		q->q_last = NULL;
		q->q_count = 0;
		for (qbp = q->q_bandp; qbp; qbp = qbp->qb_next) {
			qbp->qb_first = NULL;
			qbp->qb_last = NULL;
			qbp->qb_count = 0;
			qbp->qb_flag &= ~QB_FULL;
		}
		q->q_flag &= ~QFULL;
		mutex_exit(QLOCK(q));
		while (mp) {
			nmp = mp->b_next;
			mp->b_next = mp->b_prev = NULL;
			if ((mp->b_band == 0) &&
			    (flag || datamsg(mp->b_datap->db_type)))
				freemsg_flush(mp);
			else
				(void) putq(q, mp);
			mp = nmp;
		}
		mutex_enter(QLOCK(q));
		if ((q->q_flag & QWANTW) &&
		    (q->q_lowat == 0 || q->q_count < q->q_lowat)) {
			q->q_flag &= ~QWANTW;
			mutex_exit(QLOCK(q));

			backenable(q, (int)pri);
		} else
			mutex_exit(QLOCK(q));
	} else {	/* pri != 0 */
		ASSERT(q->q_bandp != NULL);
		band = pri;
		ASSERT(MUTEX_HELD(QLOCK(q)));
		qbp = q->q_bandp;
		while (--band > 0)
			qbp = qbp->qb_next;
		mp = qbp->qb_first;
		if (mp == NULL) {
			mutex_exit(QLOCK(q));
			return;
		}
		last = qbp->qb_last;
		if (mp == last)		/* only message in band */
			last = mp->b_next;
		while (mp != last) {
			nmp = mp->b_next;
			if (mp->b_band == pri) {
				if (flag || datamsg(mp->b_datap->db_type)) {
					rmvq(q, mp);
					freemsg_flush(mp);
				}
			}
			mp = nmp;
		}
		if (mp && mp->b_band == pri) {
			if (flag || datamsg(mp->b_datap->db_type)) {
				rmvq(q, mp);
				freemsg_flush(mp);
			}
		}
		mutex_exit(QLOCK(q));
	}
}

/*
 * Return 1 if the queue is not full.  If the queue is full, return
 * 0 (may not put message) and set QWANTW flag (caller wants to write
 * to the queue).
 */
int
canput(queue_t *q)
{

	TRACE_2(TR_FAC_STREAMS_FR, TR_CANPUT_IN,
	    "canput?:%s(%X)\n", QNAME(q), q);

	ASSERT(q != NULL);

	/* this is for loopback transports, they should not do a canput */
	ASSERT(STRMATED(q->q_stream) || STREAM(q) == STREAM(q->q_nfsrv));

	/* Find next forward module that has a service procedure */
	q = q->q_nfsrv;
	ASSERT(q != NULL);

	if (!(q->q_flag & QFULL)) {
		TRACE_2(TR_FAC_STREAMS_FR, TR_CANPUT_OUT, "canput:%X %d", q, 1);
		return (1);
	}
	mutex_enter(QLOCK(q));
	if (q->q_flag & QFULL) {
		q->q_flag |= QWANTW;
		mutex_exit(QLOCK(q));
		TRACE_2(TR_FAC_STREAMS_FR, TR_CANPUT_OUT, "canput:%X %d", q, 0);
		return (0);
	}
	mutex_exit(QLOCK(q));
	TRACE_2(TR_FAC_STREAMS_FR, TR_CANPUT_OUT, "canput:%X %d", q, 1);
	return (1);
}

/*
 * This is the new canput for use with priority bands.  Return 1 if the
 * band is not full.  If the band is full, return 0 (may not put message)
 * and set QWANTW(QB_WANTW) flag for zero(non-zero) band (caller wants to
 * write to the queue).
 */
int
bcanput(queue_t *q, unsigned char pri)
{
	register qband_t *qbp;

	TRACE_2(TR_FAC_STREAMS_FR, TR_BCANPUT_IN, "bcanput?:%X %X", q, pri);
	if (!q)
		return (0);

	/* Find next forward module that has a service procedure */
	q = q->q_nfsrv;
	ASSERT(q != NULL);

	mutex_enter(QLOCK(q));
	if (pri == 0) {
		if (q->q_flag & QFULL) {
			q->q_flag |= QWANTW;
			mutex_exit(QLOCK(q));
			TRACE_3(TR_FAC_STREAMS_FR, TR_BCANPUT_OUT,
				"bcanput:%X %X %d", q, pri, 0);
			return (0);
		}
	} else {	/* pri != 0 */
		if (pri > q->q_nband) {
			/*
			 * No band exists yet, so return success.
			 */
			mutex_exit(QLOCK(q));
			TRACE_3(TR_FAC_STREAMS_FR, TR_BCANPUT_OUT,
				"bcanput:%X %X %d", q, pri, 1);
			return (1);
		}
		qbp = q->q_bandp;
		while (--pri)
			qbp = qbp->qb_next;
		if (qbp->qb_flag & QB_FULL) {
			qbp->qb_flag |= QB_WANTW;
			mutex_exit(QLOCK(q));
			TRACE_3(TR_FAC_STREAMS_FR, TR_BCANPUT_OUT,
				"bcanput:%X %X %d", q, pri, 0);
			return (0);
		}
	}
	mutex_exit(QLOCK(q));
	TRACE_3(TR_FAC_STREAMS_FR, TR_BCANPUT_OUT,
		"bcanput:%X %X %d", q, pri, 1);
	return (1);
}

/*
 * Put a message on a queue.
 *
 * Messages are enqueued on a priority basis.  The priority classes
 * are HIGH PRIORITY (type >= QPCTL), PRIORITY (type < QPCTL && band > 0),
 * and B_NORMAL (type < QPCTL && band == 0).
 *
 * Add appropriate weighted data block sizes to queue count.
 * If queue hits high water mark then set QFULL flag.
 *
 * If QNOENAB is not set (putq is allowed to enable the queue),
 * enable the queue only if the message is PRIORITY,
 * or the QWANTR flag is set (indicating that the service procedure
 * is ready to read the queue.  This implies that a service
 * procedure must NEVER put a high priority message back on its own
 * queue, as this would result in an infinite loop (!).
 */
int
putq(queue_t *q, mblk_t *bp)
{
	register mblk_t *tmp;
	register qband_t *qbp = NULL;
	int mcls = (int)queclass(bp);
	kthread_id_t freezer;

	ASSERT(q && bp);
	freezer = STREAM(q)->sd_freezer;
	if (freezer == curthread) {
		ASSERT(frozenstr(q));
		ASSERT(MUTEX_HELD(QLOCK(q)));
	} else
		mutex_enter(QLOCK(q));

	/*
	 * Make sanity checks and if qband structure is not yet
	 * allocated, do so.
	 */
	if (mcls == QPCTL) {
		if (bp->b_band != 0)
			bp->b_band = 0;		/* force to be correct */
	} else if (bp->b_band != 0) {
		register int i;
		qband_t **qbpp;

		if (bp->b_band > q->q_nband) {

			/*
			 * The qband structure for this priority band is
			 * not on the queue yet, so we have to allocate
			 * one on the fly.  It would be wasteful to
			 * associate the qband structures with every
			 * queue when the queues are allocated.  This is
			 * because most queues will only need the normal
			 * band of flow which can be described entirely
			 * by the queue itself.
			 */
			qbpp = &q->q_bandp;
			while (*qbpp)
				qbpp = &(*qbpp)->qb_next;
			while (bp->b_band > q->q_nband) {
				if ((*qbpp = allocband()) == NULL) {
					if (freezer != curthread)
						mutex_exit(QLOCK(q));
					return (0);
				}
				(*qbpp)->qb_hiwat = q->q_hiwat;
				(*qbpp)->qb_lowat = q->q_lowat;
				q->q_nband++;
				qbpp = &(*qbpp)->qb_next;
			}
		}
		ASSERT(MUTEX_HELD(QLOCK(q)));
		qbp = q->q_bandp;
		i = bp->b_band;
		while (--i)
			qbp = qbp->qb_next;
	}

	/*
	 * If queue is empty, add the message and initialize the pointers.
	 * Otherwise, adjust message pointers and queue pointers based on
	 * the type of the message and where it belongs on the queue.  Some
	 * code is duplicated to minimize the number of conditionals and
	 * hopefully minimize the amount of time this routine takes.
	 */
	if (!q->q_first) {
		bp->b_next = NULL;
		bp->b_prev = NULL;
		q->q_first = bp;
		q->q_last = bp;
		if (qbp) {
			qbp->qb_first = bp;
			qbp->qb_last = bp;
		}
	} else if (!qbp) {	/* bp->b_band == 0 */

		/*
		 * If queue class of message is less than or equal to
		 * that of the last one on the queue, tack on to the end.
		 */
		tmp = q->q_last;
		if (mcls <= (int)queclass(tmp)) {
			bp->b_next = NULL;
			bp->b_prev = tmp;
			tmp->b_next = bp;
			q->q_last = bp;
		} else {
			tmp = q->q_first;
			while ((int)queclass(tmp) >= mcls)
				tmp = tmp->b_next;
			ASSERT(tmp != NULL);

			/*
			 * Insert bp before tmp.
			 */
			bp->b_next = tmp;
			bp->b_prev = tmp->b_prev;
			if (tmp->b_prev)
				tmp->b_prev->b_next = bp;
			else
				q->q_first = bp;
			tmp->b_prev = bp;
		}
	} else {		/* bp->b_band != 0 */
		if (qbp->qb_first) {
			ASSERT(qbp->qb_last != NULL);
			tmp = qbp->qb_last;

			/*
			 * Insert bp after the last message in this band.
			 */
			bp->b_next = tmp->b_next;
			if (tmp->b_next)
				tmp->b_next->b_prev = bp;
			else
				q->q_last = bp;
			bp->b_prev = tmp;
			tmp->b_next = bp;
		} else {
			tmp = q->q_last;
			if ((mcls < (int)queclass(tmp)) ||
			    (bp->b_band <= tmp->b_band)) {

				/*
				 * Tack bp on end of queue.
				 */
				bp->b_next = NULL;
				bp->b_prev = tmp;
				tmp->b_next = bp;
				q->q_last = bp;
			} else {
				tmp = q->q_first;
				while (tmp->b_datap->db_type >= QPCTL)
					tmp = tmp->b_next;
				while (tmp->b_band >= bp->b_band)
					tmp = tmp->b_next;
				ASSERT(tmp != NULL);

				/*
				 * Insert bp before tmp.
				 */
				bp->b_next = tmp;
				bp->b_prev = tmp->b_prev;
				if (tmp->b_prev)
					tmp->b_prev->b_next = bp;
				else
					q->q_first = bp;
				tmp->b_prev = bp;
			}
			qbp->qb_first = bp;
		}
		qbp->qb_last = bp;
	}

	if (qbp) {
		for (tmp = bp; tmp; tmp = tmp->b_cont)
			qbp->qb_count += (tmp->b_wptr - tmp->b_rptr);
		if (qbp->qb_count >= qbp->qb_hiwat)
			qbp->qb_flag |= QB_FULL;
	} else {
		for (tmp = bp; tmp; tmp = tmp->b_cont)
			q->q_count += (tmp->b_wptr - tmp->b_rptr);
		if (q->q_count >= q->q_hiwat)
			q->q_flag |= QFULL;
	}
	if ((mcls > QNORM) ||
	    (canenable(q) && (q->q_flag & QWANTR || bp->b_band)))
		qenable_locked(q);
	ASSERT(MUTEX_HELD(QLOCK(q)));
	if (freezer != curthread)
		mutex_exit(QLOCK(q));
	return (1);
}

/*
 * Put stuff back at beginning of Q according to priority order.
 * See comment on putq above for details.
 */
int
putbq(queue_t *q, mblk_t *bp)
{
	register mblk_t *tmp;
	register qband_t *qbp = NULL;
	int mcls = (int)queclass(bp);
	kthread_id_t freezer;

	ASSERT(q && bp);
	ASSERT(bp->b_next == NULL);
	freezer = STREAM(q)->sd_freezer;
	if (freezer == curthread) {
		ASSERT(frozenstr(q));
		ASSERT(MUTEX_HELD(QLOCK(q)));
	} else
		mutex_enter(QLOCK(q));

	/*
	 * Make sanity checks and if qband structure is not yet
	 * allocated, do so.
	 */
	if (mcls == QPCTL) {
		if (bp->b_band != 0)
			bp->b_band = 0;		/* force to be correct */
	} else if (bp->b_band != 0) {
		register int i;
		qband_t **qbpp;

		if (bp->b_band > q->q_nband) {
			qbpp = &q->q_bandp;
			while (*qbpp)
				qbpp = &(*qbpp)->qb_next;
			while (bp->b_band > q->q_nband) {
				if ((*qbpp = allocband()) == NULL) {
					if (freezer != curthread)
						mutex_exit(QLOCK(q));
					return (0);
				}
				(*qbpp)->qb_hiwat = q->q_hiwat;
				(*qbpp)->qb_lowat = q->q_lowat;
				q->q_nband++;
				qbpp = &(*qbpp)->qb_next;
			}
		}
		qbp = q->q_bandp;
		i = bp->b_band;
		while (--i)
			qbp = qbp->qb_next;
	}

	/*
	 * If queue is empty or if message is high priority,
	 * place on the front of the queue.
	 */
	tmp = q->q_first;
	if ((!tmp) || (mcls == QPCTL)) {
		bp->b_next = tmp;
		if (tmp)
			tmp->b_prev = bp;
		else
			q->q_last = bp;
		q->q_first = bp;
		bp->b_prev = NULL;
		if (qbp) {
			qbp->qb_first = bp;
			qbp->qb_last = bp;
		}
	} else if (qbp) {	/* bp->b_band != 0 */
		tmp = qbp->qb_first;
		if (tmp) {

			/*
			 * Insert bp before the first message in this band.
			 */
			bp->b_next = tmp;
			bp->b_prev = tmp->b_prev;
			if (tmp->b_prev)
				tmp->b_prev->b_next = bp;
			else
				q->q_first = bp;
			tmp->b_prev = bp;
		} else {
			tmp = q->q_last;
			if ((mcls < (int)queclass(tmp)) ||
			    (bp->b_band < tmp->b_band)) {

				/*
				 * Tack bp on end of queue.
				 */
				bp->b_next = NULL;
				bp->b_prev = tmp;
				tmp->b_next = bp;
				q->q_last = bp;
			} else {
				tmp = q->q_first;
				while (tmp->b_datap->db_type >= QPCTL)
					tmp = tmp->b_next;
				while (tmp->b_band > bp->b_band)
					tmp = tmp->b_next;
				ASSERT(tmp != NULL);

				/*
				 * Insert bp before tmp.
				 */
				bp->b_next = tmp;
				bp->b_prev = tmp->b_prev;
				if (tmp->b_prev)
					tmp->b_prev->b_next = bp;
				else
					q->q_first = bp;
				tmp->b_prev = bp;
			}
			qbp->qb_last = bp;
		}
		qbp->qb_first = bp;
	} else {		/* bp->b_band == 0 && !QPCTL */

		/*
		 * If the queue class or band is less than that of the last
		 * message on the queue, tack bp on the end of the queue.
		 */
		tmp = q->q_last;
		if ((mcls < (int)queclass(tmp)) || (bp->b_band < tmp->b_band)) {
			bp->b_next = NULL;
			bp->b_prev = tmp;
			tmp->b_next = bp;
			q->q_last = bp;
		} else {
			tmp = q->q_first;
			while (tmp->b_datap->db_type >= QPCTL)
				tmp = tmp->b_next;
			while (tmp->b_band > bp->b_band)
				tmp = tmp->b_next;
			ASSERT(tmp != NULL);

			/*
			 * Insert bp before tmp.
			 */
			bp->b_next = tmp;
			bp->b_prev = tmp->b_prev;
			if (tmp->b_prev)
				tmp->b_prev->b_next = bp;
			else
				q->q_first = bp;
			tmp->b_prev = bp;
		}
	}

	if (qbp) {
		for (tmp = bp; tmp; tmp = tmp->b_cont)
			qbp->qb_count += (tmp->b_wptr - tmp->b_rptr);
		if (qbp->qb_count >= qbp->qb_hiwat)
			qbp->qb_flag |= QB_FULL;
	} else {
		for (tmp = bp; tmp; tmp = tmp->b_cont)
			q->q_count += (tmp->b_wptr - tmp->b_rptr);
		if (q->q_count >= q->q_hiwat)
			q->q_flag |= QFULL;
	}
	if ((mcls > QNORM) || (canenable(q) && (q->q_flag & QWANTR)))
		qenable_locked(q);
	ASSERT(MUTEX_HELD(QLOCK(q)));
	if (freezer != curthread)
		mutex_exit(QLOCK(q));
	return (1);
}

/*
 * Insert a message before an existing message on the queue.  If the
 * existing message is NULL, the new messages is placed on the end of
 * the queue.  The queue class of the new message is ignored.  However,
 * the priority band of the new message must adhere to the following
 * ordering:
 *
 *	emp->b_prev->b_band >= mp->b_band >= emp->b_band.
 *
 * All flow control parameters are updated.
 *
 * insq can be called with the stream frozen, but other utility functions
 * holding QLOCK, and by streams modules without any locks/frozen.
 */
int
insq(queue_t *q, mblk_t *emp, mblk_t *mp)
{
	register mblk_t *tmp;
	register qband_t *qbp = NULL;
	int mcls = (int)queclass(mp);
	kthread_id_t freezer;

	freezer = STREAM(q)->sd_freezer;
	if (freezer == curthread) {
		ASSERT(frozenstr(q));
		ASSERT(MUTEX_HELD(QLOCK(q)));
	} else if (MUTEX_HELD(QLOCK(q))) {
		/* Don't drop lock on exit */
		freezer = curthread;
	} else
		mutex_enter(QLOCK(q));

	if (mcls == QPCTL) {
		if (mp->b_band != 0)
			mp->b_band = 0;		/* force to be correct */
		if (emp && emp->b_prev &&
		    (emp->b_prev->b_datap->db_type < QPCTL))
			goto badord;
	}
	if (emp) {
		if (((mcls == QNORM) && (mp->b_band < emp->b_band)) ||
		    (emp->b_prev && (emp->b_prev->b_datap->db_type < QPCTL) &&
		    (emp->b_prev->b_band < mp->b_band))) {
			goto badord;
		}
	} else {
		tmp = q->q_last;
		if (tmp && (mcls == QNORM) && (mp->b_band > tmp->b_band)) {
badord:
			cmn_err(CE_WARN,
			    "insq: attempt to insert message out of order "
			    "on q %x\n", (int)q);
			if (freezer != curthread)
				mutex_exit(QLOCK(q));
			return (0);
		}
	}

	if (mp->b_band != 0) {
		register int i;
		qband_t **qbpp;

		if (mp->b_band > q->q_nband) {
			qbpp = &q->q_bandp;
			while (*qbpp)
				qbpp = &(*qbpp)->qb_next;
			while (mp->b_band > q->q_nband) {
				if ((*qbpp = allocband()) == NULL) {
					if (freezer != curthread)
						mutex_exit(QLOCK(q));
					return (0);
				}
				(*qbpp)->qb_hiwat = q->q_hiwat;
				(*qbpp)->qb_lowat = q->q_lowat;
				q->q_nband++;
				qbpp = &(*qbpp)->qb_next;
			}
		}
		qbp = q->q_bandp;
		i = mp->b_band;
		while (--i)
			qbp = qbp->qb_next;
	}

	if ((mp->b_next = emp) != NULL) {
		if ((mp->b_prev = emp->b_prev) != NULL)
			emp->b_prev->b_next = mp;
		else
			q->q_first = mp;
		emp->b_prev = mp;
	} else {
		if ((mp->b_prev = q->q_last) != NULL)
			q->q_last->b_next = mp;
		else
			q->q_first = mp;
		q->q_last = mp;
	}

	if (qbp) {	/* adjust qband pointers and count */
		if (!qbp->qb_first) {
			qbp->qb_first = mp;
			qbp->qb_last = mp;
		} else {
			if (qbp->qb_first == emp)
				qbp->qb_first = mp;
			else if (mp->b_next && (mp->b_next->b_band !=
			    mp->b_band))
				qbp->qb_last = mp;
		}
		for (tmp = mp; tmp; tmp = tmp->b_cont)
			qbp->qb_count += (tmp->b_wptr - tmp->b_rptr);
		if (qbp->qb_count >= qbp->qb_hiwat)
			qbp->qb_flag |= QB_FULL;
	} else {
		for (tmp = mp; tmp; tmp = tmp->b_cont)
			q->q_count += (tmp->b_wptr - tmp->b_rptr);
		if (q->q_count >= q->q_hiwat)
			q->q_flag |= QFULL;
	}
	if (canenable(q) && (q->q_flag & QWANTR))
		qenable_locked(q);

	ASSERT(MUTEX_HELD(QLOCK(q)));
	if (freezer != curthread)
		mutex_exit(QLOCK(q));
	return (1);
}

/*
 * Create and put a control message on queue.
 */
int
putctl(queue_t *q, int type)
{
	register mblk_t *bp;
	int unsafe = UNSAFE_DRIVER_LOCK_HELD();

	if ((datamsg(type) && (type != M_DELAY)) ||
	    (bp = allocb_tryhard(0, BPRI_HI)) == NULL)
		return (0);
	bp->b_datap->db_type = (unsigned char) type;

	if (unsafe)
		mutex_exit(&unsafe_driver);
	put(q, bp);
	if (unsafe)
		mutex_enter(&unsafe_driver);

	return (1);
}

/*
 * Control message with a single-byte parameter
 */
int
putctl1(queue_t *q, int type, int param)
{
	register mblk_t *bp;
	int unsafe = UNSAFE_DRIVER_LOCK_HELD();

	if ((datamsg(type) && (type != M_DELAY)) ||
	    (bp = allocb_tryhard(1, BPRI_HI)) == NULL)
		return (0);
	bp->b_datap->db_type = (unsigned char)type;
	*bp->b_wptr++ = (unsigned char)param;

	if (unsafe)
		mutex_exit(&unsafe_driver);
	put(q, bp);
	if (unsafe)
		mutex_enter(&unsafe_driver);

	return (1);
}

/*
 * Return the queue upstream from this one
 */
queue_t *
backq(queue_t *q)
{
	ASSERT(q);
	q = OTHERQ(q);
	if (q->q_next) {
		q = q->q_next;
		return (OTHERQ(q));
	}
	return (NULL);
}



/*
 * Send a block back up the queue in reverse from this
 * one (e.g. to respond to ioctls)
 */
void
qreply(queue_t *q, mblk_t *bp)
{
	ASSERT(q && bp);

	(void) putnext(OTHERQ(q), bp);
}

/*
 * Streams Queue Scheduling
 *
 * Queues are enabled through qenable() when they have messages to
 * process.  They are serviced by queuerun(), which runs each enabled
 * queue's service procedure.  The call to queuerun() is processor
 * dependent - the general principle is that it be run whenever a queue
 * is enabled but before returning to user level.  For system calls,
 * the function runqueues() is called if their action causes a queue
 * to be enabled.  For device interrupts, queuerun() should be
 * called before returning from the last level of interrupt.  Beyond
 * this, no timing assumptions should be made about queue scheduling.
 */

/*
 * Enable a queue: put it on list of those whose service procedures are
 * ready to run and set up the scheduling mechanism.
 * The broadcast is done outside the mutex -> to avoid the woken thread
 * from contending with the mutex. This is OK 'cos the queue has been
 * enqueued on the runlist and flagged safely at this point.
 */
void
qenable(queue_t *q)
{
	mutex_enter(QLOCK(q));
	qenable_locked(q);
	mutex_exit(QLOCK(q));
}
/* Used within framework when the queue is already locked */
void
qenable_locked(q)
	register queue_t *q;
{
	ASSERT(q);
	ASSERT(MUTEX_HELD(QLOCK(q)));

	if (!q->q_qinfo->qi_srvp)
		return;

	/*
	 * Do not place on run queue if already enabled.
	 */
	if (q->q_flag & (QWCLOSE|QENAB))
		return;

	mutex_enter(&service_queue);
#ifdef TRACE	/* XXX: this is gross */
	{
		extern int _get_pc(void);

		TRACE_3(TR_FAC_STREAMS_FR, TR_QENABLE,
			"qenable:enable %s(%X) from %K",
			QNAME(q), q, _get_pc());
	}
#endif

	ASSERT(!(q->q_flag&QHLIST));

	/*
	 * mark queue enabled and place on run list
	 * if it is not already being serviced.
	 */
	q->q_flag |= QENAB;
	if (q->q_flag & QINSERVICE) {
		mutex_exit(&service_queue);
		return;
	}
	ASSERT(q->q_link == NULL);
	if (!qhead) {
		ASSERT(qtail == NULL);
		qhead = q;
	} else {
		ASSERT(qtail);
		qtail->q_link = q;
	}
	qtail = q;
#ifdef TRACE
	enqueued++;
#endif /* TRACE */
	q->q_link = NULL;

	/*
	 * set up scheduling mechanism
	 */
	setqsched();

	mutex_exit(&service_queue);
	if (run_queues == 0)
		cv_signal(&services_to_run);
}

/*
 * Return number of messages on queue
 */
int
qsize(queue_t *qp)
{
	register count = 0;
	register mblk_t *mp;

	ASSERT(qp);

	mutex_enter(QLOCK(qp));
	for (mp = qp->q_first; mp; mp = mp->b_next)
		count++;
	mutex_exit(QLOCK(qp));
	return (count);
}

/*
 * noenable - set queue so that putq() will not enable it.
 * enableok - set queue so that putq() can enable it.
 */
void
noenable(queue_t *q)
{
	mutex_enter(QLOCK(q));
	q->q_flag |= QNOENB;
	mutex_exit(QLOCK(q));
}

void
enableok(queue_t *q)
{
	mutex_enter(QLOCK(q));
	q->q_flag &= ~QNOENB;
	mutex_exit(QLOCK(q));
}

#if 0

/*
 * Since the getmid() and getadmin() functions are not used,
 * they have been deleted rather than make them safe for
 * unloadable streams.
 * findmod() should be used instead.
 */

/*
 * Given a name, return the module id.
 * Returns 0 on error.
 */
ushort
getmid(char *name)
{
	register struct dev_ops *cdp;
	register int i = 0;
	register struct fmodsw *fmp;
	register struct module_info *mip;

	if (!name || *name == (char)0)
		return ((ushort)0);
	cdp = devopsp[i];
	for (; i < devcnt; i++, cdp = devopsp[i]) {
		if (cdp->devo_cb_ops->cb_str) {
			mip = cdp->devo_cb_ops->cb_str->st_rdinit->qi_minfo;
			if (strcmp(name, mip->mi_idname) == 0)
				return (mip->mi_idnum);
		}
	}
	for (fmp = fmodsw; fmp < &fmodsw[fmodcnt]; fmp++) {
		if (fmp->f_str != NULL && strcmp(name, fmp->f_name) == 0) {
			mip = fmp->f_str->st_rdinit->qi_minfo;
			return (mip->mi_idnum);
		}
	}
	return ((ushort)0);
}

/*
 * Given a module id, return the qadmin pointer.
 * Returns NULL on error.
 */
int
(*getadmin(u_short mid))()
{
	register struct dev_ops *cdp;
	register int i = 0;
	register struct fmodsw *fmp;
	register struct qinit *qip;

	if (mid == 0)
		return (NULL);
	cdp = devopsp[i];
	for (; i < devcnt; i++, cdp = devopsp[i]) {
		if (cdp->devo_cb_ops->cb_str) {
			qip = cdp->devo_cb_ops->cb_str->st_rdinit;
			if (mid == qip->qi_minfo->mi_idnum)
				return (qip->qi_qadmin);
		}
	}
	for (fmp = fmodsw; fmp < &fmodsw[fmodcnt]; fmp++) {
		if (fmp->f_str != NULL) {
			qip = fmp->f_str->st_rdinit;
			if (mid == qip->qi_minfo->mi_idnum)
				return (qip->qi_qadmin);
		}
	}
	return (NULL);
}
#endif 0

/*
 * Set queue fields.
 */
int
strqset(queue_t *q, qfields_t what, unsigned char pri, long val)
{
	register qband_t *qbp = NULL;
	queue_t	*wrq;
	int error = 0;
	kthread_id_t freezer;

	freezer = STREAM(q)->sd_freezer;
	if (freezer == curthread) {
		ASSERT(frozenstr(q));
		ASSERT(MUTEX_HELD(QLOCK(q)));
	} else
		mutex_enter(QLOCK(q));

	if (what >= QBAD) {
		error = EINVAL;
		goto done;
	}
	if (pri != 0) {
		register int i;
		qband_t **qbpp;

		if (pri > q->q_nband) {
			qbpp = &q->q_bandp;
			while (*qbpp)
				qbpp = &(*qbpp)->qb_next;
			while (pri > q->q_nband) {
				if ((*qbpp = allocband()) == NULL) {
					error = EAGAIN;
					goto done;
				}
				(*qbpp)->qb_hiwat = q->q_hiwat;
				(*qbpp)->qb_lowat = q->q_lowat;
				q->q_nband++;
				qbpp = &(*qbpp)->qb_next;
			}
		}
		qbp = q->q_bandp;
		i = pri;
		while (--i)
			qbp = qbp->qb_next;
	}
	switch (what) {

	case QHIWAT:
		if (qbp)
			qbp->qb_hiwat = (ulong)val;
		else
			q->q_hiwat = (ulong)val;
		break;

	case QLOWAT:
		if (qbp)
			qbp->qb_lowat = (ulong)val;
		else
			q->q_lowat = (ulong)val;
		break;

	case QMAXPSZ:
		if (qbp)
			error = EINVAL;
		else
			q->q_maxpsz = val;

		/*
		 * Performance concern, strwrite looks at the module below
		 * the stream head for the maxpsz each time it does a write
		 * we now cache it at the stream head.  Check to see if this
		 * queue is sitting directly below the stream head.
		 */
		wrq = STREAM(q)->sd_wrq;
		if (q != wrq->q_next)
			break;

		/*
		 * If the stream is not frozen drop the current QLOCK and
		 * acquire the sd_wrq QLOCK which protects sd_qn_*
		 */
		if (freezer != curthread) {
			mutex_exit(QLOCK(q));
			mutex_enter(QLOCK(wrq));
		}
		ASSERT(MUTEX_HELD(QLOCK(wrq)));

		if (strmsgsz != 0) {
			if (val == INFPSZ)
				val = strmsgsz;
			else  {
				if (STREAM(q)->sd_vnode->v_type == VFIFO)
					val = MIN(PIPE_BUF, val);
				else
					val = MIN(strmsgsz, val);
			}
		}
		STREAM(q)->sd_qn_maxpsz = val;
		if (freezer != curthread) {
			mutex_exit(QLOCK(wrq));
			mutex_enter(QLOCK(q));
		}
		break;

	case QMINPSZ:
		if (qbp)
			error = EINVAL;
		else
			q->q_minpsz = val;

		/*
		 * Performance concern, strwrite looks at the module below
		 * the stream head for the maxpsz each time it does a write
		 * we now cache it at the stream head.  Check to see if this
		 * queue is sitting directly below the stream head.
		 */
		wrq = STREAM(q)->sd_wrq;
		if (q != wrq->q_next)
			break;

		/*
		 * If the stream is not frozen drop the current QLOCK and
		 * acquire the sd_wrq QLOCK which protects sd_qn_*
		 */
		if (freezer != curthread) {
			mutex_exit(QLOCK(q));
			mutex_enter(QLOCK(wrq));
		}
		STREAM(q)->sd_qn_minpsz = val;

		if (freezer != curthread) {
			mutex_exit(QLOCK(wrq));
			mutex_enter(QLOCK(q));
		}
		break;

	case QSTRUIOT:
		if (qbp)
			error = EINVAL;
		else
			q->q_struiot = (ushort)val;
		break;

	case QCOUNT:
	case QFIRST:
	case QLAST:
	case QFLAG:
		error = EPERM;
		break;

	default:
		error = EINVAL;
		break;
	}
done:
	if (freezer != curthread)
		mutex_exit(QLOCK(q));
	return (error);
}

/*
 * Get queue fields.
 */
int
strqget(queue_t *q, qfields_t what, unsigned char pri, long *valp)
{
	register qband_t *qbp = NULL;
	int error = 0;
	kthread_id_t freezer;

	freezer = STREAM(q)->sd_freezer;
	if (freezer == curthread) {
		ASSERT(frozenstr(q));
		ASSERT(MUTEX_HELD(QLOCK(q)));
	} else
		mutex_enter(QLOCK(q));
	if (what >= QBAD) {
		error = EINVAL;
		goto done;
	}
	if (pri != 0) {
		register int i;
		qband_t **qbpp;

		if (pri > q->q_nband) {
			qbpp = &q->q_bandp;
			while (*qbpp)
				qbpp = &(*qbpp)->qb_next;
			while (pri > q->q_nband) {
				if ((*qbpp = allocband()) == NULL) {
					error = EAGAIN;
					goto done;
				}
				(*qbpp)->qb_hiwat = q->q_hiwat;
				(*qbpp)->qb_lowat = q->q_lowat;
				q->q_nband++;
				qbpp = &(*qbpp)->qb_next;
			}
		}
		qbp = q->q_bandp;
		i = pri;
		while (--i)
			qbp = qbp->qb_next;
	}
	switch (what) {
	case QHIWAT:
		if (qbp)
			*(ulong *)valp = qbp->qb_hiwat;
		else
			*(ulong *)valp = q->q_hiwat;
		break;

	case QLOWAT:
		if (qbp)
			*(ulong *)valp = qbp->qb_lowat;
		else
			*(ulong *)valp = q->q_lowat;
		break;

	case QMAXPSZ:
		if (qbp)
			error = EINVAL;
		else
			*(long *)valp = q->q_maxpsz;
		break;

	case QMINPSZ:
		if (qbp)
			error = EINVAL;
		else
			*(long *)valp = q->q_minpsz;
		break;

	case QCOUNT:
		if (qbp)
			*(ulong *)valp = qbp->qb_count;
		else
			*(ulong *)valp = q->q_count;
		break;

	case QFIRST:
		if (qbp)
			*(mblk_t **)valp = qbp->qb_first;
		else
			*(mblk_t **)valp = q->q_first;
		break;

	case QLAST:
		if (qbp)
			*(mblk_t **)valp = qbp->qb_last;
		else
			*(mblk_t **)valp = q->q_last;
		break;

	case QFLAG:
		if (qbp)
			*(ulong *)valp = qbp->qb_flag;
		else
			*(ulong *)valp = q->q_flag;
		break;

	case QSTRUIOT:
		if (qbp)
			error = EINVAL;
		else
			*(ushort *)valp = q->q_struiot;
		break;

	default:
		error = EINVAL;
		break;
	}
done:
	if (freezer != curthread)
		mutex_exit(QLOCK(q));
	return (error);
}

/*
 * Function awakes all in cvwait/sigwait/pollwait, on one of:
 *	QWANTWSYNC or QWANTR or QWANTW,
 *
 * Note: for QWANTWSYNC/QWANTW and QWANTR, if no WSLEEPer or RSLEEPer then a
 *	 deferred wakeup will be done. Also if strpoll() in progress then a
 *	 deferred pollwakeup will be done.
 */
int
strwakeq(queue_t *q, int flag)
{
	register stdata_t *stp = STREAM(q);
	pollhead_t *pl;

	mutex_enter(&stp->sd_lock);
	pl = &stp->sd_pollist;
	if (flag & QWANTWSYNC) {
		if (stp->sd_flag & WSLEEP) {
			stp->sd_flag &= ~WSLEEP;
			cv_broadcast(&stp->sd_wrq->q_wait);
		} else
			stp->sd_wakeq |= WSLEEP;
		if (stp->sd_sigflags & S_WRNORM)
			strsendsig(stp->sd_siglist, S_WRNORM, 0L);
		mutex_exit(&stp->sd_lock);
		pollwakeup_safe(pl, POLLWRNORM);
	} else if (flag & QWANTR) {
		if (stp->sd_flag & RSLEEP) {
			stp->sd_flag &= ~RSLEEP;
			cv_broadcast(&RD(stp->sd_wrq)->q_wait);
		} else
			stp->sd_wakeq |= RSLEEP;
		if (stp->sd_sigflags) {
			if (stp->sd_sigflags & S_INPUT)
				strsendsig(stp->sd_siglist, S_INPUT, 0L);
			if (stp->sd_sigflags & S_RDNORM)
				strsendsig(stp->sd_siglist, S_RDNORM, 0L);
		}
		mutex_exit(&stp->sd_lock);
		pollwakeup_safe(pl, POLLIN | POLLRDNORM);
	} else {
		if (stp->sd_flag & WSLEEP) {
			stp->sd_flag &= ~WSLEEP;
			cv_broadcast(&stp->sd_wrq->q_wait);
		}
		if (stp->sd_sigflags & S_WRNORM)
			strsendsig(stp->sd_siglist, S_WRNORM, 0L);
		mutex_exit(&stp->sd_lock);
		pollwakeup_safe(pl, POLLWRNORM);
	}
	return (0);
}

int
struioput(queue_t *q, mblk_t *mp, struiod_t *dp)
{
	stdata_t *stp = STREAM(q);
	int typ = stp->sd_struiordq->q_struiot;
	uio_t *uiop = &dp->d_uio;
	int uiocnt;
	int cnt;
	unsigned char *ptr;
	int error = 0;

	for (; uiop->uio_resid > 0 && mp; mp = mp->b_cont) {
		dblk_t *dbp = mp->b_datap;

		if (! (dbp->db_struioflag & STRUIO_SPEC) ||
		    (dbp->db_struioflag & STRUIO_DONE))
			continue;
		ptr = dbp->db_struioptr;
		uiocnt = dbp->db_struiolim - ptr;
		cnt = MIN(uiocnt, uiop->uio_resid);
		switch (typ) {
		case STRUIOT_STANDARD:
			if (error = uiomove((caddr_t)ptr, cnt, UIO_READ, uiop))
				goto out;
			break;

		case STRUIOT_IP:
			if (error = uioipcopyout((caddr_t)ptr, cnt, uiop,
			    (ushort *)dbp->db_struioun.data))
				goto out;
			break;

		default:
			error = EIO;
			goto out;
		}
		dbp->db_struioptr += cnt;
		dbp->db_struioflag |= STRUIO_DONE;
	}
out:;
	return (error);
}

int
struioget(queue_t *q, mblk_t *mp, struiod_t *dp)
{
	stdata_t *stp = STREAM(q);
	int typ = stp->sd_struiowrq->q_struiot;
	uio_t *uiop = &dp->d_uio;
	int uiocnt;
	int cnt;
	unsigned char *ptr;
	int error = 0;

	for (; uiop->uio_resid > 0 && mp; mp = mp->b_cont) {
		dblk_t *dbp = mp->b_datap;

		if (! (dbp->db_struioflag & STRUIO_SPEC) ||
		    (dbp->db_struioflag & STRUIO_DONE))
			continue;
		ptr = dbp->db_struioptr;
		uiocnt = dbp->db_struiolim - ptr;
		cnt = MIN(uiocnt, uiop->uio_resid);
		switch (typ) {
		case STRUIOT_STANDARD:
			if (error = uiomove((caddr_t)ptr, cnt, UIO_WRITE, uiop))
				goto out;
			break;

		case STRUIOT_IP:
			if (error = uioipcopyin((caddr_t)ptr, cnt, uiop,
			    (ushort *)dbp->db_struioun.data))
				goto out;
			dbp->db_struioflag |= STRUIO_IP;
			break;

		default:
			error = EIO;
			goto out;
		}
		dbp->db_struioflag |= STRUIO_DONE;
		dbp->db_struioptr += cnt;
	}
out:
	return (error);
}

/*
 * The purpose of rwnext() is to call the rw procedure of the
 * next (downstream) modules queue. Blocking isn't allowed in
 * the recursive rwnext() path, instead EBUSY will be returned.
 * If the queue is flow-controlled (should only happen on the
 * write side) then the queue will be marked and EWOULDBLOCK
 * will be returned. If the module doesn't support synchronous
 * qinit entries or if the queue doesn't have a r/w procedure
 * then EINVAL will be returned.
 */
int
rwnext(queue_t *qp, struiod_t *dp)
{
	queue_t		*nqp;
	syncq_t		*sq;
	u_long		count;
	u_long		flags;
	struct qinit	*qi;
	int		(*proc)();
	struct stdata	*stp;
	int		isread;
	int		rval;

	stp = STREAM(qp);
	ASSERT(stp != NULL);
	flags = qp->q_flag;
	if (flags & QUNSAFE) {
		/* Coming from an unsafe module */
		cmn_err(CE_PANIC, "rwnext: QUNSAFE");
	}
	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	/*
	 * Prevent q_next from changing by holding sd_lock until acquiring
	 * SQLOCK. Note that a read-side rwnext from the streamhead will
	 * already have sd_lock acquired. In either case sd_lock is always
	 * released after acquiring SQLOCK.
	 *
	 * The streamhead read-side holding sd_lock when calling rwnext is
	 * required to prevent a race condition were M_DATA mblks flowing
	 * up the read-side of the stream could be bypassed by a rwnext()
	 * down-call. In this case sd_lock acts as the streamhead perimeter.
	 */
	if ((nqp = WR(qp)) == qp) {
		isread = 0;
		mutex_enter(&stp->sd_lock);
		qp = nqp->q_next;
	} else {
		isread = 1;
		if (nqp != stp->sd_wrq)
			/* Not streamhead */
			mutex_enter(&stp->sd_lock);
		qp = RD(nqp->q_next);
	}
	qi = qp->q_qinfo;
	if (qp->q_struiot == STRUIOT_NONE || ! (proc = qi->qi_rwp)) {
		/*
		 * Not a synchronous module or no r/w procedure for this
		 * queue, so just return EINVAL and let the caller handle it.
		 */
		mutex_exit(&stp->sd_lock);
		return (EINVAL);
	}
	sq = qp->q_syncq;
	ASSERT(sq != NULL);
	mutex_enter(SQLOCK(sq));
	mutex_exit(&stp->sd_lock);
	count = sq->sq_count;
	flags = sq->sq_flags;
	if (flags & SQ_GOAWAY) {
		/*
		 * Can't enter the perimeter, just return.
		 */
		rval = EBUSY;
		goto out2;
	} else if (flags & SQ_UNSAFE) {
		/*
		 * Unsafe synchronous modules not supported, so
		 * just return EINVAL and let the caller handle it.
		 */
		rval = EINVAL;
		goto out2;
	} else if (! (flags & SQ_CIPUT))
		sq->sq_flags = flags | SQ_EXCL;
	sq->sq_count = count + 1;
	ASSERT(sq->sq_count != 0);		/* Wraparound */
	/*
	 * Note: The only message ordering guarantee that rwnext() makes is
	 *	 for the write queue flow-control case. All others (r/w queue
	 *	 with q_count > 0 (or q_first != 0)) are the resposibilty of
	 *	 the queue's rw procedure. This could be genralized here buy
	 *	 running the queue's service procedure, but that wouldn't be
	 *	 the most efficent for all cases.
	 */
	mutex_exit(SQLOCK(sq));
	if (! isread && (qp->q_flag & QFULL)) {
		/*
		 * Write queue may be flow controlled. If so,
		 * mark the queue for wakeup when it's not.
		 */
		mutex_enter(QLOCK(qp));
		if (qp->q_flag & QFULL) {
			qp->q_flag |= QWANTWSYNC;
			mutex_exit(QLOCK(qp));
			rval = EWOULDBLOCK;
			goto out;
		}
		mutex_exit(QLOCK(qp));
	}

	rval = (*proc)(qp, dp);
out:
	mutex_enter(SQLOCK(sq));
out1:
	flags = sq->sq_flags;
	count = sq->sq_count;
	if (flags & (SQ_QUEUED|SQ_WANTWAKEUP|SQ_WANTEXWAKEUP)) {
		putnext_tail(sq, 0, flags, count);
		return (rval);
	}
	ASSERT(count != 0);
	sq->sq_count = count - 1;
	ASSERT(flags & (SQ_EXCL|SQ_CIPUT));
	/*
	 * Safe to always drop SQ_EXCL:
	 *	Not SQ_CIPUT means we set SQ_EXCL above
	 *	For SQ_CIPUT SQ_EXCL will only be set if the put procedure
	 *	did a qwriter(INNER) in which case nobody else
	 *	is in the inner perimeter and we are exiting.
	 */
#ifdef DEBUG
	if ((flags & (SQ_EXCL|SQ_CIPUT)) == (SQ_EXCL|SQ_CIPUT)) {
		ASSERT(sq->sq_count == 0);
	}
#endif DEBUG
	sq->sq_flags = flags & ~SQ_EXCL;
out2:
	mutex_exit(SQLOCK(sq));
	return (rval);
}

/*
 * The purpose of pollnext() is to call the poll procedure of the
 * next (downstream) modules queue. Blocking isn't allowed in the
 * recursive pollnext() path, so any pollnext() (or poll routine
 * called) that would block returns EWOULDBLOCK, else returns NULL.
 */

int
infonext(queue_t *qp, infod_t *idp)
{
	queue_t		*nqp;
	syncq_t		*sq;
	u_long		count;
	u_long		flags;
	struct qinit	*qi;
	int		(*proc)();
	struct stdata	*stp;
	int		rval;

	stp = STREAM(qp);
	ASSERT(stp != NULL);
	flags = qp->q_flag;
	if (flags & QUNSAFE) {
		/* Coming from an unsafe module */
		cmn_err(CE_PANIC, "pollnext: QUNSAFE");
	}
	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	/*
	 * Prevent q_next from changing by holding sd_lock until
	 * acquiring SQLOCK.
	 */
	mutex_enter(&stp->sd_lock);
	if ((nqp = WR(qp)) == qp) {
		qp = nqp->q_next;
	} else {
		qp = RD(nqp->q_next);
	}
	qi = qp->q_qinfo;
	if (qp->q_struiot == STRUIOT_NONE || ! (proc = qi->qi_infop)) {
		mutex_exit(&stp->sd_lock);
		return (EINVAL);
	}
	sq = qp->q_syncq;
	ASSERT(sq != NULL);
	mutex_enter(SQLOCK(sq));
	mutex_exit(&stp->sd_lock);
	count = sq->sq_count;
	flags = sq->sq_flags;
	if (flags & (SQ_GOAWAY|SQ_UNSAFE)) {
		mutex_exit(SQLOCK(sq));
		return (EBUSY);
	} else if (! (flags & SQ_CIPUT))
		sq->sq_flags = flags | SQ_EXCL;
	else
		ASSERT(flags & SQ_CIPUT);
	sq->sq_count = count + 1;
	ASSERT(sq->sq_count != 0);		/* Wraparound */
	mutex_exit(SQLOCK(sq));

	rval = (*proc)(qp, idp);

	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	count = sq->sq_count;
	if (flags & (SQ_QUEUED|SQ_WANTWAKEUP|SQ_WANTEXWAKEUP)) {
		putnext_tail(sq, 0, flags, count);
		return (rval);
	}
	ASSERT(count != 0);
	sq->sq_count = count - 1;
	ASSERT(flags & (SQ_EXCL|SQ_CIPUT));
	/*
	 * Safe to always drop SQ_EXCL:
	 *	Not SQ_CIPUT means we set SQ_EXCL above
	 *	For SQ_CIPUT SQ_EXCL will only be set if the put procedure
	 *	did a qwriter(INNER) in which case nobody else
	 *	is in the inner perimeter and we are exiting.
	 */
#ifdef DEBUG
	if ((flags & (SQ_EXCL|SQ_CIPUT)) == (SQ_EXCL|SQ_CIPUT)) {
		ASSERT(sq->sq_count == 0);
	}
#endif DEBUG
	sq->sq_flags = flags & ~SQ_EXCL;
	mutex_exit(SQLOCK(sq));
	return (rval);
}

/*
 * Return nonzero if the queue is responsible for struio(), else return 0.
 */
int
isuioq(queue_t *q)
{
	if (q->q_flag & QREADR)
		return (STREAM(q)->sd_struiordq == q);
	else
		return (STREAM(q)->sd_struiowrq == q);
}
