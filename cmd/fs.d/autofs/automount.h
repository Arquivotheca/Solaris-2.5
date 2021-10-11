/*
 *	automount.h
 *
 *	Copyright (c) 1988-1994 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#ifndef	_AUTOMOUNT_H
#define	_AUTOMOUNT_H

#pragma ident	"@(#)automount.h	1.14	95/02/06 SMI"

#include <fslib.h>	/* needed for mntlist_t declaration */
#include <synch.h>	/*    "    "  mutex_t */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _REENTRANT
#define	fork1			vfork
#define	rpc_control(a, b)	1
#endif

#define	MXHOSTNAMELEN	64
#define	MAXNETNAMELEN   255
#define	MAXFILENAMELEN  255
#define	LINESZ		4096

#define	MNTTYPE_NFS	"nfs"
#define	MNTTYPE_AUTOFS  "autofs"
#define	MNTOPT_FSTYPE	"fstype"

/* stack ops */
#define	ERASE		0
#define	PUSH		1
#define	POP		2
#define	INIT		3

#define	FNPREFIX	"-xfn"
#define	FNPREFIXLEN	4

struct mapline {
	char linebuf[LINESZ];
	char lineqbuf[LINESZ];
};

/*
 * Structure describing a host/filesystem/dir tuple in a NIS map entry
 */
struct mapfs {
	struct mapfs *mfs_next;	/* next in entry */
	int 	mfs_ignore;	/* ignore this entry */
	char	*mfs_host;	/* host name */
	char	*mfs_dir;	/* dir to mount */
	int	mfs_penalty;	/* mount penalty for this host */
};

/*
 * NIS entry - lookup of name in DIR gets us this
 */
struct mapent {
	char	*map_fstype;	/* file system type e.g. "nfs" */
	char	*map_mounter;	/* base fs e.g. "cachefs" */
	char	*map_root;	/* path to mount root */
	char	*map_mntpnt;	/* path from mount root */
	char	*map_mntopts;	/* mount options */
	struct mapfs *map_fs;	/* list of replicas for nfs */
	struct mapent *map_next;
};


int getmapent();

/*
 * Descriptor for each directory served by the automounter
 */
struct autodir {
	char	*dir_name;		/* mount point */
	char	*dir_map;		/* name of map for dir */
	char	*dir_opts;		/* default mount options */
	int 	dir_direct;		/* direct mountpoint ? */
	int 	dir_remount;		/* a remount */
	struct autodir *dir_next;	/* next entry */
	struct autodir *dir_prev;	/* prev entry */
};

/*
 * This structure is used to build an array of
 * hostnames with associated penalties to be
 * passed to the nfs_cast procedure
 */
struct host_names {
	char *host;
	int  penalty;
};

time_t time_now;	/* set at start of processing of each RPC call */

/*
 * Lock used to serialize sections of daemon that are not MT safe.
 */
extern mutex_t mt_unsafe;

int verbose;
int trace;

extern int add_mnttab(struct mnttab *);
extern struct mapent *parse_entry(char *, char *, char *, struct mapline *);
extern void free_mapent(struct mapent *);
extern int macro_expand(char *, char *, char *, int);
extern void unquote(char *, char *);
void trace_prt(int, char *, ...);

#ifdef __cplusplus
}
#endif

#endif	/* _AUTOMOUNT_H */
