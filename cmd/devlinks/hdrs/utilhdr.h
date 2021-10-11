#ident	"@(#)utilhdr.h	1.2	92/07/14 SMI"
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef _UTILHDR_H
#define _UTILHDR_H


/*
 * Routines defined in utils.h
 */

/*
 * message utilities
 */
void wmessage(const char *message, ...);
void fmessage(const int rc, const char *message, ...);

/*
 * Safe alloc utilities (fmessage's on alloc error)
 */
void *s_calloc(const size_t, const size_t);
void *s_malloc(const size_t);
void *s_realloc(void *, const size_t);
char *s_strdup(const char *);

/*
 * directory-path creation routine
 */
void create_dirs(const char *);

/*
 * devfs-name parsing routines
 */
#define DS_SSSEP	','

const char *getdevname(const char *devnm);
const char *getdevaddr(const char *devnm, const unsigned int fieldno);
const char *getdevminor(const char *devnm, const unsigned int fieldno);

/*
 * substring routine
 */
const char *substring(const char *string, const char sep_char, const int ss_no);

/*
 * directory cacheing data defs
 */
typedef struct {
    const char *name;
    const char *linkto;
} CACHE_ENT;

struct cache_ient {
    CACHE_ENT ent;
    struct cache_ient *next;
};

typedef struct cache_ient CACHE_IENT;

typedef struct {
    char *dirname;
    CACHE_IENT *head;
    CACHE_IENT *current;
    DIR *dp;			/* For first use */
    int state;
} CACHE_DIR;

/*
 * Directory cacheing utilities
 */
CACHE_DIR *cache_opendir(const char *);
const CACHE_ENT *cache_readdir(CACHE_DIR *);
void cache_closedir(CACHE_DIR *);
int cache_unlink(const char *, const char *);
int cache_symlink(const char *, const char *, const char *);

#endif /* _UTILHDR_H */
