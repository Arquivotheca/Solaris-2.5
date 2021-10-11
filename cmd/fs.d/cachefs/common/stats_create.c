/*
 *
 *			stats_create.c
 *
 * Routines for the `clean interface' to cachefs statistics.
 */

#pragma ident "@(#)stats_create.c   1.11     94/11/11 SMI"

/*
 *  Copyright (c) 1994, by Sun Microsystems, Inc.
 *  All Rights Reserved.
 */

#include <varargs.h>
#include <libintl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/fs/cachefs_fs.h>
#include <string.h>
#include "stats.h"

/* forward declarations of statics */
static stats_cookie_t *stats_create(char *);

static stats_cookie_t *
stats_create(char *progname)
{
	stats_cookie_t *rc;

	if ((rc = (stats_cookie_t *) calloc(1, sizeof (*rc))) == NULL)
		goto out;

	rc->st_magic = STATS_MAGIC;
	if (rc->st_progname = strrchr(progname, '/'))
		rc->st_progname++;
	else
		rc->st_progname = progname;

	if ((rc->st_kstat_cookie = kstat_open()) == NULL) {
		stats_perror(rc, SE_KERNEL,
		    gettext("Cannot initialize kstats"));
		goto out;
	}

out:
	return (rc);
}

stats_cookie_t *
stats_create_unbound(char *progname)
{
	stats_cookie_t *st;

	if ((st = stats_create(progname)) == NULL)
		goto out;

	st->st_flags |= ST_VALID;

out:
	return (st);
}

stats_cookie_t *
stats_create_mountpath(char *mountpath, char *progname)
{
	stats_cookie_t *st;
	kstat_t *key;
	cachefs_kstat_key_t *k;
	dev_t dev;
	ino_t ino;
	struct stat s;
	int i, n;

	if ((st = stats_create(progname)) == NULL)
		goto out;

	if ((key = kstat_lookup(st->st_kstat_cookie, "cachefs", 0, "key"))
	    == NULL) {
		stats_perror(st, SE_KERNEL,
		    gettext("Cannot lookup cachefs key kstat"));
		goto out;
	}
	if (kstat_read(st->st_kstat_cookie, key, NULL) < 0) {
		stats_perror(st, SE_KERNEL,
		    gettext("Cannot read cachefs key kstat"));
		goto out;
	}
	k = (cachefs_kstat_key_t *) key->ks_data;
	n = key->ks_ndata;

	if (stat(mountpath, &s) != 0) {
		stats_perror(st, SE_FILE,
		    gettext("Cannot stat %s"), mountpath);
		goto out;
	}
	ino = s.st_ino;
	dev = s.st_dev;

	for (i = 0; i < n; i++) {
		k[i].ks_mountpoint += (int) k;
		k[i].ks_backfs += (int) k;
		k[i].ks_cachedir += (int) k;
		k[i].ks_cacheid += (int) k;

		if (! k[i].ks_mounted)
			continue;

		if ((stat(k[i].ks_mountpoint, &s) == 0) &&
		    (s.st_dev == dev) &&
		    (s.st_ino == ino))
			break;
	}

	if (i >= n) {
		stats_perror(st, SE_FILE,
		    gettext("%s: not a cachefs mountpoint"), mountpath);
		goto out;
	}

	st->st_fsid = k[i].ks_id;

	st->st_flags |= ST_VALID | ST_BOUND;

out:
	return (st);
}

/*
 * stats_next - bind the cookie to the next valid cachefs mount.
 *
 * returns cachefs_kstat_key_t *, which gives all the info you need.
 * returns NULL if we're out of mounts, or if an error occured.
 * returns malloc()ed data, which the client has to free() itself.
 */

cachefs_kstat_key_t *
stats_next(stats_cookie_t *st)
{
	kstat_t *key;
	cachefs_kstat_key_t *k, *prc = NULL, *rc = NULL;
	int i, n;

	assert(stats_good(st));

	if (((key = kstat_lookup(st->st_kstat_cookie, "cachefs", 0,
	    "key")) == NULL) ||
	    (kstat_read(st->st_kstat_cookie, key, NULL) < 0)) {
		stats_perror(st, SE_KERNEL,
		    gettext("Cannot get cachefs key kstat"));
		goto out;
	}
	k = (cachefs_kstat_key_t *) key->ks_data;
	n = key->ks_ndata;

	if (st->st_flags & ST_BOUND) {
		for (i = 0; i < n; i++)
			if (st->st_fsid == k[i].ks_id)
				break;
		++i;
		if (i < n) {
			prc = k + i;
			st->st_fsid = k[i].ks_id;
		} else
			st->st_flags &= ~ST_BOUND;
	} else if (n > 0) {
		st->st_fsid = k[0].ks_id;
		st->st_flags |= ST_BOUND;
		prc = k;
	}

out:
	if (prc != NULL) {
		char *s;
		int size;

		prc->ks_mountpoint += (int) k;
		prc->ks_backfs += (int) k;
		prc->ks_cachedir += (int) k;
		prc->ks_cacheid += (int) k;

		size = sizeof (*rc);
		size += strlen(prc->ks_mountpoint) + 1;
		size += strlen(prc->ks_backfs) + 1;
		size += strlen(prc->ks_cachedir) + 1;
		size += strlen(prc->ks_cacheid) + 1;

		if ((rc = (cachefs_kstat_key_t *)
		    malloc(size)) == NULL) {
			stats_perror(st, SE_NOMEM,
			    gettext("Cannot malloc return code"));
		} else {
			memcpy(rc, prc, sizeof (*rc));
			s = (char *) rc + sizeof (*rc);

			(void) strcpy(s, prc->ks_mountpoint);
			rc->ks_mountpoint = s;
			s += strlen(s) + 1;
			(void) strcpy(s, prc->ks_backfs);
			rc->ks_backfs = s;
			s += strlen(s) + 1;
			(void) strcpy(s, prc->ks_cachedir);
			rc->ks_cachedir = s;
			s += strlen(s) + 1;
			(void) strcpy(s, prc->ks_cacheid);
			rc->ks_cacheid = s;
		}
	}

	return (rc);
}

cachefs_kstat_key_t *
stats_getkey(stats_cookie_t *st)
{
	kstat_t *ksp;
	cachefs_kstat_key_t *k, *key, *rc = NULL;
	int size;
	char *s;

	assert(stats_good(st));
	assert(st->st_flags & ST_BOUND);

	if (((ksp = kstat_lookup(st->st_kstat_cookie, "cachefs", 0,
	    "key")) == NULL) ||
	    (kstat_read(st->st_kstat_cookie, ksp, NULL) < 0)) {
		stats_perror(st, SE_KERNEL,
		    gettext("Cannot get cachefs key kstat"));
		goto out;
	}
	key = (cachefs_kstat_key_t *) ksp->ks_data;
	k = key + st->st_fsid - 1;
	k->ks_mountpoint += (int) key;
	k->ks_backfs += (int) key;
	k->ks_cachedir += (int) key;
	k->ks_cacheid += (int) key;
	size = sizeof (*rc);
	size += strlen(k->ks_mountpoint) + 1;
	size += strlen(k->ks_backfs) + 1;
	size += strlen(k->ks_cachedir) + 1;
	size += strlen(k->ks_cacheid) + 1;

	if ((rc = (cachefs_kstat_key_t *) malloc(size)) == NULL)
		stats_perror(st, SE_NOMEM,
		    gettext("Cannot malloc return code"));
	else {
		memcpy(rc, k, sizeof (*rc));
		s = (char *) rc + sizeof (*rc);

		(void) strcpy(s, k->ks_mountpoint);
		rc->ks_mountpoint = s;
		s += strlen(s) + 1;
		(void) strcpy(s, k->ks_backfs);
		rc->ks_backfs = s;
		s += strlen(s) + 1;
		(void) strcpy(s, k->ks_cachedir);
		rc->ks_cachedir = s;
		s += strlen(s) + 1;
		(void) strcpy(s, k->ks_cacheid);
		rc->ks_cacheid = s;
		s += strlen(s) + 1;
	}

	assert(rc->ks_id == st->st_fsid);

out:
	return (rc);
}

void
stats_destroy(stats_cookie_t *st)
{
	if (st == NULL)
		return;

	if (st->st_kstat_cookie != NULL)
		kstat_close(st->st_kstat_cookie);
	if (st->st_logxdr.x_ops != NULL)
		xdr_destroy(&st->st_logxdr);
	if ((st->st_logstream != NULL) && (st->st_flags & ST_LFOPEN))
		(void) fclose(st->st_logstream);

	/*
	 * we don't want to depend on dbm (or stats_dbm), so we don't
	 * do a stats_dbm_close.  we do try to require the client to
	 * have done it, via an assert(), however.
	 */

	assert(! (st->st_flags & ST_DBMOPEN));

	st->st_magic++;

	free(st);
}

int
stats_good(stats_cookie_t *st)
{
	if (st == NULL)
		return (0);
	if (st->st_magic != STATS_MAGIC)
		return (0);
	if (! (st->st_flags & ST_VALID))
		return (0);

	return (1);
}

void
/*PRINTFLIKE3*/
stats_perror(stats_cookie_t *st, int errno, char *fmt, ...)
{

	va_list ap;

	assert(st != NULL);
	assert(st->st_magic == STATS_MAGIC);

	va_start(ap);
	(void) vsprintf(st->st_errorstr, fmt, ap);
	va_end(ap);

	st->st_errno = errno;

	st->st_flags |= ST_ERROR;
}

char *
stats_errorstr(stats_cookie_t *st)
{
	assert(st != NULL);
	assert(st->st_magic == STATS_MAGIC);

	return (st->st_errorstr);
}

int
stats_errno(stats_cookie_t *st)
{
	assert(st != NULL);
	assert(st->st_magic == STATS_MAGIC);

	return (st->st_errno);
}

int
stats_inerror(stats_cookie_t *st)
{
	assert(st != NULL);
	assert(st->st_magic == STATS_MAGIC);

	return (st->st_flags & ST_ERROR);
}
