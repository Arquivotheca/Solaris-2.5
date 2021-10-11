/*
 * DKI/DDI MT synchronization primitives.
 */

#ifndef _SYS_DKI_LKINFO_H
#define	_SYS_DKI_LKINFO_H

#pragma ident	"@(#)dki_lkinfo.h	1.8	93/05/03 SMI"

#include <sys/types.h>
#include <sys/dl.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * DKI/DDI-specified data types for lkinfo and statistics.
 * The statistics fields are maintained by some types of the mutex primitives.
 */
typedef struct lkinfo {
	char	*lk_name;	/* lock name */
	int	lk_flags;	/* flags */
	long	lk_pad[2];	/* reserved for future use */
} lkinfo_t;

typedef struct _lkstat_t {
	lkinfo_t	*ls_infop;	/* lock information for debugging */
	ulong_t		ls_wrcnt;	/* #times writelocked (exclusive) */
	ulong_t		ls_rdcnt;	/* #times readlocked (shared-RW only) */
	ulong_t		ls_solordcnt;	/* #times rdlcked w/out other readers */
	ulong_t		ls_fail;	/* # failed attempts to acquire lock */
	/* this union will be problem if sizeof (lkstat_t *) > sizeof(dl_t) */
	union {
		dl_t lsu_time;		/* start time when acquired */
		struct _lkstat_t *lsu_next; /* for free list */
	} un;
#define	ls_stime 	un.lsu_time
	dl_t		ls_wtime;	/* cumulative wait time */
	dl_t		ls_htime;	/* cumulative hold time */
} lkstat_t;

typedef struct lkstat_sum {
	lkstat_t	*sp;
	struct lkstat_sum *next;
} lkstat_sum_t;

/*
 * lk_flags values.
 */
#define	NOSTATS	1		/* don't keep statistics on this lock */

/* total size of lksblk_t should be a little less than pagesize */
#define	LSB_NLKDS	91

typedef struct lksblk {
	struct lksblk *lsb_prev, *lsb_next;
	int lsb_nfree;				/* num free lkstat_t's in blk */
	lkstat_t *lsb_free;			/* head of free list */
	lkstat_t lsb_bufs[LSB_NLKDS];		/* block of bufs */
} lksblk_t;

#ifdef __STDC__
extern lkstat_t *lkstat_alloc(lkinfo_t *, int);
extern void	lkstat_free(lkstat_t *, int);
extern void	lkstat_sum_on_destroy(lkstat_t *);
extern void *	startup_alloc(size_t, void **);
extern void	startup_free(void *, size_t, void **);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DKI_LKINFO_H */
