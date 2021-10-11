#ident	"@(#)devlinkhdr.h	1.3	93/02/18 SMI"
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

/*
 * Basic idea is to have a single logical data-structure per disk device.
 * This structure contains information both about the devices names in the
 * /dev/[r]mt and /devfs naming systems; so in some sense that data structure
 * represents the links that should be in place between the two name structures.
 *
 * In addition, there is information abot the /dev/[r]SA names used for the
 * device.
 */

#define MAX_MISC	8192

struct miscdev {
    char *unit;			/* The /dev unit part of name */
    char *devfsnm;		/* The devfs identifier */
    int  state;			/* State of link: valid, invalid or missing */
    int	 extra;			/* Index of entry in 'extra' structure */
};

#define LN_VALID	0
#define LN_MISSING	1	/* Link missing */
#define LN_INVALID	2	/* Link invalid */
#define LN_DANGLING	3	/* Link Dangling */

/*
 * Command file definitions
 */

#define TABLE_FILENAME	"/etc/devlink.tab"

#define CF_SEPCHAR	'\t'
#define CF_NMSEP	'='
#define CF_VPSEP	';'

#define CFS_TYPE	"type"
#define CFS_NAME	"name"
#define CFS_ADDR	"addr"
#define CFS_MINOR	"minor"

typedef enum {
    PAT_INVALID, PAT_ABSOLUTE,
    PAT_DERIVED, PAT_COUNTER
    } pattype_t;

#define MAX_DEVFS_COMPMATCH	5

struct subfield {
    int	no;			/* SubField number - 0 means all */
    char *pat;			/* Pointer to pattern string */
};

/*
 * Structure derived from input command file
 */
struct devdef {
    struct {
	char *type;		/* devfs 'type' */
	char *name;		/* devfs module name */
	struct subfield addr[MAX_DEVFS_COMPMATCH];
				/* devfs address field */
	struct subfield minor[MAX_DEVFS_COMPMATCH];
				/* devfs minor field */
    } dfs;
    char devpat[PATH_MAX];	/* dev name prefix (omitting the '/dev/') */
    char extra_linkpat[PATH_MAX];
				/* Extra link spec (link to /dev link) */
    boolean_t devcheck;		/* True if /dev to be checked.  If not,
				   created links automatically overwrite
				   anything present */
    /* Next entries are calculated, not read in */
    pattype_t devpat_type;	/* pattern-match type */
    char devdir[PATH_MAX];	/* dev subdir name prefix (omitting the '/dev/') */
    char *devspec;		/* dev name prefix (omitting the '/dev/') */
    char extra_linkdir[PATH_MAX];
				/* Extra link directory name */
    char *extra_linkspec;	/* Extra link spec (link to /dev link) */
    char pathto_devfs[PATH_MAX]; /* relative path to /dev/devfs */
};

/*
 * Next structure is both the cache entry and the cache header;
 * in the case of the neader, the 'name' filed is the Devinfo device type,
 * in the case of entries it is the actual devfs node name
 */

struct devfscache {
    char *name;			/* Cached name or (in header) devinfo type */
    struct devfscache *next;	/* pointer to next entry of smae type */
};

#include "utilhdr.h"
