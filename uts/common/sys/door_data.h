/*
 * Copyright (c) 1994 Sun Microsystems, Inc.
 * All rights reserved.
 *
 * The I/F's described herein are expermental, highly volatile and
 * intended at this time only for use with Sun internal products.
 * SunSoft reserves the right to change these definitions in a minor
 * release.
 */

#ifndef	_SYS_DOOR_DATA_H
#define	_SYS_DOOR_DATA_H

#pragma ident	"@(#)door_data.h	1.4	95/04/17 SMI"

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(_KERNEL)
/*
 * Data associated with a door invocation
 */
struct _kthread;
struct door_node;
struct file;

typedef struct door_data {
	struct _kthread	*d_caller;	/* Door caller */
	struct _kthread *d_servers;	/* List of door servers */
	struct door_node *d_active;	/* Active door */
	caddr_t		d_buf;		/* Arg/result + descriptors */
	int		d_bsize;	/* Buffer size */
	int		d_asize;	/* Arg size */
	int		d_ndid;		/* Number of descriptors */
	int		d_error;	/* Error (if any) */
	int		d_fpp_size;	/* Number of File ptrs */
	caddr_t		d_overflow;	/* Overflow address */
	u_int		d_olen;		/* Overflow buffer length */
	struct file	**d_fpp;	/* File ptrs  */
	kcondvar_t	d_cv;
	u_short		d_flag;		/* keep client/server from exiting */
} door_data_t;

#define	DOOR_HOLD	0x01		/* Hold on to client/server */
#define	DOOR_WAITING	0x02		/* Client/server is waiting */

#endif	/* defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DOOR_DATA_H */
