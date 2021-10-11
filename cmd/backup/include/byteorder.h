/*
 * Copyright (c) 1990-1994 by Sun Microsystems, Inc.
 */

#ifndef	_BYTEORDER_H
#define	_BYTEORDER_H

#pragma ident	"@(#)byteorder.h	1.8	94/08/10 SMI"

#include <stdio.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/fs/ufs_acl.h>
#include <protocols/dumprestore.h>

struct byteorder_ctx {
	int initialized;
	int Bcvt;
};

#ifdef __STDC__
extern struct byteorder_ctx *byteorder_create(void);
extern void byteorder_destroy(struct byteorder_ctx *);
extern void byteorder_banner(struct byteorder_ctx *, FILE *);
extern void swabst(char *, char *);
extern long swabl(long);
extern int normspcl(struct byteorder_ctx *, struct s_spcl *, int *, int);
extern void normdirect(struct byteorder_ctx *, struct direct *);
extern void normacls(struct byteorder_ctx *, ufs_acl_t *, int);
#else /* __STDC__ */
extern struct byteorder_ctx *byteorder_create();
extern void byteorder_destroy();
extern void byteorder_banner();
extern void swabst();
extern long swabl();
extern int normspcl();
extern void normdirect();
extern void normacls();
#endif /* __STDC__ */

#endif /* _BYTEORDER_H */
