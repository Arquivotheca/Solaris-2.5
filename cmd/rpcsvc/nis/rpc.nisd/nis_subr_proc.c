#pragma ident	"@(#)nis_subr_proc.c	1.39	95/02/24 SMI"
/*
 *	nis_subr_proc.c
 *
 *	Copyright (c) 1991, 1992 Sun Microsystems Inc
 *	All rights reserved.
 *
 * This module contains subroutines that are used by the NIS+ service but
 * _not_ the NIS+ clients.  NIS+ client routines are in the library module
 * nis_subr.c
 */

#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pwd.h>
#include <thread.h>
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <rpc/svc.h>
#include <rpc/auth.h>
#ifndef TDRPC
#include <rpc/auth_sys.h>
#else
#include <rpc/auth_unix.h>
#endif
#include <rpc/auth_des.h>
#include <rpcsvc/nis.h>
#include <rpcsvc/nis_callback.h>
#include <errno.h>
#include <sys/stat.h>
#include "nis_svc.h"
#include "nis_proc.h"

#ifndef NIS_MAXLINKS
#define	NIS_MAXLINKS 16
#endif

#define	GETSERVER(o, n) (((o)->DI_data.do_servers.do_servers_val) + n)
#define	MAXSERVER(o)    (o)->DI_data.do_servers.do_servers_len
#define	h_mask_clear(x)  memset((char *)(x), 0, NIS_MAXREPLICAS+1)

typedef u_char h_mask[NIS_MAXREPLICAS+1];

extern void __nis_destroy_callback();
extern nis_result *nis_iblist_svc(), *nis_lookup_svc();
extern CLIENT *__nis_get_server(directory_obj *, int);
extern fd_result *__fd_res(nis_name, nis_error, directory_obj*);
extern nis_server *__nis_select_replica(nis_server*, int, h_mask, h_mask);
extern nis_result *__nis_make_binding(nis_result **, nis_name,
					directory_obj *, int);

/* routines for registering server's availability wrt given dirctory */
extern void nis_put_offline(nis_name);
extern void nis_put_online(nis_name);
extern nis_name __nis_local_root();

extern FILE *cons;

static nis_error	apply_update(log_entry *);
nis_result *__nis_core_lookup(ib_request *, u_long, int, void *,
				int (*)(nis_name, nis_object *, void *));

static struct directory_item {
	NIS_HASH_ITEM	dl_item;	/* Generic ITEM tag 	*/
	nis_object	*dl_obj;	/* Directory object	*/
	u_long		dl_expires;	/* Expiration time	*/
	u_long		dl_serve;	/* nonzero if we serve	*/
	u_long		*dl_stamps; 	/* Replica timestamps	*/
};

static NIS_HASH_TABLE *dl = NULL;

void
free_abort(p)
	char *p;
{
	syslog(LOG_CRIT, "Attempting to free a free rag!");
	syslog(LOG_ERR, "Attempting to free a free rag!");
	abort();
}

/*
 * These functions are used to clean up after ourselves when we return
 * a result. The primary client are the svc functions which malloc
 * their data and then put it on the cleanup list when they return.
 * this allows them to be reentrant.
 */
void
do_cleanup(stuff)
	cleanup	*stuff;
{
	cleanup	*this, *nextrag;

	for (this = stuff; this; this = nextrag) {
		nextrag = this->next;
#ifdef DEBUG
		if ((this->tag) && verbose)
			syslog(LOG_INFO, "do_cleanup : '%s'", this->tag);
#endif
		(*(this->func))(this->data);
		this->func = free_abort;
		this->data = NULL;
		this->tag = NULL;
		this->next = free_rags;
		free_rags = this;
	}
}

static int cleanup_tag = 0;
void
add_cleanup(clean_func, clean_data, ragtag)
	void	(*clean_func)();
	void	*clean_data;
	char	*ragtag;
{
	register cleanup *newrag;
	int	i;

	if ((! clean_func) || (! clean_data)) {
		if (cons)
			fprintf(cons, "no func or data : %s\n", ragtag);
		return;
	}

	if (! free_rags) {

		syslog(LOG_INFO,
			"add_cleanup: Low on rags, allocating some more.");
		newrag = (cleanup *)XCALLOC(1024, sizeof (cleanup));
		if (! newrag) {
			syslog(LOG_CRIT,
				"add_cleanup: Can't allocate more rags.");
			return;
		}
		for (i = 0; i < 1024; i++) {
			newrag[i].next = free_rags;
			free_rags = &(newrag[i]);
		}
	}

	newrag = free_rags;
	free_rags = free_rags->next;

	newrag->func = clean_func;
	newrag->data = clean_data;
	newrag->tag  = ragtag;
	newrag->next = looseends;
	newrag->id = cleanup_tag;
#ifdef DEBUG
	if (verbose)
		syslog(LOG_INFO, "add_cleanup: tag # %d '%s'",
			cleanup_tag, ragtag);
#endif
	cleanup_tag++;
	looseends = newrag;
}

void
do_xdr_cleanup(data)
	xdr_clean_data *data;
{
	if (! data)
		return;

	xdr_free(data->xdr_func, data->xdr_data);
	XFREE(data->xdr_data);
	XFREE(data);
}

void
add_xdr_cleanup(func, data, t)
	bool_t	(*func)();
	char	*data;
	char	*t;
{
	xdr_clean_data	*dat = NULL;

	dat = (xdr_clean_data *)XCALLOC(1, sizeof (xdr_clean_data));
	if (! dat)
		return;

	dat->xdr_func = func;
	dat->xdr_data = (void *)data;
	add_cleanup(do_xdr_cleanup, dat, t);
}

/*
 * This is a link list of all the directories served by this server.
 */
static struct nis_dir_list {
	char	*name;
	struct nis_dir_list *next;
};
static struct nis_dir_list *dirlisthead = NULL;

/*
 * nis_server_control() controls various aspects of server administration.
 */
nis_server_control(infotype, op, argp)
	enum NIS_SERVER_INFO	infotype;
	enum NIS_SERVER_OP	op;
	void			*argp;
{
	char	filename[BUFSIZ], tmpname[BUFSIZ];
	char	buf[BUFSIZ];
	FILE	*fr, *fw;
	char	*name, *end;
	int	ss;
	char	oldval;
	char	**arrayp = (char **)argp;
	int	i;
	struct stat st;
	struct nis_dir_list *tmp, *prev;

	filename[0] = NULL;
	strcat(filename, nis_data("serving_list"));
	switch (infotype) {
	    case SERVING_LIST:
		/*
		 * The file "serving_list" contains one directory name per
		 * line.
		 */
		switch (op) {
		    case DIR_ADD:
			/* Check whether I already serve this directory? */
			for (tmp = dirlisthead; tmp; tmp = tmp->next)
				if (strcasecmp(tmp->name, (char *)argp) == 0)
					return (1);

			fw = fopen(filename, "r+");
			if (fw == NULL) {
				ss = stat(filename, &st);
				if (ss == -1 && errno == ENOENT) {
					fw = fopen(filename, "a+");
				}
				if (fw == NULL) {
					syslog(LOG_ERR,
					"could not open file %s for updating",
						filename);
					return (0);
				}
			}

			/* Add it to the incore copy */
			tmp = XMALLOC(sizeof (struct nis_dir_list));
			if (tmp == NULL) {
				/* Should never really happen */
				fclose(fw);
				return (0);
			}
			if ((tmp->name = strdup((char *)argp)) == NULL) {
				/* Should never really happen */
				fclose(fw);
				XFREE(tmp);
				return (0);
			}
			tmp->next = dirlisthead;
			dirlisthead = tmp;

			/* Add it to the file */
			while (fgets(buf, BUFSIZ, fw)) {
				name = buf;
				while (isspace(*name))
					name++;
				end = name;
				while (!isspace(*end))
					end++;
				*end = NULL;
				if (strcasecmp(name, (char *)argp) == 0) {
					/* already exists */
					fclose(fw);
					return (1);
				}
			}
			fprintf(fw, "%s\n", (char *)argp);
			fclose(fw);
			return (1);

		    case DIR_DELETE:
			prev = dirlisthead;
			for (tmp = dirlisthead; tmp; tmp = tmp->next) {
				if (strcasecmp(tmp->name, (char *)argp) == 0) {
					if (tmp == dirlisthead)
						dirlisthead = tmp->next;
					else
						prev->next = tmp->next;
					XFREE(tmp->name);
					XFREE(tmp);
					break;
				}
				prev = tmp;
			}
			if (tmp == NULL)
				/* It wasnt found, so return success */
				return (1);

			fr = fopen(filename, "r");
			if (fr == NULL) {
				syslog(LOG_ERR,
				"could not open file %s for reading",
					filename);
				return (0);
			}
			sprintf(tmpname, "%s.tmp", filename);
			fw = fopen(tmpname, "w");
			if (fw == NULL) {
				syslog(LOG_ERR,
				"could not open file %s for updating",
					tmpname);
				fclose(fr);
				return (0);
			}
			while (fgets(buf, BUFSIZ, fr)) {
				name = buf;
				while (isspace(*name))
					name++;
				end = name;
				while (!isspace(*end))
					end++;
				oldval = *end;
				*end = NULL;
				if (strcasecmp(name, (char *)argp) == 0) {
					continue; /* skip this one */
				}
				*end = oldval;
				fputs(buf, fw);
			}
			fclose(fr);
			fclose(fw);
			rename(tmpname, filename);
			return (1);

		    case DIR_INITLIST:
			/* initialize dirlisthead */
			fr = fopen(filename, "r");
			if (fr == NULL) {
				/* The server is just starting out */
				return (-1);
			}
			while (fgets(buf, BUFSIZ, fr)) {
				name = buf;
				while (isspace(*name))
					name++;
				end = name;
				while (!isspace(*end))
					end++;
				*end = NULL;
				tmp = XMALLOC(sizeof (struct nis_dir_list));
				if (tmp == NULL) {
					/* Should never really happen */
					fclose(fr);
					return (0);
				}
				if ((tmp->name = strdup(name)) == NULL) {
					/* Should never really happen */
					XFREE(tmp);
					fclose(fr);
					return (0);
				}
				tmp->next = dirlisthead;
				dirlisthead = tmp;
			}
			fclose(fr);
			return (1);

		    case DIR_GETLIST:
			i = 0;
			for (tmp = dirlisthead; tmp; tmp = tmp->next) {
				if ((arrayp[i++] = strdup(tmp->name)) == NULL)
					break;
			}
			arrayp[i] = NULL;
			return (i);

		    case DIR_SERVED:	/* Do I serve this directory */
			for (tmp = dirlisthead; tmp; tmp = tmp->next)
				if (strcasecmp(tmp->name, (char *)argp) == 0)
					return (1);
			return (0);

		    default:
			return (0);
		}
	    default:
		return (0);
	}
}

/*
 * nis_isserving()
 *
 * This function returns state indicating whether or not we serve the
 * indicated directory.
 * 0 = we don't serve it
 * n = which server we are (1 == master)
 */
int
nis_isserving(dobj)
	nis_object	*dobj;	/* Directory object */
{
	int			ns, i;	/* number of servers */
	nis_name		me = nis_local_host();

	if (__type_of(dobj) != DIRECTORY_OBJ)
		return (0);

	ns = MAXSERVER(dobj);
	/*
	 * POLICY : Should host names be compared in a case independent
	 *	    mode?
	 * ANSWER : Yes, to support the semantics of DNS and existing
	 *	    software which assume hostnames are case insensitive.
	 */

	if (nis_dir_cmp(me, GETSERVER(dobj, 0)->name) == SAME_NAME)
		return (1);

	/* Not master, check to see if we serve as a replica */
	for (i = 1; i < ns; i++) {
		if (nis_dir_cmp(me, GETSERVER(dobj, i)->name) == SAME_NAME)
			return (i+1);
	}
	return (0);
}

/*
 * free_dlitem()
 *
 * This function will remove a directory_item from the cache and free the
 * memory associated with it.
 */
static void
free_dlitem(dd)
	struct directory_item	*dd;
{
	dd = (struct directory_item *)nis_remove_item(dd->dl_item.name, dl);
	free(dd->dl_item.name);
	free(dd->dl_stamps);
	nis_destroy_object(dd->dl_obj);
	free(dd);
}


/*
 * 1. Flush server's local directory cache.
 * 2. Flush cachemgr's cache, using one of the following
 *    a.  given directory object
 *    b.  if none given and directory is in local cache, use its dir object.
 *    c.  if directory is not in local cache, look it up from cachemgr,
 *		if found, delete it.
 * 3. Flush the associated table cache.
 */
void
flush_dircache(name, dobj)
	nis_name	name;
	directory_obj *dobj;
{
	struct directory_item	*dd;
	int cachemgr_updated = 0;

	if (verbose)
		syslog(LOG_INFO, "flush_dircache: name '%s'", name);

	/* Flush given directory object from cachemgr. */
	if (dobj) {
		__nis_CacheRemoveEntry(dobj);
		cachemgr_updated = 1;
	}

	/* Update server's local directory cache. */
	if (dl) {
		dd = (struct directory_item *) nis_find_item(name, dl);
		if (dd) {
			if (cachemgr_updated == 0) {
				__nis_CacheRemoveEntry(&(dd->dl_obj->DI_data));
				cachemgr_updated = 1;
			}
			free_dlitem(dd);
		}
	}

	/* Update cachemgr if have not done so yet. */
	if (cachemgr_updated == 0) {
		directory_obj dobj2;
		if (__nis_CacheSearch(name, &dobj2) == NIS_SUCCESS &&
		    nis_dir_cmp(name, dobj2.do_name) == SAME_NAME) {
			__nis_CacheRemoveEntry(&dobj2);
			xdr_free((xdrproc_t)xdr_directory_obj, (char *)&dobj2);
		}
	}

	/*
	 * Since we cache the table for this dir object,
	 * we will flush that away as well.
	 */
	flush_tablecache(name);
}

void
flush_dircache_refresh(name)
	nis_name	name;
{
	struct ticks	t;
	nis_object	*obj;

	if (verbose)
		syslog(LOG_INFO, "flush_dircache_refresh: name '%s'", name);

	flush_dircache(name, (directory_obj *) NULL);	/* Flush it first */
	/*
	 * Refresh it from the master.
	 * XXX: We may have some scalability problems
	 * if all servers go to the master at the same time.  Hopefully
	 * directory objects would not be updated very frequently, so it
	 * should not be such a big problem.
	 */
	(void) __directory_object(name, &t, NO_CACHE, &obj);
}

/*
 * Flush all cached directory objects from dircache and cachemgr.
 */
void
flush_dircache_all()
{
	struct directory_item	*dd;

	if (verbose)
		syslog(LOG_INFO, "flush_dircache_all");

	while (dd = (struct directory_item *)nis_pop_item(dl)) {
		__nis_CacheRemoveEntry(&(dd->dl_obj->DI_data));
		free(dd->dl_item.name);
		free(dd->dl_stamps);
		/*
		 * Since we cache the table for this dir object,
		 * we will flush that away as well.
		 */
		flush_tablecache(dd->dl_item.name);
		nis_destroy_object(dd->dl_obj);
		free(dd);
	}
}

static void
flush_local_dircache(name)
	nis_name	name;
{
	struct directory_item	*dd;

	if (verbose)
		syslog(LOG_INFO, "flush_local_dircache: name '%s'", name);
	if (! dl)
		return;
	dd = (struct directory_item *) nis_find_item(name, dl);
	if (dd)
		free_dlitem(dd);
}

/*
 * Flushes the local TABLE cache.  Called when the database changes.
 */
void
flush_tablecache(name)
	nis_name	name;
{
	struct table_item	*ti;

	if (verbose)
		syslog(LOG_INFO, "flush_tablecache: name '%s'", name);
	if (! table_cache)
		return;
	ti = (struct table_item *)nis_remove_item(name, table_cache);
	if (ti) {
		free(ti->ti_item.name);
		/*
		 * Who knows who might be using it somewhere, so lets play
		 * it safe. We'll delete it later.
		 */
		add_cleanup(nis_destroy_object, (char *)ti->ibobj,
			    "destroy table cache object");
		free(ti);
	}
}

/*
 * Flushes all TABLE caches.
 */
void
flush_tablecache_all()
{
	struct table_item	*ti;

	if (verbose)
		syslog(LOG_INFO, "flush_tablecache_all");
	if (! table_cache)
		return;
	while (ti = (struct table_item *)nis_pop_item(table_cache)) {
		free(ti->ti_item.name);
		/*
		 * Who knows who might be using it somewhere, so lets play
		 * it safe. We'll delete it later.
		 */
		add_cleanup(nis_destroy_object, (char *)ti->ibobj,
			    "destroy table cache object");
		free(ti);
	}
}

/*
 * Flushes a specific item from the group cache.
 */
void
flush_groupcache(name)
	nis_name	name;
{
	if (verbose)
		syslog(LOG_INFO, "flush_groupcache: name '%s'", name);

	__nis_flush_group_exp_name(name);
}

/*
 * Flushes all group caches.
 */
void
flush_groupcache_all()
{
	if (verbose)
		syslog(LOG_INFO, "flush_groupcache_all");

	nis_flushgroups();
}

extern enum clnt_stat __nis_cast_proc();

/*
 * A convenient wrapper around __nis_cast_proc().
 */
bool_t
nis_mcast_tags(dobj, taglist)
	directory_obj	*dobj;
	nis_taglist	*taglist;
{
	enum clnt_stat 	stat;
	h_mask		hostmask;

	memset((void *)&hostmask, 0, sizeof (h_mask));
	if (dobj == NULL)
		return (FALSE);
	stat = __nis_cast_proc(dobj, hostmask, NIS_SERVSTATE,
			xdr_nis_taglist, taglist, NULL, NULL,
			NULL, NULL, 0);
	return ((stat == RPC_SUCCESS)? TRUE : FALSE);
}

/*
 * We need this function, since the root object is stored as a directory
 * obj structure and not a nis_object. directory_obj strucs don't have the
 * domain name.
 */
void
add_pingitem_with_name(buf, dir, ptime, tbl)
	char		*buf;
	nis_object	*dir;
	u_long		ptime;
	NIS_HASH_TABLE	*tbl;
{
	ping_item	*pp;

	pp = (ping_item *)nis_find_item(buf, tbl);
	if (pp) {
		if (verbose)
			syslog(LOG_INFO, "add_pingitem: updated %s", buf);
		pp->mtime = ptime;
		return;
	}

	pp = (ping_item *)XCALLOC(1, sizeof (ping_item));
	if (! pp) {
		if (verbose)
			syslog(LOG_ERR,
		    "add_pingitem: Couldn't add '%s' to pinglist (no memory)",
									buf);
		return;
	}
	pp->item.name = (nis_name) XSTRDUP(buf);
	if (pp->item.name == NULL) {
		if (verbose)
			syslog(LOG_ERR,
		    "add_pingitem: Couldn't add '%s' to pinglist (no memory)",
									buf);
		XFREE(pp);
		return;
	}
	pp->mtime = ptime;
	if (dir)
		pp->obj = nis_clone_object(dir, NULL);
	else
		pp->obj = NULL;
	nis_insert_item((NIS_HASH_ITEM *)pp, tbl);
	if (verbose)
		syslog(LOG_INFO, "add_pingitem: added %s", buf);
}

void
add_pingitem(dir, ptime, tbl)
	nis_object	*dir;
	u_long		ptime;
	NIS_HASH_TABLE	*tbl;
{
	char		buf[1024];

	if (dir == NULL)
		return;

	sprintf(buf, "%s.%s", dir->zo_name, dir->zo_domain);
	add_pingitem_with_name(buf, dir, ptime, tbl);
}

/*
 * Updates directory item with given information.
 * If directory requires update, add it to 'upd_list'.
 * If directory is root directory, check whether root object requires
 * update.
 */
static void
update_dl_item(nis_name name,
	struct directory_item *dd,
	nis_object* d_obj,
	struct timeval *tv,
	enum name_pos p)
{
	nis_name rootname;

	dd->dl_expires = tv->tv_sec + d_obj->zo_ttl;
	dd->dl_obj = d_obj;

	if ((dd->dl_serve = nis_isserving(d_obj)) == 0)
		return;

	/*
	 * Add it to the list of directories served by this
	 * server if not already listed.
	 */
	nis_server_control(SERVING_LIST, DIR_ADD, name);

	if (dd->dl_serve == 1) /* master */
		dd->dl_stamps[0] = last_update(name);
	else { /* replica */
		/* schedule update for directory contents */
		dd->dl_stamps[dd->dl_serve-1] =
			nis_cptime(GETSERVER(d_obj, 0), name);
		if (!readonly &&
		    last_update(name) < dd->dl_stamps[dd->dl_serve-1]) {
			add_pingitem(dd->dl_obj,
				    dd->dl_stamps[dd->dl_serve-1],
				    &upd_list);
		}

		/* If root object, schedule update for object itself too */
		if (!readonly && p == SAME_NAME &&
		    ((rootname = __nis_local_root()) != 0) &&
		    nis_dir_cmp(name, rootname) == SAME_NAME &&
		    last_update(ROOT_OBJ) < d_obj->zo_oid.mtime) {
			add_pingitem_with_name(ROOT_OBJ,
						d_obj,
						d_obj->zo_oid.mtime,
						&upd_list);
		}
	}
}


/*
 * These statistics are used to calculate the effectiveness of the
 * directory object cache. You can retrieve them with a TAG_DIRCACHE
 * in the stats call.
 */
int	dircachecall = 0,
	dircachehit  = 0,
	dircachemiss = 0;

/*
 * __directory_object()
 * This function is used to search for directory objects that describe
 * directories that this server serves. When called it will either
 * return a directory object which describes a directory that is served
 * or NULL if the directory is not served by this server.
 * It takes three parameters, name, ticks and is_master. Name is
 * presumed to be the NIS+ name of the directory we're looking for, ticks
 * points to a ticks structure that we can fill in with some statistics
 * and is_master indicates that not only should we serve the directory
 * but also we must be the master server for that directory.
 */
nis_error
__directory_object(name, ticks, is_master, obj)
	nis_name	name;
	struct ticks 	*ticks;
	int		is_master;	/* Need we be the master ? */
	nis_object	**obj;
{
	register nis_object		*d_obj = NULL;
	register struct directory_item	*dd;
	nis_db_result			*dbres = NULL;
	nis_result			*zres = NULL;
	int				lookup_flags = 0;
	struct timeval 			tv;
	enum name_pos			p;

	/*
	 * A sanity check, if nis_name_of() returns NULL then either the
	 * name passed is identical to the local directory (in which case
	 * we can't possibly serve it unless we are a root server of a
	 * local root), or the name isn't in our tree at all (shares
	 * no common path). In which case we can't possibly serve it.
	 */
	*obj = NULL;

	if (name == NULL)
		return (NIS_BADNAME); /* illegal name */

	p = nis_dir_cmp(name, nis_local_directory());
	if ((p == HIGHER_NAME) || (p == NOT_SEQUENTIAL) || (p == BAD_NAME))
			return (NIS_BADNAME);

	/*
	 * XXX intermediate workaround, until the next source
	 * crank. The is_master "boolean" gets expanded here to
	 * be a set of bit flags. In particular, the new flag
	 * "NO_CACHE" is used to identify when the function calling
	 * this function wants it to retrieve a new copy of the
	 * directory object from the cache.
	 */
	if ((is_master & NO_CACHE) != 0) {
		is_master = ((is_master & TRUE) != 0);
		/* get latest copy of directory object */
		lookup_flags = MASTER_ONLY+NO_CACHE;
	}

	if (! dl) {
		dl = (NIS_HASH_TABLE *)calloc(1, sizeof (NIS_HASH_TABLE));
		if (!dl) {
			syslog(LOG_ERR, "__directory_object: out of memory");
			return (NIS_NOMEMORY);
		}
	}

	/* some statistics stuff ... */
	memset((char *)ticks, 0, sizeof (struct ticks));
	dircachecall++;
	if (verbose)
		syslog(LOG_INFO, "Looking for directory object %s", name);

	/*
	 * Now we check the cache to see if we have it cached locally.
	 */
	(void) gettimeofday(&tv, NULL);
	dd = (struct directory_item *) nis_find_item(name, dl);

	/*
	 * If we found it in the cache, and we're asking for the
	 * "no cache" version, then we flush the cache and
	 * cause it to be re-looked up.
	 */
	if (dd && (lookup_flags & NO_CACHE)) {
		free_dlitem(dd);
		dd = NULL;
	}

	if (dd) {
		if (verbose)
			syslog(LOG_INFO, "__directory_object: Cache hit.");
		dircachehit++;
		/*
		 * Found it in the cache. First we check to see if it
		 * has expired, and then we check to see if we are the
		 * master server. (we must serve it, otherwise it wouldn't
		 * be in our cache!)
		 *
		 * Check the expiration date on the object
		 */
		if ((tv.tv_sec < dd->dl_expires) && !(is_master)) {
			if (dd->dl_serve) {
				*obj = dd->dl_obj;
				return (NIS_SUCCESS);	/* Here it is ... */
			} else {
				/* Should never happen */
				*obj = NULL;
				return (NIS_NOT_ME);
			}
		} else if ((tv.tv_sec < dd->dl_expires) && is_master) {
			if (dd->dl_serve == 1) {
				*obj = dd->dl_obj;
				return (NIS_SUCCESS);	/* Here it is ... */
			} else
				return (NIS_NOTMASTER);	/* Not master */
		}
		/* Else it is expired so we're refreshing it. */
	}
	if (! dd) {
		if (verbose)
			syslog(LOG_INFO, "__directory_object: Cache miss.");
		dircachemiss++;
	}

	/*
	 * At this point we either don't have it in our cache (dd == NULL)
	 * or it is expired (dd != NULL) so we have to go looking for it
	 * from either inside (our database) or outside (our parent).
	 * First, we check to see if we are a root server and they want
	 * the root directory.
	 */

	dbres = db_lookup(name);
	ticks->dticks += dbres->ticks;
	if (dbres->status == NIS_SUCCESS) {
		d_obj = nis_clone_object(dbres->obj, NULL);
	} else if (dbres->status == NIS_SYSTEMERROR) {
		syslog(LOG_ERR,
			"__directory_object: internal database error.");
	} else if (dbres->status != NIS_NOTFOUND) {
		/*
		 * (db result-> no such table )
		 * Ask the "parent" of "name" since this should not
		 * recurse to the same server, we don't
		 * worry about screwing up the current
		 * server clock (clock(2)). But we will
		 * get ticks back from the other server
		 * so we return those in the ticks structure.
		 */
		zres = nis_lookup(name, lookup_flags);
		if (zres->status == NIS_SUCCESS) {
			d_obj = nis_clone_object(
					zres->objects.objects_val, 0);
			ticks->zticks += zres->zticks;
			ticks->dticks += zres->dticks;
			ticks->cticks += zres->cticks;
			ticks->aticks += zres->aticks;
		} else if (zres->status == NIS_NOTFOUND) {
			if (dd) {
				/* It no longer exists */
				free_dlitem(dd);
				dd = NULL;
			}
		} else {
			syslog(LOG_WARNING,
	    "__directory_object: Failed to lookup %s, status %s",
				name, nis_sperrno(zres->status));
		}
		nis_freeresult(zres);
	} else if (dd) {
		/* It no longer exists */
		free_dlitem(dd);
		dd = NULL;
	}

	/*
	 * At this point, if d_obj is null then we couldn't find
	 * it. We check to see if we have an expired cached version
	 * and if so, log a warning and go ahead and use it. If
	 * we don't have a cached version we just return NOTFOUND.
	 */
	if (! d_obj) {
		if (dd) {
			syslog(LOG_WARNING,
	"__directory_object: Using expired directory object for %s", name);
			if ((!(is_master) && (dd->dl_serve)) ||
			    (is_master && (dd->dl_serve == 1))) {
				*obj = dd->dl_obj;
				return (NIS_S_SUCCESS);
			} else {
				*obj = NULL;
				return (NIS_NOT_ME);
			}
		} else
			return (NIS_NOTFOUND);
	}

	/* If we found one, and it isn't a directory abort */
	if (__type_of(d_obj) != DIRECTORY_OBJ) {
		syslog(LOG_WARNING, "Object %s isn't a directory.",
				name);
		/* If the cache had one toss it out */
		if (dd)
			free_dlitem(dd);
		nis_destroy_object(d_obj);
		return (NIS_BADOBJECT);
	}


	/*
	 * Ok we found the directory object, do we really serve it ?
	 * Check to see if this server is supposed to serve this
	 * domain. (Must be master to manipulate the name space)
	 */
	if (dd) {
		if (MAXSERVER(dd->dl_obj) < MAXSERVER(d_obj)) {
			XFREE(dd->dl_stamps);
			dd->dl_stamps = (u_long *)calloc(MAXSERVER(d_obj),
							sizeof (u_long));
		}
		nis_destroy_object(dd->dl_obj);
		update_dl_item(name, dd, d_obj, &tv, p);
	} else {
		dd = (struct directory_item *)calloc(1, sizeof (*dd));
		if (dd) {
			dd->dl_item.name = (nis_name) strdup(name);
			dd->dl_stamps = (u_long *) calloc(MAXSERVER(d_obj),
							sizeof (u_long));
			update_dl_item(name, dd, d_obj, &tv, p);
			nis_insert_item((NIS_HASH_ITEM *)dd, dl);
		}
	}

	/* Check the master requirement */
	if (is_master)
		if (dd && (dd->dl_serve != 1))
			return (NIS_NOTMASTER);
		else {
			*obj = d_obj;
			return (NIS_SUCCESS);
		}

	if (dd && dd->dl_serve) {
		*obj = d_obj;
		return (NIS_SUCCESS);
	}

	return (NIS_NOT_ME);
}

/*
 * Returns the NIS+ principal name of the person making the request
 * XXX This is set up to use Secure RPC only at the moment, it should
 * be possible for any authentication scheme to be incorporated if it
 * has a "full name" that we can return as the principal name.
 */
static const nis_name nobody = "nobody";

#ifdef TDRPC
#define	AUTH_SYS AUTH_UNIX
#define	authsys_parms authunix_parms
#endif

static NIS_HASH_TABLE credtbl;
struct creditem {
	NIS_HASH_ITEM	item;
	char	pname[1024];
};

static void
add_cred_item(char *netname, char *pname)
{
	struct creditem *foo, *old;

	old = (struct creditem *)nis_find_item(netname, &credtbl);
	if (old)
		return;

	foo = (struct creditem *)calloc(1, sizeof (struct creditem));
	foo->item.name = strdup(netname);
	strcpy(foo->pname, pname);
	nis_insert_item((NIS_HASH_ITEM *)foo, &credtbl);
}

static int
find_cred_item(char *netname, char *pname)
{
	struct creditem	*old;

	old = (struct creditem *) nis_find_item(netname, &credtbl);
	if (! old)
		return (0);
	strcpy(pname, old->pname);
	return (1);
}

void
nis_getprincipal(name, flavor, auth)
	char		*name;
	int		flavor;
	caddr_t 	auth;
{
	struct authsys_parms	*au;
	struct authdes_cred	*ad;
	char			*rmtdomain;
	char			srch[2048]; /* search criteria */
	nis_result		*res;
	int			message;

	message = verbose || auth_verbose;
	strcpy(name, nobody); /* default is "nobody" */
	if (flavor == AUTH_NONE) {
		if (message) {
			syslog(LOG_INFO,
		    "nis_getprincipal: flavor = NONE: returning '%s'", nobody);
		}
		return;
	} else if (flavor == AUTH_SYS) { /* XXX ifdef this for 4.1 */
		if (secure_level > 1) {
			if (message) {
				syslog(LOG_INFO,
			"nis_getprincipal: flavor = SYS: returning '%s'",
								nobody);
			}
			return;
		}
		au = (struct authsys_parms *)(auth);
		rmtdomain = nis_domain_of(au->aup_machname);
		if (au->aup_uid == 0) {
			sprintf(name, "%s", au->aup_machname);
			if (! rmtdomain)
				strcat(name, nis_local_directory());
			if (name[strlen(name) - 1] != '.')
				strcat(name, ".");
			if (message) {
				syslog(LOG_INFO,
		    "nis_getprincipal: flavor = SYS: returning '%s'", name);
			}
			return;
		}
		sprintf(srch,
		    "[auth_name=\"%d\", auth_type=LOCAL], cred.org_dir.%s",
				au->aup_uid, (*rmtdomain == '.') ?
				(char *) nis_local_directory() : rmtdomain);
		if (srch[strlen(srch) - 1] != '.') {
			strcat(srch, ".");
		}
	} else if (flavor == AUTH_DES) {
		ad = (struct authdes_cred *)(auth);
		if (find_cred_item(ad->adc_fullname.name, name)) {
			if (message)
				syslog(LOG_INFO,
		"nis_getprincipal: flavor = DES: returning from cache '%s'",
					name);
			return;
		}
		rmtdomain = strchr(ad->adc_fullname.name, '@');
		if (rmtdomain) {
			rmtdomain++;
			sprintf(srch,
			    "[auth_name=%s, auth_type=DES], cred.org_dir.%s",
					ad->adc_fullname.name, rmtdomain);
			if (srch[strlen(srch) - 1] != '.') {
				strcat(srch, ".");
			}
		} else {
			if (auth_verbose) {
				syslog(LOG_INFO,
			    "nis_getprincipal: flavor = DES: returning '%s'",
					nobody);
			}
			return;
		}
	} else {
		syslog(LOG_WARNING,
		"nis_getprincipal: flavor = %d(unknown): returning '%s'",
							flavor, nobody);
		return;
	}
	if (message)
		syslog(LOG_INFO,
			"nis_getprincipal: calling list with name '%s'",
							name);
	res = nis_list(srch, NO_AUTHINFO+USE_DGRAM+FOLLOW_LINKS, NULL, NULL);
	if (res->status != NIS_SUCCESS) {
		if (message)
			syslog(LOG_INFO,
				"nis_getprincipal: error doing nis_list: %s",
						nis_sperrno(res->status));
	} else {
		strncpy(name, ENTRY_VAL(res->objects.objects_val, 0), 1024);
		if (flavor == AUTH_DES)
			add_cred_item(ad->adc_fullname.name, name);
	}

	nis_freeresult(res);
	if (message)
		syslog(LOG_INFO,
		"nis_getprincipal: flavor = %s: returning : '%s'",
			flavor == AUTH_SYS? "SYS" : "DES", name);
}

/*
 * __can_do()
 * This function returns true if the given principal has the right to
 * do the requested function on the given object. It could be a define
 * if that would save time. At the moment it is a function.
 * NOTE: It recursively calls NIS by doing the lookup on the group if
 * the conditional gets that far.
 *
 * N.B. If the principal passed is 'null' then we're recursing and don't
 * need to check it. (we always let ourselves look at the data)
 */
int
__can_do(right, mask, obj, pr)
	unsigned long	right;	/* The Access right we desire 		*/
	unsigned long	mask;	/* The mask for World/Group/Owner 	*/
	nis_object	*obj;	/* A pointer to the object		*/
	nis_name	pr;	/* Principal making the request		*/
{
	if ((secure_level == 0) || (*pr == 0))
		return (1);

	return (NOBODY(mask, right) ||
		(WORLD(mask, right) &&
			(strcmp(pr, "nobody") != 0)) ||
		(OWNER(mask, right) &&
			(nis_dir_cmp(pr, obj->zo_owner) == SAME_NAME)) ||
		(GROUP(mask, right) &&
			(strlen(obj->zo_group) > (size_t)(1)) &&
			__do_ismember(pr, obj->zo_group, nis_lookup)));
}

/*
 * Get an xdr buffer
 */
u_char *
__get_xdr_buf(size)
	int	size;	/* Size in bytes of the buffer */
{
	static struct nis_sdata local_buf;

	return ((u_char *)nis_get_static_storage(&local_buf, 1, size));
}

/*
 * Get a string buffer
 */
char *
__get_string_buf(size)
	int	size;	/* Size in bytes of the buffer */
{
	static struct nis_sdata local_buf;

	return ((char *)nis_get_static_storage(&local_buf, 1, size));
}

/*
 * get an array of entry columns
 */
entry_col *
__get_entry_col(n)
	int	n;	/* required array size */
{
	static struct nis_sdata local_buf;

	return ((entry_col *)nis_get_static_storage(&local_buf,
						sizeof (entry_col), n));
}

/*
 * get an array of table columns
 */
table_col *
__get_table_col(n)
	int	n;	/* required array size */
{
	static struct nis_sdata local_buf;

	return ((table_col *)nis_get_static_storage(&local_buf,
						sizeof (table_col), n));
}

/*
 * get an array of nis attributes
 */
nis_attr *
__get_attrs(n)
	int	n;	/* required array size */
{
	static struct nis_sdata local_buf;

	return ((nis_attr *)nis_get_static_storage(&local_buf,
						sizeof (nis_attr), n));
}

/*
 * Stability functions. These functions check to see if a transaction
 * has been propogated to all replicas.
 *
 * nis_isstable()
 * This function checks to see if an update has been propogated to all
 * of the replicates for the given domain. If so, the log code will be
 * free to delete it, otherwise we will continue to hold it until the
 * replicate picks up the change. If we can't figure out if it is stable
 * or not we return 0 and hold onto it.
 */
int
nis_isstable(le, first)
	log_entry	*le;
	int		first;
{
	int			i;
	struct directory_item	*dd;
	nis_object		*dobj;
	struct ticks		t;
	nis_name		dirname;
	nis_error		err;


	if (le->le_type == UPD_STAMP)
		dirname = le->le_name;
	else
		dirname = nis_domain_of(le->le_name);

	/*
	 * get object for the domain, if this is the first time we're
	 * "visitin" this domain then get a fresh copy (NO_CACHE)
	 */
	if (first)
		err = __directory_object(dirname, &t, NO_CACHE+1, &dobj);
	else
		err = __directory_object(dirname, &t, 1, &dobj);

	/*
	 * If we aren't the master any more, or the directory doesn't exist
	 * anymore we can toss this update.
	 */
	if ((err == NIS_NOTMASTER) || (err == NIS_NOTFOUND) ||
			(err == NIS_BADNAME))
		return (TRUE);

	/*
	 * If we encountered a problem trying to get the directory object
	 * we hold on to the update.
	 */
	if ((err != NIS_SUCCESS) && (err != NIS_S_SUCCESS))
		return (FALSE);

	/*
	 * If we look at the object and there is only one server then
	 * the update has by definition propogated.
	 */
	if (MAXSERVER(dobj) == 1)
		return (TRUE);

	/*
	 * Otherwise we go ahead and check to see if it is stable.
	 * Note the find_item will always work if the __directory_object()
	 * call worked.
	 */
	dd = (struct directory_item *)nis_find_item(dirname, dl);
	for (i = 1; i < MAXSERVER(dobj); i++) {
		if (dd->dl_stamps[i] == 0) {
			/* Fetch the time from the replicate */
			dd->dl_stamps[i] = nis_cptime(GETSERVER(dobj, i),
							    dd->dl_item.name);

			if (! dd->dl_stamps[i]) {
				dd->dl_stamps[i] = 1;
			}
		}

		if (dd->dl_stamps[i] < le->le_time)
			return (FALSE);
	}
	return (TRUE);
}

/*
 * add_fenceposts()
 *
 * This function will put "fenceposts" into the log for each directory we
 * serve. This makes the "lastupdate()" function faster and is essential
 * for directories that have been stable long enough that there are no
 * delta's left.
 */
void
add_fenceposts()
{
	int			i;
	struct directory_item	*dd;

	for (dd = (struct directory_item *)dl->first; dd;
			dd = (struct directory_item *)(dd->dl_item.nxt_item)) {
		for (i = 0; i < MAXSERVER(dd->dl_obj); i++) {
			if (nis_dir_cmp(GETSERVER(dd->dl_obj, i)->name,
				nis_local_host()) == SAME_NAME) {
				if (dd->dl_stamps[i] >
					last_update(dd->dl_item.name)) {
					make_stamp(dd->dl_item.name,
							    dd->dl_stamps[i]);
					break;
				}
			}
		}
	}
}

/*
 * nis_cptime()
 *
 * This function will ask the indicated replicate for the last
 * update it has seen to the given directory.
 */
u_long
nis_cptime(replica, name)
	nis_server	*replica;
	nis_name	name;
{
	CLIENT		*clnt;
	enum clnt_stat 	status;
	struct timeval	tv;
	u_long		res;

	clnt = nis_make_rpchandle(replica, 0, NIS_PROG, NIS_VERSION, ZMH_DG,
						1024, 128);
	/* If we can't contact it, return the safe answer */
	if (! clnt) {
		if (verbose)
		    syslog(LOG_INFO, "nis_cptime: could not contact %s",
			replica->name);
		return (0);
	}

	tv.tv_sec = 3;	/* retry time out */
	tv.tv_usec = 0;
	clnt_control(clnt, CLSET_RETRY_TIMEOUT, (void *)&tv);
	tv.tv_sec = 5;
	status = clnt_call(clnt, NIS_CPTIME, xdr_nis_name, (char *) &name,
					    xdr_u_long, (char *) &res, tv);
	if (status != RPC_SUCCESS) {
		syslog(LOG_WARNING,
			"nis_cptime: RPC error srv='%s', dir='%s', err='%s'",
				    replica->name, name, clnt_sperrno(status));
		res = 0;
	}
	clnt_destroy(clnt);
	if (verbose)
		syslog(LOG_INFO, "nis_cptime: returning %d for %s from %s",
			res, name, replica->name);
	return (res);
}

/*
 * apply_update()
 *
 * This function applies a log entry that we've received from the master
 * for a given domain to the log and the database.
 */
static nis_error
apply_update(le)
	log_entry	*le;
{
	nis_error	status;
	char		*msg;

	if (cons) {
		fprintf(cons, "update type %d, name %s.%s\n", le->le_type,
				le->le_object.zo_name, le->le_object.zo_domain);
	}

	switch (le->le_type) {
		case ADD_NAME :
			status = __db_add(le->le_name, &(le->le_object), 0);
			msg = "Failed to add object ";
			break;
		case MOD_NAME_NEW :
			status = __db_add(le->le_name, &(le->le_object), 1);
			msg = "Failed to modify object ";
			break;

		case REM_NAME :
			status = __db_remove(le->le_name, &(le->le_object));
			msg = "Failed to remove object ";
			break;

		/* For old modified objects, just add this to the log */
		case MOD_NAME_OLD :
			status = NIS_SUCCESS;
			break;

		case ADD_IBASE :
			status = __db_addib(le->le_name,
						    le->le_attrs.le_attrs_len,
						    le->le_attrs.le_attrs_val,
						    &(le->le_object));
			msg = "Failed to add entry ";
			break;

		case REM_IBASE :
			status = __db_remib(le->le_name,
						le->le_attrs.le_attrs_len,
						le->le_attrs.le_attrs_val);
			msg = "Failed to remove entry ";
			break;
		/* ignore these as they are both NOPs */
		case UPD_STAMP :
		case LOG_NOP :
			status = NIS_SUCCESS;
			break;

		default :
			status = NIS_SYSTEMERROR;
			msg = "Illegal transaction type on ";
			break;
	}
	if (status != NIS_SUCCESS) {
		syslog(LOG_ERR, "apply_update : %s %s NIS+ STATUS : %s",
			msg, __make_name(le), nis_sperrno(status));
		if (cons)
			fprintf(cons, "apply_update : %s %s\n",
				msg, __make_name(le));
	}

	return (status);
}

extern table_obj tbl_prototype;

/*
 * clear_directory()
 *
 * This function will restores a directory to "virgin" status. This is
 * accomplished by deleting any databases (tables) that are contained in
 * that directory, then destroying and recreating the database for the
 * directory.
 */
int
clear_directory(name)
	nis_name	name;
{
	nis_fn_result	*fnr;
	char		namebuf[NIS_MAXNAMELEN+1];
	nis_error	status;

	fnr = db_firstib(name, 0, NULL, FN_NOMANGLE+FN_NORAGS, NULL);
	while (fnr->status == NIS_SUCCESS) {
		sprintf(namebuf, "%s.%s", fnr->obj->zo_name,
					    fnr->obj->zo_domain);
		if (__type_of(fnr->obj) == TABLE_OBJ) {
			if ((status = db_destroy(namebuf)) != NIS_SUCCESS)
				syslog(LOG_WARNING,
					"Could not destroy table %s: %s.",
					namebuf, nis_sperrno(status));
		}

		__db_remove(namebuf, fnr->obj);
		nis_destroy_object(fnr->obj); 	/* free this */
		XFREE(fnr->cookie.n_bytes);	/* and this  */
		XFREE(fnr);			/* and this */
		fnr = db_firstib(name, 0, NULL, FN_NOMANGLE+FN_NORAGS, NULL);
	}

	XFREE(fnr);
	if ((status = db_destroy(name)) != NIS_SUCCESS) {
		syslog(LOG_WARNING,
			"Could not destroy table %s: %s.",
			name, nis_sperrno(status));
		/* XXX recover? */
	}

	/* Make a clean version. */
	if ((status = db_create(name, &tbl_prototype)) != NIS_SUCCESS) {
		syslog(LOG_WARNING,
			"Could not create table %s: %s.",
			name, nis_sperrno(status));
		/* XXX recover? */
	}

	if (verbose)
		syslog(LOG_INFO, "directory '%s' cleared.", name);
	return (1);
}

static struct {
	int	successes;
	int	errors;
	int	ticks;
	u_long	utime;
} repl_stats;


/*
 * nis_makename()
 *
 * This function prints out a nice name for a objects.
 */
void
nis_makename(obj, tobj, str)
	nis_object	*obj;
	nis_object	*tobj;
	char		*str;
{
	int		i;
	table_col	*tcols;
	entry_col	*ecols;
	int		ncols;

	if (tobj == NULL) {
		sprintf(str, "%s.%s", obj->zo_name, obj->zo_domain);
		return;
	}

	strcpy(str, "[");
	tcols = tobj->TA_data.ta_cols.ta_cols_val;
	ncols = tobj->TA_data.ta_cols.ta_cols_len;
	ecols = obj->EN_data.en_cols.en_cols_val;

	for (i = 0; i < ncols; i++) {
		if (tcols[i].tc_flags & TA_SEARCHABLE) {
			strcat(str, tcols[i].tc_name);
			strcat(str, " = \"");
			if (ecols[i].ENLEN)
				strncat(str, ecols[i].ENVAL, ecols[i].ENLEN);
			strcat(str, "\", ");
		}
	}

	str[strlen(str) - 2] = '\0';
	strcat(str, "],");
	strcat(str, obj->zo_name);
	strcat(str, ".");
	if (strlen(obj->zo_domain)) {
		strcat(str, obj->zo_domain);
	}
}

/*
 * During full resyncs, repl_objs caches the table objects, so that
 * we don't have to look them up every time we add an entry to
 * the table.
 */
static NIS_HASH_TABLE repl_objs;

/* This is the callback for nis_dump (Full Resyncs) */
int
update_directory(name, obj, x)
	nis_name	name;
	nis_object	*obj;
	void		*x;
{
	nis_db_result	*dbres;
	nis_error	status;
	nis_object	*tobj;
	char		namebuf[NIS_MAXNAMELEN+1];
	int		i, na, mc;
	entry_col	*ec;
	table_col	*tc;
	nis_attr	*attr_list;
	ping_item	*pp;

	if (obj->zo_oid.mtime > repl_stats.utime) {
		repl_stats.utime = obj->zo_oid.mtime;
	}

	/*
	 * Note :
	 *	Once entered this if block returns to the caller.
	 */
	if (__type_of(obj) == ENTRY_OBJ) {
		pp = (ping_item *)nis_find_item(name, &repl_objs);
		if (! pp) {
			dbres = db_lookup(name);
			repl_stats.ticks += dbres->ticks;
			if (dbres->status == NIS_SUCCESS)
				add_pingitem(dbres->obj, 1, &repl_objs);
			tobj = dbres->obj;
		} else
			tobj = pp->obj;

		if (tobj) {
			if (verbose) {
				nis_makename(obj, tobj, namebuf);
				syslog(LOG_INFO, "update_dir: %s", namebuf);
			}
			/* Build a fully specified name from the entry */
			mc = tobj->TA_data.ta_cols.ta_cols_len;
			tc = tobj->TA_data.ta_cols.ta_cols_val;
			ec = obj->EN_data.en_cols.en_cols_val;
			attr_list = __get_attrs(mc);
			if (attr_list == NULL) {
				syslog(LOG_WARNING,
			"update_directory: out of memory resync aborted.");
				repl_stats.errors++;
				return (1);
			}
			for (i = 0, na = 0; i < mc; i++) {
				if ((tc[i].tc_flags & TA_SEARCHABLE) != 0) {
					attr_list[na].zattr_ndx = tc[i].tc_name;
					attr_list[na].ZAVAL = ec[i].ENVAL;
					attr_list[na].ZALEN = ec[i].ENLEN;
					na++;
				}
			}
			status = __db_addib(name, na, attr_list, obj);
			if (status == NIS_SUCCESS) {
				repl_stats.successes++;
				if ((repl_stats.successes % 1000) == 0)
					syslog(LOG_INFO,
				"update_directory : %d objects, still running.",
						repl_stats.successes);
				return (0);
			} else {
				nis_makename(obj, tobj, namebuf);
				syslog(LOG_ERR,
		"replica_update (update_directory) : adding %s returned %s",
						namebuf, nis_sperrno(status));
				repl_stats.errors++;
				return (1);
			}
		}
	}

	/*
	 * At this point it isn't an ENTRY object so we're not adding
	 * it to a table.
	 */
	if (verbose) {
		nis_makename(obj, NULL, namebuf);
		syslog(LOG_INFO, "update_dir: %s", namebuf);
	}
	status = __db_add(name, obj, 0);
	if (status == NIS_SUCCESS) {
		repl_stats.successes++;
		return (0);
	} else {
		/*
		 * xxx sometimes clear_directory doesn't successfully
		 * clear out tables (bug), this tries again before
		 * giving up on the table object.
		 */
		if (__type_of(obj) == TABLE_OBJ) {
			if ((status = db_destroy(name)) == NIS_SUCCESS) {
				status = __db_add(name, obj, 0);
				if (status == NIS_SUCCESS) {
					repl_stats.successes++;
					return (0);
				}
			}
		}
		repl_stats.errors++;
		return (1);
	}
}

/*
 * make_stamp()
 *
 * This function adds a "null" entry into the log that indicates either
 * the directory is stable or gone. When a directory is deleted, a tombstone
 * (entry with a timestamp of 0) is written to the log. This prevents prior
 * activity on the log from being confused with current activity. When a
 * directory is resynchronized with a full dump, timestamp information is
 * lost so this is used to mark the directory as being stable up to that point.
 */
void
make_stamp(name, stime)
	nis_name	name;
	u_long		stime;
{
	log_entry	le;
	u_long		xid;

	memset((char *)&le, 0, sizeof (le));
	le.le_princp = nis_local_principal();
	le.le_time = stime;
	le.le_type = UPD_STAMP;
	le.le_name = name;
	__type_of(&(le.le_object)) = NO_OBJ;
	le.le_object.zo_name = "";
	le.le_object.zo_owner = "";
	le.le_object.zo_group = "";
	le.le_object.zo_domain = name;
	if (xid = begin_transaction(le.le_princp)) {
		add_update(&le);
		end_transaction(xid);
	}
}

/*
 * Make sure the directory exists, if it doesn't then we've
 * missed a create somewhere or we were just added as a replica
 * of the directory.
 */
int
verify_table_exists(nis_name name)
{
	nis_error status;

	status = db_find_table(name);
	switch (status) {
	case NIS_NOSUCHTABLE:
		if ((status = db_create(name, &tbl_prototype)) !=
		    NIS_SUCCESS) {
			syslog(LOG_ERR,
			"verify_table_exists: cannot create table for %s:%s.",
				name, nis_sperrno(status));
			return (0);
		}
		break;
	case NIS_SUCCESS:
		break;
	default:
		syslog(LOG_ERR,
		"verify_table_exists: unexpected database error for %s: %s.",
			name, nis_sperrno(status));
		return (0);
	}

	return (1);
}

/*
 * Update root object (creating it if not there before) or remove root object
 * depending on whether we're still listed as a root replica.
 * Affects root_server flag.
 * Returns 1 if successful; 0 otherwise.
 */
static int
root_replica_update()
{
	nis_object	*obj;
	nis_result	*res = 0;
	nis_name	root_dir = nis_local_directory();

#ifdef DEBUG_STALEDATA
	printf(" In root_replica_update ... \n");
#endif
	res = nis_lookup(root_dir, MASTER_ONLY);
	if (res->status != NIS_SUCCESS) {
		syslog(LOG_WARNING,
"root_replica_update : update failed '%s': could not fetch object from master.",
			root_dir);
		nis_freeresult(res);
		return (0);    /* failed, try again later. */
	}

	obj = res->objects.objects_val;
	if (we_serve(&(obj->DI_data), 0)) {
		if (verbose)
			syslog(LOG_INFO,
				"root_replica_update: updating '%s'",
				root_dir);
		if (!update_root_object(root_dir, obj)) {
			nis_freeresult(res);
			return (0);  /* failed, try again later. */
		}
	} else {
		if (verbose)
			syslog(LOG_INFO,
				"root_replica_update: removing '%s'",
				root_dir);
		remove_root_object(root_dir, obj);
	}
	nis_freeresult(res);
	return (1);
}

/*
 * replica_update();
 *
 * This function is used by the replicas to keep themselves up to date
 * with the master server. When called, it iteratively removes the names
 * of directories that have changed from it's list.
 */
int
replica_update(upd)
	ping_item	*upd;
{
	nis_name	name;	/* Directory name		*/
	nis_server	*srv;	/* Endpoint for the server	*/
	log_result	*lres;
	log_entry	*le;
	u_long		ttime;	/* timestamp retrieved from last_update() */
	u_long		xttime;	/* timestamp used to write back into the
				   transaction log when full resync is
				   completed */
	int		i, xid, nument;
	ping_item	*pp, *nxt;

	if (verbose)
		syslog(LOG_INFO, "replica_update:");

	memset((char *)(&repl_stats), 0, sizeof (repl_stats));

	if (cons)
		fprintf(cons, "replica_update : %s\n", upd->item.name);
	name = upd->item.name;

	if (CHILDPROC) {
		syslog(LOG_INFO,
		"replica_update: Child process attempting update, aborted.");
		return (1);
	}

	/* check to see if we're replicating the root object */
	if (root_object_p(name)) {
		return (root_replica_update());
	}

	srv = GETSERVER(upd->obj, 0); /* master server for directory */

	if (nis_dir_cmp(srv->name, nis_local_host()) == SAME_NAME) {
		syslog(LOG_WARNING,
"host %s thinks that it is the replica for %s, but it is the master!",
			nis_local_host(), name);
		return (1);
	}

	if (verbose)
		syslog(LOG_INFO, "replica_update: updating '%s'", name);

	if (verify_table_exists(name) == 0) {
		return (0);   /* failed, try again later. */
	}

	ttime = last_update(name);
	/*
	 * If ttime is non-zero, then get the delta from the master using
	 * nis_dumplog().
	 * If ttime is zero, then go directly to the full resync section.
	 * There is no need to even try to get the delta from the master.
	 */
	if (ttime != 0L) {
		/* get the delta from transaction log */
		if (cons)
			fprintf(cons,
				"replica_update: dumping master's log.\n");
		syslog(LOG_INFO, "replica_update : delta update of %s", name);
		lres = nis_dumplog(srv, name, ttime);
		if ((lres->lr_status != NIS_SUCCESS) && cons)
			fprintf(cons, "replica_update: error result was %s\n",
				nis_sperrno(lres->lr_status));
		if (lres->lr_status == NIS_SYSTEMERROR) {
			syslog(LOG_WARNING,
	"replica_update: Couldn't contact '%s' serving '%s' for an update.",
						srv->name, name);
			xdr_free((xdrproc_t)xdr_log_result, (char *)lres);
			return (0);
		}

		if (lres->lr_status == NIS_DUMPLATER) {
			syslog(LOG_INFO,
		    "replica_update: master server is busy, will try later.");
			xdr_free((xdrproc_t)xdr_log_result, (char *)lres);
			return (0);
		}

		if (lres->lr_status == NIS_SUCCESS) {
			if (verbose)
				syslog(LOG_INFO,
				"replica_update: %d updates from '%s'",
				lres->lr_entries.lr_entries_len, srv->name);
			if (cons)
				fprintf(cons,
				"replica_update: %d updates from '%s'\n",
				lres->lr_entries.lr_entries_len, srv->name);
			nument = lres->lr_entries.lr_entries_len;
			le = lres->lr_entries.lr_entries_val;
			xid = begin_transaction(nis_local_principal());
			for (i = 0; i < nument; i++) {
				if (cons)
					fprintf(cons,
						"apply update #%d\n", i+1);
				if (add_update(&(le[i])) == 0) {
					repl_stats.errors++;
					break;
				}
				if (cons)
					fprintf(cons, "added update to log.\n");
				if (apply_update(&(le[i])) != NIS_SUCCESS) {
					if (cons)
						fprintf(cons,
						"Failed to apply update.\n");
					syslog(LOG_ERR,
				"replica_update: Unable to apply update.");
				repl_stats.errors++;
					break;
				} else {
					if (cons)
						fprintf(cons,
							"applied update.\n");
					repl_stats.successes++;
				}
			}
			if (repl_stats.errors == 0) {
				if (cons)
					fprintf(cons,
						"Successfully updated.\n");
				end_transaction(xid);
				ttime = last_update(name);
				if (! ttime)
					/*
					 * This should never happen, otherwise
					 * this can force the replica to go
					 * into full resync.
					 */
					syslog(LOG_INFO,
			"replica_update: WARNING: last_update(%s) returned 0!",
					name);
				make_stamp(name, ttime);
				xdr_free((xdrproc_t)xdr_log_result,
					(char *)lres);
				return (1);
			} else {
				if (cons)
					fprintf(cons, "Aborted the update.\n");
				abort_transaction(xid);
				xdr_free((xdrproc_t)xdr_log_result,
					(char *)lres);
				return (0);
			}
		}

		if (lres->lr_status != NIS_RESYNC) {
			syslog(LOG_WARNING,
	"replica_update: nis_dumplog failed: srv='%s', dir='%s', err='%s'",
			srv->name, name, nis_sperrno(lres->lr_status));
			if (cons)
				fprintf(cons,
	"replica_update: nis_dumplog failed: srv='%s', dir='%s', err='%s'\n",
				srv->name, name, nis_sperrno(lres->lr_status));
			xdr_free((xdrproc_t)xdr_log_result, (char *)lres);
			return (0);
		}

		xdr_free((xdrproc_t)xdr_log_result, (char *)lres);
	}	/* end of delda update */

	/*
	 * Our log and the masters are sufficently out of date that we
	 * need to completely resync.
	 */
	if (verbose) {
		if (cons)
			fprintf(cons, "replica_update: Full dump required.\n");
		syslog(LOG_INFO, "replica_update: Full dump required.");
	}

	/*
	 * The following code handles full dump (resync) from the
	 * master.  Full dump only happens if the local timestamp
	 * returned from last_update():
	 * 	1) is 0 (zero), or
	 * 	2) cannot be found in on the master.
	 */
	syslog(LOG_INFO, "replica_update : Full dump of %s", name);

	nis_put_offline(name);
	/* clear_directory(name); */ /* we might serve stale data */
	/*
	 * Note we *don't* transact this because if we fail we'll simply
	 * restart from the beginning.
	 */
try_again:
	memset((char *)&repl_objs, 0, sizeof (NIS_HASH_TABLE));
	__nis_destroy_callback();
	lres = nis_dump(srv, name, update_directory);
	syslog(LOG_INFO, "replica_update: nis_dump result %s",
					nis_sperrno(lres->lr_status));
	syslog(LOG_INFO, "replica_update: %d updates, %d errors.",
				repl_stats.successes, repl_stats.errors);
	/*
	 * Free up the cache of table objects
	 */
	for (pp = (ping_item *)(repl_objs.first); pp; pp = nxt) {
		nxt = (ping_item *)(pp->item.nxt_item);
		(void) nis_remove_item(pp->item.name, &repl_objs);
		XFREE(pp->item.name);
		nis_destroy_object(pp->obj);
		XFREE(pp);
	}

	if (lres->lr_status == NIS_DUMPLATER) {
		syslog(LOG_INFO,
	"replica_update: master server busy, rescheduling the resync.");
		if (ttime != 0L) {
			ttime = 0;
			make_stamp(name, ttime);
		}
		/*
		 * Note, we are not addressing the case when several replicas
		 * are initialized simultaneously i.e several nismkdir -s
		 * are executed before a nisping.
		 */
		nis_put_online(name); /* serve stale data */
		xdr_free((xdrproc_t)xdr_log_result, (char *)lres);
		/*
		 * Returning zero makes sure that this directory is not
		 * deleted from the upd_list. We will try again after our
		 * poll times out.
		 */
		return (0);
	}

	if (lres->lr_status == NIS_RPCERROR) { /* callback _setup_ failed */
		if (ttime != 0L) {
			ttime = 0;
			make_stamp(name, ttime);
		}
		/*
		 * Put back online, since the directory would not have been
		 * cleared.
		 */
		nis_put_online(name);
		xdr_free((xdrproc_t)xdr_log_result, (char *)lres);
		return (0); /* don't delete from upd_list */

	} else if (lres->lr_status != NIS_CBRESULTS) { /* CB itself failed */
		syslog(LOG_WARNING,
	"replica_update: nis_dump failed: srv='%s', dir='%s', err='%s'",
			    srv->name, name, nis_sperrno(lres->lr_status));
		if (ttime != 0L) {
			ttime = 0;
			make_stamp(name, ttime);
		}
		xdr_free((xdrproc_t)xdr_log_result, (char *)lres);
		/*
		 * Do not put online since our database could be in a
		 * corrupted state.
		 */
		sleep(200);
		syslog(LOG_INFO, "replica_update: trying a full resync again.");
		goto try_again;
	} else if (repl_stats.errors)  {
		syslog(LOG_WARNING,
		"replica_update: errors during resync : srv='%s', dir='%s'",
				    srv->name, name);
		xttime = 0;
		xdr_free((xdrproc_t)xdr_log_result, (char *)lres);
		if (ttime != 0L) {
			ttime = 0;
			make_stamp(name, ttime);
		}
		/*
		 * Do not put online since our database could be in a
		 * corrupted state.
		 */
		sleep(200);
		syslog(LOG_INFO, "replica_update: trying a full resync again.");
		goto try_again;
	}
	/*
	 * Mark the now resync'd directory as stable.
	 * This is done by comparing the latest timestamp of
	 * all the objects we've seen with the latest timestamp
	 * from the master. This is required because the master
	 * stamps "remove" operations but they won't show up
	 * on a full resync.
	 */
	if (lres->lr_entries.lr_entries_len) {
		xttime = lres->lr_entries.lr_entries_val[0].le_time;
	} else {
		syslog(LOG_INFO,
"replica_update: downrev version of NIS+ service serving dir %s as master.",
			name);
		xttime = nis_cptime(srv, name);
		xttime = (xttime < repl_stats.utime) ?
					repl_stats.utime : xttime;
	}
	if (cons)
		fprintf(cons, "Latest update was '%s'\n",
				ctime((time_t *)&repl_stats.utime));
	if (verbose)
		syslog(LOG_INFO,
			"replica_update : directory %s updated", name);

	/* Add it to the list of directories that it serves */
	nis_server_control(SERVING_LIST, DIR_ADD, name);
	xdr_free((xdrproc_t)xdr_log_result, (char *)lres);
	nis_put_online(name);

	/*
	 * The full resync is now complete.  We need to put the
	 * current timestamp for the directory in the local
	 * transaction log.
	 * NOTE: variable "xttime" contains the valid timestamp
	 * for the directory.  It should never be 0.  However,
	 * if we do get this, we'll have to assume that there
	 * were some problem and we should force another resync.
	 */
	if ((xttime != 0L) || ((xttime == 0L) && (ttime != 0L)))
		/*
		 * Always stamp with valid timestamp.
		 * We only stamp with timestamp of 0 if there
		 * isn't already a timestamp of 0 in the
		 * transaction log.
		 */
		make_stamp(name, xttime);
	if (xttime == 0L)
		syslog(LOG_DEBUG,
	"replica_update: timestamp=0 after full resync completed!");
	return (1);
}

#ifdef PING_FORK
/*
 * ping_replicas()
 *
 * This function will send a "ping" to the replicas for a given directory
 * indicating that they should be ready to get updates to the database.
 *
 * XXX it should create a thread, instead it forks.
 *
 * Once the fork is successful the operation is presumed to complete.
 *
 */
int
ping_replicas(pung)
	ping_item	*pung;
{
	pid_t	pid;

	pid = fork();
	if (pid == -1) {
		if (verbose)
			syslog(LOG_INFO, "ping_replicas: fork failed");
		return (0);
	} else if (pid) {
		if (verbose)
			syslog(LOG_INFO,
			"ping_replicas: forked ping child pid %d", pid);
		return (1);
		}

	if (verbose)
		syslog(LOG_INFO, "ping_replicas: Pinging %s", pung->item.name);

	if (verbose)
		syslog(LOG_INFO, "ping_replicas: directory %s time %ld",
			pung->item.name, pung->mtime);
	nis_ping(pung->item.name, pung->mtime, pung->obj);
	exit(0); /* child simply exits */
}

#else

static void
ping_replicas_thread(t_pung)
	ping_item	*t_pung;
{

	if (verbose)
		syslog(LOG_INFO, "ping_replicas: Pinging %s",
			t_pung->item.name);

	nis_ping(t_pung->item.name, t_pung->mtime, t_pung->obj);

	if (t_pung->obj)
		nis_destroy_object(t_pung->obj);
	XFREE(t_pung->item.name);
	XFREE(t_pung);


	if (verbose)
		syslog(LOG_INFO, "ping_replicas_thread: directory %s time %ld",
			t_pung->item.name, t_pung->mtime);

	thr_exit(0);	/*	thread simply exits	*/
}



/*
 * ping_replicas()
 *
 * This function will send a "ping" to the replicas for a given directory
 * indicating that they should be ready to get updates to the database.
 *
 * It creates a thread to send the ping.
 * NOTE:        The main thread destroys the ping_item "pung" when
 *              ping_replicas() returns. To avoid problems, the
 *              ping_item is first cloned. The cloned ping_item
 *              must be destroyed in the thread.
 *
 * Once the thr_create is successful the operation is presumed to complete.
 *
 */
int
ping_replicas(pung)
	ping_item	*pung;
{
	thread_t	tid;
	sigset_t	new_mask, orig_mask;
	int		error;

	if (sigemptyset(&new_mask) != 0) {
		if (verbose)
			syslog(LOG_ERR,
				"ping_replicas: Error (%d) zeroing mask for %s",
				errno, pung->item.name);
		return (FALSE);
	}
	if (thr_sigsetmask(SIG_SETMASK, &new_mask, &orig_mask) != 0) {
		if (verbose)
			syslog(LOG_ERR,
				"ping_replicas: Error (%d) setting mask for %s",
				errno, pung->item.name);
		return (FALSE);
	}
	error = thr_create(NULL, 0,
		(void *(*)(void *))ping_replicas_thread, (void *)pung,
		THR_DETACHED, &tid);
	if (thr_sigsetmask(SIG_SETMASK, &orig_mask, NULL) != 0) {
		if (verbose)
			syslog(LOG_ERR,
			"ping_replicas: Error (%d) restoring mask for %s",
			errno, pung->item.name);
	}
	if (error) {
		if (verbose)
			syslog(LOG_ERR,
			"ping_replicas: Error (%d) creating ping thread for %s",
				error, pung->item.name);
		return (FALSE);
	}
	if (tid) {
		if (verbose)
			syslog(LOG_INFO,
				"ping_replicas: Created ping thread %d for %s",
				tid, pung->item.name);
		return (TRUE);
	} else
		return (FALSE);
}

#endif /* ifdef PING_FORK */

/*
 * Recursion Safe versions of the lookup internals.
 *
 * These functions are the internal functions that are used by the
 * lookup and list functions in the library. When the server is linked
 * against libnsl, these functions replace those in the library and make
 * it safe for use to call nis_lookup() or nis_list().
 */

/*
 * nis_local_lookup()
 *
 * Lookup the requested information by calling the services listsvc or
 * lookup_svc entry points.
 */
nis_result *
nis_local_lookup(req, list_op, res, flags, slist)
	ib_request	*req;
	int		list_op;
	nis_result	*res;
	u_long		flags;
	directory_obj	*slist;
{
	ns_request	nsr;
	nis_result	*local_res;
	int		i;

	if (verbose)
		syslog(LOG_INFO, "nis_local_lookup: making local call.");
	/*
	 * Depending on name or list_op either
	 * list it or look it up in the namespace.
	 */
	if ((req->ibr_srch.ibr_srch_len > 0) || (list_op)) {
		if (verbose)
			syslog(LOG_INFO, "local LIST");
		local_res = nis_iblist_svc(req, NULL);
	} else {
		if (verbose)
			syslog(LOG_INFO, "local LOOKUP");
		nsr.ns_name = req->ibr_name;
		nsr.ns_object.ns_object_len = 0;
		nsr.ns_object.ns_object_val = NULL;
		local_res = nis_lookup_svc(&nsr, NULL);
	}

	/*
	 * Now duplicate the result for the client so that the
	 * client can call nis_freeresult() with impunity.
	 */
	*res = *local_res;
	if (res->objects.objects_len) {
		res->objects.objects_val = (nis_object *)
		    calloc(res->objects.objects_len,
				sizeof (nis_object));
		for (i = 0; i < res->objects.objects_len; i++)
			(void) nis_clone_object(
				local_res->objects.objects_val+i,
				res->objects.objects_val+i);
	}
	if (res->cookie.n_len) {
		res->cookie.n_bytes = (char *)
				malloc(res->cookie.n_len);
		memcpy(res->cookie.n_bytes, local_res->cookie.n_bytes,
					    local_res->cookie.n_len);
	}
	return (res);
}

#define	GOOD_SERVER	0
#define	BAD_SERVER	1

CLIENT *
nis_remote_lookup(req, list_op, res, flags, slist)
	ib_request	*req;
	int		list_op;
	nis_result	*res;
	directory_obj	*slist;
	u_long		flags;
{
	ns_request	nsr;
	CLIENT		*cur_server;
	enum clnt_stat	stat;
	struct timeval	tv;

	if (verbose)
		syslog(LOG_INFO, "nis_remote_lookup");
	/*
	 * select a server, from the list that serve dir
	 * note: No way to get authenication error back from
	 * this function call.
	 */
	cur_server = __nis_get_server(slist, flags);

	if (! cur_server) {
		/* may in fact be an auth error */
		res->status = NIS_NAMEUNREACHABLE;
		return (NULL);
	}

	/*
	 * Depending on name or list_op either list it or
	 * look it up in the namespace.
	 */
	memset((char *)res, 0, sizeof (nis_result));
	tv.tv_sec = 30;
	tv.tv_usec = 0;
	if (list_op) {
		stat = clnt_call(cur_server, NIS_IBLIST,
					xdr_ib_request, (char *) req,
					xdr_nis_result, (char *) res, tv);
	} else {
		memset((char *)&nsr, 0, sizeof (nsr));
		nsr.ns_name = req->ibr_name;
		stat = clnt_call(cur_server, NIS_LOOKUP,
					xdr_ns_request, (char *) &nsr,
					xdr_nis_result, (char *) res, tv);
	}
	/*
	 * If the RPC failed coerce the status into an NIS_RPCERROR
	 * and try the next server in the list.
	 * If there are no more servers, above code will return a
	 * NIS_NAMEUNREACHABLE error when __nis_get_server() returns
	 * NULL.
	 */
	if (stat == RPC_AUTHERROR) {
		res->status = NIS_SRVAUTH;
	} else if (stat != RPC_SUCCESS) {
		res->status = NIS_RPCERROR;
	}
	return (cur_server);
}

/*
 * we_server()
 *
 * return TRUE if an RPC call to this directory might call us
 * back.
 */
int
we_serve(srv, flags)
	directory_obj	*srv;
	u_long		flags;
{
	nis_server	*servers;
	int		ns, i;

	if (flags & MASTER_ONLY)
		ns = 1;
	else
		ns = srv->do_servers.do_servers_len;
	servers = srv->do_servers.do_servers_val;
	/*
	 * XXX NB: if the server name doesn't match but the
	 * address does, then we'll miss this check and recurse
	 * anyway. Sigh.
	 */
	for (i = 0; i < ns; i++)
		if (nis_dir_cmp(servers[i].name, nis_local_host())
				== SAME_NAME)
			return (1);
	return (0);
}

static void
count_me_out(directory_obj* dobj)
{
	extern void __nis_modify_dirobj(char *, directory_obj *);

	__nis_modify_dirobj(nis_local_host(), dobj);
}

/*
 * nis_core_lookup()
 *
 * The bones of the lookup and list function, this function binds to, then
 * calls the appropriate NIS+ server for the given name. If the HARD_LOOKUP
 * flag is set it is ignored because the server cannot afford to block.
 *
 * NB: This function now follows links if flags is set up correctly. THis
 * localized this policy to this function and eliminated about 4
 * implementations of the same code in other modules.
 */
nis_result *
__nis_core_lookup(req, flags, list_op, cbdata, cback)
	ib_request	*req;		/* name parameters		*/
	u_long		flags;		/* user flags			*/
	int		list_op;	/* list semantics 		*/
	void		*cbdata;	/* Callback data		*/
	int		(*cback)();	/* Callback (for list calls) 	*/
{
	nis_result	*res;		/* the result we return */
	directory_obj	slist;		/* to hold binding info	*/
	CLIENT		*cur_server;	/* server handle	*/
	int		error, linknum,	/* state vars		*/
			binding_policy;	/* true for name_first binding */
	char		curname[NIS_MAXNAMELEN]; /* name used	*/
	ib_request	local_req;	/* local version of request */
	nis_object	link_obj;	/* used when following links */
	unsigned long	aticks = 0,	/* profiling variables	*/
			cticks = 0,
			dticks = 0,
			zticks = 0;
	int		local_srv,	/* whether call was serviced locallly */
			newname = 1,    /* was retry due to using new name? */
			flushed_cache = 0, /* has cache been flushed before? */
			soft_errors = 0, /* soft errors encountered so far */
			soft_error_limit; /* soft error limit */

	if (verbose)
		syslog(LOG_INFO, "__nis_core_lookup: (safe) called on %s",
							req->ibr_name);
	res = (nis_result *)calloc(1, sizeof (nis_result));
	if (! res)
		return (NULL);	/* Crash and burn */

	/*
	 * AS THE SERVICE, we don't support callbacks
	 */
	if (cbdata || cback) {
		res->status = NIS_NOCALLBACK;
		return (res);
	}

	/*
	 * start out using the request as passed to us, we clone it
	 * to prevent damage to the user's version. Then we use a local
	 * name buffer that we can update when we're following links.
	 */
	strcpy(curname, req->ibr_name);
	local_req = *req;
	local_req.ibr_name = &(curname[0]);
	binding_policy = 0; /* never do lists on directories */

	memset((char *)&link_obj, 0, sizeof (nis_object));

	/*
	 * NOTE : Binding bug : due to the implementation of the
	 *	NIS+ service, when you want to list a directory you
	 *	need to bind not to a server that serves the directory
	 *	object, but to a server that serves the directory itself.
	 *	This will often be different machines.
	 *
	 *	However, we can mitigate the impact of always trying to
	 *	bind to the table if we're searching a table by checking
	 *	for search criteria. (listing directories can't have a
	 *	search criteria). So if we're a list, and we don't have
	 *	a search criteria, bind to the "name" passed first. Otherwise
	 *	attempt to bind to domain_of(name) first.
	 *
	 *	NIS_S_SUCCESS means the cache knew about the directory but
	 *	the directory object has expired.
	 */
	linknum = 0;
	flushed_cache = 0;	/* has cache been flushed before? */
	while (1) {
		/* get a directory obj describing the desired dir */
		if (newname) {
			memset((char *)&slist, 0, sizeof (directory_obj));
			res = __nis_make_binding(&res, curname, &slist,
						    binding_policy);

			/* Determine what the soft error limit should be */
			if (flags&MASTER_ONLY)
				soft_error_limit = 1;
			else
				soft_error_limit =
						slist.do_servers.do_servers_len;
			aticks += res->aticks;

			/* Each newname gets new chance to flush cache once */
			soft_errors = 0;
			newname = 0;
		}

		/*
		 * special handling for root replicas.
		 *
		 * If the following conditions are met :
		 * 1) the directory we're going to query == our domain name
		 * 2) we allegedly serve it.
		 * 3) we don't have the "root_server" flag set
		 * Then we are a root_replica and we need to shunt this
		 * request to the master root server who will read the
		 * root object and return it to us.
		 */
		if ((nis_dir_cmp(slist.do_name, nis_local_directory())
							== SAME_NAME) &&
				! root_server && we_serve(&slist, flags)) {
			flags |= MASTER_ONLY;
			if (we_serve(&slist, flags)) {
				res->status = NIS_FAIL;
				break;  /* out of while loop */
			}
		}

		/* if no such binding, return error we received */
		if ((res->status != NIS_SUCCESS) &&
		    (res->status != NIS_S_SUCCESS)) {
			/* check to see if we were following a link */
			if (__type_of(&link_obj) == LINK_OBJ) {
				res->objects.objects_val =
					    nis_clone_object(&link_obj, NULL);
				if (! res->objects.objects_val) {
					res->status = NIS_NOMEMORY;
					return (res);
				}
				res->objects.objects_len = 1;
				res->status = NIS_LINKNAMEERROR;
				/* free our copy */
				xdr_free(xdr_nis_object, (char *) &link_obj);
			}
			break;  /* out of while loop */
		}

		aticks += res->aticks;

		/*
		 * As a server, we may serve the indicated directory, if we
		 * do then we *DON'T* do an RPC, rather we just call our
		 * selves. Note this *can* recurse.
		 */
		if (local_srv = we_serve(&slist, flags)) {
			nis_local_lookup(&local_req, list_op, res, flags,
					&slist);
		} else {
			cur_server = nis_remote_lookup(&local_req,
						list_op, res, flags, &slist);
		}

		/*
		 * note even if link_obj was never used this works because
		 * the xdr_free becomes a no-op. If we were following links
		 * then this cleans up the last object we followed and possibly
		 * invalidates the search attribute pointers in local_req
		 */
		xdr_free(xdr_nis_object, (char *) &link_obj);
		memset((char *) &link_obj, 0, sizeof (nis_object));

		/* note the statistics from the result */
		zticks += res->zticks;
		dticks += res->dticks;
		cticks += res->cticks;
		aticks += res->aticks;

		/* be prepared to return the totals in res */
		res->zticks = zticks;
		res->dticks = dticks;
		res->cticks = cticks;
		res->aticks = aticks;

		/*
		 * now process the result of the call to the service.
		 * We either return to the caller from this switch
		 * statement or go around again.
		 */
		switch (res->status) {
		case NIS_NAMEUNREACHABLE :
			if (flags & HARD_LOOKUP) {
				syslog(LOG_WARNING,
		"NIS+ server for %s not responding, HARD_LOOKUP flag ignored",
					curname);
			}
			break;  /* return error */
		case NIS_SRVAUTH :
			/* first auth error, see if refreshing cache helps. */
			if (flushed_cache == 0) {
				if (!local_srv)
					__nis_bad_auth_server(cur_server);
				__nis_CacheRemoveEntry(&slist);
				flushed_cache = 1;
				newname = 1;
				xdr_free(xdr_directory_obj, (char *) &slist);
				res->status = NIS_SUCCESS; /* try again */
				break;
			}
			/* otherwise, continue with soft error handling */
		case NIS_RPCERROR :
		case NIS_NOT_ME :
			/*
			 * NOT_ME means the replica hasn't had a chance
			 * to create a directory yet. So ignore it as
			 * though it didn't answer.
			 */
			if (local_srv) {
				count_me_out(&slist);
			} else
				__nis_release_server(cur_server, BAD_SERVER);
			++soft_errors;
			if (soft_errors <= soft_error_limit)
				res->status = NIS_SUCCESS; /* try again */
			break;
		case NIS_CBRESULTS :
			syslog(LOG_ERR, "NIS_CBRESULTS in __nis_core_lookup");
			res->status = NIS_NOCALLBACK;
#if 0
/*
 *  The server never uses callbacks, so this section of code is never
 *  run (see the check at the beginning of this function that makes sure
 *  that cback and cbdata are NULL).  I am commenting this code out so
 *  that things like lint don't complain about the use of cur_server.
 *  I am leaving the text here in case someone needs to make this work
 *  in the future.
 */
			/*
			 * Callback calling, start the callback service
			 * running and collect results.
			 */
			error = __nis_run_callback(&(NIS_RES_COOKIE(res)),
				    NIS_CALLBACK, 0, cur_server);
			if (error < 0)
				res->status = -error;
#endif /* 0 */
			break;
		case NIS_SUCCESS :
			/*
			 * We actually found something, if it is a link
			 * and we are "following" links, then we set that
			 * up. Otherwise we can just return it.
			 */
			if (!local_srv)
				__nis_release_server(cur_server, GOOD_SERVER);
			if ((flags & FOLLOW_LINKS) == 0) {
				xdr_free(xdr_directory_obj, (char *) &slist);
				return (res);  /* just return it */
			}
			linknum++;
			if (linknum > NIS_MAXLINKS) {
				res->status = NIS_LINKNAMEERROR;
				break; /* return error */
			}

			if (__type_of(res->objects.objects_val) != LINK_OBJ) {
				xdr_free(xdr_directory_obj, (char *) &slist);
				return (res); /* return it */
			}

			/* clone the link, freed after the clnt_call */
			(void) nis_clone_object(res->objects.objects_val,
								&link_obj);

			strcpy(curname, link_obj.LI_data.li_name);
			newname++;
			flushed_cache = 0; /* in case of previous AUTH err */
			xdr_free(xdr_directory_obj, (char *) &slist);

			/* check to see if it points at entries */
			if (link_obj.LI_data.li_attrs.li_attrs_len) {
				/* can only happen once */
				local_req.ibr_srch.ibr_srch_len =
					link_obj.LI_data.li_attrs.li_attrs_len;
				local_req.ibr_srch.ibr_srch_val =
					link_obj.LI_data.li_attrs.li_attrs_val;
				list_op = TRUE;
				/* must be a table w/ searchable columns */
				binding_policy = 0;
			}

			/* free result but keep the pointer */
			xdr_free(xdr_nis_result, (char *) res);
			memset((char *) res, 0, sizeof (nis_result));
			res->status = NIS_SUCCESS;
			break;  /* try again */
		case NIS_PARTIAL :
			/*
			 * This is what the list call will return when
			 * it hits a link so check for that. note tables
			 * don't care about search criteria in the link
			 * (its an error) so the link object cloning isn't
			 * needed.
			 */
			if (!local_srv)
				__nis_release_server(cur_server, GOOD_SERVER);
			if ((flags & FOLLOW_LINKS) == 0)
				break;  /* just return it */
			if (__type_of(res->objects.objects_val) != LINK_OBJ)
				break;  /* just return it */
			linknum++;
			if (linknum > NIS_MAXLINKS) {
				res->status = NIS_LINKNAMEERROR;
				break;  /* return link error */
			}

			if (
	    res->objects.objects_val[0].LI_data.li_attrs.li_attrs_len != 0) {
				res->status = NIS_LINKNAMEERROR;
				break;  /* return error */
			}

			strcpy(curname, link_obj.LI_data.li_name);
			newname++;
			flushed_cache = 0; /* if a AUTH err had occurred */
			xdr_free(xdr_directory_obj, (char *) &slist);

			/* free result but keep the pointer */
			xdr_free(xdr_nis_result, (char *) res);
			memset((char *) res, 0, sizeof (nis_result));
			res->status = NIS_SUCCESS;
			break;  /* try next link */

		default:
			if (!local_srv)
				__nis_release_server(cur_server, GOOD_SERVER);
			/* All other cases, return the result */
			break;
		} /* switch res->status */
	} /* the "forever" loop */
	xdr_free(xdr_directory_obj, (char *) &slist);
	return (res);
}

fd_result *
dup_fdres(o, n)
	fd_result	*o;	/* The result to duplicate 	*/
	fd_result	*n;	/* Where to put the data	*/
{

#ifdef DEBUG
	if (verbose)
		syslog(LOG_INFO, "dup_fdres: cloning the FD result.");
#endif

	*n = *o;

	if (o->source)
		n->source = strdup(o->source);

	if (o->dir_data.dir_data_val) {
		n->dir_data.dir_data_val = (char *)
					    malloc(o->dir_data.dir_data_len);
		if (! n->dir_data.dir_data_val) {
			syslog(LOG_ERR, "dup_fdres: Out of memory!");
			return (NULL);
		}
		memcpy(n->dir_data.dir_data_val, o->dir_data.dir_data_val,
						o->dir_data.dir_data_len);
	}
	if (o->signature.signature_val) {
		n->signature.signature_val = (char *)
					    malloc(o->signature.signature_len);
		if (! n->signature.signature_val) {
			syslog(LOG_ERR, "dup_fdres: Out of memory!");
			if (n->dir_data.dir_data_val)
				free(n->dir_data.dir_data_val);
			return (NULL);
		}
		memcpy(n->signature.signature_val, o->signature.signature_val,
						o->signature.signature_len);
	}
	return (n);
}

/*
 * nis_finddirectory()
 *
 * This function will ask one of the servers of the given directory
 * where some unknown directory "name" is. This function is called
 * from within the __nis_CacheBind() code so is generally not needed by
 * the client. If the directory cannot be found it returns NULL.
 */
fd_result *
nis_finddirectory(slist, name)
	directory_obj		*slist;
	nis_name		name;
{
	fd_result	*res;
	fd_result	*svc_res;
	fd_args		argp;
	int		i, ns;
	enum clnt_stat	stat;
	CLIENT		*clnt;
	nis_server	*srv;
	nis_server	*srvlist;
	struct timeval	tv;
	enum name_pos	p;
	h_mask		tried;		/* list of machines tried */
	h_mask		favored;	/* list of machines favored */

	if (verbose)
		syslog(LOG_INFO, "nis_finddirectory (safe) : called.");
	memset((char *)&argp, 0, sizeof (argp));
	res = (fd_result *) XCALLOC(1, sizeof (fd_result));
	add_cleanup((void (*)())XFREE, (char *) res, "fd_result (safe) result");
	argp.dir_name = name;
	argp.requester = nis_local_host();
	p = nis_dir_cmp(name, nis_local_directory());

	ns = slist->do_servers.do_servers_len;
	/*
	 * special case the root replica servers.
	 */
	if (we_serve(slist, 0) && (p != LOWER_NAME) && ! root_server)
		ns = 1; /* effectively "master only" */

	srvlist = slist->do_servers.do_servers_val;

	/*
	 * If this server is on the list and we have a database for it,
	 * just call the svc routine
	 */
	for (i = 0; i < ns; i++) {
		if (nis_dir_cmp(srvlist[i].name, argp.requester) ==
		    SAME_NAME) {
			/* Make sure we've got a database for it. */
			nis_error tab_status = db_find_table(slist->do_name);
			if (tab_status == NIS_NOSUCHTABLE) {
				if (we_serve(slist, MASTER_ONLY))
					tab_status = NIS_SYSTEMERROR;
				else
					tab_status = NIS_NOT_ME;
			}

			switch (tab_status) {
			case NIS_SUCCESS:
				svc_res = nis_finddirectory_svc(&argp, NULL);
				/*
				 * Note, we "clone" the find directory result
				 * because the cache client code will free it.
				 */
				if (svc_res && dup_fdres(svc_res, res))
					return (res);
				return (NULL);

			case NIS_NOT_ME:
				/* not master */
				count_me_out(slist);
				ns = slist->do_servers.do_servers_len;
				srvlist = slist->do_servers.do_servers_val;
				goto try_srvlist;

			default:
				/* _fd_res puts returned obj on rags list */
				svc_res = __fd_res(argp.requester, tab_status,
						    NULL);
				/* clone fd_result for cache to free */
				if (svc_res && dup_fdres(svc_res, res))
					return (res);
				return (NULL);
			}
		}
	}

	/* Make an RPC call to locate the directory. */

try_srvlist:
	if (verbose)
		syslog(LOG_INFO, "nis_finddirectory (safe) : using rpc.");
	h_mask_clear(tried);
	h_mask_clear(favored);
	tv.tv_sec = 10;
	tv.tv_usec = 0;
	for (i = 0; i < ns; i++) {	/* do atmost ns times */
		srv = __nis_select_replica(srvlist, ns, favored, tried);
		if (srv == NULL) /* couldn't find one */
			break;

		clnt = nis_make_rpchandle(srv, 0, NIS_PROG, NIS_VERSION,
		    ZMH_DG, 2048, 2048);
		if (clnt) {
			stat = clnt_call(clnt, NIS_FINDDIRECTORY,
					    xdr_fd_args, (char *) &argp,
					    xdr_fd_result, (char *) res, tv);
			auth_destroy(clnt->cl_auth);
			clnt_destroy(clnt);
			if (stat == RPC_SUCCESS)
				return (res);
		}
	}
	res->status = NIS_NAMEUNREACHABLE;
	return (res);
}

int
update_root_object(nis_name root_dir, nis_object *d_obj)
{
	if (nis_write_obj(nis_data(ROOT_OBJ), d_obj)) {
		writeColdStartFile(&(d_obj->DI_data));
		__nis_CacheRestart();
		flush_local_dircache(root_dir);
		if (! root_server)
			root_server = 1;
		return (1);
	} else
		return (0);
}

nis_object*
get_root_object()
{
	/*
	 * XXX Maybe this can be cached and updated whenever
	 * update_root_object is called instead of being reread ???
	 */
	nis_object* d_obj;

	d_obj = nis_read_obj(nis_data(ROOT_OBJ));
	return (d_obj);
}

int
root_object_p(nis_name name)
{
	return (strcmp(name, ROOT_OBJ) == 0);
}

int
remove_root_object(nis_name root_dir, nis_object* d_obj)
{
	unlink(nis_data(ROOT_OBJ));
	writeColdStartFile(&(d_obj->DI_data));
	__nis_CacheRestart();
	flush_local_dircache(root_dir);
	root_server = 0;
	return (1);
}
