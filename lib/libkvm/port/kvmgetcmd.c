/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident	"@(#)kvmgetcmd.c	2.44	94/06/21 SMI"

#include "kvm_impl.h"
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/vmparam.h>
#include <vm/as.h>
#include <vm/seg_vn.h>
#include <vm/seg_map.h>
#include <vm/anon.h>
#include <vm/page.h>
#include <sys/swap.h>
#define _KERNEL
#include <vm/seg_kp.h>
#undef _KERNEL
#include <sys/sysmacros.h>	/* for MIN() */

#define	KVM_PAGE_HASH_FUNC(kd, vp, off) \
        ((((off) >> (kd)->pageshift) + ((int)(vp) >> PAGE_HASHVPSHIFT)) & \
                ((kd)->page_hashsz - 1))

#define	KVM_SEGKP_HASH(kd, vaddr) \
	(((u_int)(vaddr) >> (kd)->pageshift) & SEGKP_HASHMASK)

extern char *malloc();
static u_longlong_t page_to_physaddr();
static int anon_to_fdoffset();
static int vp_to_fdoffset();
static int getswappage();

/*
 * VERSION FOR MACHINES WITH STACKS GROWING DOWNWARD IN MEMORY
 *
 * On program entry, the top of the stack frame looks like this:
 *
 * hi:	|-----------------------|
 *	|	   0		|
 *	|-----------------------|+
 *	|	   :		| \
 *	|  arg and env strings	|  > no more than NCARGS bytes
 *	|	   :		| /
 *	|-----------------------|+
 *	|	(char *)0	|
 *	|-----------------------|
 *	|  ptrs to env strings	|
 *	|	   :		|
 *	|-----------------------|
 *	|	(char *)0	|
 *	|-----------------------|
 *	|  ptrs to arg strings	|
 *	|   (argc = # of ptrs)	|
 *	|	   :		|
 *	|-----------------------|
 *	|	  argc		| <- sp
 * low:	|-----------------------|
 */

/* define a structure for describing an argument list */
typedef struct {
	int	cnt;		/* number of strings */
	u_long	sp;		/* user virtual addr of first string */
	u_long	se;		/* user virtual addr of end of strings */
} argv_t;

static char **argcopy();
static int stkcopy();
static int getstkpg();
static int getseg();
static int readseg();
static struct page *pagefind();
int _swapread();
int _anonoffset();

/*
 * Convert an array offset to a user virtual address.
 * This is done by adding the array offset to the virtual address
 * of the start of the current page (kd->stkpg counts pages from the
 * top of the stack).
 */
#define	Uvaddr(kd, x)	\
	((kd)->usrstack - (((kd)->stkpg + 1) * (kd)->pagesz) \
		+ ((char *)(x) - (kd)->sbuf))
/*
 * reconstruct an argv-like argument list from the target process
 */
int
kvm_getcmd(kd, proc, u, arg, env)
	kvm_t *kd;
	struct proc *proc;
	struct user *u;
	char ***arg;
	char ***env;
{
	argv_t argd;
	argv_t envd;
	register char *cp;
	register int eqseen, i;

	/* XXX is this correct? -- should we warn, or just return -1? */
	if (proc->p_stksize == 0) {	/* if no stack, give up now */
		KVM_ERROR_1("kvm_getcmd: warning: no stack");
		return (-1);
	}

	/*
	 * Read the last stack page into kd->sbuf (allocating, if necessary).
	 * Then, from top of stack, find the end of the environment strings.
	 */
	if (getstkpg(kd, proc, u, 0) == -1)
		return (-1);

	/*
	 * Point to the last byte of the environment strings.
	 */
	cp = &kd->sbuf[(kd->pagesz) - NBPW - 1];

	/*
	 * Skip backward over any zero bytes used to pad the string data
	 * to a word boundary.
	 */
	for (i = 0; i != NBPW && *--cp == '\0'; i++)
		;

	/*
	 * Initialize descriptors
	 */
	envd.cnt = 0;
	envd.sp = envd.se = Uvaddr(kd, cp + 1); /* this must point at '\0' */
	argd = envd;

	/*
	 * Now, working backwards, count the environment strings and the
	 * argument strings and look for the (int)0 that delimits the
	 * environment pointers.
	 */
	while (*(int *)((u_long)cp & ~(NBPW - 1)) != 0) {
		eqseen = 0;
		while (*cp != '\0') {
			if (*cp-- == '=')
				eqseen = 1;
			if (cp < kd->sbuf) {
				if (kd->stkpg * kd->pagesz > NCARGS ||
				    getstkpg(kd, proc, u, ++kd->stkpg) == -1)
					return (-1);
				else
					cp = &kd->sbuf[kd->pagesz - 1];
			}
		}
		if (eqseen && argd.cnt == 0) {
			envd.cnt++;
			envd.sp = Uvaddr(kd, cp + 1);
		} else {
			argd.cnt++;
			argd.sp = Uvaddr(kd, cp + 1);
		}
		cp--;
	}

	if (envd.cnt != 0) {
		argd.se = envd.sp - 1;
		if (argd.cnt == 0)
			argd.sp = argd.se;
	}

	if (envd.se - argd.sp > NCARGS - 1) {
		KVM_ERROR_1("kvm_getcmd: could not locate arg pointers");
		return (-1);
	}

	/* Copy back the (adjusted) vectors and strings */
	if (arg != NULL) {
		if ((*arg = argcopy(kd, proc, u, &argd)) == NULL)
			return (-1);
	}
	if (env != NULL) {
		if ((*env = argcopy(kd, proc, u, &envd)) == NULL) {
			if (arg != NULL)
				(void) free((char *)*arg);
			return (-1);
		}
	}
	return (0);
}

static char **
argcopy(kd, proc, u, arg)
	kvm_t *kd;
	struct proc *proc;
	struct user *u;
	argv_t *arg;
{
	int pcnt;
	int scnt;
	register char **ptr;
	register char **p;
	char *str;

	/* Step 1: allocate a buffer to hold all pointers and strings */
	pcnt = (arg->cnt + 1) * sizeof (char *);	/* #bytes in ptrs */
	scnt = arg->se - arg->sp + 1;			/* #bytes in strings */
	ptr = (char **)malloc((u_int)scnt + pcnt);
	if (ptr == NULL) {
		KVM_PERROR_1("argcopy");
		return (NULL);
	}
	str = (char *)ptr + pcnt;

	/* Step 2: copy the strings from user space to buffer */
	if (stkcopy(kd, proc, u, arg->sp, str, scnt) == -1) {
		(void) free((char *)ptr);
		return (NULL);
	}
	if (str[scnt-1] != '\0') {
		KVM_ERROR_1("argcopy: added NULL at end of strings");
		str[scnt-1] = '\0';
	}

	/* Step 3: calculate the pointers */
	for (p = ptr, pcnt = arg->cnt; pcnt-- > 0;) {
		*p++ = str;
		while (*str++ != '\0')
			;
	}
	*p++ = NULL;			/* NULL pointer at end */
	if ((str - (char *)p) != (arg->cnt ? scnt : 0)) {
		KVM_ERROR_1("argcopy: string pointer botch");
	}
	return (ptr);
}

/* XXX this and getstkpg should use new kvm_as_read instead of readseg */

/*
 * Copy user stack into specified buffer
 */
static int
stkcopy(kd, proc, u, va, buf, cnt)
	kvm_t *kd;
	struct proc *proc;
	struct user *u;
	u_long va;
	char *buf;
	int cnt;
{
	register int i = 0;
	register u_int off;
	register int pg;
	register int c;

	if ((kd->usrstack - va) < cnt) {
		KVM_ERROR_2("stkcopy: bad stack copy length %d", cnt);
		return (-1);
	}
	off = va & kd->pageoffset;
	pg = (int) ((kd->usrstack - (va + 1)) / kd->pagesz);

	while (cnt > 0) {
		if ((kd->stkpg != pg) && (getstkpg(kd, proc, u, pg) == -1))
			return (-1);
		c = MIN((kd->pagesz - off), cnt);
		memcpy(&buf[i], &kd->sbuf[off], c);
		i += c;
		cnt -= c;
		off = 0;
		pg--;
	}
	return (0);
}

#ifdef _KVM_DEBUG
#define	getkvm(a, b, m)							\
	if (kvm_read(kd, (u_long)(a), (caddr_t)(b), sizeof (*b))	\
						!= sizeof (*b)) {	\
		KVM_ERROR_2("error reading %s", m);			\
		return (-1);						\
	}
#else !_KVM_DEBUG
#define	getkvm(a, b, m)							\
	if (kvm_read(kd, (u_long)(a), (caddr_t)(b), sizeof (*b))	\
						!= sizeof (*b)) {	\
		return (-1);						\
	}
#endif _KVM_DEBUG

/*
 * read a user stack page into a holding area
 */
/*ARGSUSED*/
static int
getstkpg(kd, proc, u, pg)
	kvm_t *kd;
	struct proc *proc;
	struct user *u;
	int pg;
{
	caddr_t vaddr;

	/* If no address segment ptr, this is a system process (e.g. biod) */
	if (proc->p_as == NULL)
		return (-1);

	/* First time through, allocate a user stack cache */
	if (kd->sbuf == NULL) {
		kd->sbuf = malloc(kd->pagesz);
		if (kd->sbuf == NULL) {
			KVM_PERROR_1("can't allocate stack cache");
			return (-1);
		}
	}

	/* If no seg struct for this process, get one for the 1st stack page */
	if (kd->useg.s_as != proc->p_as) {
		struct as uas;

		getkvm(proc->p_as, &uas, "user address space descriptor");
		getkvm(uas.a_tail, &kd->useg, "stack segment descriptor");
	}

	kd->stkpg = pg;

	/* Make sure we've got the right seg structure for this address */
	vaddr = (caddr_t)(kd->usrstack - ((pg+1) * kd->pagesz));
	if (getseg(kd, &kd->useg, vaddr, &kd->useg) == -1)
		return (-1);
	if (kd->useg.s_as != proc->p_as) {
		KVM_ERROR_1("wrong segment for user stack");
		return (-1);
	}
	if (kd->useg.s_ops != kd->segvn) {
		KVM_ERROR_1("user stack segment not segvn type");
		return (-1);
	}

	/* Now go find and read the page */
	if (readseg(kd, &kd->useg, vaddr, kd->sbuf, kd->pagesz)
	    != kd->pagesz) {
		KVM_ERROR_1("error reading stack page");
		return (-1);
	}
	return (0);
}

/*
 * getseg - given a seg structure, find the appropriate seg for a given address
 *	  (nseg may be identical to oseg)
 */
static int
getseg(kd, oseg, addr, nseg)
	kvm_t *kd;
	struct seg *oseg;
	caddr_t addr;
	struct seg *nseg;
{

	if (addr < oseg->s_base) {
		do {
			if (oseg->s_prev == NULL)
				goto noseg;
			getkvm(oseg->s_prev, nseg, "prev segment descriptor");
			oseg = nseg;
		} while (addr < nseg->s_base);
		if (addr >= (nseg->s_base + nseg->s_size))
			goto noseg;

	} else if (addr >= (oseg->s_base + oseg->s_size)) {
		struct as xas;

		getkvm(oseg->s_as, &xas, "segment's address space");
		do {
			struct seg *next;

			if (xas.a_lrep == AS_LREP_LINKEDLIST)
				next = oseg->s_next.list;
			else {
				seg_skiplist ssl;

				getkvm(oseg->s_next.skiplist, &ssl,
				    "segment skiplist structure");
				next = ssl.segs[0];
			}
			if (next == NULL)
				goto noseg;
			getkvm(next, nseg, "next segment descriptor");
			oseg = nseg;
		} while (addr >= nseg->s_base + nseg->s_size);
		if (addr < nseg->s_base)
			goto noseg;

	} else if (nseg != oseg) {
		*nseg = *oseg;		/* copy if necessary */
	}
	return (0);

noseg:
	KVM_ERROR_2("can't find segment for user address %x", addr);
	return (-1);
}

/*
 * readseg - read data described by a virtual address and seg structure.
 *	   The data block must be entirely contained within seg.
 *	   Readseg() returns the number of bytes read, or -1.
 */
static int
readseg(kd, seg, addr, buf, size)
	kvm_t *kd;
	struct seg *seg;
	caddr_t addr;
	char *buf;
	u_int size;
{
	u_int count;

	if ((addr + size) > (seg->s_base + seg->s_size)) {
		KVM_ERROR_1("readseg: segment too small");
		return (-1);
	}

	count = 0;

	if (seg->s_ops == kd->segvn) {
		/* Segvn segment */
		struct segvn_data sdata;
		struct anon_map amap;
		struct anon **anp;
		struct anon *ap;
		u_int apsz;
		u_int aoff;
		u_int rsize;

		/* get private data for segment */
		if (seg->s_data == NULL) {
			KVM_ERROR_1("NULL segvn_data ptr in segment");
			return (-1);
		}
		getkvm(seg->s_data, &sdata, "segvn_data");

		/* Null vnode indicates anonymous page */
		if (sdata.vp != NULL) {
			KVM_ERROR_1("non-NULL vp in segvn_data");
			return (-1);
		}
		if (sdata.amp == NULL) {
			KVM_ERROR_1("NULL anon_map ptr in segvn_data");
			return (-1);
		}

		/* get anon_map structure */
		getkvm(sdata.amp, &amap, "anon_map");
		if (amap.anon == NULL) {
			KVM_ERROR_1("anon_map has NULL ptr");
			return (-1);
		}
		apsz = (amap.size >> kd->pageshift) * sizeof (struct anon *);
		if (apsz == 0) {
			KVM_ERROR_1("anon_map has zero size");
			return (-1);
		}
		if ((anp = (struct anon **)malloc(apsz)) == NULL) {
			KVM_PERROR_1("can't allocate anon pointer array");
			return (-1);
		}

		/* read anon pointer array */
		if (kvm_read(kd, (u_long)amap.anon, (char *)anp, apsz) != apsz){
			KVM_ERROR_1("error reading anon ptr array");
			free((char *)anp);
			return (-1);
		}
		/* since data may cross page boundaries, break up request */
		while (count < size) {
			struct page *paddr;
			struct page page;
			int swapoff;
			struct vnode *vp;
			u_int vpoff;
			u_longlong_t skaddr;
			struct page *pages;
			u_int pages_base;

			aoff = (long)addr & kd->pageoffset;
			rsize = MIN((kd->pagesz - aoff), (size - count));

			/* index into anon ptrs to find the right one */
			ap = anp[sdata.anon_index +
			    ((addr - seg->s_base)>>kd->pageshift)];
			if (ap == NULL) {
				KVM_ERROR_1("NULL anon ptr");
				break;
			}
			if ((swapoff = _anonoffset(kd, ap, &vp, &vpoff)) == -1) {
				break;
			}

			/* try hash table in case page is still around */
			paddr = pagefind(kd, &page, vp, vpoff);
			if (paddr == NULL)
				goto tryswap;

			/* make sure the page structure is useful */
			if (page.p_selock == -1) {
				KVM_ERROR_1("anon page is gone");
				break;
			}

			/*
			 * Page is in core (probably).
			 * XXX - the following is going to change when
			 *	 tuples are implemented
			 */

			if ((paddr >= kd->memseg.pages) &&
			    (paddr < kd->memseg.epages)) {
				pages = kd->memseg.pages;
				pages_base = kd->memseg.pages_base;
			} else {
				struct memseg memseg;
				struct memseg *next;

				/* not in first memseg */
				next = kd->memseg.next;
				memseg.pages = kd->memseg.pages;
				memseg.epages = kd->memseg.epages;
				while (paddr < memseg.pages ||
				    paddr >= memseg.epages) {
					getkvm(next, &memseg,
					    "next memory segment");
					next = memseg.next;
				}
				pages = memseg.pages;
				pages_base = memseg.pages_base;
			}
			skaddr = aoff + ((u_longlong_t)((paddr - pages) +
			    pages_base) << kd->pageshift);
			if (_uncondense(kd, kd->corefd, &skaddr)) {
				KVM_ERROR_2("%s: anon page uncondense error",
						kd->core);
				break;
			}
			if (llseek(kd->corefd, (offset_t)skaddr, L_SET) == -1) {
				KVM_PERROR_2("%s: anon page seek error",
						kd->core);
				break;
			}
			if (read(kd->corefd, buf, rsize) != rsize) {
				KVM_PERROR_2("%s: anon page read error",
						kd->core);
				break;
			}
			goto readok;

tryswap:
			/*
			 * If no page structure, page is swapped out
			 */
			if (kd->swapfd == -1)
				break;
			if (_swapread(kd, (long)swapoff, aoff, buf, rsize) !=
			    rsize) {
				KVM_PERROR_2("%s: anon page read error",
						kd->swap);
				break;
			}
readok:
			count += rsize;
			addr += rsize;
			buf += rsize;
		}
		free((char *)anp);	/* no longer need this */

	} else if (seg->s_ops == kd->segmap) {
		/* Segmap segment */
		KVM_ERROR_1("cannot read segmap segments yet");
		return (-1);

	} else if (seg->s_ops == kd->segdev) {
		/* Segdev segment */
		KVM_ERROR_1("cannot read segdev segments yet");
		return (-1);

 	} else if (seg->s_ops == kd->segkp) {
		/* Segkp segment */
		KVM_ERROR_1("cannot read segkp segments yet");
		return (-1);
	} else {
		/* Segkmem or unknown segment */
		KVM_ERROR_1("unknown segment type");
		return (-1);
	}
	if (count == 0)
		return (-1);
	else
		return ((int) count);
}


/* this is like the getkvm() macro, but returns NULL on error instead of -1 */
#ifdef _KVM_DEBUG
#define	getkvmnull(a, b, m)						\
	if (kvm_read(kd, (u_long)(a), (caddr_t)(b), sizeof (*b)) 	\
						!= sizeof (*b)) {	\
		KVM_ERROR_2("error reading %s", m);			\
		return (NULL);						\
	}
#else !_KVM_DEBUG
#define	getkvmnull(a, b, m)						\
	if (kvm_read(kd, (u_long)(a), (caddr_t)(b), sizeof (*b)) 	\
						!= sizeof (*b)) {	\
		return (NULL);						\
	}
#endif _KVM_DEBUG
	
/*
 * Return the fd and offset for the address in segkp.
 */
static int
getsegkp(kd, seg, vaddr, fdp, offp)
	kvm_t *kd;
	struct seg *seg;
	caddr_t vaddr;
	int *fdp;
	u_longlong_t *offp;
{
	struct segkp_segdata segdata;
	int index;
	int stop;
	struct segkp_data **hash;
	struct segkp_data *head;
	struct segkp_data segkpdata;
	caddr_t vlim;
	struct anon anonx;
	struct anon *anonp;
	struct anon *anonq;
	struct anon **anonpp;
	struct vnode *vp;
	u_int i;
	int redzone = 0;
	u_int off;
	int retval = 0;

	off = (u_int)vaddr & kd->pageoffset;
	vaddr = (caddr_t)((u_int)vaddr & kd->pagemask);

	/* get private data for segment */
	if (seg->s_data == NULL) {
		KVM_ERROR_1("getsegkp: NULL segkp_data ptr in segment");
		return (-1);
	}

	getkvm(seg->s_data, &segdata, "segkp_data");
	
	/*
	 * find kpd associated with the virtual address:
	 */
	
	if (segdata.kpsd_hash == NULL) {
		KVM_ERROR_1("getsegkp: NULL kpsd_hash ptr in segment");
		return (-1);
	}

	if ((hash = (struct segkp_data **)
	     malloc((size_t)(sizeof(struct segkp_data **)
			     * SEGKP_HASHSZ))) == NULL) {
		KVM_ERROR_1("getsegkp: can't allocate hash");
		return (-1);
	}

 	if (kvm_read(kd, (u_long)segdata.kpsd_hash, (char *)hash,
	    sizeof (struct segkp_data **) * SEGKP_HASHSZ)
		!= sizeof(struct segkp_data **) * SEGKP_HASHSZ) 
 		KVM_ERROR_1("getsegkp: can't read hash table");

	index = stop = KVM_SEGKP_HASH(kd, vaddr);
	do {
		for (head = hash[index]; head; head = segkpdata.kp_next) {

			getkvm(head, &segkpdata, "head");
			if (vaddr >= segkpdata.kp_base &&
			    vaddr <= segkpdata.kp_base + segkpdata.kp_len)

				goto found_it;

		}
		if (--index < 0)
			index = SEGKP_HASHSZ - 1; /* wrap */
	} while (index != stop);

	KVM_ERROR_2("getsegkp: can't find segment: vaddr %x", vaddr);
 
	retval = -1;
	goto exit_getsegkp;
	
found_it:
	
	/* OK, we have the kpd */

	getkvm(head, &segkpdata, "head");	/* entry head pts to */
	if (!(segkpdata.kp_flags & KPD_NO_ANON) && segkpdata.kp_anon == NULL) {
		KVM_ERROR_1("getsegkp: KPD_NO_ANON, NULL anon ptr in segment");
		retval = -1;
		goto exit_getsegkp;
	}

	i = (vaddr - segkpdata.kp_base) / (int)kd->pagesz;

	if (segkpdata.kp_flags & KPD_NO_ANON) {
		register u_longlong_t phyadd;
		struct page *pp;
		struct page page;

		if ((pp = pagefind(kd, &page, kd->kvp, vaddr)) == NULL) {
			KVM_ERROR_1("getsegkp: can't read page array");
			retval = -1;
			goto exit_getsegkp;
		}
		phyadd = page_to_physaddr(kd, pp);

		*fdp = kd->corefd;
		*offp = phyadd + off;
		retval = 0;
		goto exit_getsegkp;
	} else {
		anonpp = segkpdata.kp_anon;
				
		/* see common/vm/vm_swap/swap_xlate() */
		
		/* calculate address corresponding to anon[i] */
		anonp = (struct anon *)((int *)anonpp + i);
		
		if (anonp == NULL) {
			KVM_ERROR_1("getsegkp: NULL anonp ptr in segment");
			retval = 0;
			goto exit_getsegkp;
		}

		/* now get the anon table entry */
		getkvm(anonp, &anonq, "anon");
		retval = anon_to_fdoffset(kd, anonq, fdp, offp, off);
		goto exit_getsegkp;
	}

exit_getsegkp:
	(void) free((void *)hash);
	return (retval);
}

/*
 * pagefind - hashed lookup to see if a page exists for a given vnode
 *	    Returns address of page structure in 'page', or NULL if error.
 */
static struct page *
pagefind(kd, page, vp, off)
	kvm_t *kd;
	struct page *page;
	struct vnode *vp;
	u_int off;
{
	struct page *pp;

	getkvmnull((kd->page_hash + KVM_PAGE_HASH_FUNC(kd, vp, off)), &pp,
	    "initial hashed page struct ptr");
	while (pp != NULL) {
		getkvmnull(pp, page, "hashed page structure");
		if ((page->p_vnode == vp) && (page->p_offset == off)) {
			return (pp);
		}
		pp = page->p_hash;
	}
	return (NULL);
}

/*
 * _swapread - read data from the swap device, handling alignment properly,
 *	    Swapread() returns the number of bytes read, or -1.
 */
int
_swapread(kd, addr, offset, buf, size)
	kvm_t *kd;
	long addr;
	u_int offset;
	char *buf;
	u_int size;
{
	u_longlong_t ua = addr;

	if (_uncondense(kd, kd->swapfd, &ua)) {
		/* Does this need to handle the -2 case? */
		KVM_ERROR_2("%s: uncondense error", kd->swap);
		return (-1);
	}
	addr = ua;
	if (lseek(kd->swapfd, addr, L_SET) == -1) {
		KVM_PERROR_2("%s: seek error", kd->swap);
		return (-1);
	}
	if ((offset == 0) && ((size % DEV_BSIZE) == 0)) {
		return (read(kd->swapfd, buf, size));
	} else {
		KVM_ERROR_2("%s: swap offsets not implemented", kd->swap);
		return (-1);
	}
}

/*
 * _anonoffset
 *
 * Convert a pointer into an anon array into an offset (in bytes)
 * into the swap file. For live systems the file descriptor for the associated
 * swapfile is returned in kd->swapfd, and the offset is into that file.
 * Since each individual swap file has a separate swapinfo structure, we cache 
 * the linked list of swapinfo structures in order to do this calculation 
 * faster.  For dead systems the offset is an offset into a virtual swapfile 
 * formed by concatenating all the real files. Also, save the
 * real vp and offset for the vnode that contains this page.
 */
int
_anonoffset(kd, ap, vp, vpoffset)
	kvm_t *kd;
	struct anon *ap;
	struct vnode **vp;
	u_int *vpoffset;
{
	register struct swapinfo *sip;
	struct anon anon;

	sip = kd->sip;

	/*
	 * First time through, read in all the swapinfo structures.
	 * Open each swap file and store the fd in si_hint.
	 * Re-use the si_allocs field to store swap offset. This will
	 * only be used on a dump with a dead swapfile.
	 */
	if (sip == NULL) {
		register struct swapinfo **spp;
		register struct swapinfo *sn;
		register int soff;
		char *pname;
		char *sp, *np;

		sn = kd->swapinfo;
		spp = &kd->sip;
		for (; sn != NULL; spp = &(*spp)->si_next, sn = *spp) {
			*spp = (struct swapinfo *)malloc(sizeof (*sn));
			if (*spp == NULL) {
				KVM_PERROR_1("no memory for swapinfo");
				break;
			}
			if (kvm_read(kd, (u_long)sn, (char*)*spp, sizeof (*sn))
						!= sizeof (*sn)) {
				KVM_ERROR_1("error reading swapinfo");
				free((char *)*spp);
				break;
			}
			pname = (*spp)->si_pname;
			(*spp)->si_pname = malloc((*spp)->si_pnamelen + 10);
			if ((*spp)->si_pname == NULL) {
				KVM_PERROR_1("no memory for swapinfo");
				break;
			}
			if (kvm_read(kd, (u_long)pname,
			    (char *)(*spp)->si_pname, (*spp)->si_pnamelen)
			    != (*spp)->si_pnamelen) {
				KVM_ERROR_1("error reading swapinfo");
				break;
			}
			(*spp)->si_hint = open((*spp)->si_pname, O_RDONLY, 0);
			if ((*spp)->si_hint == -1) {
				KVM_ERROR_2("can't open swapfile %s", 
					(*spp)->si_pname);
			}
			(*spp)->si_allocs = soff;
			soff += (((*spp)->si_eoff - (*spp)->si_soff)
				>> kd->pageshift);
		}
		*spp = NULL;		/* clear final 'next' pointer */
		sip = kd->sip;		/* reset list pointer */
	}
	getkvm(ap, &anon, "anon structure");

	/*
	 * If the anon slot has no physical vnode for backing store just
	 * return the (an_vp,an_off) that gives the name of the page. Return
	 * the swapfile offset as 0. This offset will never be used, as the
	 * caller will always look for and find the page first, thus it will
	 * never try the swapfile.
	 */
	if (anon.an_pvp == NULL) {
		*vp = anon.an_vp;
		*vpoffset = anon.an_off;
		kd->swapfd = -1;
		kd->swap = NULL;
		return(0);
	}
	/*
	 * If there is a physical backing store vnode, find the
	 * corresponding swapinfo. If this is a live system, set the
	 * swapfd in the kvm to be the swapfile for this swapinfo.
	 */
	for (; sip != NULL; sip = sip->si_next) {
		if ((anon.an_pvp == sip->si_vp) && 
		    (anon.an_poff >= sip->si_soff) && 
		    (anon.an_poff <= sip->si_eoff)) {
			*vp = anon.an_vp;
			*vpoffset = anon.an_off;
			if (strcmp(kd->core, LIVE_COREFILE) == 0) {
				kd->swapfd = sip->si_hint;
				kd->swap = sip->si_pname;
				return (anon.an_poff);
			} else {
				return 
				    ((sip->si_allocs << kd->pageshift) + *vpoffset);
			}
		}
	}
	KVM_ERROR_1("can't find anon ptr in swapinfo list");
	return (-1);
}

/*
 * Find physical address correspoding to an address space/offset
 * Returns -1 if address does not correspond to any physical memory
 */
u_longlong_t
kvm_physaddr(kd, as, vaddr)
	kvm_t *kd;
	struct as *as;
	u_int vaddr;
{
	int fd;
	u_longlong_t off;

	_kvm_physaddr(kd, as, vaddr, &fd, &off);
	return (fd == kd->corefd) ? off : (-1);
}

/* internal interface that also finds swap offset if any */
/* fd is either kd->corefd if in core, kd->swapfd if in swap or -1 if nowhere */
_kvm_physaddr(kd, as, vaddr, fdp, offp)
	kvm_t *kd;
	struct as *as;
	u_int vaddr;
	int *fdp;
	u_longlong_t *offp;
{
	struct seg s, *seg, *fseg;

	*fdp = -1;
	/* get first seg structure */
	seg = &s;
	if (as->a_segs.list == kd->Kas.a_segs.list) {
		fseg = (vaddr < (u_int)kd->Kseg.s_base) ?
			&kd->Ktextseg : &kd->Kseg;
	} else {
		fseg = &s;
		if (as->a_lrep == AS_LREP_LINKEDLIST) {
			getkvm(as->a_segs.list, fseg,
			    "1st user segment descriptor");
		} else {
			seg_skiplist ssl;

			getkvm(as->a_segs.skiplist, &ssl,
			    "1st segment skiplist structure");
			getkvm(ssl.segs[0], fseg,
			    "1st user segment descriptor");
		}
	}

	/* Make sure we've got the right seg structure for this address */
	if (getseg(kd, fseg, (caddr_t)vaddr, seg) == -1) 
		return (-1);

	if (seg->s_ops == kd->segvn) {
		/* Segvn segment */
		struct segvn_data sdata;
		u_int off;
		struct vnode *vp;
		u_int vpoff;

		off = (long)vaddr & kd->pageoffset;

		/* get private data for segment */
		if (seg->s_data == NULL) {
			KVM_ERROR_1("NULL segvn_data ptr in segment");
			return (-1);
		}
		getkvm(seg->s_data, &sdata, "segvn_data");

		/* Try anonymous pages first */
		if (sdata.amp != NULL) {
			struct anon_map amap;
			struct anon **anp;
			struct anon *ap;
			u_int apsz;

			/* get anon_map structure */
			getkvm(sdata.amp, &amap, "anon_map");
			if (amap.anon == NULL)
				goto notanon;

			/* get space for anon pointer array */
			apsz = ((amap.size + kd->pageoffset) >> kd->pageshift) *
				sizeof (struct anon *);
			if (apsz == 0)
				goto notanon;
			if ((anp = (struct anon **)malloc(apsz)) == NULL) {
				KVM_PERROR_1("anon pointer array");
				return (-1);
			}

			/* read anon pointer array */
			if (kvm_read(kd, (u_long)amap.anon,
			    (char *)anp, apsz) != apsz) {
				KVM_ERROR_1("error reading anon ptr array");
				free((char *)anp);
				return (-1);
			}

			/* index into anon ptrs to find the right one */
			ap = anp[sdata.anon_index +
			    ((caddr_t)vaddr - seg->s_base) >> kd->pageshift];
			free((char *)anp); /* no longer need this */
			if (ap == NULL)
				goto notanon;
			anon_to_fdoffset(kd, ap, fdp, offp, off);
			return (0);
		}
notanon:

		/* If not in anonymous; try the vnode */
		vp = sdata.vp;
		vpoff = sdata.offset + ((caddr_t)vaddr - seg->s_base);

		/*
		 * If vp is null then the page doesn't exist.
		 */
		if (vp != NULL) {
			vp_to_fdoffset(kd, vp, fdp, offp, vpoff);
		} else {
			KVM_ERROR_1("Can't translate virtual address");
			return (-1);
		}
	} else if (seg->s_ops == kd->segkmem) {
		/* Segkmem segment */
		register int poff;
		register u_longlong_t paddr;
		register u_int p;
		register u_int vbase;

		if (as->a_segs.list != kd->Kas.a_segs.list)
			return (-1);

		poff = vaddr & kd->pageoffset;	/* save offset into page */
		vaddr &= kd->pagemask;		/* rnd down to start of page */

		/*
		 * First, check if it's in kvtop.
		 */
		if ((paddr = __kvm_kvtop(kd, vaddr)) == -1) {
			struct page *pp;
			struct page pg;

			/*
			 * Next, try the page hash table.
			 */
			if ((pp = pagefind(kd, &pg, kd->kvp, vaddr)) == NULL)
				return (-1);
			if ((paddr = page_to_physaddr(kd, pp)) == -1)
				return (-1);
		}
		*fdp = kd->corefd;
		*offp = paddr + poff;
	} else if (seg->s_ops == kd->segmap) {
		/* Segmap segment */
		struct segmap_data sdata;
		struct smap *smp;
		struct smap *smap;
		u_int smpsz;
		u_int off;
		struct vnode *vp = NULL;
		u_int vpoff;

		off = (long)vaddr & MAXBOFFSET;
		vaddr &= MAXBMASK;

		/* get private data for segment */
		if (seg->s_data == NULL) {
			KVM_ERROR_1("NULL segmap_data ptr in segment");
			return (-1);
		}
		getkvm(seg->s_data, &sdata, "segmap_data");

		/* get space for smap array */
		smpsz = (seg->s_size >> MAXBSHIFT) * sizeof (struct smap);
		if (smpsz == 0) {
			KVM_ERROR_1("zero-length smap array");
			return (-1);
		}
		if ((smap = (struct smap *)malloc(smpsz)) == NULL) {
			KVM_ERROR_1("no memory for smap array");
			return (-1);
		}

		/* read smap array */
		if (kvm_read(kd, (u_long)sdata.smd_sm,
		    (char *)smap, smpsz) != smpsz) {
			KVM_ERROR_1("error reading smap array");
			(void) free((char *)smap);
			return (-1);
		}

		/* index into smap array to find the right one */
		smp = &smap[((caddr_t)vaddr - seg->s_base)>>MAXBSHIFT];
		vp = smp->sm_vp;

		if (vp == NULL) {
			KVM_ERROR_1("NULL vnode ptr in smap");
			free((char *)smap); /* no longer need this */
			return (-1);
		}
		vpoff = smp->sm_off + off;
		free((char *)smap); /* no longer need this */
		vp_to_fdoffset(kd, vp, fdp, offp, vpoff);
	} else if (seg->s_ops == kd->segkp) {
		return (getsegkp(kd, seg, vaddr, fdp, offp));
	} else if (seg->s_ops == kd->segdev) {
		KVM_ERROR_1("cannot read segdev segments yet");
		return (-1);
	} else {
		KVM_ERROR_1("unknown segment type");
		return (-1);
	}
	return (0);
}

/* convert anon pointer/offset to fd (swap, core or nothing) and offset */
static int
anon_to_fdoffset(kd, ap, fdp, offp, aoff)
	kvm_t *kd;
	struct anon *ap;
	int *fdp;
	u_longlong_t *offp;
	u_int aoff;
{
	struct page *paddr;
	struct page page;
	int swapoff;
	struct vnode *vp;
	u_int vpoff;
	u_longlong_t skaddr;

	if (ap == NULL) {
		KVM_ERROR_1("anon_to_fdoffset: null anon ptr");
		return (-1);
	}

	if ((swapoff = _anonoffset(kd, ap, &vp, &vpoff)) == -1) {
		return (-1);
	}

	/* try hash table in case page is still around */
	paddr = pagefind(kd, &page, vp, vpoff);
	if (paddr == NULL) {
		*fdp = kd->swapfd;
		*offp = swapoff + aoff;
		return (0);
	}

gotpage:
	/* make sure the page structure is useful */
	if (page.p_selock == -1) {
		KVM_ERROR_1("anon page is gone");
		return (-1);
	}

	/*
	 * Page is in core.
	 */
	skaddr = page_to_physaddr(kd, paddr);
	if (skaddr == -1) {
		KVM_ERROR_2("anon_to_fdoffset: can't find page 0x%x", paddr);
		return (-1);
	}
	*fdp = kd->corefd;
	*offp = skaddr + aoff;
	return (0);
}

/* convert vnode pointer/offset to fd (core or nothing) and offset */
static int
vp_to_fdoffset(kd, vp, fdp, offp, vpoff)
	kvm_t *kd;
	struct vnode *vp;
	int *fdp;
	u_longlong_t *offp;
	u_int vpoff;
{
	struct page *paddr;
	struct page page;
	u_int off;
	u_longlong_t skaddr;

	off = vpoff & kd->pageoffset;
	vpoff &= kd->pagemask;

	if (vp == NULL) {
		KVM_ERROR_1("vp_to_fdoffset: null vp ptr");
		return (-1);
	}

	paddr = pagefind(kd, &page, vp, vpoff);
	if (paddr == NULL) {
		KVM_ERROR_1("vp_to_fdoffset: page not mapped in");
		return (-1);
	}

	/* make sure the page structure is useful */
	if (page.p_selock == -1) {
		KVM_ERROR_1("vp_to_fdoffset: page is gone");
		return (-1);
	}

	/*
	 * Page is in core.
	 */
	skaddr = page_to_physaddr(kd, paddr);
	if (skaddr == -1) {
		KVM_ERROR_2("vp_to_fdoffset: can't find page 0x%x", paddr);
		return (-1);
	}
	*fdp = kd->corefd;
	*offp = skaddr + off;
	return (0);
}

/* convert page pointer to physical address */
static u_longlong_t
page_to_physaddr(kd, paddr)
	kvm_t *kd;
	struct page *paddr;
{
	struct memseg memseg, *memsegp;

	/*
	 * Find out which memory segment page is in.
	 */
	for (memsegp = &kd->memseg; ; memsegp = &memseg) {
		if (paddr >= memsegp->pages && paddr < memsegp->epages)
			return ((u_longlong_t)((paddr - memsegp->pages) +
			    memsegp->pages_base) << kd->pageshift);
		else
			getkvm(memsegp->next,&memseg,"next memseg");
	 }
}

/*
 *
 */
static int
getswappage(kd, ap, aoff, buf, rsize)
	kvm_t *kd;
	struct anon *ap;
	u_int aoff;
	caddr_t buf;
	u_int rsize;
{
	struct vnode *vp;
	u_int vpoff;
	struct page *paddr;
	struct page page;
	u_longlong_t skaddr;
	struct page *pages;
	struct anon anon;
	u_int pages_base;
	int swapoff;

	if ((swapoff = _anonoffset(kd, ap, &vp, &vpoff)) == -1) {
		return (-1);
	}

#if 0
	getkvm(ap, &anon, "anon structure");
	
	/*
	 * If there is a page structure pointer,
	 * make sure it is valid.  If not, try the
	 * hash table in case the page is free but
	 * reclaimable.
	 */
	paddr = anon.un.an_page;
	if (paddr != NULL) {
		getkvm(paddr, &page, "anon page structure");
		if ((page.p_vnode == vp) &&
		    (vpoff == page.p_offset)) {
			goto gotpage;
		}
		KVM_ERROR_1("anon page struct invalid");
	}
#endif
	
	/* try hash table in case page is still around */
	paddr = pagefind(kd, &page, vp, vpoff);
	if (paddr == NULL)
		goto tryswap;
	
 gotpage:
	/* make sure the page structure is useful */
	if (page.p_selock == -1) {
		KVM_ERROR_1("anon page is gone");
		return (-1);
	}
	
	/*
	 * Page is in core (probably).
	 * XXX - the following is going to change when
	 *	 tuples are implemented
	 */
	
	if ((paddr >= kd->memseg.pages) &&
	    (paddr < kd->memseg.epages)) {
		pages = kd->memseg.pages;
		pages_base = kd->memseg.pages_base;
	} else {
		struct memseg memseg;
		struct memseg *next;

		/* not in first memseg */
		next = kd->memseg.next;
		memseg.pages = kd->memseg.pages;
		memseg.epages = kd->memseg.epages;
		while (paddr < memseg.pages ||
		    paddr >= memseg.epages) {
			getkvm(next, &memseg, "next memory segment");
			next = memseg.next;
		}
		pages = memseg.pages;
		pages_base = memseg.pages_base;
	}
	skaddr = aoff +
		((u_longlong_t)((paddr - pages) + pages_base) << kd->pageshift);
	if (_uncondense(kd, kd->corefd, &skaddr)) {
		KVM_ERROR_2("%s: anon page uncondense error", kd->core);
		return (-1);
	}
	if (llseek(kd->corefd, (offset_t)skaddr, L_SET) == -1) {
		KVM_PERROR_2("%s: anon page seek error", kd->core);
		return (-1);
	}
	if (read(kd->corefd, buf, rsize) != rsize) {
		KVM_PERROR_2("%s: anon page read error", kd->core);
		return (-1);
	}
	goto readok;
	
 tryswap:
	/*
	 * If no page structure, page is swapped out
	 */
	if (kd->swapfd == -1)
		return (-1);

	if (_swapread(kd, (long)swapoff, aoff, buf, rsize) != rsize) {
		KVM_PERROR_2("%s: anon page read error", kd->swap);
		return (-1);
	}
 readok:
	return (0);
}
