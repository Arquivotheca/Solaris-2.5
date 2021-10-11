/*
 *	ns_fn.c
 *
 *	Copyright (c) 1994 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)ns_fn.c	1.3	95/01/12 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <syslog.h>
#include <dlfcn.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>
#include <xfn/xfn.h>
#include "automount.h"
#include "autofs_prot.h"


/*
 * The maximum sizes of map names, key names, composite names, and status
 * descriptions, including the trailing '\0'.
 */
#define	MAPNAMESZ	(size_t)(A_MAXNAME + 1)
#define	KEYNAMESZ	(size_t)(A_MAXNAME + 1)
#define	COMPNAMESZ	(size_t)(MAPNAMESZ - FNPREFIXLEN + KEYNAMESZ - 1)
#define	DESCSZ		(size_t)512


/*
 * Number of the home directory field in NIS+ password tables.
 */
#define	NIS_HOME	5


typedef struct mapent	mapent;
typedef struct mapline	mapline;


/*
 * The FNS functions are normally linked in with "-lxfn".  We dlsym()
 * them instead to avoid a dependency on libxfn.so and an implicit
 * dependency on libC.so.  The need for this should go away some day.
 */

static FN_attribute_t *(*fn_attr_get_)(FN_ctx_t *ctx,
    const FN_composite_name_t *name, const FN_identifier_t *attr_id,
    FN_status_t *status);

static void (*fn_attribute_destroy_)(FN_attribute_t *);

static void (*fn_bindinglist_destroy_)(FN_bindinglist_t *bl,
    FN_status_t *status);

static FN_string_t *(*fn_bindinglist_next_)(FN_bindinglist_t *bl,
    FN_ref_t **ref, FN_status_t *status);

static void (*fn_composite_name_destroy_)(FN_composite_name_t *);

static FN_composite_name_t *(*fn_composite_name_from_string_)(
    const FN_string_t *);

static void (*fn_ctx_handle_destroy_)(FN_ctx_t *ctx);

static FN_ctx_t *(*fn_ctx_handle_from_initial_)(FN_status_t *status);

static FN_ctx_t *(*fn_ctx_handle_from_ref_)(const FN_ref_t *ref,
    FN_status_t *status);

static FN_bindinglist_t *(*fn_ctx_list_bindings_)(FN_ctx_t *ctx,
    const FN_composite_name_t *name, FN_status_t *status);

static FN_ref_t *(*fn_ctx_lookup_)(FN_ctx_t *ctx,
    const FN_composite_name_t *name, FN_status_t *status);

static const void *(*fn_ref_addr_data_)(const FN_ref_addr_t *addr);

static size_t (*fn_ref_addr_length_)(const FN_ref_addr_t *addr);

static const FN_identifier_t *(*fn_ref_addr_type_)(const FN_ref_addr_t *addr);

static void (*fn_ref_destroy_)(FN_ref_t *ref);

static const FN_ref_addr_t *(*fn_ref_first_)(const FN_ref_t *ref,
    void **iter_pos);

static const FN_ref_addr_t *(*fn_ref_next_)(const FN_ref_t *ref,
    void **iter_pos);

static const FN_identifier_t *(*fn_ref_type_)(const FN_ref_t *ref);

static unsigned int (*fn_status_code_)(const FN_status_t *);

static FN_status_t *(*fn_status_create_)(void);

static FN_string_t *(*fn_status_description_)(const FN_status_t *,
    unsigned int detail, unsigned int *more_detail);

static void (*fn_status_destroy_)(FN_status_t *);

static int (*fn_status_is_success_)(const FN_status_t *);

static unsigned int (*fn_status_link_code_)(const FN_status_t *);

static void (*fn_string_destroy_)(FN_string_t *);

static FN_string_t *(*fn_string_from_str_)(const unsigned char *str);

static const unsigned char *(*fn_string_str_)(const FN_string_t *str,
    unsigned int *status);

#define	FNSYM(name) {(void **)&name ## _, #name}

static struct {
	void		**sym;
	const char	*name;
} fn_syms[] = {
	FNSYM(fn_attr_get),
	FNSYM(fn_attribute_destroy),
	FNSYM(fn_bindinglist_destroy),
	FNSYM(fn_bindinglist_next),
	FNSYM(fn_composite_name_destroy),
	FNSYM(fn_composite_name_from_string),
	FNSYM(fn_ctx_handle_destroy),
	FNSYM(fn_ctx_handle_from_initial),
	FNSYM(fn_ctx_handle_from_ref),
	FNSYM(fn_ctx_list_bindings),
	FNSYM(fn_ctx_lookup),
	FNSYM(fn_ref_addr_data),
	FNSYM(fn_ref_addr_length),
	FNSYM(fn_ref_addr_type),
	FNSYM(fn_ref_destroy),
	FNSYM(fn_ref_first),
	FNSYM(fn_ref_next),
	FNSYM(fn_ref_type),
	FNSYM(fn_status_code),
	FNSYM(fn_status_create),
	FNSYM(fn_status_description),
	FNSYM(fn_status_destroy),
	FNSYM(fn_status_is_success),
	FNSYM(fn_status_link_code),
	FNSYM(fn_string_destroy),
	FNSYM(fn_string_from_str),
	FNSYM(fn_string_str),
};


/*
 * FNS file system reference and address types.  Each (char *) array is indexed
 * using the corresponding enumeration.
 */
static char *reftypes[] = {
	"onc_fn_fs",
};
typedef enum {
	REF_FN_FS,
	NUM_REFTYPES	/* Not a ref type, but rather a count of them */
} reftype_t;

static char *addrtypes[] = {
	"onc_fn_fs_mount",
	"onc_fn_fs_host",
	"onc_fn_fs_user_nisplus",
};
typedef enum {
	ADDR_MOUNT,
	ADDR_HOST,
	ADDR_USER_NISPLUS,
	NUM_ADDRTYPES	/* Not an addr type, but rather a count of them */
} addrtype_t;


/*
 * The name of an attribute.
 */
static const FN_identifier_t attr_exported = {FN_ID_STRING, 8, "exported"};


/*
 * Given a request to mount the name "key" under map/context "map", and
 * a set of default mount options, return a list of mapents giving the
 * mounts that need to be performed.  (The actual declaration is in
 * "automount.h".)
 *
 *	mapent *
 *	getmapent_fn(char *key, char *map, char *opts);
 */

/*
 * Initialization for FNS.  Return 0 on success.
 */
static int
init_fn(void);

/*
 * Given a reference, its composite name, default mount options, and a
 * mapent root, return a list of mapents to mount.  The map and key
 * strings are pieces of the composite name such that
 * "FNPREFIX/cname" == "map/key/".
 */
static mapent *
process_ref(const FN_ref_t *ref, const char *cname, char *map, char *key,
    char *opts, char *root, FN_status_t *status);

/*
 * Traverse the namespace to find a frontier below ref along which
 * future mounts may need to be triggered.  Add to mapents the
 * corresponding direct autofs mount points.
 *     map:	map name for ref
 *     maplen:	strlen(map)
 *     mntpnt:	suffix of map where the current mount request begins
 *		(starts off as "", and grows as we traverse the namespace)
 *     opts:	default mount options
 *     status:	passed from above to avoid having to allocate one on each call
 * Works by calling frontier_aux() on each name bound under ref.
 * Return the new mapents, or free mapents and return NULL on failure.
 */
static mapent *
frontier(mapent *mapents, const FN_ref_t *ref, char *map, size_t maplen,
    char *mntpnt, char *opts, FN_status_t *status);

/*
 * Called by frontier(), once for each "name" that it finds.  map is
 * passed unchanged from frontier().  ref is the reference named by
 * "map/name".  If ref is found to be along the frontier, add the
 * corresponding direct autofs mount point to mapents.  Otherwise
 * continue traversing the namespace to find the frontier.  Other
 * arguments and the return value are as for frontier().
 */
static mapent *
frontier_aux(mapent *mapents, const FN_ref_t *ref, char *map, size_t maplen,
    char *mntpnt, const char *name, char *opts, FN_status_t *status);

/*
 * Given a reference with an address type of ADDR_HOST and its
 * composite name, check the attr_exported attribute to determine if
 * the corresponding directory is exported.  Return FALSE on error.
 */
static bool_t
exported(const FN_ref_t *ref, const char *cname, FN_status_t *status);

/*
 * Find a reference's address type and, if "data" is not NULL, its
 * data string.  If there is no address of a known type, set *typep to
 * NUM_ADDRTYPES; if there are several, stop after finding the first.
 * Return 0 on success.
 */
static int
addr_from_ref(const FN_ref_t *ref, const char *cname, addrtype_t *typep,
    char *data, size_t datasz);

/*
 * Return the type of a reference, or NUM_REFTYPES if the type is unknown.
 */
static reftype_t
reftype(const FN_ref_t *ref);

/*
 * Return the type of an address, or NUM_ADDRTYPES if the type is unknown.
 */
static addrtype_t
addrtype(const FN_ref_addr_t *addr);

/*
 * Decode an address's data into a string.  Return 0 on success.
 */
static int
str_from_addr(const char *cname, const FN_ref_addr_t *addr, char str[],
    size_t strsz);

/*
 * Determine whether an identifier and a string match.
 */
static bool_t
ident_str_equal(const FN_identifier_t *id, const char *str);

/*
 * Allocate a new composite name.  On error, log an error message and
 * return NULL.
 */
static FN_composite_name_t *
new_cname(const char *str);

/*
 * Syslog an error message and status info (with detail level DETAIL)
 * if "verbose" is set.
 */
#define	DETAIL	0
static void
logstat(const FN_status_t *status, const char *msg1, const char *msg2);

/*
 * Determine whether an error is potentially transient.
 */
static bool_t
transient(const FN_status_t *status);

/*
 * Perform a NIS+ query to find a home directory.  The result is a
 * newly-allocated string, or NULL on error.
 */
static char *
nisplus_homedir(const char *nisname);

/*
 * Given a map name and its current length, append "/name".  Return
 * the new length.  On error, syslog a warning and return 0.
 */
static size_t
append_mapname(char *map, size_t maplen, const char *name);

/*
 * Concatenate two strings using the given separator.  The result is a
 * newly-allocated string, or NULL on error.
 */
static char *
concat(const char *s1, char sep, const char *s2);

/*
 * Trim comments and trailing whitespace from ml->linebuf, then
 * unquote it and leave the result in ml.  Return 0 on success.
 */
static int
trim_line(mapline *ml);

/*
 * Determine whether ml contains an option string (such as "-ro") and
 * nothing else.
 */
static bool_t
opts_only(const mapline *ml);

/*
 * Allocate a new mapent structure.  The arguments must have been
 * malloc'ed, and are owned by the mapent; they are freed if
 * new_mapent() fails.  If any argument is NULL, the call fails and a
 * memory allocation failure is logged.  A root argument of "noroot"
 * indicates that the map_root field does not need to be set (it's
 * only needed in the first of a list of mapents).
 */
static char *noroot = "[no root]";
static mapent *
new_mapent(char *root, char *mntpnt, char *fstype, char *mntopts, char *host,
    char *dir);

/*
 * Automounter utilities used in this file and not declared in "automount.h".
 */
extern void free_mapent(mapent *);


static FN_ctx_t			*init_ctx = NULL;	/* initial context */
static FN_composite_name_t	*empty_cname = NULL;	/* will be set to "" */


mapent *
getmapent_fn(char *key, char *map, char *opts)
{
	size_t			maplen;
	FN_status_t		*status;
	int			statcode;
	char			cname[COMPNAMESZ];
	FN_composite_name_t	*compname;
	FN_ref_t		*ref;
	char			mapname[MAPNAMESZ];
	char			*root;
	mapent			*mapents;

	if (init_fn() != 0) {
		return (NULL);
	}

	/*
	 * For direct mounts, the key is the entire path, and the map
	 * name already has the final key component appended.  Split
	 * apart the map name and key.  The "root" of the mapent is
	 * "/key" for indirect mounts, and "" for direct mounts.
	 */
	strcpy(mapname, map);
	if (key[0] == '/') {
		key = strrchr(key, '/') + 1;
		*strrchr(mapname, '/') = '\0';
		root = strdup("");
	} else {
		root = concat("", '/', key);
	}
	map = mapname;
	maplen = strlen(map);

	if ((maplen - FNPREFIXLEN + strlen(key) + 1) >= COMPNAMESZ) {
		if (verbose) {
			syslog(LOG_ERR, "name %s/%s too long", map, key);
		}
		return (NULL);
	}
	if (maplen == FNPREFIXLEN) {
		sprintf(cname, "%s/", key);
	} else {
		sprintf(cname, "%s/%s/", map + FNPREFIXLEN + 1, key);
	}

	if ((compname = new_cname(cname)) == NULL) {
		return (NULL);
	}

	status = fn_status_create_();
	if (status == NULL) {
		if (verbose) {
			syslog(LOG_ERR, "Could not create FNS status object");
		}
		return (NULL);
	}

	ref = fn_ctx_lookup_(init_ctx, compname, status);
	fn_composite_name_destroy_(compname);
	statcode = fn_status_code_(status);

	if (trace > 1) {
		trace_prt(1, "  FNS traversal: %s\n", cname);
	}

	if (ref == NULL) {
		if ((statcode != FN_E_NAME_NOT_FOUND) &&
		    (statcode != FN_E_NOT_A_CONTEXT)) {
			logstat(status, "lookup failed on", cname);
		}
		fn_status_destroy_(status);
		return (NULL);
	}

	mapents = process_ref(ref, cname, map, key, opts, root, status);
	fn_ref_destroy_(ref);
	fn_status_destroy_(status);
	return (mapents);
}


static int
init_fn(void)
{
	static mutex_t	init_ctx_lock = DEFAULTMUTEX;
	static void	*libxfn = NULL;
	FN_status_t	*status;
	int		i;

	if (init_ctx != NULL) {
		return (0);
	}

	mutex_lock(&init_ctx_lock);

	/* Load FNS symbols */

	if (libxfn == NULL) {
		libxfn = dlopen("libxfn.so", RTLD_LAZY);
		if (libxfn == NULL) {
			syslog(LOG_ERR, "dlopen(\"libxfn.so\") failed");
			goto unlock;
		}
		for (i = 0; i < sizeof (fn_syms) / sizeof (fn_syms[0]); i++) {
			*fn_syms[i].sym = dlsym(libxfn, fn_syms[i].name);
			if (*fn_syms[i].sym == NULL) {
				syslog(LOG_ERR, "dlsym(\"%s\") failed",
					fn_syms[i].name);
				dlclose(libxfn);
				libxfn = NULL;
				goto unlock;
			}
		}
	}

	status = fn_status_create_();
	if (status == NULL) {
		if (verbose) {
			syslog(LOG_ERR, "Could not create FNS status object");
		}
		goto unlock;
	}

	if (empty_cname == NULL) {
		if ((empty_cname = new_cname("")) == NULL) {
			fn_status_destroy_(status);
			goto unlock;
		}
	}
	if (init_ctx == NULL) {
		init_ctx = fn_ctx_handle_from_initial_(status);
		if (init_ctx == NULL) {
			logstat(status, "", "No initial context");
		}
	}
	fn_status_destroy_(status);
unlock:
	mutex_unlock(&init_ctx_lock);

	return ((init_ctx != NULL) ? 0 : -1);
}


static mapent *
process_ref(const FN_ref_t *ref, const char *cname, char *map, char *key,
    char *opts, char *root, FN_status_t *status)
{
	addrtype_t	addrtype;
	mapline		ml;
	char		*addrdata = ml.linebuf;
	mapent		*mapents;
	char		*homedir;
	size_t		maplen;
	char		*colon;
	char		*nfshost;
	char		*nfsdir;

	if ((reftype(ref) < NUM_REFTYPES) &&
	    (addr_from_ref(ref, cname, &addrtype, addrdata, LINESZ) == 0)) {

		switch (addrtype) {
		case ADDR_MOUNT:
			if (trim_line(&ml) != 0) {
				return (NULL);
			}
			if (opts_only(&ml)) {
				if (macro_expand("&", ml.linebuf,
				    ml.lineqbuf, LINESZ)) {
					syslog(LOG_ERR,
					"%s/%s: opts too long (max %d chars)",
					FNPREFIX, cname, LINESZ - 1);
					return (NULL);
				}
				opts = ml.linebuf + 1;	/* skip '-' */
				goto indirect;
			}
			mapents = parse_entry(key, map, opts, &ml);
			if (mapents == NULL) {
				return (NULL);
			}
			free(mapents->map_root);
			mapents->map_root = root;
			break;

		case ADDR_HOST:
			/* Check if file system is exported */
			if (! exported(ref, cname, status)) {
				if (transient(status)) {
					return (NULL);
				} else {
					goto indirect;
				}
			}
			/*
			 * Do an NFS mount.  Address is of the form "host:dir".
			 * If "dir" is not supplied, it defaults to "/".
			 */
			colon = strchr(addrdata, ':');
			if (colon == NULL) {
				nfsdir = strdup("/");
			} else {
				*colon = '\0';
				nfsdir = strdup(colon + 1);
			}
			nfshost = strdup(addrdata);
			mapents = new_mapent(root, strdup(""), strdup("nfs"),
						strdup(opts), nfshost, nfsdir);
			break;

		case ADDR_USER_NISPLUS:
			homedir = nisplus_homedir(addrdata);
			mapents = (homedir == NULL)
				? NULL
				: new_mapent(root, strdup(""), strdup("lofs"),
					strdup(opts), strdup(""), homedir);
			break;
		}

		if (mapents == NULL) {
			return (NULL);
		}

		/* "map" => "map/key" */
		if ((maplen = append_mapname(map, strlen(map), key)) == 0) {
			return (mapents);
		}
		return (frontier(mapents, ref, map, maplen, map + maplen,
				opts, status));
	}
indirect:
	/* Install an indirect autofs mount point. */
	return (new_mapent(root, strdup(""), strdup("autofs"), strdup(opts),
				strdup(""), concat(map, '/', key)));
}


/*
 * All that this function really does is call frontier_aux() on every
 * name bound under ref.  The rest is error checking(!)
 *
 * The error handling strategy is to reject the entire mount request
 * (by freeing mapents) if any (potentially) transient error occurs,
 * and to treat nontransient errors as holes in the affected portions
 * of the namespace.
 */
static mapent *
frontier(mapent *mapents, const FN_ref_t *ref, char *map, size_t maplen,
    char *mntpnt, char *opts, FN_status_t *status)
{
	FN_ctx_t		*ctx;
	FN_bindinglist_t	*bindings = NULL;
	FN_ref_t		*child_ref;
	FN_string_t		*child_s;
	const char		*child;
	unsigned int		statcode;

	ctx = fn_ctx_handle_from_ref_(ref, status);
	if (ctx == NULL) {
		if (fn_status_code_(status) != FN_E_NO_SUPPORTED_ADDRESS) {
			logstat(status, "from_ref failed for", map);
		}
		goto checkerr_return;
	}

	bindings = fn_ctx_list_bindings_(ctx, empty_cname, status);
	fn_ctx_handle_destroy_(ctx);
	if (bindings == NULL) {
		logstat(status, "list_bindings failed for", map);
		goto checkerr_return;
	}

	while ((child_s = fn_bindinglist_next_(bindings, &child_ref, status))
			!= NULL) {
		child = (const char *)fn_string_str_(child_s, &statcode);
		if (child == NULL) {
			if (verbose) {
				syslog(LOG_ERR,
					"FNS string error listing %s", map);
			}
			fn_string_destroy_(child_s);
			goto err_return;
		}
		mapents = frontier_aux(mapents, child_ref, map, maplen,
					mntpnt, child, opts, status);
		fn_string_destroy_(child_s);
		fn_ref_destroy_(child_ref);
		if (mapents == NULL) {
			goto noerr_return;
		}
	}
	if (fn_status_is_success_(status)) {
		goto noerr_return;
	} else {
		logstat(status, "error while listing", map);
		/* Fall through to checkerr_return. */
	}

checkerr_return:
	if (!transient(status)) {
		goto noerr_return;
	}
err_return:
	free_mapent(mapents);
	mapents = NULL;
noerr_return:
	if (bindings != NULL) {
		fn_bindinglist_destroy_(bindings, status);
	}
	return (mapents);
}


static mapent *
frontier_aux(mapent *mapents, const FN_ref_t *ref, char *map, size_t maplen,
    char *mntpnt, const char *name, char *opts, FN_status_t *status)
{
	addrtype_t	addrtype;
	bool_t		at_frontier;
	mapent		*me;
	size_t		maplen_save = maplen;
	char		*cname = map + FNPREFIXLEN + 1;	/* for error msgs */

	if (reftype(ref) >= NUM_REFTYPES) {
		/*
		 * We could instead install an indirect autofs mount point
		 * here.  That would allow, for example, a user to be bound
		 * beneath a file system.
		 */
		return (mapents);
	}

	/* "map" => "map/name" */
	if ((maplen = append_mapname(map, maplen, name)) == 0) {
		return (mapents);
	}
	if (trace > 1) {
		trace_prt(1, "  FNS traversal: %s/\n", cname);
	}

	/*
	 * If this is an address type that we know how to mount, then
	 * we have reached the frontier.
	 */
	at_frontier = (addr_from_ref(ref, cname, &addrtype, NULL, 0) == 0);
	/*
	 * For an ADDR_HOST address, treat a non-exported directory as
	 * if the address type were not known:  continue searching for
	 * exported subdirectories.
	 */
	if (at_frontier && (addrtype == ADDR_HOST)) {
		if (!exported(ref, cname, status)) {
			if (transient(status)) {
				free_mapent(mapents);
				return (NULL);
			} else {
				at_frontier = FALSE;
			}
		}
	}
	/*
	 * If we have reached the frontier, install a direct autofs
	 * mount point (which will trigger the actual mount if the
	 * user steps on it later).  Otherwise, continue traversing
	 * the namespace looking for known address types.
	 */
	if (at_frontier) {
		opts = (opts[0] != '\0')
			? concat(opts, ',', "direct")
			: strdup("direct");
		me = new_mapent(noroot, strdup(mntpnt), strdup("autofs"), opts,
				strdup(""), strdup(map));
		if (me != NULL) {
			/* Link new mapent into list (not at the head). */
			me->map_next = mapents->map_next;
			mapents->map_next = me;
		} else {
			free_mapent(mapents);
			mapents = NULL;
		}
	} else {
		mapents =
		    frontier(mapents, ref, map, maplen, mntpnt, opts, status);
	}
	map[maplen_save] = '\0';	/* "map/name" => "map" */
	return (mapents);
}


static bool_t
exported(const FN_ref_t *ref, const char *cname, FN_status_t *status)
{
	FN_ctx_t		*ctx;
	FN_attribute_t		*attr;

	ctx = fn_ctx_handle_from_ref_(ref, status);
	if (ctx == NULL) {
		logstat(status, "from_ref failed for", cname);
		return (FALSE);
	}
	attr = fn_attr_get_(ctx, empty_cname, &attr_exported, status);
	fn_ctx_handle_destroy_(ctx);

	switch (fn_status_code_(status)) {
	case FN_SUCCESS:
		fn_attribute_destroy_(attr);
		break;
	case FN_E_NO_SUCH_ATTRIBUTE:
		break;
	default:
		logstat(status, "could not get attributes for", cname);
	}
	return (attr != NULL);
}


static int
addr_from_ref(const FN_ref_t *ref, const char *cname, addrtype_t *typep,
    char *data, size_t datasz)
{
	const FN_ref_addr_t	*addr;
	void			*iter_pos;

	addr = fn_ref_first_(ref, &iter_pos);
	if (addr == NULL) {
		if (verbose) {
			syslog(LOG_ERR, "FNS ref with no address: %s", cname);
		}
		return (-1);
	}
	while (addr != NULL) {
		*typep = addrtype(addr);
		if (*typep < NUM_ADDRTYPES) {
			return ((data != NULL)
				? str_from_addr(cname, addr, data, datasz)
				: 0);
		}
		addr = fn_ref_next_(ref, &iter_pos);
	}
	return (-1);
}


static reftype_t
reftype(const FN_ref_t *ref)
{
	reftype_t	rtype;

	for (rtype = 0; rtype < NUM_REFTYPES; rtype++) {
		if (ident_str_equal(fn_ref_type_(ref), reftypes[rtype])) {
			break;
		}
	}
	return (rtype);
}


static addrtype_t
addrtype(const FN_ref_addr_t *addr)
{
	addrtype_t		atype;
	const FN_identifier_t	*ident = fn_ref_addr_type_(addr);

	for (atype = 0; atype < NUM_ADDRTYPES; atype++) {
		if (ident_str_equal(ident, addrtypes[atype])) {
			break;
		}
	}
	return (atype);
}


static int
str_from_addr(const char *cname, const FN_ref_addr_t *addr, char str[],
    size_t strsz)
{
	XDR	xdr;
	int	res;

	xdrmem_create(&xdr, (caddr_t)fn_ref_addr_data_(addr),
			fn_ref_addr_length_(addr), XDR_DECODE);
	if (!xdr_string(&xdr, &str, strsz)) {
		if (verbose) {
			syslog(LOG_ERR,
				"Could not decode FNS address for %s", cname);
		}
		res = -1;
	} else {
		res = 0;
	}
	xdr_destroy(&xdr);
	return (res);
}


static bool_t
ident_str_equal(const FN_identifier_t *id, const char *str)
{
	return ((id->format == FN_ID_STRING) &&
		(id->length == strlen(str)) &&
		(strncmp(str, id->contents, id->length) == 0));
}


static FN_composite_name_t *
new_cname(const char *str)
{
	FN_string_t		*string;
	FN_composite_name_t	*cname;

	string = fn_string_from_str_((unsigned char *)str);
	if (string == NULL) {
		if (verbose) {
			syslog(LOG_ERR, "Could not create FNS string object");
		}
		return (NULL);
	}
	cname = fn_composite_name_from_string_(string);
	fn_string_destroy_(string);
	if ((cname == NULL) && verbose) {
		syslog(LOG_ERR, "Could not create FNS composite name object");
	}
	return (cname);
}


static void
logstat(const FN_status_t *status, const char *msg1, const char *msg2)
{
	FN_string_t	*desc_string;
	const char	*desc = NULL;

	if (verbose) {
		desc_string = fn_status_description_(status, DETAIL, NULL);
		if (desc_string != NULL) {
			desc = (const char *)fn_string_str_(desc_string, NULL);
		}
		if (desc == NULL) {
			desc = "(no status description)";
		}
		syslog(LOG_ERR, "FNS %s %s: %s (%u)",
				msg1, msg2, desc, fn_status_code_(status));
		if (desc_string != NULL) {
			fn_string_destroy_(desc_string);
		}
	}
}


static bool_t
transient(const FN_status_t *status)
{
	unsigned int statcode;

	statcode = fn_status_code_(status);
	if (statcode == FN_E_LINK_ERROR) {
		statcode = fn_status_link_code_(status);
	}
	switch (statcode) {
	case FN_E_COMMUNICATION_FAILURE:
	case FN_E_CTX_UNAVAILABLE:
	case FN_E_INSUFFICIENT_RESOURCES:
	case FN_E_INVALID_ENUM_HANDLE:
	case FN_E_PARTIAL_RESULT:
	case FN_E_UNSPECIFIED_ERROR:
		return (TRUE);
	default:
		return (FALSE);
	}
}


static char *
nisplus_homedir(const char *nisname)
{
	nis_result	*res;
	void		*val;
	size_t		len;
	char		*homedir = NULL;

	/* NIS+ query to find passwd table entry */
	res = nis_list((nis_name)nisname,
			FOLLOW_LINKS | FOLLOW_PATH | USE_DGRAM, NULL, NULL);
	if (res == NULL) {
		if (verbose) {
			syslog(LOG_ERR,
				"FNS home dir query failed: %s", nisname);
		}
		return (NULL);
	}
	if (res->status != NIS_SUCCESS) {
		if ((res->status != NIS_NOTFOUND) && verbose) {
			syslog(LOG_ERR,
				"FNS home dir query failed: %s", nisname);
		}
		goto done;
	}
	val = ENTRY_VAL(NIS_RES_OBJECT(res), NIS_HOME);
	len = ENTRY_LEN(NIS_RES_OBJECT(res), NIS_HOME);

	if (len < 1) {
		if (verbose) {
			syslog(LOG_ERR, "FNS home dir not found: %s", nisname);
		}
		goto done;
	}
	homedir = malloc(len + 1);
	if (homedir == NULL) {
		if (verbose) {
			syslog(LOG_ERR, "Memory allocation failed");
		}
		goto done;
	}

	strncpy(homedir, (char *)val, len);
	homedir[len] = '\0';
done:
	nis_freeresult(res);
	return (homedir);
}


static size_t
append_mapname(char *map, size_t maplen, const char *name)
{
	size_t namelen = strlen(name);

	if (maplen + 1 + namelen >= MAPNAMESZ) {
		if (verbose) {
			syslog(LOG_ERR, "FNS name %s/%s too long",
				map + FNPREFIXLEN + 1, name);
		}
		return (0);
	}
	sprintf(map + maplen, "/%s", name);
	return (maplen + 1 + namelen);
}


static char *
concat(const char *s1, char sep, const char *s2)
{
	char *s = malloc(strlen(s1) + 1 + strlen(s2) + 1);

	if (s != NULL) {
		sprintf(s, "%s%c%s", s1, sep, s2);
	}
	return (s);
}


static int
trim_line(mapline *ml)
{
	char	*end;	/* pointer to '\0' at end of linebuf */

	end = ml->linebuf + strcspn(ml->linebuf, "#");
	while ((end > ml->linebuf) && isspace(end[-1])) {
		end--;
	}
	if (end <= ml->linebuf) {
		return (-1);
	}
	*end = '\0';
	unquote(ml->linebuf, ml->lineqbuf);
	return (0);
}


static bool_t
opts_only(const mapline *ml)
{
	const char *s = ml->linebuf;
	const char *q = ml->lineqbuf;

	if (*s != '-') {
		return (FALSE);
	}
	for (; *s != '\0'; s++, q++) {
		if (isspace(*s) && (*q == ' ')) {
			return (FALSE);
		}
	}
	return (TRUE);
}


static mapent *
new_mapent(char *root, char *mntpnt, char *fstype, char *mntopts, char *host,
    char *dir)
{
	mapent		*me;
	struct mapfs	*mfs;
	char		*mounter = NULL;

	me = calloc(1, sizeof (*me));
	mfs = calloc(1, sizeof (*mfs));
	if (fstype != NULL) {
		mounter = strdup(fstype);
	}
	if ((mntpnt == NULL) || (fstype == NULL) || (mntopts == NULL) ||
	    (host == NULL) || (dir == NULL) || (me == NULL) || (mfs == NULL) ||
	    (mounter == NULL) || (root == NULL)) {
		if (verbose) {
			syslog(LOG_ERR, "Memory allocation failed");
		}
		free(me);
		free(mfs);
		free(mounter);
		free(root);
		free(mntpnt);
		free(fstype);
		free(mntopts);
		free(host);
		free(dir);
		return (NULL);
	}
	me->map_root	= (root != noroot) ? root : NULL;
	me->map_fstype	= fstype;
	me->map_mounter	= mounter;
	me->map_mntpnt	= mntpnt;
	me->map_mntopts	= mntopts;
	me->map_fs	= mfs;
	mfs->mfs_host	= host;
	mfs->mfs_dir	= dir;
	return (me);
}
