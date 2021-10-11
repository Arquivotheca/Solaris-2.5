/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)kmem.c	1.25	95/10/11 SMI"

/*
 * Kernel memory allocator, as described in:
 *
 * Jeff Bonwick,
 * The Slab Allocator: An Object-Caching Kernel Memory Allocator.
 * Proceedings of the Summer 1994 Usenix Conference.
 *
 * See /shared/sac/PSARC/1994/028 for copies of the paper and
 * related design documentation.
 */

#include <sys/kmem.h>
#include <sys/kmem_impl.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/vm.h>
#include <sys/proc.h>
#include <sys/tuneable.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/mutex.h>
#include <sys/bitmap.h>
#include <sys/vtrace.h>
#include <sys/kobj.h>
#include <vm/page.h>
#include <vm/seg_kmem.h>
#include <sys/map.h>

extern void prom_printf(char *fmt, ...);
extern char Syslimit[];
extern char Sysbase[];

struct kmem_cpu_kstat {
	kstat_named_t	kmcpu_alloc_from;
	kstat_named_t	kmcpu_free_to;
	kstat_named_t	kmcpu_buf_avail;
} kmem_cpu_kstat_template = {
	{ "alloc_from_cpu%d",		KSTAT_DATA_LONG },
	{ "free_to_cpu%d",		KSTAT_DATA_LONG },
	{ "buf_avail_cpu%d",		KSTAT_DATA_LONG },
};

struct kmem_cpu_kstat *kmem_cpu_kstat;

struct kmem_cache_kstat {
	kstat_named_t	kmc_buf_size;
	kstat_named_t	kmc_align;
	kstat_named_t	kmc_chunk_size;
	kstat_named_t	kmc_slab_size;
	kstat_named_t	kmc_alloc;
	kstat_named_t	kmc_alloc_fail;
	kstat_named_t	kmc_depot_alloc;
	kstat_named_t	kmc_depot_free;
	kstat_named_t	kmc_depot_contention;
	kstat_named_t	kmc_global_alloc;
	kstat_named_t	kmc_buf_avail;
	kstat_named_t	kmc_buf_total;
	kstat_named_t	kmc_buf_max;
	kstat_named_t	kmc_slab_create;
	kstat_named_t	kmc_slab_destroy;
	kstat_named_t	kmc_hash_size;
	kstat_named_t	kmc_hash_lookup_depth;
	kstat_named_t	kmc_hash_rescale;
	kstat_named_t	kmc_full_magazines;
	kstat_named_t	kmc_empty_magazines;
	kstat_named_t	kmc_magazine_size;
} kmem_cache_kstat_template = {
	{ "buf_size",		KSTAT_DATA_LONG },
	{ "align",		KSTAT_DATA_LONG },
	{ "chunk_size",		KSTAT_DATA_LONG },
	{ "slab_size",		KSTAT_DATA_LONG },
	{ "alloc",		KSTAT_DATA_LONG },
	{ "alloc_fail",		KSTAT_DATA_LONG },
	{ "depot_alloc",	KSTAT_DATA_LONG },
	{ "depot_free",		KSTAT_DATA_LONG },
	{ "depot_contention",	KSTAT_DATA_LONG },
	{ "global_alloc",	KSTAT_DATA_LONG },
	{ "buf_avail",		KSTAT_DATA_LONG },
	{ "buf_total",		KSTAT_DATA_LONG },
	{ "buf_max",		KSTAT_DATA_LONG },
	{ "slab_create",	KSTAT_DATA_LONG },
	{ "slab_destroy",	KSTAT_DATA_LONG },
	{ "hash_size",		KSTAT_DATA_LONG },
	{ "hash_lookup_depth",	KSTAT_DATA_LONG },
	{ "hash_rescale",	KSTAT_DATA_LONG },
	{ "full_magazines",	KSTAT_DATA_LONG },
	{ "empty_magazines",	KSTAT_DATA_LONG },
	{ "magazine_size",	KSTAT_DATA_LONG },
};

struct kmem_cache_kstat *kmem_cache_kstat;

struct {
	kstat_named_t	arena_size;
	kstat_named_t	huge_alloc;
	kstat_named_t	huge_alloc_fail;
	kstat_named_t	perm_size;
	kstat_named_t	perm_alloc;
	kstat_named_t	perm_alloc_fail;
} kmem_misc_kstat = {
	{ "arena_size",		KSTAT_DATA_LONG },
	{ "huge_alloc",		KSTAT_DATA_LONG },
	{ "huge_alloc_fail",	KSTAT_DATA_LONG },
	{ "perm_size",		KSTAT_DATA_LONG },
	{ "perm_alloc",		KSTAT_DATA_LONG },
	{ "perm_alloc_fail",	KSTAT_DATA_LONG },
};

/*
 * The default set of caches to back kmem_alloc().
 * These sizes should be reevaluated periodically.
 */
static int kmem_alloc_sizes[] = {
	8,
	16,	24,
	32,	40,	48,	56,
	64,	80,	96,	112,
	128,	144,	160,	176,	192,	208,	224,	240,
	256,	320,	384,	448,
	512,	576,	672,	800,
	1024,	1152,	1344,	1632,
	2048,	2720,
	4096,	5440,	6144,	6816,
	8192,	9536,	12288,
	16384
};

#define	KMEM_MAXBUF	16384

static kmem_cache_t *kmem_alloc_table[KMEM_MAXBUF >> KMEM_ALIGN_SHIFT];

/*
 * The magazine types for fast per-cpu allocation
 */
typedef struct kmem_magazine_type {
	int		mt_magsize;	/* magazine size (number of rounds) */
	int		mt_align;	/* magazine alignment */
	int		mt_minbuf;	/* all smaller buffers qualify */
	int		mt_maxbuf;	/* no larger buffers qualify */
	kmem_cache_t	*mt_cache;
} kmem_magazine_type_t;

kmem_magazine_type_t kmem_magazine_type[] = {
	{ 0,	0,	16384,	16384	},
	{ 1,	8,	3200,	8192	},
	{ 3,	16,	256,	4096	},
	{ 7,	32,	64,	2048	},
	{ 15,	64,	0,	1024	},
	{ 31,	64,	0,	512	},
	{ 47,	64,	0,	256	},
	{ 63,	64,	0,	128	},
	{ 95,	64,	0,	64	},
	{ 143,	64,	0,	0	},
};

static int kmem_reap_lasttime;	/* time of last reap */
int kmem_reap_interval;		/* don't allow more frequent reaping */
int kmem_depot_contention;	/* max failed depot tryenters/minute */
int kmem_reapahead = 0;		/* diff of kmem_reap and pageout thresholds */
int kmem_maxraw = -1;
int kmem_align = KMEM_ALIGN;	/* minimum alignment for all caches */
int kmem_panic = 1;		/* by default, panic on error */
int kmem_logging = 1;

#ifdef DEBUG
int kmem_flags = KMF_AUDIT | KMF_DEADBEEF | KMF_REDZONE | KMF_VERIFY;
int kmem_fast_debug = 1;
#else
int kmem_flags = 0;
int kmem_fast_debug = 0;
#endif
int kmem_ready;

static kmem_cache_t	*kmem_slab_cache;
static kmem_cache_t	*kmem_bufctl_cache;

static kmutex_t		kmem_cache_lock;	/* inter-cache linkage only */
static kmem_cache_t	*kmem_cache_freelist;
kmem_cache_t		kmem_null_cache;
static kmem_bufctl_t	kmem_null_bufctl;

static kmutex_t		kmem_async_lock;
static kmem_async_t	*kmem_async_freelist;
static kmem_async_t	kmem_async_queue;
static kcondvar_t	kmem_async_cv;
static kmutex_t		kmem_async_serialize;

static kmutex_t		kmem_perm_lock;
static kmem_perm_t	*kmem_perm_freelist;

kmem_bufctl_audit_t	*kmem_log;	/* for buffer auditing, if enabled */
int			kmem_log_size;
int			kmem_log_count;
int			kmem_log_display = 2;	/* # to show in kmem_error */

#define	KMEM_BZERO_INLINE	48	/* do inline bzero for smaller bufs */
#define	KMEM_BZERO(bufarg, sizearg)					\
	{								\
		u_int Xsize = (u_int)(sizearg);				\
		u_longlong_t *Xbuf = (u_longlong_t *)(bufarg);		\
		if (Xsize > KMEM_BZERO_INLINE) {			\
			bzero((void *)Xbuf, Xsize);			\
		} else {						\
			u_longlong_t *Xend;				\
			Xend = (u_longlong_t *)((char *)Xbuf + Xsize); 	\
			while (Xbuf < Xend)				\
				*Xbuf++ = 0LL;				\
		}							\
	}

#define	KMERR_MODIFIED	0	/* buffer modified while on freelist */
#define	KMERR_REDZONE	1	/* redzone violation (write past end of buf) */
#define	KMERR_BADADDR	2	/* freed a bad (unallocated) address */
#define	KMERR_DUPFREE	3	/* freed a buffer twice */
#define	KMERR_BADBUFTAG	4	/* buftag corrupted */
#define	KMERR_BADBUFCTL	5	/* bufctl corrupted */
#define	KMERR_BADCACHE	6	/* freed a buffer to the wrong cache */

struct {
	hrtime_t	kmp_timestamp;	/* timestamp of panic */
	int		kmp_error;	/* type of kmem error */
	int		kmp_count;	/* index into kmem_log at panic */
	kmem_cache_t	*kmp_cache;	/* buffer's cache */
	void		*kmp_buffer;	/* buffer that induced panic */
	kmem_bufctl_t	*kmp_bufctl;	/* buffer's bufctl */
	kmem_bufctl_t	*kmp_realbcp;	/* bufctl according to kmem_locate() */
} kmem_panic_info;

static void
copy_pattern(u_long pattern, void *buf_arg, int size)
{
	u_long *bufend = (u_long *)((char *)buf_arg + size);
	u_long *buf;

	for (buf = buf_arg; buf < bufend; buf++)
		*buf = pattern;
}

static void *
verify_pattern(u_long pattern, void *buf_arg, int size)
{
	u_long *bufend = (u_long *)((char *)buf_arg + size);
	u_long *buf;

	for (buf = buf_arg; buf < bufend; buf++)
		if (*buf != pattern)
			return (buf);
	return (NULL);
}

/*
 * Debugging support.  Given any buffer address, find its bufctl
 * by searching every cache in the system.
 */
static kmem_bufctl_t *
kmem_locate(void *buf)
{
	kmem_cache_t *cp;
	kmem_bufctl_t *bcp = NULL;

	mutex_enter(&kmem_cache_lock);
	for (cp = kmem_null_cache.cache_next; cp != &kmem_null_cache;
	    cp = cp->cache_next) {
		if (!(cp->cache_flags & KMF_HASH))
			continue;
		mutex_enter(&cp->cache_lock);
		for (bcp = *KMEM_HASH(cp, buf); bcp != NULL; bcp = bcp->bc_next)
			if (bcp->bc_addr == buf)
				break;
		mutex_exit(&cp->cache_lock);
		if (bcp != NULL)
			break;
	}
	mutex_exit(&kmem_cache_lock);
	return (bcp);
}

static void
kmem_bufctl_display(kmem_bufctl_audit_t *bcp)
{
	int d;
	timestruc_t ts;

	hrt2ts(kmem_panic_info.kmp_timestamp - bcp->bc_timestamp, &ts);
	prom_printf("\nthread=%x  time=T-%d.%9d  slab=%x  cache: %s\n",
		bcp->bc_thread, ts.tv_sec, ts.tv_nsec,
		bcp->bc_slab, bcp->bc_cache->cache_name);
	for (d = 0; d < bcp->bc_depth; d++) {
		u_int off;
		char *sym = kobj_getsymname(bcp->bc_stack[d], &off);
		prom_printf("%s+%x\n", sym ? sym : "?", off);
	}
}

static void
kmem_error(int error, kmem_cache_t *cp, void *buf, kmem_bufctl_t *bcp)
{
	kmem_bufctl_audit_t *logp, logcopy;
	kmem_buftag_t *btp = KMEM_BUFTAG(cp, buf);
	u_long *off;

	kmem_logging = 0;	/* stop logging when a bad thing happens */

	kmem_panic_info.kmp_timestamp = gethrtime();
	kmem_panic_info.kmp_error = error;
	kmem_panic_info.kmp_count = kmem_log_count;
	kmem_panic_info.kmp_cache = cp;
	kmem_panic_info.kmp_buffer = buf;
	kmem_panic_info.kmp_bufctl = bcp;

	prom_printf("kernel memory allocator: ");

	switch (error) {

	    case KMERR_MODIFIED:

		prom_printf("buffer modified after being freed\n");
		off = verify_pattern(0xdeadbeef, buf,
			cp ? cp->cache_offset : ((int *)buf)[-1]);
		if (off == NULL)	/* shouldn't happen */
			off = buf;
		prom_printf("modification occurred at offset 0x%x "
			"(0x%x replaced by 0x%x)\n",
			(int)off - (int)buf, 0xdeadbeef, *off);
		break;

	    case KMERR_REDZONE:

		prom_printf("redzone violation: write past end of buffer\n");
		break;

	    case KMERR_BADADDR:

		bcp = kmem_locate(buf);
		if (bcp && bcp->bc_slab->slab_cache != cp) {
			kmem_panic_info.kmp_error = KMERR_BADCACHE;
			prom_printf("buffer freed to wrong cache\n");
			prom_printf("buffer was allocated from cache %s,\n",
				bcp->bc_slab->slab_cache->cache_name);
			prom_printf("caller attempting free to cache %s.\n",
				cp->cache_name);
		} else {
			prom_printf("invalid free: buffer not in cache\n");
		}
		break;

	    case KMERR_DUPFREE:

		prom_printf("duplicate free: buffer freed twice\n");
		break;

	    case KMERR_BADBUFTAG:

		prom_printf("boundary tag corrupted\n");
		prom_printf("bcp ^ bxstat = %x, should be %x\n",
			(int)bcp ^ btp->bt_bxstat, KMEM_BUFTAG_FREE);
		bcp = kmem_locate(buf);
		break;

	    case KMERR_BADBUFCTL:

		prom_printf("bufctl corrupted\n");
		prom_printf("bcp->bc_addr = %x, should be %x\n",
			bcp->bc_addr, buf);
		bcp = kmem_locate(buf);
		break;

	}

	kmem_panic_info.kmp_realbcp = bcp;

	prom_printf("buffer=%x  bufctl=%x  cache: %s\n",
		buf, bcp, cp ? cp->cache_name : "none");

	if (cp && (cp->cache_flags & KMF_AUDIT)) {
		int i;
		int count = 0;
		prom_printf("previous transactions on buffer %x:\n", buf);
		for (i = kmem_log_count + kmem_log_size - 1;
		    i > kmem_log_count; i--) {
			logp = &kmem_log[i & (kmem_log_size - 1)];
			mutex_enter(&kmem_cache_lock);
			if (logp->bc_addr != buf) {
				mutex_exit(&kmem_cache_lock);
				continue;
			}
			logcopy = *logp;
			mutex_exit(&kmem_cache_lock);
			logp = &logcopy;
			kmem_bufctl_display(logp);
			if (++count >= kmem_log_display) {
				if (logp->bc_lastlog)
					prom_printf("\n(transaction log "
					    "continues at %x)\n",
						logp->bc_lastlog);
				break;
			}
		}
		/*
		 * If we didn't find any history in the log (because
		 * it wrapped since the last transaction on this buffer),
		 * we can still get the last transaction from the bufctl.
		 */
		if (count == 0 && kmem_log_display > 0) {
			logp = (kmem_bufctl_audit_t *)kmem_locate(buf);
			if (logp != NULL)
				kmem_bufctl_display(logp);
		}
	}
	if (kmem_panic)
		cmn_err(CE_PANIC, "kernel heap corruption detected");
	debug_enter(NULL);
	kmem_logging = 1;	/* resume logging */
}

/*
 * Get pages from the VM system and update all the bean counters.
 */
static void *
kmem_page_alloc(int npages, int flags)
{
	void *pages;

	mutex_enter(&freemem_lock);
	while ((availrmem - npages < tune.t_minarmem) ||
	    (availsmem - npages < tune.t_minasmem)) {
		mutex_exit(&freemem_lock);
		if (flags & KM_NOSLEEP)
			return (NULL);
		/*
		 * We're out of memory.  It would be nice if there
		 * were something appropriate to cv_wait() for,
		 * but there are currently many ways for pages to
		 * come and go -- there's no reliable, centralized
		 * notification mechanism.  So, we just hang out
		 * for a moment, give pageout a chance to run,
		 * and try again.  It's lame, but this situation is
		 * rare in practice -- all we're really trying to do
		 * here is unwedge the system if it gets stuck.
		 */
		kmem_reap();
		delay(HZ >> 2);
		mutex_enter(&freemem_lock);
	}
	availrmem -= npages;
	availsmem -= npages;
	pages_pp_kernel += npages;
	kmem_misc_kstat.arena_size.value.l += ptob(npages);
	mutex_exit(&freemem_lock);

	if ((pages = kmem_getpages(npages, KM_NOSLEEP)) == NULL) {
		/*
		 * We couldn't get pages with KM_NOSLEEP.  This might be
		 * because we're out of physical memory, but it could also
		 * be because we're out of kernelmap.  In the latter case,
		 * kmem_reap() is the only potential salvation.
		 */
		if (flags & KM_NOSLEEP) {
			mutex_enter(&freemem_lock);
			availrmem += npages;
			availsmem += npages;
			pages_pp_kernel -= npages;
			kmem_misc_kstat.arena_size.value.l -= ptob(npages);
			mutex_exit(&freemem_lock);
		} else {
			kmem_reap();
			pages = kmem_getpages(npages, KM_SLEEP);
			ASSERT(pages != NULL);
		}
	}

	return (pages);
}

/*
 * Give pages back to the VM system and update all the bean counters.
 */
static void
kmem_page_free(void *pages, int npages)
{
	kmem_freepages(pages, npages);
	mutex_enter(&freemem_lock);
	availrmem += npages;
	availsmem += npages;
	pages_pp_kernel -= npages;
	kmem_misc_kstat.arena_size.value.l -= ptob(npages);
	mutex_exit(&freemem_lock);
}

/*
 * XXX -- This interface can't express the availability of more than 4GB of
 * kernel memory, so just truncate it to 1GB if there's more.  (Truncation to
 * 1GB rather than 2GB or 4GB protects the caller from arithmetic overflow.)
 * kmem_avail() is used to make policy decisions when memory is low, not when
 * memory is plentiful, so truncation hardly matters.  Convert this to return
 * u_longlong_t in 2.6.
 */
u_long
kmem_avail(void)
{
	int rmem = (int)(availrmem - tune.t_minarmem);
	int fmem = (int)(freemem - minfree);
	int pages_avail = min(max(min(rmem, fmem), 0), 1 << (30 - PAGESHIFT));

	return (ptob(pages_avail));
}

/*
 * Return the maximum amount of memory that is (in theory) allocatable
 * from the heap. This may be used as an estimate only since there
 * is no guarentee this space will still be available when an allocation
 * request is made, nor that the space may be allocated in one big request
 * due to kernelmap fragmentation.
 */
u_longlong_t
kmem_maxavail(void)
{
	int max_phys = (int)(availrmem - tune.t_minarmem);
	int max_virt = (int)kmem_maxvirt();
	int pages_avail = max(min(max_phys, max_virt), 0);

	return ((u_longlong_t)pages_avail << PAGESHIFT);
}

/*
 * Allocate memory permanently.
 */
void *
kmem_perm_alloc(size_t size, int align, int flags)
{
	kmem_perm_t *pp, **prev_ppp, **best_prev_ppp;
	char *buf;
	int best_avail = INT_MAX;

	if (align < KMEM_ALIGN)
		align = KMEM_ALIGN;
	if ((align & (align - 1)) || align > PAGESIZE)
		cmn_err(CE_PANIC, "kmem_perm_alloc: bad alignment %d", align);
	size = (size + align - 1) & -align;

	mutex_enter(&kmem_perm_lock);
	kmem_misc_kstat.perm_alloc.value.l++;
	best_prev_ppp = NULL;
	for (prev_ppp = &kmem_perm_freelist; (pp = *prev_ppp) != NULL;
	    prev_ppp = &pp->perm_next) {
		if (pp->perm_avail - (-(int)pp->perm_current & (align - 1)) >=
		    size && pp->perm_avail < best_avail) {
			best_prev_ppp = prev_ppp;
			best_avail = pp->perm_avail;
		}
	}
	if ((prev_ppp = best_prev_ppp) == NULL) {
		int npages = btopr(size + sizeof (kmem_perm_t));
		mutex_exit(&kmem_perm_lock);
		buf = kmem_page_alloc(npages, flags);
		if (buf == NULL) {
			kmem_misc_kstat.perm_alloc_fail.value.l++;
			return (NULL);
		}
		mutex_enter(&kmem_perm_lock);
		kmem_misc_kstat.perm_size.value.l += ptob(npages);
		pp = (kmem_perm_t *)buf;
		pp->perm_next = kmem_perm_freelist;
		pp->perm_current = buf + sizeof (kmem_perm_t);
		pp->perm_avail = ptob(npages) - sizeof (kmem_perm_t);
		kmem_perm_freelist = pp;
		prev_ppp = &kmem_perm_freelist;
	}
	pp = *prev_ppp;
	buf = (char *)(((int)pp->perm_current + align - 1) & -align);
	pp->perm_avail = pp->perm_avail - (buf + size - pp->perm_current);
	pp->perm_current = buf + size;
	if (pp->perm_avail < KMEM_PERM_MINFREE)
		*prev_ppp = pp->perm_next;
	mutex_exit(&kmem_perm_lock);
	return (buf);
}

static kmem_slab_t *
kmem_slab_create(kmem_cache_t *cp, int flags)
{
	int bufsize = cp->cache_bufsize;
	int chunksize = cp->cache_chunksize;
	int cache_flags = cp->cache_flags;
	int color, chunks;
	void (*constructor)(void *, size_t) = cp->cache_constructor;
	void (*destructor)(void *, size_t) = cp->cache_destructor;
	char *buf, *base, *limit;
	kmem_slab_t *sp;
	kmem_bufctl_t *bcp;

	ASSERT(MUTEX_HELD(&cp->cache_lock));

	TRACE_2(TR_FAC_KMEM, TR_KMEM_SLAB_CREATE_START,
		"kmem_slab_create_start:cache %S flags %x",
		cp->cache_name, flags);

	if ((color = cp->cache_color += cp->cache_align) > cp->cache_maxcolor)
		color = cp->cache_color = 0;

	mutex_exit(&cp->cache_lock);

	if ((base = kmem_page_alloc(btop(cp->cache_slabsize), flags)) == NULL)
		goto page_alloc_failure;

	if (cache_flags & KMF_DEADBEEF) {
		copy_pattern(0xdeadbeef, base, cp->cache_slabsize);
		constructor = destructor = NULL;
	}

	if (cache_flags & KMF_HASH) {
		limit = base + cp->cache_slabsize;
		if ((sp = kmem_cache_alloc(kmem_slab_cache, flags)) == NULL)
			goto slab_alloc_failure;
	} else {
		limit = base + cp->cache_slabsize - sizeof (kmem_slab_t);
		sp = (kmem_slab_t *)limit;
	}

	sp->slab_cache	= cp;
	sp->slab_head	= NULL;
	sp->slab_refcnt	= 0;
	sp->slab_base	= buf = base + color;

	chunks = 0;
	while (buf + chunksize <= limit) {
		if (cache_flags & KMF_HASH) {
			bcp = kmem_cache_alloc(kmem_bufctl_cache, flags);
			if (bcp == NULL)
				goto bufctl_alloc_failure;
			if (cache_flags & KMF_AUDIT) {
				bzero((void *)bcp,
					sizeof (kmem_bufctl_audit_t));
				((kmem_bufctl_audit_t *)bcp)->bc_cache = cp;
			}
			bcp->bc_addr = buf;
			bcp->bc_slab = sp;
		} else {
			bcp = (kmem_bufctl_t *)((int)buf + cp->cache_offset);
		}
		if (cache_flags & KMF_BUFTAG) {
			kmem_buftag_t *btp = KMEM_BUFTAG(cp, buf);
			copy_pattern(KMEM_REDZONE_WORD, btp->bt_redzone,
				KMEM_REDZONE_SIZE * sizeof (void *));
			btp->bt_bufctl = bcp;
			btp->bt_bxstat = (int)bcp ^ KMEM_BUFTAG_FREE;
		}
		bcp->bc_next = sp->slab_head;
		sp->slab_head = bcp;
		if (chunks == 0)
			sp->slab_tail = bcp;
		if (constructor != NULL)
			(*constructor)(buf, bufsize);
		buf += chunksize;
		chunks++;
	}
	sp->slab_chunks = chunks;

	mutex_enter(&cp->cache_lock);

	cp->cache_slab_create++;
	cp->cache_buftotal += sp->slab_chunks;
	if (cp->cache_buftotal > cp->cache_bufmax)
		cp->cache_bufmax = cp->cache_buftotal;

	TRACE_1(TR_FAC_KMEM, TR_KMEM_SLAB_CREATE_END,
		"kmem_slab_create_end:slab %x", sp);

	return (sp);

bufctl_alloc_failure:
	while ((bcp = sp->slab_head) != NULL) {
		if (destructor != NULL)
			(*destructor)(bcp->bc_addr, bufsize);
		sp->slab_head = bcp->bc_next;
		kmem_cache_free(kmem_bufctl_cache, bcp);
	}
	kmem_cache_free(kmem_slab_cache, sp);

slab_alloc_failure:
	kmem_page_free(base, btop(cp->cache_slabsize));

page_alloc_failure:
	mutex_enter(&cp->cache_lock);

	TRACE_1(TR_FAC_KMEM, TR_KMEM_SLAB_CREATE_END,
		"kmem_slab_create_end:slab %x", sp);

	return (NULL);
}

static void
kmem_slab_destroy(kmem_cache_t *cp, kmem_slab_t *sp)
{
	int bufsize = cp->cache_bufsize;
	int cache_flags = cp->cache_flags;
	void (*destructor)(void *, size_t) = cp->cache_destructor;
	kmem_bufctl_t *bcp, *next_bcp;
	void *buf, *base;

	ASSERT(MUTEX_HELD(&cp->cache_lock));

	TRACE_2(TR_FAC_KMEM, TR_KMEM_SLAB_DESTROY_START,
		"kmem_slab_destroy_start:cache %S slab %x", cp->cache_name, sp);

	cp->cache_slab_destroy++;
	cp->cache_buftotal -= sp->slab_chunks;

	mutex_exit(&cp->cache_lock);

	if (cache_flags & KMF_DEADBEEF)
		destructor = NULL;

	next_bcp = sp->slab_head;
	sp->slab_tail->bc_next = NULL;	/* normally a garbage pointer */
	while ((bcp = next_bcp) != NULL) {
		next_bcp = bcp->bc_next;
		if (cache_flags & KMF_HASH) {
			buf = bcp->bc_addr;
			kmem_cache_free(kmem_bufctl_cache, bcp);
		} else {
			buf = (void *)((int)bcp - cp->cache_offset);
		}
		if (destructor != NULL)
			(*destructor)(buf, bufsize);
	}
	base = (void *)((int)sp->slab_base & -PAGESIZE);
	if (cache_flags & KMF_HASH)
		kmem_cache_free(kmem_slab_cache, sp);
	kmem_page_free(base, btop(cp->cache_slabsize));

	mutex_enter(&cp->cache_lock);

	TRACE_0(TR_FAC_KMEM, TR_KMEM_SLAB_DESTROY_END, "kmem_slab_destroy_end");
}

static void
kmem_audit(kmem_bufctl_audit_t *bcp)
{
	kmem_bufctl_audit_t *logp;

	bcp->bc_timestamp = gethrtime();
	bcp->bc_thread = curthread;
	bcp->bc_depth = getpcstack(bcp->bc_stack, KMEM_STACK_DEPTH);

	if (kmem_logging == 0)
		return;

	mutex_enter(&kmem_cache_lock);
	logp = &kmem_log[kmem_log_count++ & (kmem_log_size - 1)];
	*logp = *bcp;
	bcp->bc_lastlog = logp;
	mutex_exit(&kmem_cache_lock);
}

static void
kmem_cache_alloc_debug(kmem_cache_t *cp, void *buf)
{
	kmem_buftag_t *btp = KMEM_BUFTAG(cp, buf);
	kmem_bufctl_t *bcp = btp->bt_bufctl;

	if (btp->bt_bxstat != ((int)bcp ^ KMEM_BUFTAG_FREE)) {
		kmem_error(KMERR_BADBUFTAG, cp, buf, bcp);
		return;
	}
	btp->bt_bxstat = (int)bcp ^ KMEM_BUFTAG_ALLOC;

	if ((cp->cache_flags & KMF_HASH) != 0 && bcp->bc_addr != buf) {
		kmem_error(KMERR_BADBUFCTL, cp, buf, bcp);
		return;
	}
	if (cp->cache_flags & KMF_REDZONE)
		btp->bt_redzone[0] = KMEM_REDZONE_WORD;

	if (cp->cache_flags & KMF_DEADBEEF) {
		if (verify_pattern(0xdeadbeef, buf, cp->cache_offset) != NULL)
			kmem_error(KMERR_MODIFIED, cp, buf, bcp);
		copy_pattern(0xbaddcafe, buf, cp->cache_offset);
		if (cp->cache_constructor != NULL)
			(*cp->cache_constructor)(buf, cp->cache_bufsize);
	}
	if (cp->cache_flags & KMF_AUDIT)
		kmem_audit((kmem_bufctl_audit_t *)bcp);
}

static int
kmem_cache_free_debug(kmem_cache_t *cp, void *buf)
{
	kmem_buftag_t *btp = KMEM_BUFTAG(cp, buf);
	kmem_bufctl_t *bcp = btp->bt_bufctl;

	if (btp->bt_bxstat != ((int)bcp ^ KMEM_BUFTAG_ALLOC)) {
		if (btp->bt_bxstat == ((int)bcp ^ KMEM_BUFTAG_FREE)) {
			kmem_error(KMERR_DUPFREE, cp, buf, bcp);
			return (-1);
		}
		bcp = kmem_locate(buf);
		if (bcp == NULL || bcp->bc_slab->slab_cache != cp)
			kmem_error(KMERR_BADADDR, cp, buf, NULL);
		else
			kmem_error(KMERR_REDZONE, cp, buf, bcp);
		return (-1);
	}
	btp->bt_bxstat = (int)bcp ^ KMEM_BUFTAG_FREE;

	if ((cp->cache_flags & KMF_HASH) != 0 && bcp->bc_addr != buf) {
		kmem_error(KMERR_BADBUFCTL, cp, buf, bcp);
		return (-1);
	}
	if (cp->cache_flags & KMF_REDZONE) {
		if (verify_pattern(KMEM_REDZONE_WORD, btp->bt_redzone,
		    KMEM_REDZONE_SIZE * sizeof (void *)) != NULL) {
			kmem_error(KMERR_REDZONE, cp, buf, bcp);
			return (-1);
		}
	}
	if (cp->cache_flags & KMF_DEADBEEF) {
		if (cp->cache_destructor != NULL)
			(*cp->cache_destructor)(buf, cp->cache_bufsize);
		copy_pattern(0xdeadbeef, buf, cp->cache_offset);
	}
	if (cp->cache_flags & KMF_AUDIT)
		kmem_audit((kmem_bufctl_audit_t *)bcp);

	return (0);
}

/*
 * To make the magazine layer as fast as possible, we don't check for
 * kmem debugging flags in production (non-DEBUG) kernels.  We can get
 * equivalent debugging functionality in the field by setting the
 * KMF_NOMAGAZINE flag in addition to any others; that causes the
 * allocator to bypass the magazine layer entirely and go straight
 * to the global layer, which always checks the flags.  This is not
 * satisfactory for internal testing, however, because we also want
 * to stress the magazine code itself; the #defines below enable
 * magazine-layer debugging in DEBUG kernels.
 */
#ifdef DEBUG

#define	KMEM_CACHE_ALLOC_DEBUG(cp, buf)	\
	if ((cp)->cache_flags & KMF_BUFTAG) \
		kmem_cache_alloc_debug(cp, buf)

#define	KMEM_CACHE_FREE_DEBUG(cp, buf)	\
	if ((cp)->cache_flags & KMF_BUFTAG) \
		if (kmem_cache_free_debug(cp, buf)) \
			return;
#else

#define	KMEM_CACHE_ALLOC_DEBUG(cp, buf)
#define	KMEM_CACHE_FREE_DEBUG(cp, buf)

#endif

static void *
kmem_cache_alloc_global(kmem_cache_t *cp, int flags)
{
	void *buf;
	kmem_slab_t *sp, *snext, *sprev;
	kmem_bufctl_t *bcp, **hash_bucket;

	TRACE_2(TR_FAC_KMEM, TR_KMEM_CACHE_ALLOC_START,
		"kmem_cache_alloc_start:cache %S flags %x",
		cp->cache_name, flags);

	mutex_enter(&cp->cache_lock);
	cp->cache_alloc++;
	sp = cp->cache_freelist;
	ASSERT(sp->slab_cache == cp);
	if ((bcp = sp->slab_head) == sp->slab_tail) {
		if (bcp == NULL) {
			/*
			 * The freelist is empty.  Create a new slab.
			 */
			if ((sp = kmem_slab_create(cp, flags)) == NULL) {
				cp->cache_alloc_fail++;
				mutex_exit(&cp->cache_lock);
				TRACE_1(TR_FAC_KMEM, TR_KMEM_CACHE_ALLOC_END,
					"kmem_cache_alloc_end:buf %x", NULL);
				return (NULL);
			}
			/*
			 * Add slab to tail of freelist
			 */
			sp->slab_next = snext = &cp->cache_nullslab;
			sp->slab_prev = sprev = snext->slab_prev;
			snext->slab_prev = sp;
			sprev->slab_next = sp;
			if (cp->cache_freelist == snext)
				cp->cache_freelist = sp;
			sp = cp->cache_freelist;
		}
		/*
		 * If this is last buf in slab, remove slab from free list
		 */
		if ((bcp = sp->slab_head) == sp->slab_tail) {
			cp->cache_freelist = sp->slab_next;
			sp->slab_tail = NULL;
		}
	}

	sp->slab_head = bcp->bc_next;
	sp->slab_refcnt++;
	ASSERT(sp->slab_refcnt <= sp->slab_chunks);

	if (cp->cache_flags & KMF_HASH) {
		/*
		 * add buf to allocated-address hash table
		 */
		buf = bcp->bc_addr;
		hash_bucket = KMEM_HASH(cp, buf);
		bcp->bc_next = *hash_bucket;
		*hash_bucket = bcp;
	} else {
		buf = (void *)((int)bcp - cp->cache_offset);
	}

	ASSERT((u_int)buf - (u_int)sp->slab_base < cp->cache_slabsize);

	mutex_exit(&cp->cache_lock);

	if (cp->cache_flags & KMF_BUFTAG)
		kmem_cache_alloc_debug(cp, buf);

	TRACE_1(TR_FAC_KMEM, TR_KMEM_CACHE_ALLOC_END,
		"kmem_cache_alloc_end:buf %x", buf);

	return (buf);
}

static void
kmem_cache_free_global(kmem_cache_t *cp, void *buf)
{
	kmem_slab_t *sp, *snext, *sprev;
	kmem_bufctl_t *bcp, **prev_bcpp, *old_slab_tail;

	TRACE_2(TR_FAC_KMEM, TR_KMEM_CACHE_FREE_START,
		"kmem_cache_free_start:cache %S buf %x", cp->cache_name, buf);

	ASSERT(buf != NULL);

	if (cp->cache_flags & KMF_BUFTAG)
		if (kmem_cache_free_debug(cp, buf))
			return;

	mutex_enter(&cp->cache_lock);

	if (cp->cache_flags & KMF_HASH) {
		/*
		 * look up buf in allocated-address hash table
		 */
		prev_bcpp = KMEM_HASH(cp, buf);
		bcp = *prev_bcpp;
		do {
			if (bcp->bc_addr == buf)
				goto lookup_success;
			cp->cache_lookup_depth++;
			prev_bcpp = &bcp->bc_next;
		} while ((bcp = *prev_bcpp) != NULL);

		mutex_exit(&cp->cache_lock);
		kmem_error(KMERR_BADADDR, cp, buf, NULL);
		return;
lookup_success:
		/*
		 * remove buf from hash table
		 */
		*prev_bcpp = bcp->bc_next;
		sp = bcp->bc_slab;
	} else {
		bcp = (kmem_bufctl_t *)((int)buf + cp->cache_offset);
		sp = (kmem_slab_t *)((((int)buf) & -PAGESIZE) +
			(PAGESIZE - sizeof (kmem_slab_t)));
	}

	ASSERT(sp->slab_cache == cp);

	old_slab_tail = sp->slab_tail;
	sp->slab_tail = bcp;
	if (old_slab_tail == NULL) {
		/*
		 * Return slab to head of free list
		 */
		sp->slab_head = bcp;
		if ((snext = sp->slab_next) != cp->cache_freelist) {
			snext->slab_prev = sprev = sp->slab_prev;
			sprev->slab_next = snext;
			sp->slab_next = snext = cp->cache_freelist;
			sp->slab_prev = sprev = snext->slab_prev;
			sprev->slab_next = sp;
			snext->slab_prev = sp;
		}
		cp->cache_freelist = sp;
	} else {
		old_slab_tail->bc_next = bcp;
	}
	ASSERT(sp->slab_refcnt >= 1);
	if (--sp->slab_refcnt == 0) {
		/*
		 * There are no outstanding allocations from this slab,
		 * so we can reclaim the memory.
		 */
		snext = sp->slab_next;
		sprev = sp->slab_prev;
		snext->slab_prev = sprev;
		sprev->slab_next = snext;
		if (sp == cp->cache_freelist)
			cp->cache_freelist = snext;
		kmem_slab_destroy(cp, sp);
	}
	mutex_exit(&cp->cache_lock);

	TRACE_0(TR_FAC_KMEM, TR_KMEM_CACHE_FREE_END, "kmem_cache_free_end");
}

void *
kmem_cache_alloc(kmem_cache_t *cp, int flags)
{
	void *buf;
	kmem_cpu_cache_t *ccp;
	kmem_magazine_t *mp, *fmp;

	TRACE_2(TR_FAC_KMEM, TR_KMEM_CACHE_ALLOC_START,
		"kmem_cache_alloc_start:cache %S flags %x",
		cp->cache_name, flags);

	ccp = &cp->cache_cpu[CPU->cpu_seqid];
	mutex_enter(&ccp->cc_lock);
	mp = ccp->cc_loaded_mag;
	if (ccp->cc_rounds > 0) {
		ccp->cc_alloc++;
		buf = mp->mag_round[--ccp->cc_rounds];
		mutex_exit(&ccp->cc_lock);
		TRACE_1(TR_FAC_KMEM, TR_KMEM_CACHE_ALLOC_END,
			"kmem_cache_alloc_end:buf %x", buf);
		KMEM_CACHE_ALLOC_DEBUG(cp, buf);
		return (buf);
	}
	if ((fmp = ccp->cc_full_mag) != NULL) {
		ASSERT(ccp->cc_empty_mag == NULL);
		ccp->cc_alloc++;
		ccp->cc_empty_mag = mp;
		ccp->cc_loaded_mag = fmp;
		ccp->cc_full_mag = NULL;
		ccp->cc_rounds = ccp->cc_magsize - 1;
		buf = fmp->mag_round[ccp->cc_magsize - 1];
		mutex_exit(&ccp->cc_lock);
		TRACE_1(TR_FAC_KMEM, TR_KMEM_CACHE_ALLOC_END,
			"kmem_cache_alloc_end:buf %x", buf);
		KMEM_CACHE_ALLOC_DEBUG(cp, buf);
		return (buf);
	}
	if (ccp->cc_magsize > 0) {
		if (!mutex_tryenter(&cp->cache_depot_lock)) {
			mutex_enter(&cp->cache_depot_lock);
			cp->cache_depot_contention++;
		}
		if ((fmp = cp->cache_fmag_list) != NULL) {
			cp->cache_fmag_list = fmp->mag_next;
			if (--cp->cache_fmag_total < cp->cache_fmag_min)
				cp->cache_fmag_min = cp->cache_fmag_total;
			if (mp != NULL) {
				mp->mag_next = cp->cache_emag_list;
				cp->cache_emag_list = mp;
				cp->cache_emag_total++;
			}
			cp->cache_depot_alloc++;
			mutex_exit(&cp->cache_depot_lock);
			ccp->cc_loaded_mag = fmp;
			ccp->cc_rounds = ccp->cc_magsize - 1;
			buf = fmp->mag_round[ccp->cc_magsize - 1];
			mutex_exit(&ccp->cc_lock);
			TRACE_1(TR_FAC_KMEM, TR_KMEM_CACHE_ALLOC_END,
				"kmem_cache_alloc_end:buf %x", buf);
			KMEM_CACHE_ALLOC_DEBUG(cp, buf);
			return (buf);
		}
		mutex_exit(&cp->cache_depot_lock);
	}
	mutex_exit(&ccp->cc_lock);
	buf = kmem_cache_alloc_global(cp, flags);
	TRACE_1(TR_FAC_KMEM, TR_KMEM_CACHE_ALLOC_END,
		"kmem_cache_alloc_end:buf %x", buf);
	return (buf);
}

void
kmem_cache_free(kmem_cache_t *cp, void *buf)
{
	kmem_cpu_cache_t *ccp;
	kmem_magazine_t *mp, *emp;

	TRACE_2(TR_FAC_KMEM, TR_KMEM_CACHE_FREE_START,
		"kmem_cache_free_start:cache %S buf %x", cp->cache_name, buf);
	KMEM_CACHE_FREE_DEBUG(cp, buf);

	ccp = &cp->cache_cpu[CPU->cpu_seqid];
	mutex_enter(&ccp->cc_lock);
	mp = ccp->cc_loaded_mag;
	if ((u_int)ccp->cc_rounds < ccp->cc_magsize) {
		ccp->cc_free++;
		mp->mag_round[ccp->cc_rounds++] = buf;
		mutex_exit(&ccp->cc_lock);
		TRACE_0(TR_FAC_KMEM, TR_KMEM_CACHE_FREE_END,
			"kmem_cache_free_end");
		return;
	}
	if ((emp = ccp->cc_empty_mag) != NULL) {
		ASSERT(ccp->cc_full_mag == NULL);
		ccp->cc_free++;
		ccp->cc_full_mag = mp;
		ccp->cc_loaded_mag = emp;
		ccp->cc_empty_mag = NULL;
		ccp->cc_rounds = 1;
		emp->mag_round[0] = buf;
		mutex_exit(&ccp->cc_lock);
		TRACE_0(TR_FAC_KMEM, TR_KMEM_CACHE_FREE_END,
			"kmem_cache_free_end");
		return;
	}
	if (ccp->cc_magsize > 0) {
		if (!mutex_tryenter(&cp->cache_depot_lock)) {
			mutex_enter(&cp->cache_depot_lock);
			cp->cache_depot_contention++;
		}
		if ((emp = cp->cache_emag_list) != NULL) {
			cp->cache_emag_list = emp->mag_next;
			if (--cp->cache_emag_total < cp->cache_emag_min)
				cp->cache_emag_min = cp->cache_emag_total;
		}
		if (emp != NULL || (emp =
		    kmem_cache_alloc_global(cp->cache_magazine_cache,
		    KM_NOSLEEP)) != NULL) {
			if (mp != NULL) {
				mp->mag_next = cp->cache_fmag_list;
				cp->cache_fmag_list = mp;
				cp->cache_fmag_total++;
			}
			cp->cache_depot_free++;
			mutex_exit(&cp->cache_depot_lock);
			ccp->cc_loaded_mag = emp;
			ccp->cc_rounds = 1;
			emp->mag_round[0] = buf;
			mutex_exit(&ccp->cc_lock);
			TRACE_0(TR_FAC_KMEM, TR_KMEM_CACHE_FREE_END,
				"kmem_cache_free_end");
			return;
		}
		mutex_exit(&cp->cache_depot_lock);
	}
	mutex_exit(&ccp->cc_lock);
	KMEM_CACHE_ALLOC_DEBUG(cp, buf);
	kmem_cache_free_global(cp, buf);
	TRACE_0(TR_FAC_KMEM, TR_KMEM_CACHE_FREE_END, "kmem_cache_free_end");
}

void *
kmem_zalloc(size_t size, int flags)
{
	void *buf;
	int index = (int)(size - 1) >> KMEM_ALIGN_SHIFT;

	TRACE_2(TR_FAC_KMEM, TR_KMEM_ZALLOC_START,
		"kmem_zalloc_start:size %d flags %x", size, flags);

	if ((u_int)index >= KMEM_MAXBUF >> KMEM_ALIGN_SHIFT) {
		if ((buf = kmem_alloc(size, flags)) != NULL)
			KMEM_BZERO(buf, size);
	} else {
		kmem_cache_t *cp = kmem_alloc_table[index];
		kmem_cpu_cache_t *ccp = &cp->cache_cpu[CPU->cpu_seqid];
		mutex_enter(&ccp->cc_lock);
		if (ccp->cc_rounds > 0) {
			ccp->cc_alloc++;
			buf = ccp->cc_loaded_mag->mag_round[--ccp->cc_rounds];
			mutex_exit(&ccp->cc_lock);
			KMEM_CACHE_ALLOC_DEBUG(cp, buf);
			KMEM_BZERO(buf, size);
		} else {
			mutex_exit(&ccp->cc_lock);
			if ((buf = kmem_cache_alloc(cp, flags)) != NULL)
				KMEM_BZERO(buf, size);
		}
	}

	TRACE_1(TR_FAC_KMEM, TR_KMEM_ZALLOC_END, "kmem_zalloc_end:buf %x", buf);

	return (buf);
}

void *
kmem_alloc(size_t size, int flags)
{
	void *buf;
	int index = (int)(size - 1) >> KMEM_ALIGN_SHIFT;

	TRACE_2(TR_FAC_KMEM, TR_KMEM_ALLOC_START,
		"kmem_alloc_start:size %d flags %x", size, flags);

	if ((u_int)index >= KMEM_MAXBUF >> KMEM_ALIGN_SHIFT) {
		if (size == 0) {
			TRACE_1(TR_FAC_KMEM, TR_KMEM_ALLOC_END,
				"kmem_alloc_end:buf %x", NULL);
			return (NULL);
		}
		kmem_misc_kstat.huge_alloc.value.l++;
		if ((buf = kmem_page_alloc(btopr(size), flags)) == NULL)
			kmem_misc_kstat.huge_alloc_fail.value.l++;
		else if (kmem_flags & KMF_DEADBEEF)
			copy_pattern(0xbaddcafe, buf, size);
	} else {
		kmem_cache_t *cp = kmem_alloc_table[index];
		kmem_cpu_cache_t *ccp = &cp->cache_cpu[CPU->cpu_seqid];
		mutex_enter(&ccp->cc_lock);
		if (ccp->cc_rounds > 0) {
			ccp->cc_alloc++;
			buf = ccp->cc_loaded_mag->mag_round[--ccp->cc_rounds];
			mutex_exit(&ccp->cc_lock);
			KMEM_CACHE_ALLOC_DEBUG(cp, buf);
		} else {
			mutex_exit(&ccp->cc_lock);
			buf = kmem_cache_alloc(cp, flags);
		}
	}

	TRACE_1(TR_FAC_KMEM, TR_KMEM_ALLOC_END, "kmem_alloc_end:buf %x", buf);

	return (buf);
}

void
kmem_free(void *buf, size_t size)
{
	int index = (size - 1) >> KMEM_ALIGN_SHIFT;

	TRACE_2(TR_FAC_KMEM, TR_KMEM_FREE_START,
		"kmem_free_start:buf %x size %d", buf, size);

	if ((u_int)index >= KMEM_MAXBUF >> KMEM_ALIGN_SHIFT) {
		if (buf == NULL && size == 0) {
			TRACE_0(TR_FAC_KMEM, TR_KMEM_FREE_END, "kmem_free_end");
			return;
		}
		if (buf == NULL || size == 0)
			cmn_err(CE_PANIC, "kmem_free(%x, %d) impossible",
				buf, size);
		kmem_page_free(buf, btopr(size));
	} else {
		kmem_cache_t *cp = kmem_alloc_table[index];
		kmem_cpu_cache_t *ccp = &cp->cache_cpu[CPU->cpu_seqid];
		KMEM_CACHE_FREE_DEBUG(cp, buf);
		mutex_enter(&ccp->cc_lock);
		if ((u_int)ccp->cc_rounds < ccp->cc_magsize) {
			ccp->cc_free++;
			ccp->cc_loaded_mag->mag_round[ccp->cc_rounds++] = buf;
			mutex_exit(&ccp->cc_lock);
		} else {
			mutex_exit(&ccp->cc_lock);
			KMEM_CACHE_ALLOC_DEBUG(cp, buf);
			kmem_cache_free(cp, buf);
		}
	}

	TRACE_0(TR_FAC_KMEM, TR_KMEM_FREE_END, "kmem_free_end");
}

/* ARGSUSED */
void *
kmem_fast_zalloc(char **base, size_t size, int chunks, int flags)
{
	void *buf;

	TRACE_4(TR_FAC_KMEM, TR_KMEM_FAST_ZALLOC_START,
		"kmem_fast_zalloc_start:base %K size %d chunks %d flags %x",
		base, size, chunks, flags);

	if ((buf = *base) == NULL) {
		if (kmem_fast_debug) {
			buf = kmem_perm_alloc(size + KMEM_ALIGN,
				KMEM_ALIGN, flags);
			if (buf != NULL) {
				buf = (char *)buf + KMEM_ALIGN;
				((int *)buf)[-1] = size;
				copy_pattern(0xdeadbeef, buf, size);
			}
		} else {
			buf = kmem_perm_alloc(size, KMEM_ALIGN, flags);
		}
		if (buf == NULL) {
			TRACE_1(TR_FAC_KMEM, TR_KMEM_FAST_ZALLOC_END,
				"kmem_fast_zalloc_end:buf %x", buf);
			return (NULL);
		}
	} else {
		*base = *(void **)buf;
	}

	if (kmem_fast_debug) {
		*(void **)buf = (void *)0xdeadbeef;
		ASSERT(((int *)buf)[-1] == size);
		if (verify_pattern(0xdeadbeef, buf, size) != NULL)
			kmem_error(KMERR_MODIFIED, NULL, buf, NULL);
	}

	KMEM_BZERO(buf, size);

	TRACE_1(TR_FAC_KMEM, TR_KMEM_FAST_ZALLOC_END,
		"kmem_fast_zalloc_end:buf %x", buf);

	return (buf);
}

/* ARGSUSED */
void *
kmem_fast_alloc(char **base, size_t size, int chunks, int flags)
{
	void *buf;

	TRACE_4(TR_FAC_KMEM, TR_KMEM_FAST_ALLOC_START,
		"kmem_fast_alloc_start:base %K size %d chunks %d flags %x",
		base, size, chunks, flags);

	if ((buf = *base) == NULL) {
		if (kmem_fast_debug) {
			buf = kmem_perm_alloc(size + KMEM_ALIGN,
				KMEM_ALIGN, flags);
			if (buf != NULL) {
				buf = (char *)buf + KMEM_ALIGN;
				((int *)buf)[-1] = size;
				copy_pattern(0xdeadbeef, buf, size);
			}
		} else {
			buf = kmem_perm_alloc(size, KMEM_ALIGN, flags);
		}
		if (buf == NULL) {
			TRACE_1(TR_FAC_KMEM, TR_KMEM_FAST_ALLOC_END,
				"kmem_fast_alloc_end:buf %x", buf);
			return (NULL);
		}
	} else {
		*base = *(void **)buf;
	}

	if (kmem_fast_debug) {
		*(void **)buf = (void *)0xdeadbeef;
		ASSERT(((int *)buf)[-1] == size);
		if (verify_pattern(0xdeadbeef, buf, size) != NULL)
			kmem_error(KMERR_MODIFIED, NULL, buf, NULL);
		copy_pattern(0xbaddcafe, buf, size);
	}

	TRACE_1(TR_FAC_KMEM, TR_KMEM_FAST_ALLOC_END,
		"kmem_fast_alloc_end:buf %x", buf);

	return (buf);
}

void
kmem_fast_free(char **base, void *buf)
{
	TRACE_2(TR_FAC_KMEM, TR_KMEM_FAST_FREE_START,
		"kmem_fast_free_start:base %K buf %x", base, buf);

	if (kmem_fast_debug)
		copy_pattern(0xdeadbeef, buf, ((int *)buf)[-1]);

	*(void **)buf = *base;
	*base = buf;

	TRACE_0(TR_FAC_KMEM, TR_KMEM_FAST_FREE_END, "kmem_fast_free_end");
}

static void
kmem_magazine_destroy_one(kmem_cache_t *cp, kmem_magazine_t *mp, int nrounds)
{
	if (mp != NULL) {
		int round;
		for (round = 0; round < nrounds; round++) {
			KMEM_CACHE_ALLOC_DEBUG(cp, mp->mag_round[round]);
			kmem_cache_free_global(cp, mp->mag_round[round]);
		}
		kmem_cache_free_global(cp->cache_magazine_cache, mp);
	}
}

static void
kmem_magazine_destroy(kmem_cache_t *cp, kmem_magazine_t *mp, int nrounds)
{
	while (mp != NULL) {
		kmem_magazine_t *nextmp = mp->mag_next;
		kmem_magazine_destroy_one(cp, mp, nrounds);
		mp = nextmp;
	}
}

static void
kmem_async_dispatch(void (*func)(kmem_cache_t *), kmem_cache_t *cp, int flags)
{
	kmem_async_t *ap, *anext, *aprev;

	TRACE_3(TR_FAC_KMEM, TR_KMEM_ASYNC_DISPATCH_START,
		"kmem_async_dispatch_start:%K(%S) flags %x",
		func, cp->cache_name, flags);

	mutex_enter(&kmem_async_lock);
	if ((ap = kmem_async_freelist) == NULL) {
		mutex_exit(&kmem_async_lock);
		if ((ap = kmem_perm_alloc(sizeof (*ap), 0, flags)) == NULL)
			return;
		mutex_enter(&kmem_async_lock);
	} else {
		kmem_async_freelist = ap->async_next;
	}
	ap->async_next = anext = &kmem_async_queue;
	ap->async_prev = aprev = kmem_async_queue.async_prev;
	aprev->async_next = ap;
	anext->async_prev = ap;
	ap->async_func = func;
	ap->async_cache = cp;
	cv_signal(&kmem_async_cv);
	mutex_exit(&kmem_async_lock);

	TRACE_1(TR_FAC_KMEM, TR_KMEM_ASYNC_DISPATCH_END,
		"kmem_async_dispatch_end:async_entry %x", ap);
}

/*
 * Reclaim all unused memory from a cache.
 */
static void
kmem_cache_reap(kmem_cache_t *cp)
{
	int reaplimit, magsize;
	kmem_magazine_t *mp, *fmp, *reaplist;
	kmem_cpu_cache_t *ccp;
	void *buf = NULL;

	ASSERT(MUTEX_HELD(&kmem_async_serialize));

	/*
	 * Ask the cache's owner to free some memory if possible.
	 * The idea is to handle things like the inode cache, which
	 * typically sits on a bunch of memory that it doesn't truly
	 * *need*.  Reclaim policy is entirely up to the owner; this
	 * callback is just an advisory plea for help.
	 */
	if (cp->cache_reclaim != NULL)
		(*cp->cache_reclaim)();

	/*
	 * We want to ensure that unused buffers scattered across multiple
	 * cpus will eventually coalesce into complete slabs.  Each time
	 * this routine runs it selects a victim cpu, returns its full
	 * magazine to the depot, and returns one buffer from its loaded
	 * magazine to the global layer.  Eventually all unused memory
	 * is reclaimed.
	 */
	mutex_enter(&cp->cache_depot_lock);
	if (++cp->cache_cpu_rotor >= ncpus)
		cp->cache_cpu_rotor = 0;
	ccp = &cp->cache_cpu[cp->cache_cpu_rotor];
	mutex_exit(&cp->cache_depot_lock);

	mutex_enter(&ccp->cc_lock);
	if ((fmp = ccp->cc_full_mag) != NULL) {
		ccp->cc_full_mag = NULL;
		mutex_enter(&cp->cache_depot_lock);
		fmp->mag_next = cp->cache_fmag_list;
		cp->cache_fmag_list = fmp;
		cp->cache_fmag_total++;
		mutex_exit(&cp->cache_depot_lock);
	}
	if (ccp->cc_rounds > 0) {
		ccp->cc_alloc++;
		buf = ccp->cc_loaded_mag->mag_round[--ccp->cc_rounds];
	}
	mutex_exit(&ccp->cc_lock);

	if (buf != NULL) {
		KMEM_CACHE_ALLOC_DEBUG(cp, buf);
		kmem_cache_free_global(cp, buf);
	}

	mutex_enter(&cp->cache_depot_lock);
	reaplimit = min(cp->cache_fmag_reaplimit, cp->cache_fmag_min);
	reaplist = NULL;
	cp->cache_fmag_total -= reaplimit;
	cp->cache_fmag_min -= reaplimit;
	cp->cache_fmag_reaplimit = 0;
	while (--reaplimit >= 0) {
		mp = cp->cache_fmag_list;
		cp->cache_fmag_list = mp->mag_next;
		mp->mag_next = reaplist;
		reaplist = mp;
	}
	magsize = cp->cache_magazine_size;
	mutex_exit(&cp->cache_depot_lock);
	kmem_magazine_destroy(cp, reaplist, magsize);

	mutex_enter(&cp->cache_depot_lock);
	reaplimit = min(cp->cache_emag_reaplimit, cp->cache_emag_min);
	reaplist = NULL;
	cp->cache_emag_total -= reaplimit;
	cp->cache_emag_min -= reaplimit;
	cp->cache_emag_reaplimit = 0;
	while (--reaplimit >= 0) {
		mp = cp->cache_emag_list;
		cp->cache_emag_list = mp->mag_next;
		mp->mag_next = reaplist;
		reaplist = mp;
	}
	mutex_exit(&cp->cache_depot_lock);
	kmem_magazine_destroy(cp, reaplist, 0);
}

/*
 * Reclaim all unused memory from all caches.  Called from the VM system
 * when memory gets tight.
 */
void
kmem_reap(void)
{
	kmem_cache_t *cp;

	if ((int)(lbolt - kmem_reap_lasttime) < kmem_reap_interval)
		return;
	kmem_reap_lasttime = lbolt;

	mutex_enter(&kmem_cache_lock);
	for (cp = kmem_null_cache.cache_next; cp != &kmem_null_cache;
	    cp = cp->cache_next)
		kmem_async_dispatch(kmem_cache_reap, cp, KM_NOSLEEP);
	mutex_exit(&kmem_cache_lock);
}

/*
 * Purge all magazines from a cache and set its magazine limit to zero.
 * All calls are serialized by kmem_async_serialize.
 */
static void
kmem_cache_magazine_purge(kmem_cache_t *cp)
{
	kmem_cpu_cache_t *ccp;
	kmem_magazine_t *mp, *fmp, *emp;
	int rounds, magsize, cpu_seqid;

	ASSERT(MUTEX_HELD(&kmem_async_serialize));
	ASSERT(MUTEX_NOT_HELD(&cp->cache_lock));

	for (cpu_seqid = 0; cpu_seqid < ncpus; cpu_seqid++) {
		ccp = &cp->cache_cpu[cpu_seqid];

		mutex_enter(&ccp->cc_lock);
		rounds = ccp->cc_rounds;
		magsize = ccp->cc_magsize;
		mp = ccp->cc_loaded_mag;
		fmp = ccp->cc_full_mag;
		emp = ccp->cc_empty_mag;
		ccp->cc_rounds = -1;
		ccp->cc_magsize = 0;
		ccp->cc_loaded_mag = NULL;
		ccp->cc_full_mag = NULL;
		ccp->cc_empty_mag = NULL;
		mutex_exit(&ccp->cc_lock);

		kmem_magazine_destroy_one(cp, mp, rounds);
		kmem_magazine_destroy_one(cp, fmp, magsize);
		kmem_magazine_destroy_one(cp, emp, 0);
	}

	mutex_enter(&cp->cache_depot_lock);
	cp->cache_fmag_min = cp->cache_fmag_reaplimit = cp->cache_fmag_total;
	cp->cache_emag_min = cp->cache_emag_reaplimit = cp->cache_emag_total;
	mutex_exit(&cp->cache_depot_lock);

	kmem_cache_reap(cp);
}

/*
 * Recompute a cache's magazine size.  The trade-off is that larger magazines
 * provide a higher transfer rate with the global layer, while smaller
 * magazines reduce memory consumption.  Magazine resizing is an expensive
 * operation; it should not be done frequently.  Changes to the magazine size
 * are serialized by kmem_async_serialize.
 *
 * Note: at present this only grows the magazine size.  It might be useful
 * to allow shrinkage too.
 */
static void
kmem_magazine_resize(kmem_cache_t *cp)
{
	kmem_cpu_cache_t *ccp;
	int cpu_seqid;
	kmem_magazine_type_t *mtp;

	ASSERT(MUTEX_HELD(&kmem_async_serialize));

	mutex_enter(&cp->cache_depot_lock);

	if (cp->cache_magazine_size == cp->cache_magazine_maxsize) {
		mutex_exit(&cp->cache_depot_lock);
		return;
	}

	mtp = kmem_magazine_type;
	while (mtp->mt_magsize <= cp->cache_magazine_size)
		mtp++;

	mutex_exit(&cp->cache_depot_lock);

	kmem_cache_magazine_purge(cp);

	mutex_enter(&cp->cache_depot_lock);
	cp->cache_magazine_cache = mtp->mt_cache;
	cp->cache_magazine_size = mtp->mt_magsize;
	mutex_exit(&cp->cache_depot_lock);

	for (cpu_seqid = 0; cpu_seqid < ncpus; cpu_seqid++) {
		ccp = &cp->cache_cpu[cpu_seqid];
		mutex_enter(&ccp->cc_lock);
		ccp->cc_magsize = mtp->mt_magsize;
		mutex_exit(&ccp->cc_lock);
	}

	/*
	 * Recalibrate contention count -- we don't want any previous
	 * contention to be counted against the new magazine size.
	 */
	mutex_enter(&cp->cache_depot_lock);
	cp->cache_depot_contention_last = cp->cache_depot_contention;
	mutex_exit(&cp->cache_depot_lock);
}

/*
 * Rescale a cache's hash table, so that the table size is roughly the
 * cache size.  We want the average lookup time to be extremely small.
 */
static void
kmem_hash_rescale(kmem_cache_t *cp)
{
	int old_size, new_size, h, rescale;
	kmem_bufctl_t **old_table, **new_table, *bcp;

	ASSERT(MUTEX_HELD(&kmem_async_serialize));

	TRACE_2(TR_FAC_KMEM, TR_KMEM_HASH_RESCALE_START,
		"kmem_hash_rescale_start:cache %S buftotal %d",
		cp->cache_name, cp->cache_buftotal);

	new_size = max(KMEM_MIN_HASH_SIZE,
		1 << (highbit(3 * cp->cache_buftotal + 4) - 2));
	new_table = kmem_alloc(new_size * sizeof (kmem_bufctl_t *), KM_NOSLEEP);
	if (new_table == NULL)
		return;

	copy_pattern((u_long)&kmem_null_bufctl, new_table,
		new_size * sizeof (kmem_bufctl_t *));

	mutex_enter(&cp->cache_lock);

	old_size = cp->cache_hash_mask + 1;
	old_table = cp->cache_hash_table;

	cp->cache_hash_mask = new_size - 1;
	cp->cache_hash_table = new_table;

	for (h = 0; h < old_size; h++) {
		bcp = old_table[h];
		while (bcp != &kmem_null_bufctl) {
			void *addr = bcp->bc_addr;
			kmem_bufctl_t *next_bcp = bcp->bc_next;
			kmem_bufctl_t **hash_bucket = KMEM_HASH(cp, addr);
			bcp->bc_next = *hash_bucket;
			*hash_bucket = bcp;
			bcp = next_bcp;
		}
	}
	rescale = cp->cache_rescale++;

	mutex_exit(&cp->cache_lock);

	if (rescale == 0)	/* if old_table is the initial table */
		kmem_page_free(old_table, 1);
	else
		kmem_free(old_table, old_size * sizeof (kmem_bufctl_t *));

	TRACE_0(TR_FAC_KMEM, TR_KMEM_HASH_RESCALE_END, "kmem_hash_rescale_end");
}

/*
 * Perform periodic maintenance on a cache: hash rescaling,
 * depot working-set update, and magazine resizing.
 */
static void
kmem_cache_update(kmem_cache_t *cp)
{
	mutex_enter(&cp->cache_lock);

	/*
	 * If the cache has become much larger or smaller than its hash table,
	 * fire off a request to rescale the hash table.
	 */
	if ((cp->cache_flags & KMF_HASH) &&
	    (cp->cache_buftotal > (cp->cache_hash_mask << 1) ||
	    (cp->cache_buftotal < (cp->cache_hash_mask >> 1) &&
	    cp->cache_hash_mask > KMEM_MIN_HASH_SIZE)))
		kmem_async_dispatch(kmem_hash_rescale, cp, KM_NOSLEEP);

	mutex_enter(&cp->cache_depot_lock);

	/*
	 * Update the depot working set sizes
	 */
	cp->cache_fmag_reaplimit = cp->cache_fmag_min;
	cp->cache_fmag_min = cp->cache_fmag_total;

	cp->cache_emag_reaplimit = cp->cache_emag_min;
	cp->cache_emag_min = cp->cache_emag_total;

	/*
	 * If there's a lot of contention in the depot,
	 * increase the magazine size.
	 */
	if (cp->cache_magazine_size < cp->cache_magazine_maxsize &&
	    cp->cache_depot_contention - cp->cache_depot_contention_last >
	    kmem_depot_contention)
		kmem_async_dispatch(kmem_magazine_resize, cp, KM_NOSLEEP);

	cp->cache_depot_contention_last = cp->cache_depot_contention;

	mutex_exit(&cp->cache_depot_lock);
	mutex_exit(&cp->cache_lock);
}

static void
kmem_update(void *dummy)
{
	kmem_cache_t *cp;

	mutex_enter(&kmem_cache_lock);
	for (cp = kmem_null_cache.cache_next; cp != &kmem_null_cache;
	    cp = cp->cache_next)
		kmem_cache_update(cp);
	mutex_exit(&kmem_cache_lock);

	/*
	 * XXX -- Check to see if the system is out of kernelmap.
	 * This gets an 'XXX' because the allocator shouldn't
	 * know about such things.  This will go away when I add
	 * pluggable back-end page supply vectors.
	 */
	if (mapwant(kernelmap))
		kmem_reap();

	(void) timeout(kmem_update, dummy, kmem_reap_interval);
}

static int
kmem_cache_kstat_update(kstat_t *ksp, int rw)
{
	struct kmem_cache_kstat *kmcp;
	kmem_cache_t *cp;
	kmem_slab_t *sp;
	int cpu_buf_avail;
	int buf_avail = 0;
	int cpu_seqid;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	kmcp = kmem_cache_kstat;
	cp = ksp->ks_private;

	kmcp->kmc_alloc_fail.value.l		= cp->cache_alloc_fail;
	kmcp->kmc_alloc.value.l			= cp->cache_alloc;
	kmcp->kmc_global_alloc.value.l		= cp->cache_alloc;

	for (cpu_seqid = 0; cpu_seqid < ncpus; cpu_seqid++) {
		kmem_cpu_cache_t *ccp = &cp->cache_cpu[cpu_seqid];
		struct kmem_cpu_kstat *kmcpup = &kmem_cpu_kstat[cpu_seqid];

		mutex_enter(&ccp->cc_lock);

		cpu_buf_avail = 0;
		if (ccp->cc_rounds > 0)
			cpu_buf_avail += ccp->cc_rounds;
		if (ccp->cc_full_mag)
			cpu_buf_avail += ccp->cc_magsize;

		kmcpup->kmcpu_alloc_from.value.l	= ccp->cc_alloc;
		kmcpup->kmcpu_free_to.value.l		= ccp->cc_free;
		kmcpup->kmcpu_buf_avail.value.l		= cpu_buf_avail;

		kmcp->kmc_alloc.value.l			+= ccp->cc_alloc;
		buf_avail				+= cpu_buf_avail;

		mutex_exit(&ccp->cc_lock);
	}

	mutex_enter(&cp->cache_depot_lock);

	kmcp->kmc_depot_alloc.value.l		= cp->cache_depot_alloc;
	kmcp->kmc_depot_free.value.l		= cp->cache_depot_free;
	kmcp->kmc_depot_contention.value.l	= cp->cache_depot_contention;
	kmcp->kmc_empty_magazines.value.l	= cp->cache_emag_total;
	kmcp->kmc_full_magazines.value.l	= cp->cache_fmag_total;
	kmcp->kmc_magazine_size.value.l		= cp->cache_magazine_size;

	kmcp->kmc_alloc.value.l			+= cp->cache_depot_alloc;
	buf_avail += cp->cache_fmag_total * cp->cache_magazine_size;

	mutex_exit(&cp->cache_depot_lock);

	kmcp->kmc_buf_size.value.l	= cp->cache_bufsize;
	kmcp->kmc_align.value.l		= cp->cache_align;
	kmcp->kmc_chunk_size.value.l	= cp->cache_chunksize;
	kmcp->kmc_slab_size.value.l	= cp->cache_slabsize;
	for (sp = cp->cache_freelist; sp != &cp->cache_nullslab;
	    sp = sp->slab_next)
		buf_avail += sp->slab_chunks - sp->slab_refcnt;
	kmcp->kmc_buf_avail.value.l	= buf_avail;
	kmcp->kmc_buf_total.value.l	= cp->cache_buftotal;
	kmcp->kmc_buf_max.value.l	= cp->cache_bufmax;
	kmcp->kmc_slab_create.value.l	= cp->cache_slab_create;
	kmcp->kmc_slab_destroy.value.l	= cp->cache_slab_destroy;
	kmcp->kmc_hash_size.value.l	= (cp->cache_hash_mask + 1) & -2;
	kmcp->kmc_hash_lookup_depth.value.l = cp->cache_lookup_depth;
	kmcp->kmc_hash_rescale.value.l	= cp->cache_rescale;
	return (0);
}

static void
kmem_cache_create_finish(kmem_cache_t *cp)
{
	ASSERT(MUTEX_HELD(&kmem_async_serialize));

	if (cp->cache_ncpus > ncpus) {
		/*
		 * We over-allocated cache_cpu[] in kmem_cache_create()
		 * because we didn't know ncpus at that time.  Now we do
		 * so return the unused memory to kmem_perm_freelist.
		 */
		int size, cpu_seqid;

		for (cpu_seqid = ncpus; cpu_seqid < cp->cache_ncpus;
		    cpu_seqid++)
			mutex_destroy(&cp->cache_cpu[cpu_seqid].cc_lock);

		size = (cp->cache_ncpus - ncpus) * sizeof (kmem_cpu_cache_t);

		if (size >= sizeof (kmem_perm_t) + KMEM_PERM_MINFREE) {
			char *buf = (char *)&cp->cache_cpu[ncpus];
			kmem_perm_t *pp = (kmem_perm_t *)buf;

			mutex_enter(&kmem_perm_lock);
			kmem_misc_kstat.perm_size.value.l += size;
			pp->perm_next = kmem_perm_freelist;
			pp->perm_current = buf + sizeof (kmem_perm_t);
			pp->perm_avail = size - sizeof (kmem_perm_t);
			kmem_perm_freelist = pp;
			mutex_exit(&kmem_perm_lock);
		}
		cp->cache_ncpus = ncpus;
	}

	if ((cp->cache_kstat = kstat_create("unix", 0, cp->cache_name,
	    "kmem_cache", KSTAT_TYPE_NAMED,
	    sizeof (struct kmem_cache_kstat) / sizeof (kstat_named_t) +
	    sizeof (struct kmem_cpu_kstat) * ncpus / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL)) != NULL) {
		cp->cache_kstat->ks_data = kmem_cache_kstat;
		cp->cache_kstat->ks_update = kmem_cache_kstat_update;
		cp->cache_kstat->ks_private = cp;
		cp->cache_kstat->ks_lock = &cp->cache_lock;
		kstat_install(cp->cache_kstat);
	}
}

kmem_cache_t *
kmem_cache_create(
	char *name,		/* descriptive name for this cache */
	size_t bufsize,		/* size of the objects it manages */
	int align,		/* required object alignment */
	void (*constructor)(void *, size_t),	/* object constructor */
	void (*destructor)(void *, size_t),	/* object destructor */
	void (*reclaim)(void))			/* memory reclaim callback */
{
	int cpu_seqid, slabsize, chunksize, offset, maxcolor, raw;
	kmem_cache_t *cp, *cnext, *cprev;
	kmem_magazine_type_t *mtp;
	char namebuf[128];

	mutex_enter(&kmem_cache_lock);
	if ((cp = kmem_cache_freelist) == NULL) {
		mutex_exit(&kmem_cache_lock);
		cp = kmem_perm_alloc((KMEM_CACHE_SIZE(max_ncpus) +
			KMEM_CPU_CACHE_SIZE - 1) & -KMEM_CPU_CACHE_SIZE,
			KMEM_CPU_CACHE_SIZE, KM_SLEEP);
		/*
		 * Make sure that cp->cache_cpu[0] is aligned on a
		 * KMEM_CPU_CACHE_SIZE-byte boundary.  The idea is
		 * to keep per-cpu data on different cache lines.
		 */
		cp = (kmem_cache_t *)((int)cp +
			(-KMEM_CACHE_SIZE(0) & (KMEM_CPU_CACHE_SIZE)));
		bzero((char *)cp, KMEM_CACHE_SIZE(max_ncpus));
		cp->cache_ncpus = max_ncpus;
	} else {
		kmem_cache_freelist = cp->cache_next;
		mutex_exit(&kmem_cache_lock);
		bzero((char *)cp, KMEM_CACHE_SIZE(ncpus));
		cp->cache_ncpus = ncpus;
	}

	strncpy(cp->cache_name, name, KMEM_CACHE_NAMELEN);
	mutex_init(&cp->cache_lock, name, MUTEX_DEFAULT, NULL);
	sprintf(namebuf, "%s_depot", name);
	mutex_init(&cp->cache_depot_lock, namebuf, MUTEX_DEFAULT, NULL);
	cv_init(&cp->cache_cv, name, CV_DEFAULT, NULL);

	if (align < kmem_align)
		align = kmem_align;
	if ((align & (align - 1)) || align > PAGESIZE)
		cmn_err(CE_PANIC, "kmem_cache_create: bad alignment %d", align);

	cp->cache_bufsize	= bufsize;
	cp->cache_align		= align;
	cp->cache_constructor	= constructor;
	cp->cache_destructor	= destructor;
	cp->cache_reclaim	= reclaim;

	chunksize = (bufsize + (KMEM_ALIGN - 1)) & -KMEM_ALIGN;
	raw = ((chunksize + KMEM_ALIGN + align) <= kmem_maxraw);
	if (raw && (constructor || destructor)) {
		if (chunksize - bufsize >= sizeof (void *)) {
			offset = chunksize - sizeof (void *);
		} else {
			offset = chunksize;
			chunksize += KMEM_ALIGN;
		}
	} else {
		offset = chunksize - KMEM_ALIGN;
	}
	if (kmem_flags & KMF_BUFTAG) {
		offset = (bufsize + (sizeof (void *) - 1)) & -(sizeof (void *));
		chunksize = offset + sizeof (kmem_buftag_t);
	}
	chunksize = (chunksize + align - 1) & -align;

	cp->cache_chunksize = chunksize;
	cp->cache_offset = offset;
	cp->cache_flags = kmem_flags | KMF_READY;
	cp->cache_freelist = &cp->cache_nullslab;
	cp->cache_nullslab.slab_cache = cp;
	cp->cache_nullslab.slab_refcnt = -1;
	cp->cache_nullslab.slab_next = &cp->cache_nullslab;
	cp->cache_nullslab.slab_prev = &cp->cache_nullslab;

	mtp = kmem_magazine_type;
	while (chunksize <= mtp->mt_maxbuf)
		mtp++;
	if (!(kmem_flags & KMF_NOMAGAZINE) && mtp->mt_magsize != 0) {
		cp->cache_magazine_maxsize = mtp->mt_magsize;
		mtp = kmem_magazine_type;
		while (chunksize <= mtp->mt_minbuf)
			mtp++;
		cp->cache_magazine_size = mtp->mt_magsize;
		cp->cache_magazine_cache = mtp->mt_cache;
	}

	if (raw) {
		slabsize = PAGESIZE;
		maxcolor = (slabsize - sizeof (kmem_slab_t)) % chunksize;
	} else {
		int chunks, bestfit, waste, minwaste = INT_MAX;
		for (chunks = 1; chunks <= KMEM_VOID_FRACTION; chunks++) {
			slabsize = ptob(btopr(chunksize * chunks));
			chunks = slabsize / chunksize;
			maxcolor = slabsize % chunksize;
			waste = maxcolor / chunks;
			if (waste < minwaste) {
				minwaste = waste;
				bestfit = slabsize;
			}
		}
		slabsize = bestfit;
		maxcolor = slabsize % chunksize;
	}

	if (kmem_flags & KMF_NOCOLOR)
		maxcolor = 0;

	cp->cache_slabsize	= slabsize;
	cp->cache_color		= (maxcolor >> 1) & -align;
	cp->cache_maxcolor	= maxcolor;

	if (raw) {
		/*
		 * Buffer auditing can't be applied to raw caches
		 */
		cp->cache_flags &= ~KMF_AUDIT;
	} else {
		cp->cache_hash_table = kmem_page_alloc(1, KM_SLEEP);
		cp->cache_hash_mask = PAGESIZE / sizeof (void *) - 1;
		cp->cache_hash_shift = highbit((u_long)chunksize) - 1;
		copy_pattern((u_long)&kmem_null_bufctl,
			cp->cache_hash_table, PAGESIZE);
		cp->cache_flags |= KMF_HASH;
		kmem_async_dispatch(kmem_hash_rescale, cp, KM_SLEEP);
	}

	for (cpu_seqid = 0; cpu_seqid < cp->cache_ncpus; cpu_seqid++) {
		kmem_cpu_cache_t *ccp = &cp->cache_cpu[cpu_seqid];
		sprintf(namebuf, "%s_cpu_seqid_%d", name, cpu_seqid);
		mutex_init(&ccp->cc_lock, namebuf, MUTEX_ADAPTIVE, NULL);
		ccp->cc_magsize = cp->cache_magazine_size;
		ccp->cc_rounds = -1;	/* no current magazine */
	}

	mutex_enter(&kmem_cache_lock);
	cp->cache_next = cnext = &kmem_null_cache;
	cp->cache_prev = cprev = kmem_null_cache.cache_prev;
	cnext->cache_prev = cp;
	cprev->cache_next = cp;
	mutex_exit(&kmem_cache_lock);

	/*
	 * We can't quite finish creating the cache now because caches can
	 * be created very early in the life of the system.  Specifically
	 * kmem_cache_create() can be called before the total number of
	 * cpus is known and before the kstat framework is initialized.
	 * However, the cache *is* usable at this point, so we can just
	 * direct the kmem async thread (which doesn't come to life until
	 * ncpus is known and kstats are working) to apply the finishing
	 * touches later, when it's safe to do so.
	 */
	kmem_async_dispatch(kmem_cache_create_finish, cp, KM_SLEEP);

	return (cp);
}

static void
kmem_cache_destroy_finish(kmem_cache_t *cp)
{
	int cpu_seqid;

	ASSERT(MUTEX_HELD(&kmem_async_serialize));

	if (cp->cache_kstat)
		kstat_delete(cp->cache_kstat);

	if (cp->cache_flags & KMF_HASH) {
		if (cp->cache_rescale == 0)	/* initial hash table */
			kmem_page_free(cp->cache_hash_table, 1);
		else
			kmem_free(cp->cache_hash_table, sizeof (void *) *
				(cp->cache_hash_mask + 1));
	}

	for (cpu_seqid = 0; cpu_seqid < ncpus; cpu_seqid++)
		mutex_destroy(&cp->cache_cpu[cpu_seqid].cc_lock);

	cv_destroy(&cp->cache_cv);
	mutex_destroy(&cp->cache_depot_lock);
	mutex_destroy(&cp->cache_lock);

	mutex_enter(&kmem_cache_lock);
	cp->cache_next = kmem_cache_freelist;
	kmem_cache_freelist = cp;
	mutex_exit(&kmem_cache_lock);
}

void
kmem_cache_destroy(kmem_cache_t *cp)
{
	/*
	 * Remove the cache from the global cache list (so that no one else
	 * can schedule async events on its behalf), purge the cache, and
	 * then destroy it.  Since the async thread processes requests in
	 * FIFO order we can assume that kmem_cache_destroy_finish() will
	 * not be invoked until all other pending async events for this
	 * cache have completed and the cache is empty.
	 *
	 * Note that we *must* purge the cache synchonously because we have
	 * pointers to the caller's constructor, destructor, and reclaim
	 * routines.  These can either go away (via module unloading) or
	 * cease to make sense (by referring to destroyed client state)
	 * as soon as kmem_cache_destroy() returns control to the caller.
	 */
	mutex_enter(&kmem_cache_lock);
	cp->cache_prev->cache_next = cp->cache_next;
	cp->cache_next->cache_prev = cp->cache_prev;
	mutex_exit(&kmem_cache_lock);

	mutex_enter(&kmem_async_serialize);	/* lock out the async thread */
	kmem_cache_magazine_purge(cp);
	mutex_enter(&cp->cache_lock);
	if (cp->cache_buftotal != 0)
		cmn_err(CE_PANIC, "kmem_cache_destroy: '%s' (%x) not empty",
		    cp->cache_name, (int)cp);
	cp->cache_reclaim = NULL;
	/*
	 * The cache is now dead.  There should be no further activity.
	 * We enforce this by setting land mines in the constructor and
	 * destructor routines that induce a kernel text fault if invoked.
	 */
	cp->cache_constructor = (void (*)(void *, size_t))1;
	cp->cache_destructor = (void (*)(void *, size_t))2;
	mutex_exit(&cp->cache_lock);
	mutex_exit(&kmem_async_serialize);

	kmem_async_dispatch(kmem_cache_destroy_finish, cp, KM_SLEEP);
}

void
kmem_async_thread(void)
{
	kmem_async_t *ap, *anext, *aprev;
	kstat_t *ksp;
	cpu_t *cpup;
	int nk;

	kmem_cache_kstat = kmem_perm_alloc(sizeof (struct kmem_cache_kstat) +
		ncpus * sizeof (struct kmem_cpu_kstat), 0, KM_SLEEP);
	kmem_cpu_kstat = (void *)(kmem_cache_kstat + 1);

	bcopy((void *)&kmem_cache_kstat_template, (void *)kmem_cache_kstat,
		sizeof (struct kmem_cache_kstat));

	mutex_enter(&cpu_lock);
	cpup = cpu_list;
	do {
		kstat_named_t *src = (void *)&kmem_cpu_kstat_template;
		kstat_named_t *dst = (void *)&kmem_cpu_kstat[cpup->cpu_seqid];
		bcopy((void *)src, (void *)dst,
			sizeof (kmem_cpu_kstat_template));
		nk = sizeof (struct kmem_cpu_kstat) / sizeof (kstat_named_t);
		while (--nk >= 0)
			sprintf((dst++)->name, (src++)->name, cpup->cpu_id);
		cpup = cpup->cpu_next;
	} while (cpup != cpu_list);
	mutex_exit(&cpu_lock);

	if ((ksp = kstat_create("unix", 0, "kmem_misc", "kmem",
	    KSTAT_TYPE_NAMED, sizeof (kmem_misc_kstat) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL)) != NULL) {
		ksp->ks_data = &kmem_misc_kstat;
		kstat_install(ksp);
	}

	(void) timeout(kmem_update, 0, kmem_reap_interval);

	mutex_enter(&kmem_async_lock);
	for (;;) {
		while (kmem_async_queue.async_next == &kmem_async_queue)
			cv_wait(&kmem_async_cv, &kmem_async_lock);
		ap = kmem_async_queue.async_next;
		anext = ap->async_next;
		aprev = ap->async_prev;
		aprev->async_next = anext;
		anext->async_prev = aprev;
		mutex_exit(&kmem_async_lock);

		TRACE_3(TR_FAC_KMEM, TR_KMEM_ASYNC_SERVICE_START,
			"kmem_async_service_start:async_entry %x %K(%S)",
			ap, ap->async_func, ap->async_cache->cache_name);

		mutex_enter(&kmem_async_serialize);
		(*ap->async_func)(ap->async_cache);
		mutex_exit(&kmem_async_serialize);

		TRACE_0(TR_FAC_KMEM, TR_KMEM_ASYNC_SERVICE_END,
			"kmem_async_service_end");

		mutex_enter(&kmem_async_lock);
		ap->async_next = kmem_async_freelist;
		kmem_async_freelist = ap;
	}
}

void
kmem_init(void)
{
	int i, size, cache_size;
	kmem_cache_t *cp;
	kmem_magazine_type_t *mtp;

#ifdef DEBUG
	/*
	 * Hack to deal with suninstall brokenness.
	 * Suninstall has to run in 16MB of *total virtual memory*, so if
	 * we've got less than 20MB, don't do memory-intensive kmem debugging.
	 */
	if ((physmem >> (20 - PAGESHIFT)) < 20)
		kmem_flags = 0;
#endif

	mutex_init(&kmem_cache_lock, "kmem_cache_lock", MUTEX_DEFAULT, NULL);
	mutex_init(&kmem_perm_lock, "kmem_perm_lock", MUTEX_DEFAULT, NULL);
	cv_init(&kmem_async_cv, "kmem_async_cv", CV_DEFAULT, NULL);
	mutex_init(&kmem_async_serialize, "kmem_async_serialize",
		MUTEX_DEFAULT, NULL);

	kmem_async_queue.async_next = &kmem_async_queue;
	kmem_async_queue.async_prev = &kmem_async_queue;

	if (kmem_maxraw == -1)		/* i.e., if not patched */
		kmem_maxraw = PAGESIZE / KMEM_VOID_FRACTION - 1;

	/*
	 * We stick the kmem_null_bufctl struct on the tail of every
	 * hash chain, so that we don't have to check for a NULL
	 * pointer on the first lookup (which almost always succeeds).
	 */
	kmem_null_bufctl.bc_addr = (void *)0xdefec8ed;

	kmem_null_cache.cache_next = &kmem_null_cache;
	kmem_null_cache.cache_prev = &kmem_null_cache;

	kmem_maxraw += PAGESIZE;	/* magazine, slab, bufctl MUST be raw */

	kmem_magazine_type[0].mt_maxbuf -= INT_MAX;	/* no mags for mags */

	for (i = 1; i < sizeof (kmem_magazine_type) / sizeof (*mtp); i++) {
		char namebuf[64];
		mtp = &kmem_magazine_type[i];
		sprintf(namebuf, "kmem_magazine_%d", mtp->mt_magsize);
		mtp->mt_cache = kmem_cache_create(namebuf,
			(mtp->mt_magsize + 1) * sizeof (void *),
			mtp->mt_align, NULL, NULL, NULL);
	}

	kmem_magazine_type[0].mt_maxbuf += INT_MAX;

	kmem_slab_cache = kmem_cache_create("kmem_slab_cache",
		sizeof (kmem_slab_t), 4 * sizeof (int), NULL, NULL, NULL);

	kmem_bufctl_cache = kmem_cache_create("kmem_bufctl_cache",
		(kmem_flags & KMF_AUDIT) ?
		sizeof (kmem_bufctl_audit_t) : sizeof (kmem_bufctl_t),
		4 * sizeof (int), NULL, NULL, NULL);

	kmem_maxraw -= PAGESIZE;
	kmem_reap_interval = 15 * HZ;
	kmem_depot_contention = (KMEM_DEPOT_CONTENTION * kmem_reap_interval) /
		(60 * HZ);

	/*
	 * Freed-address verification is a side-effect of the hash lookup in
	 * any hashed cache.  Therefore, to implement KMF_VERIFY debugging for
	 * all caches, all we have to do is set the hashing threshold to zero.
	 * In order to providing audit information we also need to set
	 * the hashing threshold to zero.
	 */
	if (kmem_flags & (KMF_VERIFY | KMF_AUDIT))
		kmem_maxraw = 0;

	if (kmem_flags & KMF_PAGEPERBUF)
		kmem_align = PAGESIZE;

	if (kmem_flags & KMF_AUDIT) {
		/*
		 * Use about 2% of available memory for the transaction log.
		 */
		u_int physmegs = physmem >> (20 - PAGESHIFT);
		u_int virtmegs = (Syslimit - Sysbase) >> 20;
		if (kmem_log_size == 0)
			kmem_log_size = min(physmegs, virtmegs) *
				(20000 / sizeof (kmem_bufctl_audit_t));
		kmem_log_size = 1 << highbit(kmem_log_size);
		kmem_log = kmem_perm_alloc(kmem_log_size *
			sizeof (kmem_bufctl_audit_t), 0, KM_SLEEP);
		bzero((void *)kmem_log,
			kmem_log_size * sizeof (kmem_bufctl_audit_t));
	}

	/*
	 * Set up the default caches to back kmem_alloc()
	 */
	size = KMEM_ALIGN;
	for (i = 0; i < sizeof (kmem_alloc_sizes) / sizeof (int); i++) {
		char name[40];
		int align = KMEM_ALIGN;
		cache_size = kmem_alloc_sizes[i];
		if (cache_size > kmem_maxraw &&
		    (cache_size & (cache_size - 1)) == 0)
			align = cache_size;
		if ((cache_size & PAGEOFFSET) == 0)
			align = PAGESIZE;
		sprintf(name, "kmem_alloc_%d", cache_size);
		cp = kmem_cache_create(name, cache_size, align,
			NULL, NULL, NULL);
		while (size <= cache_size) {
			kmem_alloc_table[(size - 1) >> KMEM_ALIGN_SHIFT] = cp;
			size += KMEM_ALIGN;
		}
	}

	kmem_flags |= KMF_READY;
	kmem_ready = 1;
}
