/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)devfssubr.c	1.4	93/10/12 SMI"

/*
 * This file is broken away from devfswalk.c since it requires that
 * _KERNEL be defined to gain access to structures pointed to by
 * dev_info nodes.
 */
#define	_KERNEL

#include <kvm.h>
#include <sys/devops.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>

#include "libdevinfo.h"

/*
 * nodecheck - check node for device type match
 */
boolean_t
devfs_iscbdriver(const dev_info_t *dev_info)
{
	struct dev_ops *dops;

	if (DEVI(dev_info) == NULL)
		return (B_FALSE);

	if (DEVI(dev_info)->devi_ops == NULL)
		return (B_FALSE);

	dops = (struct dev_ops *)local_addr((caddr_t)DEVI(dev_info)->devi_ops);

 	if (dops->devo_cb_ops == NULL)
		return (B_FALSE);

	return (B_TRUE);
}
