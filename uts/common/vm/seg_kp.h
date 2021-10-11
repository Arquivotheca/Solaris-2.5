/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

#ifndef	_VM_SEG_KP_H
#define	_VM_SEG_KP_H

#pragma ident	"@(#)seg_kp.h	1.14	94/01/11 SMI"

/*
 * segkp (as in kernel pageable) is a segment driver that supports allocation
 * of page-aligned variable size of vm resources.
 *
 * Each vm resource represents a page-aligned range of virtual addresses.
 * The caller may specify whether the resource should include a redzone,
 * be locked down, or be zero initialized.
 */

#include <vm/seg.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * Private information per overall segkp segment (as opposed
 * to per resource within segment). There are as many anon slots
 * allocated as there there are pages in the segment.
 */
struct segkp_segdata {
	struct anon 	**kpsd_anon;	/* ptr to array of anon structs */
	struct map 	*kpsd_kpmap; 	/* resource map for vaddr allocation */
	struct segkp_data **kpsd_hash;	/* Hash table for lookups */
};

/*
 * A hash table is used to aid in the lookup of a kpd's based on vaddr.
 * Since the heaviest use of segkp occurs from segkp_*get and segkp_*release,
 * the hashing is based on the vaddr used by these routines.
 */
#define	SEGKP_HASHSZ		256	/* power of two */
#define	SEGKP_HASHMASK		(SEGKP_HASHSZ - 1)
#define	SEGKP_HASH(vaddr)	(((u_int)vaddr >> PAGESHIFT) & SEGKP_HASHMASK)

/*
 * Allocate the resource map based on the size of the segkp segment
 */
#define	SEGKP_MAP_SHIFT		13	/* seg->size / (8k) */

struct segkp_data {
	kmutex_t	kp_lock;	/* per resource lock */
	caddr_t		kp_base;	/* starting addr of chunk */
	int		kp_len;		/* # of bytes */
	u_int		kp_flags;	/* state info */
	int		kp_cookie;	/* index into cache array */
	struct anon 	**kp_anon;	/* ptr. to anon array */
	struct segkp_data *kp_next;	/* ptr to next in hash chain */
};

/*
 * Flag bits
 *
 */
#define	KPD_ZERO	0x01		/* initialize resource with 0 */
#define	KPD_LOCKED	0x02		/* resources locked */
#define	KPD_NO_ANON	0x04		/* no swap resources required */
#define	KPD_HASREDZONE	0x08		/* include a redzone */
#define	KPD_NOWAIT	0x10		/* do not wait for res. if unavail. */
#define	KPD_WRITEDIRTY	0x20		/* dirty pages should be flushed */

/*
 * A cache of segkp elements may be created via segkp_cache_init().
 * The elements on the freelist all have the same len and flags value.
 * The cookie passed to the client is an index into the freelist array.
 */
struct segkp_cache  {
	int		kpf_max;		/* max # of elements allowed */
	int		kpf_count;		/* current no. of elments */
	int		kpf_inuse;		/* list inuse */
	u_int		kpf_flags;		/* seg_kp flag value */
	u_int		kpf_len;		/* len of resource */
	struct seg	*kpf_seg;		/* segment */
	struct segkp_data *kpf_list;		/* list of kpd's */
};
#define	SEGKP_MAX_CACHE		4	/* Number of caches maintained */

/*
 * Define redzone, and stack_to_memory macros.
 * The redzone is PAGESIZE bytes.
 */
#ifdef	STACK_GROWTH_DOWN
#define	KPD_REDZONE(kpd)	(0)
#define	stom(v, flags)	(((flags) & KPD_HASREDZONE) ? (v) + PAGESIZE : (v))

#else	/* STACK_GROWTH_DOWN */

#define	KPD_REDZONE(kpd) (btop(kpd->len) - 1)
#define	stom(v)	(v)
#endif	/* STACK_GROWTH_DOWN */

#define	SEGKP_MAPLEN(len, flags) \
	(((flags) & KPD_HASREDZONE) ? (len) - PAGESIZE : (len))

extern	struct seg *segkp;

/*
 * Public routine declarations not part of the segment ops vector go here.
 */
int	segkp_create(struct seg *);
caddr_t	segkp_get(struct seg *, u_int, u_int);
void	segkp_release(struct seg *, caddr_t);
void *  segkp_cache_init(struct seg *, int, u_int, u_int);
void	segkp_cache_free();
caddr_t segkp_cache_get(void *);
int	swapsize(caddr_t);

/*
 * We allow explicit calls to segkp_fault, even though it's part
 * of the segkp ops vector.
 */
faultcode_t segkp_fault(struct hat *, struct seg *, caddr_t, u_int,
	    enum fault_type, enum seg_rw);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_SEG_KP_H */
