/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986, 1987, 1988, 1989, 1990, 1991  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

/*
 * VM - segment for non-faulting loads.
 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/debug.h>

#include <vm/page.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/vpage.h>

/*
 * Private seg op routines.
 */
static int	segnf_dup(struct seg *, struct seg *);
static int	segnf_unmap(struct seg *, caddr_t, u_int);
static void	segnf_free(struct seg *);
static faultcode_t segnf_nomap(void);
static int	segnf_setprot(struct seg *, caddr_t, u_int, u_int);
static int	segnf_checkprot(struct seg *, caddr_t, u_int, u_int);
static void	segnf_badop(void);
static int	segnf_nop(void);
static int	segnf_getprot(struct seg *, caddr_t, u_int, u_int *);
static off_t	segnf_getoffset(struct seg *, caddr_t);
static int	segnf_gettype(struct seg *, caddr_t);
static int	segnf_getvp(struct seg *, caddr_t, struct vnode **);
static void	segnf_dump(struct seg *);


struct seg_ops segnf_ops = {
	segnf_dup,
	segnf_unmap,
	segnf_free,
	(faultcode_t (*)(struct hat *, struct seg *, caddr_t, u_int,
	    enum fault_type, enum seg_rw))
		segnf_nomap,		/* fault */
	(faultcode_t (*)(struct seg *, caddr_t))
		segnf_nomap,		/* faulta */
	segnf_setprot,
	segnf_checkprot,
	(int (*)())segnf_badop,		/* kluster */
	(u_int (*)(struct seg *))NULL,	/* swapout */
	(int (*)(struct seg *, caddr_t, u_int, int, u_int))
		segnf_nop,		/* sync */
	(int (*)(struct seg *, caddr_t, u_int, char *))
		segnf_nop,		/* incore */
	(int (*)(struct seg *, caddr_t, u_int, int, int, u_long *, size_t))
		segnf_nop,		/* lockop */
	segnf_getprot,
	segnf_getoffset,
	segnf_gettype,
	segnf_getvp,
	(int (*)(struct seg *, caddr_t, u_int, int))
		segnf_nop,		/* advise */
	segnf_dump,
};

/*
 * vnode and page for the page of zeros we use for the nf mappings.
 */
struct vnode zvp;
struct page *zpp;

static void
segnf_init()
{
	zpp = page_create_va(&zvp, 0, PAGESIZE, PG_WAIT, &kas, 0);
	pagezero(zpp, 0, PAGESIZE);
}

/*
 * Create a no-fault segment.
 */
int
segnf_create(struct seg *seg, void *argsp)
{
	register int error, prot;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	if (zpp == NULL)
		segnf_init();

	error = hat_map(seg->s_as->a_hat, seg->s_as, seg->s_base,
	    seg->s_size, HAT_MAP);
	if (error != 0)
		return (error);

	/*
	 * s_data can't be NULL because of ASSERTS in the common vm code.
	 */
	seg->s_ops = &segnf_ops;
	seg->s_data = seg;

	prot = PROT_READ;
	if (seg->s_as != &kas)
		prot |= PROT_USER;
	hat_memload(seg->s_as->a_hat, seg->s_as, seg->s_base,
		    zpp, prot, HAT_LOAD | HAT_NOFAULT);
	return (0);
}

/*
 * Duplicate seg and return new segment in newseg.
 */
static int
segnf_dup(struct seg *seg, struct seg *newseg)
{
	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	newseg->s_ops = seg->s_ops;
	newseg->s_data = seg->s_data;

	hat_memload(newseg->s_as->a_hat, newseg->s_as, newseg->s_base,
		    zpp, PROT_READ | PROT_USER, HAT_LOAD | HAT_NOFAULT);
	return (0);
}

/*
 * Split a segment at addr for length len.
 */
static int
segnf_unmap(register struct seg *seg, register caddr_t addr, u_int len)
{
	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * Check for bad sizes
	 */
	if (addr != seg->s_base || len != PAGESIZE)
		cmn_err(CE_PANIC, "segnf_unmap");

	/*
	 * Unload any hardware translations in the range to be taken out.
	 */
	hat_unmap(seg->s_as, addr, len, HAT_UNMAP);
	seg_free(seg);

	return (0);
}

/*
 * Free a segment.
 */
static void
segnf_free(struct seg *seg)
{
	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));
}

/*
 * No faults allowed on segnf.
 */
static faultcode_t
segnf_nomap(void)
{
	return (FC_NOMAP);
}

static int
segnf_setprot(struct seg * seg, caddr_t addr, u_int len, u_int prot)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));
	return (EACCES);
}

static int
segnf_checkprot(struct seg * seg, caddr_t addr, u_int len, u_int prot)
{
	u_int sprot;
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	sprot = seg->s_as == &kas ?  PROT_READ : PROT_READ|PROT_USER;
	return ((prot & sprot) == prot ? 0 : EACCES);
}

static void
segnf_badop(void)
{
	cmn_err(CE_PANIC, "segnf_badop");
	/*NOTREACHED*/
}

static int
segnf_nop(void)
{
	return (0);
}

static int
segnf_getprot(
	register struct seg *seg,
	register caddr_t addr,
	register u_int len,
	register u_int *protv)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));
	ASSERT(seg->s_base == addr);

	protv[0] = PROT_READ;
	return (0);
}

static off_t
segnf_getoffset(register struct seg *seg, caddr_t addr)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));
	ASSERT(seg->s_base == addr);

	return (0);
}

static int
segnf_gettype(register struct seg *seg, caddr_t addr)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));
	ASSERT(seg->s_base == addr);

	return (MAP_SHARED);
}

static int
segnf_getvp(register struct seg *seg, caddr_t addr, struct vnode **vpp)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));
	ASSERT(seg->s_base == addr);

	*vpp = &zvp;
	return (0);
}

/*
 * segnf pages are not dumped, so we just return
 */
static void
segnf_dump(struct seg *seg)
{}
