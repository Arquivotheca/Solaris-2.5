/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All Rights Reserved
 */

#ifndef	_GETXBY_DOOR_H
#define	_GETXBY_DOOR_H

#pragma ident	"@(#)getxby_door.h	1.1	94/11/08 SMI"

/*
 * Definitions for client side of doors-based name service caching
 */

#ifdef	__cplusplus
extern "C" {
#endif



#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <grp.h>
#include <pwd.h>


/*
 *	statistics & control structure
 */

typedef struct nsc_stat {
	int 	nsc_pos_cache_hits;	/* hits on real entries */
	int	nsc_neg_cache_hits;	/* hits on hegative entries */
	int	nsc_pos_cache_misses;	/* miss that results in real entry */
	int	nsc_neg_cache_misses;	/* miss that results in neg entry */
	int	nsc_entries;		/* count of cache entries */
	int     nsc_throttle_count;     /* count of load shedding */
	int	nsc_suggestedsize;	/* suggested size */
	int	nsc_enabled;		/* if 0, always return NOSERVER */
	int	nsc_invalidate;		/* command to invalidate cache */
	int	nsc_pos_ttl;		/* time to live for positive entries */
	int	nsc_neg_ttl;		/* time to live for negative entries */
	short	nsc_keephot;		/* number of entries to keep hot */
	short	nsc_old_data_ok;	/* set if expired data is acceptable */
	short	nsc_check_files;	/* set if file time should be checked */
} nsc_stat_t;


/*
 * structure returned by server for all calls
 */

typedef struct {
	int 		nsc_bufferbytesused;
	int 		nsc_return_code;
	int 		nsc_errno;

	union {
		struct passwd 	pwd;
		struct group  	grp;
		struct hostent 	hst;
		nsc_stat_t 	stats;
		char 		buff[4];
	} nsc_u;

} nsc_return_t;

/*
 * calls look like this
 */

typedef struct {
	int nsc_callnumber;
	union {
		uid_t uid;
		gid_t gid;
		char name[sizeof (int)]; 	/* size is indeterminate */
		struct {
			int  a_type;
			int  a_length;
			char a_data[sizeof (int)];
		} addr;
	} nsc_u;
} nsc_call_t;
/*
 * how the client views the call process
 */

typedef union {
	nsc_call_t 		nsc_call;
	nsc_return_t 		nsc_ret;
	char 			nsc_buff[sizeof (int)];
} nsc_data_t;

/*
 *  What each entry in the nameserver cache looks like.
 */

typedef struct {
	int		nsc_hits;	/* number of hits */
	int		nsc_status;	/* flag bits */
	time_t		nsc_timestamp;	/* last time entry validated */
	int 		nsc_refcount;	/* reference count 		*/
	nsc_return_t	nsc_data;	/* data returned to client	*/
} nsc_bucket_t;

typedef struct hash_entry {
	struct hash_entry *next_entry;
	struct hash_entry *right_entry;
	struct hash_entry *left_entry;
	char 	*key;
	char 	*data;
} hash_entry_t;

typedef struct hash {
	int 		size;
	hash_entry_t	**table;
	hash_entry_t	*start;
	enum hash_type {
		String_Key = 0, Integer_Key = 1
	} hash_type;
} hash_t;

typedef struct passwd_cache {
	hash_entry_t	passwd;
} passwd_cache_t;

typedef struct group_cache {
	hash_entry_t	group;
} group_cache_t;

typedef struct host_cache {
	hash_entry_t	host;
} host_cache_t;

/*
 *	structure to handle waiting for pending name service requests
 */

typedef struct waiter {
	cond_t	w_waitcv;
	char **		w_key;
	struct waiter * w_next, * w_prev;
} waiter_t;




hash_t * 	make_hash();
hash_t * 	make_ihash();
char 		**get_hash();
char 		**find_hash();
char 		*del_hash();
int  		operate_hash();
void 		destroy_hash();


int
nscd_wait(waiter_t * wchan,  mutex_t * lock, char ** key);
int
nscd_signal(waiter_t * wchan, char ** key);

/*
 * OR'D in by server to call self for updates
 */

#define	UPDATEBIT	(1<<30)
#define	MASKUPDATEBIT(a) ((~UPDATEBIT)&(a))

#define	NULLCALL	0
#define	GETPWUID 	1
#define	GETPWNAM	2
#define	GETGRNAM	3
#define	GETGRGID	4
#define	GETHOSTBYNAME	5
#define	GETHOSTBYADDR	6

/*
 * administrative calls
 */

#define	KILLSERVER	7
#define	GETADMIN	8
#define	SETADMIN	9

/*
 * debug levels
 */

#define	DBG_OFF		0
#define	DBG_CANT_FIND	2
#define	DBG_NETLOOKUPS	4
#define	DBG_ALL		6

/*
 * flag  bits
 */

#define	ST_UPDATE_PENDING	1


/*
 * defines for client-server interaction
 */

#define	NAME_SERVICE_DOOR_VERSION 1
#define	NAME_SERVICE_DOOR "/etc/.name_service_door"
#define	NAME_SERVICE_DOOR_COOKIE ((void*)(0xdeadbeef^NAME_SERVICE_DOOR_VERSION))
#define	UPDATE_DOOR_COOKIE ((void*)(0xdeadcafe)

#define	SUCCESS		0
#define	NOTFOUND  	-1
#define	CREDERROR 	-2
#define	SERVERERROR 	-3
#define	NOSERVER 	-4

int
_nsc_trydoorcall(nsc_data_t **dptr, int *ndata, int *adata);

struct passwd *
_uncached_getpwuid_r(uid_t uid, struct passwd *result, char *buffer,
	int buflen);

struct passwd *
_uncached_getpwnam_r(const char *name, struct passwd *result, char *buffer,
	int buflen);

struct group *
_uncached_getgrnam_r(const char *name, struct group *result, char *buffer,
    int buflen);

struct group *
_uncached_getgrgid_r(gid_t gid, struct group *result, char *buffer, int buflen);


#ifdef	__cplusplus
}
#endif


#endif	/* _GETXBY_DOOR_H */
