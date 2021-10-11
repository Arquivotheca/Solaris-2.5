/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_IA_SWITCH_H
#define	_IA_SWITCH_H

#pragma ident	"@(#)ia_switch.h	1.5	93/04/01 SMI"

#ifdef __cplusplus
extern "C" {
#endif

#define	AUTH_LIB	"/usr/lib/libauth.a"

#define	IA_MAX_ITEMS	64	/*	Max number of items	*/

struct	pam_item {
	int	pi_type;
	void	* pi_addr;
	int	pi_size;
};

struct pam_sdata {
	struct  pam_item ps_item[IA_MAX_ITEMS];
	struct 	pam_sdata *pd_nextp;
};

#ifdef __cplusplus
}
#endif

#endif	/* _IA_SWITCH_H */
