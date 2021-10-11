/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 *
 * Default backend-finder(s) for the name-service-switch routines.
 * At present there is a single finder that uses dlopen() to do its thing.
 *
 * === Could also do a finder that includes db-name in filename
 * === and one that does dlopen(0) to check in the executable
 */

#pragma ident	"@(#)nss_deffinder.c	1.5	93/03/17 SMI"

#ifdef __STDC__
	/* Allow our finder(s) to be overridden by user-supplied ones */

	#pragma weak nss_default_finders = _nss_default_finders
#endif

#include "synonyms.h"
#include "shlib.h"
#include <nss_common.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <mtlib.h>

/* === ? move these constants to a public header file ? */
static const int  dlopen_version  = 1;
static const char dlopen_format[] = "nss_%s.so.%d";
static const char dlsym_format [] = "_nss_%s_%s_constr";
static const int  format_maxlen   = sizeof (dlsym_format) - 4;

#ifndef	DO_DL_LOCKING
/*
 * If I understood Rod Evans correctly, locking around dlopen() and dlsym()
 *   might give us a warm feeling but wouldn't really do anything for MT-safety
 */
#define	DO_DL_LOCKING	0
#endif	DO_DL_LOCKING

#if	DO_DL_LOCKING
static mutex_t serialize_dlopen	  = DEFAULTMUTEX;
#endif

static nss_backend_constr_t
SO_per_src_lookup(dummy, db_name, src_name, delete_privp)
	void		*dummy;
	const char	*db_name;
	const char	*src_name;
	void		**delete_privp;
{
	char			buf[64];
	char			*name;
	void			*dlhandle;
	void			*sym;
	int			len;
	nss_backend_constr_t	res = 0;

	len = format_maxlen + strlen(db_name) + strlen(src_name);
	if (len <= sizeof (buf)) {
		name = buf;
	} else if ((name = malloc(len)) == 0) {
		return (0);
	}
	sprintf(name, dlopen_format, src_name, dlopen_version);
#if	DO_DL_LOCKING
	_mutex_lock(&serialize_dlopen);
#endif
	if ((dlhandle = dlopen(name, RTLD_LAZY)) != 0) {
		sprintf(name, dlsym_format, src_name, db_name);
		if ((sym = dlsym(dlhandle, name)) == 0) {
			dlclose(dlhandle);
		} else {
			*delete_privp = dlhandle;
			res = (nss_backend_constr_t)sym;
		}
	}
#if	DO_DL_LOCKING
	_mutex_unlock(&serialize_dlopen);
#endif
	if (name != buf) {
		free(name);
	}
	return (res);
}

static void
SO_per_src_delete(delete_priv, dummy)
	void			*delete_priv;
	nss_backend_constr_t	dummy;
{
#if	DO_DL_LOCKING
	_mutex_lock(&serialize_dlopen);
#endif
	dlclose(delete_priv);
#if	DO_DL_LOCKING
	_mutex_unlock(&serialize_dlopen);
#endif
}

static nss_backend_finder_t SO_per_src = {
	SO_per_src_lookup,
	SO_per_src_delete,
	0,
	0
};

nss_backend_finder_t *_nss_default_finders = &SO_per_src;
