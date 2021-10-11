
/*	@(#)backupdb.h 1.5 91/12/20	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*
 * the files which comprise a database.  dnode, header and pathfile
 * all have unique dumpids concatenated on the end of the name.
 */
#ifndef BACKUPDB_H
#define	BACKUPDB_H

#define	TAPEFILE	"activetapes"
#define	DIRFILE		"dir"
#define	INSTANCEFILE	"instance"
#define	DNODEFILE	"dnode"
#define	HEADERFILE	"header"
#define	PATHFILE	"pathcomponent"
#define	LINKFILE	"symlinks"

/*
 * this lets a user over-ride the default number
 * of entries per instance record.  This is only consulted when
 * we're creating an instance file - otherwise the number of entries
 * per record has already been determined.
 */
#define	INSTANCECONFIG	".instancerc"

#define	UTIL_LOCKFILE	".dbutil_lock"
#define	DBSERV_LOCKFILE	".dbserv_lock"

/*
 * temporary files used during database update.
 */
#define	TEMP_PREFIX		"T."
#define	TRANS_SUFFIX		".trans"
#define	MAP_SUFFIX		".map"
#define	UPDATE_FILE		"batch_update"
#define	UPDATE_INPROGRESS	"update.inprogress"
#define	UPDATE_DONE		"update.done"
#define	TAPE_UPDATE		"tape_update"
#define	TAPE_UPDATEDONE		"tape_update.done"
#define	DELETE_TAPE		"delete_tape"
#define	HOST_RENAME		"rename_host"

#define	NONEXISTENT_BLOCK	(u_long)-1

#define	MAXFILESIZE		0x7fffffff	/* 2 GB */
#define	MAPSIZE			0x100000	/* map files 1MB at a time */

#ifdef __STDC__
extern char *getmapblock(char **, char **, u_long  *, u_long *,
	u_long, int, int, int, int, int *);
extern void release_map(char *, int);
#else
extern char *getmapblock();
extern void release_map();
#endif
#endif
