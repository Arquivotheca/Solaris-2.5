/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)server_door.h	1.1	94/12/05 SMI"

/*
 * Definitions for server side of doors-based name service caching
 */

#ifndef	_SERVER_DOOR_H
#define	_SERVER_DOOR_H

typedef struct admin {
	nsc_stat_t	passwd;
	nsc_stat_t	group;
	nsc_stat_t	host;
	int		debug_level;
	int	       	avoid_nameservice;/* set to true for disconnected op */
	int		ret_stats;	/* return status of admin calls */
	char		logfile[128];	/* debug file for logging */
} admin_t;


extern struct group *_uncached_getgrgid_r(gid_t, struct group *, char *, int);

extern struct group *_uncached_getgrnam_r(const char *, struct group *,
    char *, int);

extern struct passwd *_uncached_getpwuid_r(uid_t, struct passwd *, char *, int);

extern struct passwd *_uncached_getpwnam_r(const char *, struct passwd *,
    char *, int);

extern struct hostent  *_uncached_gethostbyname_r(const char *, struct hostent *,
   char *, int, int *h_errnop);

extern struct hostent  *_uncached_gethostbyaddr_r(const char *, int, int,
    struct hostent *, char *, int, int *h_errnop);

extern int _nsc_trydoorcall(nsc_data_t **dptr, int *ndata, int *adata);

#endif /* _SERVER_DOOR_H */





