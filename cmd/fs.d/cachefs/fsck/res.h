/*
 *
 *			res.h
 *
 *   Defines routines to operate on the resource file.
 */

#pragma ident "@(#)res.h   1.5     94/11/11 SMI"

/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *	All rights reserved.
 */

#ifndef	cfs_fsck_res_h
#define	cfs_fsck_res_h

typedef struct res res;

res *res_create(char *namep, int entries);
void res_destroy(res *resp);
int res_done(res *resp);
void res_addfile(res *resp, long nbytes);
int res_addident(res *resp, int index, int fsid, ino_t fileno, int local,
    int attrc, int onlru, long nbytes);
int res_clearident(res *resp, int index, int lru, int nbytes);

#endif /* cfs_fsck_res_h */
