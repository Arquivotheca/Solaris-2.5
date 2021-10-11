/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_ESUNDDI_H
#define	_SYS_ESUNDDI_H

#pragma ident	"@(#)esunddi.h	1.5	92/08/06 SMI"	/* SVr4.0 */

#include <sys/sunddi.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * esunddi.h:		Function prototypes for kernel ddi functions.
 */

int
e_ddi_prop_create(dev_t dev, dev_info_t *dip, int flag,
	char *name, caddr_t value, int length);

int
e_ddi_prop_modify(dev_t dev, dev_info_t *dip, int flag,
	char *name, caddr_t value, int length);

int
e_ddi_prop_remove(dev_t dev, dev_info_t *dip, char *name);

void
e_ddi_prop_remove_all(dev_info_t *dip);

int
e_ddi_prop_undefine(dev_t dev, dev_info_t *dip, int flag, char *name);

int
e_ddi_getprop(dev_t dev, vtype_t type, char *name, int flags, int defaultval);

int
e_ddi_getproplen(dev_t dev, vtype_t type, char *name, int flags, int *lengthp);

int
e_ddi_getlongprop(dev_t dev, vtype_t type, char *name, int flags,
	caddr_t valuep, int *lengthp);

int
e_ddi_getlongprop_buf(dev_t dev, vtype_t type, char *name, int flags,
	caddr_t valuep, int *lengthp);

dev_info_t *
e_ddi_get_dev_info(dev_t dev, vtype_t type);

int
e_ddi_deferred_attach(major_t maj, dev_t dev);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ESUNDDI_H */
