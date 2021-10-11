/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

/*
 * a collection of usefule debug defines and macros
 */

#pragma ident	"@(#)bootdebug.h	1.1	94/08/01 SMI"

/* #define	COMPFS_OPS_DEBUG */
/* #define	PCFS_OPS_DEBUG */
/* #define	HSFS_OPS_DEBUG */
/* #define	UFS_OPS_DEBUG */
/* #define	NFS_OPS_DEBUG */
/* #define	CFS_OPS_DEBUG */
/* #define	VERIFY_HASH_REALLOC */

#include <sys/reboot.h>

extern int boothowto;			/* What boot options are set */
extern int verbosemode;
#define	DBFLAGS	(RB_DEBUG | RB_VERBOSE)
