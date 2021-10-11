#ident	"@(#)stream.c	1.16	95/06/01 SMI"		/* SVr4.0 1.15.16.1 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

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

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/var.h>
#include <sys/proc.h>
#include <sys/fstyp.h>
#include <sys/fsid.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/elf.h>
#include <nlist.h>
#include <sys/poll.h>
#include <sys/stream.h>
#define	STREAMS_DEBUG
#include <sys/systm.h>		/* XXX for rval_t -- sigh */
#include <sys/strsubr.h>
#undef STREAMS_DEBUG
#define	_STRING_H		/* XXX strsubr defines strsignal */
#include <sys/stropts.h>
#include <sys/kmem.h>
#include <sys/kmem_impl.h>
#include "crash.h"

static struct nlist Qhead;
static struct nlist Kmem_flags;
extern struct syment *Vnode, *Streams;
static void prstream();
static void kmstream(void *kaddr, void *buf);
static void prmblk();
static void kmmblk(void *kaddr, void *buf);
static void prmblkuser(void *kaddr, void *buf,
			u_int size, kmem_bufctl_audit_t *bcp);
static void prlinkblk();
static void kmlinkblk(void *kaddr, void *buf);
static void prqueue();
static void kmqueue(void *kaddr, void *buf);
static void prstrstat();
static void prqrun();

static int strfull;
static char *strheading =
" ADDRESS      WRQ   STRTAB    VNODE  PUSHCNT  RERR/WERR FLAG\n";
static char *mblkheading =
" ADDRESS     NEXT     PREV     CONT     RPTR     WPTR   DATAP BAND/FLAG\n"
"            FRTNP     BASE      LIM  UIOBASE   UIOLIM  UIOPTR REF/TYPE/FLG\n";

/* get arguments for stream function */
int
getstream()
{
	int all = 0;
	int phys = 0;
	long addr = -1;
	int c;

	strfull = 0;
	optind = 1;
	while ((c = getopt(argcnt, args, "efpw:")) != EOF) {
		switch (c) {
			case 'e':
				all = 1;
				break;
			case 'f':
				strfull = 1;
				break;
			case 'w':
				redirect();
				break;
			case 'p':
				phys = 1;
				break;
			default:
				longjmp(syn, 0);
		}
	}
	if (!strfull)
		fprintf(fp, "%s", strheading);
	if (args[optind]) {
		all = 1;
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				error("\n");
			prstream(all, addr, phys);
		} while (args[optind]);
	} else {
		kmem_cache_apply(kmem_cache_find("stream_head_cache"),
			kmstream);
	}
	return (0);
}

/*ARGSUSED1*/
static void
kmstream(void *kaddr, void *buf)
{
	prstream(0, (int)kaddr, 0);
}

static void
prstream(int all, int addr, int phys)
{
	stdata_t st, *stp;
	struct strsig sigbuf, *signext;
	struct polldat *pdp;
	struct polldat pdat;
	int pollevents;
	int events;
	struct pid pid;

	readbuf((unsigned)addr, 0, phys, -1, (char *)&st, sizeof (st),
		"stream table slot");
	stp = (struct stdata *)&st;
	if (!stp->sd_wrq && !all)
		return;
	if (strfull)
		fprintf(fp, "%s", strheading);
	fprintf(fp, "%8lx %8lx %8lx %8lx %6d    %d/%d", addr,
			(long)stp->sd_wrq, (long)stp->sd_strtab,
			(long)stp->sd_vnode, stp->sd_pushcnt, stp->sd_rerror,
			stp->sd_werror);
	fprintf(fp,
		"      %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
		((stp->sd_flag & IOCWAIT) ? "iocw " : ""),
		((stp->sd_flag & RSLEEP) ? "rslp " : ""),
		((stp->sd_flag & WSLEEP) ? "wslp " : ""),
		((stp->sd_flag & STRPRI) ? "pri " : ""),
		((stp->sd_flag & STRHUP) ? "hup " : ""),
		((stp->sd_flag & STWOPEN) ? "stwo " : ""),
		((stp->sd_flag & STPLEX) ? "plex " : ""),
		((stp->sd_flag & STRISTTY) ? "istty " : ""),
		((stp->sd_flag & RMSGDIS) ? "mdis " : ""),
		((stp->sd_flag & RMSGNODIS) ? "mnds " : ""),
		((stp->sd_flag & STRDERR) ? "rerr " : ""),
		((stp->sd_flag & STWRERR) ? "werr " : ""),
		((stp->sd_flag & STRCLOSE) ? "clos " : ""),
		((stp->sd_flag & SNDMREAD) ? "mrd " : ""),
		((stp->sd_flag & OLDNDELAY) ? "ondel " : ""),
		((stp->sd_flag & SNDZERO) ? "sndz " : ""),
		((stp->sd_flag & STRTOSTOP) ? "tstp " : ""),
		((stp->sd_flag & RDPROTDAT) ? "pdat " : ""),
		((stp->sd_flag & RDPROTDIS) ? "pdis " : ""),
		((stp->sd_flag & STRMOUNT) ? "mnt " : ""),
		((stp->sd_flag & STRDELIM) ? "delim " : ""),
		((stp->sd_flag & STRSIGPIPE) ? "spip " : ""));

	if (strfull) {
		fprintf(fp, "\t     SID     PGID   IOCBLK    IOCID  IOCWAIT\n");
		if (stp->sd_sidp) {
			fprintf(fp, "\t%8d", readpid(stp->sd_sidp));
		} else
			fprintf(fp, "\t    -   ");
		if (stp->sd_pgidp) {
			fprintf(fp, "\t%8d", readpid(stp->sd_pgidp));
		} else
			fprintf(fp, "    -   ");
		fprintf(fp, " %8lx %8d \n",
			(long)stp->sd_iocblk, stp->sd_iocid);
		fprintf(fp, "\t  WOFF     MARK CLOSTIME\n");
		fprintf(fp, "\t%6d %8lx %8d\n", stp->sd_wroff,
			(long)stp->sd_mark, stp->sd_closetime);
		fprintf(fp, "\tSIGFLAGS:  %s%s%s%s%s%s%s%s%s\n",
			((stp->sd_sigflags & S_INPUT) ? " input" : ""),
			((stp->sd_sigflags & S_HIPRI) ? " hipri" : ""),
			((stp->sd_sigflags & S_OUTPUT) ? " output" : ""),
			((stp->sd_sigflags & S_RDNORM) ? " rdnorm" : ""),
			((stp->sd_sigflags & S_RDBAND) ? " rdband" : ""),
			((stp->sd_sigflags & S_WRBAND) ? " wrband" : ""),
			((stp->sd_sigflags & S_ERROR) ? " err" : ""),
			((stp->sd_sigflags & S_HANGUP) ? " hup" : ""),
			((stp->sd_sigflags & S_MSG) ? " msg" : ""));
		fprintf(fp, "\tSIGLIST:\n");
		signext = stp->sd_siglist;
		while (signext) {
			readmem((unsigned)signext, 1, -1, (char *)&sigbuf,
				sizeof (sigbuf), "stream event buffer");
			readmem((unsigned) sigbuf.ss_pidp, 1, -1, (char *) &pid,
				sizeof (pid), "stream event buffer pidp");
			events = sigbuf.ss_events;
			fprintf(fp, "\t\tPROC:  %3d   %s%s%s%s%s%s%s%s%s\n",
				pid.pid_prslot,
				((events & S_INPUT) ? " input" : ""),
				((events & S_HIPRI) ? " hipri" : ""),
				((events & S_OUTPUT) ? " output" : ""),
				((events & S_RDNORM) ? " rdnorm" : ""),
				((events & S_RDBAND) ? " rdband" : ""),
				((events & S_WRBAND) ? " wrband" : ""),
				((events & S_ERROR) ? " err" : ""),
				((events & S_HANGUP) ? " hup" : ""),
				((events & S_MSG) ? " msg" : ""));
			signext = sigbuf.ss_next;
		}
		fprintf(fp, "\tPOLLIST:\n");
		pdp = stp->sd_pollist.ph_list;
		pollevents = 0;
		while (pdp) {
			readmem((unsigned)pdp, 1, -1, (char *)&pdat,
				sizeof (pdat), "poll data buffer");
			fprintf(fp, "\t\tTHREAD:  %#.8lx   %s%s%s%s%s%s\n",
				(long)pdat.pd_thread,
				((pdat.pd_events & POLLIN) ? " in" : ""),
				((pdat.pd_events & POLLPRI) ? " pri" : ""),
				((pdat.pd_events & POLLOUT) ? " out" : ""),
				((pdat.pd_events & POLLRDNORM) ?
							" rdnorm" : ""),
				((pdat.pd_events & POLLRDBAND) ?
							" rdband" : ""),
				((pdat.pd_events & POLLWRBAND) ?
							" wrband" : ""));
			pollevents |= pdat.pd_events;
			pdp = pdat.pd_next;
		}
		fprintf(fp, "\tPOLLFLAGS:  %s%s%s%s%s%s\n",
			((pollevents & POLLIN) ? " in" : ""),
			((pollevents & POLLPRI) ? " pri" : ""),
			((pollevents & POLLOUT) ? " out" : ""),
			((pollevents & POLLRDNORM) ? " rdnorm" : ""),
			((pollevents & POLLRDBAND) ? " rdband" : ""),
			((pollevents & POLLWRBAND) ? " wrband" : ""));
		fprintf(fp, "\n");
	}
}

static int qfull;
static char *qheading =
	" QUEADDR     INFO     NEXT     LINK      PTR     RCNT FLAG\n";

/* get arguments for queue function */
int
getqueue()
{
	int all = 0;
	int phys = 0;
	long addr = -1;
	int c;
	qfull = 0;

	optind = 1;
	while ((c = getopt(argcnt, args, "efpw:")) != EOF) {
		switch (c) {
			case 'e':
				all = 1;
				break;
			case 'w':
				redirect();
				break;
			case 'p':
				phys = 1;
				break;
			case 'f':
				qfull = 1;
				break;
			default:
				longjmp(syn, 0);
		}
	}
	if (args[optind]) {
		all = 1;
		if (!qfull)
			fprintf(fp, qheading);
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				error("\n");
			prqueue(all, addr, NULL, phys);
		} while (args[optind]);
	} else {
		if (!qfull)
			fprintf(fp, qheading);
		kmem_cache_apply(kmem_cache_find("queue_cache"), kmqueue);
	}
	return (0);
}

static void
kmqueue(void *kaddr, void *buf)
{
	queinfo_t *kqip = kaddr;
	queinfo_t *qip = buf;
	prqueue(0, &kqip->qu_rqueue, &qip->qu_rqueue, 0);
	prqueue(0, &kqip->qu_wqueue, &qip->qu_wqueue, 0);
}

/* print queue table */
static void
prqueue(int all, void *qaddr, void *uqaddr, int phys)
{
	queue_t q, *qp;

	if (uqaddr) {
		qp = uqaddr;
	} else {
		readmem((unsigned)qaddr, !phys, -1, (char *)&q,
			sizeof (q), "queue");
		qp = &q;
	}
	if (!(qp->q_flag & QUSE) && !all)
		return;

	if (qfull)
		fprintf(fp, qheading);
	fprintf(fp, "%8lx ", qaddr);
	fprintf(fp, "%8lx ", (long)qp->q_qinfo);
	if (qp->q_next)
		fprintf(fp, "%8lx ", (long)qp->q_next);
	else
		fprintf(fp, "       - ");

	if (qp->q_link)
		fprintf(fp, "%8lx ", (long)qp->q_link);
	else
		fprintf(fp, "       - ");

	fprintf(fp, "%8lx", (long)qp->q_ptr);
	fprintf(fp, " %8lu ", qp->q_count);
	fprintf(fp, "%s%s%s%s%s%s%s%s\n",
		((qp->q_flag & QENAB) ? "en " : ""),
		((qp->q_flag & QWANTR) ? "wr " : ""),
		((qp->q_flag & QWANTW) ? "ww " : ""),
		((qp->q_flag & QFULL) ? "fl " : ""),
		((qp->q_flag & QREADR) ? "rr " : ""),
		((qp->q_flag & QUSE) ? "us " : ""),
		((qp->q_flag & QNOENB) ? "ne " : ""),
		((qp->q_flag & QOLD) ? "ol " : ""));
	if (!qfull)
		return;
	fprintf(fp, "\t    HEAD     TAIL     MINP     MAXP     ");
	fprintf(fp, "HIWT     LOWT BAND BANDADDR\n");
	if (qp->q_first)
		fprintf(fp, "\t%8lx ", (long)qp->q_first);
	else
		fprintf(fp, "\t       - ");
	if (qp->q_last)
		fprintf(fp, "%8lx ", (long)qp->q_last);
	else
		fprintf(fp, "       - ");
	fprintf(fp, "%8ld %8ld %8lu %8lu ",
		qp->q_minpsz,
		qp->q_maxpsz,
		qp->q_hiwat,
		qp->q_lowat);
	fprintf(fp, " %3d %8lx\n\n",
		qp->q_nband, (long)qp->q_bandp);
}

/* get arguments for mblk function */
int
getmblk()
{
	int all = 0;
	int phys = 0;
	long addr = -1;
	int c;

	strfull = 0;
	optind = 1;
	while ((c = getopt(argcnt, args, "efpw:")) != EOF) {
		switch (c) {
			case 'e':
				all = 1;
				break;
			case 'f':
				strfull = 1;
				break;
			case 'w':
				redirect();
				break;
			case 'p':
				phys = 1;
				break;
			default:
				longjmp(syn, 0);
		}
	}
	if (!strfull)
		fprintf(fp, "%s", mblkheading);
	if (args[optind]) {
		all = 1;
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				error("\n");
			prmblk(all, addr, phys);
		} while (args[optind]);
	} else {
		kmem_cache_t *msg_cache[100];
		int ncaches, i;

		ncaches = kmem_cache_find_all("streams_msg", msg_cache, 100);
		for (i = 0; i < ncaches; i++)
			kmem_cache_apply(msg_cache[i], kmmblk);
	}
	return (0);
}

/*ARGSUSED1*/
static void
kmmblk(void *kaddr, void *buf)
{
	prmblk(0, (int)kaddr, 0);
}

/* ARGSUSED */
static void
prmblk(int all, int addr, int phys)
{
	MH mh, *mhp;
	mblk_t *mp;
	dblk_t *dp;

	readbuf((unsigned)addr, 0, phys, -1, (char *)&mh, sizeof (mh),
		"mblk");
	mhp = (MH *)&mh;
	mp = &mhp->mh_mblk;
	dp = &mhp->mh_dblk;
	if (strfull)
		fprintf(fp, "%s", mblkheading);
	fprintf(fp, "%8lx %8lx %8lx %8lx %8lx %8lx %8lx %4d/%x\n", addr,
			(long)mp->b_next, (long)mp->b_prev,
			(long)mp->b_cont, (long)mp->b_rptr, (long)mp->b_wptr,
			(long)mp->b_datap, mp->b_band, mp->b_flag);
	fprintf(fp, "\t %8lx %8lx %8lx %8lx %8lx %8lx %4d/%x/%x\n",
			(long)dp->db_frtnp, (long)dp->db_base,
			(long)dp->db_lim,
			(long)dp->db_struiobase,
			(long)dp->db_struiolim, (long)dp->db_struioptr,
			dp->db_ref, dp->db_type,
			dp->db_struioflag);
}


/*
 * Print mblk/dblk usage with stack traces when KMF_AUDIT is enabled
 */
int
getmblkusers()
{
	int c;
	u_long kmem_flags;
	kmem_cache_t *msg_cache[100];
	int ncaches, i;
	int mem_threshold = 1024;	/* Minimum # bytes for printing */
	int cnt_threshold = 10;		/* Minimum # blocks for printing */

	if (nl_getsym("kmem_flags", &Kmem_flags))
		error("kmem_flags not found in symbol table\n");
	readmem((unsigned)Kmem_flags.n_value, 1, -1, (char *)&kmem_flags,
		sizeof (kmem_flags), "kmem_flags");
	if (!(kmem_flags & KMF_AUDIT))
		error("mblkusers requires that KMF_AUDIT be turned on "
			"in kmem_flags");

	strfull = 0;
	optind = 1;
	while ((c = getopt(argcnt, args, "efw:")) != EOF) {
		switch (c) {
			case 'e':
				mem_threshold = 0;
				cnt_threshold = 0;
				break;
			case 'f':
				strfull = 1;
				break;
			case 'w':
				redirect();
				break;
			default:
				longjmp(syn, 0);
		}
	}

	init_owner();
	ncaches = kmem_cache_find_all("streams_msg", msg_cache, 100);
	for (i = 0; i < ncaches; i++)
		kmem_cache_audit_apply(msg_cache[i], prmblkuser);

	print_owner("messages", mem_threshold, cnt_threshold);
	return (0);
}

/* ARGSUSED */
static void
prmblkuser(void *kaddr, void *buf, u_int size, kmem_bufctl_audit_t *bcp)
{
	MH mh, *mhp;
	mblk_t *mp;
	dblk_t *dp;
	int i;

	readbuf((unsigned)kaddr, 0, 0, -1, (char *)&mh, sizeof (mh),
		"mblk");
	mhp = (MH *)&mh;
	mp = &mhp->mh_mblk;
	dp = &mhp->mh_dblk;
	if (strfull) {
		fprintf(fp, "%s", mblkheading);
		fprintf(fp, "%8lx %8lx %8lx %8lx %8lx %8lx %8lx %4d/%x\n",
			(long)kaddr,
			(long)mp->b_next, (long)mp->b_prev,
			(long)mp->b_cont, (long)mp->b_rptr, (long)mp->b_wptr,
			(long)mp->b_datap, mp->b_band, mp->b_flag);
		fprintf(fp, "\t %8lx %8lx %8lx %8lx %8lx %8lx %4d/%x/%x\n",
			(long)dp->db_frtnp, (long)dp->db_base,
			(long)dp->db_lim,
			(long)dp->db_struiobase,
			(long)dp->db_struiolim, (long)dp->db_struioptr,
			dp->db_ref, dp->db_type,
			dp->db_struioflag);
		fprintf(fp, "\t size %d, stack depth %d\n",
			size - sizeof (MH), bcp->bc_depth);
		for (i = 0; i < bcp->bc_depth; i++) {
			fprintf(fp, "\t ");
			prsymbol(NULL, bcp->bc_stack[i]);
		}
	}
	add_owner(bcp, size, size - sizeof (MH));
}

/* get arguments for qrun function */
int
getqrun()
{
	int c;

	if (nl_getsym("qhead", &Qhead))
		error("qhead not found in symbol table\n");
	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w':
				redirect();
				break;
			default:
				longjmp(syn, 0);
		}
	}
	prqrun();
	return (0);
}

/* print qrun information */
static void
prqrun()
{
	queue_t que, *q;

	readmem((unsigned)Qhead.n_value, 1, -1, (char *)&q,
		sizeof (q), "qhead");
	fprintf(fp, "Queue slots scheduled for service: ");
	if (!q)
		fprintf(fp, "\n  NONE");
	while (q) {
		fprintf(fp, "%8lx ", (long)q);
		readmem((unsigned)q, 1, -1, (char *)&que,
			sizeof (que), "scanning queue list");
		q = que.q_link;
	}
	fprintf(fp, "\n");
}

/* get arguments for strstat function */
int
getstrstat()
{
	int c;

	if (nl_getsym("qhead", &Qhead))
		error("qhead not found in symbol table\n");
	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w':
				redirect();
				break;
			default:
				longjmp(syn, 0);
		}
	}
	prstrstat();
	return (0);
}

static void
showstrstat(kmem_cache_t *cp)
{
	kmem_cache_stat_t kcs;
	kmem_cache_t c;

	kmem_cache_getstats(cp, &kcs);
	if (kvm_read(kd, (u_long)cp, (char *)&c, sizeof (c)) == -1)
		return;

	fprintf(fp, "%-21s %7d %7d %7d %10d %7d\n",
		c.cache_name,
		kcs.kcs_buf_total - kcs.kcs_buf_avail,
		kcs.kcs_buf_avail,
		kcs.kcs_buf_max,
		kcs.kcs_alloc,
		kcs.kcs_alloc_fail);
}

/* print stream statistics */
static void
prstrstat()
{
	queue_t q, *qp;
	int qruncnt = 0;
	int i, ncaches;
	kmem_cache_t *msg_cache[100];

	fprintf(fp, "ITEM                    ALLOC    FREE     MAX");
	fprintf(fp, "      TOTAL    FAIL\n");

	ncaches = kmem_cache_find_all("streams_msg", msg_cache, 100);
	for (i = 0; i < ncaches; i++)
		showstrstat(msg_cache[i]);

	showstrstat(kmem_cache_find("stream_head_cache"));
	showstrstat(kmem_cache_find("queue_cache"));
	showstrstat(kmem_cache_find("syncq_cache"));
	showstrstat(kmem_cache_find("linkinfo_cache"));
	showstrstat(kmem_cache_find("strevent_cache"));
	showstrstat(kmem_cache_find("qband_cache"));

	readmem((unsigned)Qhead.n_value, 1, -1, (char *)&qp,
		sizeof (qp), "qhead");
	while (qp) {
		qruncnt++;
		readmem((unsigned)qp, 1, -1, (char *)&q, sizeof (q),
			"queue run list");
		qp = q.q_link;
	}
	fprintf(fp, "\nCount of scheduled queues:%4d\n", qruncnt);
}

/* get arguments for linkblk function */
int
getlinkblk()
{
	int all = 0;
	long addr = -1;
	int c;

	optind = 1;
	while ((c = getopt(argcnt, args, "epw:")) != EOF) {
		switch (c) {
			case 'e':
				all = 1;
				break;
			case 'w':
				redirect();
				break;
			case 'p':
				/* do nothing */
				break;
			default:
				longjmp(syn, 0);
		}
	}
	fprintf(fp, "LBLKADDR     QTOP     QBOT FILEADDR    MUXID\n");
	if (args[optind]) {
		all = 1;
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				error("\n");
			prlinkblk(all, addr);
		} while (args[optind]);
	} else {
		kmem_cache_apply(kmem_cache_find("linkinfo_cache"), kmlinkblk);
	}
	return (0);
}

/* ARGSUSED1 */
static void
kmlinkblk(void *kaddr, void *buf)
{
	prlinkblk(0, kaddr);
}

/* print linkblk table */
static void
prlinkblk(int all, long addr)
{
	struct linkinfo linkbuf;
	struct linkinfo *lp;

	readmem((unsigned)addr, 1, -1, (char *)&linkbuf, sizeof (linkbuf),
		"linkblk table");
	lp = &linkbuf;
	if (!lp->li_lblk.l_qbot && !all)
		return;
	fprintf(fp, "%8lx", addr);
	fprintf(fp, " %8lx", (long)lp->li_lblk.l_qtop);
	fprintf(fp, " %8lx", (long)lp->li_lblk.l_qbot);
	fprintf(fp, " %8lx", (long)lp->li_fpdown);
	if (lp->li_lblk.l_qbot)
		fprintf(fp, " %8d\n", lp->li_lblk.l_index);
	else
		fprintf(fp, "        -\n");
}
