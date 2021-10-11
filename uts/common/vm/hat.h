/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986, 1987, 1988, 1989, 1990, 1994  Sun Microsystems, Inc
 *	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

#ifndef	_VM_HAT_H
#define	_VM_HAT_H

#pragma ident	"@(#)hat.h	1.58	95/10/24 SMI"
/*	From:	SVr4.0	"kernel:vm/hat.h	1.9"		*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <vm/faultcode.h>
#include <vm/devpage.h>
#include <sys/kstat.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * VM - Hardware Address Translation management.
 *
 * This file describes the machine independent interfaces to
 * the hardware address translation management routines.  Other
 * machine specific interfaces and structures are defined
 * in <vm/hat_xxx.h>.  The hat layer manages the address
 * translation hardware as a cache driven by calls from the
 * higher levels of the VM system.
 */


#define	HAT_PRIVSIZ 4		/* number of words of private data storage */

struct hat {
	struct	hatops	*hat_op;	/* public ops for this hat */
	struct	hat	*hat_next;	/* for address space list */
	struct	as	*hat_as;	/* as this hat provides mapping for */
	u_int	hat_data[HAT_PRIVSIZ];	/* private data optimization */
	kmutex_t	hat_mutex;	/* protects hat, hatops */
};

/*
 * We can't include <vm/page.h> because it includes <vm/hat.h>
 * Therefore we just declare 'page' to be a structure tag.
 * It will be defined when <vm/page.h> is included.
 * We treat certain other structure tags the same way.
 */
struct page;
struct hment;
struct as;

/*
 * The hat operations
 */
struct hatops {
	void		(*h_init)(void);
	void		(*h_alloc)(struct hat *, struct as *);

	struct as *	(*h_setup)(struct as *, int);
	void		(*h_free)(struct hat *, struct as *);
	int		(*h_dup)(struct hat *, struct as *, struct as *);
	void		(*h_swapin)(struct hat *, struct as *);
	void		(*h_swapout)(struct hat *, struct as *);

	void		(*h_memload)(struct hat *, struct as *, caddr_t, \
			    struct page *, u_int, int);
	void		(*h_devload)(struct hat *, struct as *, caddr_t, \
			    devpage_t *, u_int, u_int, int);
	void		(*h_contig_memload)(struct hat *, struct as *, \
			    caddr_t, struct page *, u_int, int, u_int);
	void		(*h_contig_devload)(struct hat *, struct as *, \
			    caddr_t, devpage_t *, u_int, u_int, int, u_int);
	void		(*h_unlock)(struct hat *, struct as *, caddr_t, u_int);
	faultcode_t	(*h_fault)(struct hat *, caddr_t);

	void		(*h_chgprot)(struct as *, caddr_t, u_int, u_int);
	void		(*h_unload)(struct as *, caddr_t, u_int, int);
	void		(*h_sync)(struct as *, caddr_t, u_int, u_int);

	void		(*h_pageunload)(struct page *, struct hment *);
	int		(*h_pagesync)(struct hat *, struct page *, \
			    struct hment *, u_int);
	void		(*h_pagecachectl)(struct page *, u_int);

	u_int		(*h_getkpfnum)(caddr_t);
	u_int		(*h_getpfnum)(struct as *, caddr_t);

	int		(*h_map)(struct hat *, struct as *, caddr_t,
				u_int, int);
	int		(*h_probe)(struct hat *, struct as *, caddr_t);
	void		(*h_lock_init)();
	int		(*h_share)(struct as *, caddr_t, struct as *, \
			    caddr_t, u_int);
	void		(*h_unshare)(struct as *, caddr_t, u_int);
	void		(*h_unmap)(struct as *, caddr_t, u_int, int);
};

/*
 * The entries of the table of hat types.
 */
struct hatsw {
	char		*hsw_name;	/* type name string */
	struct hatops	*hsw_ops;	/* hat operations vector */
};

extern	struct hatsw hattab[];
extern	int nhattab;			/* # of entries in hattab array */

/*
 * The hment entry, hat mapping entry.
 * The mmu independent translation on the mapping list for a page
 */
struct hment {
	struct	page *hme_page;		/* what page this maps */
	struct	hment *hme_next;	/* next hment */
	u_int	hme_hat : 16;		/* index into hats */
	u_int	hme_impl : 8;		/* implementation hint */
	u_int	hme_notused : 2;	/* extra bits */
	u_int	hme_prot : 2;		/* protection */
	u_int	hme_noconsist : 1;	/* mapping can't be kept consistent */
	u_int	hme_ncpref: 1;		/* consistency resolution preference */
	u_int	hme_nosync : 1;		/* ghost unload flag */
	u_int	hme_valid : 1;		/* hme loaded flag */
	struct	hment *hme_prev;	/* prev hment */

};

#ifdef	_KERNEL

/*
 * One time hat initialization
 */
void	hat_init();

/*
 * Operations on hat resources for an address space:
 *	- initialize any needed hat structures for the address space
 *	- make an address space context the current active one
 *	- free all hat resources now owned by this address space
 *	- initialize any needed hat structures when the process is
 *	  swapped in.
 *	- free all hat resources that are not needed while the process
 *	  is swapped out.
 *	- dup any hat resources that can be created when duplicating
 *	  another process' address space.
 *
 * N.B. - The hat structure is guaranteed to be zeroed when created.
 * The hat layer can choose to define hat_alloc as a macro to avoid
 * a subroutine call if this is sufficient initialization.
 */
void		hat_lock_init();
struct hat *	hat_alloc(struct as *, struct hatops *);
struct as *	hat_setup(struct as *, int);
void		hat_free(struct hat *, struct as *);
void		hat_swapin(struct as *);
void		hat_swapout(struct as *);
int		hat_dup(struct as *, struct as *);

extern	int	nhats;
extern	struct hat *hats;
extern	struct hat *hatsNHATS;


/*
 * Operations on a named address within a segment:
 *
 * hat_memload(hat, as, addr, pp, prot, flags)
 *	load/lock the given page struct
 *
 * hat_devload(hat, as, addr, dp, pf, prot, flags)
 *	load/lock the given page frame number (dp is the devpage)
 *
 * hat_contig_memload(hat, as, addr, pp, prot, flags, len)
 *	load/lock translations to a range of virtual addresses backed by
 *	physically contiguous memory.
 *
 * hat_contig_devload(hat, as, addr, dp, pf, prot, flags, len)
 *	load/lock translations to a range of virtual addresses backed by
 *	physically contiguous device memory.
 *
 * hat_unlock(hat, as, addr, len)
 *	unlock a given range of addresses
 *
 * hat_probe(hat, as, addr)
 *	*advisory* call (the wrong value may be returned) to test
 *	if the translation for addr is loaded in the underlying MMU
 *
 * hat_map(hat, as, addr, len, flags)
 *	Advisory call to allocate/reserve mapping structures for a
 *	segment. Called from the as layer when an address range is
 *	being created.
 *
 */

void	hat_memload(struct hat *, struct as *, caddr_t, struct page *,
			u_int, int);
void	hat_devload(struct hat *, struct as *, caddr_t, devpage_t *,
			u_int, u_int, int);
void	hat_contig_memload(struct hat *, struct as *, caddr_t, struct page *,
			u_int, int, u_int);
void	hat_contig_devload(struct hat *, struct as *, caddr_t, devpage_t *,
			u_int, u_int, int, u_int);
void	hat_unlock(struct hat *, struct as *, caddr_t, u_int);
int	hat_probe(struct hat *, struct as *, caddr_t);
int	hat_map(struct hat *, struct as *, caddr_t, u_int, int);

/*
 * Operations over an address range:
 *
 * hat_chgprot(as, addr, len, vprot)
 *	change protections
 *
 * hat_unload(as, addr, len, flags)
 *	unload mapping
 *
 * hat_sync(as, addr, len, flags)
 *	synchronize mapping with software data structures
 *
 * hat_unmap(as, addr, len, flags)
 *	Free up MMU resources allocated to map the address range
 *	<addr, addr + len>. Called from the as layer when an address space is
 *	is being freed and when an address range is being unmapped.
 */
void	hat_chgprot(struct as *, caddr_t, u_int, u_int);
void	hat_unload(struct as *, caddr_t, u_int, int);
void	hat_sync(struct as *, caddr_t, u_int, u_int);
void	hat_unmap(struct as *, caddr_t, u_int, int);

/*
 * Operations that work on all active translation for a given page:
 *
 * hat_pageunload(pp)
 *	unload all translations to page
 *
 * hat_pagesync(pp, flag)
 *	get hw stats from hardware into page struct and reset hw stats
 *
 * hat_pagecachectl(pp, flag)
 *	make the given page cached on uncached
 *
 * hat_kill_procs(pp, addr)
 *	kill processes using the given page
 */
void	hat_pageunload(struct page *);
void	hat_pagesync(struct page *, u_int);
void	hat_pagecachectl(struct page *, u_int);
int	hat_kill_procs(struct page *, caddr_t);

/*
 * Operations that return physical page numbers (ie - used by mapin):
 *	- return the pfn for kernel virtual address
 *	- return the pfn for arbitrary virtual address
 */
u_int	hat_getkpfnum(caddr_t);
u_int	hat_getpfnum(struct as *, caddr_t);

/*
 * Utility routines for manipulating the hat list of an address space
 * and the mapping list for a page
 */
struct hat	*hat_gethat();
void		hat_freehat(struct hat *);

void		hat_add(struct hat *, struct as *);
void		hat_sub(struct hat *, struct as *);

void 		hme_add(struct hment *, struct page *);
void 		hme_sub(struct hment *, struct page *);

/*
 * Routines to share page tables, aka ISM.
 * Currently available for SRMMU-based systems.
 */
int		hat_share(struct as *, caddr_t, struct as *,
				caddr_t, u_int);
void		hat_unshare(struct as *, caddr_t, u_int);

/*
 * Routines to set, get and clear "mod" and "ref" fields
 * in the page structure.
 */
void	hat_setmod(struct page *);
void	hat_setref(struct page *);
void	hat_setrefmod(struct page *);
void	hat_clrmod(struct page *);
void	hat_clrref(struct page *);
void	hat_clrrefmod(struct page *);

/*
 * Random stuff that is not currently included in the sun implementation
 * Mostly additions from ATT&T.
 */
#ifdef	notdef

/* an equivalent of hat_setup */
void	hat_asload(/* vaddr */);

/* a very mmu dependent optimization added by ATT for their 3bs */
int	hat_exec(/* oas, ostka, stksz, nas, nstka, flag */);
#else
#define	hat_asload()
#define	hat_exec(oas, ostka, stksz, nas, nstka, flag) 0
#endif


/*
 * Flags to pass to hat routines.
 *
 * Certain flags only apply to some interfaces:
 *
 * 	HAT_LOAD	Default flags to load a translation to the page.
 * 	HAT_LOCK	Lock down mapping resources; hat_map(), hat_memload(),
 *			and hat_devload().
 *	HAT_NOSYNCLOAD	Don't sync ref and mod bits to the page structure
 *			when translation is unloaded or sync'ed.
 *	HAT_ADVLOAD	Advisory load - Load translation if and only if
 *			sufficient MMU resources exist (i.e., do not steal).
 * 	HAT_UNLOCK	Unlock mapping resources; hat_memload(), hat_devload(),
 *			and hat_unload().
 *	HAT_NOFAULT	No-fault mapping (a la sparc v9)
 * 	HAT_FREEPP	Free pp if unloading last mapping; hat_unload().
 * 	HAT_RELEPP	PAGE_RELE() pp after mapping is unloaded; hat_unload().
 *	HAT_LOADSHARE	A flag to hat_memload() to indicate h/w page tables
 *			that map some user pages (not kas) is shared by more
 *			than one process (eg. ISM).
 */
#define	HAT_LOAD	0x00
#define	HAT_LOCK	0x01
#define	HAT_UNLOCK	0x02

#define	HAT_NOSYNCLOAD	0x04
#define	HAT_ADVLOAD	0x08
#define	HAT_NOFAULT	0x10

#define	HAT_FREEPP	0x20
#define	HAT_RELEPP	0x40

#define	HAT_NOCONSIST	0x80

#define	HAT_LOADSHARED	0x100

/*
 * XXX
 * grotesque hack for seg_dev which allows /dev/kmem
 * to get consistent mmap() mappings
 */
#define	HAT_KMEM	0x10000

/* flag for hat_setup */
#define	HAT_DONTALLOC	0
#define	HAT_ALLOC	1

/* flag for hat_unload */
#define	HAT_UNLOAD		0x0
#define	HAT_TMPUNLOAD		0x1
#define	HAT_NOSYNCUNLOAD	0x2
#define	HAT_UNLOCKUNLOAD	0x4
#define	HAT_UNMAP		0x8	/* Address range is being destroyed */

/* flag for hat_map */

#define	HAT_MAP		0x0


/* flags for hat_pagesync, hat_getstat */
#define	HAT_DONTZERO	0x0
#define	HAT_ZERORM	0x1
#define	HAT_STOPON_REF	0x2	/* or'ed with DONTZERO */
#define	HAT_STOPON_MOD	0x4	/* or'ed with DONTZERO */
#define	HAT_STOPON_RM	(HAT_STOPON_REF | HAT_STOPON_MOD) /* mask */

/* return code for hat_pagesync  & hat_pageunload */
/* grc - need to move into hat_srmmu.h */
#define	HAT_DONE	0x0
#define	HAT_RESTART	0x1
#define	HAT_VAC_DONE	0x2

/* flags for hat_pagecachectl */
#define	HAT_CACHE	0x0
#define	HAT_UNCACHE	0x1
#define	HAT_TMPNC	0x2

/* data order flags */
#define	HAT_STRICTORDER		0x0000
#define	HAT_UNORDERED_OK	0x0100
#define	HAT_MERGING_OK		0x0200
#define	HAT_LOADCACHING_OK	0x0300
#define	HAT_STORECACHING_OK	0x0400
#define	HAT_ORDER_MASK		0x0700

/* endian flags */
#define	HAT_NEVERSWAP		0x0000
#define	HAT_STRUCTURE_BE	0x0800
#define	HAT_STRUCTURE_LE	0x1000
#define	HAT_ENDIAN_MASK		0x1800

/*
 * Macros for the hat operations
 */

#define	HATOP_ALLOC(hat, as) \
		(*(hat)->hat_op->h_alloc)(hat, as)

#define	HATOP_SETUP(as, flag) \
		(*(as)->a_hat->hat_op->h_setup)(as, flag)

#define	HATOP_FREE(hat, as) \
		(*(hat)->hat_op->h_free)(hat, as)

#define	HATOP_DUP(hat, as, newas) \
		(*(hat)->hat_op->h_dup)(hat, as, newas)

#define	HATOP_SWAPIN(hat, as) \
		(*(hat)->hat_op->h_swapin)(hat, as)

#define	HATOP_SWAPOUT(hat, as) \
		(*(hat)->hat_op->h_swapout)(hat, as)

#define	HATOP_MEMLOAD(hat, as, addr, pp, prot, flags) \
		(*(hat)->hat_op->h_memload)(hat, as, addr, pp, prot, flags)

#define	HATOP_DEVLOAD(hat, as, addr, dp, pf, prot, flags) \
		(*(hat)->hat_op->h_devload)(hat, as, addr, dp, pf, prot, flags)

#define	HATOP_CONTIG_MEMLOAD(hat, as, addr, pp, prot, flags, len) \
		(*(hat)->hat_op->h_contig_memload)(hat, as, addr, \
			pp, prot, flags, len)

#define	HATOP_CONTIG_DEVLOAD(hat, as, addr, dp, pf, prot, flags, len) \
		(*(hat)->hat_op->h_contig_devload)(hat, as, addr, \
			dp, pf, prot, flags, len)

#define	HATOP_UNLOCK(hat, as, addr, len) \
		(*(hat)->hat_op->h_unlock)(hat, as, addr, len)

#define	HATOP_FAULT(hat, addr) \
		(*(hat)->hat_op->h_fault)(hat, addr)

#define	HATOP_CHGPROT(hat, as, addr, len, vprot) \
		(*(hat)->hat_op->h_chgprot)(as, addr, len, vprot)

#define	HATOP_UNLOAD(hat, as, addr, len, flags) \
		(*(hat)->hat_op->h_unload)(as, addr, len, flags)

#define	HATOP_SYNC(hat, as, addr, len, flags) \
		(*(hat)->hat_op->h_sync)(as, addr, len, flags)

#define	HATOP_PAGEUNLOAD(hat, pp, hme) \
		(*(hat)->hat_op->h_pageunload)(pp, hme)

#define	HATOP_PAGESYNC(hat, pp, hme, flag) \
		(*(hat)->hat_op->h_pagesync)(hat, pp, hme, flag)

#define	HATOP_PAGECACHECTL(hsw, pp, flag) \
		(*(hsw)->hsw_ops->h_pagecachectl)(pp, flag)

#define	HATOP_GETKPFNUM(hat, addr) \
		(*(hat)->hat_op->h_getkpfnum)(addr)

#define	HATOP_GETPFNUM(hat, as, addr) \
		(*(hat)->hat_op->h_getpfnum)(as, addr)

#define	HATOP_MAP(hat, as, addr, len, flags) \
		(*(hat)->hat_op->h_map)(hat, as, addr, len, flags)

#define	HATOP_PROBE(hat, as, addr) \
		(*(hat)->hat_op->h_probe)(hat, as, addr)

#define	HATOP_SHARE(hat, das, daddr, sas, saddr, len) \
		(*(hat)->hat_op->h_share)(das, daddr, sas, saddr, len)

#define	HATOP_UNSHARE(hat, as, addr, len) \
		(*(hat)->hat_op->h_unshare)(as, addr, len)

#define	HATOP_UNMAP(hat, as, addr, len, flags) \
		(*(hat)->hat_op->h_unmap)(as, addr, len, flags)
/*
 * Other routines, for statistics
 */
int	hat_startstat(struct as *);
int	hat_setstat(struct as *, caddr_t, u_int);
void	hat_getstat(struct as *, caddr_t, u_int, u_int, caddr_t, int);
void	hat_getstatby(struct as *, caddr_t, u_int, u_int, char *, int);
void	hat_freestat(struct as *, int);
void	hrm_getblk(int, struct as *);

#endif /* _KERNEL */

/*
 * The size of the bit array for ref and mod bit storage must be a power of 2.
 * 2 bits are collected for each page.  Below the power used is 4,
 * which is 16 8-bit characters = 128 bits, ref and mod bit information
 * for 64 pages.
 */
#define	HRM_SHIFT		4
#define	HRM_BYTES		(1 << HRM_SHIFT)
#define	HRM_PAGES		((HRM_BYTES * NBBY) / 2)
#define	HRM_PGPERBYTE		(NBBY/2)
#define	HRM_PGBYTEMASK		(HRM_PGPERBYTE-1)

#define	HRM_PGOFFMASK		((HRM_PGPERBYTE-1) << MMU_PAGESHIFT)
#define	HRM_BASEOFFSET		(((MMU_PAGESIZE * HRM_PAGES) - 1))
#define	HRM_BASEMASK		(~(HRM_BASEOFFSET))

#define	HRM_BASESHIFT		(MMU_PAGESHIFT + (HRM_SHIFT + 2))
#define	HRM_PAGEMASK		(MMU_PAGEMASK ^ HRM_BASEMASK)

#define	HRM_HASHSIZE		0x200
#define	HRM_HASHMASK		(HRM_HASHSIZE - 1)

#define	HRM_BLIST_INCR		0x200

/*
 * The structure for maintaining referenced and modified information
 */
struct hrmstat {
	struct as	*hrm_as;	/* stat block belongs to this as */
	u_int		hrm_base;	/* base of block */
	u_short		hrm_id;		/* opaque identifier, one of a_vbits */
	struct hrmstat	*hrm_anext;	/* as statistics block list */
	struct hrmstat	*hrm_hnext;	/* list for hashed blocks */
	u_char		hrm_bits[HRM_BYTES]; /* the ref and mod bits */
};

/*
 * For global monitoring of the reference and modified bits
 * of all address spaces we reserve one id bit.
 */
#define	HRM_SWSMONID	1


#ifdef _KERNEL

/*
 * Hat locking functions
 */
void	hat_enter(struct as *);
void	hat_exit(struct as *);
void	hat_page_enter(struct page *);
void	hat_page_exit(struct page *);
void	hat_mlist_enter(struct page *);
void	hat_mlist_exit(struct page *);

struct hat_lockops {
	void	(*hl_page_enter)(struct page *);
	void	(*hl_page_exit)(struct page *);
	void	(*hl_hat_enter)(struct as *);
	void	(*hl_hat_exit)(struct as *);
	void	(*hl_mlist_enter)(struct page *);
	void	(*hl_mlist_exit)(struct page *);
	int	(*hl_mlist_held)(struct page *);
	void	(*hl_cachectl_enter)(struct page *);
	void	(*hl_cachectl_exit)(struct page *);
};

extern struct hat_lockops *hat_lockops;

/* per page locking - p_nrm bits */
#define	HATLOCK_PAGE_ENTER(pp)	((hat_lockops->hl_page_enter)(pp))
#define	HATLOCK_PAGE_EXIT(pp)	((hat_lockops->hl_page_exit)(pp))

/* per hat locking */
#define	HATLOCK_HAT_ENTER(as)	((hat_lockops->hl_hat_enter)(as))
#define	HATLOCK_HAT_EXIT(as)	((hat_lockops->hl_hat_exit)(as))

/* mapping list locking - p_mapping */
#define	HATLOCK_MLIST_ENTER(pp)	((hat_lockops->hl_mlist_enter)(pp))
#define	HATLOCK_MLIST_EXIT(pp)	((hat_lockops->hl_mlist_exit)(pp))
#define	HATLOCK_MLIST_HELD(pp)	((hat_lockops->hl_mlist_held)(pp))

#endif /* _KERNEL */

/* per page cachectl locking - p_nc */
#define	HATLOCK_CACHECTL_ENTER(pp)	((hat_lockops->hl_cachectl_enter)(pp))
#define	HATLOCK_CACHECTL_EXIT(pp)	((hat_lockops->hl_cachectl_exit)(pp))

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_HAT_H */
