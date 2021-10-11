/*
 * Copyright (c) 1991-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)ddi_impl.c	1.18	95/03/31 SMI"

/*
 * sun4u specific DDI implementation
 */

/*
 * indicate that this is the implementation code.
 */
#define	SUNDDI_IMPL

#include <sys/types.h>
#include <sys/param.h>
#include <sys/tuneable.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/cred.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <sys/kmem.h>
#include <sys/kmem_impl.h>
#include <sys/vm.h>
#include <vm/hat.h>
#include <vm/seg.h>
#include <vm/as.h>
#include <sys/spl.h>

#include <sys/dditypes.h>
#include <sys/ddidmareq.h>
#include <sys/devops.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_implfuncs.h>
#include <sys/sysiosbus.h>
#include <sys/mman.h>
#include <sys/map.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/promif.h>
#include <sys/modctl.h>
#include <sys/autoconf.h>
#include <sys/avintr.h>
#include <sys/machsystm.h>

/*
 * DDI(Sun) Function and flag definitions:
 */

caddr_t	kalloca(u_int, int, int, int);
void kfreea(caddr_t);

/*
 * Enable DDI_MAP_DEBUG for map debugging messages...
 * (c.f. rootnex.c)
 * #define	DDI_MAP_DEBUG
 */

#ifdef	DDI_MAP_DEBUG
int ddi_map_debug_flag = 1;
#define	ddi_map_debug	if (ddi_map_debug_flag) printf
#endif	DDI_MAP_DEBUG

/*
 * i_ddi_bus_map:
 * Generic bus_map entry point, for byte addressable devices
 * conforming to the reg/range addressing model with no HAT layer
 * to be programmed at this level.
 */

int
i_ddi_bus_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
	off_t offset, off_t len, caddr_t *vaddrp)
{
	struct regspec tmp_reg, *rp;
	ddi_map_req_t mr = *mp;		/* Get private copy of request */
	int error;

	mp = &mr;

	/*
	 * First, if given an rnumber, convert it to a regspec...
	 */

	if (mp->map_type == DDI_MT_RNUMBER)  {

		int rnumber = mp->map_obj.rnumber;
#ifdef	DDI_MAP_DEBUG
		static char *out_of_range =
		    "i_ddi_bus_map: Out of range rnumber <%d>, device <%s>";
#endif	DDI_MAP_DEBUG

		rp = i_ddi_rnumber_to_regspec(rdip, rnumber);
		if (rp == (struct regspec *)0)  {
#ifdef	DDI_MAP_DEBUG
			cmn_err(CE_WARN, out_of_range, rnumber,
			    ddi_get_name(rdip));
#endif	DDI_MAP_DEBUG
			return (DDI_ME_RNUMBER_RANGE);
		}

		/*
		 * Convert the given ddi_map_req_t from rnumber to regspec...
		 */

		mp->map_type = DDI_MT_REGSPEC;
		mp->map_obj.rp = rp;
	}

	/*
	 * Adjust offset and length correspnding to called values...
	 * XXX: A non-zero length means override the one in the regspec.
	 * XXX: (Regardless of what's in the parent's range)
	 */

	tmp_reg = *(mp->map_obj.rp);		/* Preserve underlying data */
	rp = mp->map_obj.rp = &tmp_reg;		/* Use tmp_reg in request */

	rp->regspec_addr += (u_int)offset;
	if (len != 0)
		rp->regspec_size = (u_int)len;

	/*
	 * If we had an MMU, this is where you'd program the MMU and hat layer.
	 * Since we're using the default function here, we do not have an MMU
	 * to program.
	 */

	/*
	 * Apply any parent ranges at this level, if applicable.
	 * (This is where nexus specific regspec translation takes place.
	 * Use of this function is implicit agreement that translation is
	 * provided via ddi_apply_range.)  Note that we assume that
	 * the request is within the parents limits.
	 */

#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("applying range of parent <%s> to child <%s>...\n",
	    ddi_get_name(dip), ddi_get_name(rdip));
#endif	DDI_MAP_DEBUG

	if ((error = i_ddi_apply_range(dip, rdip, mp->map_obj.rp)) != 0)
		return (error);

	/*
	 * Call my parents bus_map function with modified values...
	 */

	return (ddi_map(dip, mp, (off_t)0, (off_t)0, vaddrp));
}

/*
 * Creating register mappings and handling interrupts:
 */

struct regspec *
i_ddi_rnumber_to_regspec(dev_info_t *dip, int rnumber)
{

	if (rnumber > sparc_pd_getnreg(DEVI(dip)))
		return ((struct regspec *)0);

	return (sparc_pd_getreg(DEVI(dip), rnumber));
}

/*
 * Static function to determine if a reg prop is enclosed within
 * a given a range spec.  (For readability: only used by i_ddi_aply_range.).
 */
static int
reg_is_enclosed_in_range(struct regspec *rp, struct rangespec *rangep)
{
	if (rp->regspec_bustype != rangep->rng_cbustype)
		return (0);

	if (rp->regspec_addr < rangep->rng_coffset)
		return (0);

	if (rangep->rng_size == 0)
		return (1);	/* size is really 2**(bits_per_word) */

	if ((rp->regspec_addr + rp->regspec_size - 1) <=
	    (rangep->rng_coffset + rangep->rng_size - 1))
		return (1);

	return (0);
}

/*
 * i_ddi_apply_range:
 * Apply range of dp to struct regspec *rp, if applicable.
 * If there's any range defined, it gets applied.
 */

int
i_ddi_apply_range(dev_info_t *dp, dev_info_t *rdip, struct regspec *rp)
{
	int nrange, b;
	struct rangespec *rangep;
	static char *out_of_range =
	    "Out of range register specification from device node <%s>\n";

	nrange = sparc_pd_getnrng(dp);
	if (nrange == 0)  {
#ifdef	DDI_MAP_DEBUG
		ddi_map_debug("    No range.\n");
#endif	DDI_MAP_DEBUG
		return (0);
	}

	/*
	 * Find a match, making sure the regspec is within the range
	 * of the parent, noting that a size of zero in a range spec
	 * really means a size of 2**(bitsperword).
	 */

	for (b = 0, rangep = sparc_pd_getrng(dp, 0); b < nrange; ++b, ++rangep)
		if (reg_is_enclosed_in_range(rp, rangep))
			break;		/* found a match */

	if (b == nrange)  {
		cmn_err(CE_WARN, out_of_range, ddi_get_name(rdip));
		return (DDI_ME_REGSPEC_RANGE);
	}

#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("    Input:  %x.%x.%x\n", rp->regspec_bustype,
	    rp->regspec_addr, rp->regspec_size);
	ddi_map_debug("    Range:  %x.%x %x.%x %x\n",
	    rangep->rng_cbustype, rangep->rng_coffset,
	    rangep->rng_bustype, rangep->rng_offset, rangep->rng_size);
#endif	DDI_MAP_DEBUG

	rp->regspec_bustype = rangep->rng_bustype;
	rp->regspec_addr += rangep->rng_offset - rangep->rng_coffset;

#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("    Return: %x.%x.%x\n", rp->regspec_bustype,
	    rp->regspec_addr, rp->regspec_size);
#endif	DDI_MAP_DEBUG

	return (0);
}

/*
 * i_ddi_map_fault: wrapper for bus_map_fault.
 */
int
i_ddi_map_fault(dev_info_t *dip, dev_info_t *rdip,
	struct hat *hat, struct seg *seg, caddr_t addr,
	struct devpage *dp, u_int pfn, u_int prot, u_int lock)
{
	dev_info_t *pdip;

	if (dip == NULL)
		return (DDI_FAILURE);

	pdip = (dev_info_t *)DEVI(dip)->devi_bus_map_fault;

	/* request appropriate parent to map fault */
	return ((*(DEVI(pdip)->devi_ops->devo_bus_ops->bus_map_fault))(pdip,
	    rdip, hat, seg, addr, dp, pfn, prot, lock));
}

/*
 * i_ddi_get_intrspec:	convert an interrupt number to an interrupt
 *			specification. The interrupt number determines which
 *			interrupt will be returned if more than one exists.
 *			returns an interrupt specification if successful and
 *			NULL if the interrupt specification could not be found.
 *			If "name" is NULL, first (and only) interrupt
 *			name is searched for.  this is the wrapper for the
 *			bus function bus_get_intrspec.
 */
ddi_intrspec_t
i_ddi_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int inumber)
{
	dev_info_t *pdip = (dev_info_t *)DEVI(dip)->devi_parent;

	/* request parent to return an interrupt specification */
	return ((*(DEVI(pdip)->devi_ops->devo_bus_ops->bus_get_intrspec))(pdip,
	    rdip, inumber));
}

/*
 * i_ddi_add_intrspec:	Add an interrupt specification.	If successful,
 *			the parameters "iblock_cookiep", "device_cookiep",
 *			"int_handler", "int_handler_arg", and return codes
 *			are set or used as specified in "ddi_add_intr". This
 *			is the wrapper for the bus function bus_add_intrspec.
 */
int
i_ddi_add_intrspec(dev_info_t *dip, dev_info_t *rdip, ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg, int kind)
{
	dev_info_t *pdip = (dev_info_t *)DEVI(dip)->devi_parent;

	/* request parent to add an interrupt specification */
	return ((*(DEVI(pdip)->devi_ops->devo_bus_ops->bus_add_intrspec))(pdip,
	    rdip, intrspec, iblock_cookiep, idevice_cookiep,
	    int_handler, int_handler_arg, kind));
}


/*
 * Soft interrupt priorities.  patterned after 4M.
 * The entries of this table correspond to system level PIL's.  I'd
 * prefer this table lived in the sbus nexus, but it didn't seem
 * like an easy task with the DDI soft interrupt framework.
 * XXX The 4m priorities are probably not a good choice here. (RAZ)
 */
static int soft_interrupt_priorities[] = {
	4,				/* Low soft interrupt */
	4,				/* Medium soft interrupt */
	6};				/* High soft interrupt */

/*
 * i_ddi_add_softintr - add a soft interrupt to the system
 */
int
i_ddi_add_softintr(dev_info_t *rdip, int preference, ddi_softintr_t *idp,
	ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg)
{
	dev_info_t *rp;
	struct soft_intrspec *sspec;
	struct intrspec *ispec;
	int r;

	if (idp == NULL)
		return (DDI_FAILURE);
	sspec = (struct soft_intrspec *)kmem_zalloc(sizeof (*sspec), KM_SLEEP);
	sspec->si_devi = (struct dev_info *)rdip;
	ispec = &sspec->si_intrspec;

	if (preference <= DDI_SOFTINT_LOW)
		ispec->intrspec_pri = soft_interrupt_priorities[0];

	else if (preference > DDI_SOFTINT_LOW && preference <= DDI_SOFTINT_MED)
		ispec->intrspec_pri = soft_interrupt_priorities[1];
	else
		ispec->intrspec_pri = soft_interrupt_priorities[2];

	rp = ddi_root_node();
	r = (*(DEVI(rp)->devi_ops->devo_bus_ops->bus_add_intrspec))(rp,
	    rdip, (ddi_intrspec_t)ispec, iblock_cookiep, idevice_cookiep,
	    int_handler, int_handler_arg, IDDI_INTR_TYPE_SOFT);
	if (r != DDI_SUCCESS) {
		kmem_free((caddr_t)sspec, sizeof (*sspec));
		return (r);
	}
	*idp = (ddi_softintr_t)sspec;
	return (DDI_SUCCESS);
}

void
i_ddi_trigger_softintr(ddi_softintr_t id)
{
	u_int pri = ((struct soft_intrspec *)id)->si_intrspec.intrspec_pri;
	/* ICK! */
	setsoftint(pri);
}

void
i_ddi_remove_softintr(ddi_softintr_t id)
{
	struct soft_intrspec *sspec = (struct soft_intrspec *)id;
	struct intrspec *ispec = &sspec->si_intrspec;
	dev_info_t *rp = ddi_root_node();
	dev_info_t *rdip = (dev_info_t *)sspec->si_devi;

	(*(DEVI(rp)->devi_ops->devo_bus_ops->bus_remove_intrspec))(rp,
	    rdip, (ddi_intrspec_t)ispec, (ddi_iblock_cookie_t)0);
	kmem_free((caddr_t)sspec, sizeof (*sspec));
}


/*
 * i_ddi_remove_intrspec: this is a wrapper for the bus function
 *			bus_remove_intrspec.
 */
void
i_ddi_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookie)
{
	dev_info_t *pdip = (dev_info_t *)DEVI(dip)->devi_parent;

	/* request parent to remove an interrupt specification */
	(*(DEVI(pdip)->devi_ops->devo_bus_ops->bus_remove_intrspec))(pdip,
	    rdip, intrspec, iblock_cookie);
}

#define	NOSPLX	((int)0x80000000)
void
i_ddi_splx(int s)
{
	if (s != NOSPLX)
		(void) splx(s);
}

int
i_ddi_splaudio(void)
{
	/*CONSTANTCONDITION*/
	if (ipltospl(13) <= ipltospl(LOCK_LEVEL))
		return (NOSPLX);
	else
		return (splr(ipltospl(13)));
}

int
i_ddi_splhigh(void)
{
	/*CONSTANTCONDITION*/
	if (ipltospl(10) <= ipltospl(LOCK_LEVEL))
		return (NOSPLX);
	else
		return (splr(ipltospl(10)));
}

int
i_ddi_splclock(void)
{
	/*CONSTANTCONDITION*/
	if (ipltospl(10) <= ipltospl(LOCK_LEVEL))
		return (NOSPLX);
	else
		return (splr(ipltospl(10)));
}

int
i_ddi_spltty(void)
{
	/*CONSTANTCONDITION*/
	if (ipltospl(10) <= ipltospl(LOCK_LEVEL))
		return (NOSPLX);
	else
		return (splr(ipltospl(10)));
}

int
i_ddi_splbio(void)
{
	/*CONSTANTCONDITION*/
	if (ipltospl(10) <= ipltospl(LOCK_LEVEL))
		return (NOSPLX);
	else
		return (splr(ipltospl(10)));
}

int
i_ddi_splimp(void)
{
	/*CONSTANTCONDITION*/
	if (ipltospl(6) <= ipltospl(LOCK_LEVEL))
		return (NOSPLX);
	else
		return (splr(ipltospl(6)));
}

int
i_ddi_splnet(void)
{
	/*CONSTANTCONDITION*/
	if (ipltospl(6) <= ipltospl(LOCK_LEVEL))
		return (NOSPLX);
	else
		return (splr(ipltospl(6)));
}

int
i_ddi_splstr(void)
{
	/*CONSTANTCONDITION*/
	if (ipltospl(10) <= ipltospl(LOCK_LEVEL))
		return (NOSPLX);
	else
		return (splr(ipltospl(10)));
}

/*
 * Functions for nexus drivers only:
 */

/*
 * config stuff
 */
dev_info_t *
i_ddi_add_child(dev_info_t *pdip, char *name, u_int nodeid, u_int unit)
{
	struct dev_info *devi;
	char buf[16];

	devi = (struct dev_info *)kmem_zalloc(sizeof (*devi), KM_SLEEP);
	devi->devi_name = kmem_alloc(strlen(name) + 1, KM_SLEEP);
	strcpy(devi->devi_name,  name);
	devi->devi_nodeid = nodeid;
	devi->devi_instance = unit;
	(void) sprintf(buf, "di %x", (int)devi);
	mutex_init(&(devi->devi_lock), buf, MUTEX_DEFAULT, NULL);
	ddi_append_dev(pdip, (dev_info_t *)devi);
	return ((dev_info_t *)devi);
}

int
i_ddi_remove_child(dev_info_t *dip, int lockheld)
{
	register struct dev_info *pdev, *cdev;
	major_t major;

	if ((dip == (dev_info_t *)0) ||
	    (DEVI(dip)->devi_child != (struct dev_info *)0) ||
	    ((pdev = DEVI(dip)->devi_parent) == (struct dev_info *)0)) {
		return (DDI_FAILURE);
	}

	rw_enter(&(devinfo_tree_lock), RW_WRITER);
	cdev = pdev->devi_child;

	/*
	 * Remove 'dip' from its parents list of children
	 */
	if (cdev == (struct dev_info *)dip) {
		pdev->devi_child = cdev->devi_sibling;
	} else {
		while (cdev != (struct dev_info *)NULL) {
			if (cdev->devi_sibling == DEVI(dip)) {
				cdev->devi_sibling = DEVI(dip)->devi_sibling;
				cdev = DEVI(dip);
				break;
			}
			cdev = cdev->devi_sibling;
		}
	}
	rw_exit(&(devinfo_tree_lock));
	if (cdev == (struct dev_info *)NULL)
		return (DDI_FAILURE);

	/*
	 * Remove 'dip' from the list held on the devnamesp table.
	 */
	major = ddi_name_to_major(ddi_get_name(dip));
	if (major != (major_t)-1)  {
		register kmutex_t *lp = &(devnamesp[major].dn_lock);

		if (lockheld == 0)
			LOCK_DEV_OPS(lp);
		cdev = DEVI(devnamesp[major].dn_head);
		if (cdev == DEVI(dip))  {
			devnamesp[major].dn_head =
			    (dev_info_t *)cdev->devi_next;
		} else while (cdev != NULL)  {
			if (cdev->devi_next == DEVI(dip))  {
				cdev->devi_next = DEVI(dip)->devi_next;
				break;
			}
			cdev = cdev->devi_next;
		}
		if (lockheld == 0)
			UNLOCK_DEV_OPS(lp);
	}

	/*
	 * Strip 'dip' clean and toss it over the side ..
	 */
	ddi_remove_minor_node(dip, NULL);
	impl_rem_dev_props(dip);
	mutex_destroy(&(DEVI(dip)->devi_lock));
	kmem_free(DEVI(dip)->devi_name,
	    (size_t)(strlen(DEVI(dip)->devi_name) + 1));
	kmem_free(dip, sizeof (struct dev_info));

	return (DDI_SUCCESS);
}

int
i_ddi_initchild(dev_info_t *prnt, dev_info_t *proto)
{
	int (*f)(dev_info_t *, dev_info_t *, ddi_ctl_enum_t, void *, void *);
	int error;

	if (DDI_CF1(proto))
		return (DDI_SUCCESS);

	ASSERT(prnt);
	ASSERT(DEVI(prnt) == DEVI(proto)->devi_parent);

	/*
	 * The parent must be in canonical form 2 in order to use its bus ops.
	 */
	if (impl_proto_to_cf2(prnt) != DDI_SUCCESS)
		return (DDI_FAILURE);

	/*
	 * The parent must have a bus_ctl operation.
	 */
	if ((DEVI(prnt)->devi_ops->devo_bus_ops == NULL) ||
	    (f = DEVI(prnt)->devi_ops->devo_bus_ops->bus_ctl) == NULL) {
		/*
		 * Release the dev_ops which were held in impl_proto_to_cf2().
		 */
		ddi_rele_devi(prnt);
		return (DDI_FAILURE);
	}

	/*
	 * Invoke the parent's bus_ctl operation with the DDI_CTLOPS_INITCHILD
	 * command to transform the child to canonical form 1. If there
	 * is an error, ddi_remove_child should be called, to clean up.
	 */
	error = (*f)(prnt, prnt, DDI_CTLOPS_INITCHILD, proto, (void *)0);
	if (error != DDI_SUCCESS)
		ddi_rele_devi(prnt);
	else {
		/*
		 * Apply multi-parent/deep-nexus optimization to the new node
		 */
		ddi_optimize_dtree(proto);
	}

	return (error);
}

/*
 * IOPB functions
 */

int
i_ddi_mem_alloc(dev_info_t *dip, ddi_dma_lim_t *limits,
	u_int length, int cansleep, int streaming,
	ddi_device_acc_attr_t *accattrp,
	caddr_t *kaddrp, u_int *real_length, ddi_acc_hdl_t *handlep)
{
	caddr_t a;
	int iomin, align;
	u_int endian_flags = DDI_NEVERSWAP_ACC;

#if defined(lint)
	*handlep = *handlep;
#endif

	/*
	 * Check legality of arguments
	 */

	if (dip == 0 || length == 0 || kaddrp == 0) {
		return (DDI_FAILURE);
	}

	if (limits) {
		if (limits->dlim_minxfer == 0 ||
		    (limits->dlim_minxfer & (limits->dlim_minxfer - 1))) {
			return (DDI_FAILURE);
		}
	}

	/*
	 * Get the minimum effective I/O transfer size
	 * It will be a power of two, so we can use it to
	 * enforce alignment and padding.
	 */

	if (limits) {
		/*
		 * If in streaming mode, pick the most efficient
		 * size by finding the largest burstsize. Else,
		 * pick the mincycle value.
		 */
		if (streaming && limits->dlim_burstsizes != 0) {
			iomin = 1 << (ddi_fls(limits->dlim_burstsizes) - 1);
		} else
			iomin = limits->dlim_minxfer;
	} else {
		/*
		 * If no limits specified, be safe and generous by
		 * assuming longword alignment for streaming mode
		 * or byte alignment otherwise. Remember that nexus
		 * drivers will adjust this as appropriate for I/O
		 * caches, etc.
		 */
		iomin = (streaming) ? sizeof (long) : 1;
	}
	iomin = ddi_iomin(dip, iomin, streaming);
	if (iomin == 0)
		return (DDI_FAILURE);

	ASSERT((iomin & (iomin - 1)) == 0);
	ASSERT(iomin >= ((limits) ? ((int)limits->dlim_minxfer) : iomin));

	length = roundup(length, iomin);
	align = iomin;

	if (accattrp != NULL)
		endian_flags = accattrp->devacc_attr_endian_flags;
	a = kalloca(length, align, cansleep, endian_flags);
	if ((*kaddrp = a) == 0) {
		return (DDI_FAILURE);
	} else {
		/*
		 * Make sure that this allocated address is mappable-
		 * use an advisory mapping call.
		 */
		if (ddi_dma_addr_setup(dip, &kas, a, length,
		    0, DDI_DMA_DONTWAIT, (caddr_t)0, limits,
		    (ddi_dma_handle_t *)0)) {
			kfreea(a);
			*kaddrp = (caddr_t)0;
			return (DDI_FAILURE);
		}
		if (real_length) {
			*real_length = length;
		}
		return (DDI_SUCCESS);
	}
}

/* ARGSUSED */
void
i_ddi_mem_free(caddr_t kaddr, int stream)
{
	kfreea(kaddr);
}

/*
 * Miscellaneous implementation functions
 */

#define	get_prop(di, pname, flag, pval, plen)	\
	(ddi_prop_op(DDI_DEV_T_NONE, di, PROP_LEN_AND_VAL_ALLOC, \
	flag | DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, \
	pname, (caddr_t)pval, plen))

/*
 * impl_ddi_merge_child:
 *
 * Framework function to merge .conf file nodes into a specifically
 * named hw devinfo node of the same name_addr, allowing .conf file
 * overriding of hardware properties. May be called from nexi, though the call
 * may result in the given "child" node being uninitialized and subject
 * to subsequent removal.  DDI_FAILURE indicates that the child has
 * been merged into another node, has been uninitialized and should
 * be removed and is not actually a failure, but should be returned
 * to the caller's caller.  DDI_SUCCESS means that the child was not
 * removed and was not merged into a hardware .conf node.
 */
static char *cantmerge = "Cannot merge %s devinfo node %s@%s";

int
impl_ddi_merge_child(dev_info_t *child)
{
	dev_info_t *parent, *och;
	char *name = ddi_get_name_addr(child);
	extern struct dev_ops *mod_hold_dev_by_devi(dev_info_t *);

	parent = ddi_get_parent(child);

	/*
	 * If another child already exists by this name,
	 * merge these properties into the other child.
	 *
	 * NOTE - This property override/merging depends on several things:
	 *
	 * 1) That hwconf nodes are 'named' (ddi_initchild()) before prom
	 *	devinfo nodes.
	 *
	 * 2) That ddi_findchild() will call ddi_initchild() for all
	 *	siblings with a matching devo_name field.
	 *
	 * 3) That hwconf devinfo nodes come "after" prom devinfo nodes.
	 *
	 * Then "och" should be a prom node with no attached properties.
	 */
	if ((och = ddi_findchild(parent, ddi_get_name(child), name)) != NULL &&
	    och != child) {
		if (ddi_get_nodeid(och) == DEVI_PSEUDO_NODEID ||
		    ddi_get_nodeid(child) != DEVI_PSEUDO_NODEID ||
		    DEVI(och)->devi_sys_prop_ptr ||
		    DEVI(och)->devi_drv_prop_ptr || DDI_CF2(och)) {
			cmn_err(CE_WARN, cantmerge, "hwconf",
			    ddi_get_name(child), name);
			/*
			 * Do an extra hold on the parent,
			 * to compensate for ddi_uninitchild's
			 * extra release of the parent. [ plus the
			 * release done by returning "failure" from
			 * this function. ]
			 */
			(void) mod_hold_dev_by_devi(parent);
			(void) ddi_uninitchild(child);
			return (DDI_FAILURE);
		}
		/*
		 * Move "child"'s properties to "och." and allow the node
		 * to be init-ed (this handles 'reg' and 'interrupts'
		 * in hwconf files overriding those in a hw node)
		 *
		 * Note that 'och' is not yet in canonical form 2, so we
		 * can happily transform it to prototype form and recreate it.
		 */
		(void) ddi_uninitchild(och);
		DEVI(och)->devi_sys_prop_ptr = DEVI(child)->devi_sys_prop_ptr;
		DEVI(och)->devi_drv_prop_ptr = DEVI(child)->devi_drv_prop_ptr;
		DEVI(child)->devi_sys_prop_ptr = NULL;
		DEVI(child)->devi_drv_prop_ptr = NULL;
		(void) ddi_initchild(parent, och);
		/*
		 * To get rid of this child...
		 *
		 * Do an extra hold on the parent, to compensate for
		 * ddi_uninitchild's extra release of the parent.
		 * [ plus the release done by returning "failure" from
		 * this function. ]
		 */
		(void) mod_hold_dev_by_devi(parent);
		(void) ddi_uninitchild(child);
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

/*
 * impl_ddi_merge_wildcard:
 *
 * Framework function to merge .conf file 'wildcard' nodes into all
 * previous named hw nodes of the same "name", with the same parent.
 *
 * This path is a bit different from impl_ddi_merge_child.  merge_child
 * merges a single child with the same exact name and address; this
 * function can merge into several nodes with the same driver name, only.
 *
 * This function may be used by buses which export a wildcarding mechanism
 * in .conf files, such as the "registers" mechanism, in sbus .conf files.
 * (The registers applies to all prev named nodes' reg properties.)
 *
 * This function always returns DDI_FAILURE as an indication to the
 * caller's caller, that the wildcard node has been uninitialized
 * and should be removed. (Presumably, it's properties have been copied
 * into the "merged" children of the same parent.)
 */
int
impl_ddi_merge_wildcard(dev_info_t *child)
{
	major_t major;
	dev_info_t *parent, *och;
	extern struct dev_ops *mod_hold_dev_by_devi(dev_info_t *);

	parent = ddi_get_parent(child);

	/*
	 * If the wildcard node has no properties, there is nothing to do...
	 */
	if ((DEVI(child)->devi_drv_prop_ptr == NULL) &&
	    (DEVI(child)->devi_sys_prop_ptr == NULL))  {
		/* Compensate for extra release done in ddi_uninitchild() */
		(void) mod_hold_dev_by_devi(parent);
		ddi_uninitchild(child);
		return (DDI_FAILURE);
	}

	/*
	 * Find all previously defined h/w children with the same name
	 * and same parent and copy the property lists from the
	 * prototype node into the h/w nodes and re-inititialize them.
	 */
	if ((major = ddi_name_to_major(ddi_get_name(child))) == -1)  {
		/* Compensate for extra release done in ddi_uninitchild() */
		(void) mod_hold_dev_by_devi(parent);
		ddi_uninitchild(child);
		return (DDI_FAILURE);
	}

	for (och = devnamesp[major].dn_head;
	    (och != NULL) && (och != child);
	    och = ddi_get_next(och))  {

		if ((ddi_get_parent(och) != parent) ||
		    (ddi_get_nodeid(och) == DEVI_PSEUDO_NODEID))
			continue;
		if (strcmp(ddi_get_name(och), ddi_get_name(child)) != 0)
			continue;

		if (DEVI(och)->devi_sys_prop_ptr ||
		    DEVI(och)->devi_drv_prop_ptr || DDI_CF2(och)) {
			cmn_err(CE_WARN, cantmerge, "wildcard",
			    ddi_get_name(och), ddi_get_name_addr(och));
			continue;
		}

		ddi_uninitchild(och);
		(void) copy_prop(DEVI(child)->devi_drv_prop_ptr,
		    &(DEVI(och)->devi_drv_prop_ptr));
		(void) copy_prop(DEVI(child)->devi_sys_prop_ptr,
		    &(DEVI(och)->devi_sys_prop_ptr));
		ddi_initchild(parent, och);
	}

	/*
	 * We can toss the wildcard node. Note that we do an extra
	 * hold on the parent to compensate for the extra release
	 * done in ddi_uninitchild(). [ plus the release already done
	 * by returning "failure" from this function. ]
	 */
	(void) mod_hold_dev_by_devi(parent);
	ddi_uninitchild(child);
	return (DDI_FAILURE);
}

/*
 * Create a ddi_parent_private_data structure from the ddi properties of
 * the dev_info node.
 *
 * The "reg" and "interrupts" properties are required
 * if the driver wishes to create mappings or field interrupts on behalf
 * of the device.
 *
 * The "reg" property is assumed to be a list of at least one triple
 *
 *	<bustype, address, size>*1
 *
 * The "interrupts" property is assumed to be a list of at least one
 * n-tuples that describes the interrupt capabilities of the bus the device
 * is connected to.  For SBus, this looks like
 *
 *	<SBus-level>*1
 *
 * For VME this looks like
 *
 *	<VME-level, VME-vector#>*1
 *
 * The "ranges" property describes the mapping of child addresses to parent
 * addresses.
 */
static void
make_ddi_ppd(dev_info_t *child, struct ddi_parent_private_data **ppd)
{
	register struct ddi_parent_private_data *pdptr;
	register int n;
	int *reg_prop, *rng_prop, *irupts_prop;
	int reg_len, rng_len, irupts_len;

	*ppd = pdptr = (struct ddi_parent_private_data *)
			kmem_zalloc(sizeof (*pdptr), KM_SLEEP);

	/*
	 * "reg" property ...
	 */

	if (get_prop(child, OBP_REG, 0, &reg_prop, &reg_len) != DDI_SUCCESS)
		reg_len = 0;

	if ((n = reg_len) != 0)  {
		pdptr->par_nreg = n / (int)sizeof (struct regspec);
		pdptr->par_reg = (struct regspec *)reg_prop;
	}

	/*
	 * "ranges" property ...
	 */
	if (get_prop(child, OBP_RANGES, 0, &rng_prop,
	    &rng_len) == DDI_SUCCESS) {
		pdptr->par_nrng = rng_len / (int)(sizeof (struct rangespec));
		pdptr->par_rng = (struct rangespec *)rng_prop;
	}

	/*
	 * "interrupts" ...
	 */
	if (get_prop(child, OBP_INTERRUPTS, 0, &irupts_prop, &irupts_len)
	    != DDI_SUCCESS) {
		irupts_len = 0;
	}

	if ((n = irupts_len) != 0) {
		size_t size;
		int *out;

		/*
		 * Translate the 'interrupts' property into an array
		 * of intrspecs for the rest of the DDI framework to
		 * toy with.  Only our ancestors really know how to
		 * do this, so ask 'em.  We massage the 'interrupts'
		 * property so that it is pre-pended by a count of
		 * the number of integers in the argument.
		 */
		size = sizeof (int) + n;
		out = kmem_alloc(size, KM_SLEEP);
		*out = n / sizeof (int);
		bcopy((caddr_t)irupts_prop, (caddr_t)(out + 1), (size_t)n);
		kmem_free(irupts_prop, irupts_len);
		if (ddi_ctlops(child, child, DDI_CTLOPS_XLATE_INTRS,
		    out, pdptr) != DDI_SUCCESS) {
			cmn_err(CE_CONT,
			    "Unable to translate 'interrupts' for %s%d\n",
			    DEVI(child)->devi_name,
			    DEVI(child)->devi_instance);
		}
		kmem_free(out, size);
	}
}

/*
 * Called from the bus_ctl op of some drivers.
 * to implement the DDI_CTLOPS_INITCHILD operation.  That is, it names
 * the children of sun busses based on the reg spec.
 *
 * Handles the following properties:
 *
 *	Property		value
 *	  Name			type
 *
 *	reg		register spec
 *	interrupts	new (bus-oriented) interrupt spec
 *	ranges		range spec
 *
 * NEW drivers should NOT use this function, but should declare
 * there own initchild/uninitchild handlers. (This function assumes
 * the layout of the parent private data and the format of "reg",
 * "ranges", "interrupts" properties and that #address-cells and
 * #size-cells of the parent bus are defined to be default values.)
 */
int
impl_ddi_sunbus_initchild(dev_info_t *child)
{
	struct ddi_parent_private_data *pdptr;
	char name[MAXNAMELEN];
	dev_info_t *parent;

	/*
	 * Fill in parent-private data and this function returns to us
	 * an indication if it used "registers" to fill in the data.
	 */
	make_ddi_ppd(child, &pdptr);
	ddi_set_parent_data(child, (caddr_t)pdptr);
	parent = ddi_get_parent(child);

	name[0] = '\0';
	if (sparc_pd_getnreg(child) > 0) {
		struct regspec *rp = sparc_pd_getreg(child, 0);
		/*
		 * On sun4u, the 'name' of children of the root node
		 * is foo@<upa-mid>,<offset>, which is derived from,
		 * but not identical to the physical address.
		 */
		if (parent == ddi_root_node()) {
			sprintf(name, "%x,%x",
			    (rp->regspec_bustype >> 1) & 0x1f,	/* UPA mid */
			    rp->regspec_addr);			/* offset */
		} else {
			sprintf(name, "%x,%x",
			    rp->regspec_bustype,
			    rp->regspec_addr);
		}
	}

	ddi_set_name_addr(child, name);
	return (impl_ddi_merge_child(child));
}

/*
 * A better name for this function would be impl_ddi_sunbus_uninitchild()
 * It does not remove the child, it uninitializes it, reclaiming the
 * resources taken by impl_ddi_sunbus_initchild.
 */
void
impl_ddi_sunbus_removechild(dev_info_t *dip)
{
	register struct ddi_parent_private_data *pdptr;
	register size_t n;

	if ((pdptr = (struct ddi_parent_private_data *)
	    ddi_get_parent_data(dip)) != NULL)  {
		if ((n = (size_t)pdptr->par_nintr) != 0)
			kmem_free(pdptr->par_intr, n *
			    sizeof (struct intrspec));

		if ((n = (size_t)pdptr->par_nrng) != 0)
			kmem_free(pdptr->par_rng, n *
			    sizeof (struct rangespec));

		if ((n = pdptr->par_nreg) != 0)
			kmem_free(pdptr->par_reg, n * sizeof (struct regspec));

		kmem_free(pdptr, sizeof (*pdptr));
		ddi_set_parent_data(dip, NULL);
	}
	ddi_set_name_addr(dip, NULL);
	/*
	 * Strip the node to properly convert it back to prototype form
	 */
	ddi_remove_minor_node(dip, NULL);
	impl_rem_dev_props(dip);
}

static int impl_acc_hdl_id = 0;
/*
 * access handle allocator
 */
ddi_acc_hdl_t *
impl_acc_hdl_get(ddi_acc_handle_t hdl)
{
	/*
	 * recast to ddi_acc_hdl_t instead of
	 * casting to ddi_acc_impl_t and then return the ah_platform_private
	 *
	 * this optimization based on the ddi_acc_hdl_t is the
	 * first member of the ddi_acc_impl_t.
	 */
	return ((ddi_acc_hdl_t *)hdl);
}

ddi_acc_handle_t
impl_acc_hdl_alloc(int (*waitfp)(caddr_t), caddr_t arg)
{
	ddi_acc_hdl_t *hp;
	int sleepflag;

	sleepflag = ((waitfp == (int (*)())KM_SLEEP) ? KM_SLEEP : KM_NOSLEEP);
	/*
	 * Allocate and initialize the data access handle.
	 */
	hp = (ddi_acc_hdl_t *)kmem_zalloc(sizeof (ddi_acc_hdl_t), sleepflag);
	if (!hp) {
		if ((waitfp != (int (*)())KM_SLEEP) &&
			(waitfp != (int (*)())KM_NOSLEEP))
			ddi_set_callback(waitfp, arg, &impl_acc_hdl_id);
		return (NULL);
	}

#if 0
	hp->ahi_common.ah_platform_private = (void *)hp;
#endif
	return ((ddi_acc_handle_t)hp);
}

void
impl_acc_hdl_free(ddi_acc_handle_t handle)
{
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(handle);
	if (hp) {
		kmem_free(hp, sizeof (*hp));
		if (impl_acc_hdl_id)
			ddi_run_callback(&impl_acc_hdl_id);
	}
}

/*ARGSUSED*/
void
impl_acc_hdl_init(ddi_acc_hdl_t *handlep)
{
}


/*
 * Allocate from the system, aligned on a specific boundary.
 *
 * It is assumed that alignment, if non-zero, is a simple power of two.
 *
 * It is also assumed that the power of two alignment is
 * less than or equal to the system's pagesize.
 */

static kmutex_t knc_lock;
static int kcadebug = 0;
static int kcused, kctotasked, kctotused;

void
kncinit()
{
	mutex_init(&knc_lock, "knc_lock", MUTEX_DEFAULT, DEFAULT_WT);
}

caddr_t
kalloca(u_int nbytes, int align, int cansleep, int endian_flags)
{
	u_long addr, raddr, alloc, *saddr;
	void *kmem_alloc_le(size_t size, int flags);

	if (kcadebug) {
		printf("kalloca: request is %d%%%d slp=%d\n", nbytes,
		    align, cansleep);
	}

	/*
	 * Validate and fiddle with alignment
	 */

	if (align < 16)
		align = 16;
	if (align & (align - 1))
		return ((caddr_t)0);

	/*
	 * Round up the allocation to an alignment boundary,
	 * and add in padding for both alignment and storage
	 * of the 'real' address and length.
	 */

	alloc = roundup(nbytes, align) + 16 + align;

	/*
	 * Allocate the requested space from the desired space
	 */

	if (endian_flags != DDI_STRUCTURE_LE_ACC)
		addr = (u_long)
			kmem_alloc(alloc, (cansleep) ? KM_SLEEP : KM_NOSLEEP);
	else
		addr = (u_long)
			kmem_alloc_le(alloc, (cansleep)? KM_SLEEP : KM_NOSLEEP);
	raddr = addr;

	if (addr) {
		mutex_enter(&knc_lock);
		kctotasked += nbytes;
		kctotused += alloc;
		kcused += alloc;
		mutex_exit(&knc_lock);
		if (addr & (align - 1)) {
			addr += 16;
			addr = roundup(addr, align);
		} else
			addr += align;

		saddr = (u_long *)addr;
		saddr[-1] = raddr;
		saddr[-2] = alloc;
		saddr[-3] = endian_flags;
	}

	if (kcadebug) {
		printf("kalloca: %d%%%d from heap got %x.%x returns %x\n",
		    nbytes, align, (int)raddr,
		    (int)(raddr + alloc), (int)addr);
	}

	return ((caddr_t)addr);
}

void
kfreea(caddr_t addr)
{
	u_long *saddr, raddr;
	u_int nbytes;
	int endian_flags;
	void kmem_free_le(void *buf, size_t size);

	saddr = (u_long *)(((u_long) addr) & ~0x3);
	raddr = saddr[-1];
	nbytes = (u_int)saddr[-2];
	endian_flags = (int)saddr[-3];
	if (kcadebug) {
		printf("kfreea: freeing %x (real %x.%x) from heap area\n",
		    (int)addr, (int)raddr, (int)(raddr + nbytes));
	}
	mutex_enter(&knc_lock);
	kcused -= nbytes;
	mutex_exit(&knc_lock);

	if (endian_flags != DDI_STRUCTURE_LE_ACC)
		kmem_free((caddr_t)raddr, nbytes);
	else
		kmem_free_le((caddr_t)raddr, nbytes);
}


/*
 * Start of little-endian memory allocator.
 *
 * The little-endian memory allocator consists of two interfaces
 * intented to be private to sun4u architecture.  These interfaces
 * are
 *
 *	void *kmem_alloc_le(size_t size, int flags)
 *	void kmem_free_le(void *buf, size_t size)
 *
 * These interfaces were developed to support ddi_dma_mem_alloc(9F)
 * requests for little-endian memory.  Such requests are not expected
 * on Sbus-based systems but are likely on PCI-based system where
 * a driver for a little-endian device might attempt to allocate
 * an IOPB.
 *
 * These interfaces behave similar to kmem_alloc(9F) and kmem_free(9F),
 * except that the memory returned is mapped with invert-endianness
 * attribute.  Since these interfaces are unlikely to be called on
 * Sbus-based systems and may not even get invoked on some PCI-based
 * system, non little-endian memory is even preallocated unless
 * at least one call is made to kmem_alloc_le.
 */

#define	KMEM_ADJUST(s)		((s) + 64)
#define	KMEM_ALIGN_SHIFT_LE	5
#define	KMEM_ALIGN_LE		(1 << KMEM_ALIGN_SHIFT_LE)
#define	KMEM_MAXBUF_LE		KMEM_ADJUST(2048)
#define	KMEM_TABLE_SIZE_LE	(KMEM_MAXBUF_LE >> KMEM_ALIGN_SHIFT_LE)
#define	KMEM_TABLE_INDEX_LE(n)	(((n) - 1) >> KMEM_ALIGN_SHIFT_LE)

/*
 * The follow data structure specifies the size and alignment of the
 * kmem caches.  Requests larger than KMEM_MAXBUF_LE will use the page
 * allocator.
 */
static struct {
	u_int size;
	u_int align;
} kmem_alloc_sizes_le[] = {
	{ KMEM_ADJUST(64),	64 },
	{ KMEM_ADJUST(128),	128 },
	{ KMEM_ADJUST(256),	256 },
	{ KMEM_ADJUST(512),	512 },
	{ KMEM_ADJUST(1024),	1024 },
	{ KMEM_MAXBUF_LE,	2048 },
};

/*
 * The following data structure is the table of kmem caches.  It is
 * intentened to be indexed by the request size.  It's dynamically
 * configured in kmem_alloc_le().
 */
static kmem_cache_t *kmem_alloc_table_le[KMEM_TABLE_SIZE_LE];

/*
 * Forward declarations and debugging stuff:
 */
static void *kmem_page_alloc_le(int npages, int flags);
void kmem_page_free_le(void *pages, int npages);
static void *kmem_cache_alloc_le(kmem_cache_t *cp, int flags);
static void kmem_cache_free_le(kmem_cache_t *cp, void *buf);
static kmem_slab_t *kmem_slab_create_le(kmem_cache_t *cp, int flags);
static void kmem_slab_destroy_le(kmem_cache_t *cp, kmem_slab_t *sp);

/*
 * This routine is like kmem_alloc, except that the memory returned is
 * mapped with the sfmmu invert-endianness attribute.
 */
void *
kmem_alloc_le(size_t size, int flags)
{
	int index, cache_size, align, i;
	kmem_cache_t *cp;
	char name[40];

	if (size == 0)
		return (NULL);

	/*
	 * If the request is large, just allocate the memory using the
	 * page allocator.
	 */
	index = KMEM_TABLE_INDEX_LE(size);
	if (index >= KMEM_TABLE_SIZE_LE)
		return (kmem_page_alloc_le(btopr(size), flags));

	/*
	 * If the request is small, we use kmem_cache_alloc() to get
	 * the memory.  Since many configurations may never use the
	 * little-endian memory allocator, we create the caches on
	 * demand.
	 */
	cp = kmem_alloc_table_le[index];
	if (cp == NULL) {

		/*
		 * We must create the cache for this index.  Determine
		 * its size, create it.
		 */
		for (i = 0; i < sizeof (kmem_alloc_sizes_le) /
				sizeof (kmem_alloc_sizes_le[0]); i++) {
			if (size < kmem_alloc_sizes_le[i].size) {
				cache_size = kmem_alloc_sizes_le[i].size;
				align = kmem_alloc_sizes_le[i].align;
				break;
			}
		}
		sprintf(name, "kmem_alloc_le_%d", cache_size);
		cp = kmem_cache_create(name, cache_size, align,
					NULL, NULL, NULL);
		/*
		 * Now install the cache in the appropriate places in
		 * the table.
		 */
		if (i != 0)
			size = kmem_alloc_sizes_le[i - 1].size;
		while (size <= cache_size) {
			kmem_alloc_table_le[KMEM_TABLE_INDEX_LE(size)] = cp;
			size += KMEM_ALIGN_LE;
		}
	}
	return (kmem_cache_alloc_le(cp, flags));
}

/*
 * This routine is for freeing memory allocated via kmem_alloc_le().
 */
void
kmem_free_le(void *buf, size_t size)
{
	int index;

	index = KMEM_TABLE_INDEX_LE(size);
	if (index >= KMEM_TABLE_SIZE_LE)
		kmem_page_free_le(buf, btopr(size));
	else
		kmem_cache_free_le(kmem_alloc_table_le[index], buf);
}

/*
 * Get pages (mapping with invert-endianness attribute) from the VM system.
 */
static void *
kmem_page_alloc_le(int npages, int flags)
{
	void *pages;
	void *kmem_getpages_le(int npages, int flag);

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
	mutex_exit(&freemem_lock);

	if ((pages = kmem_getpages_le(npages, KM_NOSLEEP)) == NULL) {
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
			mutex_exit(&freemem_lock);
		} else {
			kmem_reap();
			pages = kmem_getpages_le(npages, KM_SLEEP);
			ASSERT(pages != NULL);
		}
	}
	return (pages);
}

/*
 * Give pages back to the VM system and update all the bean counters.
 */
void
kmem_page_free_le(void *pages, int npages)
{
	extern void kmem_freepages(caddr_t addr, int npages);

	kmem_freepages(pages, npages);
	mutex_enter(&freemem_lock);
	availrmem += npages;
	availsmem += npages;
	pages_pp_kernel -= npages;
	mutex_exit(&freemem_lock);
}

/*
 * This routine allocates an entry from a kmem cache.
 */
static void *
kmem_cache_alloc_le(kmem_cache_t *cp, int flags)
{
	void *buf;
	kmem_slab_t *sp, *snext, *sprev;
	kmem_bufctl_t *bcp;

	mutex_enter(&cp->cache_lock);
	cp->cache_alloc++;
	sp = cp->cache_freelist;
	ASSERT(sp->slab_cache == cp);
	if ((bcp = sp->slab_head) == sp->slab_tail) {
		if (bcp == NULL) {
			/*
			 * The freelist is empty.  Create a new slab.
			 */
			if ((sp = kmem_slab_create_le(cp, flags)) == NULL) {
				cp->cache_alloc_fail++;
				mutex_exit(&cp->cache_lock);
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

	buf = (void *)((int)bcp - cp->cache_offset);

	ASSERT((u_int)buf - (u_int)sp->slab_base < cp->cache_slabsize);

	mutex_exit(&cp->cache_lock);
	return (buf);
}

/*
 * This routine is called from kmem_cache_alloc_le() to create a slab
 * for the cache.
 */
static kmem_slab_t *
kmem_slab_create_le(kmem_cache_t *cp, int flags)
{
	int bufsize = cp->cache_bufsize;
	int chunksize = cp->cache_chunksize;
	int color, chunks;
	void (*constructor)(void *, size_t) = cp->cache_constructor;
	char *buf, *base, *limit;
	kmem_slab_t *sp;
	kmem_bufctl_t *bcp;

	ASSERT(MUTEX_HELD(&cp->cache_lock));

	if ((color = cp->cache_color += cp->cache_align) > cp->cache_maxcolor)
		color = cp->cache_color = 0;

	mutex_exit(&cp->cache_lock);

	base = kmem_page_alloc_le(btop(cp->cache_slabsize), flags);
	if (base == NULL) {
		mutex_enter(&cp->cache_lock);
		return (NULL);
	}

	limit = base + cp->cache_slabsize - sizeof (kmem_slab_t);
	sp = (kmem_slab_t *)limit;

	sp->slab_cache	= cp;
	sp->slab_head	= NULL;
	sp->slab_refcnt	= 0;
	sp->slab_base	= buf = base + color;

	chunks = 0;
	while (buf + chunksize <= limit) {
		bcp = (kmem_bufctl_t *)((int)buf + cp->cache_offset);
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
	return (sp);
}

/*
 * This routine is used to return an entry to a kmem_cache.
 */
static void
kmem_cache_free_le(kmem_cache_t *cp, void *buf)
{
	kmem_slab_t *sp, *snext, *sprev;
	kmem_bufctl_t *bcp, *old_slab_tail;

	mutex_enter(&cp->cache_lock);

	bcp = (kmem_bufctl_t *)((int)buf + cp->cache_offset);
	sp = (kmem_slab_t *)((((int)buf) & -PAGESIZE) +
				(PAGESIZE - sizeof (kmem_slab_t)));

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
		kmem_slab_destroy_le(cp, sp);
	}
	mutex_exit(&cp->cache_lock);
}

/*
 * This routine is called from kmem_cache_free_le() to destroy a slab.
 */
static void
kmem_slab_destroy_le(kmem_cache_t *cp, kmem_slab_t *sp)
{
	int bufsize = cp->cache_bufsize;
	int cache_flags = cp->cache_flags;
	void (*destructor)(void *, size_t) = cp->cache_destructor;
	kmem_bufctl_t *bcp, *next_bcp;
	void *buf, *base;

	ASSERT(MUTEX_HELD(&cp->cache_lock));

	cp->cache_slab_destroy++;
	cp->cache_buftotal -= sp->slab_chunks;

	mutex_exit(&cp->cache_lock);

	if (cache_flags & KMF_DEADBEEF)
		destructor = NULL;

	next_bcp = sp->slab_head;
	sp->slab_tail->bc_next = NULL;	/* normally a garbage pointer */
	while ((bcp = next_bcp) != NULL) {
		next_bcp = bcp->bc_next;
		buf = (void *)((int)bcp - cp->cache_offset);
		if (destructor != NULL)
			(*destructor)(buf, bufsize);
	}
	base = (void *)((int)sp->slab_base & -PAGESIZE);
	kmem_page_free_le(base, btop(cp->cache_slabsize));
	mutex_enter(&cp->cache_lock);
}

/*
 * end of little-endian memory allocator
 */

/*
 * Code to search hardware layer (PROM), if it exists,
 * on behalf of child.
 *
 * if input dip != child_dip, then call is on behalf of child
 * to search PROM, do it via ddi_prop_search_common() and ascend only
 * if allowed.
 *
 * if input dip == ch_dip (child_dip), call is on behalf of root driver,
 * to search for PROM defined props only.
 *
 * Note that the PROM search is done only if the requested dev
 * is either DDI_DEV_T_ANY or DDI_DEV_T_NONE. PROM properties
 * have no associated dev, thus are automatically associated with
 * DDI_DEV_T_NONE.
 *
 * Modifying flag DDI_PROP_NOTPROM inhibits the search in the h/w layer.
 *
 * Returns DDI_PROP_FOUND_1275 if found to indicate to framework
 * that the property resides in the prom.
 */

int
impl_ddi_bus_prop_op(dev_t dev, dev_info_t *dip, dev_info_t *ch_dip,
    ddi_prop_op_t prop_op, int mod_flags,
    char *name, caddr_t valuep, int *lengthp)
{
	int	len;
	caddr_t	buffer;

	/*
	 * If requested dev is DDI_DEV_T_NONE or DDI_DEV_T_ANY, then
	 * look in caller's PROM if it's a self identifying device...
	 * XXX: For sun4c, we use nodeid == DEVI_PSEUDO_NODEID
	 * to indicate a non-self identifying device.
	 *
	 * Note that this is very similar to ddi_prop_op, but we
	 * search the PROM instead of the s/w defined properties,
	 * and we are called on by the parent driver to do this for
	 * the child.
	 */

	if (((dev == DDI_DEV_T_NONE) || (dev == DDI_DEV_T_ANY)) &&
	    (DEVI(ch_dip)->devi_nodeid != DEVI_PSEUDO_NODEID) &&
	    ((mod_flags & DDI_PROP_NOTPROM) == 0))  {
		len = prom_getproplen((dnode_t)DEVI(ch_dip)->devi_nodeid,
			name);
		if (len == -1)
			return (DDI_PROP_NOT_FOUND);

		/*
		 * If exists only request, we're done
		 */
		if (prop_op == PROP_EXISTS)
			return (DDI_PROP_FOUND_1275);

		/*
		 * If length only request or prop length == 0, get out
		 */
		if ((prop_op == PROP_LEN) || (len == 0))  {
			*lengthp = len;
			return (DDI_PROP_FOUND_1275);
		}

		/*
		 * Allocate buffer if required... (either way `buffer'
		 * is receiving address).
		 */

		switch (prop_op)  {

		case PROP_LEN_AND_VAL_ALLOC:

			buffer = (caddr_t)kmem_alloc((size_t)len,
			    mod_flags & DDI_PROP_CANSLEEP ?
			    KM_SLEEP : KM_NOSLEEP);
			if (buffer == NULL)  {
				return (DDI_PROP_NO_MEMORY);
			}
			*(caddr_t *)valuep = buffer;
			break;

		case PROP_LEN_AND_VAL_BUF:

			if (len > (*lengthp))  {
				*lengthp = len;
				return (DDI_PROP_BUF_TOO_SMALL);
			}

			buffer = valuep;
			break;
		}

		/*
		 * Call the PROM function to do the copy.
		 */
		(void) prom_getprop((dnode_t)DEVI(ch_dip)->devi_nodeid,
			name, buffer);
		*lengthp = len;	/* return the actual length to the caller */
		(void) impl_fix_props(dip, ch_dip, name, len, buffer);
		return (DDI_PROP_FOUND_1275);
	}

	return (DDI_PROP_NOT_FOUND);
}


/*
 * Return an integer in native machine format from an OBP 1275 integer
 * representation, which is big-endian, with no particular alignment
 * guarantees.  intp points to the OBP data, and n the number of bytes.
 *
 * Byte-swapping may be needed on some implementations.
 */
int
impl_ddi_prop_int_from_prom(u_char *intp, int n)
{
	int	i = 0;

	ASSERT(n > 0 && n <= 4);

	while (n-- > 0) {
		i = (i << 8) | *intp++;
	}

	return (i);
}
