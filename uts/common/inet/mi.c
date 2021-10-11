/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mi.c	1.27	95/10/24 SMI"

#define USE_STDARG

#include <sys/types.h>
#include <inet/common.h>
#undef MAX	/* Defined in sysmacros.h */
#undef MIN
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strstat.h>
#include <sys/sysmacros.h>
#include <inet/nd.h>
#include <sys/tihdr.h>
#include <sys/tiuser.h>
#include <sys/timod.h>
#include <sys/vtrace.h>
#include <sys/kmem.h>
#include <sys/mkdev.h>
#include <sys/strlog.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>	/* For ASSERT */

#ifdef _KERNEL	/**/
#include <sys/varargs.h>	/**/
#endif		/**/

#define	ISDIGIT(ch)	((ch) >= '0'  &&  (ch) <= '9')
#define	ISUPPER(ch)	((ch) >= 'A'  &&  (ch) <= 'Z')
#define	tolower(ch)	('a' + ((ch) - 'A'))

#define	MI_COPY_IN		1							/**/
#define	MI_COPY_OUT		2							/**/
#define	MI_COPY_DIRECTION(mp)	(*(int *)&(mp)->b_cont->b_next)				/**/
#define	MI_COPY_COUNT(mp)	(*(int *)&(mp)->b_cont->b_prev)				/**/
#define	MI_COPY_CASE(dir,cnt)	(((cnt)<<2)|dir)					/**/
#define	MI_COPY_STATE(mp)	MI_COPY_CASE(MI_COPY_DIRECTION(mp),MI_COPY_COUNT(mp))	/**/
#define	MI_IS_TRANSPARENT(mp)	(mp->b_cont  &&  (mp->b_cont->b_rptr != mp->b_cont->b_wptr))

/* Internal buff call holder */
typedef struct ibc_s {
	struct ibc_s	* ibc_next;
	int	ibc_size;
	int	ibc_pri;
	queue_t	* ibc_q;
} IBC, * IBCP, ** IBCPP;

/*
 * Double linked list of type MI_O with a mi_head_t as the head.
 * Used for mi_open_comm etc.
 */
typedef struct mi_o_s {
	IBC		mi_o_ibcs[2];
	struct mi_o_s	* mi_o_next;
	struct mi_o_s	* mi_o_prev;
	unsigned long	mi_o_dev;
} MI_O, * MI_OP;

/*
 * List head for MI_O doubly linked list.
 * The list consist of two parts: first a list of driver instances sorted
 * by increasing minor numbers then an unsorted list of module instances
 * and detached instances.
 *
 * The insert pointer is where driver instance are inserted in the list.
 * Module and detached instances are always inserted at the tail of the list.
 *
 * The range of free minor numbers is used for O(1)  minor number assignment.
 * A scan of the complete list is performed when this range runs
 * out thereby reestablishing the largest unused range of minor numbers.
 *
 * The module_dev is used to give almost unique numbers to module instances.
 * This is only needed for mi_strlog which uses the mi_o_dev field when
 * logging messages.
 */
typedef struct mi_head_s {
	struct mi_o_s	mh_o;	/* Contains head of doubly linked list */
	int	mh_min_minor;	/* Smallest free minor number */
	int	mh_max_minor;	/* Largest free minor number */
	MI_OP	mh_insert;	/* Insertion point for alloc */
	int	mh_module_dev;	/* Wraparound number for use when MODOPEN */
} mi_head_t;

#ifdef	_KERNEL	/**/
typedef	struct stroptions * STROPTP;
typedef union T_primitives	* TPRIMP;

/* Timer block states. */
#define	TB_RUNNING	1
#define	TB_IDLE		2
/*
 * Could not stop/free before putq
 */
#define	TB_RESCHED	3	/* mtb_time_left contains tick count */
#define	TB_CANCELLED	4
#define	TB_TO_BE_FREED	5


typedef struct mtb_s {
	long	mtb_state;
	int	mtb_tid;
	queue_t	* mtb_q;
	MBLKP	mtb_mp;
	u_long	mtb_time_left;
} MTB, * MTBP;

	void	mi_bufcall(   queue_t * q, int size, int pri   );
	void	mi_copyin(   queue_t * q, MBLKP mp, char * uaddr, int len   );
	void	mi_copyout(   queue_t * q, MBLKP mp   );
	MBLKP	mi_copyout_alloc(   queue_t * q, MBLKP mp, char * uaddr, int len   );
	void	mi_copy_done(   queue_t * q, MBLKP mp, int err   );
	int	mi_copy_state(   queue_t * q, MBLKP mp, MBLKP * mpp   );
	void	mi_free(   char * ptr   );
staticf	int	mi_ibc_qenable(   long long_ibc   );
staticf	int	mi_ibc_timer(   IBCPP ibcp   );
staticf	void	mi_ibc_timer_add(   IBCP ibc   );
staticf	void	mi_ibc_timer_del(   IBCP ibc   );
	int	mi_iprintf(   char * fmt, va_list ap, pfi_t putc_func, char * cookie   );
	int	mi_mpprintf_putc(   char * cookie, int ch   );
	u8	* mi_offset_param(   mblk_t * mp, u32 offset, u32 len   );
	u8	* mi_offset_paramc(   mblk_t * mp, u32 offset, u32 len   );
	boolean_t	mi_set_sth_hiwat(   queue_t * q, int size   );
	boolean_t	mi_set_sth_maxblk(   queue_t * q, int size   );
	boolean_t	mi_set_sth_wroff(   queue_t * q, int size   );
	int	mi_sprintf_putc(   char * cookie, int ch   );
#if 0
staticf	char	* mi_strchr(   char * str, int ch   );
#endif
	long	mi_strtol(   char * str, char ** ptr, int base   );
	MBLKP	mi_timer_alloc(   uint size   );
staticf int	mi_timer_fire(   MTBP mtb   );
	void	mi_timer_free(   MBLKP mp   );
	void	mi_timer_move(   queue_t * q, mblk_t * mp   );
	void	mi_timer_start(   mblk_t * mp, long tim   );
	void	mi_timer_stop(   mblk_t * mp   );
	void	mi_timer(   queue_t * q, MBLKP mp, long tim   );
	MBLKP	mi_tpi_ack_alloc(   MBLKP mp, uint size, uint type   );
staticf	void	mi_tpi_addr_and_opt(   MBLKP mp, char * addr, int addr_length, char * opt, int opt_length   );
	MBLKP	mi_tpi_trailer_alloc(   MBLKP trailer_mp, int size, int type   );

	IDP	mi_zalloc(   uint size   );
	IDP	mi_zalloc_sleep(   uint size   );

static	IBCP	mi_g_ibc_timer_head;
static	IBCP	mi_g_ibc_timer_tail;

/* Maximum minor number to use */
static int mi_maxminor = MAXMIN;

#ifndef MPS	/**/
int
mi_adjmsg (mp, len_to_trim)
	MBLKP	mp;
	int	len_to_trim;
{
reg	int	len_we_have;
reg	MBLKP	mp1;
	int	type;

	mp1 = mp;
	len_we_have = mp1->b_wptr - mp1->b_rptr;
	if (len_to_trim >= 0) {
		if (len_we_have < len_to_trim) {
			type = mp1->b_datap->db_type;
			do {		
				mp1 = mp1->b_cont;
				if (!mp1  ||  mp1->b_datap->db_type != type)
					return 0;
				len_we_have += mp1->b_wptr - mp1->b_rptr;
			} while (len_we_have < len_to_trim);
			do {
				mp->b_rptr = mp->b_wptr;
				mp = mp->b_cont;
			} while (mp != mp1);
		}
		mp1->b_rptr = mp1->b_wptr - (len_we_have - len_to_trim);
		return 1;
	}
	len_to_trim = -len_to_trim;
	if (!mp1->b_cont) {
		if (len_we_have >= len_to_trim) {
			mp1->b_wptr -= len_to_trim;
			return 1;
		}
		return 0;
	}
	type = mp1->b_datap->db_type;
	do {
		mp1 = mp1->b_cont;
		if (mp1->b_datap->db_type != type) {
			mp = mp1;
			type = mp1->b_datap->db_type;
			len_we_have = 0;
		}
		len_we_have += mp1->b_wptr - mp1->b_rptr;
	} while (mp1->b_cont);
	if ((mp1->b_wptr - mp1->b_rptr) >= len_to_trim) {
		mp1->b_wptr -= len_to_trim;
		return 1;
	}
	if (len_we_have < len_to_trim)
		return 0;
	mp1 = mp;
	for (;;) {
		len_we_have -= (mp1->b_wptr - mp1->b_rptr);
		if (len_we_have <= len_to_trim)
			break;
		mp1 = mp1->b_cont;
	}
	mp1->b_wptr -= (len_to_trim - len_we_have);
	while ((mp1 = mp1->b_cont) != NULL)
		mp1->b_wptr = mp1->b_rptr;
	return 1;
}
#endif	/**/


#ifndef NATIVE_ALLOC		/**/
/* ARGSUSED1 */
caddr_t
mi_alloc (size, pri)
	uint	size;
	int	pri;
{
reg	MBLKP	mp;

	if ((mp = allocb(size + sizeof(MBLKP), pri)) != NULL) {
		((MBLKP *)mp->b_rptr)[0] = mp;
		mp->b_rptr += sizeof(MBLKP);
		mp->b_wptr = mp->b_rptr + size;
		return (caddr_t)mp->b_rptr;
	}
	return nil(caddr_t);
}
#endif				/**/

#ifdef NATIVE_ALLOC_KMEM		/**/
/* ARGSUSED1 */
caddr_t
mi_alloc (size, pri)
	uint	size;
	int	pri;
{
	caddr_t	ptr;

	size += sizeof(int);
	if (ptr = kmem_alloc(size, KM_NOSLEEP)) {
		*(int *)ALIGN32(ptr) = size;
		ptr += sizeof(int);
		return ptr;
	}
	return nil(caddr_t);
}

/* ARGSUSED1 */
caddr_t
mi_alloc_sleep (size, pri)
	uint	size;
	int	pri;
{
	caddr_t	ptr;

	size += sizeof(int);
	ptr = kmem_alloc(size, KM_SLEEP);
	*(int *)ALIGN32(ptr) = size;
	ptr += sizeof(int);
	return ptr;
}
#endif				/**/

#ifdef SVR3_STYLE
queue_t *
mi_allocq (st)
reg	struct streamtab	* st;
{
reg	queue_t	* q;
extern	queue_t	* allocq(   void   );

	if (!st)
		return nilp(queue_t);
	if (q = allocq()) {
		q->q_qinfo = st->st_rdinit;
		q->q_minpsz = st->st_rdinit->qi_minfo->mi_minpsz;
		q->q_maxpsz = st->st_rdinit->qi_minfo->mi_maxpsz;
		q->q_hiwat = st->st_rdinit->qi_minfo->mi_hiwat;
		q->q_lowat = st->st_rdinit->qi_minfo->mi_lowat;
		WR(q)->q_qinfo = st->st_wrinit;
		WR(q)->q_minpsz = st->st_wrinit->qi_minfo->mi_minpsz;
		WR(q)->q_maxpsz = st->st_wrinit->qi_minfo->mi_maxpsz;
		WR(q)->q_hiwat = st->st_wrinit->qi_minfo->mi_hiwat;
		WR(q)->q_lowat = st->st_wrinit->qi_minfo->mi_lowat;
	}
	return q;
}
#endif /* SVR3_STYLE */

void
mi_bufcall (q, size, pri)
	queue_t	* q;
	int	size;
	int	pri;
{
reg	MI_OP	mi_o;
reg	IBCP	ibc;

	/* Encode which ibc is used in sign of ibc_size field */
	size++;
	if (!q  ||  !(mi_o = (MI_OP)q->q_ptr) ||  size <= 0)
		return;
	--mi_o;
	if (mi_o->mi_o_ibcs[0].ibc_q == q
	||  mi_o->mi_o_ibcs[1].ibc_q == q)
		return;
	if (!mi_o->mi_o_ibcs[0].ibc_q) {
		ibc = &mi_o->mi_o_ibcs[0];
		ibc->ibc_size = size;
	} else if (!mi_o->mi_o_ibcs[1].ibc_q) {
		ibc = &mi_o->mi_o_ibcs[1];
		ibc->ibc_size = -size;
	} else
		return;
	ibc->ibc_pri = pri;
	ibc->ibc_q = q;
	if (!bufcall(size - 1, pri, (pfv_t)mi_ibc_qenable, (long)ibc))
		mi_ibc_timer_add(ibc);
}

int mi_rescan_debug = 0;

int
mi_close_comm (mi_headp, q)
	void		** mi_headp;
	queue_t		* q;
{
	mi_head_t	* mi_head = *(mi_head_t **)mi_headp;
	MI_OP		mi_o;

	mi_o = (MI_OP)q->q_ptr;
	if (!mi_o)
		return (0);
	mi_o--;

	/* If we are the insertion point move it to the next guy */
	if (mi_head->mh_insert == mi_o) {
		if (mi_rescan_debug)
			printf("mi_close_com: moving insert\n");
		mi_head->mh_insert = mi_o->mi_o_next;
	}
	/*
	 * If we are either edge of the current range update the current
	 * range.
	 */
	if (mi_o->mi_o_dev < MAXMIN) {
		if (mi_o->mi_o_dev == mi_head->mh_min_minor - 1) {
			if (mi_o->mi_o_prev == &mi_head->mh_o) {
				/* First one */
				mi_head->mh_min_minor = 0;
			} else {
				mi_head->mh_min_minor =
					mi_o->mi_o_prev->mi_o_dev + 1;
			}
			if (mi_rescan_debug)
				printf("mi_close_comm: removed min %d, new %d\n",
				       (int)mi_o->mi_o_dev,
				       mi_head->mh_min_minor);
		}
		if (mi_o->mi_o_dev == mi_head->mh_max_minor) {
			if (mi_o->mi_o_next == &mi_head->mh_o) {
				/* Last one */
				mi_head->mh_max_minor = mi_maxminor;
			} else {
				mi_head->mh_max_minor =
					mi_o->mi_o_next->mi_o_dev;
			}
			if (mi_rescan_debug)
				printf("mi_close_comm: removed minor %d, new %d\n",
				       (int)mi_o->mi_o_dev,
				       mi_head->mh_max_minor);
		}
	}
	/* Unlink from list */
	mi_o->mi_o_next->mi_o_prev = mi_o->mi_o_prev;
	mi_o->mi_o_prev->mi_o_next = mi_o->mi_o_next;
	mi_o->mi_o_next = mi_o->mi_o_prev = NULL;

	q->q_ptr = nil(IDP);
	OTHERQ(q)->q_ptr = nil(IDP);
	mi_o->mi_o_dev = (unsigned long)OPENFAIL;

	mi_ibc_timer_del(&mi_o->mi_o_ibcs[0]);
	mi_ibc_timer_del(&mi_o->mi_o_ibcs[1]);
	if (!mi_o->mi_o_ibcs[0].ibc_q
	&&  !mi_o->mi_o_ibcs[1].ibc_q)
		mi_free((IDP)mi_o);

	/* If list now empty free the list head */
	if (mi_head->mh_o.mi_o_next == &mi_head->mh_o) {
		ASSERT(mi_head->mh_o.mi_o_prev == &mi_head->mh_o);
		*mi_headp = nilp(void *);
		mi_free((IDP)mi_head);
	}
	return 0;
}

int
mi_close_detached (mi_headp, ptr)
	void		** mi_headp;
	IDP		ptr;
{
	mi_head_t	* mi_head = *(mi_head_t **)mi_headp;
	MI_OP	mi_o;

	mi_o = (MI_OP)ALIGN32(ptr);
	if (!mi_o)
		return (0);
	mi_o--;

	/* If we are the insertion point move it to the next guy */
	if (mi_head->mh_insert == mi_o) {
		if (mi_rescan_debug)
			printf("mi_close_detached: moving insert\n");
		mi_head->mh_insert = mi_o->mi_o_next;
	}
	/* mi_detach always make the device be OPENFAIL */
	ASSERT(mi_o->mi_o_dev == OPENFAIL);

	/* Unlink from list */
	mi_o->mi_o_next->mi_o_prev = mi_o->mi_o_prev;
	mi_o->mi_o_prev->mi_o_next = mi_o->mi_o_next;
	mi_o->mi_o_next = mi_o->mi_o_prev = NULL;

	mi_ibc_timer_del(&mi_o->mi_o_ibcs[0]);
	mi_ibc_timer_del(&mi_o->mi_o_ibcs[1]);
	if (!mi_o->mi_o_ibcs[0].ibc_q
	&&  !mi_o->mi_o_ibcs[1].ibc_q)
		mi_free((IDP)mi_o);

	/* If list now empty free the list head */
	if (mi_head->mh_o.mi_o_next == &mi_head->mh_o) {
		ASSERT(mi_head->mh_o.mi_o_prev == &mi_head->mh_o);
		*mi_headp = nilp(void *);
		mi_free((IDP)mi_head);
	}
	return (0);
}

void
mi_copyin (q, mp, uaddr, len)
	queue_t	* q;
	MBLKP	mp;
	char	* uaddr;
	int	len;
{
	struct iocblk * iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
	struct copyreq * cq;
	int	err;
	MBLKP	mp1;
	
	if ( mp->b_datap->db_type == M_IOCTL ) {
		if (iocp->ioc_count != TRANSPARENT) {
			mp1 = mp->b_cont;
			if (!uaddr  &&  (!mp1  ||  msgdsize(mp1) < len)) {
				err = EINVAL;
				goto err_ret;
			}
			mp1 = allocb(0, BPRI_MED);
			if (!mp1
			||  (mp->b_cont->b_cont  &&  !pullupmsg(mp->b_cont, -1))) {
				err = ENOMEM;
				goto err_ret;
			}
			mp1->b_cont = mp->b_cont;
			mp->b_cont = mp1;
		}
		MI_COPY_COUNT(mp) = 0;
		mp->b_datap->db_type = M_IOCDATA;
	} else if (!uaddr) {
		err = EPROTO;
		goto err_ret;
	}
	mp1 = mp->b_cont;
	cq = (struct copyreq *)iocp;
	cq->cq_private = mp1;
	cq->cq_size = len;
	cq->cq_addr = uaddr;
	cq->cq_flag = 0;
	MI_COPY_DIRECTION(mp) = MI_COPY_IN;
	MI_COPY_COUNT(mp)++;
	if (!uaddr) {
		if ( !MI_IS_TRANSPARENT(mp) ) {
			struct copyresp * cp =
				(struct copyresp *)ALIGN32(mp->b_rptr);
			mp->b_datap->db_type = M_IOCDATA;
			mp->b_cont = mp1->b_cont;
			cp->cp_private->b_cont = nil(MBLKP);
			cp->cp_rval = 0;
			put(q, mp);
			return;
		}
		bcopy((char *)mp1->b_rptr, (char *)&cq->cq_addr, sizeof(cq->cq_addr));
	}
	mp->b_cont = nil(MBLKP);
	mp->b_datap->db_type = M_COPYIN;
	qreply(q, mp);
	return;
err_ret:
	iocp->ioc_error = err;
	iocp->ioc_count = 0;
	if ( mp->b_cont ) {
		freemsg(mp->b_cont);
		mp->b_cont = nil(MBLKP);
	}
	mp->b_datap->db_type = M_IOCACK;
	qreply(q, mp);
	return;
}

void
mi_copyout (q, mp)
	queue_t	* q;
	MBLKP	mp;
{
	struct iocblk * iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
	struct copyreq * cq = (struct copyreq *)iocp;
	struct copyresp * cp = (struct copyresp *)cq;
	MBLKP	mp1;
	MBLKP	mp2;
	
	if ( mp->b_datap->db_type != M_IOCDATA  ||  !mp->b_cont) {
		mi_copy_done(q, mp, EPROTO);
		return;
	}
	/* Check completion of previous copyout operation. */
	mp1 = mp->b_cont;
	if ( (int)cp->cp_rval  ||  !mp1->b_cont) {
		mi_copy_done(q, mp, (int)cp->cp_rval);
		return;
	}
	if (!mp1->b_cont->b_cont  &&  !MI_IS_TRANSPARENT(mp)) {
		mp1->b_next = nil(MBLKP);
		mp1->b_prev = nil(MBLKP);
		mp->b_cont = mp1->b_cont;
		freeb(mp1);
		mp1 = mp->b_cont;
		mp1->b_next = nil(MBLKP);
		mp1->b_prev = nil(MBLKP);
		iocp->ioc_count = mp1->b_wptr - mp1->b_rptr;
		iocp->ioc_error = 0;
		mp->b_datap->db_type = M_IOCACK;
		qreply(q, mp);
		return;
	}
	if (MI_COPY_DIRECTION(mp) == MI_COPY_IN) {
		/* Set up for first copyout. */
		MI_COPY_DIRECTION(mp) = MI_COPY_OUT;
		MI_COPY_COUNT(mp) = 1;
	} else {
		++MI_COPY_COUNT(mp);
	}
	cq->cq_private = mp1;
	/* Find message preceding last. */
	for ( mp2 = mp1; mp2->b_cont->b_cont; mp2 = mp2->b_cont )
		;
	if (mp2 == mp1)
		bcopy((char *)mp1->b_rptr, (char *)&cq->cq_addr, sizeof(cq->cq_addr));
	else
		cq->cq_addr = (char *)mp2->b_cont->b_next;
	mp1 = mp2->b_cont;
	mp->b_datap->db_type = M_COPYOUT;
	mp->b_cont = mp1;
	mp2->b_cont = nil(MBLKP);
	mp1->b_next = nil(MBLKP);
	cq->cq_size = mp1->b_wptr - mp1->b_rptr;
	cq->cq_flag = 0;
	qreply(q, mp);
	return;
}

MBLKP
mi_copyout_alloc (q, mp, uaddr, len)
	queue_t	* q;
	MBLKP	mp;
	char	* uaddr;
	int	len;
{
	struct iocblk * iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
	MBLKP	mp1;

	if ( mp->b_datap->db_type == M_IOCTL ) {
		if (iocp->ioc_count != TRANSPARENT) {
			mp1 = allocb(0, BPRI_MED);
			if (!mp1) {
				iocp->ioc_error = ENOMEM;
				iocp->ioc_count = 0;
				freemsg(mp->b_cont);
				mp->b_cont = nil(MBLKP);
				mp->b_datap->db_type = M_IOCACK;
				qreply(q, mp);
				return nil(MBLKP);
			}
			mp1->b_cont = mp->b_cont;
			mp->b_cont = mp1;
		}
		MI_COPY_COUNT(mp) = 0;
		MI_COPY_DIRECTION(mp) = MI_COPY_OUT;
		/* Make sure it looks clean to mi_copyout. */
		mp->b_datap->db_type = M_IOCDATA;
		((struct copyresp *)iocp)->cp_rval = 0;
	}
	mp1 = allocb(len, BPRI_MED);
	if (!mp1) {
		if (q)
			mi_copy_done(q, mp, ENOMEM);
		return nil(MBLKP);
	}
	linkb(mp, mp1);
	mp1->b_next = (MBLKP)ALIGN32(uaddr);
	return mp1;
}

void
mi_copy_done (q, mp, err)
	queue_t	* q;
	MBLKP	mp;
	int	err;
{
	struct iocblk * iocp;
	MBLKP	mp1;
	
	if (!mp)
		return;
	if (!q  ||  (mp->b_wptr - mp->b_rptr) < sizeof(struct iocblk)) {
		freemsg(mp);
		return;
	}
	iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
	mp->b_datap->db_type = M_IOCACK;
	iocp->ioc_error = err;
	iocp->ioc_count = 0;
	if ((mp1 = mp->b_cont) != NULL) {
		for ( ; mp1; mp1 = mp1->b_cont) {
			mp1->b_prev = nil(MBLKP);
			mp1->b_next = nil(MBLKP);
		}
		freemsg(mp->b_cont);
		mp->b_cont = nil(MBLKP);
	}
	qreply(q, mp);
}

int
mi_copy_state (q, mp, mpp)
	queue_t	* q;
	MBLKP	mp;
	MBLKP	* mpp;
{
	struct iocblk * iocp = (struct iocblk *)ALIGN32(mp->b_rptr);
	struct copyresp * cp = (struct copyresp *)iocp;
	MBLKP	mp1;

	mp1 = mp->b_cont;
	mp->b_cont = cp->cp_private;
	if (mp1) {
		if ( mp1->b_cont  &&  !pullupmsg(mp1, -1) ) {
			mi_copy_done(q, mp, ENOMEM);
			return -1;
		}
		linkb(mp->b_cont, mp1);
	}
	if ( cp->cp_rval ) {
		mi_copy_done(q, mp, (int)cp->cp_rval);
		return -1;
	}
	if ( mpp  &&  MI_COPY_DIRECTION(mp) == MI_COPY_IN )
		*mpp = mp1;
	return MI_COPY_STATE(mp);
}

#ifndef NATIVE_ALLOC			/**/
void
mi_free (ptr)
	char	* ptr;
{
	MBLKP	* mpp;

	if ((mpp = (MBLKP *)ptr)  &&  mpp[-1])
		freeb(mpp[-1]);
}
#endif					/**/

#ifdef NATIVE_ALLOC_KMEM		/**/
void
mi_free (ptr)
	char	* ptr;
{
	int	size;

	if (!ptr)
		return;
	if ((size = ((int *)ALIGN32(ptr))[-1]) <= 0)
		cmn_err(CE_PANIC, "mi_free");

	kmem_free(ptr - sizeof(int), (size_t)size);
}
#endif					/**/

void
mi_detach (mi_headp, ptr)
	void		** mi_headp;
	IDP		ptr;
{
	mi_head_t	* mi_head = *(mi_head_t **)mi_headp;
reg	MI_OP	mi_o = (MI_OP)ALIGN32(ptr);
	MI_OP		insert;

	if (mi_o == NULL)
		return;

	mi_o--;

	/* If we are the insertion point move it to the next guy */
	if (mi_head->mh_insert == mi_o) {
		if (mi_rescan_debug)
			printf("mi_detach: moving insert\n");
		mi_head->mh_insert = mi_o->mi_o_next;
	}
	/*
	 * If we are either edge of the current range update the current
	 * range.
	 */
	if (mi_o->mi_o_dev < MAXMIN) {
		if (mi_o->mi_o_dev == mi_head->mh_min_minor - 1) {
			if (mi_o->mi_o_prev == &mi_head->mh_o) {
				/* First one */
				mi_head->mh_min_minor = 0;
			} else {
				mi_head->mh_min_minor =
					mi_o->mi_o_prev->mi_o_dev + 1;
			}
			if (mi_rescan_debug)
				printf("mi_detach: removed min %d, new %d\n",
				       (int)mi_o->mi_o_dev,
				       mi_head->mh_min_minor);
		}
		if (mi_o->mi_o_dev == mi_head->mh_max_minor) {
			if (mi_o->mi_o_next == &mi_head->mh_o) {
				/* Last one */
				mi_head->mh_max_minor = mi_maxminor;
			} else {
				mi_head->mh_max_minor =
					mi_o->mi_o_next->mi_o_dev;
			}
			if (mi_rescan_debug)
				printf("mi_detach: removed minor %d, new %d\n",
				       (int)mi_o->mi_o_dev,
				       mi_head->mh_max_minor);
		}
	}
	/* Unlink from list */
	mi_o->mi_o_next->mi_o_prev = mi_o->mi_o_prev;
	mi_o->mi_o_prev->mi_o_next = mi_o->mi_o_next;
	mi_o->mi_o_next = mi_o->mi_o_prev = NULL;
	mi_o->mi_o_dev = (unsigned long)OPENFAIL;
	
	/* Reinsert at end of list */
	insert = &mi_head->mh_o;
	mi_o->mi_o_next = insert;
	insert->mi_o_prev->mi_o_next = mi_o;
	mi_o->mi_o_prev = insert->mi_o_prev;
	insert->mi_o_prev = mi_o;

	/*
	 * Make sure that mh_insert is before all the MODOPEN/OPENFAIL
	 * instances.
	 */
	if (mi_head->mh_insert == &mi_head->mh_o)
		mi_head->mh_insert = mi_o;
}

int
mi_iprintf (fmt, ap, putc_func, cookie)
reg	char	* fmt;
	va_list	ap;
	pfi_t	putc_func;
	char	* cookie;
{
reg	int	base;
	char	buf[(sizeof(long) * 3) + 1];
static	char	hex_val[] = "0123456789abcdef";
	int	ch;
	int	count;
	char	* cp1;
	int	digits;
reg	char	* fcp;
	boolean_t	is_long;
	ulong	uval;
	long	val;
	boolean_t	zero_filled;

	if (!fmt)
		return -1;
	count = 0;
	while (*fmt) {
		if (*fmt != '%' || *++fmt == '%') {
			count += (*putc_func)(cookie, *fmt++);
			continue;
		}
		if (*fmt == '0') {
			zero_filled = true;
			fmt++;
			if (!*fmt)
				break;
		} else
			zero_filled = false;
		base = 0;
		for (digits = 0; ISDIGIT(*fmt); fmt++) {
			digits *= 10;
			digits += (*fmt - '0');
		}
		if (!*fmt)
			break;
		is_long = false;
		if (*fmt == 'l') {
			is_long = true;
			fmt++;
		}
		if (!*fmt)
			break;
		ch = *fmt++;
		if (ISUPPER(ch)) {
			ch = tolower(ch);
			is_long = true;
		}
		switch (ch) {
		case 'c':
			count += (*putc_func)(cookie, va_arg(ap, int *));
			continue;
		case 'd':
			base = 10;
			break;
		case 'm':	/* Print out memory, 2 hex chars per byte */
			if (is_long)
				fcp = va_arg(ap, char *);
			else {
				if ((cp1 = va_arg(ap, char *)) != NULL)
					fcp = (char *)cp1;
				else
					fcp = nilp(char);
			}
			if (!fcp) {
				for (fcp = (char *)"(NULL)"; *fcp; fcp++)
					count += (*putc_func)(cookie, *fcp);
			} else {
				while (digits--) {
					int u1 = *fcp++ & 0xFF;
					count += (*putc_func)(cookie,hex_val[(u1>>4)& 0xF]);
					count += (*putc_func)(cookie,hex_val[u1& 0xF]);
				}
			}
			continue;
		case 'o':
			base = 8;
			break;
		case 'x':
			base = 16;
			break;
		case 's':
			if (is_long)
				fcp = va_arg(ap, char *);
			else {
				if ((cp1 = va_arg(ap, char *)) != NULL)
					fcp = (char *)cp1;
				else
					fcp = nilp(char);
			}
			if (!fcp)
				fcp = (char *)"(NULL)";
			while (*fcp) {
				count += (*putc_func)(cookie, *fcp++);
				if (digits  &&  --digits == 0)
					break;
			}
			while (digits > 0) {
				count += (*putc_func)(cookie, ' ');
				digits--;
			}
			continue;
		case 'u':
			base = 10;
			break;
		default:
			return count;
		}
		if (is_long)
			val = va_arg(ap, long);
		else
			val = va_arg(ap, int);
		if (base == 10  &&  ch != 'u') {
			if (val < 0) {
				count += (*putc_func)(cookie, '-');
				val = -val;
			}
			uval = val;
		} else {
			if (is_long)
				uval = val;
			else
				uval = (unsigned)val;
		}
		/* Hand overload/restore the register variable 'fmt' */
		cp1 = fmt;
		fmt = A_END(buf);
		*--fmt = '\0';
		do {
			if (fmt > buf)
				*--fmt = hex_val[uval % base];
			if (digits  &&  --digits == 0)
				break;
		} while (uval /= base);
		if (zero_filled) {
			while (digits > 0  &&  fmt > buf) {
				*--fmt = '0';
				digits--;
			}
		}
		while (*fmt)
			count += (*putc_func)(cookie, *fmt++);
		fmt = cp1;
	}
	return count;
}

#ifdef SVR3_STYLE
boolean_t
mi_link_device (orig_q, name)
reg	queue_t	* orig_q;
	char	* name;
{
reg	queue_t	* q;
	struct streamtab * str;

	str = dname_to_str((char *)name);
	if (q = mi_allocq(str)) {
		q->q_next = orig_q->q_next;
		WR(orig_q->q_next)->q_next = WR(q);
#ifdef MPS
		sqh_set_parent(q, str);
		sqh_set_parent(WR(q), str);
#endif
		if ((*q->q_qinfo->qi_qopen)(q, 0, 0, CLONEOPEN) != OPENFAIL) {
			q->q_next = orig_q;
			WR(orig_q->q_next)->q_next = WR(orig_q);
			WR(orig_q)->q_next = WR(q);
#ifdef	MPS
			/* Fix the flow control pointers.  We assume here that mi_link_device is only called during open. */
			if ( orig_q->q_qinfo->qi_srvp )
				q->q_ffcp = orig_q;
			else {
				q->q_ffcp = orig_q->q_next;
				orig_q->q_next->q_bfcp = q;
			}
			orig_q->q_bfcp = q;
			if ( WR(orig_q)->q_qinfo->qi_srvp )
				WR(q)->q_bfcp = WR(orig_q);
			else {
				WR(q)->q_bfcp = WR(orig_q)->q_bfcp;
				WR(orig_q)->q_bfcp->q_ffcp = WR(q);
			}
			WR(orig_q)->q_ffcp = WR(q);
#endif
			return true;
		}
		q->q_next = nilp(queue_t);
		WR(orig_q->q_next)->q_next = WR(orig_q);
		freeq(q);
#ifndef MPS
#ifndef SUNOS
		strst.queue.use--;
#endif
#endif
	}
	return false;
}
#endif

staticf int
mi_ibc_qenable (long_ibc)
	long	long_ibc;
{
reg	IBCP	ibc = (IBCP)long_ibc;
reg	MI_OP	mi_o;

	mi_o = (MI_OP)((ibc->ibc_size < 0) ? &ibc[-1] : ibc);
	if (mi_o->mi_o_dev == OPENFAIL) {
		if (!mi_o->mi_o_ibcs[0].ibc_q  &&  !mi_o->mi_o_ibcs[1].ibc_q)
			mi_free((IDP)mi_o);
	} else if (ibc->ibc_q)
		qenable(ibc->ibc_q);
	ibc->ibc_q = nilp(queue_t);
	return 0;
}

staticf int
mi_ibc_timer (ibcp)
	IBCPP	ibcp;
{
reg	IBCP	ibc;
	int	size;

	while ((ibc = *ibcp) != NULL) {
		size = (ibc->ibc_size < 0) ? -ibc->ibc_size : ibc->ibc_size;
		if (!bufcall(size - 1, ibc->ibc_pri, (pfv_t)mi_ibc_qenable, 
			     (long)ibc))
			break;
		mi_ibc_timer_del(ibc);
	}
	return 0;
}

staticf void
mi_ibc_timer_add (ibc)
reg	IBCP	ibc;
{
	if (!ibc->ibc_next  &&  ibc != mi_g_ibc_timer_tail) {
		if (mi_g_ibc_timer_head)
			mi_g_ibc_timer_tail->ibc_next = ibc;
		else {
			mi_g_ibc_timer_head = ibc;
			(void) timeout((pfv_t)mi_ibc_timer, 
				(caddr_t)&mi_g_ibc_timer_head, 
				MS_TO_TICKS(4000));
		}
		mi_g_ibc_timer_tail = ibc;
	}
}

staticf void
mi_ibc_timer_del (ibc)
reg	IBCP	ibc;
{
reg	IBCPP	ibcp;
	IBCP	prev_ibc = nil(IBCP);

	for (ibcp = &mi_g_ibc_timer_head; ibcp[0]; ibcp = &ibcp[0]->ibc_next) {
		if (ibcp[0] == ibc) {
			ibcp[0] = ibc->ibc_next;
			if (ibc == mi_g_ibc_timer_tail)
				mi_g_ibc_timer_tail = prev_ibc;
			break;
		}
		prev_ibc = ibcp[0];
	}
}

#ifdef USE_STDARG
int
mi_mpprintf (MBLKP mp, char * fmt, ...)
#else
int
mi_mpprintf (mp, fmt, va_alist)
	MBLKP	mp;
	char	* fmt;
	va_dcl
#endif
{
	va_list	ap;
	int	count = -1;
#ifdef USE_STDARG
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	if (mp) {
		count = mi_iprintf(fmt, ap, (pfi_t)mi_mpprintf_putc, (char *)mp);
		if (count != -1)
			mi_mpprintf_putc((char *)mp, '\0');
	}
	va_end(ap);
	return count;
}

#ifdef USE_STDARG
int
mi_mpprintf_nr (MBLKP mp, char * fmt, ...)
#else
int
mi_mpprintf_nr (mp, fmt, va_alist)
	MBLKP	mp;
	char	* fmt;
	va_dcl
#endif
{
	va_list	ap;
	int	count = -1;
#ifdef USE_STDARG
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	if (mp) {
		adjmsg(mp, -1);
		count = mi_iprintf(fmt, ap, (pfi_t)mi_mpprintf_putc, (char *)mp);
		if (count != -1)
			mi_mpprintf_putc((char *)mp, '\0');
	}
	va_end(ap);
	return count;
}

int
mi_mpprintf_putc (cookie, ch)
	char	* cookie;
	int	ch;
{
reg	MBLKP	mp = (MBLKP)ALIGN32(cookie);

	while (mp->b_cont)
		mp = mp->b_cont;
	if (mp->b_wptr >= mp->b_datap->db_lim) {
		mp->b_cont = allocb(1024, BPRI_HI);
		mp = mp->b_cont;
		if (!mp)
			return 0;
	}
	*mp->b_wptr++ = (unsigned char)ch;
	return 1;
}

IDP
mi_first_ptr (mi_headp)
	void		** mi_headp;
{
	mi_head_t	* mi_head = *(mi_head_t **)mi_headp;
	MI_OP	mi_op;

	mi_op = mi_head->mh_o.mi_o_next;
	if (mi_op && mi_op != &mi_head->mh_o)
		return (IDP)&mi_op[1];
	return nil(IDP);
}

IDP
mi_next_ptr (mi_headp, ptr)
	void	** mi_headp;
	IDP	ptr;
{
	mi_head_t	* mi_head = *(mi_head_t **)mi_headp;
	MI_OP	mi_op = ((MI_OP)ALIGN32(ptr)) - 1;

	if ((mi_op = mi_op->mi_o_next) != NULL &&
	    mi_op != &mi_head->mh_o)
		return (IDP)&mi_op[1];
	return nil(IDP);
}

staticf mi_dev_rescan(mi_head)
	mi_head_t	* mi_head;
{
	int maxrange = 1, prevdev = 0, nextdev = 0;
	MI_OP	maxinsert = NULL;
	MI_OP	mi_o;

	if (mi_rescan_debug)
		printf("mi_open_comm: exceeded max %d\n",
		       mi_head->mh_max_minor);
				
	if (mi_head->mh_o.mi_o_next == &mi_head->mh_o) {
		if (mi_rescan_debug)
			printf("mi_open_comm: nothing to scan\n");
		mi_head->mh_min_minor = 0;
		mi_head->mh_max_minor = mi_maxminor;
		mi_head->mh_insert = &mi_head->mh_o;
		return 0;
	}
	for (mi_o = mi_head->mh_o.mi_o_next;
	     mi_o != &mi_head->mh_o && mi_o->mi_o_dev < MAXMIN;
	     mi_o = mi_o->mi_o_next) {
		nextdev = mi_o->mi_o_dev;
		if (nextdev - prevdev > maxrange) {
			if (mi_rescan_debug)
				printf("max: %d, %d\n", prevdev, nextdev);
			maxrange = nextdev - prevdev;
			maxinsert = mi_o;
		}
		prevdev = mi_o->mi_o_dev;
	}
	/* Last one */
	nextdev = mi_maxminor;
	if (nextdev - prevdev > maxrange) {
		if (mi_rescan_debug)
			printf("last max: %d, %d\n", prevdev, nextdev);
		maxrange = nextdev - prevdev;
		maxinsert = mi_o;
	}
	if (maxinsert == NULL) {
		if (mi_rescan_debug)
			printf("No minors left\n");
		return EBUSY;
	}
	if (maxinsert == mi_head->mh_o.mi_o_next) {
		/* First */
		if (mi_rescan_debug)
			printf("mi_open_comm: got first\n");
		prevdev = 0;
		nextdev = maxinsert->mi_o_dev;
	} else if (maxinsert == &mi_head->mh_o ||
		   maxinsert->mi_o_dev >= MAXMIN) {
		/* Last */
		if (mi_rescan_debug)
			printf("mi_open_comm: got last\n");
		prevdev = maxinsert->mi_o_prev->mi_o_dev + 1;
		nextdev = mi_maxminor;
	} else {
		prevdev = maxinsert->mi_o_prev->mi_o_dev + 1;
		nextdev = maxinsert->mi_o_dev;
	}
	ASSERT(nextdev - prevdev >= 1);
	if (mi_rescan_debug)
		printf("mi_open_comm: min %d, max %d\n", prevdev, nextdev);
	mi_head->mh_min_minor = prevdev;
	mi_head->mh_max_minor = nextdev;
	mi_head->mh_insert = maxinsert;
	return 0;
}

/*
 * If sflag == CLONEOPEN, search for the lowest number available.
 * If sflag != CLONEOPEN then attempt to open the 'dev' given.
 */
/* ARGSUSED4 */
int
mi_open_comm (mi_headp, size, q, devp, flag, sflag, credp)
	void		** mi_headp;
	uint	size;
	queue_t	* q;
	dev_t	* devp;
	int	flag;
	int	sflag;
	cred_t	* credp;
{
	mi_head_t	* mi_head = *(mi_head_t **)mi_headp;
	MI_OP		insert;
	MI_OP		mi_o;
	dev_t		dev;

	if (!mi_head) {
		mi_head = (mi_head_t *)ALIGN32(mi_zalloc_sleep(
							sizeof (mi_head_t)));
		*mi_headp = (void *)mi_head;
		/* Setup doubly linked list */
		mi_head->mh_o.mi_o_next = &mi_head->mh_o;
		mi_head->mh_o.mi_o_prev = &mi_head->mh_o;
		/* mi_dev_rescan will setup the initial values */
	}
	if (!q  ||  size > (MAX_UINT - sizeof(MI_O)))
		return ENXIO;
	if (q->q_ptr)
		return 0;
	if (sflag == MODOPEN) {
		devp = nilp(dev_t);
		/*
		 * Set device number to MAXMIN + incrementing number
		 * and insert at tail.
		 */
		dev = MAXMIN + ++mi_head->mh_module_dev;
		insert = &mi_head->mh_o;
	} else {
		if ( !devp )
			return ENXIO;
		if (sflag == CLONEOPEN) {
			if (mi_head->mh_min_minor >= mi_head->mh_max_minor) {
				int error;

				error = mi_dev_rescan(mi_head);
				if (error)
					return error;
			}
			insert = mi_head->mh_insert;
			dev = mi_head->mh_min_minor++;
		} else {
			dev = geteminor(*devp);
			for (insert = mi_head->mh_o.mi_o_next;
			     insert != &mi_head->mh_o;
			     insert = insert->mi_o_next) {
				if (insert->mi_o_dev > dev) {
					/* found free slot */
					break;
				}
				if (insert->mi_o_dev == dev)
					return EBUSY;
			}
		}
	}
	/* Insert before "insert" */
	if ((mi_o = (MI_OP)ALIGN32(mi_zalloc_sleep(size + sizeof(MI_O))))
	    == NULL)
		return EAGAIN;
	mi_o->mi_o_dev = dev;
	mi_o->mi_o_next = insert;
	insert->mi_o_prev->mi_o_next = mi_o;
	mi_o->mi_o_prev = insert->mi_o_prev;
	insert->mi_o_prev = mi_o;

	/*
	 * Make sure that mh_insert is before all the MODOPEN/OPENFAIL
	 * instances.
	 */
	if (dev > MAXMIN && mi_head->mh_insert == &mi_head->mh_o)
		mi_head->mh_insert = mi_o;

	mi_o++;
	q->q_ptr = (IDP)mi_o;
	OTHERQ(q)->q_ptr = (IDP)mi_o;
	if ( devp )
		*devp = makedevice(getemajor(*devp), dev);
	return 0;
}

u8 *
mi_offset_param (mp, offset, len)
	mblk_t	* mp;
	u32	offset;
	u32	len;
{
	unsigned int	msg_len;

	if (!mp)
		return nilp(u8);
	msg_len = mp->b_wptr - mp->b_rptr;
	if ((int)msg_len <= 0
	||  offset > msg_len
	||  len > msg_len
	||  (offset + len) > msg_len
	||  len == 0)
		return nilp(u8);
	return &mp->b_rptr[offset];
}

u8 *
mi_offset_paramc (mp, offset, len)
	mblk_t	* mp;
	u32	offset;
	u32	len;
{
	u8	* param;

	for ( ; mp; mp = mp->b_cont) {
		int type = mp->b_datap->db_type;
		if (datamsg(type)) {
			if (param = mi_offset_param(mp, offset, len))
				return param;
			if (offset < (mp->b_wptr - mp->b_rptr))
				break;
			offset -= (mp->b_wptr - mp->b_rptr);
		}
	}
	return nilp(u8);
}

#ifdef USE_STDARG
int
mi_panic (char * fmt, ...   )
#else
int
mi_panic (fmt, va_alist     )
	char	* fmt;
	va_dcl
#endif
{
	va_list	ap;
	char	lbuf[256];
	char	* buf = lbuf;
#ifdef USE_STDARG
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	mi_iprintf(fmt, ap, (pfi_t)mi_sprintf_putc, (char *)&buf);
	mi_sprintf_putc((char *)&buf, '\0');
	va_end(ap);
	cmn_err(CE_PANIC, lbuf);
	return (0);
	/* NOTREACHED */
}

boolean_t
mi_set_sth_hiwat (q, size)
	queue_t	* q;
	int	size;
{
reg	MBLKP	mp;
	STROPTP stropt;

	if (!(mp = allocb(sizeof(*stropt), BPRI_LO)))
		return false;
	mp->b_datap->db_type = M_SETOPTS;
	mp->b_wptr += sizeof(*stropt);
	stropt = (STROPTP)ALIGN32(mp->b_rptr);
	stropt->so_flags = SO_HIWAT;
	stropt->so_hiwat = size;
	putnext(q, mp);
	return true;
}

boolean_t
mi_set_sth_lowat (q, size)
	queue_t	* q;
	int	size;
{
reg	MBLKP	mp;
	STROPTP stropt;

	if (!(mp = allocb(sizeof(*stropt), BPRI_LO)))
		return false;
	mp->b_datap->db_type = M_SETOPTS;
	mp->b_wptr += sizeof(*stropt);
	stropt = (STROPTP)ALIGN32(mp->b_rptr);
	stropt->so_flags = SO_LOWAT;
	stropt->so_lowat = size;
	putnext(q, mp);
	return true;
}

int mi_so_maxblk = 1;
/* ARGSUSED */
boolean_t
mi_set_sth_maxblk (q, size)
	queue_t	* q;
	int	size;
{
#ifdef SO_MAXBLK
reg	MBLKP	mp;
	STROPTP stropt;

	if (!mi_so_maxblk)
		return true;
	if (!(mp = allocb(sizeof(*stropt), BPRI_LO)))
		return false;
	mp->b_datap->db_type = M_SETOPTS;
	mp->b_wptr += sizeof(*stropt);
	stropt = (STROPTP)ALIGN32(mp->b_rptr);
	stropt->so_flags = SO_MAXBLK;
	stropt->so_maxpsz = size;
	putnext(q, mp);
#endif /* SO_MAXBLK */
	return true;
}

boolean_t
mi_set_sth_wroff (q, size)
	queue_t	* q;
	int	size;
{
reg	MBLKP	mp;
	STROPTP stropt;

	if (!(mp = allocb(sizeof(*stropt), BPRI_LO)))
		return false;
	mp->b_datap->db_type = M_SETOPTS;
	mp->b_wptr += sizeof(*stropt);
	stropt = (STROPTP)ALIGN32(mp->b_rptr);
	stropt->so_flags = SO_WROFF;
	stropt->so_wroff = (u_short)size;
	putnext(q, mp);
	return true;
}

#ifdef USE_STDARG
int
mi_sprintf (char * buf, char * fmt, ...)
#else
int
mi_sprintf (buf, fmt, va_alist)
	char	* buf;
	char	* fmt;
	va_dcl
#endif
{
	va_list	ap;
	int	count = -1;
#ifdef USE_STDARG
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	if (buf) {
		count = mi_iprintf(fmt, ap, (pfi_t)mi_sprintf_putc, (char *)&buf);
		if (count != -1)
			mi_sprintf_putc((char *)&buf, '\0');
	}
	va_end(ap);
	return count;
}

/* Used to count without writing data */
/* ARGSUSED1 */
staticf int
mi_sprintf_noop (cookie, ch)
	char	* cookie;
	int	ch;
{
	char	** cpp = (char **)ALIGN32(cookie);

	(*cpp)++;
	return 1;
}

int
mi_sprintf_putc (cookie, ch)
	char	* cookie;
	int	ch;
{
	char	** cpp = (char **)ALIGN32(cookie);

	**cpp = (char)ch;
	(*cpp)++;
	return 1;
}

int
mi_strcmp (cp1, cp2)
reg	char	* cp1;
reg	char	* cp2;
{
	while (*cp1++ == *cp2++) {
		if (!cp2[-1])
			return 0;
	}
	return ((uint)cp2[-1]  & 0xFF) - ((uint)cp1[-1] & 0xFF);
}

int
mi_strlen (str)
	char	* str;
{
reg	char	* cp;

	cp = str;
	while (*cp)
		cp++;
	return cp - str;
}

#ifdef USE_STDARG
int
mi_strlog (queue_t * q, ...)
#else
int
mi_strlog (q, va_alist)
	queue_t	* q;
	va_dcl
#endif
{
	va_list	ap;
	char	buf[200];
	char	* alloc_buf = buf;
	int	count = -1;
	char	* cp;
	ushort	flags;
	char	* fmt;
	char	level;
	short	mid;
	MI_OP	mi_op;
	int	ret;
	short	sid;
#ifdef USE_STDARG
	va_start(ap, q);
#else
	va_start(ap);
#endif
	sid = 0;
	mid = 0;
	if (q) {
		if ((mi_op = (MI_OP)q->q_ptr) != NULL)
			sid = mi_op[-1].mi_o_dev;
		mid = q->q_qinfo->qi_minfo->mi_idnum;
	}
	level = (char)va_arg(ap, int);
	flags = (ushort)va_arg(ap, int);
	fmt = va_arg(ap, char *);

	/* Find out how many bytes we need and allocate if necesary */
	cp = buf;
	count = mi_iprintf(fmt, ap, mi_sprintf_noop, (char *)&cp);
	if (count > sizeof(buf)
	&& !(alloc_buf = mi_alloc((uint)count + 2, BPRI_MED))) {
		va_end(ap);
		return -1;
	}

	cp = alloc_buf;
	count = mi_iprintf(fmt, ap, mi_sprintf_putc, (char *)&cp);
	if (count != -1)
		mi_sprintf_putc((char *)&cp, '\0');
	else
		alloc_buf[0] = '\0';
	va_end(ap);
	ret = strlog(mid, sid, level, flags, alloc_buf);
	if (alloc_buf != buf)
		mi_free(alloc_buf);
	return ret;
}

#if 0
staticf char *
mi_strchr (str, ch)
	char	* str;
	int	ch;
{
reg	char	* cp;

	if ((cp = str) != NULL) {
		for ( ; *cp != ch; cp++) {
			if (!*cp)
				return nilp(char);
		}
	}
	return cp;
}
#endif

long
mi_strtol (str, ptr, base)
	char	* str;
	char	** ptr;
	int	base;
{
reg	char	* cp;
	long	digits, value;
	boolean_t	is_negative;

	cp = str;
	while (*cp == ' '  ||  *cp == '\t'  ||  *cp == '\n')
		cp++;
	is_negative = (*cp == '-');
	if (is_negative)
		cp++;
	if (base == 0) {
		base = 10;
		if (*cp == '0') {
			base = 8;
			cp++;
			if (*cp == 'x'  ||  *cp == 'X') {
				base = 16;
				cp++;
			}
		}
	}
	value = 0;
	for (; *cp; cp++) {
		if (*cp >= '0'  &&  *cp <= '9')
			digits = *cp - '0';
		else if (*cp >= 'a'  &&  *cp <= 'f')
			digits = *cp - 'a' + 10;
		else if (*cp >= 'A'  &&  *cp <= 'F')
			digits = *cp - 'A' + 10;
		else
			break;
		if (digits >= base)
			break;
		value = (value * base) + digits;
	}
	if (ptr)
		*ptr = cp;
	if (is_negative)
		value = -value;
	return value;
}

/*
 *		mi_timer mechanism.
 *
 * Each timer is represented by a timer mblk and a (streams) queue. When the
 * timer fires the timer mblk will be put on the associated streams queue
 * so that the streams module can process the timer even in its service
 * procedure.
 *
 * The interface consists of 4 entry points:
 *	mi_timer_alloc		- create a timer mblk
 *	mi_timer_free		- free a timer mblk
 *	mi_timer		- start, restart, stop, or move the
 *				  timer to a different queue
 *	mi_timer_valid		- called by streams module to verify that
 *				  the timer did indeed fire.
 */

/*
 * Start, restart, stop, or move the timer to a new queue.
 * If "tim" is -2 the timer is moved to a different queue.
 * If "tim" is -1 the timer is stopped.
 * Otherwise, the timer is stopped if it is already running, and
 * set to fire tim milliseconds from now.
 */
void
mi_timer (q, mp, tim)
	queue_t	* q;
	MBLKP	mp;
	long	tim;
{
	MTBP	mtb;
	long	state;

	if (!q
	||  !mp
	||  (mp->b_rptr - mp->b_datap->db_base) != sizeof(MTB))
		return;
	mtb = (MTBP)ALIGN32(mp->b_datap->db_base);
	ASSERT(mp->b_datap->db_type == M_PCSIG);
	if (tim >= 0) {
		mtb->mtb_q = q;
		state = mtb->mtb_state;
		tim = MS_TO_TICKS(tim);
		if (state == TB_RUNNING) {
			if (untimeout(mtb->mtb_tid) < 0 ) {
				/* Message has already been putq */
				ASSERT(mtb->mtb_q->q_first == mp ||
				       mp->b_prev || mp->b_next);
				mtb->mtb_state = TB_RESCHED;
				mtb->mtb_time_left = tim;
				/* mi_timer_valid will start timer */
				return;
			}
		} else if (state != TB_IDLE) {
			ASSERT(state != TB_TO_BE_FREED);
			if (state == TB_CANCELLED) {
				ASSERT(mtb->mtb_q->q_first == mp ||
				       mp->b_prev || mp->b_next);
				mtb->mtb_state = TB_RESCHED;
				mtb->mtb_time_left = tim;
				/* mi_timer_valid will start timer */
				return;
			}
			if (state == TB_RESCHED) {
				ASSERT(mtb->mtb_q->q_first == mp ||
				       mp->b_prev || mp->b_next);
				mtb->mtb_time_left = tim;
				/* mi_timer_valid will start timer */
				return;
			}
		}
		mtb->mtb_state = TB_RUNNING;
		mtb->mtb_tid = timeout((pfv_t)mi_timer_fire, (caddr_t)mtb,
				       tim);
		return;
	}
	switch (tim) {
	case -1:
		mi_timer_stop(mp);
		break;
	case -2:
		mi_timer_move(q, mp);
		break;
	default:
		mi_panic("mi_timer: bad tim value: %d", tim);
		break;
	}
}

/*
 * Allocate an M_PCSIG timer message. The space between db_base and
 * b_rptr is used by the mi_timer mechanism, and after b_rptr there are
 * "size" bytes that the caller can use for its own purposes.
 *
 * Note that db_type has to be a priority message since otherwise
 * the putq will not cause the service procedure to run when
 * there is flow control.
 */
MBLKP
mi_timer_alloc (size)
	uint	size;
{
reg	MBLKP	mp;
reg	MTBP	mtb;

	if ((mp = allocb(size + sizeof(MTB), BPRI_HI)) != NULL) {
		mp->b_datap->db_type = M_PCSIG;
		mtb = (MTBP)ALIGN32(mp->b_datap->db_base);
		mp->b_rptr = (u8 *)&mtb[1];
		mp->b_wptr = mp->b_rptr + size;
		mtb->mtb_state = TB_IDLE;
		mtb->mtb_mp = mp;
		mtb->mtb_q = nil(queue_t *);
		return mp;
	}
	return nil(MBLKP);
}

/*
 * timeout() callback function.
 * Put the message on the current queue.
 * If the timer is stopped or moved to a different queue after
 * it has fired then mi_timer() and mi_timer_valid() will clean
 * things up.
 */
staticf int
mi_timer_fire (mtb)
	MTBP	mtb;
{
	ASSERT(mtb == (MTBP)ALIGN32(mtb->mtb_mp->b_datap->db_base));
	ASSERT(mtb->mtb_mp->b_datap->db_type == M_PCSIG);
	return putq(mtb->mtb_q, mtb->mtb_mp);
}

/*
 * Logically free a timer mblk (that might have a pending timeout().)
 * If the timer has fired and the mblk has been put on the queue then
 * mi_timer_valid will free the mblk.
 */
void
mi_timer_free (mp)
	MBLKP	mp;
{
reg	MTBP	mtb;
	long	state;

	if (!mp
	|| (mp->b_rptr - mp->b_datap->db_base) != sizeof(MTB))
		return;
	mtb = (MTBP)ALIGN32(mp->b_datap->db_base);
	state = mtb->mtb_state;
	if (state == TB_RUNNING ) {
		if (untimeout(mtb->mtb_tid) < 0 ) {
			/* Message has already been putq */
			ASSERT(mtb->mtb_q->q_first == mp ||
			       mp->b_prev || mp->b_next);
			mtb->mtb_state = TB_TO_BE_FREED;
			/* mi_timer_valid will free the mblk */
			return;
		}
	} else if ( state != TB_IDLE ) {	
		/* Message has already been putq */
		ASSERT(mtb->mtb_q->q_first == mp ||
		       mp->b_prev || mp->b_next);
		ASSERT(state != TB_TO_BE_FREED);
		mtb->mtb_state = TB_TO_BE_FREED;
		/* mi_timer_valid will free the mblk */
		return;
	}
	ASSERT(mtb->mtb_q ==  NULL || mtb->mtb_q->q_first != mp);
	freemsg(mp);
}

/*
 * Called from mi_timer(,,-2)
 */
void
mi_timer_move (q, mp)
	queue_t	* q;
	MBLKP	mp;
{
	MTBP	mtb;
	int	tim;

	mtb = (MTBP)ALIGN32(mp->b_datap->db_base);
	/*
	 * Need to untimeout and restart to make
	 * sure that the mblk is not about to be putq on the old queue
	 * by mi_timer_fire.
	 */
	if (mtb->mtb_state == TB_RUNNING) {
		if ((tim = untimeout(mtb->mtb_tid)) < 0 ) {
			/*
			 * Message has already been putq. Move from old queue
			 * to new queue.
			 */
			ASSERT(mtb->mtb_q->q_first == mp ||
			       mp->b_prev || mp->b_next);
			rmvq(mtb->mtb_q, mp);
			ASSERT(mtb->mtb_q->q_first != mp &&
			       mp->b_prev == NULL && mp->b_next == NULL);
			mtb->mtb_q = q;
			(void) putq(mtb->mtb_q, mp);
			return;
		}
		mtb->mtb_q = q;
		mtb->mtb_state = TB_RUNNING;
		mtb->mtb_tid = timeout((pfv_t)mi_timer_fire,
				       (caddr_t)mtb,
				       tim);
	} else if (mtb->mtb_state != TB_IDLE) {
		ASSERT(mtb->mtb_state != TB_TO_BE_FREED);
		/*
		 * Message is already sitting on queue. Move to new queue.
		 */
		ASSERT(mtb->mtb_q->q_first == mp ||
		       mp->b_prev || mp->b_next);
		rmvq(mtb->mtb_q, mp);
		ASSERT(mtb->mtb_q->q_first != mp &&
		       mp->b_prev == NULL && mp->b_next == NULL);
		mtb->mtb_q = q;
		(void) putq(mtb->mtb_q, mp);
	} else
		mtb->mtb_q = q;
}

#ifdef notyet
void
mi_timer_start (mp, tim)
	MBLKP	mp;
	long	tim;
{
	MTBP	mtb;
	long	state;

	mtb = (MTBP)ALIGN32(mp->b_datap->db_base);
	tim = MS_TO_TICKS(tim);
	state = mtb->mtb_state;
	if (state == TB_RUNNING) {
		if (untimeout(mtb->mtb_tid) < 0 ) {
			/* Message has already been putq */
			ASSERT(mtb->mtb_q->q_first == mp ||
			       mp->b_prev || mp->b_next);
			mtb->mtb_state = TB_RESCHED;
			mtb->mtb_time_left = tim;
			/* mi_timer_valid will start timer */
			return;
		}
	} else if (state != TB_IDLE) {
		ASSERT(state != TB_TO_BE_FREED);
		if (state == TB_CANCELLED) {
			ASSERT(mtb->mtb_q->q_first == mp ||
			       mp->b_prev || mp->b_next);
			mtb->mtb_state = TB_RESCHED;
			mtb->mtb_time_left = tim;
			/* mi_timer_valid will start timer */
			return;
		}
		if (state == TB_RESCHED) {
			ASSERT(mtb->mtb_q->q_first == mp ||
			       mp->b_prev || mp->b_next);
			mtb->mtb->mtb_time_left = tim;
			/* mi_timer_valid will start timer */
			return;
		}
	}
	mtb->mtb_state = TB_RUNNING;
	mtb->mtb_tid = timeout((pfv_t)mi_timer_fire,
			       (caddr_t)mtb,
			       tim);
}
#endif

/*
 * Called from mi_timer(,,-1)
 */
void
mi_timer_stop (mp)
	MBLKP	mp;
{
	MTBP	mtb;
	long	state;

	mtb = (MTBP)ALIGN32(mp->b_datap->db_base);
	state = mtb->mtb_state;
	if (state == TB_RUNNING) {
		if (untimeout(mtb->mtb_tid) < 0 ) {
			/* Message has already been putq */
			ASSERT(mtb->mtb_q->q_first == mp ||
			       mp->b_prev || mp->b_next);
			mtb->mtb_state = TB_CANCELLED;
		} else {
			mtb->mtb_state = TB_IDLE;
		}
	} else if (state == TB_RESCHED) {
		ASSERT(mtb->mtb_q->q_first == mp ||
		       mp->b_prev || mp->b_next);
		mtb->mtb_state = TB_CANCELLED;
	}
}

/*
 * The user of the mi_timer mechanism is required to call mi_timer_valid() for
 * each M_PCSIG message processed in the service procedures.
 * mi_timer_valid will return "true" if the timer actually did fire.
 */
boolean_t
mi_timer_valid (mp)
	MBLKP	mp;
{
reg	MTBP	mtb;
	long	state;

	if (!mp
	||  (mp->b_rptr - mp->b_datap->db_base) != sizeof(MTB)
	||  mp->b_datap->db_type != M_PCSIG)
		return false;
	mtb = (MTBP)ALIGN32(mp->b_datap->db_base);
	state = mtb->mtb_state;
	if ( state != TB_RUNNING ) {
		ASSERT(state != TB_IDLE);
		if ( state == TB_TO_BE_FREED ) {
			/*
			 * mi_timer_free was called after the message
			 * was putq'ed.
			 */
			freemsg(mp);
			return false;
		}
		if ( state == TB_CANCELLED ) {
			/* The timer was stopped after the mblk was putq'ed */
			mtb->mtb_state = TB_IDLE;
			return false;
		}
		if ( state == TB_RESCHED ) {
			/*
			 * The timer was stopped and then restarted after
			 * the mblk was putq'ed.
			 * mtb_time_left contains the number of ticks that
			 * the timer was restarted with.
			 */
			mtb->mtb_state = TB_RUNNING;
			mtb->mtb_tid = timeout((pfv_t)mi_timer_fire,
					       (caddr_t)mtb,
					       mtb->mtb_time_left);
			return false;
		}
	}
	mtb->mtb_state = TB_IDLE;
	return true;
}

MBLKP
mi_reallocb (mp, new_size)
	MBLKP	mp;
	int	new_size;
{
	MBLKP	mp1;
	int	our_size;

	if ((mp->b_datap->db_lim - mp->b_rptr) >= new_size)
		return mp;
	our_size = mp->b_wptr - mp->b_rptr;
	if ((mp->b_datap->db_lim - mp->b_datap->db_base) >= new_size) {
		bcopy((char *)mp->b_rptr, (char *)mp->b_datap->db_base, our_size);
		mp->b_rptr = mp->b_datap->db_base;
		mp->b_wptr = mp->b_rptr + our_size;
		return mp;
	}
	if (mp1 = allocb(new_size, BPRI_MED)) {
		mp1->b_wptr = mp1->b_rptr + our_size;
		mp1->b_datap->db_type = mp->b_datap->db_type;
		mp1->b_cont = mp->b_cont;
		bcopy((char *)mp->b_rptr, (char *)mp1->b_rptr, our_size);
		freeb(mp);
	}
	return mp1;
}

/* Allocate and return a TPI ack packet, filling in the primitive type
** and bumping 'mp->b_wptr' down by the size indicated.  If 'mp' is non-nil
** and not reuseable, blow it off.
*/
MBLKP
mi_tpi_ack_alloc (mp, size, type)
reg	MBLKP	mp;
	uint	size;
	uint	type;
{
	if (mp) {
		struct datab * db = mp->b_datap;
		if (db->db_ref == 1
		&& (db->db_lim - db->db_base) >= size) {
			if (mp->b_cont) {
				freemsg(mp->b_cont);
				mp->b_cont = nilp(mblk_t);
			}
			mp->b_rptr = db->db_base;
			goto ok;
		}
		freemsg(mp);
	}
	mp = allocb(size, BPRI_HI);
	if (mp) {
ok:		mp->b_datap->db_type = M_PCPROTO;
		mp->b_wptr = &mp->b_rptr[size];
		((TPRIMP)ALIGN32(mp->b_rptr))->type = type;
	}
	return mp;
}

staticf void
mi_tpi_addr_and_opt (mp, addr, addr_length, opt, opt_length)
	MBLKP	mp;
	char	* addr;
	int	addr_length;
	char	* opt;
	int	opt_length;
{
	struct T_unitdata_ind	* tudi;

	tudi = (struct T_unitdata_ind *)ALIGN32(mp->b_rptr);
	tudi->SRC_offset = mp->b_wptr - mp->b_rptr;
	tudi->SRC_length = addr_length;
	if (addr_length > 0) {
		bcopy(addr, (char *)mp->b_wptr, addr_length);
		mp->b_wptr += addr_length;
	}
	tudi->OPT_offset = mp->b_wptr - mp->b_rptr;
	tudi->OPT_length = opt_length;
	if (opt_length > 0) {
		bcopy(opt, (char *)mp->b_wptr, opt_length);
		mp->b_wptr += opt_length;
	}
}

MBLKP
mi_tpi_conn_con (trailer_mp, src, src_length, opt, opt_length)
	MBLKP	trailer_mp;
	char	* src;
	int	src_length;
	char	* opt;
	int	opt_length;
{
	int	len;
reg	MBLKP	mp;

	len = sizeof(struct T_conn_con) + src_length + opt_length;
	if ((mp = mi_tpi_trailer_alloc(trailer_mp, len, T_CONN_CON)) != NULL) {
		mp->b_wptr = &mp->b_rptr[sizeof(struct T_conn_con)];
		mi_tpi_addr_and_opt(mp, src, src_length, opt, opt_length);
	}
	return mp;
}

MBLKP
mi_tpi_conn_ind (trailer_mp, src, src_length, opt, opt_length, seqnum)
	MBLKP	trailer_mp;
	char	* src;
	int	src_length;
	char	* opt;
	int	opt_length;
	int	seqnum;
{
	int	len;
reg	MBLKP	mp;

	len = sizeof(struct T_conn_ind) + src_length + opt_length;
	if ((mp = mi_tpi_trailer_alloc(trailer_mp, len, T_CONN_IND)) != NULL) {
		mp->b_wptr = &mp->b_rptr[sizeof(struct T_conn_ind)];
		mi_tpi_addr_and_opt(mp, src, src_length, opt, opt_length);
		((struct T_conn_ind *)ALIGN32(mp->b_rptr))->SEQ_number = seqnum;
		mp->b_datap->db_type = M_PROTO;
	}
	return mp;
}

MBLKP
mi_tpi_discon_ind (trailer_mp, reason, seqnum)
	MBLKP	trailer_mp;
	int	reason;
	int	seqnum;
{
	MBLKP	mp;
	struct T_discon_ind	* tdi;

	if ((mp = mi_tpi_trailer_alloc(trailer_mp, sizeof(struct T_discon_ind),
					T_DISCON_IND)) != NULL) {
		tdi = (struct T_discon_ind *)ALIGN32(mp->b_rptr);
		tdi->DISCON_reason = reason;
		tdi->SEQ_number = seqnum;
	}
	return mp;
}

/* Allocate and fill in a TPI err ack packet using the 'mp' passed in
** for the 'error_prim' context as well as sacrifice.
*/
MBLKP
mi_tpi_err_ack_alloc (mp, tlierr, unixerr)
reg	MBLKP	mp;
	int	tlierr;
	int	unixerr;
{
	struct T_error_ack	* teackp;
	long	error_prim;

	if (!mp)
		return nil(MBLKP);
	error_prim = ((TPRIMP)ALIGN32(mp->b_rptr))->type;
	if ((mp = mi_tpi_ack_alloc(mp, sizeof(struct T_error_ack),
					T_ERROR_ACK)) != NULL) {
		teackp = (struct T_error_ack *)ALIGN32(mp->b_rptr);
		teackp->ERROR_prim = error_prim;
		teackp->TLI_error = tlierr;
		teackp->UNIX_error = unixerr;
	}
	return mp;
}

MBLKP
mi_tpi_ok_ack_alloc (mp)
	MBLKP	mp;
{
	long	correct_prim;

	if (!mp)
		return nil(MBLKP);
	correct_prim = ((TPRIMP)ALIGN32(mp->b_rptr))->type;
	if ((mp = mi_tpi_ack_alloc(mp, sizeof(struct T_ok_ack),
					T_OK_ACK)) != NULL)
		((struct T_ok_ack *)ALIGN32(mp->b_rptr))->CORRECT_prim =
			correct_prim;
	return mp;
}

MBLKP
mi_tpi_ordrel_ind ()
{
reg	MBLKP	mp;

	if ((mp = allocb(sizeof(struct T_ordrel_ind), BPRI_HI)) != NULL) {
		mp->b_datap->db_type = M_PROTO;
		((struct T_ordrel_ind *)ALIGN32(mp->b_rptr))->PRIM_type =
			T_ORDREL_IND;
		mp->b_wptr += sizeof(struct T_ordrel_ind);
	}
	return mp;
}

MBLKP
mi_tpi_trailer_alloc (trailer_mp, size, type)
	MBLKP	trailer_mp;
	int	size;
	int	type;
{
reg	MBLKP	mp;

	if ((mp = allocb(size, BPRI_MED)) != NULL) {
		mp->b_cont = trailer_mp;
		mp->b_datap->db_type = M_PROTO;
		((union T_primitives *)ALIGN32(mp->b_rptr))->type = type;
		mp->b_wptr += size;
	}
	return mp;
}

MBLKP
mi_tpi_uderror_ind (dest, dest_length, opt, opt_length, error)
	char	* dest;
	int	dest_length;
	char	* opt;
	int	opt_length;
	int	error;
{
	int	len;
	MBLKP	mp;
	struct T_uderror_ind	* tudei;

	len = sizeof(struct T_uderror_ind) + dest_length + opt_length;
	if ((mp = allocb(len, BPRI_HI)) != NULL) {
		mp->b_datap->db_type = M_PROTO;
		tudei = (struct T_uderror_ind *)ALIGN32(mp->b_rptr);
		tudei->PRIM_type = T_UDERROR_IND;
		tudei->ERROR_type = error;
		mp->b_wptr = &mp->b_rptr[sizeof(struct T_uderror_ind)];
		mi_tpi_addr_and_opt(mp, dest, dest_length, opt, opt_length);
	}
	return mp;
}

IDP
mi_zalloc (size)
	uint	size;
{
	IDP	ptr;

	if (ptr = mi_alloc(size, BPRI_LO))
		bzero(ptr, size);
	return ptr;
}

IDP
mi_zalloc_sleep (size)
	uint	size;
{
	IDP	ptr;

	if (ptr = mi_alloc_sleep(size, BPRI_LO))
		bzero(ptr, size);
	return ptr;
}

#endif	/**_KERNEL*/
