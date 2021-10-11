/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef	_LIBDEVINFO_H
#define	_LIBDEVINFO_H

#pragma ident	"@(#)libdevinfo.h	1.5	93/11/01 SMI"

#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

extern int devfs_find(const char *devtype,
    void (*found)(const char *, const char *,
    const dev_info_t *, struct ddi_minor_data *minor_data,
    struct ddi_minor_data *alias_data), int check_aliases);

extern boolean_t devfs_iscbdriver(const dev_info_t *);

extern const char * local_addr(caddr_t addr);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBDEVINFO_H */
