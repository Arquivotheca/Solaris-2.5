/*
 * Copyright (c) 1991, by Sun Microsystems Inc.
 */

/*
 * This header file defines the interface to the NIS database. All
 * implementations of the database must export at least these routines.
 * They must also follow the conventions set herein. See the implementors
 * guide for specific semantics that are required.
 */

#ifndef	_RPCSVC_NIS_DB_H
#define	_RPCSVC_NIS_DB_H

#pragma ident	"@(#)nis_db.h	1.8	94/01/06 SMI"

#include <rpc/rpc.h>
#include <rpcsvc/nis.h>

#ifdef	__cplusplus
extern "C" {
#endif

enum db_status {
	DB_SUCCESS = 0,
	DB_NOTFOUND = 1,
	DB_NOTUNIQUE = 2,
	DB_BADTABLE = 3,
	DB_BADQUERY = 4,
	DB_BADOBJECT = 5,
	DB_MEMORY_LIMIT = 6,
	DB_STORAGE_LIMIT = 7,
	DB_INTERNAL_ERROR = 8
};
typedef enum db_status db_status;

enum db_action {
	DB_LOOKUP = 0,
	DB_REMOVE = 1,
	DB_ADD = 2,
	DB_FIRST = 3,
	DB_NEXT = 4,
	DB_ALL = 5,
	DB_RESET_NEXT = 6
};
typedef enum db_action db_action;

typedef entry_obj *entry_object_p;

typedef struct {
	u_int db_next_desc_len;
	char *db_next_desc_val;
} db_next_desc;

struct db_result {
	db_status status;
	db_next_desc nextinfo;
	struct {
		u_int objects_len;
		entry_object_p *objects_val;
	} objects;
	long ticks;
};
typedef struct db_result db_result;

/*
 * Prototypes for the database functions.
 */

#if (__STDC__)

extern bool_t db_initialize(char *);
extern db_status db_create_table(char *, table_obj *);
extern db_status db_destroy_table(char *);
extern db_result *db_first_entry(char *, int, nis_attr *);
extern db_result *db_next_entry(char *, db_next_desc *);
extern db_result *db_reset_next_entry(char *, db_next_desc *);
extern db_result *db_list_entries(char *, int, nis_attr *);
extern db_result *db_add_entry(char *, int,  nis_attr *, entry_obj *);
extern db_result *db_remove_entry(char *, int, nis_attr *);
extern db_status db_checkpoint(char *);
extern db_status db_standby(char *);
extern db_status db_table_exists(char *);
extern db_status db_unload_table(char *);
extern void db_free_result(db_result *);

#else /* Non-prototype definitions */

extern bool_t db_initialize();
extern db_status db_create_table();
extern db_status db_destroy_table();
extern db_result *db_first_entry();
extern db_result *db_next_entry();
extern db_result *db_reset_next_entry();
extern db_result *db_list_entries();
extern db_result *db_add_entry();
extern db_result *db_remove_entry();
extern db_status db_checkpoint();
extern db_status db_standby();
extern db_status db_table_exists();
extern db_status db_unload_table();
extern void db_free_result();

#endif  /* __STDC__ */

#ifdef __cplusplus
}
#endif

#endif	/* _RPCSVC_NIS_DB_H */
