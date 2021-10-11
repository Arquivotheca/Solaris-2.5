/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _CIS_PROTOS_H
#define	_CIS_PROTOS_H

#pragma ident	"@(#)cis_protos.h	1.11	95/07/17 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This file contains all of the function prototypes for functions
 *	used by the CIS interpreter.
 *
 * Prototypes for general functions
 */
int		cis_list_create(cistpl_callout_t *, volatile cisptr_t *,
								cistpl_t **);
int		cis_list_destroy(cistpl_t **);
char		*cis_tuple_text(cistpl_callout_t *, cisdata_t,  cisdata_t);
cistpl_t	*cis_get_ltuple(cistpl_t *, cisdata_t, int);
ulong_t		cistpl_devspeed(cistpl_t *, cisdata_t, int);
ulong_t		cistpl_expd_parse(cistpl_t *, int *);
int		cis_convert_devspeed(convert_speed_t *);
int		cis_convert_devsize(convert_size_t *);
int		cis_validate_longlink_ac(volatile cisptr_t *);

/*
 * Prototypes for the tuple handlers
 */
int	cis_tuple_handler(cistpl_callout_t *, cistpl_t *, int,
							void *, cisdata_t);
int	cis_no_tuple_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_vers_1_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_config_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_device_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_cftable_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_jedec_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_vers_2_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_format_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_geometry_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_byteorder_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_date_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_battery_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_org_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_funcid_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_funce_serial_handler(cistpl_callout_t *, cistpl_t *,
								int, void *);
int	cistpl_funce_lan_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_manfid_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_linktarget_handler(cistpl_callout_t *, cistpl_t *, int, void *);
int	cistpl_longlink_ac_handler(cistpl_callout_t *, cistpl_t *, int, void *);

u_char 	*cis_getstr(cistpl_t *);

#ifdef	_KERNEL
caddr_t	cis_malloc(size_t);
void	cis_free(caddr_t);
#endif	_KERNEL

#ifdef	__cplusplus
}
#endif

#endif	/* _CIS_PROTOS_H */
