/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident	"@(#)svvs_confcommon.c	1.2	92/07/14 SMI"

#include <sys/param.h>
#include <sys/stream.h>
#include <sys/lo.h>
#include <sys/tidg.h>
#include <sys/tivc.h>
#include <sys/tmux.h>
#include <sys/stropts.h>

#ifdef SVVS

int	tivc_cnt = 12;
int	tidg_cnt = 12;
int	tmxcnt = 4;
int	tmxlcnt = 3;
int	locnt = 4;

struct lo lo_lo[4];
struct ti_tivc ti_tivc[12];
struct ti_tidg ti_tidg[12];
struct tmx tmx_tmx[4];
struct tmxl tmx_low[3];

/*
 * This routine is in here because it is common to the
 * modules tivc.kmod and tidg.kmod. It could be made into
 * a stub but then we would have to load the tivc.kmod
 * module if tidg referenced the routine. This is not
 * optimal dynamic performance.
 */

snd_flushrw(q)
queue_t *q;
{
	mblk_t *mp;

	if ((mp = allocb(1, BPRI_HI)) == NULL)
		return (0);
	mp->b_wptr++;
	mp->b_datap->db_type = M_FLUSH;
	*mp->b_rptr = FLUSHRW;
	if (q->q_flag&QREADR) {
		putnext(q, mp);
	} else
		qreply(q, mp);
	return (1);
}


#endif /* SVVS */
