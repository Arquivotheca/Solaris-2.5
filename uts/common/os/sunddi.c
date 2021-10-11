/*
 * Copyright (c) 1990-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)sunddi.c	1.81	95/08/23 SMI"

/* From prototype sunddi.c 1.24 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/cred.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <sys/kmem.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/open.h>
#include <sys/user.h>
#include <sys/t_lock.h>
#include <sys/vm.h>
#include <sys/stat.h>
#include <vm/hat.h>
#include <vm/seg.h>
#include <vm/as.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/avintr.h>
#include <sys/sunddi.h>
#include <sys/esunddi.h>
#include <sys/kstat.h>
#include <sys/conf.h>
#include <sys/ddi_impldefs.h>	/* include implementation structure defs */
#include <sys/hwconf.h>
#include <sys/pathname.h>
#include <sys/autoconf.h>
#include <sys/modctl.h>


/*
 * DDI(Sun) Function and flag definitions:
 */

#ifdef i386
/*
 * Used to indicate which entries were chosen from a range.
 */
char	*chosen_intr = "chosen-interrupt";
char	*chosen_reg = "chosen-reg";
#endif

/*
 * Creating register mappings and handling interrupts:
 */

/*
 * Generic ddi_map: Call parent to fulfill request...
 */

int
ddi_map(dev_info_t *dp, ddi_map_req_t *mp, off_t offset,
    off_t len, caddr_t *addrp)
{
	register dev_info_t *pdip;

	ASSERT(dp);
	pdip = (dev_info_t *)DEVI(dp)->devi_parent;
	return ((DEVI(pdip)->devi_ops->devo_bus_ops->bus_map)(pdip,
	    dp, mp, offset, len, addrp));
}

/*
 * ddi_apply_range: (Called by nexi only.)
 * Apply ranges in parent node dp, to child regspec rp...
 */

int
ddi_apply_range(dev_info_t *dp, dev_info_t *rdip, struct regspec *rp)
{
	return (i_ddi_apply_range(dp, rdip, rp));
}

int
ddi_map_regs(dev_info_t *dip, u_int rnumber, caddr_t *kaddrp, off_t offset,
    off_t len)
{
	ddi_map_req_t mr;
#ifdef i386
	struct
	{
		int	bus;
		int	addr;
		int	size;
	} reg, *reglist;
	int	length;
	int	rc;

	/*
	 * get the 'registers' or the 'reg' property.
	 * Given a pointer, ddi_getlongprop ()
	 * will allocate the memory.  It will set length to the number of
	 * bytes allocated.
	 */
	rc = ddi_getlongprop(DDI_DEV_T_NONE, dip,
			DDI_PROP_DONTPASS, "registers",
			(caddr_t)&reglist, &length);
	if (rc != DDI_SUCCESS)
		rc = ddi_getlongprop(DDI_DEV_T_NONE, dip,
				DDI_PROP_DONTPASS, "reg",
				(caddr_t)&reglist, &length);
	if (rc == DDI_SUCCESS) {
		/*
		 * point to the required entry.
		 */
		reg = reglist[rnumber];
		reg.addr += offset;
		if (len != 0)
			reg.size = len;
		/*
		 * make a new property containing ONLY the required tuple.
		 */
		(void) ddi_prop_create(DDI_DEV_T_NONE, dip,
				DDI_PROP_CANSLEEP, chosen_reg,
				(caddr_t)&reg, sizeof (reg));
		/*
		 * free the memory allocated by ddi_getlongprop ().
		 */
		kmem_free((caddr_t)reglist, length);
	}

#endif
	mr.map_op = DDI_MO_MAP_LOCKED;
	mr.map_type = DDI_MT_RNUMBER;
	mr.map_obj.rnumber = rnumber;
	mr.map_prot = PROT_READ | PROT_WRITE;
	mr.map_flags = DDI_MF_KERNEL_MAPPING;
	mr.map_handlep = NULL;
	mr.map_vers = DDI_MAP_VERSION;

	/*
	 * Call my parent to map in my regs.
	 */

	return (ddi_map(dip, &mr, offset, len, kaddrp));
}

void
ddi_unmap_regs(dev_info_t *dip, u_int rnumber, caddr_t *kaddrp, off_t offset,
    off_t len)
{
	ddi_map_req_t mr;

	mr.map_op = DDI_MO_UNMAP;
	mr.map_type = DDI_MT_RNUMBER;
	mr.map_flags = DDI_MF_KERNEL_MAPPING;
	mr.map_prot = PROT_READ | PROT_WRITE;	/* who cares? */
	mr.map_obj.rnumber = rnumber;
	mr.map_handlep = NULL;
	mr.map_vers = DDI_MAP_VERSION;

	/*
	 * Call my parent to unmap my regs.
	 */

	(void) ddi_map(dip, &mr, offset, len, kaddrp);
	*kaddrp = (caddr_t)0;
#ifdef i386
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, chosen_reg);
#endif
}

int
ddi_bus_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
	off_t offset, off_t len, caddr_t *vaddrp)
{
	return (i_ddi_bus_map(dip, rdip, mp, offset, len, vaddrp));
}

/*
 * nullbusmap:	The/DDI default bus_map entry point for nexi
 *		not conforming to the reg/range paradigm (i.e. scsi, etc.)
 *		with no HAT/MMU layer to be programmed at this level.
 *
 *		If the call is to map by rnumber, return an error,
 *		otherwise pass anything else up the tree to my parent.
 *
 *		XXX: Is the name `nullbusmap' misleading?
 */

/*ARGSUSED*/
int
nullbusmap(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
	off_t offset, off_t len, caddr_t *vaddrp)
{
	if (mp->map_type == DDI_MT_RNUMBER)
		return (DDI_ME_UNSUPPORTED);

	return (ddi_map(dip, mp, offset, len, vaddrp));
}

/*
 * ddi_rnumber_to_regspec: Not for use by leaf drivers.
 *			   Only for use by nexi using the reg/range paradigm.
 */
struct regspec *
ddi_rnumber_to_regspec(dev_info_t *dip, int rnumber)
{
	return (i_ddi_rnumber_to_regspec(dip, rnumber));
}

/*
 * Peek and poke whilst giving a chance for the nexus drivers to
 * intervene to flush write buffers for us.
 *
 * Note that we allow the dip to be nil because we may be called
 * prior even to the instantiation of the devinfo tree itself - all
 * regular leaf and nexus drivers should always use a non-nil dip!
 *
 * We treat peek in a somewhat cavalier fashion .. assuming that we'll
 * simply get a synchronous fault as soon as we touch a missing address.
 *
 * Poke is rather more carefully handled because we might poke to a write
 * buffer, "succeed", then only find some time later that we got an
 * asynchronous fault that indicated that the address we were writing to
 * was not really backed by hardware.
 */
/*ARGSUSED*/
static int
ddi_peek(dev_info_t *devi, size_t size,
    register void *addr, register void *value_p)
{
	register label_t *saved_jb;
	register int err = DDI_SUCCESS;
	auto label_t jb;
	auto longlong_t trash;

	/*
	 * arrange that peeking to a nil destination pointer silently succeeds
	 */
	if (value_p == (void *)0)
		value_p = &trash;

	saved_jb = curthread->t_nofault;
	curthread->t_nofault = &jb;

	if (!setjmp(&jb)) {
		switch (size) {
		case sizeof (char):
			*(char *)value_p = *(char *)addr;
			break;

		case sizeof (short):
			*(short *)value_p = *(short *)addr;
			break;

		case sizeof (long):
			*(long *)value_p = *(long *)addr;
			break;

#ifndef _NO_LONGLONG
		case sizeof (longlong_t):
			*(longlong_t *)value_p = *(longlong_t *)addr;
			break;
#endif	/* !_NO_LONGLONG */

		default:
			err = DDI_FAILURE;
			break;
		}
					/* if we get to here, it worked */
	} else
		err = DDI_FAILURE;	/* else .. a fault occurred */

	curthread->t_nofault = saved_jb;
	return (err);
}

int
ddi_peekc(dev_info_t *dip, char *addr, char *val_p)
{
	return (ddi_peek(dip, sizeof (char), addr, val_p));
}

int
ddi_peeks(dev_info_t *dip, short *addr, short *val_p)
{
	return (ddi_peek(dip, sizeof (short), addr, val_p));
}

int
ddi_peekl(dev_info_t *dip, long *addr, long *val_p)
{
	return (ddi_peek(dip, sizeof (long), addr, val_p));
}

int
ddi_peekd(dev_info_t *dip, longlong_t *addr, longlong_t *val_p)
{
	return (ddi_peek(dip, sizeof (longlong_t), addr, val_p));
}

static int
ddi_poke(dev_info_t *devi, size_t size,
    register void *addr, register void *value_p)
{
	register label_t *saved_jb;
	register int err = DDI_SUCCESS;
	auto label_t jb;

	saved_jb = curthread->t_nofault;
	curthread->t_nofault = &jb;

	if (!setjmp(&jb)) {

		/*
		 * Inform our parent nexi what we're about to do, giving them
		 * an early opportunity to tell us not to even try.
		 */
		if (devi && ddi_ctlops(devi, devi, DDI_CTLOPS_POKE_INIT,
		    addr, 0) != DDI_SUCCESS) {
			curthread->t_nofault = saved_jb;
			return (DDI_FAILURE);
		}

		switch (size) {
		case sizeof (char):
			*(char *)addr = *(char *)value_p;
			break;

		case sizeof (short):
			*(short *)addr = *(short *)value_p;
			break;

		case sizeof (long):
			*(long *)addr = *(long *)value_p;
			break;

#ifndef _NO_LONGLONG
		case sizeof (longlong_t):
			*(longlong_t *)addr = *(longlong_t *)value_p;
			break;
#endif	/* !_NO_LONGLONG */

		default:
			err = DDI_FAILURE;
			break;
		}

		/*
		 * Now give our parent(s) a chance to ensure that what we
		 * did really propagated through any intermediate buffers,
		 * returning failure if we detected any problems .. more
		 * likely though, the resulting flush will cause us to
		 * longjmp into the 'else' clause below ..
		 */
		if (devi && ddi_ctlops(devi, devi, DDI_CTLOPS_POKE_FLUSH,
		    addr, (void *)0) != DDI_SUCCESS)
			err = DDI_FAILURE;
		curthread->t_nofault = saved_jb;
	} else {
		err = DDI_FAILURE;		/* a fault occurred */
		curthread->t_nofault = saved_jb;
	}

	/*
	 * Give our parents a chance to tidy up after us.  If
	 * 'tidying up' causes faults, we crash and burn.
	 */
	if (devi)
		(void) ddi_ctlops(devi, devi, DDI_CTLOPS_POKE_FINI,
		    addr, (void *)0);

	return (err);
}

int
ddi_pokec(dev_info_t *dip, char *addr, char val)
{
	return (ddi_poke(dip, sizeof (char), addr, &val));
}

int
ddi_pokes(dev_info_t *dip, short *addr, short val)
{
	return (ddi_poke(dip, sizeof (short), addr, &val));
}

int
ddi_pokel(dev_info_t *dip, long *addr, long val)
{
	return (ddi_poke(dip, sizeof (long), addr, &val));
}

int
ddi_poked(dev_info_t *dip, longlong_t *addr, longlong_t val)
{
	return (ddi_poke(dip, sizeof (longlong_t), addr, &val));
}

/*
 * ddi_peekpokeio() is used primarily by the mem drivers for moving
 * data to and from uio structures via peek and poke.  Note that we
 * use "internal" routines ddi_peek and ddi_poke to make this go
 * slightly faster, avoiding the call overhead ..
 */
int
ddi_peekpokeio(dev_info_t *devi, struct uio *uio, enum uio_rw rw,
    caddr_t addr, int len, size_t xfersize)
{
	register int c;
	int o;
	long lsh;
	char ch;

	while (len > 0) {
		if ((len|(int)addr) & 1) {
			c = sizeof (char);
			if (rw == UIO_WRITE) {
				if ((o = uwritec(uio)) == -1)
					return (DDI_FAILURE);
				if (ddi_pokec(devi, (char *)addr,
				    (char)o) != DDI_SUCCESS)
					return (DDI_FAILURE);
			} else {
				if (ddi_peek(devi, c, (char *)addr,
				    &ch) != DDI_SUCCESS)
					return (DDI_FAILURE);
				if (ureadc(ch, uio))
					return (DDI_FAILURE);
			}
		} else {
			/*
			 * XXX	Should probably add support for
			 *	all-char and aligned longlong transfers
			 *	too.
			 */
			if ((xfersize == sizeof (long)) &&
			    (((int)addr % sizeof (long)) == 0) &&
			    (len % sizeof (long)) == 0)
				c = sizeof (long);
			else
				c = sizeof (short);

			if (rw == UIO_READ) {
				if (ddi_peek(devi, c, (long *)addr,
				    &lsh) != DDI_SUCCESS)
					return (DDI_FAILURE);
			}

			if (uiomove((caddr_t)&lsh, c, rw, uio))
				return (DDI_FAILURE);

			if (rw == UIO_WRITE) {
				if (ddi_poke(devi, c, (long *)addr,
				    &lsh) != DDI_SUCCESS)
					return (DDI_FAILURE);
			}
		}
		addr += c;
		len -= c;
	}
	return (DDI_SUCCESS);
}

/*
 * These routines are used by drivers that do layered ioctls
 * On sparc, they're implemented in assembler to avoid spilling
 * register windows in the common (copyin) case ..
 */
#ifndef	sparc
int
ddi_copyin(caddr_t buf, caddr_t kernbuf, size_t size, int flags)
{
	extern int kcopy(caddr_t, caddr_t, size_t);

	if (flags & FKIOCTL)
		return (kcopy(buf, kernbuf, size) ? -1 : 0);
	return (copyin(buf, kernbuf, size));
}
#endif	/* !sparc */

#ifndef	sparc
int
ddi_copyout(caddr_t buf, caddr_t kernbuf, size_t size, int flags)
{
	extern int kcopy(caddr_t, caddr_t, size_t);

	if (flags & FKIOCTL)
		return (kcopy(buf, kernbuf, size) ? -1 : 0);
	return (copyout(buf, kernbuf, size));
}
#endif	/* !sparc */

/*
 * Conversions in nexus pagesize units.  We don't duplicate the
 * 'nil dip' semantics of peek/poke because btopr/btop/ptob are DDI/DKI
 * routines anyway.
 */
unsigned long
ddi_btop(dev_info_t *dip, unsigned long bytes)
{
	auto unsigned long pages;

	(void) ddi_ctlops(dip, dip, DDI_CTLOPS_BTOP, &bytes, &pages);
	return (pages);
}

unsigned long
ddi_btopr(dev_info_t *dip, unsigned long bytes)
{
	auto unsigned long pages;

	(void) ddi_ctlops(dip, dip, DDI_CTLOPS_BTOPR, &bytes, &pages);
	return (pages);
}

unsigned long
ddi_ptob(dev_info_t *dip, unsigned long pages)
{
	auto unsigned long bytes;

	(void) ddi_ctlops(dip, dip, DDI_CTLOPS_PTOB, &pages, &bytes);
	return (bytes);
}

/*
 * Controlled (MT-safe) access to shared device registers
 */

#ifdef notdef
/*
 * Helper function for inspecting bit arrays
 *
 * XXX	Check out the existing bitfield manipulation functions -
 *	won't they do?
 */
static int
bitcmp(void *bitset_a, void *bitset_b, register size_t bitset_size)
{
	register unsigned char *a, *b;

	/*
	 * If either bit set is a nil pointer, or there are
	 * zero bits to compare, then we know there's
	 * no bit set intersection
	 */
	if ((a = bitset_a) == (void *)0 ||
	    (b = bitset_b) == (void *)0 ||
	    bitset_size == 0)
		return (0);

	while (bitset_size--)
		if (((int)*a++ & (int)*b++) != 0)
			return (1);

	return (0);
}
#endif notdef

/*
 * ddi_shared_reg_fetch:	get a reference to a shared register
 *
 * XXX	This isn't implemented.  Maybe it shouldn't be..
 *
 * The caller indicates which bits of the named register he
 * intends to read and write, and whether shared or exclusive
 * access is expected by filling in the various bitfield arguments.
 * The 'size' field is the length (in bytes) of the shortest
 * non-null bitfield argument.
 *
 * A register bit can be marked read-shared, read-exclusive
 * write-shared, or write-exclusive.  Only the first caller
 * that attempts it will succeed in getting exclusive read or write
 * access to a given bit. Many callers may obtain read or
 * write shared access to a location, but once a location has
 * been marked shared, an exclusive lock cannot be held.
 *
 * For convenience, a null bitfield argument means that the
 * caller has no interest in that particular form of access
 * for the register.
 *
 * If the named register is not known to the implementation,
 * the routine returns 'DDI_SHREG_UNKNOWN_REG'
 *
 * If the named register is known to the implementation, but the
 * requested access to the register could not be achieved, the
 * routine returns 'DDI_SHREG_BAD_ACCESS'
 *
 * The implementation returns the value 'DDI_SHREG_SUCCESS' to
 * the caller if all went well. In this case the register and
 * mutex pointers will be updated. The mutex should be used
 * to guard *all* access to the shared register.
 *
 * It is the responsibility of the caller to ensure that when
 * claimed bits are updated, the remaining unclaimed bits of the
 * register are kept consistent.
 *
 * A given devinfo node is only permitted to make one
 * shared register request for a given register.
 */
/*ARGSUSED*/
int
ddi_shared_reg_fetch(dev_info_t *devi, char *name, void *bits_rd_shr,
    void *bits_wr_shr, void *bits_rd_excl, void *bits_wr_excl,
    size_t size, kmutex_t **reg_mutex_p, caddr_t *reg_kvaddr_p)
{
#ifdef notdef
	auto struct ddi_sreg_impl		*sregp;
	register struct ddi_sreg_impl_list	*this;
	register int				nvalid;

	/*
	 * Sanity check
	 */
	if ((bits_rd_excl == (void *)0 && bits_rd_shr == (void *)0 &&
	    bits_wr_excl == (void *)0 && bits_wr_shr == (void *)0) ||
	    size == 0) {
		return (DDI_SHREG_BADACCESS);
	}
#endif
	/*
	 * Ask the implementation for the underlying
	 * data structure that describes the named register.
	 *
	 * XXX	Urk - this whole routine needs more justification.
	 */
	return (DDI_SHREG_UNKNOWN);

#ifdef notdef
	/*
	 * The register is known to the implementation, so walk the list of
	 * clients, testing if the caller should be allowed to access it in
	 * the way requested.
	 */
	size = min(size, sregp->sreg_size);
	mutex_enter(&sregp->sreg_impl_mutex);
	for (this = sregp->sreg_list; this; this = this->sreg_next) {
		nvalid = min(this->sreg_bits_valid, size);
		if (devi == this->sreg_devi ||
		    bitcmp(this->sreg_bits_rd_excl, bits_rd_excl, nvalid) ||
		    bitcmp(this->sreg_bits_rd_shr, bits_rd_excl, nvalid) ||
		    bitcmp(this->sreg_bits_rd_excl, bits_rd_shr, nvalid) ||
		    bitcmp(this->sreg_bits_wr_excl, bits_wr_excl, nvalid) ||
		    bitcmp(this->sreg_bits_wr_shr, bits_rd_excl, nvalid) ||
		    bitcmp(this->sreg_bits_wr_excl, bits_wr_shr, nvalid)) {
			mutex_exit(&sregp->sreg_impl_mutex);
			return (DDI_SHREG_BADACCESS);
		}
	}

	/*
	 * Ok, so it's allowed.  Add an element to the list describing
	 * this allocation.
	 */
	this = kmem_zalloc(sizeof (struct ddi_sreg_impl_list *), KM_SLEEP);
	this->sreg_devi = devi;

	/*
	 * We copy the four bit arrays into one piece of actual heap,
	 * and direct the pointers to the right places.  Not pretty.
	 */
	this->sreg_bits = kmem_alloc(size << 2, KM_SLEEP);

	if (bits_rd_excl) {
		this->sreg_bits_rd_excl = this->sreg_bits;
		bcopy(bits_rd_excl, this->sreg_bits_rd_excl, size);
	}

	if (bits_rd_shr) {
		this->sreg_bits_rd_shr = this->sreg_bits + size;
		bcopy(bits_rd_shr, this->sreg_bits_rd_shr, size);
	}

	if (bits_wr_excl) {
		this->sreg_bits_wr_excl = this->sreg_bits + 2 * size;
		bcopy(bits_wr_excl, this->sreg_bits_wr_excl, size);
	}

	if (bits_wr_shr) {
		this->sreg_bits_wr_shr = this->sreg_bits + 3 * size;
		bcopy(bits_wr_shr, this->sreg_bits_wr_shr, size);
	}

	this->sreg_bits_valid = size;

	this->sreg_next = sregp->sreg_list;
	sregp->sreg_list = this;
	mutex_exit(&sregp->sreg_impl_mutex);

	if (reg_mutex_p != (kmutex_t **)0)
		*reg_mutex_p = &sregp->sreg_mutex;

	if (reg_kvaddr_p != (caddr_t *)0)
		*reg_kvaddr_p = sregp->sreg_kvaddr;

	return (DDI_SHREG_SUCCESS);
#endif /* notdef */
}

/*
 * ddi_shared_reg_rele:		release a reference to a shared register
 *
 * Release a shared register named 'name' associated with dev_info
 * node 'devi'.  The routine will complain if the named register has
 * never been fetched by the given dev_info node, or it an attempt it
 * made to free a register unknown to the implementation.
 */
/*ARGSUSED*/
void
ddi_shared_reg_rele(dev_info_t *devi, char *name, kmutex_t **reg_mutex_p,
    caddr_t *reg_kvaddr_p)
{
#ifdef notdef
	auto struct ddi_sreg_impl		*sregp;
	register struct ddi_sreg_impl_list	*this, **this_p;
#endif
	/*
	 * Ask the implementation for the underlying
	 * data structures that describes the named register.
	 */
	cmn_err(CE_WARN, "%s%d: cannot access unknown reg '%s'",
	    DEVI(devi)->devi_name, DEVI(devi)->devi_instance, name);
	return;

#ifdef notdef
	/*
	 * Find the corresponding 'devi' on the linked list
	 * and patch the list back together again.
	 */
	mutex_enter(&sregp->sreg_impl_mutex);
	this_p = &sregp->sreg_list;
	while (*this_p) {
		this = *this_p;
		if (this->sreg_devi == devi) {
			*this_p = this->sreg_next;
			mutex_exit(&sregp->sreg_impl_mutex);
			goto found;
			/* NOTREACHED */
		} else
			this_p = &this->sreg_next;
	}
	mutex_exit(&sregp->sreg_impl_mutex);

	cmn_err(CE_WARN, "%s%d: reg '%s' never fetched by this instance",
	    DEVI(devi)->devi_name, DEVI(devi)->devi_instance, name);
	return;

found:
	kmem_free(this->sreg_bits, this->sreg_bits_valid);
	kmem_free(this, sizeof (struct ddi_sreg_impl_list));

	*reg_mutex_p = (kmutex_t *)0;
	*reg_kvaddr_p = (caddr_t)0;
#endif /* notdef */
}

/*
 * Return non-zero if the specified interrupt exists and the handler
 * will be restricted to using only certain functions because the
 * interrupt level is not blocked by the scheduler.  I.e., it cannot
 * signal other threads.
 */
int
ddi_intr_hilevel(dev_info_t *dip, u_int inumber)
{
	ddi_intrspec_t ispec;
	int	r;

	/*
	 * Get the named interrupt specification.  If found, perform the
	 * bus op to find out whether it is hilevel or not.
	 */
	ispec = i_ddi_get_intrspec(dip, dip, inumber);
	if (ispec != NULL &&
	    ddi_ctlops(dip, dip, DDI_CTLOPS_INTR_HILEVEL, (void *)ispec,
	    (void *)&r) == DDI_SUCCESS)
		return (r);
	return (0);
}

int
ddi_get_iblock_cookie(dev_info_t *dip, u_int inumber,
    ddi_iblock_cookie_t *iblock_cookiep)
{
	ddi_iblock_cookie_t	c;
	int	error;

	ASSERT(iblock_cookiep != NULL);

	error = ddi_add_intr(dip, inumber, &c, NULL, nullintr, NULL);
	if (error != DDI_SUCCESS)
		return (error);
	ddi_remove_intr(dip, inumber, c);

	*iblock_cookiep = c;
	return (DDI_SUCCESS);
}

int
ddi_get_soft_iblock_cookie(dev_info_t *dip, int preference,
    ddi_iblock_cookie_t *iblock_cookiep)
{
	ddi_iblock_cookie_t	c;
	int	error;
	ddi_softintr_t	id;

	ASSERT(iblock_cookiep != NULL);

	error = ddi_add_softintr(dip, preference, &id, &c, NULL,
	    nullintr, NULL);
	if (error != DDI_SUCCESS)
		return (error);
	ddi_remove_softintr(id);

	*iblock_cookiep = c;
	return (DDI_SUCCESS);
}

/* Comments in <sys/sunddi.h> */
int
ddi_add_intr(dev_info_t *dip, u_int inumber,
    ddi_iblock_cookie_t *iblock_cookiep,
    ddi_idevice_cookie_t *idevice_cookiep,
    u_int (*int_handler)(caddr_t int_handler_arg),
    caddr_t int_handler_arg)
{
	ddi_intrspec_t ispec;
#ifdef i386
	struct
	{
		int	ipl;
		int	irq;
	} intr, *intrlist;
	int	length;
	int	rc;
#endif

	/* get the named interrupt specification */
	if ((ispec = i_ddi_get_intrspec(dip, dip, inumber)) == NULL) {
		return (DDI_INTR_NOTFOUND);
	}

#ifdef i386
	/*
	 * get the 'interrupts' or the 'intr' property.
	 * Given a pointer, ddi_getlongprop ()
	 * will allocate the memory.  It will set length to the number of
	 * bytes allocated.
	 */
	rc = ddi_getlongprop(DDI_DEV_T_NONE, dip,
			DDI_PROP_DONTPASS, "interrupts",
			(caddr_t)&intrlist, &length);
	if (rc != DDI_PROP_SUCCESS)
		rc = ddi_getlongprop(DDI_DEV_T_NONE, dip,
				DDI_PROP_DONTPASS, "intr",
				(caddr_t)&intrlist, &length);
	if (rc == DDI_PROP_SUCCESS) {
		/*
		 * point to the required entry.
		 */
		intr = intrlist[inumber];

		/*
		 * make a new property containing ONLY the required tuple.
		 */
		(void) ddi_prop_create(DDI_DEV_T_NONE, dip,
				DDI_PROP_CANSLEEP, chosen_intr,
				(caddr_t)&intr, sizeof (intr));
		/*
		 * free the memory allocated by ddi_getlongprop ().
		 */
		kmem_free((caddr_t)intrlist, length);
	}
#endif
	/* request the parent node to add it */
	return (i_ddi_add_intrspec(dip, dip, ispec, iblock_cookiep,
	    idevice_cookiep, int_handler, int_handler_arg,
	    IDDI_INTR_TYPE_NORMAL));
}

int
ddi_add_fastintr(dev_info_t *dip, u_int inumber,
    ddi_iblock_cookie_t *iblock_cookiep,
    ddi_idevice_cookie_t *idevice_cookiep,
    u_int (*hi_int_handler)())
{
	ddi_intrspec_t ispec;

	/* get the named interrupt specification */
	if ((ispec = i_ddi_get_intrspec(dip, dip, inumber)) == NULL) {
		return (DDI_INTR_NOTFOUND);
	}

	/* request the parent node to add it */
	return (i_ddi_add_intrspec(dip, dip, ispec, iblock_cookiep,
	    idevice_cookiep, hi_int_handler, 0,	IDDI_INTR_TYPE_FAST));
}

int
ddi_add_softintr(dev_info_t *dip, int preference, ddi_softintr_t *idp,
    ddi_iblock_cookie_t *iblock_cookiep,
    ddi_idevice_cookie_t *idevice_cookiep,
    u_int (*int_handler)(caddr_t int_handler_arg),
    caddr_t int_handler_arg)
{
	return (i_ddi_add_softintr(dip, preference, idp, iblock_cookiep,
	    idevice_cookiep, int_handler, int_handler_arg));
}

void
ddi_trigger_softintr(ddi_softintr_t id)
{
	i_ddi_trigger_softintr(id);
}

void
ddi_remove_softintr(ddi_softintr_t id)
{
	i_ddi_remove_softintr(id);
}

void
ddi_remove_intr(dev_info_t *dip, u_int inum, ddi_iblock_cookie_t iblock_cookie)
{
	ddi_intrspec_t ispec;

	/* get the named interrupt specification */
	if ((ispec = i_ddi_get_intrspec(dip, dip, inum)) != NULL) {
		/* request the parent node to remove it */
		i_ddi_remove_intrspec(dip, dip, ispec, iblock_cookie);
#ifdef i386
		(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, chosen_intr);
#endif
	}
}

unsigned int
ddi_enter_critical(void)
{
	extern int spl7(void);
	return (spl7());
}

void
ddi_exit_critical(unsigned int spl)
{
#ifdef sparc
	extern void splx(int);
#endif
	(void) splx((int)spl);
}

/*
 * Nexus ctlops punter
 */

#ifndef	sparc
/*
 * Request bus_ctl parent to handle a bus_ctl request
 *
 * (In sparc_ddi.s)
 */
int
ddi_ctlops(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t op, void *a, void *v)
{
	register int (*fp)();

	if (!d || !r)
		return (DDI_FAILURE);

	if ((d = (dev_info_t *)DEVI(d)->devi_bus_ctl) == NULL)
		return (DDI_FAILURE);

	fp = DEVI(d)->devi_ops->devo_bus_ops->bus_ctl;
	return ((*fp)(d, r, op, a, v));
}
#endif

/*
 * DMA/DVMA setup
 */

#ifdef sparc
static ddi_dma_lim_t standard_limits = {
	(u_long)0,	/* addr_t dlim_addr_lo */
	(u_long)-1,	/* addr_t dlim_addr_hi */
	(u_int)-1,	/* u_int dlim_cntr_max */
	(u_int)1,	/* u_int dlim_burstsizes */
	(u_int)1,	/* u_int dlim_minxfer */
	0		/* u_int dlim_dmaspeed */
};
#endif
#ifdef i386
static ddi_dma_lim_t standard_limits = {
	(u_long)0,		/* addr_t dlim_addr_lo */
	(u_long)0xffffff,	/* addr_t dlim_addr_hi */
	(u_int)0,		/* u_int dlim_cntr_max */
	(u_int)0x00000001,	/* u_int dlim_burstsizes */
	(u_int)DMA_UNIT_8,	/* u_int dlim_minxfer */
	(u_int)0,		/* u_int dlim_dmaspeed */
	(u_int)0x86<<24+0,	/* u_int dlim_version */
	(u_int)0xffff,		/* u_int dlim_adreg_max */
	(u_int)0xffff,		/* u_int dlim_ctreg_max */
	(u_int)512,		/* u_int dlim_granular */
	(int)1,			/* int dlim_sgllen */
	(u_int)0xffffffff	/* u_int dlim_reqsizes */
};
#endif

#ifndef	sparc
/*
 * Request bus_dma_map parent to setup a dma request
 *
 * (In sparc_ddi.s)
 */
int
ddi_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareqp, ddi_dma_handle_t *handlep)
{
	register int (*fp)();

	dip = (dev_info_t *)DEVI(dip)->devi_bus_dma_map;
	fp = DEVI(dip)->devi_ops->devo_bus_ops->bus_dma_map;
	return ((*fp)(dip, rdip, dmareqp, handlep));
}

int
ddi_dma_allochdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_attr_t *attr,
    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep)
{
	register int (*fp)();

	dip = (dev_info_t *)DEVI(dip)->devi_bus_dma_allochdl;
	fp = DEVI(dip)->devi_ops->devo_bus_ops->bus_dma_allochdl;
	return ((*fp)(dip, rdip, attr, waitfp, arg, handlep));
}

int
ddi_dma_freehdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	register int (*fp)();

	dip = (dev_info_t *)DEVI(dip)->devi_bus_dma_freehdl;
	fp = DEVI(dip)->devi_ops->devo_bus_ops->bus_dma_freehdl;
	return ((*fp)(dip, rdip, handle));
}

/*
 * Request bus_dma_bindhdl parent to bind object to handle
 */
int
ddi_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
    ddi_dma_cookie_t *cp, u_int *ccountp)
{
	register int (*fp)();

	dip = (dev_info_t *)DEVI(dip)->devi_bus_dma_bindhdl;
	fp = DEVI(dip)->devi_ops->devo_bus_ops->bus_dma_bindhdl;
	return ((*fp)(dip, rdip, handle, dmareq, cp, ccountp));
}

/*
 * Request bus_dma_unbindhdl parent to unbind object from handle
 */
int
ddi_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	register int (*fp)();

	dip = (dev_info_t *)DEVI(dip)->devi_bus_dma_unbindhdl;
	fp = DEVI(dip)->devi_ops->devo_bus_ops->bus_dma_unbindhdl;
	return ((*fp)(dip, rdip, handle));
}

int
ddi_dma_flush(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, off_t off, u_int len,
    u_int cache_flags)
{
	register int (*fp)();

	dip = (dev_info_t *)DEVI(dip)->devi_bus_dma_flush;
	fp = DEVI(dip)->devi_ops->devo_bus_ops->bus_dma_flush;
	return ((*fp)(dip, rdip, handle, off, len, cache_flags));
}

int
ddi_dma_win(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, uint_t win, off_t *offp,
    uint_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	register int (*fp)();

	dip = (dev_info_t *)DEVI(dip)->devi_bus_dma_win;
	fp = DEVI(dip)->devi_ops->devo_bus_ops->bus_dma_win;
	return ((*fp)(dip, rdip, handle, win, offp, lenp, cookiep,
		ccountp));
}
#endif

int
ddi_dma_setup(dev_info_t *dip, struct ddi_dma_req *dmareqp,
    ddi_dma_handle_t *handlep)
{
	register int (*funcp)() = ddi_dma_map;
	register struct bus_ops *bop;
#ifdef sparc
	auto ddi_dma_lim_t dma_lim;

	if (dmareqp->dmar_limits == (ddi_dma_lim_t *)0) {
		dma_lim = standard_limits;
	} else {
		dma_lim = *dmareqp->dmar_limits;
	}
	dmareqp->dmar_limits = &dma_lim;
#endif
#ifdef i386
	if (dmareqp->dmar_limits == (ddi_dma_lim_t *)0)
		return (DDI_FAILURE);
#endif

	/*
	 * Handle the case that the requestor is both a leaf
	 * and a nexus driver simultaneously by calling the
	 * requestor's bus_dma_map function directly instead
	 * of ddi_dma_map.
	 */
	bop = DEVI(dip)->devi_ops->devo_bus_ops;
	if (bop && bop->bus_dma_map)
		funcp = bop->bus_dma_map;
	return ((*funcp)(dip, dip, dmareqp, handlep));
}

/*
 * The following three functions are convenience wrappers for ddi_dma_setup().
 */

int
ddi_dma_addr_setup(dev_info_t *dip, struct as *as, caddr_t addr, u_int len,
    u_int flags, int (*waitfp)(), caddr_t arg,
    ddi_dma_lim_t *limits, ddi_dma_handle_t *handlep)
{
	register int (*funcp)() = ddi_dma_map;
	auto ddi_dma_lim_t dma_lim;
	auto struct ddi_dma_req dmareq;
	register struct bus_ops *bop;

	if (limits == (ddi_dma_lim_t *)0) {
		dma_lim = standard_limits;
	} else {
		dma_lim = *limits;
	}
	dmareq.dmar_limits = &dma_lim;
	dmareq.dmar_flags = flags;
	dmareq.dmar_fp = waitfp;
	dmareq.dmar_arg = arg;
	dmareq.dmar_object.dmao_size = len;
	dmareq.dmar_object.dmao_type = DMA_OTYP_VADDR;
	dmareq.dmar_object.dmao_obj.virt_obj.v_as = as;
	dmareq.dmar_object.dmao_obj.virt_obj.v_addr = addr;

	/*
	 * Handle the case that the requestor is both a leaf
	 * and a nexus driver simultaneously by calling the
	 * requestor's bus_dma_map function directly instead
	 * of ddi_dma_map.
	 */
	bop = DEVI(dip)->devi_ops->devo_bus_ops;
	if (bop && bop->bus_dma_map)
		funcp = bop->bus_dma_map;

	return ((*funcp)(dip, dip, &dmareq, handlep));
}

int
ddi_dma_buf_setup(dev_info_t *dip, struct buf *bp, u_int flags,
    int (*waitfp)(), caddr_t arg, ddi_dma_lim_t *limits,
    ddi_dma_handle_t *handlep)
{
	register int (*funcp)() = ddi_dma_map;
	auto ddi_dma_lim_t dma_lim;
	auto struct ddi_dma_req dmareq;
	register struct bus_ops *bop;

	if (limits == (ddi_dma_lim_t *)0) {
		dma_lim = standard_limits;
	} else {
		dma_lim = *limits;
	}
	dmareq.dmar_limits = &dma_lim;
	dmareq.dmar_flags = flags;
	dmareq.dmar_fp = waitfp;
	dmareq.dmar_arg = arg;
	dmareq.dmar_object.dmao_size = (u_int) bp->b_bcount;

	if ((bp->b_flags & (B_PAGEIO|B_REMAPPED)) == B_PAGEIO) {
		dmareq.dmar_object.dmao_type = DMA_OTYP_PAGES;
		dmareq.dmar_object.dmao_obj.pp_obj.pp_pp = bp->b_pages;
		dmareq.dmar_object.dmao_obj.pp_obj.pp_offset =
		    (u_int) (((u_int)bp->b_un.b_addr) & MMU_PAGEOFFSET);
	} else {
		/*
		 * If the buffer has no proc pointer, or the proc
		 * struct has the kernel address space, or the buffer has
		 * been marked B_REMAPPED (meaning that it is now
		 * mapped into the kernel's address space), then
		 * the address space is kas (kernel address space).
		 *
		 * Otherwise, the address space described by the
		 * the buffer's process owner had better be valid!
		 */

		dmareq.dmar_object.dmao_type = DMA_OTYP_VADDR;
		dmareq.dmar_object.dmao_obj.virt_obj.v_addr = bp->b_un.b_addr;

		if (bp->b_proc == (struct proc *)NULL ||
		    bp->b_proc->p_as == &kas ||
		    (bp->b_flags & B_REMAPPED) != 0) {
			dmareq.dmar_object.dmao_obj.virt_obj.v_as = 0;
		} else {
			dmareq.dmar_object.dmao_obj.virt_obj.v_as =
			    bp->b_proc->p_as;
		}
	}

	/*
	 * Handle the case that the requestor is both a leaf
	 * and a nexus driver simultaneously by calling the
	 * requestor's bus_dma_map function directly instead
	 * of ddi_dma_map.
	 */
	bop = DEVI(dip)->devi_ops->devo_bus_ops;
	if (bop && bop->bus_dma_map)
		funcp = bop->bus_dma_map;

	return ((*funcp)(dip, dip, &dmareq, handlep));
}

int
ddi_dma_pp_setup(dev_info_t *dip, struct page *pp, off_t ppoff, u_int len,
    u_int flags, int (*waitfp)(), caddr_t arg,
    ddi_dma_lim_t *limits, ddi_dma_handle_t *handlep)
{
	register int (*funcp)() = ddi_dma_map;
	auto ddi_dma_lim_t dma_lim;
	auto struct ddi_dma_req dmareq;

	if (limits == (ddi_dma_lim_t *)0) {
		dma_lim = standard_limits;
	} else {
		dma_lim = *limits;
	}
	dmareq.dmar_limits = &dma_lim;
	dmareq.dmar_flags = flags;
	dmareq.dmar_fp = waitfp;
	dmareq.dmar_arg = arg;
	dmareq.dmar_object.dmao_size = len;
	dmareq.dmar_object.dmao_type = DMA_OTYP_PAGES;
	dmareq.dmar_object.dmao_obj.pp_obj.pp_pp = pp;
	dmareq.dmar_object.dmao_obj.pp_obj.pp_offset = ppoff;
	/*
	 * Handle the case that the requestor is both a leaf
	 * and a nexus driver simultaneously by calling the
	 * requestor's bus_dma_map function directly instead
	 * of ddi_dma_map.
	 */
	if (DEVI(dip)->devi_ops->devo_bus_ops) {
		funcp = DEVI(dip)->devi_ops->devo_bus_ops->bus_dma_map;
	}
	return ((*funcp)(dip, dip, &dmareq, handlep));
}

#ifndef	sparc
/*
 * Request bus_dma_ctl parent to fiddle with a dma request.
 *
 * (In sparc_subr.s)
 */
int
ddi_dma_mctl(register dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, u_int *lenp, caddr_t *objp, u_int flags)
{
	register int (*fp)();

	dip = (dev_info_t *)DEVI(dip)->devi_bus_dma_ctl;
	fp = DEVI(dip)->devi_ops->devo_bus_ops->bus_dma_ctl;
	return ((*fp) (dip, rdip, handle, request, offp, lenp, objp, flags));
}
#endif

/*
 * For all dma control functions, call the dma control
 * routine and return status.
 *
 * Just plain assume that the parent is to be called.
 * If a nexus driver or a thread outside the framework
 * of a nexus driver or a leaf driver calls these functions,
 * it is up to them to deal with the fact that the parent's
 * bus_dma_ctl function will be the first one called.
 */

#define	HD	((ddi_dma_impl_t *)h)->dmai_rdip

int
ddi_dma_kvaddrp(ddi_dma_handle_t h, off_t off, u_int len, caddr_t *kp)
{
	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_KVADDR, &off, &len, kp, 0));
}

int
ddi_dma_htoc(ddi_dma_handle_t h, off_t o, ddi_dma_cookie_t *c)
{
	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_HTOC, &o, 0, (caddr_t *)c, 0));
}

int
ddi_dma_coff(ddi_dma_handle_t h, ddi_dma_cookie_t *c, off_t *o)
{
	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_COFF,
	    (off_t *)c, 0, (caddr_t *)o, 0));
}

int
ddi_dma_movwin(ddi_dma_handle_t h, off_t *o, u_int *l, ddi_dma_cookie_t *c)
{
	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_MOVWIN, o,
	    l, (caddr_t *)c, 0));
}

int
ddi_dma_curwin(ddi_dma_handle_t h, off_t *o, u_int *l)
{
	if ((((ddi_dma_impl_t *)h)->dmai_rflags & DDI_DMA_PARTIAL) == 0)
		return (DDI_FAILURE);
	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_REPWIN, o, l, 0, 0));
}

int
ddi_dma_nextwin(register ddi_dma_handle_t h, ddi_dma_win_t win,
    ddi_dma_win_t *nwin)
{
#ifdef sparc
	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_NEXTWIN, (off_t *)&win, 0,
	    (caddr_t *)nwin, 0));
#else
	return (((ddi_dma_impl_t *)h)->dmai_mctl(HD, HD, h, DDI_DMA_NEXTWIN,
		(off_t *)win, 0, (caddr_t *)nwin, 0));
#endif
}

int
ddi_dma_nextseg(ddi_dma_win_t win, ddi_dma_seg_t seg, ddi_dma_seg_t *nseg)
{
#ifdef sparc
	ddi_dma_handle_t h = (ddi_dma_handle_t)win;

	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_NEXTSEG, (off_t *)&win,
	    (u_int *)&seg, (caddr_t *)nseg, 0));
#else
	register ddi_dma_handle_t h = (ddi_dma_handle_t)
	    ((impl_dma_segment_t *)win)->dmais_hndl;

	return (((ddi_dma_impl_t *)h)->dmai_mctl(HD, HD, h, DDI_DMA_NEXTSEG,
		(off_t *)win, (u_int *)seg, (caddr_t *)nseg, 0));
#endif
}

int
ddi_dma_segtocookie(ddi_dma_seg_t seg, off_t *o, off_t *l,
    ddi_dma_cookie_t *cookiep)
{
#ifdef sparc
	ddi_dma_handle_t h = (ddi_dma_handle_t)seg;

	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_SEGTOC, o, (u_int *)l,
	    (caddr_t *)cookiep, 0));
#else
	register ddi_dma_handle_t h = (ddi_dma_handle_t)
	    ((impl_dma_segment_t *)seg)->dmais_hndl;

	return (((ddi_dma_impl_t *)h)->dmai_mctl(HD, HD, h, DDI_DMA_SEGTOC,
		o, (u_int *)l, (caddr_t *)cookiep, (u_int)seg));
#endif
}

int
ddi_dma_get_error(ddi_dma_handle_t h, u_int len, caddr_t errblk)
{
	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_GETERR, 0, &len, &errblk, 0));
}

int
ddi_dma_sync(ddi_dma_handle_t h, off_t o, u_int l, u_int whom)
{
	register ddi_dma_impl_t *dimp = (ddi_dma_impl_t *)h;

	if ((whom == DDI_DMA_SYNC_FORDEV) &&
	    (dimp->dmai_rflags & DMP_NODEVSYNC)) {
		return (DDI_SUCCESS);
	} else if ((whom == DDI_DMA_SYNC_FORCPU ||
	    whom == DDI_DMA_SYNC_FORKERNEL) &&
	    (dimp->dmai_rflags & DMP_NOCPUSYNC)) {
		return (DDI_SUCCESS);
	}

#ifdef sparc
	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_SYNC, &o, &l, 0, whom));
#else
	return (((ddi_dma_impl_t *)h)->dmai_mctl(HD, HD, h, DDI_DMA_SYNC,
		&o, &l, 0, whom));
#endif
}

int
ddi_dma_free(ddi_dma_handle_t h)
{
#if defined(sparc)
	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_FREE, 0, 0, 0, 0));
#elif defined(i386)
	return (((ddi_dma_impl_t *)h)->dmai_mctl(HD, HD, h, DDI_DMA_FREE,
		0, 0, 0, 0));
#else
#error One of sparc or i386 must be defined
#endif
}

int
ddi_iopb_alloc(dev_info_t *dip, ddi_dma_lim_t *limp, u_int len, caddr_t *iopbp)
{
	auto ddi_dma_lim_t defalt;
	if (!limp) {
		defalt = standard_limits;
		limp = &defalt;
	}
#ifdef sparc
	return (i_ddi_mem_alloc(dip, limp, len, 0, 0, 0,
	    iopbp, (u_int *)0, NULL));
#else
	return (ddi_dma_mctl(dip, dip, 0, DDI_DMA_IOPB_ALLOC, (off_t *)limp,
	    (u_int *)len, iopbp, 0));
#endif
}

void
ddi_iopb_free(caddr_t iopb)
{
	i_ddi_mem_free(iopb, 0);
}

int
ddi_mem_alloc(dev_info_t *dip, ddi_dma_lim_t *limits, u_int length,
	u_int flags, caddr_t *kaddrp, u_int *real_length)
{
	auto ddi_dma_lim_t defalt;
	if (!limits) {
		defalt = standard_limits;
		limits = &defalt;
	}
#ifdef sparc
	return (i_ddi_mem_alloc(dip, limits, length, flags & 0x1, 1, 0, kaddrp,
	    real_length, NULL));
#else
	return (ddi_dma_mctl(dip, dip, (ddi_dma_handle_t)real_length,
	    DDI_DMA_SMEM_ALLOC, (off_t *)limits, (u_int *)length,
	    kaddrp, (flags & 0x1)));
#endif
}

void
ddi_mem_free(caddr_t kaddr)
{
	i_ddi_mem_free(kaddr, 1);
}

/*
 * DMA alignment, burst sizes, and transfer minimums
 */

int
ddi_dma_burstsizes(ddi_dma_handle_t handle)
{
	register ddi_dma_impl_t *dimp = (ddi_dma_impl_t *)handle;

	if (!dimp)
		return (0);
	else
		return (dimp->dmai_burstsizes);
}

int
ddi_dma_devalign(ddi_dma_handle_t handle, u_int *alignment, u_int *mineffect)
{
	register ddi_dma_impl_t *dimp = (ddi_dma_impl_t *)handle;

	if (!dimp || !alignment || !mineffect)
		return (DDI_FAILURE);
	if (!(dimp->dmai_rflags & DDI_DMA_SBUS_64BIT)) {
		*alignment = 1 << ddi_ffs(dimp->dmai_burstsizes);
	} else {
		if (dimp->dmai_burstsizes & 0xff0000) {
			*alignment = 1 << ddi_ffs(dimp->dmai_burstsizes >> 16);
		} else {
			*alignment = 1 << ddi_ffs(dimp->dmai_burstsizes);
		}
	}
	*mineffect = dimp->dmai_minxfer;
	return (DDI_SUCCESS);
}

int
ddi_iomin(dev_info_t *a, int i, int stream)
{
	int r;
	/*
	 * Make sure that the initial value is sane
	 */
	if (i & (i - 1))
		return (0);
	if (i == 0)
		i = (stream) ? 4 : 1;

	r = ddi_ctlops(a, a, DDI_CTLOPS_IOMIN, (void *)stream, (void *)&i);
	if (r != DDI_SUCCESS || (i & (i - 1)))
		return (0);
	else
		return (i);
}

void
ddi_dma_attr_merge(ddi_dma_attr_t *attr, ddi_dma_attr_t *mod)
{
	attr->dma_attr_addr_lo = (unsigned long long)
	    umax((u_int) attr->dma_attr_addr_lo, (u_int) mod->dma_attr_addr_lo);
	attr->dma_attr_addr_hi = (unsigned long long)
	    min((u_int) attr->dma_attr_addr_hi, (u_int) mod->dma_attr_addr_hi);
	attr->dma_attr_count_max = (unsigned long long)
	    min((u_int) attr->dma_attr_count_max,
		(u_int) mod->dma_attr_count_max);
	attr->dma_attr_align = (unsigned long long)
	    umax((u_int) attr->dma_attr_align, (u_int) mod->dma_attr_align);
	attr->dma_attr_burstsizes = (uint_t)
	    (attr->dma_attr_burstsizes & mod->dma_attr_burstsizes);
	attr->dma_attr_minxfer =
	    maxbit((u_int) attr->dma_attr_minxfer,
		(u_int) mod->dma_attr_minxfer);
	attr->dma_attr_maxxfer = (unsigned long long)
	    (unsigned long long)minbit((u_int) attr->dma_attr_maxxfer,
		(u_int) mod->dma_attr_maxxfer);
	attr->dma_attr_seg = (unsigned long long)
	    min((u_int) attr->dma_attr_seg, (u_int) mod->dma_attr_seg);
	attr->dma_attr_sgllen = (int)
	    min((u_int) attr->dma_attr_sgllen, (u_int) mod->dma_attr_sgllen);
	attr->dma_attr_granular = (uint_t)
	    (uint_t)umax((u_int) attr->dma_attr_granular,
		(u_int) mod->dma_attr_granular);
}

void
ddi_dmalim_merge(ddi_dma_lim_t *limit, ddi_dma_lim_t *mod)
{
#ifdef sparc
	limit->dlim_addr_hi = (u_long)
	    min((u_int) limit->dlim_addr_hi, (u_int) mod->dlim_addr_hi);
	limit->dlim_cntr_max =
	    min(limit->dlim_cntr_max, mod->dlim_cntr_max);
	limit->dlim_burstsizes =
	    limit->dlim_burstsizes & mod->dlim_burstsizes;
	limit->dlim_minxfer =
	    maxbit(limit->dlim_minxfer, mod->dlim_minxfer);
	limit->dlim_dmaspeed = max(limit->dlim_dmaspeed, mod->dlim_dmaspeed);
#endif
#ifdef i386
	limit->dlim_addr_lo =
	    umax(limit->dlim_addr_lo, mod->dlim_addr_lo);
	limit->dlim_addr_hi =
	    umin(limit->dlim_addr_hi, mod->dlim_addr_hi);
	limit->dlim_burstsizes =
	    limit->dlim_burstsizes & mod->dlim_burstsizes;
	limit->dlim_granular =
	    umax(limit->dlim_granular, mod->dlim_granular);
#endif
}

/*
 * mmap/segmap interface:
 */

/*
 * ddi_segmap:		setup the default segment driver. Calls the drivers
 *			XXmmap routine to validate the range to be mapped.
 *			Return ENXIO of the range is not valid.  Create
 *			a seg_dev segment that contains all of the
 *			necessary information and will reference the
 *			default segment driver routines. It returns zero
 *			on success or non-zero on failure.
 */
int
ddi_segmap(dev_t dev, off_t offset, struct as *asp, caddr_t *addrp, off_t len,
    u_int prot, u_int maxprot, u_int flags, cred_t *credp)
{
	extern int spec_segmap(dev_t, off_t, struct as *, caddr_t *,
	    off_t, u_int, u_int, u_int, struct cred *);

	return (spec_segmap(dev, offset, asp, addrp, len,
	    prot, maxprot, flags, credp));
}

/*
 * ddi_map_fault:	Resolve mappings at fault time.  Used by segment
 *			drivers. Allows each successive parent to resolve
 *			address translations and add its mappings to the
 *			mapping list supplied in the page structure. It
 *			returns zero on success	or non-zero on failure.
 */

int
ddi_map_fault(dev_info_t *dip, struct hat *hat, struct seg *seg,
    caddr_t addr, struct devpage *dp, u_int pfn, u_int prot, u_int lock)
{
	return (i_ddi_map_fault(dip, dip, hat, seg, addr, dp, pfn, prot, lock));
}

/*
 * ddi_device_mapping_check:	Called from ddi_mapdev_set_access_attr and
 *				ddi_segmap_setup.  Invokes the platform
 *				specific DDI to determine whether attributes
 *				specificed in the attr(9s) are valid for a
 *				region of memory that will be made available
 *				for direct access to a user process via the
 *				mmap(2) system call.
 */
int
ddi_device_mapping_check(dev_t dev, ddi_device_acc_attr_t *accattrp,
    u_int rnumber, u_int *hat_flags)
{
	ddi_acc_handle_t handle;
	ddi_map_req_t mr;
	ddi_acc_hdl_t *hp;
	int result;
	dev_info_t *dip;
	register major_t maj = getmajor(dev);

	/*
	 * only deals with character (VCHR) devices.
	 */
	if (!(dip = e_ddi_get_dev_info(dev, VCHR)))  {
		/*
		 * e_ddi_get_dev_info() only returns with the driver
		 * held if it successfully translated its dev_t.
		 */
		return (-1);
	}

	ddi_rele_driver(maj);	/* for dev_get_dev_info() */

	/*
	 * Allocate and initialize the common elements of data
	 * access handle.
	 */
	handle = impl_acc_hdl_alloc(KM_SLEEP, NULL);
	if (handle == NULL)
		return (-1);

	hp = impl_acc_hdl_get(handle);
	hp->ah_vers = VERS_ACCHDL;
	hp->ah_dip = dip;
	hp->ah_rnumber = rnumber;
	hp->ah_offset = 0;
	hp->ah_len = 0;
	hp->ah_acc = *accattrp;

	/*
	 * Set up the mapping request and call to parent.
	 */
	mr.map_op = DDI_MO_MAP_HANDLE;
	mr.map_type = DDI_MT_RNUMBER;
	mr.map_obj.rnumber = rnumber;
	mr.map_prot = PROT_READ | PROT_WRITE;
	mr.map_flags = DDI_MF_KERNEL_MAPPING;
	mr.map_handlep = hp;
	mr.map_vers = DDI_MAP_VERSION;
	result = ddi_map(dip, &mr, 0, 0, NULL);

	/*
	 * Region must be mappable, pick up flags from the framework.
	 */
	*hat_flags = hp->ah_hat_flags;

	impl_acc_hdl_free(handle);

	/*
	 * check for end result.
	 */
	if (result != DDI_SUCCESS) {
		return (-1);
	}

	return (0);
}


/*
 * Property functions:   See also, ddipropdefs.h.
 *
 * These functions are the framework for the property functions,
 * i.e. they support software defined properties.  All implementation
 * specific property handling (i.e.: self-identifying devices and
 * PROM defined properties are handled in the implementation specific
 * functions (defined in ddi_implfuncs.h).
 */

/*
 * nopropop:	Shouldn't be called, right?
 */

/* ARGSUSED */
int
nopropop(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op, int mod_flags,
    char *name, caddr_t valuep, int *lengthp)
{
	/*
	 * Should this be a panic?
	 */

	cmn_err(CE_CONT, "nopropop for driver <%s>", DEVI(dip)->devi_name);
	return (DDI_PROP_NOT_FOUND);
}

#ifdef	DDI_PROP_DEBUG
int ddi_prop_debug_flag = 0;

int
ddi_prop_debug(int enable)
{
	int prev = ddi_prop_debug_flag;

	if ((enable != 0) || (prev != 0))
		printf("ddi_prop_debug: debugging %s\n",
		    enable ? "enabled" : "disabled");
	ddi_prop_debug_flag = enable;
	return (prev);
}

#endif	DDI_PROP_DEBUG

/*
 * Search a property list for a match, if found return pointer
 * to matching prop struct, else return NULL.
 */

static ddi_prop_t *
ddi_prop_search(dev_t dev, char *name, u_int flags, ddi_prop_t **list_head)
{
	ddi_prop_t	*propp;

	/*
	 * find the property in child's devinfo:
	 */

	/*
	 * Search order defined by this search function is
	 * first matching property with input dev ==
	 * DDI_DEV_T_ANY matching any dev or dev == propp->prop_dev,
	 * name == propp->name, and the correct data type as specified
	 * in the flags
	 */

	for (propp = *list_head; propp != NULL; propp = propp->prop_next)  {

		if (strcmp(propp->prop_name, name) != 0)
			continue;

		if ((dev != DDI_DEV_T_ANY) && (propp->prop_dev != dev))
			continue;

		if (((propp->prop_flags & flags) & DDI_PROP_TYPE_MASK) == 0)
			continue;

		return (propp);
	}

	return ((ddi_prop_t *)0);
}


static char *prop_no_mem_msg = "can't allocate memory for ddi property <%s>";

/*
 * ddi_prop_search_common:	Lookup and return the encoded value
 */
int
ddi_prop_search_common(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op,
    u_int flags, char *name, void *valuep, u_int *lengthp)
{
	ddi_prop_t	*propp;
	int		i;
	caddr_t		buffer;
	caddr_t		prealloc = NULL;
	int		plength = 0;
	dev_info_t	*pdip;
	int		(*bop)();
	int		major;

	/*CONSTANTCONDITION*/
	while (1)  {

		mutex_enter(&(DEVI(dip)->devi_lock));

		/*
		 * find the property in child's devinfo:
		 * Search order is:
		 *	1. driver defined properties
		 *	2. system defined properties
		 *	3. driver global properties
		 */

		propp = ddi_prop_search(dev, name, flags,
		    &(DEVI(dip)->devi_drv_prop_ptr));
		if (propp == NULL)  {
			propp = ddi_prop_search(dev, name, flags,
			    &(DEVI(dip)->devi_sys_prop_ptr));
		}
		if (propp == NULL)  {
			major = ddi_name_to_major(ddi_get_name(dip));
			if (major != -1)
				propp = ddi_prop_search(dev, name, flags,
				    &devnamesp[major].dn_global_prop_ptr);
		}

		/*
		 * Software property found?
		 */
		if (propp != (ddi_prop_t *)0)   {

			/*
			 * If explicit undefine, return now.
			 */
			if (propp->prop_flags & DDI_PROP_UNDEF_IT) {
				mutex_exit(&(DEVI(dip)->devi_lock));
				if (prealloc)
					kmem_free(prealloc,
					    (size_t)plength);
				return (DDI_PROP_UNDEFINED);
			}

			/*
			 * If we only want to know if it exists, return now
			 */
			if (prop_op == PROP_EXISTS) {
				mutex_exit(&(DEVI(dip)->devi_lock));
				ASSERT(prealloc == NULL);
				return (DDI_PROP_SUCCESS);
			}

			/*
			 * If length only request or prop length == 0,
			 * service request and return now.
			 */
			if ((prop_op == PROP_LEN) ||(propp->prop_len == 0)) {
				*lengthp = propp->prop_len;
				mutex_exit(&(DEVI(dip)->devi_lock));
				if (prealloc)
					kmem_free(prealloc,
					    (size_t)plength);
				return (DDI_PROP_SUCCESS);
			}

			/*
			 * If LEN_AND_VAL_ALLOC and the request can sleep,
			 * drop the mutex, allocate the buffer, and go
			 * through the loop again.  If we already allocated
			 * the buffer, and the size of the property changed,
			 * keep trying...
			 */
			if ((prop_op == PROP_LEN_AND_VAL_ALLOC) &&
			    (flags & DDI_PROP_CANSLEEP))  {
				if (prealloc && (propp->prop_len != plength)) {
					kmem_free(prealloc,
					    (size_t)plength);
					prealloc = NULL;
				}
				if (prealloc == NULL)  {
					plength = propp->prop_len;
					mutex_exit(&(DEVI(dip)->devi_lock));
					prealloc = kmem_alloc((size_t)plength,
					    KM_SLEEP);
					continue;
				}
			}

			/*
			 * Allocate buffer, if required.  Either way,
			 * set `buffer' variable.
			 */
			i = *lengthp;			/* Get callers length */
			*lengthp = propp->prop_len;	/* Set callers length */

			switch (prop_op) {

			case PROP_LEN_AND_VAL_ALLOC:

				if (prealloc == NULL) {
					buffer = kmem_alloc(
					    (size_t)propp->prop_len,
					    KM_NOSLEEP);
				} else {
					buffer = prealloc;
				}

				if (buffer == NULL)  {
					mutex_exit(&(DEVI(dip)->devi_lock));
					cmn_err(CE_CONT, prop_no_mem_msg, name);
					return (DDI_PROP_NO_MEMORY);
				}
				/* Set callers buf ptr */
				*(caddr_t *)valuep = buffer;
				break;

			case PROP_LEN_AND_VAL_BUF:

				if (propp->prop_len > (i)) {
					mutex_exit(&(DEVI(dip)->devi_lock));
					return (DDI_PROP_BUF_TOO_SMALL);
				}

				buffer = valuep;  /* Get callers buf ptr */
				break;
			}

			/*
			 * Do the copy.
			 */
			bcopy(propp->prop_val, buffer, propp->prop_len);
			mutex_exit(&(DEVI(dip)->devi_lock));
			return (DDI_PROP_SUCCESS);
		}

		mutex_exit(&(DEVI(dip)->devi_lock));
		if (prealloc)
			kmem_free(prealloc, (size_t)plength);
		prealloc = NULL;

		/*
		 * Prop not found, call parent bus_ops to deal with possible
		 * h/w layer (possible PROM defined props, etc.) and to
		 * possibly ascend the hierarchy, if allowed by flags.
		 */
		pdip = (dev_info_t *)DEVI(dip)->devi_parent;

		/*
		 * One last call for the root driver PROM props?
		 */
		if (pdip == NULL)  {
			ASSERT(dip == ddi_root_node());
			return (ddi_bus_prop_op(dev, dip, dip, prop_op,
			    flags, name, valuep, (int *)lengthp));
		}

		/*
		 * Instead of recursing, we do interative calls up the tree.
		 * As a bit of optimization, skip the bus_op level if the
		 * node is a s/w node and if the parent's bus_prop_op function
		 * is `ddi_bus_prop_op', because we know that in this case,
		 * this function does nothing.
		 */
		i = DDI_PROP_NOT_FOUND;
		bop = DEVI(pdip)->devi_ops->devo_bus_ops->bus_prop_op;

		if ((bop != ddi_bus_prop_op) ||
		    (DEVI(dip)->devi_nodeid != DEVI_PSEUDO_NODEID))  {
			i = (*bop)(dev, pdip, dip, prop_op,
			    flags | DDI_PROP_DONTPASS,
			    name, valuep, lengthp);
		}

		if ((flags & DDI_PROP_DONTPASS) ||
		    (i != DDI_PROP_NOT_FOUND))
			return (i);

		dip = pdip;
	}
	/*NOTREACHED*/
}


/*
 * ddi_prop_op: The basic property operator for drivers.
 *
 * In ddi_prop_op, the type of valuep is interpreted based on prop_op:
 *
 *	prop_op			valuep
 *	------			------
 *
 *	PROP_LEN		<unused>
 *
 *	PROP_LEN_AND_VAL_BUF	Pointer to callers buffer
 *
 *	PROP_LEN_AND_VAL_ALLOC	Address of callers pointer (will be set to
 *				address of allocated buffer, if successful)
 */

ddi_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op, int mod_flags,
    char *name, caddr_t valuep, int *lengthp)
{
	int		i;

	ASSERT((mod_flags & DDI_PROP_TYPE_MASK) == 0);

	i = ddi_prop_search_common(dev, dip, prop_op,
		mod_flags | DDI_PROP_TYPE_ANY, name, valuep,
		(u_int *)lengthp);
	if (i == DDI_PROP_FOUND_1275)
		return (DDI_PROP_SUCCESS);
	return (i);
}


/*
 * Variable length props...
 */

/*
 * ddi_getlongprop:	Get variable length property len+val into a buffer
 *		allocated by property provider via kmem_alloc. Requestor
 *		is responsible for freeing returned property via kmem_free.
 *
 *	Arguments:
 *
 *	dev_t:	Input:	dev_t of property.
 *	dip:	Input:	dev_info_t pointer of child.
 *	flags:	Input:	Possible flag modifiers are:
 *		DDI_PROP_DONTPASS:	Don't pass to parent if prop not found.
 *		DDI_PROP_CANSLEEP:	Memory allocation may sleep.
 *	name:	Input:	name of property.
 *	valuep:	Output:	Addr of callers buffer pointer.
 *	lengthp:Output:	*lengthp will contain prop length on exit.
 *
 *	Possible Returns:
 *
 *		DDI_PROP_SUCCESS:	Prop found and returned.
 *		DDI_PROP_NOT_FOUND:	Prop not found
 *		DDI_PROP_UNDEFINED:	Prop explicitly undefined.
 *		DDI_PROP_NO_MEMORY:	Prop found, but unable to alloc mem.
 */

int
ddi_getlongprop(dev_t dev, dev_info_t *dip, int flags,
    char *name, caddr_t valuep, int *lengthp)
{
	return (ddi_prop_op(dev, dip, PROP_LEN_AND_VAL_ALLOC,
	    flags, name, valuep, lengthp));
}

/*
 *
 * ddi_getlongprop_buf:		Get long prop into pre-allocated callers
 *				buffer. (no memory allocation by provider).
 *
 *	dev_t:	Input:	dev_t of property.
 *	dip:	Input:	dev_info_t pointer of child.
 *	flags:	Input:	DDI_PROP_DONTPASS or NULL
 *	name:	Input:	name of property
 *	valuep:	Input:	ptr to callers buffer.
 *	lengthp:I/O:	ptr to length of callers buffer on entry,
 *			actual length of property on exit.
 *
 *	Possible returns:
 *
 *		DDI_PROP_SUCCESS	Prop found and returned
 *		DDI_PROP_NOT_FOUND	Prop not found
 *		DDI_PROP_UNDEFINED	Prop explicitly undefined.
 *		DDI_PROP_BUF_TOO_SMALL	Prop found, callers buf too small,
 *					no value returned, but actual prop
 *					length returned in *lengthp
 *
 */

int
ddi_getlongprop_buf(dev_t dev, dev_info_t *dip, int flags,
    char *name, caddr_t valuep, int *lengthp)
{
	return (ddi_prop_op(dev, dip, PROP_LEN_AND_VAL_BUF,
	    flags, name, valuep, lengthp));
}

/*
 * Integer/boolean sized props.
 *
 * Call is value only... returns found boolean or int sized prop value or
 * defvalue if prop not found or is wrong length or is explicitly undefined.
 * Only flag is DDI_PROP_DONTPASS...
 *
 * By convention, this interface returns boolean (0) sized properties
 * as value (int)1.
 *
 * This never returns an error, if property not found or specifically
 * undefined, the input `defvalue' is returned.
 */

int
ddi_getprop(dev_t dev, dev_info_t *dip, int flags, char *name, int defvalue)
{
	int	propvalue = defvalue;
	int	proplength = sizeof (int);
	int	error;

	error = ddi_prop_op(dev, dip, PROP_LEN_AND_VAL_BUF,
	    flags, name, (caddr_t)&propvalue, &proplength);

	if ((error == DDI_PROP_SUCCESS) && (proplength == 0))
		propvalue = 1;

	return (propvalue);
}

/*
 * Get prop length interface: flags are 0 or DDI_PROP_DONTPASS
 * if returns DDI_PROP_SUCCESS, length returned in *lengthp.
 */

int
ddi_getproplen(dev_t dev, dev_info_t *dip, int flags, char *name, int *lengthp)
{
	return (ddi_prop_op(dev, dip, PROP_LEN, flags, name, NULL, lengthp));
}

/*
 * Allocate a struct prop_driver_data, along with 'size' bytes
 * for decoded property data.  This structure is freed by
 * calling ddi_prop_free(9F).
 */
static void *
ddi_prop_decode_alloc(u_int size, void (*prop_free)(struct prop_driver_data *))
{
	struct prop_driver_data *pdd;

	/*
	 * Allocate a structure with enough memory to store the decoded data.
	 */
	pdd = kmem_zalloc((sizeof (struct prop_driver_data) + size), KM_SLEEP);
	pdd->pdd_size = (sizeof (struct prop_driver_data) + size);
	pdd->pdd_prop_free = prop_free;

	/*
	 * Return a pointer to the location to put the decoded data.
	 */
	return ((void *)((caddr_t)pdd + sizeof (struct prop_driver_data)));
}

/*
 * Allocated the memory needed to store the encoded data in the property
 * handle.
 */
static void
ddi_prop_encode_alloc(prop_handle_t *ph, u_int size)
{
	/*
	 * If size is zero, then set data to NULL and size to 0.  This
	 * is a boolean property.
	 */
	if (size == 0) {
		ph->ph_size = 0;
		ph->ph_data = NULL;
		ph->ph_cur_pos = NULL;
		ph->ph_save_pos = NULL;
	} else {
		ph->ph_size = size;
		ph->ph_data = kmem_zalloc(size, KM_SLEEP);
		ph->ph_cur_pos = ph->ph_data;
		ph->ph_save_pos = ph->ph_data;
	}
}

/*
 * Free the space allocated by the lookup routines.  Each lookup routine
 * returns a pointer to the decoded data to the driver.  The driver then
 * passes this pointer back to us.  This data actually lives in a struct
 * prop_driver_data.  We use negative indexing to find the beginning of
 * the structure and then free the entire structure using the size and
 * the free routine stored in the structure.
 */
void
ddi_prop_free(void *datap)
{
	struct prop_driver_data *pdd;

	/*
	 * Get the structure
	 */
	pdd = (struct prop_driver_data *)
		((caddr_t)datap - sizeof (struct prop_driver_data));
	/*
	 * Call the free routine to free it
	 */
	(*pdd->pdd_prop_free)(pdd);
}

/*
 * Free the data associated with an array of ints,
 * allocated with ddi_prop_decode_alloc().
 */
static void
ddi_prop_free_ints(struct prop_driver_data *pdd)
{
	kmem_free(pdd, pdd->pdd_size);
}

/*
 * Free a single string property or a single string contained within
 * the argv style return value of an array of strings.
 */
static void
ddi_prop_free_string(struct prop_driver_data *pdd)
{
	kmem_free(pdd, pdd->pdd_size);

}

/*
 * Free an array of strings.
 */
static void
ddi_prop_free_strings(struct prop_driver_data *pdd)
{
	kmem_free(pdd, pdd->pdd_size);
}

/*
 * Free the data associated with an array of bytes.
 */
static void
ddi_prop_free_bytes(struct prop_driver_data *pdd)
{
	kmem_free(pdd, pdd->pdd_size);
}

/*
 * Reset the current location pointer in the property handle to the
 * beginning of the data.
 */
void
ddi_prop_reset_pos(prop_handle_t *ph)
{
	ph->ph_cur_pos = ph->ph_data;
	ph->ph_save_pos = ph->ph_data;
}

/*
 * Restore the current location pointer in the property handle to the
 * saved position.
 */
void
ddi_prop_save_pos(prop_handle_t *ph)
{
	ph->ph_save_pos = ph->ph_cur_pos;
}

/*
 * Save the location that the current location poiner is pointing to..
 */
void
ddi_prop_restore_pos(prop_handle_t *ph)
{
	ph->ph_cur_pos = ph->ph_save_pos;
}

/*
 * Property encode/decode functions
 */

/*
 * Decode a single integer property
 */
static int
ddi_prop_fm_decode_int(prop_handle_t *ph, void *data, u_int *nelements)
{
	int	i;
	int	tmp;

	/*
	 * If there is nothing to decode return an error
	 */
	if (ph->ph_size == 0)
		return (DDI_PROP_END_OF_DATA);

	/*
	 * Decode the property as a single integer and return it
	 * in data if we were able to decode it.
	 */
	i = DDI_PROP_INT(ph, DDI_PROP_CMD_DECODE, &tmp);
	if (i < DDI_PROP_RESULT_OK) {
		switch (i) {
		case DDI_PROP_RESULT_EOF:
			return (DDI_PROP_END_OF_DATA);

		case DDI_PROP_RESULT_ERROR:
			return (DDI_PROP_CANNOT_DECODE);
		}
	}

	*(int *)data = tmp;
	*nelements = 1;
	return (DDI_PROP_SUCCESS);
}

/*
 * Decode an array of intergers property
 */
static int
ddi_prop_fm_decode_ints(prop_handle_t *ph, void *data, u_int *nelements)
{
	int	i;
	int	cnt = 0;
	int	*tmp;
	int	*intp;
	int	n;

	/*
	 * Figure out how many array elements there are by going through the
	 * data without decoding it first and counting.
	 */
	for (;;) {
		i = DDI_PROP_INT(ph, DDI_PROP_CMD_SKIP, NULL);
		if (i < 0)
			break;
		cnt++;
	}

	/*
	 * If there are no elements return an error
	 */
	if (cnt == 0)
		return (DDI_PROP_END_OF_DATA);

	/*
	 * If we cannot skip through the data, we cannot decode it
	 */
	if (i == DDI_PROP_RESULT_ERROR)
		return (DDI_PROP_CANNOT_DECODE);

	/*
	 * Reset the data pointer to the beginning of the encoded data
	 */
	ddi_prop_reset_pos(ph);

	/*
	 * Allocated memory to store the decoded value in.
	 */
	intp = ddi_prop_decode_alloc((cnt * sizeof (int)),
		ddi_prop_free_ints);

	/*
	 * Decode each elemente and place it in the space we just allocated
	 */
	tmp = intp;
	for (n = 0; n < cnt; n++, tmp++) {
		i = DDI_PROP_INT(ph, DDI_PROP_CMD_DECODE, tmp);
		if (i < DDI_PROP_RESULT_OK) {
			/*
			 * Free the space we just allocated
			 * and return an error.
			 */
			ddi_prop_free(intp);
			switch (i) {
			case DDI_PROP_RESULT_EOF:
				return (DDI_PROP_END_OF_DATA);

			case DDI_PROP_RESULT_ERROR:
				return (DDI_PROP_CANNOT_DECODE);
			}
		}
	}

	*nelements = cnt;
	*(int **)data = intp;

	return (DDI_PROP_SUCCESS);
}

/*
 * Encode an array of intergers property (Can be one element)
 */
static int
ddi_prop_fm_encode_ints(prop_handle_t *ph, void *data, u_int nelements)
{
	int	i;
	int	*tmp;
	int	cnt;
	int	size;

	/*
	 * If there is no data, we cannot do anything
	 */
	if (nelements == 0)
		return (DDI_PROP_CANNOT_ENCODE);

	/*
	 * Get the size of an encoded int.
	 */
	size = DDI_PROP_INT(ph, DDI_PROP_CMD_GET_ESIZE, NULL);
	if (size < DDI_PROP_RESULT_OK) {
		switch (size) {
		case DDI_PROP_RESULT_EOF:
			return (DDI_PROP_END_OF_DATA);

		case DDI_PROP_RESULT_ERROR:
			return (DDI_PROP_CANNOT_ENCODE);
		}
	}

	/*
	 * Allocate space in the handle to store the encoded int.
	 */
	ddi_prop_encode_alloc(ph, (u_int)(size * nelements));

	/*
	 * Encode the array of ints.
	 */
	tmp = (int *)data;
	for (cnt = 0; cnt < nelements; cnt++, tmp++) {
		i = DDI_PROP_INT(ph, DDI_PROP_CMD_ENCODE, tmp);
		if (i < DDI_PROP_RESULT_OK) {
			switch (i) {
			case DDI_PROP_RESULT_EOF:
				return (DDI_PROP_END_OF_DATA);

			case DDI_PROP_RESULT_ERROR:
				return (DDI_PROP_CANNOT_ENCODE);
			}
		}
	}

	return (DDI_PROP_SUCCESS);
}



/*
 * Decode a single string property
 */
static int
ddi_prop_fm_decode_string(prop_handle_t *ph, void *data, u_int *nelements)
{
	int		cnt = 0;
	char		*tmp;
	char		*str;
	int		size;
	int		i;

	/*
	 * If there is nothing to decode return an error
	 */
	if (ph->ph_size == 0)
		return (DDI_PROP_END_OF_DATA);

	/*
	 * Get the decoded size of the encoded string.
	 */
	size = DDI_PROP_STR(ph, DDI_PROP_CMD_GET_DSIZE, NULL);
	if (size < DDI_PROP_RESULT_OK) {
		switch (size) {
		case DDI_PROP_RESULT_EOF:
			return (DDI_PROP_END_OF_DATA);

		case DDI_PROP_RESULT_ERROR:
			return (DDI_PROP_CANNOT_DECODE);
		}
	}

	/*
	 * Allocated memory to store the decoded value in.
	 */
	str = ddi_prop_decode_alloc((u_int)size, ddi_prop_free_string);

	ddi_prop_reset_pos(ph);

	/*
	 * Decode the str and place it in the space we just allocated
	 */
	tmp = str;
	i = DDI_PROP_STR(ph, DDI_PROP_CMD_DECODE, tmp);
	if (i < DDI_PROP_RESULT_OK) {
		/*
		 * Free the space we just allocated
		 * and return an error.
		 */
		ddi_prop_free(str);
		switch (i) {
		case DDI_PROP_RESULT_EOF:
			return (DDI_PROP_END_OF_DATA);

		case DDI_PROP_RESULT_ERROR:
			return (DDI_PROP_CANNOT_DECODE);
		}
	}

	*(char **)data = str;
	*nelements = 1;

	return (DDI_PROP_SUCCESS);
}

/*
 * Decode an array of strings.
 */
static int
ddi_prop_fm_decode_strings(prop_handle_t *ph, void *data, u_int *nelements)
{
	int		cnt = 0;
	char		**strs;
	char		**tmp;
	char		*ptr;
	int		size;
	int		i;
	int		n;
	int		nbytes;

	/*
	 * Figure out how many array elements there are by going through the
	 * data without decoding it first and counting.
	 */
	for (;;) {
		i = DDI_PROP_STR(ph, DDI_PROP_CMD_SKIP, NULL);
		if (i < 0)
			break;
		cnt++;
	}

	/*
	 * If there are no elements return an error
	 */
	if (cnt == 0)
		return (DDI_PROP_END_OF_DATA);

	/*
	 * If we cannot skip through the data, we cannot decode it
	 */
	if (i == DDI_PROP_RESULT_ERROR)
		return (DDI_PROP_CANNOT_DECODE);

	/*
	 * Reset the data pointer to the beginning of the encoded data
	 */
	ddi_prop_reset_pos(ph);

	/*
	 * Figure out how much memory we need for the sum total
	 */
	nbytes = (cnt + 1) * sizeof (char *);

	for (n = 0; n < cnt; n++) {
		/*
		 * Get the decoded size of the current encoded string.
		 */
		size = DDI_PROP_STR(ph, DDI_PROP_CMD_GET_DSIZE, NULL);
		if (size < DDI_PROP_RESULT_OK) {
			switch (size) {
			case DDI_PROP_RESULT_EOF:
				return (DDI_PROP_END_OF_DATA);

			case DDI_PROP_RESULT_ERROR:
				return (DDI_PROP_CANNOT_DECODE);
			}
		}

		nbytes += size;
	}

	/*
	 * Allocate memory in which to store the decoded strings.
	 */
	strs = ddi_prop_decode_alloc(nbytes, ddi_prop_free_strings);

	/*
	 * Set up pointers for each string by figuring out yet
	 * again how long each string is.
	 */
	ddi_prop_reset_pos(ph);
	ptr = (caddr_t)strs + ((cnt+1) * sizeof (char *));
	for (tmp = strs, n = 0; n < cnt; n++, tmp++) {
		/*
		 * Get the decoded size of the current encoded string.
		 */
		size = DDI_PROP_STR(ph, DDI_PROP_CMD_GET_DSIZE, NULL);
		if (size < DDI_PROP_RESULT_OK) {
			ddi_prop_free(strs);
			switch (size) {
			case DDI_PROP_RESULT_EOF:
				return (DDI_PROP_END_OF_DATA);

			case DDI_PROP_RESULT_ERROR:
				return (DDI_PROP_CANNOT_DECODE);
			}
		}

		*tmp = ptr;
		ptr += size;
	}

	/*
	 * String array is terminated by a NULL
	 */
	*tmp = NULL;

	/*
	 * Finally, we can decode each string
	 */
	ddi_prop_reset_pos(ph);
	for (tmp = strs, n = 0; n < cnt; n++, tmp++) {
		i = DDI_PROP_STR(ph, DDI_PROP_CMD_DECODE, *tmp);
		if (i < DDI_PROP_RESULT_OK) {
			/*
			 * Free the space we just allocated
			 * and return an error
			 */
			ddi_prop_free(strs);
			switch (i) {
			case DDI_PROP_RESULT_EOF:
				return (DDI_PROP_END_OF_DATA);

			case DDI_PROP_RESULT_ERROR:
				return (DDI_PROP_CANNOT_DECODE);
			}
		}
	}

	*(char ***)data = strs;
	*nelements = cnt;

	return (DDI_PROP_SUCCESS);
}

/*
 * Encode a string.
 */
static int
ddi_prop_fm_encode_string(prop_handle_t *ph, void *data, u_int nelements)
{
	int		cnt = 0;
	char		**tmp;
	int		size;
	int		i;

	/*
	 * If there is no data, we cannot do anything
	 */
	if (nelements == 0)
		return (DDI_PROP_CANNOT_ENCODE);

	/*
	 * Get the size of the encoded string.
	 */
	tmp = (char **)data;
	size = DDI_PROP_STR(ph, DDI_PROP_CMD_GET_ESIZE, *tmp);
	if (size < DDI_PROP_RESULT_OK) {
		switch (size) {
		case DDI_PROP_RESULT_EOF:
			return (DDI_PROP_END_OF_DATA);

		case DDI_PROP_RESULT_ERROR:
			return (DDI_PROP_CANNOT_ENCODE);
		}
	}

	/*
	 * Allocate space in the handle to store the encoded string.
	 */
	ddi_prop_encode_alloc(ph, size);

	ddi_prop_reset_pos(ph);

	/*
	 * Encode the string.
	 */
	tmp = (char **)data;
	i = DDI_PROP_STR(ph, DDI_PROP_CMD_ENCODE, *tmp);
	if (i < DDI_PROP_RESULT_OK) {
		switch (i) {
		case DDI_PROP_RESULT_EOF:
			return (DDI_PROP_END_OF_DATA);

		case DDI_PROP_RESULT_ERROR:
			return (DDI_PROP_CANNOT_ENCODE);
		}
	}

	return (DDI_PROP_SUCCESS);
}


/*
 * Encode an array of strings.
 */
static int
ddi_prop_fm_encode_strings(prop_handle_t *ph, void *data, u_int nelements)
{
	int		cnt = 0;
	char		**tmp;
	int		size;
	u_int		total_size;
	int		i;

	/*
	 * If there is no data, we cannot do anything
	 */
	if (nelements == 0)
		return (DDI_PROP_CANNOT_ENCODE);

	/*
	 * Get the total size required to encode all the strings.
	 */
	total_size = 0;
	tmp = (char **)data;
	for (cnt = 0; cnt < nelements; cnt++, tmp++) {
		size = DDI_PROP_STR(ph, DDI_PROP_CMD_GET_ESIZE, *tmp);
		if (size < DDI_PROP_RESULT_OK) {
			switch (size) {
			case DDI_PROP_RESULT_EOF:
				return (DDI_PROP_END_OF_DATA);

			case DDI_PROP_RESULT_ERROR:
				return (DDI_PROP_CANNOT_ENCODE);
			}
		}
		total_size += (u_int)size;
	}

	/*
	 * Allocate space in the handle to store the encoded strings.
	 */
	ddi_prop_encode_alloc(ph, total_size);

	ddi_prop_reset_pos(ph);

	/*
	 * Encode the array of strings.
	 */
	tmp = (char **)data;
	for (cnt = 0; cnt < nelements; cnt++, tmp++) {
		i = DDI_PROP_STR(ph, DDI_PROP_CMD_ENCODE, *tmp);
		if (i < DDI_PROP_RESULT_OK) {
			switch (i) {
			case DDI_PROP_RESULT_EOF:
				return (DDI_PROP_END_OF_DATA);

			case DDI_PROP_RESULT_ERROR:
				return (DDI_PROP_CANNOT_ENCODE);
			}
		}
	}

	return (DDI_PROP_SUCCESS);
}


/*
 * Decode an array of bytes.
 */
static int
ddi_prop_fm_decode_bytes(prop_handle_t *ph, void *data, u_int *nelements)
{
	u_char		*tmp;
	int		nbytes;
	int		i;

	/*
	 * If there are no elements return an error
	 */
	if (ph->ph_size == 0)
		return (DDI_PROP_END_OF_DATA);

	/*
	 * Get the size of the encoded array of bytes.
	 */
	nbytes = DDI_PROP_BYTES(ph, DDI_PROP_CMD_GET_DSIZE,
		data, ph->ph_size);
	if (nbytes < DDI_PROP_RESULT_OK) {
		switch (nbytes) {
		case DDI_PROP_RESULT_EOF:
			return (DDI_PROP_END_OF_DATA);

		case DDI_PROP_RESULT_ERROR:
			return (DDI_PROP_CANNOT_DECODE);
		}
	}

	/*
	 * Allocated memory to store the decoded value in.
	 */
	tmp = ddi_prop_decode_alloc(nbytes, ddi_prop_free_bytes);

	/*
	 * Decode each element and place it in the space we just allocated
	 */
	i = DDI_PROP_BYTES(ph, DDI_PROP_CMD_DECODE, tmp, nbytes);
	if (i < DDI_PROP_RESULT_OK) {
		/*
		 * Free the space we just allocated
		 * and return an error
		 */
		ddi_prop_free(tmp);
		switch (i) {
		case DDI_PROP_RESULT_EOF:
			return (DDI_PROP_END_OF_DATA);

		case DDI_PROP_RESULT_ERROR:
			return (DDI_PROP_CANNOT_DECODE);
		}
	}

	*(u_char **)data = tmp;
	*nelements = nbytes;

	return (DDI_PROP_SUCCESS);
}

/*
 * Encode an array of bytes.
 */
static int
ddi_prop_fm_encode_bytes(prop_handle_t *ph, void *data, u_int nelements)
{
	int		cnt = 0;
	int		size;
	int		i;

	/*
	 * If there are no elements, then this is a boolean property,
	 * so just create a property handle with no data and return.
	 */
	if (nelements == 0) {
		ddi_prop_encode_alloc(ph, 0);
		return (DDI_PROP_SUCCESS);
	}

	/*
	 * Get the size of the encoded array of bytes.
	 */
	size = DDI_PROP_BYTES(ph, DDI_PROP_CMD_GET_ESIZE, (u_char *)data,
		nelements);
	if (size < DDI_PROP_RESULT_OK) {
		switch (size) {
		case DDI_PROP_RESULT_EOF:
			return (DDI_PROP_END_OF_DATA);

		case DDI_PROP_RESULT_ERROR:
			return (DDI_PROP_CANNOT_DECODE);
		}
	}

	/*
	 * Allocate space in the handle to store the encoded bytes.
	 */
	ddi_prop_encode_alloc(ph, (u_int)size);

	/*
	 * Encode the array of bytes.
	 */
	i = DDI_PROP_BYTES(ph, DDI_PROP_CMD_ENCODE, (u_char *)data,
		nelements);
	if (i < DDI_PROP_RESULT_OK) {
		switch (i) {
		case DDI_PROP_RESULT_EOF:
			return (DDI_PROP_END_OF_DATA);

		case DDI_PROP_RESULT_ERROR:
			return (DDI_PROP_CANNOT_ENCODE);
		}
	}

	return (DDI_PROP_SUCCESS);
}

/*
 * OBP 1275 integer, string and byte operators.
 *
 * DDI_PROP_CMD_DECODE:
 *
 *	DDI_PROP_RESULT_ERROR:		cannot decode the data
 *	DDI_PROP_RESULT_EOF:		end of data
 *	DDI_PROP_OK:			data was decoded
 *
 * DDI_PROP_CMD_ENCODE:
 *
 *	DDI_PROP_RESULT_ERROR:		cannot encode the data
 *	DDI_PROP_RESULT_EOF:		end of data
 *	DDI_PROP_OK:			data was encoded
 *
 * DDI_PROP_CMD_SKIP:
 *
 *	DDI_PROP_RESULT_ERROR:		cannot skip the data
 *	DDI_PROP_RESULT_EOF:		end of data
 *	DDI_PROP_OK:			data was skipped
 *
 * DDI_PROP_CMD_GET_ESIZE:
 *
 *	DDI_PROP_RESULT_ERROR:		cannot get encoded size
 *	DDI_PROP_RESULT_EOF:		end of data
 *	> 0:				the encoded size
 *
 * DDI_PROP_CMD_GET_DSIZE:
 *
 *	DDI_PROP_RESULT_ERROR:		cannot get decoded size
 *	DDI_PROP_RESULT_EOF:		end of data
 *	> 0:				the decoded size
 */

/*
 * OBP 1275 integer operator
 *
 * OBP properties are a byte stream of data, so integers may not be
 * properly aligned.  Therefore we need to copy them one byte at a time.
 */
int
ddi_prop_1275_int(prop_handle_t *ph, u_int cmd, int *data)
{
	int	i;

	switch (cmd) {
	case DDI_PROP_CMD_DECODE:
		/*
		 * Check that there is encoded data
		 */
		if (ph->ph_cur_pos == NULL || ph->ph_size == 0)
			return (DDI_PROP_RESULT_ERROR);
		if (ph->ph_flags & PH_FROM_PROM) {
			i = min(ph->ph_size, PROP_1275_INT_SIZE);
			if ((int *)ph->ph_cur_pos > ((int *)ph->ph_data +
				ph->ph_size - i))
				return (DDI_PROP_RESULT_ERROR);
		} else {
			if (ph->ph_size < sizeof (int) ||
			((int *)ph->ph_cur_pos > ((int *)ph->ph_data +
				ph->ph_size - sizeof (int))))
			return (DDI_PROP_RESULT_ERROR);
		}

		/*
		 * Copy the integer, using the implementation-specific
		 * copy function if the property is coming from the PROM.
		 */
		if (ph->ph_flags & PH_FROM_PROM) {
			*data = impl_ddi_prop_int_from_prom(
				(u_char *)ph->ph_cur_pos,
				(ph->ph_size < PROP_1275_INT_SIZE) ?
				ph->ph_size : PROP_1275_INT_SIZE);
		} else {
			bcopy(ph->ph_cur_pos, (caddr_t)data, sizeof (int));
		}

		/*
		 * Move the current location to the start of the next
		 * bit of undecoded data.
		 */
		ph->ph_cur_pos = (u_char *)ph->ph_cur_pos +
			PROP_1275_INT_SIZE;
		return (DDI_PROP_RESULT_OK);

	case DDI_PROP_CMD_ENCODE:
		/*
		 * Check that there is room to encoded the data
		 */
		if (ph->ph_cur_pos == NULL || ph->ph_size == 0 ||
			ph->ph_size < PROP_1275_INT_SIZE ||
			((int *)ph->ph_cur_pos > ((int *)ph->ph_data +
				ph->ph_size - sizeof (int))))
			return (DDI_PROP_RESULT_ERROR);

		/*
		 * Encode the integer into the byte stream one byte at a
		 * time.
		 */
		bcopy((caddr_t)data, ph->ph_cur_pos, sizeof (int));

		/*
		 * Move the current location to the start of the next bit of
		 * space where we can store encoded data.
		 */
		ph->ph_cur_pos = (u_char *)ph->ph_cur_pos + PROP_1275_INT_SIZE;
		return (DDI_PROP_RESULT_OK);

	case DDI_PROP_CMD_SKIP:
		/*
		 * Check that there is encoded data
		 */
		if (ph->ph_cur_pos == NULL || ph->ph_size == 0 ||
				ph->ph_size < PROP_1275_INT_SIZE)
			return (DDI_PROP_RESULT_ERROR);


		if ((caddr_t)ph->ph_cur_pos ==
				(caddr_t)ph->ph_data + ph->ph_size) {
			return (DDI_PROP_RESULT_EOF);
		} else if ((caddr_t)ph->ph_cur_pos >
				(caddr_t)ph->ph_data + ph->ph_size) {
			return (DDI_PROP_RESULT_EOF);
		}

		/*
		 * Move the current location to the start of the next bit of
		 * undecoded data.
		 */
		ph->ph_cur_pos = (u_char *)ph->ph_cur_pos + PROP_1275_INT_SIZE;
		return (DDI_PROP_RESULT_OK);

	case DDI_PROP_CMD_GET_ESIZE:
		/*
		 * Return the size of an encoded integer on OBP
		 */
		return (PROP_1275_INT_SIZE);

	case DDI_PROP_CMD_GET_DSIZE:
		/*
		 * Return the size of a decoded integer on the system.
		 */
		return (sizeof (int));

#ifdef	DEBUG
	default:
		cmn_err(CE_PANIC, "File %s, line %d: 0x%x impossible\n",
			__FILE__, __LINE__, cmd);
#endif	/* DEBUG */
	}

	/*NOTREACHED*/
}

/*
 * OBP 1275 string operator.
 *
 * OBP strings are NULL terminated.
 */
int
ddi_prop_1275_string(prop_handle_t *ph, u_int cmd, char *data)
{
	int	n;
	char	*p;
	char	*end;

	switch (cmd) {
	case DDI_PROP_CMD_DECODE:
		/*
		 * Check that there is encoded data
		 */
		if (ph->ph_cur_pos == NULL || ph->ph_size == 0) {
			return (DDI_PROP_RESULT_ERROR);
		}

		n = strlen((char *)ph->ph_cur_pos) + 1;
		if ((char *)ph->ph_cur_pos > ((char *)ph->ph_data +
				ph->ph_size - n)) {
			return (DDI_PROP_RESULT_ERROR);
		}

		/*
		 * Copy the NULL terminated string
		 */
		bcopy((char *)ph->ph_cur_pos, data, n);

		/*
		 * Move the current location to the start of the next bit of
		 * undecoded data.
		 */
		ph->ph_cur_pos = (char *)ph->ph_cur_pos + n;
		return (DDI_PROP_RESULT_OK);

	case DDI_PROP_CMD_ENCODE:
		/*
		 * Check that there is room to encoded the data
		 */
		if (ph->ph_cur_pos == NULL || ph->ph_size == 0) {
			return (DDI_PROP_RESULT_ERROR);
		}

		n = strlen(data) + 1;
		if ((char *)ph->ph_cur_pos > ((char *)ph->ph_data +
				ph->ph_size - n)) {
			return (DDI_PROP_RESULT_ERROR);
		}

		/*
		 * Copy the NULL terminated string
		 */
		bcopy(data, (char *)ph->ph_cur_pos, n);

		/*
		 * Move the current location to the start of the next bit of
		 * space where we can store encoded data.
		 */
		ph->ph_cur_pos = (char *)ph->ph_cur_pos + n;
		return (DDI_PROP_RESULT_OK);

	case DDI_PROP_CMD_SKIP:
		/*
		 * Check that there is encoded data
		 */
		if (ph->ph_cur_pos == NULL || ph->ph_size == 0) {
			return (DDI_PROP_RESULT_ERROR);
		}

		/*
		 * Return the string length plus one for the NULL
		 * We know the size of the property, we need to
		 * ensure that the string is properly formatted,
		 * since we may be looking up random OBP data.
		 */
		p = (char *)ph->ph_cur_pos;
		end = (char *)ph->ph_data + ph->ph_size;

		if (p == end) {
			return (DDI_PROP_RESULT_EOF);
		}

		for (n = 0; p < end; n++) {
			if (*p++ == 0) {
				ph->ph_cur_pos = p;
				return (DDI_PROP_RESULT_OK);
			}
		}

		return (DDI_PROP_RESULT_ERROR);

	case DDI_PROP_CMD_GET_ESIZE:
		/*
		 * Return the size of the encoded string on OBP.
		 */
		return (strlen(data) + 1);

	case DDI_PROP_CMD_GET_DSIZE:
		/*
		 * Return the string length plus one for the NULL
		 * We know the size of the property, we need to
		 * ensure that the string is properly formatted,
		 * since we may be looking up random OBP data.
		 */
		p = (char *)ph->ph_cur_pos;
		end = (char *)ph->ph_data + ph->ph_size;
		for (n = 0; p < end; n++) {
			if (*p++ == 0) {
				ph->ph_cur_pos = p;
				return (n+1);
			}
		}
		return (DDI_PROP_RESULT_ERROR);

#ifdef	DEBUG
	default:
		cmn_err(CE_PANIC, "File %s, line %d: 0x%x impossible\n",
			__FILE__, __LINE__, cmd);
#endif	/* DEBUG */
	}

	/*NOTREACHED*/
}

/*
 * OBP 1275 byte operator
 *
 * Caller must specify the number of bytes to get.  OBP encodes bytes
 * as a byte so there is a 1-to-1 translation.
 */
int
ddi_prop_1275_bytes(prop_handle_t *ph, u_int cmd, u_char *data, u_int nelements)
{
	switch (cmd) {
	case DDI_PROP_CMD_DECODE:
		/*
		 * Check that there is encoded data
		 */
		if (ph->ph_cur_pos == NULL || ph->ph_size == 0 ||
			ph->ph_size < nelements ||
			((char *)ph->ph_cur_pos > ((char *)ph->ph_data +
				ph->ph_size - nelements)))
			return (DDI_PROP_RESULT_ERROR);

		/*
		 * Copy out the bytes
		 */
		bcopy((char *)ph->ph_cur_pos, (char *)data, nelements);

		/*
		 * Move the current location
		 */
		ph->ph_cur_pos = (char *)ph->ph_cur_pos + nelements;
		return (DDI_PROP_RESULT_OK);

	case DDI_PROP_CMD_ENCODE:
		/*
		 * Check that there is room to encode the data
		 */
		if (ph->ph_cur_pos == NULL || ph->ph_size == 0 ||
			ph->ph_size < nelements ||
			((char *)ph->ph_cur_pos > ((char *)ph->ph_data +
				ph->ph_size - nelements)))
			return (DDI_PROP_RESULT_ERROR);

		/*
		 * Copy in the bytes
		 */
		bcopy((char *)data, (char *)ph->ph_cur_pos, nelements);

		/*
		 * Move the current location to the start of the next bit of
		 * space where we can store encoded data.
		 */
		ph->ph_cur_pos = (char *)ph->ph_cur_pos + nelements;
		return (DDI_PROP_RESULT_OK);

	case DDI_PROP_CMD_SKIP:
		/*
		 * Check that there is encoded data
		 */
		if (ph->ph_cur_pos == NULL || ph->ph_size == 0 ||
				ph->ph_size < nelements)
			return (DDI_PROP_RESULT_ERROR);

		if ((char *)ph->ph_cur_pos > ((char *)ph->ph_data +
				ph->ph_size - nelements))
			return (DDI_PROP_RESULT_EOF);

		/*
		 * Move the current location
		 */
		ph->ph_cur_pos = (char *)ph->ph_cur_pos + nelements;
		return (DDI_PROP_RESULT_OK);

	case DDI_PROP_CMD_GET_ESIZE:
		/*
		 * The size in bytes of the encoded size is the
		 * same as the decoded size provided by the caller.
		 */
		return (nelements);

	case DDI_PROP_CMD_GET_DSIZE:
		/*
		 * Just return the number of bytes specified by the caller.
		 */
		return (nelements);

#ifdef	DEBUG
	default:
		cmn_err(CE_PANIC, "File %s, line %d: 0x%x impossible\n",
			__FILE__, __LINE__, cmd);
#endif	/* DEBUG */
	}

	/*NOTREACHED*/
}

/*
 * Used for properties that come from the OBP, hardware configuration files,
 * or that are created by calls to ddi_prop_update(9F).
 */
static struct prop_handle_ops prop_1275_ops = {
	ddi_prop_1275_int,
	ddi_prop_1275_string,
	ddi_prop_1275_bytes
};


/*
 * Interface to create/modify a managed property on child's behalf...
 * Flags interpreted are:
 *	DDI_PROP_CANSLEEP:	Allow memory allocation to sleep.
 *	DDI_PROP_SYSTEM_DEF:	Manipulate system list rather than driver list.
 *
 * Use same dev_t when modifying or undefining a property.
 * Search for properties with DDI_DEV_T_ANY to match first named
 * property on the list.
 *
 * Properties are stored LIFO and subsequently will match the first
 * `matching' instance.
 */

/*
 * ddi_prop_add:	Add a software defined property
 */

/*
 * define to get a new ddi_prop_t.
 * km_flags are KM_SLEEP or KM_NOSLEEP.
 */

#define	DDI_NEW_PROP_T(km_flags)	\
	((ddi_prop_t *)kmem_zalloc(sizeof (ddi_prop_t), km_flags))

static int
ddi_prop_add(dev_t dev, dev_info_t *dip, int flags,
    char *name, caddr_t value, int length)
{
	ddi_prop_t	*new_propp, *propp;
	ddi_prop_t	**list_head = &(DEVI(dip)->devi_drv_prop_ptr);
	int		km_flags = KM_NOSLEEP;
	int		name_buf_len;

	/*
	 * If dev_t is DDI_DEV_T_ANY or name's length is zero return error.
	 */

	if ((dev == DDI_DEV_T_ANY) || (name == (char *)0) ||
	    (strlen(name) == 0))
		return (DDI_PROP_INVAL_ARG);

	if (flags & DDI_PROP_CANSLEEP)
		km_flags = KM_SLEEP;

	if (flags & DDI_PROP_SYSTEM_DEF)  {
		list_head = &(DEVI(dip)->devi_sys_prop_ptr);
	}

	if ((new_propp = DDI_NEW_PROP_T(km_flags)) == NULL)  {
		cmn_err(CE_CONT, prop_no_mem_msg, name);
		return (DDI_PROP_NO_MEMORY);
	}

	/*
	 * If dev is major number 0, then we need to do a ddi_name_to_major
	 * to get the real major number for the device.  This needs to be
	 * done because some drivers need to call ddi_prop_create in their
	 * attach routines but they don't have a dev.  By creating the dev
	 * ourself if the major number is 0, drivers will not have to know what
	 * their major number.  They can just create a dev with major number
	 * 0 and pass it in.  For device 0, we will be doing a little extra
	 * work by recreating the same dev that we already have, but its the
	 * price you pay :-).
	 *
	 * This fixes bug #1098060.
	 */
	if (getmajor(dev) == DDI_MAJOR_T_UNKNOWN) {
		new_propp->prop_dev =
		    makedevice(ddi_name_to_major(DEVI(dip)->devi_name),
		    getminor(dev));
	} else {
		new_propp->prop_dev = dev;
	}

	/*
	 * Allocate space for property name and copy it in...
	 */

	name_buf_len = strlen(name)+1;
	new_propp->prop_name = (char *)kmem_alloc((size_t)name_buf_len,
	    km_flags);
	if (new_propp->prop_name == 0)  {
		kmem_free(new_propp, sizeof (ddi_prop_t));
		cmn_err(CE_CONT, prop_no_mem_msg, name);
		return (DDI_PROP_NO_MEMORY);
	}
	bcopy((caddr_t)name, (caddr_t)new_propp->prop_name, (u_int)
	    name_buf_len);

	/*
	 * Set the property type
	 */
	new_propp->prop_flags = flags & DDI_PROP_TYPE_MASK;

	/*
	 * Set length and value ONLY if not an explicit property undefine:
	 * NOTE: value and length are zero for explicit undefines.
	 */

	if (flags & DDI_PROP_UNDEF_IT) {
		new_propp->prop_flags |= DDI_PROP_UNDEF_IT;
	} else {
		if ((new_propp->prop_len = length) != 0) {
			new_propp->prop_val = (caddr_t)kmem_alloc(
			    (size_t)length, km_flags);
			if (new_propp->prop_val == 0)  {
				kmem_free(new_propp->prop_name,
				    (size_t)name_buf_len);
				kmem_free(new_propp,
				    sizeof (ddi_prop_t));
				cmn_err(CE_CONT, prop_no_mem_msg, name);
				return (DDI_PROP_NO_MEMORY);
			}
			bcopy((caddr_t)value,
			    (caddr_t)new_propp->prop_val, (u_int)length);
		}
	}

	/*
	 * Link property into beginning of list. (Properties are LIFO order.)
	 */

	mutex_enter(&(DEVI(dip)->devi_lock));
	propp = *list_head;
	new_propp->prop_next = propp;
	*list_head = new_propp;
	mutex_exit(&(DEVI(dip)->devi_lock));
	return (DDI_PROP_SUCCESS);
}


/*
 * ddi_prop_modify_common:	Modify a software managed property value
 *
 *			Set new length and value if found.
 *			returns DDI_PROP_NOT_FOUND, if s/w defined prop
 *			not found or no exact match of input dev_t.
 *			DDI_PROP_INVAL_ARG if dev is DDI_DEV_T_ANY or
 *			input name is the NULL string.
 *			DDI_PROP_NO_MEMORY if unable to allocate memory
 *
 *			Note: an undef can be modified to be a define,
 *			(you can't go the other way.)
 */

static int
ddi_prop_change(dev_t dev, dev_info_t *dip, int flags,
    char *name, caddr_t value, int length)
{
	ddi_prop_t	*propp;
	int		km_flags = KM_NOSLEEP;
	caddr_t		p = NULL;

	if ((dev == DDI_DEV_T_ANY) || (name == (char *)0) ||
	    (strlen(name) == 0))
		return (DDI_PROP_INVAL_ARG);

	if (flags & DDI_PROP_CANSLEEP)
		km_flags = KM_SLEEP;

	/*
	 * Preallocate buffer, even if we don't need it...
	 */
	if (length != 0)  {
		p = (caddr_t)kmem_alloc((size_t)length, km_flags);
		if (p == NULL)  {
			cmn_err(CE_CONT, prop_no_mem_msg, name);
			return (DDI_PROP_NO_MEMORY);
		}
	}

	mutex_enter(&(DEVI(dip)->devi_lock));

	propp = DEVI(dip)->devi_drv_prop_ptr;
	if (flags & DDI_PROP_SYSTEM_DEF)  {
		propp = DEVI(dip)->devi_sys_prop_ptr;
	}

	while (propp != NULL) {
		if ((strcmp(name, propp->prop_name) == 0) &&
		    (dev == propp->prop_dev)) {

			/*
			 * Need to reallocate buffer?  If so, do it
			 * (carefully). (Reuse same space if new prop
			 * is same size and non-NULL sized).
			 */

			if (length != 0)
				bcopy((caddr_t)value, p, (u_int)length);

			if (propp->prop_len != 0)
				kmem_free(propp->prop_val,
				    (size_t)propp->prop_len);

			propp->prop_len = length;
			propp->prop_val = p;
			propp->prop_flags &= ~DDI_PROP_UNDEF_IT;
			mutex_exit(&(DEVI(dip)->devi_lock));
			return (DDI_PROP_SUCCESS);
		}
		propp = propp->prop_next;
	}

	mutex_exit(&(DEVI(dip)->devi_lock));
	if (length != 0)
		kmem_free(p, (size_t)length);
	return (DDI_PROP_NOT_FOUND);
}



/*
 * Common update routine used to update and encode a property.  Creates
 * a property handle, calls the property encode routine, figures out if
 * the property already exists and updates if it does.  Otherwise it
 * creates if it does not exist.
 */
static int
ddi_prop_update_common(dev_t match_dev, dev_info_t *dip, int flags,
    char *name, void *data, u_int nelements,
    int (*prop_create)(prop_handle_t *, void *data, u_int nelements))
{
	prop_handle_t	ph;
	int		rval;
	u_int		ourflags;
	dev_t		search_dev;

	/*
	 * If dev_t is DDI_DEV_T_ANY or name's length is zero,
	 * return error.
	 */
	if ((match_dev == DDI_DEV_T_ANY) ||
			(name == NULL) || (strlen(name) == 0))
		return (DDI_PROP_INVAL_ARG);

	/*
	 * Create the handle
	 */
	ph.ph_data = NULL;
	ph.ph_cur_pos = NULL;
	ph.ph_save_pos = NULL;
	ph.ph_size = 0;
	ph.ph_flags = 0;
	ph.ph_ops = &prop_1275_ops;

	/*
	 * Encode the data and store it in the property handle by
	 * calling the prop_encode routine.
	 */
	if ((rval = (*prop_create)(&ph, data, nelements)) !=
	    DDI_PROP_SUCCESS) {
		if (ph.ph_size > 0)
			kmem_free(ph.ph_data, ph.ph_size);
		return (rval);
	}

	/*
	 * For compatibility with the old interfaces.  The old interfaces
	 * didn't sleep by default and slept when the flag was set.  These
	 * interfaces to the opposite.  So the old interfaces now set the
	 * DDI_PROP_DONTSLEEP flag by default which tells us not to sleep.
	 */
	if (flags & DDI_PROP_DONTSLEEP)
		ourflags = flags;
	else
		ourflags = flags | DDI_PROP_CANSLEEP;

	/*
	 * If we are doing a wildcard update, we need to use the wildcard
	 * search dev DDI_DEV_T_ANY when checking to see if the property
	 * exists.
	 */
	search_dev = (match_dev == DDI_DEV_T_NONE) ?
		DDI_DEV_T_ANY : match_dev;

	/*
	 * The old interfaces use a stacking approach to creating
	 * properties.  If we are being called from the old interfaces,
	 * the DDI_PROP_STACK_CREATE flag will be set, so we just do a
	 * create without checking.
	 *
	 * Otherwise we check to see if the property exists.  If so we
	 * modify it else we create it.  We only check the driver
	 * property list.  So if a property exists on another list or
	 * the PROM, we will create one of our own.
	 */
	if ((flags & DDI_PROP_STACK_CREATE) ||
	    !ddi_prop_exists(search_dev, dip,
	    (ourflags | DDI_PROP_DONTPASS | DDI_PROP_NOTPROM), name)) {
		rval = ddi_prop_add(match_dev, dip,
		    ourflags, name, ph.ph_data, ph.ph_size);
	} else {
		rval = ddi_prop_change(match_dev, dip,
		    ourflags, name, ph.ph_data, ph.ph_size);
	}

	/*
	 * Free the encoded data allocated in the prop_encode routine.
	 */
	if (ph.ph_size > 0)
		kmem_free(ph.ph_data, ph.ph_size);

	return (rval);
}


/*
 * ddi_prop_create:	Define a managed property:
 *			See above for details.
 */

int
ddi_prop_create(dev_t dev, dev_info_t *dip, int flag,
    char *name, caddr_t value, int length)
{
	flag &= ~DDI_PROP_SYSTEM_DEF;
	return (ddi_prop_update_common(dev, dip,
	    (flag | DDI_PROP_STACK_CREATE | DDI_PROP_TYPE_ANY), name,
	    value, length, ddi_prop_fm_encode_bytes));
}

int
e_ddi_prop_create(dev_t dev, dev_info_t *dip, int flag,
    char *name, caddr_t value, int length)
{
	return (ddi_prop_update_common(dev, dip,
	    (flag | DDI_PROP_SYSTEM_DEF | DDI_PROP_STACK_CREATE |
		DDI_PROP_TYPE_ANY),
	    name, value, length, ddi_prop_fm_encode_bytes));
}

ddi_prop_modify(dev_t dev, dev_info_t *dip, int flag,
    char *name, caddr_t value, int length)
{
	ASSERT((flag & DDI_PROP_TYPE_MASK) == 0);

	/*
	 * If dev_t is DDI_DEV_T_ANY or name's length is zero,
	 * return error.
	 */
	if ((dev == DDI_DEV_T_ANY) || (name == NULL) || (strlen(name) == 0))
		return (DDI_PROP_INVAL_ARG);

	flag &= ~DDI_PROP_SYSTEM_DEF;
	if (ddi_prop_exists((dev == DDI_DEV_T_NONE) ? DDI_DEV_T_ANY : dev,
		dip, (flag | DDI_PROP_NOTPROM), name) == 0) {
		return (DDI_PROP_NOT_FOUND);
	}

	return (ddi_prop_update_common(dev, dip,
		(flag | DDI_PROP_TYPE_BYTE), name,
		value, length, ddi_prop_fm_encode_bytes));
}

int
e_ddi_prop_modify(dev_t dev, dev_info_t *dip, int flag,
    char *name, caddr_t value, int length)
{
	ASSERT((flag & DDI_PROP_TYPE_MASK) == 0);

	/*
	 * If dev_t is DDI_DEV_T_ANY or name's length is zero,
	 * return error.
	 */
	if ((dev == DDI_DEV_T_ANY) || (name == NULL) || (strlen(name) == 0))
		return (DDI_PROP_INVAL_ARG);

	if (ddi_prop_exists((dev == DDI_DEV_T_NONE) ? DDI_DEV_T_ANY : dev,
		dip, (flag | DDI_PROP_SYSTEM_DEF), name) == 0) {
		return (DDI_PROP_NOT_FOUND);
	}

	return (ddi_prop_update_common(dev, dip,
		(flag | DDI_PROP_SYSTEM_DEF | DDI_PROP_TYPE_BYTE),
		name, value, length, ddi_prop_fm_encode_bytes));
}


/*
 * Common lookup routine used to lookup and decode a property.
 * Creates a property handle, searches for the raw encoded data,
 * fills in the handle, and calls the property decode functions
 * passed in.
 *
 * This routine is not static because ddi_bus_prop_op() which lives in
 * ddi_impl.c calls it.  No driver should be calling this routine.
 */
int
ddi_prop_lookup_common(dev_t match_dev, dev_info_t *dip,
    u_int flags, char *name, void *data, u_int *nelements,
    int (*prop_decoder)(prop_handle_t *, void *data, u_int *nelements))
{
	int		rval;
	u_int		ourflags;
	prop_handle_t	ph;

	ourflags = (flags & DDI_PROP_DONTSLEEP) ? flags :
		flags | DDI_PROP_CANSLEEP;

	/*
	 * Get the encoded data
	 */
	bzero((caddr_t)&ph, sizeof (prop_handle_t));
	rval = ddi_prop_search_common(match_dev, dip, PROP_LEN_AND_VAL_ALLOC,
		ourflags, name, &ph.ph_data, &ph.ph_size);
	if (rval != DDI_PROP_SUCCESS && rval != DDI_PROP_FOUND_1275) {
		ASSERT(ph.ph_data == NULL);
		ASSERT(ph.ph_size == 0);
		return (rval);
	}

	/*
	 * If the encoded data came from a OBP or software
	 * use the 1275 OBP decode/encode routines.
	 */
	ph.ph_cur_pos = ph.ph_data;
	ph.ph_save_pos = ph.ph_data;
	ph.ph_ops = &prop_1275_ops;
	ph.ph_flags = (rval == DDI_PROP_FOUND_1275) ? PH_FROM_PROM : 0;

	rval = (*prop_decoder)(&ph, data, nelements);

	/*
	 * Free the encoded data
	 */
	if (ph.ph_size > 0)
		kmem_free(ph.ph_data, ph.ph_size);

	return (rval);
}

/*
 * Lookup and return an array of composit properties.  The driver must
 * provide the decode routine.
 */
int
ddi_prop_lookup(dev_t match_dev, dev_info_t *dip,
    u_int flags, char *name, void *data, u_int *nelements,
    int (*prop_decoder)(prop_handle_t *, void *data, u_int *nelements))
{
	return (ddi_prop_lookup_common(match_dev, dip,
	    (flags | DDI_PROP_TYPE_COMPOSITE), name,
	    data, nelements, prop_decoder));
}

/*
 * Return 1 if a property exists (no type checking done).
 * Return 0 if it does not exist.
 */
int
ddi_prop_exists(dev_t match_dev, dev_info_t *dip, u_int flags, char *name)
{
	int	i;
	u_int	x = 0;
	u_int	ourflags;

	if (flags & DDI_PROP_TYPE_MASK)
		ourflags = flags;
	else
		ourflags = (flags | DDI_PROP_TYPE_ANY);

	i = ddi_prop_search_common(match_dev, dip, PROP_EXISTS,
		ourflags, name, NULL, &x);
	return ((i == DDI_PROP_SUCCESS || i == DDI_PROP_FOUND_1275));
}


/*
 * Update an array of composit properties.  The driver must
 * provide the encode routine.
 */
int
ddi_prop_update(dev_t match_dev, dev_info_t *dip,
    char *name, void *data, u_int nelements,
    int (*prop_create)(prop_handle_t *, void *data, u_int nelements))
{
	return (ddi_prop_update_common(match_dev, dip, DDI_PROP_TYPE_COMPOSITE,
	    name, data, nelements, prop_create));
}

/*
 * Get a single integer property and return it.  If the property does not
 * exists, or cannot be decoded, then return the defvalue passed in.
 *
 * This routine always succedes.
 */
int
ddi_prop_get_int(dev_t match_dev, dev_info_t *dip, u_int flags,
    char *name, int defvalue)
{
	int	data;
	u_int	nelements;

	ASSERT((flags & ~(DDI_PROP_DONTPASS|DDI_PROP_NOTPROM)) == 0);

	if (ddi_prop_lookup_common(match_dev, dip,
	    (flags | DDI_PROP_TYPE_INT), name, &data, &nelements,
	    ddi_prop_fm_decode_int) != DDI_PROP_SUCCESS) {
		data = defvalue;
	}

	return (data);
}

/*
 * Get an array of integer property
 */
int
ddi_prop_lookup_int_array(dev_t match_dev, dev_info_t *dip, u_int flags,
    char *name, int **data, u_int *nelements)
{
	ASSERT((flags & ~(DDI_PROP_DONTPASS|DDI_PROP_NOTPROM)) == 0);

	return (ddi_prop_lookup_common(match_dev, dip,
	    (flags | DDI_PROP_TYPE_INT), name, data,
	    nelements, ddi_prop_fm_decode_ints));
}

/*
 * Update a single integer property.  If the propery exists on the drivers
 * property list it updates, else it creates it.
 */
int
ddi_prop_update_int(dev_t match_dev, dev_info_t *dip,
    char *name, int data)
{
	return (ddi_prop_update_common(match_dev, dip, DDI_PROP_TYPE_INT,
	    name, &data, 1, ddi_prop_fm_encode_ints));
}

/*
 * Update an array of integer property.  If the propery exists on the drivers
 * property list it updates, else it creates it.
 */
int
ddi_prop_update_int_array(dev_t match_dev, dev_info_t *dip,
    char *name, int *data, u_int nelements)
{
	return (ddi_prop_update_common(match_dev, dip, DDI_PROP_TYPE_INT,
	    name, data, nelements, ddi_prop_fm_encode_ints));
}

/*
 * Get a single string property.
 */
int
ddi_prop_lookup_string(dev_t match_dev, dev_info_t *dip, u_int flags,
    char *name, char **data)
{
	u_int x;

	ASSERT((flags & ~(DDI_PROP_DONTPASS|DDI_PROP_NOTPROM)) == 0);

	return (ddi_prop_lookup_common(match_dev, dip,
	    (flags | DDI_PROP_TYPE_STRING), name, data,
	    &x, ddi_prop_fm_decode_string));
}

/*
 * Get an array of strings property.
 */
int
ddi_prop_lookup_string_array(dev_t match_dev, dev_info_t *dip, u_int flags,
    char *name, char ***data, u_int *nelements)
{
	ASSERT((flags & ~(DDI_PROP_DONTPASS|DDI_PROP_NOTPROM)) == 0);

	return (ddi_prop_lookup_common(match_dev, dip,
	    (flags | DDI_PROP_TYPE_STRING), name, data,
	    nelements, ddi_prop_fm_decode_strings));
}

/*
 * Update a single string property.
 */
int
ddi_prop_update_string(dev_t match_dev, dev_info_t *dip,
    char *name, char *data)
{
	return (ddi_prop_update_common(match_dev, dip,
	    DDI_PROP_TYPE_STRING, name, &data, 1,
	    ddi_prop_fm_encode_string));
}

/*
 * Update an array of strings property.
 */
int
ddi_prop_update_string_array(dev_t match_dev, dev_info_t *dip,
    char *name, char **data, u_int nelements)
{
	return (ddi_prop_update_common(match_dev, dip,
	    DDI_PROP_TYPE_STRING, name, data, nelements,
	    ddi_prop_fm_encode_strings));
}

/*
 * Get an array of bytes property.
 */
int
ddi_prop_lookup_byte_array(dev_t match_dev, dev_info_t *dip, u_int flags,
    char *name, u_char **data, u_int *nelements)
{
	ASSERT((flags & ~(DDI_PROP_DONTPASS|DDI_PROP_NOTPROM)) == 0);

	return (ddi_prop_lookup_common(match_dev, dip,
	    (flags | DDI_PROP_TYPE_BYTE), name, data,
	    nelements, ddi_prop_fm_decode_bytes));
}

/*
 * Update an array of bytes property.
 */
int
ddi_prop_update_byte_array(dev_t match_dev, dev_info_t *dip,
    char *name, u_char *data, u_int nelements)
{
	if (nelements == 0)
		return (DDI_PROP_INVAL_ARG);

	return (ddi_prop_update_common(match_dev, dip, DDI_PROP_TYPE_BYTE,
	    name, data, nelements, ddi_prop_fm_encode_bytes));
}


/*
 * ddi_prop_remove_common:	Undefine a managed property:
 *			Input dev_t must match dev_t when defined.
 *			Returns DDI_PROP_NOT_FOUND, possibly.
 *			DDI_PROP_INVAL_ARG is also possible if dev is
 *			DDI_DEV_T_ANY or incoming name is the NULL string.
 */

static int
ddi_prop_remove_common(dev_t dev, dev_info_t *dip, char *name, int flag)
{
	ddi_prop_t	**list_head = &(DEVI(dip)->devi_drv_prop_ptr);
	ddi_prop_t	*propp;
	ddi_prop_t	*lastpropp = NULL;

	if ((dev == DDI_DEV_T_ANY) || (name == (char *)0) ||
	    (strlen(name) == 0)) {
		return (DDI_PROP_INVAL_ARG);
	}

	if (flag & DDI_PROP_SYSTEM_DEF)
		list_head = &(DEVI(dip)->devi_sys_prop_ptr);

	mutex_enter(&(DEVI(dip)->devi_lock));

	for (propp = *list_head; propp != NULL; propp = propp->prop_next)  {
		if ((strcmp(name, propp->prop_name) == 0) &&
		    (dev == propp->prop_dev)) {
			/*
			 * Unlink this propp allowing for it to
			 * be first in the list:
			 */

			if (lastpropp == NULL)
				*list_head = propp->prop_next;
			else
				lastpropp->prop_next = propp->prop_next;

			mutex_exit(&(DEVI(dip)->devi_lock));

			/*
			 * Free memory and return...
			 */
			kmem_free(propp->prop_name,
			    (size_t)(strlen(propp->prop_name) + 1));
			if (propp->prop_len != 0)
				kmem_free(propp->prop_val,
				    (size_t)(propp->prop_len));
			kmem_free(propp, sizeof (ddi_prop_t));
			return (DDI_PROP_SUCCESS);
		}
		lastpropp = propp;
	}
	mutex_exit(&(DEVI(dip)->devi_lock));
	return (DDI_PROP_NOT_FOUND);
}

int
ddi_prop_remove(dev_t dev, dev_info_t *dip, char *name)
{
	return (ddi_prop_remove_common(dev, dip, name, 0));
}

int
e_ddi_prop_remove(dev_t dev, dev_info_t *dip, char *name)
{
	return (ddi_prop_remove_common(dev, dip, name, DDI_PROP_SYSTEM_DEF));
}

/*
 * ddi_prop_remove_all_common:		Used before unloading a driver to remove
 *				all properties. (undefines all dev_t's props.)
 *				Also removes `explicitly undefined' props.
 *
 *				No errors possible.
 */

static void
ddi_prop_remove_all_common(dev_info_t *dip, int flag)
{
	ddi_prop_t	**list_head = &(DEVI(dip)->devi_drv_prop_ptr);
	ddi_prop_t	*propp;
	ddi_prop_t	*freep;

	if (flag & DDI_PROP_SYSTEM_DEF)
		list_head = &(DEVI(dip)->devi_sys_prop_ptr);

	mutex_enter(&(DEVI(dip)->devi_lock));
	propp = *list_head;

	while (propp != NULL)  {
		freep = propp;
		propp = propp->prop_next;
		kmem_free(freep->prop_name,
		    (size_t)(strlen(freep->prop_name) + 1));
		if (freep->prop_len != 0)
			kmem_free(freep->prop_val,
			    (size_t)(freep->prop_len));
		kmem_free(freep, sizeof (ddi_prop_t));
	}

	*list_head = NULL;
	mutex_exit(&(DEVI(dip)->devi_lock));
}


/*
 * ddi_prop_remove_all:		Remove all driver prop definitions.
 */

void
ddi_prop_remove_all(dev_info_t *dip)
{
	ddi_prop_remove_all_common(dip, 0);
}

/*
 * e_ddi_prop_remove_all:	Remove all system prop definitions.
 */

void
e_ddi_prop_remove_all(dev_info_t *dip)
{
	ddi_prop_remove_all_common(dip, (int)DDI_PROP_SYSTEM_DEF);
}

/*
 * ddi_prop_undefine:	Explicitly undefine a property.  Property
 *			searches which match this property return
 *			the error code DDI_PROP_UNDEFINED.
 *
 *			Use ddi_prop_remove to negate effect of
 *			ddi_prop_undefine
 *
 *			See above for error returns.
 */

int
ddi_prop_undefine(dev_t dev, dev_info_t *dip, int flag, char *name)
{
	return (ddi_prop_update_common(dev, dip,
	    (flag | DDI_PROP_STACK_CREATE | DDI_PROP_UNDEF_IT |
		DDI_PROP_TYPE_BYTE),
	    name, NULL, 0, ddi_prop_fm_encode_bytes));
}

int
e_ddi_prop_undefine(dev_t dev, dev_info_t *dip, int flag, char *name)
{
	return (ddi_prop_update_common(dev, dip,
	    (flag | DDI_PROP_SYSTEM_DEF | DDI_PROP_STACK_CREATE |
		DDI_PROP_UNDEF_IT | DDI_PROP_TYPE_BYTE),
	    name, NULL, 0, ddi_prop_fm_encode_bytes));
}

/*
 * The ddi_bus_prop_op default bus nexus prop op function.
 *
 * The implementation of this routine is in ddi_impl.c
 *
 * Code to search hardware layer (PROM), if it exists,
 * on behalf of child, then, if appropriate, ascend and check
 * my own software defined properties...
 *
 * if input dip != child_dip, then call is on behalf of child
 * to search PROM, do it via ddi_bus_prop_op and ascend only
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
 */

int
ddi_bus_prop_op(dev_t dev, dev_info_t *dip, dev_info_t *ch_dip,
    ddi_prop_op_t prop_op, int mod_flags,
    char *name, caddr_t valuep, int *lengthp)
{
	int	error;

	error = impl_ddi_bus_prop_op(dev, dip, ch_dip, prop_op, mod_flags,
				    name, valuep, lengthp);

	if (error == DDI_PROP_SUCCESS || error == DDI_PROP_FOUND_1275)
		return (error);

	if (error == DDI_PROP_NO_MEMORY) {
		cmn_err(CE_CONT, prop_no_mem_msg, name);
		return (DDI_PROP_NO_MEMORY);
	}

	/*
	 * Check the 'options' node as a last resort
	 */
	if ((mod_flags & DDI_PROP_DONTPASS) != 0)
		return (DDI_PROP_NOT_FOUND);

	if (ch_dip == ddi_root_node())  {
		static char *options = "options";
		static dev_info_t *options_dip;
		/*
		 * As a last resort, when we've reached
		 * the top and still haven't found the
		 * property, find the 'options' node
		 * (if it exists) and see if the desired
		 * property is attached to it.
		 *
		 * Load the driver if we haven't already done it,
		 * and we know it is not unloadable.
		 */
		if ((options_dip == NULL) &&
		    (ddi_install_driver(options) == DDI_SUCCESS))  {
			options_dip = ddi_find_devinfo(options, 0, 0);
		}
		ASSERT(options_dip != NULL);
		/*
		 * Force the "don't pass" flag to *just* see
		 * what the options node has to offer.
		 */
		return (ddi_prop_search_common(dev, options_dip, prop_op,
		    mod_flags|DDI_PROP_DONTPASS, name, valuep,
		    (u_int *)lengthp));
	}

	/*
	 * Otherwise, continue search with parent's s/w defined properties...
	 * NOTE: Using `dip' in following call increments the level.
	 */

	return (ddi_prop_search_common(dev, dip, prop_op, mod_flags,
	    name, valuep, (u_int *)lengthp));
}

/*
 * External property functions used by other parts of the kernel...
 */

/*
 * e_ddi_getlongprop: See comments for ddi_get_longprop.
 *			dev to dip conversion performed by driver.
 */

int
e_ddi_getlongprop(dev_t dev, vtype_t type, char *name, int flags,
    caddr_t valuep, int *lengthp)
{
	dev_info_t *devi;
	ddi_prop_op_t prop_op = PROP_LEN_AND_VAL_ALLOC;
	register int error;

	if ((devi = e_ddi_get_dev_info(dev, type)) == NULL)
		return (DDI_PROP_NOT_FOUND);	/* XXX */
	error = cdev_prop_op(dev, devi, prop_op, flags, name, valuep, lengthp);
	ddi_rele_driver(getmajor(dev));
	return (error);
}

/*
 * e_ddi_getlongprop_buf:	See comments for ddi_getlongprop_buf.
 *				dev to dip conversion done by driver.
 */

int
e_ddi_getlongprop_buf(dev_t dev, vtype_t type, char *name, int flags,
    caddr_t valuep, int *lengthp)
{
	dev_info_t *devi;
	ddi_prop_op_t prop_op = PROP_LEN_AND_VAL_BUF;
	register int error;

	if ((devi = e_ddi_get_dev_info(dev, type)) == NULL)
		return (DDI_PROP_NOT_FOUND);	/* XXX */
	error = cdev_prop_op(dev, devi, prop_op, flags, name, valuep, lengthp);
	ddi_rele_driver(getmajor(dev));
	return (error);
}

/*
 * e_ddi_getprop:	See comments for ddi_getprop.
 *			dev to dip conversion done by driver.
 */

int
e_ddi_getprop(dev_t dev, vtype_t type, char *name, int flags, int defvalue)
{
	dev_info_t *devi;
	ddi_prop_op_t prop_op = PROP_LEN_AND_VAL_BUF;
	int	propvalue = defvalue;
	int	proplength = sizeof (int);
	int	error;

	if ((devi = e_ddi_get_dev_info(dev, type)) == NULL)
		return (defvalue);
	error = cdev_prop_op(dev, devi, prop_op,
	    flags, name, (caddr_t)&propvalue, &proplength);
	ddi_rele_driver(getmajor(dev));

	if ((error == DDI_PROP_SUCCESS) && (proplength == 0))
		propvalue = 1;

	return (propvalue);
}


/*
 * e_ddi_getproplen:	See comments for ddi_getproplen.
 *			dev to dip conversion done by driver.
 */
int
e_ddi_getproplen(dev_t dev, vtype_t type, char *name, int flags, int *lengthp)
{
	dev_info_t *devi;
	ddi_prop_op_t prop_op = PROP_LEN;
	register int error;

	if ((devi = e_ddi_get_dev_info(dev, type)) == NULL)
		return (DDI_PROP_NOT_FOUND);	/* XXX */
	error = cdev_prop_op(dev, devi, prop_op, flags, name, NULL, lengthp);
	ddi_rele_driver(getmajor(dev));
	return (error);
}


/*
 * e_ddi_get_dev_info:	Call driver's devo_getinfo entry point if defined.
 *
 * NOTE: The dev_get_dev_info() routine returns with a hold on the
 * devops for the underlying driver.  The caller should ensure that
 * they get decremented again (once the object is freeable!)
 */

static char *bad_dev =
	"e_ddi_get_dev_info: Illegal major device number <%d>";

dev_info_t *
e_ddi_get_dev_info(dev_t dev, vtype_t type)
{
	register dev_info_t *dip = (dev_info_t *)0;

	switch (type) {
	case VCHR:
	case VBLK:
		if (getmajor(dev) >= devcnt)  {
			cmn_err(CE_CONT, bad_dev, getmajor(dev));
		} else {
			dip = dev_get_dev_info(dev, OTYP_CHR);
		}
		break;

	default:
		break;
	}

	return (dip);
}

/*
 * Functions for the manipulation of dev_info structures
 */

/*
 * The implementation of ddi_walk_devs().
 *
 * It is very important that the the function 'f' not remove the node passed
 * into it.
 */
int
i_ddi_walk_devs(dev_info_t *dev, int (*f)(dev_info_t *, void *), void *arg)
{
	register dev_info_t *lw = dev;

	while (lw != (dev_info_t *)NULL) {

		switch ((*f)(lw, arg)) {
		case DDI_WALK_TERMINATE:
			/*
			 * Caller is done!  Just return.
			 */
			return (DDI_WALK_TERMINATE);
			/*NOTREACHED*/

		case DDI_WALK_PRUNESIB:
			/*
			 * Caller has told us not to continue with our siblings.
			 * If we have children, then set lw to point the them
			 * and start over.  Else we are done.
			 */
			if (DEVI(lw)->devi_child == NULL) {
				return (DDI_WALK_CONTINUE);
			}
			lw = (dev_info_t *)DEVI(lw)->devi_child;
			break;

		case DDI_WALK_PRUNECHILD:
			/*
			 * Caller has told us that we don't need to go down to
			 * our child.  So we can move onto the next node
			 * (sibling) without having to use recursion.  There
			 * is no need to come back to this node ever.
			 */
			lw = (dev_info_t *)DEVI(lw)->devi_sibling;
			break;

		case DDI_WALK_CONTINUE:
		default:
			/*
			 * If we have a child node, we need to stop, and use
			 * recursion to continue with our sibling nodes.  When
			 * all sibling nodes and their children are done, then
			 * we can do our child node.
			 */
			if (DEVI(lw)->devi_child != NULL) {
				if (i_ddi_walk_devs(
				    (dev_info_t *)DEVI(lw)->devi_sibling,
				    f, arg) == DDI_WALK_TERMINATE) {
					return (DDI_WALK_TERMINATE);
				}

				/*
				 * Set lw to our child node and start over.
				 */
				lw = (dev_info_t *)DEVI(lw)->devi_child;
				break;
			}
			/*
			 * else we can move onto the next node (sibling)
			 * without having to use recursion.  This is because
			 * there is no child node so we don't have to come
			 * back to this node.  We are done with it forever.
			 */
			lw = (dev_info_t *)DEVI(lw)->devi_sibling;
			break;
		}

	}

	return (DDI_WALK_CONTINUE);
}

/*
 * This general-purpose routine traverses the tree of dev_info nodes,
 * starting from the given node, and calls the given function for each
 * node that it finds with the current node and the pointer arg (which
 * can point to a structure of information that the function
 * needs) as arguments.
 *
 * It does the walk a layer at a time, not depth-first.
 *
 * The given function must return one of the values defined above and must
 * not remove the dev_info_t passed into it..
 *
 */

void
ddi_walk_devs(dev_info_t *dev, int (*f)(dev_info_t *, void *), void *arg)
{
	(void) i_ddi_walk_devs(dev, f, arg);
}

/*
 * Routines to get at elements of the dev_info structure
 */

/*
 * ddi_get_name: Return the name of the devinfo node
 */
char *
ddi_get_name(dev_info_t *dip)
{
	return (DEVI(dip)->devi_name);
}

/*
 * ddi_get_nodeid:	Get nodeid stored in dev_info structure.
 */
int
ddi_get_nodeid(dev_info_t *dip)
{
	return (DEVI(dip)->devi_nodeid);
}

int
ddi_get_instance(dev_info_t *dip)
{
	return (DEVI(dip)->devi_instance);
}

struct dev_ops *
ddi_get_driver(dev_info_t *dip)
{
	return (DEVI(dip)->devi_ops);
}

void
ddi_set_driver(dev_info_t *dip, struct dev_ops *devo)
{
	DEVI(dip)->devi_ops = devo;
}

/*
 * ddi_set_driver_private/ddi_get_driver_private:
 * Get/set device driver private data in devinfo.
 */
void
ddi_set_driver_private(dev_info_t *dip, caddr_t data)
{
	DEVI(dip)->devi_driver_data = data;
}

caddr_t
ddi_get_driver_private(dev_info_t *dip)
{
	return ((caddr_t)DEVI(dip)->devi_driver_data);
}

/*
 * This is a dual purpose function.  If the device is power managed, it will
 * notify the pm-driver to control it.  If the device is checkpointed, it
 * will hold the thread, until CPR signals the cv.
 */
void
ddi_dev_is_needed(dev_info_t *dip, int pm_cmpt, int pm_level)
{
	void		(*fn)(dev_info_t *, int, int);
	int		length = sizeof (fn);

	if (ddi_prop_op(DDI_DEV_T_ANY, ddi_root_node(), PROP_LEN_AND_VAL_BUF,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM, "cpr-driver", (caddr_t)&fn,
	    &length) == DDI_PROP_SUCCESS ||
	    ddi_prop_op(DDI_DEV_T_ANY, ddi_root_node(), PROP_LEN_AND_VAL_BUF,
	    DDI_PROP_DONTPASS | DDI_PROP_NOTPROM, "pm-driver", (caddr_t)&fn,
	    &length) == DDI_PROP_SUCCESS)
		(*fn)(dip, pm_cmpt, pm_level);
}

int
ddi_power(dev_info_t *dip, int pm_cmpt, int pm_level)
{
	power_req	request;

	request.who = dip;
	request.cmpt = pm_cmpt;
	request.level = pm_level;
	return (ddi_ctlops(dip, dip, DDI_CTLOPS_POWER, &request, NULL));
}

/*
 * ddi_get_parent, ddi_get_child, ddi_get_next_sibling
 */

dev_info_t *
ddi_get_parent(dev_info_t *dip)
{
	return ((dev_info_t *)DEVI(dip)->devi_parent);
}

dev_info_t *
ddi_get_child(dev_info_t *dip)
{
	return ((dev_info_t *)DEVI(dip)->devi_child);
}

dev_info_t *
ddi_get_next_sibling(dev_info_t *dip)
{
	return ((dev_info_t *)DEVI(dip)->devi_sibling);
}

dev_info_t *
ddi_get_next(dev_info_t *dip)
{
	return ((dev_info_t *)DEVI(dip)->devi_next);
}

void
ddi_set_next(dev_info_t *dip, dev_info_t *nextdip)
{
	DEVI(dip)->devi_next = DEVI(nextdip);
}

/*
 * ddi_add_child:	Add a child dev_info to the specified parent.
 *			A zeroed dev_info structure is allocated
 *			and added as a child of the given "dip".
 *			The new dev_info pointer is returned if
 *			successful.
 */

dev_info_t *
ddi_add_child(dev_info_t *pdip, char *name, u_int nodeid, u_int unit)
{
	return (i_ddi_add_child(pdip, name, nodeid, unit));
}

/*
 * ddi_remove_child:	Remove the given child and free the space
 *			occupied by the dev_info structure.
 *
 *			Parent and driver private data should already
 *			be released before calling this function.
 *
 *			If there are children devices to this dev, we
 *			refuse to free the device.
 */

int
ddi_remove_child(dev_info_t *dip, int lockheld)
{
	return (i_ddi_remove_child(dip, lockheld));
}


/*
 * This routine appends the second argument onto the children list for the
 * first argument. It always appends it to the end of the list.
 * It also adds the new instance to the linked list of driver instances.
 */

void
ddi_append_dev(dev_info_t *pdip, register dev_info_t *cdip)
{
	register struct dev_info *dev;
	int major;
	register struct devnames *dnp;

	rw_enter(&(devinfo_tree_lock), RW_WRITER);
	if ((dev = DEVI(pdip)->devi_child) == (struct dev_info *)NULL) {
		DEVI(pdip)->devi_child = DEVI(cdip);
	} else {
		while (dev->devi_sibling != (struct dev_info *)NULL)
			dev = dev->devi_sibling;
		dev->devi_sibling = DEVI(cdip);
	}
	DEVI(cdip)->devi_parent = DEVI(pdip);
	DEVI(cdip)->devi_bus_ctl = DEVI(pdip);	/* XXX until 1106021 is fixed */
	rw_exit(&(devinfo_tree_lock));

	/*
	 * If there is no name_to_major entry for this node, add it
	 * the orphan list, and we'll try to unorphan it if we are
	 * told to read the major device number binding file.
	 */
	if ((major = ddi_name_to_major(ddi_get_name(cdip))) == -1)
		dnp = &orphanlist;
	else
		dnp = &(devnamesp[major]);

	/*
	 * Add this instance to the linked list of instances.
	 */
	LOCK_DEV_OPS(&(dnp->dn_lock));
	if ((dev = DEVI(dnp->dn_head)) == NULL)  {
		dnp->dn_head = cdip;
		UNLOCK_DEV_OPS(&(dnp->dn_lock));
		return;
	}

	while (dev->devi_next != NULL)  {
		dev = dev->devi_next;
	}
	dev->devi_next = DEVI(cdip);

	UNLOCK_DEV_OPS(&(dnp->dn_lock));
}

/*
 * Add this list of devinfos to the end of the orphan list.
 * Not an external ddi function!
 */

void
ddi_orphan_devs(register dev_info_t *dip)
{
	register dev_info_t *odip, *ndip;
	register struct devnames *dnp = &orphanlist;

	LOCK_DEV_OPS(&(dnp->dn_lock));
	if ((odip = dnp->dn_head) == NULL)
		dnp->dn_head = dip;
	else {
		while ((ndip = (dev_info_t *)DEVI(odip)->devi_next) != NULL)
			odip = ndip;
		DEVI(odip)->devi_next = DEVI(dip);
	}
	UNLOCK_DEV_OPS(&(dnp->dn_lock));
}

/*
 * If we re-read the major number binding file, try to
 * add any orphaned devinfo nodes to the devices instance list.
 * Not an external DDI function!
 */
void
ddi_unorphan_devs(void)
{
	register struct dev_info *dev;
	int major;
	register struct devnames *dnp;
	register dev_info_t *dip;
	dev_info_t *ndip, *pdip;

#ifdef lint
	ndip = NULL;	/* See 1094364 */
#endif
	LOCK_DEV_OPS(&(orphanlist.dn_lock));
	pdip = NULL;
	for (dip = orphanlist.dn_head; dip != NULL; dip = ndip)  {
		ndip = ddi_get_next(dip);
		if ((major = ddi_name_to_major(ddi_get_name(dip))) == -1)  {
			pdip = dip;
			continue;
		}

		/* Unlink dip from orphanlist. */
		if (pdip == NULL)
			orphanlist.dn_head = (dev_info_t *)DEVI(dip)->devi_next;
		else
			DEVI(pdip)->devi_next = DEVI(ndip);
		DEVI(dip)->devi_next = NULL;

		/* Add it to major's instance list. */
		dnp = &devnamesp[major];
		LOCK_DEV_OPS(&(dnp->dn_lock));
		if ((dev = DEVI(dnp->dn_head)) == NULL)
			dnp->dn_head = dip;
		else {
			while (dev->devi_next != NULL)
				dev = dev->devi_next;
			dev->devi_next = DEVI(dip);
		}
		UNLOCK_DEV_OPS(&(dnp->dn_lock));
	}
	UNLOCK_DEV_OPS(&(orphanlist.dn_lock));
}

/*
 * ddi_load_driver(name)
 *
 * Load a device driver.
 *
 */

ddi_load_driver(char *name)
{
	struct par_list *pl;
	major_t major;

	if ((modloadonly("drv", name) == -1) ||
	    ((major = ddi_name_to_major(name)) == -1))
		return (DDI_FAILURE);

	pl = impl_make_parlist(major);
	LOCK_DEV_OPS(&(devnamesp[major].dn_lock));
	devnamesp[major].dn_pl = pl;
	UNLOCK_DEV_OPS(&(devnamesp[major].dn_lock));
	return (DDI_SUCCESS);
}

struct dev_ops *
ddi_hold_installed_driver(major_t major)
{
	register struct devnames *dnp;
	register int circular;
	register struct dev_ops *ops;

	/*
	 * Check to see if the driver is already there .. it usually
	 * is, so just increment the refcnt and return.
	 */
	dnp = &(devnamesp[major]);
	LOCK_DEV_OPS(&dnp->dn_lock);
	if ((dnp->dn_flags & DN_WALKED_TREE) &&
	    !DN_BUSY_CHANGING(dnp->dn_flags)) {
		ops = devopsp[major];
		INCR_DEV_OPS_REF(ops);
		UNLOCK_DEV_OPS(&dnp->dn_lock);
		return (ops);
		/*NOTREACHED*/
	}
	UNLOCK_DEV_OPS(&dnp->dn_lock);

	/*
	 * Load the driver; if this fails there is nothing left to do.
	 */
	if ((ops = mod_hold_dev_by_major(major)) == NULL)
		return (NULL);

	/*
	 * Wait for or get busy/changing.
	 * Handle circular dependencies. (sbus-xbox-sbus)
	 */
	LOCK_DEV_OPS(&(dnp->dn_lock));

	/*
	 * Is this thread already installing this driver?
	 * If yes, mark it as a circular dependency and continue.
	 * If not, wait for other threads to finish with this driver.
	 */
	if (DN_BUSY_CHANGING(dnp->dn_flags) &&
	    (dnp->dn_busy_thread == curthread))  {
		dnp->dn_circular++;
	} else {
		while (DN_BUSY_CHANGING(dnp->dn_flags))
			cv_wait(&(dnp->dn_wait), &(dnp->dn_lock));
		dnp->dn_flags |= DN_BUSY_LOADING;
		dnp->dn_busy_thread = curthread;
	}
	circular = dnp->dn_circular;
	UNLOCK_DEV_OPS(&(dnp->dn_lock));

	/*
	 * If dev_info nodes haven't been made from the hwconf file
	 * for this driver, make them.  If we are in a circular dependency,
	 * and this driver is a nexus driver, skip this to avoid
	 * infinite recursion -- non-sid nexus cycles are not supported.
	 * (a -> b -> a where `a' is a nexus is not supported unless the
	 * nodes are self-identifying.
	 */
	if ((!((circular) && NEXUS_DRV(ops))) &&
	    (!(dnp->dn_flags & DN_DEVI_MADE))) {
		(void) impl_make_devinfos(major);
	}

	/*
	 * Now we try to attach the driver to hw devinfos (if any)
	 * and check to make sure it was attached somewhere.
	 * If it wasn't attached anywhere, then we can toss it.
	 * We make sure we only go through this code once, on behalf
	 * of the first call into this function.
	 */
	if ((!circular) && (!(dnp->dn_flags & DN_WALKED_TREE))) {
		attach_driver_to_hw_nodes(major, ops);
		LOCK_DEV_OPS(&(dnp->dn_lock));
		dnp->dn_flags |= DN_WALKED_TREE;
		UNLOCK_DEV_OPS(&(dnp->dn_lock));
		if (!(dnp->dn_flags & DN_DEVS_ATTACHED)) {
			ddi_rele_driver(major);
			ops = NULL;
			LOCK_DEV_OPS(&(dnp->dn_lock));
			dnp->dn_flags = DN_BUSY_UNLOADING;
			UNLOCK_DEV_OPS(&(dnp->dn_lock));
			(void) mod_remove_by_name(ddi_major_to_name(major));
			cmn_err(CE_CONT,
			    "?Unable to install/attach driver '%s'\n",
			    ddi_major_to_name(major));
		}
	}

	LOCK_DEV_OPS(&(dnp->dn_lock));
	if (circular)
		dnp->dn_circular--;
	else  {
		dnp->dn_flags &= ~(DN_BUSY_CHANGING_BITS);
		dnp->dn_busy_thread = NULL;
		cv_broadcast(&(dnp->dn_wait));
	}
	UNLOCK_DEV_OPS(&(dnp->dn_lock));
	return (ops);
}

void
ddi_rele_driver(major_t maj)
{
	mod_rele_dev_by_major(maj);
}

void
ddi_rele_devi(dev_info_t *devi)
{
	extern void mod_rele_dev_by_devi(dev_info_t *);

	mod_rele_dev_by_devi(devi);
}

/*
 * ddi_install_driver(name)
 *
 * Driver installation is currently a byproduct of driver loading.  This
 * may change.
 */

int
ddi_install_driver(char *name)
{
	register major_t major;

	if (((major = ddi_name_to_major(name)) == -1) ||
	    (ddi_hold_installed_driver(major) == NULL))
		return (DDI_FAILURE);
	ddi_rele_driver(major);
	return (DDI_SUCCESS);
}

/*
 * ddi_root_node:		Return root node of devinfo tree
 */

dev_info_t *
ddi_root_node(void)
{
	return (top_devinfo);
}

/*
 * ddi_find_devinfo- these routines look for a specifically named device
 */

static dev_info_t *
ddi_find_devinfo_search(dev_info_t *dip, char *name, int unit, int need_drv)
{
	dev_info_t *rdip;

	while (dip) {
		if ((DEVI(dip)->devi_name &&
		    strcmp(DEVI(dip)->devi_name, name) == 0) &&
		    (unit == -1 || DEVI(dip)->devi_instance == unit) &&
		    (need_drv == 0 || DEVI(dip)->devi_ops)) {
			return (dip);
		}
		if (DEVI(dip)->devi_child) {
			rdip = ddi_find_devinfo_search((dev_info_t *)
			    DEVI(dip)->devi_child, name, unit, need_drv);
			if (rdip) {
				return (rdip);
			}
		}
		dip = (dev_info_t *)DEVI(dip)->devi_sibling;
	}
	return (dip);
}

dev_info_t *
ddi_find_devinfo(char *name, int unit, int need_drv)
{
	dev_info_t *dip = ddi_root_node();

	if (dip != NULL) {
		rw_enter(&(devinfo_tree_lock), RW_READER);
		dip = ddi_find_devinfo_search(dip, name, unit, need_drv);
		rw_exit(&(devinfo_tree_lock));
	}
	return (dip);
}

/*
 * Miscellaneous functions:
 */

/*
 * Implementation specific hooks
 */

void
ddi_report_dev(dev_info_t *d)
{
	register char *b;

	(void) ddi_ctlops(d, d, DDI_CTLOPS_REPORTDEV, (void *)0, (void *)0);

	/*
	 * If this devinfo node has cb_ops, it's implicitly accessible from
	 * userland, so we print its full name together with the instance
	 * number 'abbreviation' that the driver may use internally.
	 */
	if (DEVI(d)->devi_ops->devo_cb_ops != (struct cb_ops *)0 &&
	    (b = kmem_zalloc(MAXPATHLEN, KM_NOSLEEP))) {
		cmn_err(CE_CONT, "?%s%d is %s\n",
		    DEVI(d)->devi_name, DEVI(d)->devi_instance,
		    ddi_pathname(d, b));
		kmem_free(b, MAXPATHLEN);
	}
}

int
ddi_dev_regsize(dev_info_t *dev, u_int rnumber, off_t *result)
{
	return (ddi_ctlops(dev, dev, DDI_CTLOPS_REGSIZE,
	    (void *)&rnumber, (void *)result));
}

int
ddi_dev_nregs(dev_info_t *dev, int *result)
{
	return (ddi_ctlops(dev, dev, DDI_CTLOPS_NREGS, 0, (void *)result));
}

int
ddi_dev_nintrs(dev_info_t *dev, int *result)
{
	return (ddi_ctlops(dev, dev, DDI_CTLOPS_NINTRS,
	    (void *)0, (void *)result));
}

int
ddi_dev_is_sid(dev_info_t *d)
{
	return (ddi_ctlops(d, d, DDI_CTLOPS_SIDDEV, (void *)0, (void *)0));
}

int
ddi_slaveonly(dev_info_t *d)
{
	return (ddi_ctlops(d, d, DDI_CTLOPS_SLAVEONLY, (void *)0, (void *)0));
}

int
ddi_dev_affinity(dev_info_t *a, dev_info_t *b)
{
	return (ddi_ctlops(a, a, DDI_CTLOPS_AFFINITY, (void *)b, (void *)0));
}

/*
 * callback free list
 */

static int ncallbacks;
static int nc_low = 170;
static int nc_med = 512;
static int nc_high = 2048;
static struct ddi_callback *callbackq;
static struct ddi_callback *callbackqfree;

/*
 * set/run callback lists
 */

struct {
	u_int nc_asked;
	u_int nc_new;
	u_int nc_run;
	u_int nc_delete;
	u_int nc_maxreq;
	u_int nc_maxlist;
	u_int nc_alloc;
	u_int nc_runouts;
	u_int nc_L2;
} cbstats;

static kmutex_t ddi_callback_mutex;
static struct kmem_cache *ddi_callback_cache;

/*
 * callbacks are handled using a L1/L2 cache. The L1 cache
 * comes out of kmem_cache_alloc and can expand/shrink dynamically. If
 * we can't get callbacks from the L1 cache [because pageout is doing
 * I/O at the time freemem is 0], we allocate callbacks out of the
 * L2 cache. The L2 cache is static and depends on the memory size.
 * [We might also count the number of devices at probe time and
 * allocate one structure per device and adjust for deferred attach]
 */
void
impl_ddi_callback_init(void)
{
	int i;
	u_int physmegs;

	mutex_init(&ddi_callback_mutex, "ddi_callback_mutex",
		MUTEX_DEFAULT, NULL);
	ddi_callback_cache = kmem_cache_create("ddi_callback_cache",
		sizeof (struct ddi_callback), 0, NULL, NULL, NULL);

	physmegs = physmem >> (20 - PAGESHIFT);
	if (physmegs < 48) {
		ncallbacks = nc_low;
	} else if (physmegs < 128) {
		ncallbacks = nc_med;
	} else {
		ncallbacks = nc_high;
	}

	/*
	 * init free list
	 */
	callbackq = kmem_zalloc(
			ncallbacks * sizeof (struct ddi_callback), KM_SLEEP);
	for (i = 0; i < ncallbacks-1; i++)
		callbackq[i].c_nfree = &callbackq[i+1];
	callbackqfree = callbackq;
}

static void
callback_insert(int (*funcp)(caddr_t), caddr_t arg, int *listid, int count,
    kmutex_t *mutp)
{
	struct ddi_callback *list, *marker, *new;

	list = marker = (struct ddi_callback *)*listid;
	while (list != NULL) {
		if (list->c_call == funcp && list->c_arg == arg) {
			list->c_count += count;
			return;
		}
		marker = list;
		list = list->c_nlist;
	}
	new = kmem_cache_alloc(ddi_callback_cache, KM_NOSLEEP);
	if (new == NULL) {
		new = callbackqfree;
		if (new == NULL)
			cmn_err(CE_PANIC,
				"callback_insert: no callback structures");
		callbackqfree = new->c_nfree;
		cbstats.nc_L2++;
	}
	if (marker != NULL) {
		marker->c_nlist = new;
	} else {
		*listid = (int)new;
	}
	new->c_nlist = NULL;
	new->c_call = funcp;
	new->c_arg = arg;
	new->c_mutex = mutp;
	new->c_count = count;
	cbstats.nc_new++;
	cbstats.nc_alloc++;
	if (cbstats.nc_alloc > cbstats.nc_maxlist)
		cbstats.nc_maxlist = cbstats.nc_alloc;
}

void
ddi_set_callback(int (*funcp)(), caddr_t arg, int *listid)
{
	kmutex_t *mutp;

	if (UNSAFE_DRIVER_LOCK_HELD()) {
		mutp = &unsafe_driver;
	} else {
		mutp = NULL;
	}

	mutex_enter(&ddi_callback_mutex);
	cbstats.nc_asked++;
	if ((cbstats.nc_asked - cbstats.nc_run) > cbstats.nc_maxreq)
		cbstats.nc_maxreq = (cbstats.nc_asked - cbstats.nc_run);
	(void) callback_insert(funcp, arg, listid, 1, mutp);
	mutex_exit(&ddi_callback_mutex);
}

static void
real_callback_run(caddr_t Queue)
{
	kmutex_t *mutp;
	int (*funcp)(caddr_t);
	caddr_t arg;
	int count, unsafe, rval, *listid;
	struct ddi_callback *list, *marker;
	int check_pending = 1;
	int pending = 0;

	do {
		mutex_enter(&ddi_callback_mutex);
		listid = (int *)Queue;
		list = (struct ddi_callback *)*listid;
		if (list == NULL) {
			mutex_exit(&ddi_callback_mutex);
			return;
		}
		if (check_pending) {
			marker = list;
			while (marker != NULL) {
				pending += marker->c_count;
				marker = marker->c_nlist;
			}
			check_pending = 0;
		}
		ASSERT(pending > 0);
		ASSERT(list->c_count > 0);
		funcp = list->c_call;
		arg = list->c_arg;
		mutp = list->c_mutex;
		count = list->c_count;
		*(int *)Queue = (int)list->c_nlist;
		if (list >= &callbackq[0] &&
		    list <= &callbackq[ncallbacks-1]) {
			list->c_nfree = callbackqfree;
			callbackqfree = list;
		} else {
			kmem_cache_free(ddi_callback_cache, list);
		}
		cbstats.nc_delete++;
		cbstats.nc_alloc--;
		mutex_exit(&ddi_callback_mutex);

		/*
		 * If we enter with the unsafe_driver lock held, it is
		 * 'perfectly' okay to release it since we will be reacquiring
		 * the "correct" mutex as needed as we re-traverse the
		 * list in the loop below.
		 */
		if ((unsafe = UNSAFE_DRIVER_LOCK_HELD()) != 0)
			mutex_exit(&unsafe_driver);
		if (mutp)
			mutex_enter(mutp);

		do {
			if ((rval = (*funcp)(arg)) == 0) {
				delay(HZ >> 4);
				pending -= count;
				mutex_enter(&ddi_callback_mutex);
				(void) callback_insert(funcp, arg, listid,
					count, mutp);
				cbstats.nc_runouts++;
			} else {
				pending--;
				mutex_enter(&ddi_callback_mutex);
				cbstats.nc_run++;
			}
			mutex_exit(&ddi_callback_mutex);
		} while ((rval != 0) && (--count > 0));

		if (mutp)
			mutex_exit(mutp);
		if (unsafe)
			mutex_enter(&unsafe_driver);
	} while (pending > 0);
}

void
ddi_run_callback(int *listid)
{
	softcall(real_callback_run, (caddr_t)listid);
}

/*ARGSUSED*/
dev_info_t *
nodevinfo(dev_t dev, int otyp)
{
	return ((dev_info_t *)0);
}

/*ARGSUSED*/
int
ddi_no_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	return (DDI_FAILURE);
}

/*ARGSUSED*/
int
ddifail(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	return (DDI_FAILURE);
}

/*ARGSUSED*/
int
ddi_no_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareqp, ddi_dma_handle_t *handlep)
{
	return (DDI_DMA_NOMAPPING);
}

/*ARGSUSED*/
int
ddi_no_dma_allochdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_attr_t *attr,
    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep)
{
	return (DDI_DMA_BADATTR);
}

/*ARGSUSED*/
int
ddi_no_dma_freehdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	return (DDI_FAILURE);
}

/*ARGSUSED*/
int
ddi_no_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
    ddi_dma_cookie_t *cp, u_int *ccountp)
{
	return (DDI_FAILURE);
}

/*ARGSUSED*/
int
ddi_no_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	return (DDI_FAILURE);
}

/*ARGSUSED*/
int
ddi_no_dma_flush(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, off_t off, u_int len,
    u_int cache_flags)
{
	return (DDI_FAILURE);
}

/*ARGSUSED*/
int
ddi_no_dma_win(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, uint_t win, off_t *offp,
    uint_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	return (DDI_FAILURE);
}


/*ARGSUSED*/
int
ddi_no_dma_mctl(register dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, u_int *lenp, caddr_t *objp, u_int flags)
{
	return (DDI_FAILURE);
}

void
ddivoid()
{
}

/*ARGSUSED*/
int
nochpoll(dev_t dev, short events, int anyyet, short *reventsp,
    struct pollhead **pollhdrp)
{
	return (ENXIO);
}

cred_t *
ddi_get_cred(void)
{
	return (CRED());
}


/*
 * XXX	Is this needed?
 *	If yes, does this really belong here?
 *	If no, remember to update/remove prototype in sys/sunddi.h
 *	and destroy manual page..
 */
/*
 * Swap bytes in 16-bit [half-]words
 */
void
swab(void *src, void *dst, size_t nbytes)
{
	register u_char *pf = (u_char *)src;
	register u_char *pt = (u_char *)dst;
	register u_char tmp;
	register int nshorts;

	nshorts = (nbytes+1) >> 1;

	while (--nshorts >= 0) {
		tmp = *pf++;
		*pt++ = *pf++;
		*pt++ = tmp;
	}
}

static void
ddi_append_minor_node(dev_info_t *ddip, struct ddi_minor_data *dmdp)
{
	register struct ddi_minor_data *dp;

	mutex_enter(&(DEVI(ddip)->devi_lock));
	if ((dp = DEVI(ddip)->devi_minor) == (struct ddi_minor_data *)NULL) {
		DEVI(ddip)->devi_minor = dmdp;
	} else {
		while (dp->next != (struct ddi_minor_data *)NULL)
			dp = dp->next;
		dp->next = dmdp;
	}
	mutex_exit(&(DEVI(ddip)->devi_lock));
}

/*
 * ddi_creat_minor_node:	Create a  ddi_minor_data structure and
 *				attach it to the given devinfo node.
 */
static char *warning = "Cannot create minor node for <%s> <%s> <%d>\n";

int
ddi_create_minor_common(dev_info_t *dip, char *name, int spec_type,
	int minor_num, char *node_type, int is_clone, ddi_minor_type mtype)
{
	struct ddi_minor_data *dmdp, *dmdap;
	major_t major;
	dev_info_t *ddip;
	static dev_info_t *clone_dip;
	static major_t clone_major;

	/*
	 * We don't expect the driver to know it's major number.
	 * So we look through the table of major to name mappings.
	 */

	if (spec_type != S_IFCHR && spec_type != S_IFBLK) {
		return (DDI_FAILURE);
	}
	if (name == NULL)
		return (DDI_FAILURE);
	if ((major = ddi_name_to_major(DEVI(dip)->devi_name)) == -1)  {
		cmn_err(CE_WARN, warning, ddi_get_name(dip), name, minor_num);
		cmn_err(CE_CONT, "Can't find major dev number for <%s>\n",
		    ddi_get_name(dip));
		return (DDI_FAILURE);	/* Not found so error */
	}
	/*
	 * If the driver is making a clone minor device then we find the
	 * clone driver, cache it's devinfo node in clone_dip and use that
	 * as the devinfo node for the minor device.  The major we found
	 * above becomes the minor and we find the major number of the clone
	 * device.
	 */

	if (is_clone) {
		if (clone_major == NULL) {
			clone_major = ddi_name_to_major("clone");
			if (clone_major == (dev_t)-1)  {
				cmn_err(CE_WARN, warning, ddi_get_name(dip),
				    name, minor_num);
				cmn_err(CE_CONT,
				    "Can't find clone major dev number\n");
				return (DDI_FAILURE);
			}
		}
		if ((ddip = clone_dip) == NULL) {
			(void) ddi_hold_installed_driver(clone_major);
			ddip = ddi_find_devinfo("clone", -1, 1);
			if (ddip == NULL)  {
				cmn_err(CE_WARN, warning, ddi_get_name(dip),
				    name, minor_num);
				cmn_err(CE_CONT,
				    "Can't find clone devinfo node\n");
				return (DDI_FAILURE);
			}
			clone_dip = ddip;
		} else {
			(void) ddi_hold_installed_driver(clone_major);
		}
		minor_num = major;
		major = clone_major;
	} else
		ddip = dip;
	if ((dmdp = (struct ddi_minor_data *)
	    kmem_zalloc(sizeof (struct ddi_minor_data),
	    KM_NOSLEEP)) == NULL) {
		if (is_clone)
			ddi_rele_driver(clone_major);
		return (DDI_FAILURE);
	}
	if ((dmdp->ddm_name = kmem_zalloc(strlen(name) + 1,
	    KM_NOSLEEP)) == NULL) {
		kmem_free(dmdp, sizeof (struct ddi_minor_data));
		if (is_clone)
			ddi_rele_driver(clone_major);
		return (DDI_FAILURE);
	}
	bcopy(name, dmdp->ddm_name, strlen(name));
	dmdp->dip = ddip;
	dmdp->ddm_dev = makedevice(major, minor_num);
	dmdp->ddm_spec_type = spec_type;
	dmdp->ddm_node_type = node_type;
	dmdp->type = mtype;
	ddi_append_minor_node(ddip, dmdp);
	if (is_clone) {
		dmdap = (struct ddi_minor_data *)
		    kmem_zalloc(sizeof (struct ddi_minor_data),
		    KM_NOSLEEP);
		if (dmdap == NULL) {
			ddi_remove_minor_node(ddip, name);
			ddi_rele_driver(clone_major);
			return (DDI_FAILURE);
		}
		dmdap->type = DDM_ALIAS;
		dmdap->ddm_admp = dmdp;
		dmdap->dip = dip;
		ddi_append_minor_node(dip, dmdap);
	}
	return (DDI_SUCCESS);
}

int
ddi_create_minor_node(dev_info_t *dip, char *name, int spec_type,
	int minor_num, char *node_type, int is_clone)
{
	return (ddi_create_minor_common(dip, name, spec_type, minor_num,
	    node_type, is_clone, DDM_MINOR));
}

int
ddi_create_default_minor_node(dev_info_t *dip, char *name, int spec_type,
	int minor_num, char *node_type, int is_clone)
{
	return (ddi_create_minor_common(dip, name, spec_type, minor_num,
	    node_type, is_clone, DDM_DEFAULT));
}

void
ddi_remove_minor_node(dev_info_t *dip, char *name)
{
	struct ddi_minor_data *dmdp, *dmdp1;
	struct ddi_minor_data **dmdp_prev;
	major_t major;

	mutex_enter(&(DEVI(dip)->devi_lock));
	dmdp_prev = &DEVI(dip)->devi_minor;
	dmdp = DEVI(dip)->devi_minor;
	while (dmdp != NULL) {
		dmdp1 = dmdp->next;
		if ((dmdp->type == DDM_MINOR || dmdp->type == DDM_DEFAULT) &&
		    (name == NULL || (dmdp->ddm_name != NULL &&
		    strcmp(name, dmdp->ddm_name) == 0))) {
			if (dmdp->ddm_name != NULL)
				kmem_free(dmdp->ddm_name,
				    strlen(dmdp->ddm_name) + 1);
			kmem_free(dmdp, sizeof (struct ddi_minor_data));
			*dmdp_prev = dmdp1;
			/*
			 * OK, we found it, so get out now -- if we drive on,
			 * we will strcmp against garbage.  See 1139209.
			 */
			if (name != NULL)
				break;
		} else if (dmdp->type == DDM_ALIAS &&
		    (name == NULL || ((dmdp->ddm_atype == DDM_MINOR ||
		    dmdp->ddm_atype == DDM_DEFAULT) &&
		    (dmdp->ddm_aname != NULL &&
		    strcmp(name, dmdp->ddm_aname) == 0)))) {
			major = getmajor(dmdp->ddm_adev);
			ddi_remove_minor_node(dmdp->ddm_adip, dmdp->ddm_aname);
			ddi_rele_driver(major);
			kmem_free(dmdp, sizeof (struct ddi_minor_data));
			*dmdp_prev = dmdp1;
		} else {
			dmdp_prev = &dmdp->next;
		}
		dmdp = dmdp1;
	}
	mutex_exit(&(DEVI(dip)->devi_lock));
}


int
ddi_in_panic()
{
	return (panicstr != NULL ? 1 : 0);
}


/*
 * Find first bit set in a mask (returned counting from 1 up)
 */

int
ddi_ffs(long mask)
{
	extern int ffs(long mask);
	return (ffs(mask));
}

/*
 * Find last bit set. Take mask and clear
 * all but the most significant bit, and
 * then let ffs do the rest of the work.
 *
 * Algorithm courtesy of Steve Chessin.
 */

int
ddi_fls(register long mask)
{
	extern int ffs(long);

	while (mask) {
		register long nx;

		if ((nx = (mask & (mask - 1))) == 0)
			break;
		mask = nx;
	}
	return (ffs(mask));
}

/*
 * The next five routines comprise generic storage management utilities
 * for driver soft state structures (in "the old days," this was done
 * with a statically sized array - big systems and dynamic loading
 * and unloading make heap allocation more attractive)
 */

/*
 * This data structure is entirely private to the allocator.
 */
struct i_ddi_soft_state {
	void	**array;	/* the array of pointers */
	kmutex_t lock;		/* serialize access to this struct */
	size_t	size;		/* how many bytes per state struct */
	size_t	n_items;	/* how many structs herein */
	struct i_ddi_soft_state *next;	/* 'dirty' elements */
};

/*
 * Allocate a set of pointers to 'n_items' objects of size 'size'
 * bytes.  Each pointer is initialized to nil.
 *
 * The 'size' and 'n_items' values are stashed in the opaque
 * handle returned to the caller.
 *
 * This implementation interprets 'set of pointers' to mean 'array
 * of pointers' but note that nothing in the interface definition
 * precludes an implementation that uses, for example, a linked list.
 * However there should be a small efficiency gain from using an array
 * at lookup time.
 *
 * XXX	As an optimization, we make our growable array allocations in
 *	powers of two (bytes), since that's how much kmem_alloc (currently)
 *	gives us anyway.  It should save us some free/realloc's ..
 *
 * XXX	As a further optimization, we make the growable array start out
 *	with MIN_N_ITEMS in it.
 */

#define	MIN_N_ITEMS	8	/* 8 void *'s == 32 bytes */

int
ddi_soft_state_init(void **state_p, size_t size, register size_t n_items)
{
	register struct i_ddi_soft_state *ss;

	if (state_p == NULL || *state_p != NULL || size == 0) {
		return (EINVAL);
	}

	ss = (struct i_ddi_soft_state *)kmem_zalloc(sizeof (*ss), KM_SLEEP);
	mutex_init(&ss->lock, "ddi state alloc", MUTEX_DRIVER, DEFAULT_WT);
	ss->size = size;

	if (n_items < MIN_N_ITEMS)
		ss->n_items = MIN_N_ITEMS;
	else {
		int bitlog;

		if ((bitlog = ddi_fls(n_items)) == ddi_ffs(n_items))
			bitlog--;
		ss->n_items = 1 << bitlog;
	}

	ASSERT(ss->n_items >= n_items);

	ss->array = kmem_zalloc(ss->n_items * sizeof (void *), KM_SLEEP);

	*state_p = ss;

	return (0);
}


/*
 * Allocate a state structure of size 'size' to be associated
 * with item 'item'.
 *
 * In this implementation, the array is extended to
 * allow the requested offset, if needed.
 */
int
ddi_soft_state_zalloc(register void *state, register int item)
{
	register struct i_ddi_soft_state *ss;
	register void **array;
	register void *new_element;

	if ((ss = state) == NULL || item < 0) {
		return (DDI_FAILURE);
	}

	mutex_enter(&ss->lock);
	if (ss->size == 0) {
		mutex_exit(&ss->lock);
		cmn_err(CE_WARN, "ddi_soft_state_zalloc: bad handle");
		return (DDI_FAILURE);
	}

	array = ss->array;	/* NULL if ss->n_items == 0 */
	ASSERT(ss->n_items != 0 && array != NULL);

	/*
	 * refuse to tread on an existing element
	 */
	if (item < ss->n_items && array[item] != NULL) {
		mutex_exit(&ss->lock);
		return (DDI_FAILURE);
	}

	/*
	 * Allocate a new element to plug in
	 */
	new_element = kmem_zalloc(ss->size, KM_SLEEP);

	/*
	 * Check if the array is big enough, if not, grow it.
	 */
	if (item >= ss->n_items) {
		void	**new_array;
		size_t	new_n_items;
		struct i_ddi_soft_state *dirty;

		/*
		 * Allocate a new array of the right length, copy
		 * all the old pointers to the new array, then
		 * if it exists at all, put the old array on the
		 * dirty list.
		 *
		 * Note that we can't kmem_free() the old array.
		 *
		 * Why -- well the 'get' operation is 'mutex-free', so we
		 * can't easily catch a suspended thread that is just about
		 * to dereference the array we just grew out of.  So we
		 * cons up a header and put it on a list of 'dirty'
		 * pointer arrays.  (Dirty in the sense that there may
		 * be suspended threads somewhere that are in the middle
		 * of referencing them).  Fortunately, we -can- garbage
		 * collect it all at ddi_soft_state_fini time.
		 */
		new_n_items = ss->n_items;
		while (new_n_items < (1 + item))
			new_n_items <<= 1;	/* double array size .. */

		ASSERT(new_n_items >= (1 + item));	/* sanity check! */

		new_array = kmem_zalloc(new_n_items * sizeof (void *),
		    KM_SLEEP);
		/*
		 * Copy the pointers into the new array
		 */
		bcopy((caddr_t)array, (caddr_t)new_array,
		    ss->n_items * sizeof (void *));

		/*
		 * Save the old array on the dirty list
		 */
		dirty = kmem_zalloc(sizeof (*dirty), KM_SLEEP);
		dirty->array = ss->array;
		dirty->n_items = ss->n_items;
		dirty->next = ss->next;
		ss->next = dirty;

		ss->array = (array = new_array);
		ss->n_items = new_n_items;
	}

	ASSERT(array != NULL && item < ss->n_items && array[item] == NULL);

	array[item] = new_element;

	mutex_exit(&ss->lock);
	return (DDI_SUCCESS);
}


/*
 * Fetch a pointer to the allocated soft state structure.
 *
 * This is designed to be cheap.
 *
 * There's an argument that there should be more checking for
 * nil pointers and out of bounds on the array.. but we do a lot
 * of that in the alloc/free routines.
 *
 * An array has the convenience that we don't need to lock read-access
 * to it c.f. a linked list.  However our "expanding array" strategy
 * means that we should hold a readers lock on the i_ddi_soft_state
 * structure.
 *
 * However, from a performance viewpoint, we need to do it without
 * any locks at all -- this also makes it a leaf routine.  The algorithm
 * is 'lock-free' because we only discard the pointer arrays at
 * ddi_soft_state_fini() time.
 */
void *
ddi_get_soft_state(register void *state, register int item)
{
	register struct i_ddi_soft_state *ss = state;

	ASSERT(ss != NULL && item >= 0);

	if (item < ss->n_items && ss->array != NULL)
		return (ss->array[item]);
	return (NULL);
}

/*
 * Free the state structure corresponding to 'item.'   Freeing an
 * element that has either gone or was never allocated is not
 * considered an error.  Note that we free the state structure, but
 * we don't shrink our pointer array, or discard 'dirty' arrays,
 * since even a few pointers don't really waste too much memory.
 *
 * Passing an item number that is out of bounds, or a null pointer will
 * provoke an error message.
 */
void
ddi_soft_state_free(register void *state, register int item)
{
	register struct i_ddi_soft_state *ss;
	register void **array;
	register void *element;
	static char msg[] = "ddi_soft_state_free:";

	if ((ss = state) == NULL) {
		cmn_err(CE_WARN, "%s null handle", msg);
		return;
	}

	element = NULL;

	mutex_enter(&ss->lock);

	if ((array = ss->array) == NULL || ss->size == 0) {
		cmn_err(CE_WARN, "%s bad handle", msg);
	} else if (item < 0 || item >= ss->n_items) {
		cmn_err(CE_WARN, "%s item %d not in range [0..%d]",
		    msg, item, ss->n_items - 1);
	} else if (array[item] != NULL) {
		element = array[item];
		array[item] = NULL;
	}

	mutex_exit(&ss->lock);

	if (element)
		kmem_free(element, ss->size);
}


/*
 * Free the entire set of pointers, and any
 * soft state structures contained therein.
 *
 * Note that we don't grab the ss->lock mutex, even though
 * we're inspecting the various fields of the data strucuture.
 *
 * There is an implicit assumption that this routine will
 * never run concurrently with any of the above on this
 * particular state structure i.e. by the time the driver
 * calls this routine, there should be no other threads
 * running in the driver.
 */
void
ddi_soft_state_fini(register void **state_p)
{
	register struct i_ddi_soft_state *ss, *dirty;
	register int item;
	static char msg[] = "ddi_soft_state_fini:";

	if (state_p == NULL || (ss = *state_p) == NULL) {
		cmn_err(CE_WARN, "%s null handle", msg);
		return;
	}

	if (ss->size == 0) {
		cmn_err(CE_WARN, "%s bad handle", msg);
		return;
	}

	if (ss->n_items > 0) {
		for (item = 0; item < ss->n_items; item++)
			ddi_soft_state_free(ss, item);
		kmem_free(ss->array, ss->n_items * sizeof (void *));
	}

	/*
	 * Now delete any dirty arrays from previous 'grow' operations
	 */
	for (dirty = ss->next; dirty; dirty = ss->next) {
		ss->next = dirty->next;
		kmem_free(dirty->array, dirty->n_items * sizeof (void *));
		kmem_free(dirty, sizeof (*dirty));
	}

	mutex_destroy(&ss->lock);
	kmem_free(ss, sizeof (*ss));

	*state_p = NULL;
}

/*
 *	This sets the devi_addr entry in the dev_info structure 'dip' to 'name'
 *	If name is NULL, this frees the devi_addr entry, if any.
 */

void
ddi_set_name_addr(dev_info_t *dip, char *name)
{
	register char *p = NULL;
	register char *oldname = DEVI(dip)->devi_addr;

	if (name != NULL)  {
		p = kmem_alloc(strlen(name) + 1, KM_SLEEP);
		(void) strcpy(p, name);
	}

	if (oldname != NULL)
		kmem_free(oldname, strlen(oldname) + 1);
	DEVI(dip)->devi_addr = p;
}

char *
ddi_get_name_addr(dev_info_t *dip)
{
	return (DEVI(dip)->devi_addr);
}

void
ddi_set_parent_data(dev_info_t *dip, caddr_t pd)
{
	DEVI(dip)->devi_parent_data = pd;
}

caddr_t
ddi_get_parent_data(dev_info_t *dip)
{
	return (DEVI(dip)->devi_parent_data);
}

/*
 * ddi_initchild:	Transform the prototype dev_info node into a
 *			canonical form 1 dev_info node.
 */
int
ddi_initchild(dev_info_t *parent, dev_info_t *proto)
{
	return (i_ddi_initchild(parent, proto));
}

/*
 * ddi_uninitchild:	Transform dev_info node back into a
 *			prototype form dev_info node.
 */

int
ddi_uninitchild(dev_info_t *dip)
{
	register dev_info_t *pdip;
	register struct dev_ops *ops;
	int (*f)();
	int error;

	/*
	 * If it's already a prototype node, we're done.
	 */
	if (!DDI_CF1(dip))
		return (DDI_SUCCESS);

	pdip = ddi_get_parent(dip);
	if ((pdip == NULL) || ((ops = ddi_get_driver(pdip)) == NULL) ||
	    (ops->devo_bus_ops == NULL) ||
	    ((f = ops->devo_bus_ops->bus_ctl) == NULL))
		return (DDI_FAILURE);

	error = (*f)(pdip, pdip, DDI_CTLOPS_UNINITCHILD, dip, (void *)NULL);
	ASSERT(error == DDI_SUCCESS);

	/*
	 * Strip the node to properly convert it back to prototype form
	 */
	ddi_remove_minor_node(dip, NULL);
	impl_rem_dev_props(dip);

	ddi_rele_devi(pdip);
	return (error);
}

major_t
ddi_name_to_major(register char *name)
{
	return (mod_name_to_major(name));
}

/*
 * ddi_major_to_name: Returns the module name bound to a major number.
 */

char *
ddi_major_to_name(major_t major)
{
	if (major >= devcnt)
		return (NULL);
	return ((&devnamesp[major])->dn_name);
}

/*
 * Return the name of the devinfo node pointed at by 'dip' in the buffer
 * pointed at by 'name.'  A devinfo node is named as a result of calling
 * ddi_initchild().
 *
 * Note: the driver must be held before calling this function!
 */
char *
ddi_deviname(dev_info_t *dip, char *name)
{
	register char *addrname;

	if (dip == ddi_root_node()) {
		*name = '\0';
		return (name);
	}

	ASSERT(DDI_CF1(dip));	/* Replaces ddi_initchild call */

	if (*(addrname = ddi_get_name_addr(dip)) == '\0')
		sprintf(name, "/%s", ddi_get_name(dip));
	else
		sprintf(name, "/%s@%s", ddi_get_name(dip), addrname);
	return (name);
}

/*
 * Given a pointer to a devinfo node, return the pathname of the devinfo
 * node in the buffer pointed at by "name."  The buffer is assumed to be
 * large enough to hold the pathname.
 *
 * The pathname of a devinfo node is the pathname of the parent devinfo node
 * concatenated with '/' and the name of the devinfo node (see ddi_deviname().
 * The pathname of the root devinfo node is "/".
 */
static char *
ddi_pathname_work(dev_info_t *dip, char *path)
{
	register char *bp;

	if (dip == ddi_root_node()) {
		*path = '\0';
		return (path);
	}
	(void) ddi_pathname_work(ddi_get_parent(dip), path);
	bp = path + strlen(path);
	(void) ddi_deviname(dip, bp);
	return (path);
}

char *
ddi_pathname(dev_info_t *dip, char *path)
{
	ASSERT(ddi_get_driver(dip));
	ASSERT(DEV_OPS_HELD(ddi_get_driver(dip)));

	return (ddi_pathname_work(dip, path));
}

/*
 * Given a dev_t, return the pathname of the corresponding device in the
 * buffer pointed at by "name."  The buffer is assumed to be large enough
 * to hold the pathname of the device.
 *
 * The pathname of a device is the pathname of the devinfo node to which
 * the device "belongs," concatenated with the character ':' and the name
 * of the minor node corresponding to the dev_t.
 */
int
ddi_dev_pathname(dev_t devt, char *name)
{
	register dev_info_t *dip;
	register char *bp;
	register struct ddi_minor_data *dmn;
	register major_t maj = getmajor(devt);
	register int error = DDI_FAILURE;

	if (ddi_hold_installed_driver(maj) == NULL)
		return (DDI_FAILURE);

	if (!(dip = e_ddi_get_dev_info(devt, VCHR)))  {
		/*
		 * e_ddi_get_dev_info() only returns with the driver
		 * held if it successfully translated its dev_t.
		 * So we only need to do a rele for ddi_hold_installed_driver
		 * at this point
		 */
		ddi_rele_driver(maj);
		return (DDI_FAILURE);
	}

	ddi_rele_driver(maj);	/* 1st for dev_get_dev_info() */

	(void) ddi_pathname(dip, name);
	bp = name + strlen(name);

	mutex_enter(&(DEVI(dip)->devi_lock));
	for (dmn = DEVI(dip)->devi_minor; dmn; dmn = dmn->next) {
		if ((dmn->type == DDM_MINOR || dmn->type == DDM_DEFAULT) &&
		    dmn->ddm_dev == devt) {
			*bp++ = ':';
			(void) strcpy(bp, dmn->ddm_name);
			error = DDI_SUCCESS;
			break;
			/*NOTREACHED*/
		} else if (dmn->type == DDM_ALIAS &&
		    dmn->ddm_adev == devt) {
			*bp++ = ':';
			(void) strcpy(bp, dmn->ddm_aname);
			error = DDI_SUCCESS;
			break;
			/*NOTREACHED*/
		}
	}
	mutex_exit(&(DEVI(dip)->devi_lock));

	ddi_rele_driver(maj);	/* 2nd for ddi_hold_installed_driver */
	return (error);
}

static void
parse_name(char *name, char **drvname, char **addrname, char **minorname)
{
	register char *cp, ch;
	static char nulladdrname[] = ":\0";

	cp = *drvname = name;
	*addrname = *minorname = NULL;
	while ((ch = *cp) != '\0') {
		if (ch == '@')
			*addrname = ++cp;
		else if (ch == ':')
			*minorname = ++cp;
		++cp;
	}
	if (!*addrname)
		*addrname = &nulladdrname[1];
	*((*addrname)-1) = '\0';
	if (*minorname)
		*((*minorname)-1) = '\0';
}

/*
 * Given the pathname of a device, return the dev_t of the corresponding
 * device.
 */

dev_t
ddi_pathname_to_dev_t(char *pathname)
{
	struct pathname pn;
	register struct ddi_minor_data *dmn;
	register int error = 0;
	register dev_info_t *parent;
	dev_info_t *nparent;
	char component[MAXNAMELEN], *drvname, *addrname, *minorname;
	dev_t devt;
	major_t major, lastmajor;
#if defined(i386)
	int repeat = 1;
#endif

	/*
	 * XXX	This is a bit odd
	 */
	devt = (u_long)-1;
	major = (u_long)-1;
	lastmajor = (u_long)-1;

	if (*pathname != '/' || pn_get(pathname, UIO_SYSSPACE, &pn) ||
	    ddi_install_driver("rootnex") != DDI_SUCCESS)
		return ((dev_t)-1);

	parent = ddi_root_node();
#if defined(i386)
	/*
	 * The need to do the loop twice is to fix a problem in installing
	 * system on IDE disk when the system is booted thru scsi CD device.
	 * The problem is that the way device configuration works it
	 * picks up the boot device (e.g EHA controller and not ATA
	 * controller) as the primary controller but we want the system to
	 * use ATA as the primary controller for setting up the bootpath for
	 * ATA device.
	 * XXX Until there is another proven way to fix this problem this is
	 * a working solution.
	 */
	pn_setlast(&pn);
#else
	pn_skipslash(&pn);
#endif

again:
	while (pn_pathleft(&pn)) {
		(void) pn_getcomponent(&pn, component);
		parse_name(component, &drvname, &addrname, &minorname);
		lastmajor = major;
		if ((major = ddi_name_to_major(drvname)) == (dev_t)-1)  {
			error = 1;
			break;
		}
		if (ddi_hold_installed_driver(major) == NULL)  {
			major = (dev_t)-1;
			error = 1;
			break;
		}
		nparent = ddi_findchild(parent, drvname, addrname);
		if (nparent == NULL)  {
			error = 1;
			break;
		}
		if (lastmajor != (dev_t)-1)  {		/* Release old parent */
			ddi_rele_driver(lastmajor);
			lastmajor = (dev_t)-1;
		}
		parent = nparent;
		pn_skipslash(&pn);
	}
#if defined(i386)
	if (repeat) {
		repeat = 0;
		pn_free(&pn);
		(void) pn_get(pathname, UIO_SYSSPACE, &pn);
		error = 0;
		devt = (dev_t)-1;
		major = (dev_t)-1;
		lastmajor = (dev_t)-1;
		pn_skipslash(&pn);
		goto again;
	}
#endif
	if (!error && minorname) {
		mutex_enter(&(DEVI(parent)->devi_lock));
		for (dmn = DEVI(parent)->devi_minor; dmn; dmn = dmn->next) {
			if ((dmn->type == DDM_MINOR ||
			    dmn->type == DDM_DEFAULT) &&
			    strcmp(dmn->ddm_name, minorname) == 0) {
				devt = dmn->ddm_dev;
				break;
			}
		}
		mutex_exit(&(DEVI(parent)->devi_lock));
	} else if (!error) {
		/* check for a default entry */
		mutex_enter(&(DEVI(parent)->devi_lock));
		for (dmn = DEVI(parent)->devi_minor; dmn; dmn = dmn->next) {
			if (dmn->type == DDM_DEFAULT) {
				devt = dmn->ddm_dev;
				goto got_one;
			}
		}
		/* No default minor node, so just return the first one */
		if ((dmn = DEVI(parent)->devi_minor) != NULL &&
		    dmn->type == DDM_MINOR) {
			devt = dmn->ddm_dev;
		} else {
			/* Assume 1-to-1 mapping of instance to minor */
			devt = makedevice(major, ddi_get_instance(parent));
		}
got_one:
		mutex_exit(&(DEVI(parent)->devi_lock));
	}
	pn_free(&pn);
	/*
	 * Release the holds on these drivers now that we are done with them
	 */
	if (lastmajor != (dev_t)-1)
		ddi_rele_driver(lastmajor);
	if (major != (dev_t)-1)
		ddi_rele_driver(major);
	return (devt);
}

/*
 * Find a child of 'p' whose name matches the parameters cname@caddr.
 * The caller must ensure that we are single threading anything that
 * can change the per-driver's instance list!
 */

dev_info_t *
ddi_findchild(dev_info_t *p, char *cname, char *caddr)
{
	register dev_info_t *cdip, *ndip;
	register char *naddr;
	register major_t major;

#ifdef lint
	ndip = NULL;	/* See 1094364 */
#endif

	if (p == NULL)
		return (NULL);

	major = ddi_name_to_major(cname);
	ASSERT(major != (major_t)-1);

	/*
	 * Using the drivers instance list, init each child as we look for
	 * a match.
	 */

	for (cdip = devnamesp[major].dn_head; cdip != NULL; cdip = ndip)  {
		ndip = ddi_get_next(cdip);
		if (ddi_get_parent(cdip) == p)  {
			if (ddi_initchild(p, cdip) != DDI_SUCCESS)  {
				if (ddi_get_nodeid(cdip) == DEVI_PSEUDO_NODEID)
					(void) ddi_remove_child(cdip, 0);
				continue;
			}
			if ((naddr = ddi_get_name_addr(cdip)) != NULL &&
			    (strcmp(caddr, naddr) == 0))
				break;
		}
	}
	return (cdip);
}

/*
 * e_ddi_deferred_attach:	Attempt to attach either a specific
 *		devinfo or all unattached devinfos to a driver
 *
 *	dev_t == NODEV means try all unattached instances.
 *
 * Specific case returns DDI_SUCCESS if the devinfo was attached.
 *
 * DDI framework layer, only. (Not an exported interface).
 */

int
e_ddi_deferred_attach(major_t major, dev_t dev)
{
	register struct devnames *dnp = &devnamesp[major];
	register dev_info_t *dip;
	dev_info_t *ndip;
	int instance;
	int error = DDI_FAILURE;

	/*
	 * The driver must be held before calling e_ddi_deferred_attach
	 */
	ASSERT(DEV_OPS_HELD(devopsp[major]));

	/*
	 * Prevent other threads from loading/holding/attaching/unloading this
	 * driver while we are attempting deferred attach...
	 */
	LOCK_DEV_OPS(&(dnp->dn_lock));
	while (DN_BUSY_CHANGING(dnp->dn_flags))
		cv_wait(&(dnp->dn_wait), &(dnp->dn_lock));
	dnp->dn_flags |= DN_BUSY_LOADING;
	dnp->dn_busy_thread = curthread;
	UNLOCK_DEV_OPS(&(dnp->dn_lock));

	if (dev != NODEV)  {

		/*
		 * Specific dev_t case ... Call the driver to get the
		 * instance number of the given dev_t and if it is valid,
		 * and we have the given instance, try to transform it
		 * it to CF2.
		 */
		if ((instance = dev_to_instance(dev)) < 0)
			goto out;

		for (dip = dnp->dn_head; dip != NULL; dip = ddi_get_next(dip))
			if (ddi_get_instance(dip) == instance)
				break;

		if ((dip == NULL) || (DDI_CF2(dip)))
			goto out;	/* Not found or already attached */

		if ((error = impl_proto_to_cf2(dip)) == DDI_SUCCESS)
			ddi_rele_driver(major);	/* Undo extra hold */
		goto out;
	}

	/*
	 * Non-specific dev_t case ... (always succeeds, even if it does
	 * nothing)
	 */

	error = DDI_SUCCESS;
	/*LINTED [bogus used-before-set: bugid 1094364]*/
	for (dip = dnp->dn_head; dip != NULL; dip = ndip)  {
		ndip = ddi_get_next(dip);

		/*
		 * If the devinfo is named, but not attached, attempt
		 * transformation, which will try to attach the node.
		 */
		if (DDI_CF1(dip) && (!DDI_CF2(dip)))
			if (impl_proto_to_cf2(dip) == DDI_SUCCESS)
				ddi_rele_driver(major);	/* Undo extra hold */
	}

out:

	/*
	 * Give up the busy/changing lock on this device driver
	 */
	LOCK_DEV_OPS(&(dnp->dn_lock));
	dnp->dn_flags &= ~(DN_BUSY_CHANGING_BITS);
	dnp->dn_busy_thread = NULL;
	cv_broadcast(&(dnp->dn_wait));
	UNLOCK_DEV_OPS(&(dnp->dn_lock));

	return (error);
}

/*
 * Here we try and anticipate the case where 'call parent'
 * immediately results in another 'call parent' by looking at
 * what our parent nexus driver would do if we asked it.
 *
 * If the order we do things seems a bit back-to-front, then
 * remember that we instantiate the devinfo tree top down, which
 * means that at any point in the tree, we can assume our parents
 * are already optimized.
 */
/* #define	DEBUG_DTREE */

#ifdef DEBUG_DTREE
#ifndef DEBUG
#define	DEBUG
#endif /* !DEBUG */
static void debug_dtree(dev_info_t *, struct dev_info *, char *);
#endif /* DEBUG_DTREE */

#ifdef DEBUG
/*
 * Set this variable to '0' to disable the optimization.
 */
int optimize_dtree = 1;
#endif /* DEBUG */

void
ddi_optimize_dtree(dev_info_t *devi)
{
	register struct dev_info *pdevi;
	register struct bus_ops *b;

	ASSERT(DDI_CF1(devi));
	pdevi = DEVI(devi)->devi_parent;
	ASSERT(pdevi);
	b = pdevi->devi_ops->devo_bus_ops;

#ifdef DEBUG
	/*
	 * Last chance to bailout..
	 */
	if (!optimize_dtree) {
		DEVI(devi)->devi_bus_map_fault = pdevi;
		DEVI(devi)->devi_bus_dma_map = pdevi;
		DEVI(devi)->devi_bus_dma_allochdl = pdevi;
		DEVI(devi)->devi_bus_dma_freehdl = pdevi;
		DEVI(devi)->devi_bus_dma_bindhdl = pdevi;
		DEVI(devi)->devi_bus_dma_unbindhdl = pdevi;
		DEVI(devi)->devi_bus_dma_flush = pdevi;
		DEVI(devi)->devi_bus_dma_win = pdevi;
		DEVI(devi)->devi_bus_dma_ctl = pdevi;
		DEVI(devi)->devi_bus_ctl = pdevi;
		return;
	}
#endif	/* DEBUG */

	/*
	 * XXX	This one is a bit dubious, because i_ddi_map_fault
	 *	is currently (wrongly) an implementation dependent
	 *	function.  However, given that it's only i_ddi_map_fault
	 *	that -uses- the devi_bus_map_fault pointer, this is ok.
	 */
	if (i_ddi_map_fault == b->bus_map_fault) {
		DEVI(devi)->devi_bus_map_fault = pdevi->devi_bus_map_fault;
#ifdef	DEBUG_DTREE
		debug_dtree(devi, DEVI(devi)->devi_bus_map_fault,
		    "bus_map_fault");
#endif	/* DEBUG_DTREE */
	} else
		DEVI(devi)->devi_bus_map_fault = pdevi;

	if (ddi_dma_map == b->bus_dma_map) {
		DEVI(devi)->devi_bus_dma_map = pdevi->devi_bus_dma_map;
#ifdef	DEBUG_DTREE
		debug_dtree(devi, DEVI(devi)->devi_bus_dma_map, "bus_dma_map");
#endif	/* DEBUG_DTREE */
	} else
		DEVI(devi)->devi_bus_dma_map = pdevi;

	if (ddi_dma_allochdl == b->bus_dma_allochdl) {
		DEVI(devi)->devi_bus_dma_allochdl =
			pdevi->devi_bus_dma_allochdl;
#ifdef	DEBUG_DTREE
		debug_dtree(devi, DEVI(devi)->devi_bus_dma_allochdl,
		    "bus_dma_allochdl");
#endif	/* DEBUG_DTREE */
	} else
		DEVI(devi)->devi_bus_dma_allochdl = pdevi;

	if (ddi_dma_freehdl == b->bus_dma_freehdl) {
		DEVI(devi)->devi_bus_dma_freehdl = pdevi->devi_bus_dma_freehdl;
#ifdef	DEBUG_DTREE
		debug_dtree(devi, DEVI(devi)->devi_bus_dma_freehdl,
		    "bus_dma_freehdl");
#endif	/* DEBUG_DTREE */
	} else
		DEVI(devi)->devi_bus_dma_freehdl = pdevi;

	if (ddi_dma_bindhdl == b->bus_dma_bindhdl) {
		DEVI(devi)->devi_bus_dma_bindhdl = pdevi->devi_bus_dma_bindhdl;
#ifdef	DEBUG_DTREE
		debug_dtree(devi, DEVI(devi)->devi_bus_dma_bindhdl,
		    "bus_dma_bindhdl");
#endif	/* DEBUG_DTREE */
	} else
		DEVI(devi)->devi_bus_dma_bindhdl = pdevi;

	if (ddi_dma_unbindhdl == b->bus_dma_unbindhdl) {
		DEVI(devi)->devi_bus_dma_unbindhdl =
		    pdevi->devi_bus_dma_unbindhdl;
#ifdef  DEBUG_DTREE
		debug_dtree(devi, DEVI(devi)->devi_bus_dma_unbindhdl,
		    "bus_dma_unbindhdl");
#endif	/* DEBUG_DTREE */
	} else
		DEVI(devi)->devi_bus_dma_unbindhdl = pdevi;

	if (ddi_dma_flush == b->bus_dma_flush) {
		DEVI(devi)->devi_bus_dma_flush =
		    pdevi->devi_bus_dma_flush;
#ifdef	DEBUG_DTREE
		debug_dtree(devi, DEVI(devi)->devi_bus_dma_flush,
		    "bus_dma_flush");
#endif	/* DEBUG_DTREE */
	} else
		DEVI(devi)->devi_bus_dma_flush = pdevi;

	if (ddi_dma_win == b->bus_dma_win) {
		DEVI(devi)->devi_bus_dma_win =
		    pdevi->devi_bus_dma_win;
#ifdef	DEBUG_DTREE
		debug_dtree(devi, DEVI(devi)->devi_bus_dma_win,
		    "bus_dma_win");
#endif	/* DEBUG_DTREE */
	} else
		DEVI(devi)->devi_bus_dma_win = pdevi;

	if (ddi_dma_mctl == b->bus_dma_ctl) {
		DEVI(devi)->devi_bus_dma_ctl = pdevi->devi_bus_dma_ctl;
#ifdef	DEBUG_DTREE
		debug_dtree(devi, DEVI(devi)->devi_bus_dma_ctl, "bus_dma_ctl");
#endif	/* DEBUG_DTREE */
	} else
		DEVI(devi)->devi_bus_dma_ctl = pdevi;

	if (ddi_ctlops == b->bus_ctl) {
		DEVI(devi)->devi_bus_ctl = pdevi->devi_bus_ctl;
#ifdef	DEBUG_DTREE
		debug_dtree(devi, DEVI(devi)->devi_bus_ctl, "bus_ctl");
#endif	/* DEBUG_DTREE */
	} else
		DEVI(devi)->devi_bus_ctl = pdevi;
}

#ifdef	DEBUG_DTREE
static void
debug_dtree(dev_info_t *devi, struct dev_info *adevi, char *service)
{
#ifdef	DEBUG
	char *adevi_name;
	char *adevi_addr;

	if ((dev_info_t *)adevi == ddi_root_node()) {
		adevi_name = "root";
		adevi_addr = "0";
	} else {
		adevi_name = adevi->devi_name;
		adevi_addr = adevi->devi_addr;
	}
	cmn_err(CE_CONT, "%s@%s %s -> %s@%s\n",
	    ddi_get_name(devi), ddi_get_name_addr(devi),
	    service, adevi_name, adevi_addr);
#endif /* DEBUG */
}
#endif /* DEBUG_DTREE */

int
ddi_dma_alloc_handle(dev_info_t *dip, ddi_dma_attr_t *attr,
	int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep)
{
	int (*funcp)() = ddi_dma_allochdl;
	auto ddi_dma_attr_t dma_attr;
	struct bus_ops *bop;

	if (attr == (ddi_dma_attr_t *)0) {
		return (DDI_DMA_BADATTR);
	}
	dma_attr = *attr;

	bop = DEVI(dip)->devi_ops->devo_bus_ops;
	if (bop && bop->bus_dma_allochdl)
		funcp = bop->bus_dma_allochdl;

	return ((*funcp)(dip, dip, &dma_attr, waitfp, arg, handlep));
}

void
ddi_dma_free_handle(ddi_dma_handle_t *handlep)
{
	ddi_dma_handle_t h = *handlep;
	(void) ddi_dma_freehdl(HD, HD, h);
}

static int dma_mem_list_id = 0;


int
ddi_dma_mem_alloc(ddi_dma_handle_t handle, uint_t length,
	ddi_device_acc_attr_t *accattrp, ulong_t xfermodes,
	int (*waitfp)(caddr_t), caddr_t arg, caddr_t *kaddrp,
	uint_t *real_length, ddi_acc_handle_t *handlep)
{
	ddi_dma_impl_t *hp = (ddi_dma_impl_t *)handle;
	dev_info_t *dip = hp->dmai_rdip;
	ddi_dma_lim_t lim, *limp;
	ddi_acc_hdl_t *ap;
	ddi_dma_attr_t *attrp = &hp->dmai_attr;
	u_int sleepflag;
	int (*fp)(caddr_t);
	int rval;

	limp = &lim;
	limp->dlim_addr_lo = (u_long)attrp->dma_attr_addr_lo;
	limp->dlim_addr_hi = (u_long)attrp->dma_attr_addr_hi;
	limp->dlim_minxfer = (u_int)attrp->dma_attr_minxfer;
	limp->dlim_dmaspeed = 0;
#ifdef sparc
	limp->dlim_cntr_max = (u_int)attrp->dma_attr_seg;
	limp->dlim_burstsizes = (u_int)attrp->dma_attr_burstsizes;
#else
	limp->dlim_cntr_max = 0;
	limp->dlim_burstsizes = 1;
	limp->dlim_version = DMALIM_VER0;
	limp->dlim_adreg_max = (u_int)attrp->dma_attr_seg;
	limp->dlim_ctreg_max = (u_int)attrp->dma_attr_count_max;
	limp->dlim_granular = (u_int)attrp->dma_attr_granular;
	limp->dlim_sgllen = (short)attrp->dma_attr_sgllen;
	limp->dlim_reqsize = (u_int)attrp->dma_attr_maxxfer;
#endif

	if (waitfp == DDI_DMA_SLEEP)
		fp = (int (*)())KM_SLEEP;
	else if (waitfp == DDI_DMA_DONTWAIT)
		fp = (int (*)())KM_NOSLEEP;
	else
		fp = waitfp;
	*handlep = impl_acc_hdl_alloc(fp, arg);
	if (*handlep == NULL)
		return (DDI_DMA_NORESOURCES);

	/*
	 * initialize the common elements of data access handle
	 */
	ap = impl_acc_hdl_get(*handlep);
	ap->ah_vers = VERS_ACCHDL;
	ap->ah_offset = 0;
	ap->ah_len = 0;
	ap->ah_xfermodes = xfermodes;
	ap->ah_acc = *accattrp;

	sleepflag = ((waitfp == DDI_DMA_SLEEP) ? 1 : 0);
	if (xfermodes == DDI_DMA_CONSISTENT) {
		rval = i_ddi_mem_alloc(dip, limp, (u_int)length, sleepflag, 0,
			    accattrp, kaddrp, (u_int *)0, ap);
		*real_length = length;
	} else {
		rval = i_ddi_mem_alloc(dip, limp, (u_int)length, sleepflag, 1,
			    accattrp, kaddrp, (u_int *)real_length, ap);
	}
	if (rval == DDI_SUCCESS) {
		ap->ah_len = (off_t)(*real_length);
		ap->ah_addr = *kaddrp;
	} else {
		impl_acc_hdl_free(*handlep);
		*handlep = (ddi_acc_handle_t)NULL;
		rval = DDI_DMA_NORESOURCES;
	}
bad:
	if (rval == DDI_DMA_NORESOURCES &&
	    waitfp != DDI_DMA_DONTWAIT) {
		ddi_set_callback(waitfp, arg, &dma_mem_list_id);
	}
	return (rval);
}

void
ddi_dma_mem_free(ddi_acc_handle_t *handlep)
{
	ddi_acc_hdl_t *ap;

	ap = impl_acc_hdl_get(*handlep);
	ASSERT(ap);

	if (ap->ah_xfermodes == DDI_DMA_CONSISTENT) {
		i_ddi_mem_free((caddr_t)ap->ah_addr, 0);
	} else {
		i_ddi_mem_free((caddr_t)ap->ah_addr, 1);
	}

	/*
	 * free the handle
	 */
	impl_acc_hdl_free(*handlep);
	*handlep = (ddi_acc_handle_t)NULL;

	if (dma_mem_list_id != 0) {
		ddi_run_callback(&dma_mem_list_id);
	}
}

int
ddi_dma_buf_bind_handle(ddi_dma_handle_t handle, struct buf *bp,
	uint_t flags, int (*waitfp)(caddr_t), caddr_t arg,
	ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	ddi_dma_impl_t *hp = (ddi_dma_impl_t *)handle;
	dev_info_t *dip = hp->dmai_rdip;
	register int (*funcp)() = ddi_dma_bindhdl;
	auto struct ddi_dma_req dmareq;
	register struct bus_ops *bop;

	dmareq.dmar_flags = flags;
	dmareq.dmar_fp = waitfp;
	dmareq.dmar_arg = arg;
	dmareq.dmar_object.dmao_size = (u_int) bp->b_bcount;

	if ((bp->b_flags & (B_PAGEIO|B_REMAPPED)) == B_PAGEIO) {
		dmareq.dmar_object.dmao_type = DMA_OTYP_PAGES;
		dmareq.dmar_object.dmao_obj.pp_obj.pp_pp = bp->b_pages;
		dmareq.dmar_object.dmao_obj.pp_obj.pp_offset =
		    (u_int) (((u_int)bp->b_un.b_addr) & MMU_PAGEOFFSET);
	} else {
		/*
		 * If the buffer has no proc pointer, or the proc
		 * struct has the kernel address space, or the buffer has
		 * been marked B_REMAPPED (meaning that it is now
		 * mapped into the kernel's address space), then
		 * the address space is kas (kernel address space).
		 *
		 * Otherwise, the address space described by the
		 * the buffer's process owner had better be valid!
		 */

		dmareq.dmar_object.dmao_type = DMA_OTYP_VADDR;
		dmareq.dmar_object.dmao_obj.virt_obj.v_addr = bp->b_un.b_addr;

		if (bp->b_proc == (struct proc *)NULL ||
		    bp->b_proc->p_as == &kas ||
		    (bp->b_flags & B_REMAPPED) != 0) {
			dmareq.dmar_object.dmao_obj.virt_obj.v_as = 0;
		} else {
			dmareq.dmar_object.dmao_obj.virt_obj.v_as =
			    bp->b_proc->p_as;
		}
	}

	/*
	 * Handle the case that the requestor is both a leaf
	 * and a nexus driver simultaneously by calling the
	 * requestor's bus_dma_map function directly instead
	 * of ddi_dma_map.
	 */
	bop = DEVI(dip)->devi_ops->devo_bus_ops;
	if (bop && bop->bus_dma_bindhdl)
		funcp = bop->bus_dma_bindhdl;

	return ((*funcp)(dip, dip, handle, &dmareq, cookiep, ccountp));
}

int
ddi_dma_addr_bind_handle(ddi_dma_handle_t handle, struct as *as,
	caddr_t addr, u_int len, u_int flags, int (*waitfp)(caddr_t),
	caddr_t arg, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	ddi_dma_impl_t *hp = (ddi_dma_impl_t *)handle;
	dev_info_t *dip = hp->dmai_rdip;
	int (*funcp)() = ddi_dma_bindhdl;
	auto struct ddi_dma_req dmareq;
	struct bus_ops *bop;

	dmareq.dmar_flags = flags;
	dmareq.dmar_fp = waitfp;
	dmareq.dmar_arg = arg;
	dmareq.dmar_object.dmao_size = len;
	dmareq.dmar_object.dmao_type = DMA_OTYP_VADDR;
	dmareq.dmar_object.dmao_obj.virt_obj.v_as = as;
	dmareq.dmar_object.dmao_obj.virt_obj.v_addr = addr;

	/*
	 * Handle the case that the requestor is both a leaf
	 * and a nexus driver simultaneously by calling the
	 * requestor's bus_dma_map function directly instead
	 * of ddi_dma_map.
	 */
	bop = DEVI(dip)->devi_ops->devo_bus_ops;
	if (bop && bop->bus_dma_bindhdl)
		funcp = bop->bus_dma_bindhdl;

	return ((*funcp)(dip, dip, handle, &dmareq, cookiep, ccountp));
}

int
ddi_dma_unbind_handle(ddi_dma_handle_t h)
{
	return (ddi_dma_unbindhdl(HD, HD, h));
}

void
ddi_dma_nextcookie(ddi_dma_handle_t handle, ddi_dma_cookie_t *cookiep)
{
	ddi_dma_impl_t *hp = (ddi_dma_impl_t *)handle;
	ddi_dma_cookie_t *cp;

	cp = hp->dmai_cookie;
	ASSERT(cp);

	cookiep->dmac_notused = cp->dmac_notused;
	cookiep->dmac_type = cp->dmac_type;
	cookiep->dmac_address = cp->dmac_address;
	cookiep->dmac_size = cp->dmac_size;
	hp->dmai_cookie++;
}

int
ddi_dma_numwin(ddi_dma_handle_t handle, uint_t *nwinp)
{
	ddi_dma_impl_t *hp = (ddi_dma_impl_t *)handle;
	if ((hp->dmai_rflags & DDI_DMA_PARTIAL) == 0) {
		return (DDI_FAILURE);
	} else {
		*nwinp = hp->dmai_nwin;
		return (DDI_SUCCESS);
	}
}

int
ddi_dma_getwin(ddi_dma_handle_t h, uint_t win, off_t *offp,
	uint_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	int (*funcp)() = ddi_dma_win;
	struct bus_ops *bop;

	bop = DEVI(HD)->devi_ops->devo_bus_ops;
	if (bop && bop->bus_dma_win)
		funcp = bop->bus_dma_win;

	return ((*funcp)(HD, HD, h, win, offp, lenp, cookiep, ccountp));
}

int
ddi_dma_set_sbus64(ddi_dma_handle_t h, uint_t burstsizes)
{
	return (ddi_dma_mctl(HD, HD, h, DDI_DMA_SET_SBUS64, 0,
		&burstsizes, 0, 0));
}

/*
 * register mapping routines.
 */
int
ddi_regs_map_setup(dev_info_t *dip, u_int rnumber, caddr_t *addrp,
	offset_t offset, offset_t len, ddi_device_acc_attr_t *accattrp,
	ddi_acc_handle_t *handle)
{
	ddi_map_req_t mr;
	ddi_acc_hdl_t *hp;
	int result;

	/*
	 * Allocate and initialize the common elements of data access handle.
	 */
	*handle = impl_acc_hdl_alloc(KM_SLEEP, NULL);
	hp = impl_acc_hdl_get(*handle);
	hp->ah_vers = VERS_ACCHDL;
	hp->ah_dip = dip;
	hp->ah_rnumber = rnumber;
	hp->ah_offset = offset;
	hp->ah_len = len;
	hp->ah_acc = *accattrp;

	/*
	 * Set up the mapping request and call to parent.
	 */
	mr.map_op = DDI_MO_MAP_LOCKED;
	mr.map_type = DDI_MT_RNUMBER;
	mr.map_obj.rnumber = rnumber;
	mr.map_prot = PROT_READ | PROT_WRITE;
	mr.map_flags = DDI_MF_KERNEL_MAPPING;
	mr.map_handlep = hp;
	mr.map_vers = DDI_MAP_VERSION;
	result = ddi_map(dip, &mr, offset, len, addrp);

	/*
	 * check for end result
	 */
	if (result != DDI_SUCCESS) {
		impl_acc_hdl_free(*handle);
		*handle = (ddi_acc_handle_t)NULL;
	} else {
		hp->ah_addr = *addrp;
	}

	return (result);
}

void
ddi_regs_map_free(ddi_acc_handle_t *handlep)
{
	ddi_map_req_t mr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(*handlep);
	ASSERT(hp);

	mr.map_op = DDI_MO_UNMAP;
	mr.map_type = DDI_MT_RNUMBER;
	mr.map_obj.rnumber = hp->ah_rnumber;
	mr.map_prot = PROT_READ | PROT_WRITE;
	mr.map_flags = DDI_MF_KERNEL_MAPPING;
	mr.map_handlep = hp;
	mr.map_vers = DDI_MAP_VERSION;

	/*
	 * Call my parent to unmap my regs.
	 */

	(void) ddi_map(hp->ah_dip, &mr, hp->ah_offset,
		hp->ah_len, &hp->ah_addr);
	/*
	 * free the handle
	 */
	impl_acc_hdl_free(*handlep);
	*handlep = (ddi_acc_handle_t)NULL;
}

int
ddi_device_zero(ddi_acc_handle_t handle, caddr_t dev_addr, size_t bytecount,
	long dev_advcnt, ulong_t dev_datasz)
{
	uchar_t *b;
	ushort_t *w;
	ulong_t *l;
	unsigned long long *ll;

	/* check for total byte count is multiple of data transfer size */
	if (bytecount != ((bytecount / dev_datasz) * dev_datasz))
		return (DDI_FAILURE);

	switch (dev_datasz) {
	case DDI_DATA_SZ01_ACC:
		for (b = (uchar_t *)dev_addr;
			bytecount > 0; bytecount -= 1, b += dev_advcnt)
			ddi_putb(handle, b, 0);
		break;
	case DDI_DATA_SZ02_ACC:
		for (w = (ushort_t *)dev_addr;
			bytecount > 0; bytecount -= 2, w += dev_advcnt)
			ddi_putw(handle, w, 0);
		break;
	case DDI_DATA_SZ04_ACC:
		for (l = (ulong_t *)dev_addr;
			bytecount > 0; bytecount -= 4, l += dev_advcnt)
			ddi_putl(handle, l, 0);
		break;
	case DDI_DATA_SZ08_ACC:
		for (ll = (unsigned long long *)dev_addr;
			bytecount > 0; bytecount -= 8, ll += dev_advcnt)
			ddi_putll(handle, ll, 0x0ll);
		break;
	default:
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

int
ddi_device_copy(
	ddi_acc_handle_t src_handle, caddr_t src_addr, long src_advcnt,
	ddi_acc_handle_t dest_handle, caddr_t dest_addr, long dest_advcnt,
	size_t bytecount, ulong_t dev_datasz)
{
	uchar_t *b_src, *b_dst;
	ushort_t *w_src, *w_dst;
	ulong_t *l_src, *l_dst;
	unsigned long long *ll_src, *ll_dst;

	/* check for total byte count is multiple of data transfer size */
	if (bytecount != ((bytecount / dev_datasz) * dev_datasz))
		return (DDI_FAILURE);

	switch (dev_datasz) {
	case DDI_DATA_SZ01_ACC:
		b_src = (uchar_t *)src_addr;
		b_dst = (uchar_t *)dest_addr;

		for (; bytecount > 0; bytecount -= 1) {
			ddi_putb(dest_handle, b_dst,
				ddi_getb(src_handle, b_src));
			b_dst += dest_advcnt;
			b_src += src_advcnt;
		}
		break;
	case DDI_DATA_SZ02_ACC:
		w_src = (ushort_t *)src_addr;
		w_dst = (ushort_t *)dest_addr;

		for (; bytecount > 0; bytecount -= 2) {
			ddi_putw(dest_handle, w_dst,
				ddi_getw(src_handle, w_src));
			w_dst += dest_advcnt;
			w_src += src_advcnt;
		}
		break;
	case DDI_DATA_SZ04_ACC:
		l_src = (ulong_t *)src_addr;
		l_dst = (ulong_t *)dest_addr;

		for (; bytecount > 0; bytecount -= 4) {
			ddi_putl(dest_handle, l_dst,
				ddi_getl(src_handle, l_src));
			l_dst += dest_advcnt;
			l_src += src_advcnt;
		}
		break;
	case DDI_DATA_SZ08_ACC:
		ll_src = (unsigned long long *)src_addr;
		ll_dst = (unsigned long long *)dest_addr;

		for (; bytecount > 0; bytecount -= 8) {
			ddi_putll(dest_handle, ll_dst,
				ddi_getll(src_handle, ll_src));
			ll_dst += dest_advcnt;
			ll_src += src_advcnt;
		}
		break;
	default:
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

int
pci_config_setup(dev_info_t *dip, ddi_acc_handle_t *handle)
{
	caddr_t	cfgaddr;
	int	status;
	ddi_device_acc_attr_t attr;

	attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;

	status = ddi_regs_map_setup(dip, 0, &cfgaddr, 0, 0,
		&attr, handle);
	return (status);

}

void
pci_config_teardown(ddi_acc_handle_t *handle)
{
	ddi_regs_map_free(handle);
}

uchar_t
pci_config_getb(ddi_acc_handle_t handle, ulong_t offset)
{
	caddr_t	cfgaddr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(handle);
	cfgaddr = hp->ah_addr + offset;
	return (ddi_getb(handle, (uchar_t *)cfgaddr));
}

ushort_t
pci_config_getw(ddi_acc_handle_t handle, ulong_t offset)
{
	caddr_t	cfgaddr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(handle);
	cfgaddr = hp->ah_addr + offset;
	return (ddi_getw(handle, (ushort_t *)cfgaddr));
}

ulong_t
pci_config_getl(ddi_acc_handle_t handle, ulong_t offset)
{
	caddr_t	cfgaddr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(handle);
	cfgaddr = hp->ah_addr + offset;
	return (ddi_getl(handle, (ulong_t *)cfgaddr));
}

unsigned long long
pci_config_getll(ddi_acc_handle_t handle, ulong_t offset)
{
	caddr_t	cfgaddr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(handle);
	cfgaddr = hp->ah_addr + offset;
	return (ddi_getll(handle, (unsigned long long *)cfgaddr));
}

void
pci_config_putb(ddi_acc_handle_t handle, ulong_t offset, uchar_t value)
{
	caddr_t	cfgaddr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(handle);
	cfgaddr = hp->ah_addr + offset;
	ddi_putb(handle, (uchar_t *)cfgaddr, value);
}

void
pci_config_putw(ddi_acc_handle_t handle, ulong_t offset, ushort_t value)
{
	caddr_t	cfgaddr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(handle);
	cfgaddr = hp->ah_addr + offset;
	ddi_putw(handle, (ushort_t *)cfgaddr, value);
}

void
pci_config_putl(ddi_acc_handle_t handle, ulong_t offset, ulong_t value)
{
	caddr_t	cfgaddr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(handle);
	cfgaddr = hp->ah_addr + offset;
	ddi_putl(handle, (ulong_t *)cfgaddr, value);
}

void
pci_config_putll(ddi_acc_handle_t handle, ulong_t offset,
	unsigned long long value)
{
	caddr_t	cfgaddr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(handle);
	cfgaddr = hp->ah_addr + offset;
	ddi_putll(handle, (unsigned long long *)cfgaddr, value);
}


#define	swap_ushort(value)  \
	((((value) & 0xff) << 8) | ((value) >> 8))

#define	swap_ulong(value)   \
	((ddi_swap_ushort((ushort_t)((value) & 0xffff)) << 16) | \
	ddi_swap_ushort((ushort_t)((value) >> 16)))

#define	swap_ulonglong(value)	\
	(((unsigned long long) ddi_swap_ulong((ulong_t)((value) & 0xffffffff)) \
	    << 32) | \
	(unsigned long long) ddi_swap_ulong((ulong_t)((value) >> 32)))

ushort_t
ddi_swap_ushort(ushort_t value)
{
	return (swap_ushort(value));
}

ulong_t
ddi_swap_ulong(ulong_t value)
{
	return (swap_ulong(value));
}

unsigned long long
ddi_swap_ulonglong(unsigned long long value)
{
	return (swap_ulonglong(value));
}
