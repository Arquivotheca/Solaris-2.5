/*
 * Copyright (c) 1992 - 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Shared code used by the name-service-switch frontends (e.g. getpwnam_r())
 */

#pragma ident	"@(#)nss_common.c	1.14	95/08/05 SMI"

#ifdef __STDC__
	#pragma weak nss_delete = _nss_delete
	#pragma weak nss_endent = _nss_endent
	#pragma weak nss_getent = _nss_getent
	#pragma weak nss_search = _nss_search
	#pragma weak nss_setent = _nss_setent
#endif

#include "synonyms.h"
#define	cond_init	_cond_init
#define	cond_signal	_cond_signal
#define	cond_wait	_cond_wait

#include "shlib.h"
#include <dlfcn.h>
#include <nsswitch.h>	/* === For bits of old-style nsswitch interface */
#include <nss_common.h>
#include <stdlib.h>
#include <string.h>
#include <thread.h>
#include <mtlib.h>

/* Private interface to nsparse.c */
extern struct __nsw_switchconfig *_nsw_getoneconfig(const char	*name,
						    char	*linep,
						    enum __nsw_parse_err *);

/*
 * The golden rule is:  if you hold a pointer to an nss_db_state struct and
 * you don't hold the lock, you'd better have incremented the refcount
 * while you held the lock;  otherwise, it may vanish or change
 * significantly when you least expect it.
 *
 * The pointer in nss_db_root_t is one such, so the reference count >= 1.
 * Ditto the pointer in struct nss_getent_context.
 */

/*
 * State for one nsswitch database (e.g. "passwd", "hosts")
 */
struct nss_db_state {
	nss_db_root_t		orphan_root;	/* XXX explain */
	unsigned		refcount;	/* One for the pointer in    */
						/*   nss_db_root_t, plus one */
						/*   for each active thread. */
	nss_db_params_t		p;
	struct __nsw_switchconfig *config;
	int			max_src;	/* is == config->num_lookups */
	struct nss_src_state	*src;		/* Pointer to array[max_src] */
};

/*
 * State for one of the sources (e.g. "nis", "compat") for a database
 */
struct nss_src_state {
	struct __nsw_lookup	*lkp;
	int			n_active;
	int			n_dormant;
	int			n_waiting;	/* ... on wanna_be */
	cond_t			wanna_be;
	union {
		nss_backend_t	*single; /* Efficiency hack for common case */
					    /* when limit_dead_backends == 1 */
		nss_backend_t	**multi; /* array[limit_dead_backends] of */
	} dormant;			    /* pointers to dormant backends */
	nss_backend_constr_t	be_constr;
	nss_backend_finder_t	*finder;
	void			*finder_priv;
};

#if defined(__STDC__)
static struct nss_db_state *	_nss_db_state_constr(nss_db_initf_t);
static void			_nss_db_state_destr(struct nss_db_state *);
#else
static struct nss_db_state *	_nss_db_state_constr();
static void			_nss_db_state_destr();
#endif


/* ==== null definitions if !MTSAFE?  Ditto lock field in nss_db_root_t */

#define	NSS_ROOTLOCK(r, sp)	(mutex_lock(&(r)->lock), *(sp) = (r)->s)

#define	NSS_UNLOCK(r)		(mutex_unlock(&(r)->lock))

#define	NSS_CHECKROOT(rp, s)	((s) != (*(rp))->s &&			\
				(mutex_unlock(&(*(rp))->lock),		\
				mutex_lock(&(s)->orphan_root.lock),	\
				*(rp) = &(s)->orphan_root))

#define	NSS_RELOCK(rp, s)	(mutex_lock(&(*(rp))->lock),		\
				NSS_CHECKROOT(rp, s))

#define	NSS_STATE_REF_u(s)	(++(s)->refcount)

#define	NSS_UNREF_UNLOCK(r, s)	(--(s)->refcount != 0			\
				? ((void)NSS_UNLOCK(r))			\
				: (NSS_UNLOCK(r), _nss_db_state_destr(s)))

#define	NSS_LOCK_CHECK(r, f, sp)    (NSS_ROOTLOCK((r), (sp)),	\
				    *(sp) == 0 &&		\
				    (r->s = *(sp) = _nss_db_state_constr(f)))
/* === In the future, NSS_LOCK_CHECK() may also have to check that   */
/* === the config info hasn't changed (by comparing version numbers) */


static nss_backend_t *
nss_get_backend_u(rootpp, s, n_src)
	nss_db_root_t		**rootpp;
	struct nss_db_state	*s;
	int			n_src;
{
	struct nss_src_state	*src = &s->src[n_src];
	nss_backend_t		*be;

	while (1) {
		if (src->n_dormant > 0) {
			src->n_dormant--;
			src->n_active++;
			if (s->p.max_dormant_per_src == 1) {
				be = src->dormant.single;
			} else {
				be = src->dormant.multi[src->n_dormant];
			}
			break;
		}

		if (src->be_constr == 0) {
			nss_backend_finder_t	*bf;

			for (bf = s->p.finders;  bf != 0;  bf = bf->next) {
				nss_backend_constr_t c;

				c = (*bf->lookup)
					(bf->lookup_priv,
						s->p.name,
						src->lkp->service_name,
						&src->finder_priv);
				if (c != 0) {
					src->be_constr = c;
					src->finder = bf;
					break;
				}
			}
			if (src->be_constr == 0) {
				/* Couldn't find the backend anywhere */
				be = 0;
				break;
			}
		}

		if (src->n_active < s->p.max_active_per_src) {
			be = (*src->be_constr)(s->p.name,
						src->lkp->service_name,
						0 /* === unimplemented */);
			if (be != 0) {
				src->n_active++;
				break;
			} else if (src->n_active == 0) {
				/* Something's wrong;  we should be */
				/*   able to create at least one    */
				/*   instance of the backend	    */
				break;
			}
			/*
			 * Else it's odd that we can't create another backend
			 *   instance, but don't sweat it;  instead, queue for
			 *   an existing backend instance.
			 */
		}

		src->n_waiting++;
		cond_wait(&src->wanna_be, &(*rootpp)->lock);
		NSS_CHECKROOT(rootpp, s);
		src->n_waiting--;

		/*
		 * Loop and see whether things got better for us, or whether
		 *   someone else got scheduled first and we have to try
		 *   this again.
		 *
		 * === ?? Should count iterations, assume bug if many ??
		 */
	}
	return (be);
}

static void
nss_put_backend_u(s, n_src, be)
	struct nss_db_state	*s;
	int			n_src;
	nss_backend_t		*be;
{
	struct nss_src_state	*src = &s->src[n_src];

	if (be == 0) {
		return;
	}

	src->n_active--;

	if (src->n_dormant < s->p.max_dormant_per_src) {
		if (s->p.max_dormant_per_src == 1) {
			src->dormant.single = be;
			src->n_dormant++;
		} else if (src->dormant.multi != 0 ||
			(src->dormant.multi = (nss_backend_t **)
			calloc(s->p.max_dormant_per_src,
				sizeof (nss_backend_t *))) != 0) {
			src->dormant.multi[src->n_dormant] = be;
			src->n_dormant++;
		} else {
			/* Can't store it, so toss it */
			NSS_INVOKE_DBOP(be, NSS_DBOP_DESTRUCTOR, 0);
		}
	} else {
		/* We've stored as many as we want, so toss it */
		NSS_INVOKE_DBOP(be, NSS_DBOP_DESTRUCTOR, 0);
	}
	if (src->n_waiting > 0) {
		cond_signal(&src->wanna_be);
	}
}

static struct nss_db_state *
_nss_db_state_constr(initf)
	nss_db_initf_t		initf;
{
	struct nss_db_state	*s;
	struct __nsw_switchconfig *config = 0;
	struct __nsw_lookup	*lkp;
	enum __nsw_parse_err	err;
	const char		*config_name;
	int			n_src;

	if ((s = (struct nss_db_state *) calloc(1, sizeof (*s))) == 0) {
		return (0);
	}
	mutex_init(&s->orphan_root.lock, USYNC_THREAD, 0);

	s->p.max_active_per_src	= 10;
	s->p.max_dormant_per_src = 1;
	s->p.finders = nss_default_finders;
	(*initf)(&s->p);
	if (s->p.name == 0) {
		_nss_db_state_destr(s);
		return (0);
	}
	config_name = s->p.config_name ? s->p.config_name : s->p.name;
	if (! (s->p.flags & NSS_USE_DEFAULT_CONFIG)) {
		config = __nsw_getconfig(config_name, &err);
		/* === ? test err ? */
	}
	if (config == 0) {
		/* getconfig failed, or frontend demanded default config */

		char	*str;	/* _nsw_getoneconfig() clobbers its argument */

		if ((str = strdup(s->p.default_config)) != 0) {
			config = _nsw_getoneconfig(config_name, str, &err);
			free(str);
		}
		if (config == 0) {
			_nss_db_state_destr(s);
			return (0);
		}
	}
	s->config = config;
	if ((s->max_src = config->num_lookups) <= 0 ||
	    (s->src = (struct nss_src_state *)
	    calloc(s->max_src, sizeof (*s->src))) == 0) {
		_nss_db_state_destr(s);
		return (0);
	}
	for (n_src = 0, lkp = config->lookups;
	    n_src < s->max_src; n_src++, lkp = lkp->next) {
		s->src[n_src].lkp = lkp;
		cond_init(&s->src[n_src].wanna_be, USYNC_THREAD, 0);
	}
	s->refcount = 1;
	return (s);
}

void
_nss_src_state_destr(src, max_dormant)
	struct nss_src_state	*src;
{
	if (max_dormant == 1) {
		if (src->n_dormant != 0) {
			NSS_INVOKE_DBOP(src->dormant.single,
					NSS_DBOP_DESTRUCTOR, 0);
		};
	} else if (src->dormant.multi != 0) {
		int	n;

		for (n = 0;  n < src->n_dormant;  n++) {
			NSS_INVOKE_DBOP(src->dormant.multi[n],
					NSS_DBOP_DESTRUCTOR, 0);
		}
		free(src->dormant.multi);
	}

	/* cond_destroy(&src->wanna_be); */

	if (src->finder != 0) {
		(*src->finder->delete)(src->finder_priv, src->be_constr);
	}
}

/*
 * _nss_db_state_destr() -- used by NSS_UNREF_UNLOCK() to free the entire
 *	nss_db_state structure.
 * Assumes that s has been ref-counted down to zero (in particular,
 *	rootp->s has already been dealt with).
 *
 * Nobody else holds a pointer to *s (if they did, refcount != 0),
 *   so we can clean up state *after* we drop the lock (also, by the
 *   time we finish freeing the state structures, the lock may have
 *   ceased to exist -- if we were using the orphan_root).
 */

static void
_nss_db_state_destr(s)
	struct nss_db_state	*s;
{
	/* === mutex_destroy(&s->orphan_root.lock); */
	if (s->p.cleanup != 0) {
		(*s->p.cleanup)(&s->p);
	}
	if (s->config != 0) {
		__nsw_freeconfig(s->config);
	}
	if (s->src != 0) {
		int	n_src;

		for (n_src = 0;  n_src < s->max_src;  n_src++) {
			_nss_src_state_destr(&s->src[n_src],
				s->p.max_dormant_per_src);
		}
		free(s->src);
	}
	free(s);
}

void
nss_delete(rootp)
	nss_db_root_t		*rootp;
{
	struct nss_db_state	*s;

	NSS_ROOTLOCK(rootp, &s);
	if (s == 0) {
		NSS_UNLOCK(rootp);
	} else {
		rootp->s = 0;
		NSS_UNREF_UNLOCK(rootp, s);
	}
}


/*
 * _nss_status_vec() returns a bit vector of all status codes returned during
 * the most recent call to nss_search().
 * _nss_status_vec_p() returns a pointer to this bit vector, or NULL on
 * failure.
 * These functions are private.  Don't use them externally without discussing
 * it with the switch maintainers.
 */
static unsigned int *
_nss_status_vec_p(void)
{
	static mutex_t		keylock = DEFAULTMUTEX;
	static thread_key_t	status_vec_key;
	static int		key_created = 0;
	static unsigned int	status_vec = 0;	/* for non-MT applications */
	void			*status_vec_p;

	if (_thr_main()) {
		return (&status_vec);
	}
	if (!key_created) {
		mutex_lock(&keylock);
		key_created =
		    key_created ||
		    _thr_keycreate(&status_vec_key, free) == 0;
		mutex_unlock(&keylock);
	}
	if (!key_created) {
		return (NULL);
	}
	_thr_getspecific(status_vec_key, &status_vec_p);
	if (status_vec_p == NULL) {
		status_vec_p = calloc(1, sizeof (unsigned int));
		if (status_vec_p == NULL ||
		    _thr_setspecific(status_vec_key, status_vec_p) != 0) {
			free(status_vec_p);
			return (NULL);
		}
	}
	return (status_vec_p);
}

unsigned int
_nss_status_vec(void)
{
	unsigned int		*status_vec_p = _nss_status_vec_p();

	return ((status_vec_p != NULL) ? *status_vec_p : (1 << NSS_UNAVAIL));
}


nss_status_t
nss_search(rootp, initf, search_fnum, search_args)
	nss_db_root_t		*rootp;
	nss_db_initf_t		initf;
	int			search_fnum;
	void			*search_args;
{
	nss_status_t		res = NSS_UNAVAIL;
	struct nss_db_state	*s;
	int			n_src;
	unsigned int		*status_vec_p = _nss_status_vec_p();

	if (status_vec_p == NULL) {
		return (NSS_UNAVAIL);
	}
	*status_vec_p = 0;

	NSS_LOCK_CHECK(rootp, initf, &s);
	if (s == 0) {
		NSS_UNLOCK(rootp);
		return (res);
	}
	NSS_STATE_REF_u(s);

	for (n_src = 0;  n_src < s->max_src;  n_src++) {
		nss_backend_t		*be;
		nss_backend_op_t	funcp;

		if ((be = nss_get_backend_u(&rootp, s, n_src)) != 0) {
			if ((funcp = NSS_LOOKUP_DBOP(be, search_fnum)) != 0) {
			/* Backend operation may take a while;  drop the    */
			/*   lock so we don't serialize more than necessary */
				NSS_UNLOCK(rootp);
				res = (*funcp)(be, search_args);
				*status_vec_p |= (1 << res);
				NSS_RELOCK(&rootp, s);
			}
			nss_put_backend_u(s, n_src, be);
		}
		if (__NSW_ACTION(s->src[n_src].lkp, res) == __NSW_RETURN) {
			break;
		}
	}
	NSS_UNREF_UNLOCK(rootp, s);
	return (res);
}


/*
 * Start of nss_setent()/nss_getent()/nss_endent()
 */

/*
 * State (here called "context") for one setent/getent.../endent sequence.
 *   In principle there could be multiple contexts active for a single
 *   database;  in practice, since Posix and UI have helpfully said that
 *   getent() state is global rather than, say, per-thread or user-supplied,
 *   we have at most one of these per nss_db_state.
 */

struct nss_getent_context {
	int			n_src;	/* >= max_src ==> end of sequence */
	nss_backend_t		*be;
	struct nss_db_state	*s;
	/*
	 * XXX ??  Should contain enough cross-check info that we can detect an
	 * nss_context that missed an nss_delete() or similar.
	 */
};

static void		nss_setent_u(nss_db_root_t *,
				    nss_db_initf_t,
				    nss_getent_t *);
static nss_status_t	nss_getent_u(nss_db_root_t *,
				    nss_db_initf_t,
				    nss_getent_t *,
				    void *);
static void		nss_endent_u(nss_db_root_t *,
				    nss_db_initf_t,
				    nss_getent_t *);

void
nss_setent(rootp, initf, contextpp)
	nss_db_root_t		*rootp;
	nss_db_initf_t		initf;
	nss_getent_t		*contextpp;
{
	if (contextpp == 0) {
		return;
	}
	mutex_lock(&contextpp->lock);
	nss_setent_u(rootp, initf, contextpp);
	mutex_unlock(&contextpp->lock);
}

nss_status_t
nss_getent(rootp, initf, contextpp, args)
	nss_db_root_t		*rootp;
	nss_db_initf_t		initf;
	nss_getent_t		*contextpp;
	void			*args;
{
	nss_status_t		status;

	if (contextpp == 0) {
		return;
	}
	mutex_lock(&contextpp->lock);
	status = nss_getent_u(rootp, initf, contextpp, args);
	mutex_unlock(&contextpp->lock);
	return (status);
}

void
nss_endent(rootp, initf, contextpp)
	nss_db_root_t		*rootp;
	nss_db_initf_t		initf;
	nss_getent_t		*contextpp;
{
	if (contextpp == 0) {
		return;
	}
	mutex_lock(&contextpp->lock);
	nss_endent_u(rootp, initf, contextpp);
	mutex_unlock(&contextpp->lock);
}

/*
 * Each of the _u versions of the nss interfaces assume that the context
 * lock is held.
 */

static void
end_iter_u(rootp, contextp)
	nss_db_root_t		*rootp;
	struct nss_getent_context *contextp;
{
	struct nss_db_state	*s;
	nss_backend_t		*be;
	int			n_src;

	s = contextp->s;
	n_src = contextp->n_src;
	be = contextp->be;

	if (s != 0) {
		if (n_src < s->max_src && be != 0) {
			(void) NSS_INVOKE_DBOP(be, NSS_DBOP_ENDENT, 0);
			NSS_RELOCK(&rootp, s);
			nss_put_backend_u(s, n_src, be);
			contextp->be = 0;  /* Should be unnecessary, but hey */
			NSS_UNREF_UNLOCK(rootp, s);
		}
		contextp->s = 0;
	}
}

static void
nss_setent_u(rootp, initf, contextpp)
	nss_db_root_t		*rootp;
	nss_db_initf_t		initf;
	nss_getent_t		*contextpp;
{
	struct nss_db_state	*s;
	struct nss_getent_context *contextp;
	nss_backend_t		*be;
	int			n_src;

	if ((contextp = contextpp->ctx) == 0) {
		if ((contextp = (struct nss_getent_context *)
		    calloc(1, sizeof (*contextp))) == 0) {
			return;
		}
		contextpp->ctx = contextp;
		s = 0;
	} else {
		s = contextp->s;
	}

	if (s == 0) {
		NSS_LOCK_CHECK(rootp, initf, &s);
		if (s == 0) {
			/* Couldn't set up state, so quit */
			NSS_UNLOCK(rootp);
			/* ==== is there any danger of not having done an */
			/* end_iter() here, and hence of losing backends? */
			contextpp->ctx = 0;
			free(contextp);
			return;
		}
		NSS_STATE_REF_u(s);
		contextp->s = s;
	} else {
		s	= contextp->s;
		n_src	= contextp->n_src;
		be	= contextp->be;
		if (n_src == 0 && be != 0) {
			/*
			 * Optimization:  don't do endent, don't change
			 *   backends, just do the setent.  Look Ma, no locks
			 *   (nor any context that needs updating).
			 */
			(void) NSS_INVOKE_DBOP(be, NSS_DBOP_SETENT, 0);
			return;
		}
		if (n_src < s->max_src && be != 0) {
			(void) NSS_INVOKE_DBOP(be, NSS_DBOP_ENDENT, 0);
			NSS_RELOCK(&rootp, s);
			nss_put_backend_u(s, n_src, be);
			contextp->be = 0;	/* Play it safe */
		} else {
			NSS_RELOCK(&rootp, s);
		}
	}
	for (n_src = 0, be = 0; n_src < s->max_src &&
		(be = nss_get_backend_u(&rootp, s, n_src)) == 0; n_src++) {
		;
	}
	NSS_UNLOCK(rootp);

	contextp->n_src	= n_src;
	contextp->be	= be;

	if (be == 0) {
		/* Things are broken enough that we can't do setent/getent */
		nss_endent_u(rootp, initf, contextpp);
		return;
	}
	(void) NSS_INVOKE_DBOP(be, NSS_DBOP_SETENT, 0);
}

static nss_status_t
nss_getent_u(rootp, initf, contextpp, args)
	nss_db_root_t		*rootp;
	nss_db_initf_t		initf;
	nss_getent_t		*contextpp;
	void			*args;
{
	struct nss_db_state	*s;
	struct nss_getent_context *contextp;
	int			n_src;
	nss_backend_t		*be;

	if ((contextp = contextpp->ctx) == 0) {
		nss_setent_u(rootp, initf, contextpp);
		if ((contextp = contextpp->ctx) == 0) {
			/* Give up */
			return (NSS_UNAVAIL);
		}
	}
	s	= contextp->s;
	n_src	= contextp->n_src;
	be	= contextp->be;

	if (s == 0) {
		/*
		 * We've done an end_iter() and haven't done nss_setent()
		 * or nss_endent() since;  we should stick in this state
		 * until the caller invokes one of those two routines.
		 */
		return (NSS_SUCCESS);
	}

	while (n_src < s->max_src) {
		nss_status_t res;

		if (be == 0) {
			/* If it's null it's a bug, but let's play safe */
			res = NSS_UNAVAIL;
		} else {
			res = NSS_INVOKE_DBOP(be, NSS_DBOP_GETENT, args);
		}

		if (__NSW_ACTION(s->src[n_src].lkp, res) == __NSW_RETURN) {
			if (res != __NSW_SUCCESS) {
				end_iter_u(rootp, contextp);
			}
			return (res);
		}
		(void) NSS_INVOKE_DBOP(be, NSS_DBOP_ENDENT, 0);
		NSS_RELOCK(&rootp, s);
		nss_put_backend_u(s, n_src, be);
		do {
			n_src++;
		} while (n_src < s->max_src &&
			(be = nss_get_backend_u(&rootp, s, n_src)) == 0);
		if (be == 0) {
			/*
			 * This is the case where we failed to get the backend
			 * for the last source. We exhausted all sources.
			 */
			break;
		}
		NSS_UNLOCK(rootp);
		contextp->n_src	= n_src;
		contextp->be	= be;
		(void) NSS_INVOKE_DBOP(be, NSS_DBOP_SETENT, 0);
	}
	/* Got to the end of the sources without finding another entry */
	end_iter_u(rootp, contextp);
	return (NSS_SUCCESS);
	/* success is either a successful entry or end of the sources */
}

static void
nss_endent_u(rootp, initf, contextpp)
	nss_db_root_t		*rootp;
	nss_db_initf_t		initf;
	nss_getent_t		*contextpp;
{
	struct nss_db_state	*s;
	struct nss_getent_context *contextp;

	if ((contextp = contextpp->ctx) == 0) {
		/* nss_endent() on an unused context is a no-op */
		return;
	}
	/*
	 * Existing code (BSD, SunOS) works in such a way that getXXXent()
	 *   following an endXXXent() behaves as though the user had invoked
	 *   setXXXent(), i.e. it iterates properly from the beginning.
	 * We'd better not break this, so our choices are
	 *	(1) leave the context structure around, and do nss_setent or
	 *	    something equivalent,
	 *   or	(2) free the context completely, and rely on the code in
	 *	    nss_getent() that makes getXXXent() do the right thing
	 *	    even without a preceding setXXXent().
	 * The code below does (2), which frees up resources nicely but will
	 * cost more if the user then does more getXXXent() operations.
	 * Moral:  for efficiency, don't call endXXXent() prematurely.
	 */
	end_iter_u(rootp, contextp);
	free(contextp);
	contextpp->ctx = 0;
}
