/*
 * Copyright (c) 1987, 1990-1993 by Sun Microsystems, Inc.
 */

/*
 * VM - Hardware Address Translation management.
 *
 * This file describes the contents of the sun referernce mmu (sfmmu)
 * specific hat data structures and the sfmmu specific hat procedures.
 * The machine independent interface is described in <vm/hat.h>.
 */

#ifndef	_VM_HAT_SFMMU_H
#define	_VM_HAT_SFMMU_H

#pragma ident	"@(#)hat_sfmmu.h	1.38	95/03/31 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/pte.h>

/*
 * Don't alter these without considering changes to ism_map_t.
 */
#define	ISM_HAT_SHIFT	32
#define	ISM_VB_MASK	0xff00
#define	ISM_SZ_MASK	0xff
#define	ISM_VB_SHIFT	8
#define	ISM_AL_SHIFT	24		/* 16MB */
#define	ISM_MAP_SLOTS	5	/* Change this carefully. */

#ifndef _ASM

#include <sys/t_lock.h>
#include <vm/hat.h>
#include <sys/types.h>
#include <sys/mmu.h>
#include <vm/seg.h>
#include <vm/mach_sfmmu.h>
#include <sys/machparam.h>
#include <sys/x_call.h>

extern int ism_debug;
/*
 * All shared memory segments attached with the SHM_SHARE_MMU flag (ISM)
 * will be constrained to a 16MB alignment. This restriction is not only
 * historical but also for performance. It will allow us to use 8 bits
 * for size and base virtual addresses. Also since every newly created
 * ISM segment is created out of a new address space at base va of 0 we
 * don't need to store it.
 */
#define	ISM_ALIGN	(MMU_PAGESIZE4M * 4)	/* base va aligned to 16MB  */
#define	ISM_ALIGNED(va)	(((uint) va & (ISM_ALIGN - 1)) == 0)
#define	ISM_SHIFT(x) ((u_char) ((uint) x >> (ISM_AL_SHIFT)))

/*
 * Macro to check if object crosses page boundary.
 */
#define	ISM_CROSS_PAGE(va, size)                \
	((((u_int) va & MMU_PAGEOFFSET)         \
	    + size) >= MMU_PAGESIZE)

/*
 * All segments mapped with ISM are guarenteed to be 16MB aligned.
 * Also size is guarenteed to be in 16MB chunks.
 * With this restriction we only need u_char's for ISM segment
 * base address and size.
 *
 * NOTE: Don't alter this structure without changing defines above and
 * the tsb_miss and protection handlers.
 */
typedef struct ism_map {
	struct sfmmu *ism_sfmmu;	/* hat id of dummy ISM as */
	u_short	unused;			/* unused */
	u_char	vbase;			/* base va of ISM in this process */
	u_char	size;			/* size of ISM segment */
} ism_map_t;

/*
 * ISM mapping block. One will be hung off the sfmmu structure if a
 * a process uses ISM.
 *
 * All modifications to fields in this structure will be protected
 * by the hat mutex.
 */
typedef struct ism_map_blk {
	ism_map_t	maps[ISM_MAP_SLOTS];
	struct ism_map_blk *next;	/* Used if more than ISM_MAP_SLOTS  */
					/* segments are attached to this proc */
} ism_map_blk_t;

/*
 * Software context structure.  The size of this structure is currently
 * hardwired into the tsb miss handlers in assembly code through the
 * CTX_SZ_SHIFT define.  Since this define is used in a shift we should keep
 * this structure a power of two.
 *
 * The only flag defined so far is LTTES_FLAG (large ttes).  This currently
 * means that at some point a large page mapping was created.  A future
 * optimization would be to reset the flag when sfmmu->lttecnt becomes
 * zero.
 */
struct ctx {
	union {
		struct sfmmu *c_sfmmup;	/* back pointer to hat id */
		struct ctx *c_freep;	/* next ctx in freelist */
	} c_un;

	u_short c_flags;	/* NOTE: keep c_flags/c_refcnt together */
				/* since we load/store them as an int */
	u_short	c_refcnt;	/* used as rw-lock - for ctx-stealing */
				/* Usg: 0: free, 0xffff: w-lock, >0: r-lock */
	u_longlong_t  c_ismblkpa;
				/* phys ptr to ISM blk. This is only for */
				/* performance. It allows us to service  */
				/* a tsb miss at tl > 0.		 */
				/* NOTE: must be double word aligned	 */
};

#define	c_sfmmu	c_un.c_sfmmup
#define	c_free	c_un.c_freep

#ifdef	DEBUG
/*
 * For debugging purpose only. Maybe removed later.
 */
struct ctx_trace {
	struct	sfmmu	*sc_sfmmu_stolen;
	u_short		sc_type;
	u_short		sc_cnum;
	long		sc_time;
	struct	sfmmu	*sc_sfmmu_stealing;
};
#define	CTX_STEAL	0x1
#define	CTX_FREE	0x0
#define	TRSIZE	0x400
#define	NEXT_CTXTR(ptr)	(((ptr) >= ctx_trace_last) ? \
		ctx_trace_first : ((ptr) + 1))
extern	clock_t lbolt;
#define	TRACE_CTXS(ptr, cnum, stolen_sfmmu, stealing_sfmmu, type)	\
	(ptr)->sc_sfmmu_stolen = (stolen_sfmmu);		\
	(ptr)->sc_sfmmu_stealing = (stealing_sfmmu);		\
	(ptr)->sc_cnum = (cnum);				\
	(ptr)->sc_type = (type);				\
	(ptr)->sc_time = lbolt;					\
	(ptr) = NEXT_CTXTR(ptr);				\
	num_ctx_stolen += (type);
#else

#define	TRACE_CTXS(ptr, cnum, stolen_sfmmu, stealing_sfmmu, type)

#endif	DEBUG

#endif	/* !_ASM */

/*
 * Context flags
 */
#define	LTTES_FLAG	0x0001

#define	CTX_GAP	5
#define	ctxtoctxnum(ctx)	((ctx) - ctxs)

/*
 * Defines needed for ctx stealing.
 */
#define	GET_CTX_RETRY_CNT	100

/*
 * Starting with context 0, the first NUM_LOCKED_CTXS contexts
 * are locked so that sfmmu_getctx can't steal any of these
 * contexts.  At the time this software was being developed, the
 * only context that needs to be locked is context 0 (the kernel
 * context), and context 1 (reserved for stolen context). So this constant
 * was originally defined to be 2.
 */
#define	NUM_LOCKED_CTXS 2
#define	INVALID_CONTEXT	1

#ifndef	_ASM

struct sf_hment {
	struct hment gen_hme;	/* generic hment, keep this as the first */
				/* field as the code expects it to be */
				/* this way while evaluating pointers */
	tte_t hme_tte;		/* tte for this hment */
};

#define	hme_page gen_hme.hme_page
#define	hme_next gen_hme.hme_next
#define	hme_hat gen_hme.hme_hat

/*
 * hmeblk_tag structure
 * structure used to obtain a match on a hme_blk.  Currently consists of
 * the address of the sfmmu struct (or hatid), the base page address of the
 * hme_blk, and the rehash count.  The rehash count is actually only 2 bits
 * and has the following meaning:
 * 1 = 8k or 64k hash sequence.
 * 2 = 512k hash sequence.
 * 3 = 4MB hash sequence.
 * We require this count because we don't want to get a false hit on a 512K or
 * 4MB rehash with a base address corresponding to a 8k or 64k hmeblk.
 * Note:  The ordering and size of the hmeblk_tag members are implictly known
 * by the tsb miss handlers written in assembly.  Do not change this structure
 * without checking those routines.  See HTAG_SFMMUPSZ define.
 */
typedef union {
	struct {
		u_int		hblk_basepg: 19,	/* hme_blk base pg # */
				hblk_rehash: 13;	/* rehash number */
		struct sfmmu	*sfmmup;
	} hblk_tag_un;
	u_longlong_t	htag_tag;
} hmeblk_tag;
#define	htag_id		hblk_tag_un.sfmmup
#define	htag_bspage	hblk_tag_un.hblk_basepg
#define	htag_rehash	hblk_tag_un.hblk_rehash

#endif /* !_ASM */

#define	NHMENTS		8		/* # of hments in an 8k hme_blk */
					/* needs to be multiple of 2 */
#ifndef	_ASM

#ifdef	SFMMU_TRACE

#define	SFMMU_STACK_DEPTH	15
#define	SFMMU_STACK_TRACE(hmeblkp, hmenum)				\
	if (!(hmenum)) {						\
		(hmeblkp)->hblk_thread = curthread;			\
		((hmeblkp)->hblk_depth =				\
		getpcstack((hmeblkp)->hblk_pctrace, SFMMU_STACK_DEPTH)); \
	}

#else

#define	SFMMU_STACK_TRACE(hmeblkp, hmenum)

#endif	/* SFMMU_TRACE */


/*
 * Hment block structure.
 * The hme_blk is the node data structure which the hash structure
 * mantains. An hme_blk can have 2 different sizes depending on the
 * number of hments it implicitly contains.  When dealing with 64K, 512K,
 * or 4MB hments there is one hment per hme_blk.  When dealing with
 * 8k hments we allocate an hme_blk plus an additional 7 hments to
 * give us a total of 8 (NHMENTS) hments that can be referenced through a
 * hme_blk.
 *
 * The hmeblk structure contains 2 tte reference counters used to determine if
 * it is ok to free up the hmeblk.  Both counters have to be zero in order
 * to be able to free up hmeblk.  They are protected by cas.
 * hblk_hmecnt is the number of hments present on pp mapping lists.
 * hblk_vcnt reflects number of valid ttes in hmeblk.
 *
 * The hmeblk now also has per tte lock cnts.  This is required because
 * the counts can be high and there are not enough bits in the tte. When
 * physio is fixed to not lock the translations we should be able to move
 * the lock cnt back to the tte.  See bug id 1198554.
 */
struct hme_blk {
	u_longlong_t	hblk_nextpa;	/* physical address for hash list */

	hmeblk_tag	hblk_tag;	/* tag used to obtain an hmeblk match */

	struct hme_blk	*hblk_next;	/* on free list or on hash list */
					/* protected by hash lock */

	struct hme_blk	*hblk_shadow;	/* pts to shadow hblk */
					/* protected by hash lock */

	u_int		hblk_span;	/* span of memory hmeblk maps */

	struct {
		u_short	locked_cnt;	/* HAT_LOCK ref cnt */
		u_int	notused:12;
		u_int	shadow_bit:1;	/* set for a shadow hme_blk */
		u_int	nucleus_bit:1;	/* set for a nucleus hme_blk */
		u_int	ttesize:2;	/* contains ttesz of hmeblk */
	} hblk_misc;

	union {
		struct {
			u_short	hblk_hmecount;	/* hment on mlists counter */
			u_short	hblk_validcnt;	/* valid tte reference count */
		} hblk_counts;
		u_int		hblk_shadow_mask;
	} hblk_un;

#ifdef	SFMMU_TRACE
	u_char		hblk_depth;
	kthread_id_t	hblk_thread;
	u_int		hblk_pctrace[SFMMU_STACK_DEPTH];
#endif	/* SFMMU_AUDIT */

	struct sf_hment hblk_hme[1];	/* hment array */
};

#define	hblk_lckcnt	hblk_misc.locked_cnt
#define	hblk_shw_bit	hblk_misc.shadow_bit
#define	hblk_nuc_bit	hblk_misc.nucleus_bit
#define	hblk_ttesz	hblk_misc.ttesize
#define	hblk_hmecnt	hblk_un.hblk_counts.hblk_hmecount
#define	hblk_vcnt	hblk_un.hblk_counts.hblk_validcnt
#define	hblk_shw_mask	hblk_un.hblk_shadow_mask

#define	MAX_HBLK_LCKCNT	0xFFFF
#define	HMEBLK_ALIGN	0x8		/* hmeblk has to be double aligned */

#define	HMEHASH_FACTOR	16	/* used to calc # of buckets in hme hash */
/*
 * A maximum number of user hmeblks is defined in order to place an upper
 * limit on how much nuclues memory is required.  The number below
 * corresponds to the number of buckets for an avergae length of 4 in
 * a 16 machine. XXX We need to redo hat startup for the next release.
 * The hmeblk stealer will eliminate the need for nucleus hme_blks.
 * It should be easy to change the tsb miss handler to use a physical
 * hash table.  This will eliminate the largest consumers of nucleus
 * memory and supporting more physical memory would then be trivial.
 */
#define	MAX_UHME_BUCKETS 0x4000

struct hmehash_bucket {
	kmutex_t	hmehash_mutex;
	u_longlong_t	hmeh_nextpa;	/* physical address for hash list */
	struct hme_blk *hmeblkp;
};

#endif /* !_ASM */

#define	CTX_SZ_SHIFT	4

/*
 * The tsb miss handlers written in assembly know that sfmmup is a 32 bit ptr.
 */
#define	HTAG_SFMMUPSZ		32
#define	HTAG_REHASHSZ		13

/*
 * Assembly routines need to be able to get to ttesz
 */
#define	HBLK_SZMASK		0x3

#ifndef _ASM

/*
 * Returns the number of bytes that an hmeblk spans given its tte size
 */
#define	get_hblk_span(hmeblkp) ((hmeblkp)->hblk_span)
#define	get_hblk_ttesz(hmeblkp)	((hmeblkp)->hblk_ttesz)
#define	HMEBLK_SPAN(ttesz)						\
	((ttesz == TTE8K)? (TTEBYTES(ttesz) * NHMENTS) : TTEBYTES(ttesz))

#define	set_hblk_sz(hmeblkp, ttesz)				\
	(hmeblkp)->hblk_ttesz = (ttesz);			\
	(hmeblkp)->hblk_span = HMEBLK_SPAN(ttesz)

#define	get_hblk_base(hmeblkp)	((hmeblkp)->hblk_tag.htag_bspage <<	\
		MMU_PAGESHIFT)

#define	get_hblk_endaddr(hmeblkp)				\
	((caddr_t)(get_hblk_base(hmeblkp) + get_hblk_span(hmeblkp)))

#define	in_hblk_range(hmeblkp, vaddr)					\
	(((u_int)(vaddr) >= get_hblk_base(hmeblkp)) &&			\
	((u_int)(vaddr) < (get_hblk_base(hmeblkp) +			\
	get_hblk_span(hmeblkp))))

#define	tte_to_vaddr(hmeblkp, tte)	((caddr_t)(get_hblk_base(hmeblkp) \
	+ (TTEBYTES((tte).tte_size) * (tte).tte_hmenum)))

#define	vaddr_to_vshift(hblktag, vaddr, shwsz)				\
	((((uint)(vaddr) >> MMU_PAGESHIFT) - (hblktag.htag_bspage)) >>	\
	TTE_BSZS_SHIFT((shwsz) - 1))

/*
 * Hment pool
 * The hment pool consists of 4 different linked lists of free hme_blks.
 * I need to differentiate between hmeblks having 8 vs 1 hment and between
 * nucleus and non-nucleus hmeblks.  That gives 4 lists.
 */

/*
 * The nucleus hmeblks can originate from various chunks of
 * nucleus memory.  We don't want to divide these chunks into the 2 possible
 * free lists at boot time instead do it on a demand basis.  When we require
 * a nucleus hmeblk and there is none in the corresponding free list we go
 * and substract it from a chunk of nucleus memory.  This chunks are managed
 * as the nucleus_memlist.
 */
#define	N_MLIST_NUM	3		/* # of nucleus_memlist elements */
struct nucleus_memlist {
	caddr_t	base;		/* base address */
	int	size;		/* size in bytes */
};
#define	HME8BLK_SZ	(sizeof (struct hme_blk) + \
			(NHMENTS - 1) * sizeof (struct sf_hment))

#define	HME1BLK_SZ	(sizeof (struct hme_blk))

#define	HME1_TRHOLD	15	/* threshold which triggers the */
				/* allocation of more hme1_blks */

#define	HME8_TRHOLD	15	/* threshold which triggers the */
				/* allocation of more hme8_blks */

#define	BKHME_TRHOLD	10	/* threshold which triggers the */
				/* allocation of more bkhme_blks */

#define	BKHMEGROW_NUM	10	/* max number of hme_blks to grow */
				/* bkhme_freelist each time we cross above */
				/* threshold. min is 1. */

#define	HBLK_GROW_NUM	30	/* number of hmeblks to kmem_alloc at a time */

/*
 * We have 2 mutexes to protect the hmeblk free lists.
 * One mutex protects the non-nucleus freelists and the other
 * mutex protects the nucleus freelist.
 */
#define	HBLK_FLIST_LOCK()	(mutex_enter(&hblk_lock))
#define	HBLK_FLIST_UNLOCK()	(mutex_exit(&hblk_lock))
#define	HBLK_FLIST_ISHELD()	(mutex_owned(&hblk_lock))

#define	NHBLK_FLIST_LOCK()	(mutex_enter(&nhblk_lock))
#define	NHBLK_FLIST_UNLOCK()	(mutex_exit(&nhblk_lock))
#define	NHBLK_FLIST_ISHELD()	(mutex_owned(&nhblk_lock))

/*
 * Hme_blk hash structure
 * Active mappings are kept in a hash structure of hme_blks.  The hash
 * function is based on (ctx, vaddr) The size of the hash table size is a
 * power of 2 such that the average hash chain lenth is HMENT_HASHAVELEN.
 * The hash actually consists of 2 separate hashes.  One hash is for the user
 * address space and the other hash is for the kernel address space.
 * The number of buckets are calculated at boot time and stored in the global
 * variables "uhmehash_num" and "khmehash_num".  By making the hash table size
 * a power of 2 we can use a simply & function to derive an index instead of
 * a divide.
 *
 * HME_HASH_FUNCTION(hatid, vaddr, shift) returns a pointer to a hme_hash
 * bucket.
 * An hme hash bucket contains a pointer to an hme_blk and the mutex that
 * protects the link list.
 * Spitfire supports 4 page sizes.  8k and 64K pages only need one hash.
 * 512K pages need 2 hashes and 4MB pages need 3 hashes.
 * The 'shift' parameter controls how many bits the vaddr will be shifted in
 * the hash function. It is calculated in the HME_HASH_SHIFT(ttesz) function
 * and it varies depending on the page size as follows:
 *	8k pages:  	HBLK_RANGE_SHIFT
 *	64k pages:	MMU_PAGESHIFT64K
 *	512K pages:	MMU_PAGESHIFT512K
 *	4MB pages:	MMU_PAGESHIFT4M
 * An assembly version of the hash function exists in sfmmu_ktsb_miss(). All
 * changes should be reflected in both versions.  This function and the TSB
 * miss handlers are the only places which know about the two hashes.
 *
 * HBLK_RANGE_SHIFT controls range of virtual addresses that will fall
 * into the same bucket for a particular process.  It is currently set to
 * be equivalent to 64K range or one hme_blk.
 *
 * The hme_blks in the hash are protected by a per hash bucket mutex
 * known as SFMMU_HASH_LOCK.
 * You need to acquire this lock before traversing the hash bucket link
 * list, while adding/removing a hme_blk to the list, and while
 * modifying an hme_blk.  A possible optimization is to replace these
 * mutexes by readers/writer lock but right now it is not clear whether
 * this is a win or not.
 *
 * The HME_HASH_TABLE_SEARCH will search the hash table for the
 * hme_blk that contains the hment that corresponds to the passed
 * ctx and vaddr.  It assumed the SFMMU_HASH_LOCK is held.
 */

#endif /* ! _ASM */

#define	KHATID			ksfmmup
#define	UHMEHASH_SZ		uhmehash_num
#define	KHMEHASH_SZ		khmehash_num
#define	HMENT_HASHAVELEN	4
#define	HBLK_RANGE_SHIFT	MMU_PAGESHIFT64K /* shift for HBLK_BS_MASK */
#define	MAX_HASHCNT		3

#ifndef _ASM

#define	HASHADDR_MASK(hashno)	TTE_PAGEMASK(hashno)

#define	HME_HASH_SHIFT(ttesz)						\
	((ttesz == TTE8K)? HBLK_RANGE_SHIFT : TTE_PAGE_SHIFT(ttesz))	\

#define	HME_HASH_ADDR(vaddr, hmeshift)					\
	((caddr_t)(((uint)(vaddr) >> (hmeshift)) << (hmeshift)))

#define	HME_HASH_BSPAGE(vaddr, hmeshift)				\
	(((uint)(vaddr) >> (hmeshift)) << ((hmeshift) - MMU_PAGESHIFT))

#define	HME_HASH_REHASH(ttesz)						\
	(((ttesz) < TTE512K)? 1 : (ttesz))

#define	HME_HASH_FUNCTION(hatid, vaddr, shift)				\
	((hatid != KHATID)?						\
	(&uhme_hash[ (((u_int)(hatid) ^ ((u_int)vaddr >> (shift)))	\
		& UHMEHASH_SZ) ]):					\
	(&khme_hash[ (((u_int)(hatid) ^ ((u_int)vaddr >> (shift)))	\
		& KHMEHASH_SZ) ]))

/*
 * This macro will traverse a hmeblk hash link list looking for an hme_blk
 * that owns the specified vaddr and hatid.  If if doesn't find one , hmeblkp
 * will be set to NULL, otherwise it will point to the correct hme_blk.
 * This macro also cleans empty hblks.
 */
#define	HME_HASH_SEARCH(hmebp, hblktag, hblkp, nx_hblk, pr_hblk)	\
	ASSERT(SFMMU_HASH_LOCK_ISHELD(hmebp));				\
	hblkp = hmebp->hmeblkp;						\
	while (hblkp) {							\
		if (hblkp->hblk_tag.htag_tag == hblktag.htag_tag) {	\
			/* found hme_blk */				\
			break;						\
		}							\
		nx_hblk = hblkp->hblk_next;				\
		if (!hblkp->hblk_vcnt && !hblkp->hblk_hmecnt &&		\
		    !hblkalloc_inprog) {				\
			sfmmu_hblk_hash_rm(hmebp, hblkp, pr_hblk);	\
			sfmmu_hblk_free(hmebp, hblkp);			\
		} else {						\
			pr_hblk = hblkp;				\
		}							\
		hblkp = nx_hblk;					\
	}								\

/*
 * This macro will traverse a hmeblk hash link list looking for an hme_blk
 * that owns the specified vaddr and hatid.  If if doesn't find one , hmeblkp
 * will be set to NULL, otherwise it will point to the correct hme_blk.
 * It doesn't remove empty hblks.
 */
#define	HME_HASH_FAST_SEARCH(hmebp, hblktag, hblkp)			\
	ASSERT(SFMMU_HASH_LOCK_ISHELD(hmebp));				\
	for (hblkp = hmebp->hmeblkp; hblkp;				\
	    hblkp = hblkp->hblk_next) {					\
		if (hblkp->hblk_tag.htag_tag == hblktag.htag_tag) {	\
			/* found hme_blk */				\
			break;						\
		}							\
	}								\


#define	SFMMU_HASH_LOCK(hmebp)						\
		(mutex_enter(&hmebp->hmehash_mutex))

#define	SFMMU_HASH_UNLOCK(hmebp)					\
		(mutex_exit(&hmebp->hmehash_mutex))

#define	SFMMU_HASH_LOCK_TRYENTER(hmebp)					\
		(mutex_tryenter(&hmebp->hmehash_mutex))

#define	SFMMU_HASH_LOCK_ISHELD(hmebp)					\
		(mutex_owned(&hmebp->hmehash_mutex))

/*
 * The sfmmu structure is the mmu dependent hardware address translation
 * structure linked to the address space structure to show the translation.
 * provided by the sfmmu for an address space.  The sfmmu structure is embedded
 * as part of the hat but all accesses to it should be done through the
 * specified macros in order to make this transparent.  The only place
 * that doesn't use this macros and is dependent on this is resume in swtch.s
 * The tte counters should be incremented/decremented using cas only.
 */
struct sfmmu {
	ism_map_blk_t	*sfmmu_ismblk;	/* ISM mapping blk. usually NULL */
	u_short		sfmmu_lttecnt;	/* # of large mappings in this hat */
	short		sfmmu_cnum;	/* context number */
	cpuset_t	sfmmu_cpusran;	/* cpu bit mask for efficient xcalls */
	u_int		sfmmu_free:1;	/* hat to be freed - set on as_free */
	u_char		sfmmu_clrstart;	/* start color bin for page coloring */
	u_short		sfmmu_clrbin;	/* per as phys page coloring bin */
};

#define	hattosfmmu(hat)		((struct sfmmu *)&(hat->hat_data[0]))
#define	astosfmmu(as)		(hattosfmmu((as)->a_hat))
#define	sfmmutoctxnum(sfmmup)	((sfmmup)->sfmmu_cnum)
#define	sfmmutoctx(sfmmup)	(&ctxs[sfmmutoctxnum(sfmmup)])
#define	astoctxnum(as)		(sfmmutoctxnum(astosfmmu(as)))
#define	astoctx(as)		(sfmmutoctx(astosfmmu(as)))
#define	hblktosfmmu(hmeblkp)	((hmeblkp)->hblk_tag.htag_id)
#define	ctxnumtoctx(ctxnum)	(&ctxs[ctxnum])
#define	sfhmetohat(sfhmep)	(&hats[sfhmep->hme_hat])
#define	sfhmetoas(sfhmep)	(sfhmetohat(sfhmep)->hat_as)
#define	sfhme_is_sfmmu(sfhmep)	(sfhmetohat(sfhmep)->hat_op == &sfmmu_hatops)
#define	hat_is_sfmmu(hatp)	((hatp)->hat_op == &sfmmu_hatops)
/*
 * We use the sfmmu data structure to keep the per as page coloring info.
 */
#define	as_color_bin(as)	(astosfmmu(as)->sfmmu_clrbin)
#define	as_color_start(as)	(astosfmmu(as)->sfmmu_clrstart)

/*
 * ctx, hmeblk, mlistlock and other stats for sfmmu
 */
struct vmhatstat {
	kstat_named_t	vh_ctxfree;		/* ctx alloced without steal */
	kstat_named_t	vh_ctxsteal;		/* ctx allocated by steal */

	kstat_named_t	vh_tteload;		/* calls to sfmmu_tteload */
	kstat_named_t	vh_hblk_hit;		/* found hblk during tteload */
	kstat_named_t	vh_hblk_dalloc;		/* alloc dynamic hmeblk */
	kstat_named_t	vh_hblk_nalloc;		/* alloc nucleus hmeblk */

	kstat_named_t	vh_pgcolor_conflict;	/* VAC conflict resolution */
	kstat_named_t	vh_uncache_conflict;	/* VAC conflict resolution */
	kstat_named_t	vh_unload_conflict;	/* VAC unload resolution */

	kstat_named_t	vh_mlist_enter;		/* calls to mlist_lock_enter */
	kstat_named_t	vh_mlist_exit;		/* calls to mlist_lock_exit */
	kstat_named_t	vh_pagesync;		/* # of pagesyncs */
	kstat_named_t	vh_pagesync_invalid;	/* pagesync with inv tte */
	kstat_named_t	vh_itlb_misses;		/* # of itlb misses */
	kstat_named_t	vh_dtlb_misses;		/* # of dtlb misses */
	kstat_named_t	vh_utsb_misses;		/* # of user tsb misses */
	kstat_named_t	vh_ktsb_misses;		/* # of kernel tsb misses */
	kstat_named_t	vh_tsb_hits;		/* # of tsb hits */
	kstat_named_t	vh_umod_faults;		/* # of mod (prot viol) flts */
	kstat_named_t	vh_kmod_faults;		/* # of mod (prot viol) flts */
	kstat_named_t	vh_slow_tsbmiss;	/* # of slow tsb misses */
	kstat_named_t	vh_pagefaults;		/* # of pagefaults */
	kstat_named_t	vh_uhash_searches;	/* # of user hash searches */
	kstat_named_t	vh_uhash_links;		/* # of user hash links */
	kstat_named_t	vh_khash_searches;	/* # of kernel hash searches */
	kstat_named_t	vh_khash_links;		/* # of kernel hash links */
};

#define	SFMMU_STAT_GATHER
#ifdef SFMMU_STAT_GATHER
#define	SFMMU_STAT(stat)	vmhatstat.stat.value.ul++;
#else
#define	SFMMU_STAT(stat)
#endif

/*
 * These should be in hat.h because all hats should use them. Currently,
 * hat_refmod.c uses this info in a hardcoded fashion without #defines.
 * They are here 'cos until someone who owns common/vm puts them in hat.h- XXX
 */
#define	HAT_STAT_REF	0x02
#define	HAT_STAT_MOD	0x01

/*
 * SFMMU FLAGS.
 * apart from the generic hat flags we also specify the following
 * sfmmu specific flags.
 */
#define	SFMMU_UNCACHEPTTE	0x10000		/* unencache in physical $ */
#define	SFMMU_UNCACHEVTTE	0x20000		/* unencache in virtual $ */
#define	SFMMU_SIDEFFECT		0x40000		/* set side effect bit */
#define	SFMMU_NO_TSBLOAD	0x80000		/* do not preload tsb */

/*
 * TSB related structures
 *
 * The TSB is made up of tte entries.  Both the tag and data are present
 * in the TSB.  The TSB locking is managed as follows:
 * A software bit in the tsb tag is used to indicate that entry is locked.
 * If a cpu servicing a tsb miss reads a locked entry the tag compare will
 * fail forcing the cpu to go to the hat hash for the translation.
 * The cpu who holds the lock can then modify the data side, and the tag side.
 * The last write should be to the word containing the lock bit which will
 * clear the lock and allow the tsb entry to be read.  It is assumed that all
 * cpus reading the tsb will do so with atomic 128-bit loads.  An atomic 128
 * bit load is required to prevent the following from happening:
 *
 * cpu 0			cpu 1			comments
 *
 * ldx tag						tag unlocked
 *				ldstub lock		set lock
 *				stx data
 *				stx tag			unlock
 * ldx tag						incorrect tte!!!
 *
 * The software also maintains a bit in the tag to indicate an invalid
 * tsb entry.  The purpose of this bit is to allow the tsb invalidate code
 * to invalidate a tsb entry with a single cas.  See code for details.
 */

union tsb_tag {
	struct {
		unsigned	tag_g:1;	/* copy of tte global bit */
		unsigned	tag_inv:1;	/* sw - invalid tsb entry */
		unsigned	tag_lock:1;	/* sw - locked tsb entry */
		unsigned	tag_cnum:13;	/* context # for comparison */
		unsigned	tag_res1:6;
		unsigned	tag_va_hi:10;	/* va[63:54] */
		unsigned	tag_va_lo;	/* va[53:22] */
	} tagbits;
	struct {
		uint		inthi;
		uint		intlo;
	} tagints;
};
#define	tag_global		tagbits.tag_g
#define	tag_invalid		tagbits.tag_inv
#define	tag_locked		tagbits.tag_lock
#define	tag_ctxnum		tagbits.tag_cnum
#define	tag_vahi		tagbits.tag_va_hi
#define	tag_valo		tagbits.tag_va_lo
#define	tag_inthi		tagints.inthi
#define	tag_intlo		tagints.intlo

struct tsbe {
	union tsb_tag	tte_tag;
	tte_t		tte_data;
};

#endif /* !_ASM */

/*
 * The TSB
 * The first implentation of the TSB will be a 512K common TSB.
 * This means we have a total of 0x8000 or 32k entries.
 */
#define	TSB_SPLIT_CODE		TSB_COMMON_CONFIG
#define	TSB_SIZE_CODE		6	/* 512K common TSB */
#define	TSB_ENTRY_SHIFT		4	/* each entry = 128 bits = 16 bytes */
#define	TSB_ENTRIES		(1 << (9 + TSB_SIZE_CODE + TSB_SPLIT_CODE))
#define	TSB_BYTES		(TSB_ENTRIES << TSB_ENTRY_SHIFT)
#define	TSB_OFFSET_MASK		(TSB_ENTRIES - 1)
#define	TSB_CTX_SHIFT		1	/* used in hashing of ctx in tsb */

#define	TAG_VALO_SHIFT		22		/* tag's va are bits 63-22 */
/*
 * sw bits used on tsb_tag - bit masks used only in assembly
 * use only a sethi for these fields.
 */
#define	TSBTAG_CTXMASK	0x1fff0000
#define	TSBTAG_CTXSHIFT	16
#define	TSBTAG_INVALID	0x40000000		/* tsb_tag.tag_invalid */
#define	TSBTAG_LOCKED	0x20000000		/* tsb_tag.tag_locked */

#ifndef _ASM

/*
 * Page coloring
 * The p_vcolor field of the page struct (1 byte) is used to store the
 * virtual page color.  This provides for 255 colors.  The value zero is
 * used to mean the page has no color - never been mapped or somehow
 * purified.
 */

#define	PP_GET_VCOLOR(pp)	(((pp)->p_vcolor) - 1)
#define	PP_NEWPAGE(pp)		(!((pp)->p_vcolor))
#define	PP_SET_VCOLOR(pp, color)                                          \
	(((pp)->p_vcolor = ((color) + 1)))



#define	addr_to_vcolor(addr)						\
	(((uint)(addr) & (shm_alignment - 1)) >> MMU_PAGESHIFT)

/*
 * functions known to non-vm routines.
 * XXX Should be moved to hat.h
 */
extern u_longlong_t va_to_pa(caddr_t);
extern u_int va_to_pfn(caddr_t);

/*
 * prototypes for hat assembly routines
 */
extern void	sfmmu_tlbflush_page(caddr_t, int);
extern void	sfmmu_tlbflush_ctx(int);
extern void	sfmmu_tlbflush_page_tl1(caddr_t, int);
extern u_int	sfmmu_ctx_steal_tl1(int, int);
extern void	sfmmu_tlbflush_ctx_tl1(int);
extern void	sfmmu_tlbcache_flushpage_tl1(caddr_t, int, uint);
extern void	sfmmu_xcall_sync_tl1(void);
extern void	sfmmu_itlb_ld(caddr_t, int, tte_t *);
extern void	sfmmu_dtlb_ld(caddr_t, int, tte_t *);
extern void	sfmmu_copytte(tte_t *, tte_t *);
extern int	sfmmu_modifytte(tte_t *, tte_t *, tte_t *);
extern int	sfmmu_modifytte_try(tte_t *, tte_t *, tte_t *);
extern void	itlb_rd_entry();
extern void	dtlb_rd_entry();
extern remap_kernel();
extern uint	sfmmu_ttetopfn(tte_t *, caddr_t);

extern uint	get_color_flags(struct as *);
extern int	sfmmu_get_ppvcolor(struct page *);
extern void	sfmmu_hblk_hash_rm(struct hmehash_bucket *, struct hme_blk *,
			struct hme_blk *);
extern void	sfmmu_hblk_hash_add(struct hmehash_bucket *, struct hme_blk *);

/*
 * functions known to hat_sfmmu.c
 */
void	sfmmu_disallow_ctx_steal(struct sfmmu *);
void	sfmmu_allow_ctx_steal(struct sfmmu *);


/*
 * functions and data known to mach_sfmmu.c
 */
extern int	getctx();
extern u_int	sfmmu_vatopfn(caddr_t, struct sfmmu *);
extern u_int	sfmmu_user_vatopfn(caddr_t, struct sfmmu *);
extern void	sfmmu_memtte(tte_t *, u_int, u_int, int, int);
extern void	sfmmu_tteload(struct hat *, tte_t *, caddr_t, struct page *,
			u_int);
extern void	sfmmu_init_tsb(caddr_t, u_int);
extern void	sfmmu_load_tsb(caddr_t, int, tte_t *);
extern void	sfmmu_unload_tsb(caddr_t, int, int);
extern int	sfmmu_getctx_pri();
extern int	sfmmu_getctx_sec();
extern void	sfmmu_setctx_pri(int);
extern void	sfmmu_setctx_sec(int);
extern uint	sfmmu_get_dsfar();
extern uint	sfmmu_get_isfsr();
extern uint	sfmmu_get_dsfsr();
extern void	mmu_set_itsb(caddr_t, uint, uint);
extern void	mmu_set_dtsb(caddr_t, uint, uint);
extern void	mmu_invalidate_tsbe(struct tsbe *, union tsb_tag *);
extern void	mmu_update_tsbe(struct tsbe *, tte_t *, union tsb_tag *);
extern void	obp_remove_locked_ttes();
extern void	mmu_unload_tsbctx(uint);

extern struct sfmmu 	*ksfmmup;
extern caddr_t		tsb_base;
extern struct ctx	*ctxs, *ectxs;
extern u_int		nctxs;

#endif /* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_HAT_SFMMU_H */
