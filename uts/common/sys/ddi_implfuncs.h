/*
 * Copyright (c) by 1990-1994, Sun Microsystems, Inc.
 */

#ifndef _SYS_DDI_IMPLFUNCS_H
#define	_SYS_DDI_IMPLFUNCS_H

#pragma ident	"@(#)ddi_implfuncs.h	1.15	94/12/05 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * Declare implementation functions that sunddi functions can
 * call in order to perform their required task. Each kernel
 * architecture must provide them.
 */

int
i_ddi_bus_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
	off_t offset, off_t len, caddr_t *vaddrp);

int
i_ddi_apply_range(dev_info_t *dip, dev_info_t *rdip, struct regspec *rp);

struct regspec *
i_ddi_rnumber_to_regspec(dev_info_t *dip, int rnumber);


int
i_ddi_map_fault(dev_info_t *dip, dev_info_t *rdip,
	struct hat *hat, struct seg *seg, caddr_t addr,
	struct devpage *dp, u_int pfn, u_int prot, u_int lock);

ddi_regspec_t
i_ddi_get_regspec(dev_info_t *dip, dev_info_t *rdip, u_int rnumber,
	off_t offset, off_t len);

ddi_intrspec_t
i_ddi_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int inumber);

int
i_ddi_add_intrspec(dev_info_t *dip, dev_info_t *rdip, ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg, int kind);

void
i_ddi_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookie);

int
i_ddi_add_softintr(dev_info_t *dip, int preference, ddi_softintr_t *idp,
	ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg);

void
i_ddi_trigger_softintr(ddi_softintr_t id);

void
i_ddi_remove_softintr(ddi_softintr_t id);

void
i_ddi_remove_intr(dev_info_t *dip, u_int inumber,
    ddi_iblock_cookie_t iblock_cookie);

void
i_ddi_set_parent_private(dev_info_t *dip, caddr_t data);

caddr_t
i_ddi_get_parent_private(dev_info_t *dip);

/*
 *
 */

dev_info_t *
i_ddi_add_child(dev_info_t *, char *, u_int, u_int);

int
i_ddi_remove_child(dev_info_t *, int);

int
i_ddi_initchild(dev_info_t *, dev_info_t *);

/*
 * Implementation specific memory allocation and de-allocation routines.
 */

int
i_ddi_mem_alloc(dev_info_t *dip, ddi_dma_lim_t *limits,
	u_int length, int cansleep, int streaming,
	ddi_device_acc_attr_t *accattrp, caddr_t *kaddrp,
	u_int *real_length, ddi_acc_hdl_t *handlep);

void
i_ddi_mem_free(caddr_t kaddr, int streaming);

/*
 * Search and return properties from the PROM
 */
int
impl_ddi_bus_prop_op(dev_t dev, dev_info_t *dip,
	dev_info_t *ch_dip, ddi_prop_op_t prop_op, int mod_flags,
	char *name, caddr_t valuep, int *lengthp);

/*
 * Copy an integer from PROM to native machine representation
 */
int
impl_ddi_prop_int_from_prom(u_char *intp, int n);


extern int impl_ddi_sunbus_initchild(dev_info_t *);
extern void impl_ddi_sunbus_removechild(dev_info_t *);

extern int impl_ddi_sbus_initchild(dev_info_t *);

/*
 * Implementation specific access handle allocator and init. routines
 */
extern ddi_acc_handle_t impl_acc_hdl_alloc(int (*waitfp)(caddr_t),
	caddr_t arg);
extern void impl_acc_hdl_free(ddi_acc_handle_t handle);

extern ddi_acc_hdl_t *impl_acc_hdl_get(ddi_acc_handle_t handle);
extern void impl_acc_hdl_init(ddi_acc_hdl_t *hp);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DDI_IMPLFUNCS_H */
