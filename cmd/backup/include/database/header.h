
/*	@(#)header.h 1.10 91/12/20	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ifndef HEADER_H
#define	HEADER_H

#include <rpc/rpc.h>
#include <sys/param.h>				/* for MAXPATHLEN */
#include <sys/time.h>				/* for vnode.h */
#include <sys/vnode.h>				/* for inode.h */
#ifdef USG
#include <sys/fs/ufs_inode.h>			/* for dumprestore.h */
#else
#include <ufs/inode.h>				/* for dumprestore.h */
#endif
#include <protocols/dumprestore.h>		/* for LBLSIZE */

struct dheader {
	char	dh_host[NAMELEN];		/* dumped host */
	u_long	dh_netid;			/* its internet id */
	char	dh_dev[NAMELEN];		/* dumped device name */
	char	dh_mnt[MAXPATHLEN];		/* mnt pt of dumped fs */
	time_t	dh_time;			/* time of dump */
	time_t	dh_prvdumptime;			/* time of prev lev <N dump */
	u_long	dh_level;			/* level of dump */
	u_long	dh_flags;			/* see defines below */
	u_long	dh_position;			/* file # on first tape */
	u_long	dh_ntapes;			/* # of tapes for this dump */
	char	dh_label[1][LBLSIZE];		/* labels of `ntapes' tapes */
};

/*
 * values for `dh_flags'
 */
#define	DH_ACTIVE		1		/* this dump contains only */
						/* files that were active  */
						/* during a previous dump  */

#define	DH_PARTIAL		2		/* a partial fs dump -- don't */
						/* use it in full restores */

#define	DH_TRUEINC		4		/* a "true incremental" as  */
						/* opposed to a traditional */
						/* level 9 dump		    */

#define	DH_EMPTY		8		/* empty dump - the header  */
						/* exists to allow us to    */
						/* follow `dh_prvdumptime'  */

struct dumplist {
	struct	dheader *h;
	u_long	dumpid;
	struct	dumplist *nxt;
};

struct mntpts {
	char	*mp_name;
	int	mp_namelen;
	struct	mntpts *nxt;
};

#ifdef __STDC__
extern bool_t xdr_dheader(XDR *, struct dheader *);
extern bool_t xdr_fullheader(XDR *, struct dheader *);
#else
extern bool_t xdr_dheader();
extern bool_t xdr_fullheader();
#endif
#endif
