/*
 *	nis_subr.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_subr.c	1.42	95/01/25 SMI"

/*
 *	nis_subr.c
 *
 * This module contains the subroutines used by the server to manipulate
 * objects and names.
 */
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <rpc/rpc.h>	/* Must be ahead of rpcb_clnt.h */
#include <rpc/svc.h>
#ifdef TDRPC
#include <sys/socket.h>
#else
#include <tiuser.h>
#include <netconfig.h>
#include <netdir.h>
#include <rpc/rpcb_clnt.h>
#endif
#include <rpcsvc/nis.h>
#include "nis_clnt.h"
#ifndef TDRPC
#include <sys/systeminfo.h>
#endif
#include "nis_local.h"

#define	MAXIPRINT	(11)	/* max length of printed integer */
static char    *PKTABLE	  = "cred.org_dir";
#define	PKTABLE_LEN	12
/*
 * send and receive buffer size used for clnt_tli_create if not specified.
 * This is only used for "UDP" connection.
 * This limit can be changed from the application if this value is too
 * small for the application.  To use the maximum value for the transport,
 * set this value to 0.
 */
int __nisipbufsize = 8192;

/*
 * Static function prototypes.
 */
static struct local_names *__get_local_names(void);

/* Exported function */
nis_name __nis_nextsep_of(char *);

#ifndef TDRPC
char * __map_addr(struct netconfig *, char *, u_long, u_long);
#endif


#define	COMMA	','	/* Avoid cstyle bug */

/* __nis_data_directory is READ ONLY, so no locking is needed    */
/* Note: We make it static, so external caller can not access it */
/*	i.e we make sure it stay read only			*/
static char	__nis_data_directory[1024] = {"/var/nis/"};

/* These macros make the code easier to read */

#ifdef NOTIME
#define	__start_clock(n)
#define	__stop_clock(n)		n
#else
static struct timeval clocks[MAXCLOCKS];

extern char *t_errlist[];

/*
 * __start_clock()
 *
 * This function will start the "stopwatch" on the function calls.
 * It uses an array of time vals to keep track of the time. The
 * sister call __stop_clock() will return the number of microseconds
 * since the clock was started. This allows for keeping statistics
 * on the NIS calls and tuning the service. If the clock in question
 * is not "stopped" this function returns an error.
 */
int
__start_clock(clk)
	int	clk;	/* The clock we want to start */
{
	if ((clk >= MAXCLOCKS) || (clk < 0) || (clocks[clk].tv_sec))
		return (FALSE);

	gettimeofday(&clocks[clk], NULL);
	return (TRUE);
}

unsigned long
__stop_clock(clk)
	int	clk;
{
	struct timeval 		now;
	unsigned long		secs, micros;

	if ((clk >= MAXCLOCKS) || (clk < 0) || (!clocks[clk].tv_sec))
		return (0);
	gettimeofday(&now, NULL);
	secs = now.tv_sec - clocks[clk].tv_sec;
	if (now.tv_usec < clocks[clk].tv_usec) {
		micros = (now.tv_usec + 1000000) - clocks[clk].tv_usec;
		secs--; /* adjusted 'cuz we added a second above */
	} else {
		micros = now.tv_usec - clocks[clk].tv_usec;
	}
	micros = micros + (secs * 1000000); /* All micros now */
	clocks[clk].tv_sec = 0; /* Stop the clock. */
	return (micros);
}
#endif /* no time */

/*
 * nis_dir_cmp() -- the results can be read as:
 * 	"Name 'n1' is a $result than name 'n2'"
 */
name_pos
nis_dir_cmp(n1, n2)
	nis_name	n1, n2;	/* See if these are the same domain */
{
	size_t		l1, l2;
	name_pos	result;

	if ((n1 == NULL) || (n2 == NULL))
		return (BAD_NAME);

	l1 = strlen(n1);
	l2 = strlen(n2);

	/* In this routine we're lenient and don't require a trailing '.' */
	/*   so we need to ignore it if it does appear.			  */
	/* ==== That's what the previous version did so this one does	  */
	/*   too, but why?  Is this inconsistent with rest of system?	  */
	if (l1 != 0 && n1[l1 - 1] == '.') {
		--l1;
	}
	if (l2 != 0 && n2[l2 - 1] == '.') {
		--l2;
	}

	if (l1 > l2) {
		result = LOWER_NAME;
	} else if (l1 == l2) {
		result = SAME_NAME;
	} else /* (l1 < l2); swap l1/l2 and n1/n2 */ {
		nis_name	ntmp;
		size_t		ltmp;
		ntmp = n1; n1 = n2; n2 = ntmp;
		ltmp = l1; l1 = l2; l2 = ltmp;

		result = HIGHER_NAME;
	}

	/* Now l1 >= l2 in all cases */
	if (l2 == 0) {
		/* Special case for n2 == "." or "" */
		return (result);
	}
	if (l1 > l2) {
		n1 += l1 - l2;
		if (n1[-1] != '.') {
			return (NOT_SEQUENTIAL);
		}
	}
	if (strncasecmp(n1, n2, l2) == 0) {
		return (result);
	}
	return (NOT_SEQUENTIAL);
}

#define	LN_BUFSIZE	1024

struct principal_list {
	uid_t uid;
	char principal[LN_BUFSIZE];
	struct principal_list *next;
};


struct local_names {
	char domain[LN_BUFSIZE];
	char host[LN_BUFSIZE];
	struct principal_list *principal_map;
	char group[LN_BUFSIZE];
};

static mutex_t ln_lock = DEFAULTMUTEX; /* lock level 2 */
static struct local_names *ln = NULL;

static struct local_names *
__get_local_names()
{
	struct local_names * __get_local_names1();
	struct local_names *names;
	sigset_t	   oset;

	thr_sigblock(&oset);
	mutex_lock(&ln_lock);
	names = __get_local_names1();
	mutex_unlock(&ln_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	return (names);
}


static struct local_names *
__get_local_names1()
{
	char		*t;

	if (ln != NULL) {
		/* Second and subsequent calls go this way */
		return (ln);
	}
	/* First call goes this way */
	ln = (struct local_names *)calloc(1, sizeof (*ln));
	if (ln == NULL) {
		syslog(LOG_ERR, "__get_local_names: Out of heap.");
		return (NULL);
	}
	ln->principal_map = NULL;

#ifndef TDRPC
	if (sysinfo(SI_SRPC_DOMAIN, ln->domain, LN_BUFSIZE) < 0)
		return (ln);
#else
	if (getdomainname(ln->domain, LN_BUFSIZE) < 0)
		return (ln);
#endif TDRPC
	/* If no dot exists, add one. */
	if (ln->domain[strlen(ln->domain)-1] != '.')
		strcat(ln->domain, ".");
#ifndef TDRPC
	if (sysinfo(SI_HOSTNAME, ln->host, LN_BUFSIZE) < 0)
		return (ln);
#else
	if (gethostname(ln->host, LN_BUFSIZE) < 0)
		return (ln);
#endif TDRPC

	/*
	 * Check for fully qualified hostname.  If it's a fully qualified
	 * hostname, strip off the domain part.  We always use the local
	 * domainname for the host principal name.
	 */
	t = strchr(ln->host, '.');
	if (t)
		*t = 0;
	if (ln->domain[0] != '.')
		strcat(ln->host, ".");
	strcat(ln->host, ln->domain);

	t = getenv("NIS_GROUP");
	if (t == NULL) {
		ln->group[0] = NUL;
	} else {
		strcpy(ln->group, t);
		t = strchr(ln->group, '.');
		if (!t) {
			if (ln->group[0] != '.')
				strcat(ln->group, ".");
			strcat(ln->group, ln->domain);
		}
	}

	return (ln);
}
/*
 * nis_local_group()
 *
 * Return's the group name of the current user.
 */
nis_name
nis_local_group()
{
	struct local_names	*ln = __get_local_names();

	/* LOCK NOTE: Warning, after initialization, "ln" is expected	 */
	/* to stay constant, So no need to lock here. If this assumption */
	/* is changed, this code must be protected.			 */
	if (!ln)
		return (NULL);
	return (ln->group);
}

/*
 * __nis_nextsep_of()
 *
 * This internal funtion will accept a pointer to a NIS name string and
 * return a pointer to the next separator occurring in it (it will point
 * just past the first label).  It allows for labels to be "quoted" to
 * prevent the the dot character within them to be interpreted as a
 * separator, also the quote character itself can be quoted by using
 * it twice.  If the the name contains only one label and no trailing
 * dot character, a pointer to the terminating NULL is returned.
 */
nis_name
__nis_nextsep_of(s)
	char	*s;
{
	char	*d;
	int	in_quotes = FALSE,
		quote_quote = FALSE;

	if (!s)
		return (NULL);

	for (d = s; (in_quotes && (*d != '\0')) ||
			(!in_quotes && (*d != '.') && (*d != '\0'));
			d++)
		if (quote_quote && in_quotes && (*d != '"')) {
			quote_quote = FALSE;
			in_quotes = FALSE;
			if (*d == '.')
				break;
		} else if (quote_quote && in_quotes && (*d == '"')) {
			quote_quote = FALSE;
		} else if (quote_quote && (*d != '"')) {
			quote_quote = FALSE;
			in_quotes = TRUE;
		} else if (quote_quote && (*d == '"')) {
			quote_quote = FALSE;
		} else if (in_quotes && (*d == '"')) {
			quote_quote = TRUE;
		} else if (!in_quotes && (*d == '"')) {
			quote_quote = TRUE;
		}
	return (d);
}

/*
 * nis_domain_of()
 *
 * This internal funtion will accept a pointer to a NIS name string and
 * return a pointer to the "domain" part of it.
 *
 * ==== We don't need nis_domain_of_r(), but should we provide one for
 *	uniformity?
 */
nis_name
nis_domain_of(s)
	char    *s;
{
	char	*d;

	d = __nis_nextsep_of(s);
	if (d == NULL)
		return (NULL);
	if (*d == '.')
		d++;
	if (*d == '\0')	/* Don't return a zero length string */
		return ("."); /* return root domain instead */
	return (d);
}


/*
 * nis_leaf_of()
 *
 * Returns the first label of a name. (other half of __domain_of)
 */
nis_name
nis_leaf_of_r(s, buf, bufsize)
	const nis_name	s;
	char		*buf;
	size_t		bufsize;
{
	size_t		nchars;
	const char	*d = __nis_nextsep_of((char *)s);

	if (d == 0) {
		return (0);
	}
	nchars = d - s;
	if (bufsize < nchars + 1) {
		return (0);
	}
	strncpy(buf, s, nchars);
	buf[nchars] = '\0';
	return (buf);
}

static thread_key_t buf_key;
static char buf_main[LN_BUFSIZE];

nis_name
nis_leaf_of(s)
	char	*s;
{
	char *buf;

	if (_thr_main())
		buf = buf_main;
	else {
		buf = thr_get_storage(&buf_key, LN_BUFSIZE, free);
		if (!buf)
			return (NULL);
	}
	return (nis_leaf_of_r(s, buf,  LN_BUFSIZE));
}

/*
 * nis_name_of()
 * This internal function will remove from the NIS name, the domain
 * name of the current server, this will leave the unique part in
 * the name this becomes the "internal" version of the name. If this
 * function returns NULL then the name we were given to resolve is
 * bad somehow.
 * NB: Uses static storage and this is a no-no with threads. XXX
 */

nis_name
nis_name_of_r(s, buf, bufsize)
	char	*s;	/* string with the name in it. */
	char		*buf;
	size_t		bufsize;
{
	char			*d;
	struct local_names 	*ln = __get_local_names();
	int			dl, sl;
	name_pos		p;

	if ((!s) || (!ln))
		return (NULL);		/* No string, this can't continue */

	d  = &(ln->domain[0]);
	dl = strlen(ln->domain); 	/* _always dot terminated_   */

	strcpy(buf, s);		/* Make a private copy of 's'   */
	sl = strlen(buf);
	if (buf[sl-1] != '.') {	/* Add a dot if necessary.  */
		strcat(buf, ".");
		sl++;
	}

	if (dl == 1) {			/* We're the '.' directory   */
		buf[sl-1] = NUL;	/* Lose the 'dot'	  */
		return (buf);
	}

	p = nis_dir_cmp(buf, d);

	/* 's' is above 'd' in the tree */
	if ((p == HIGHER_NAME) || (p == NOT_SEQUENTIAL) || (p == SAME_NAME))
		return (NULL);

	/* Insert a NUL where the domain name starts in the string */
	buf[(sl - dl) - 1] = NUL;

	/* Don't return a zero length name */
	if (buf[0] == '\0')
		return (NULL);

	return (buf);
}

nis_name
nis_name_of(s)
	char	*s;	/* string with the name in it. */
{
	char *buf;

	if (_thr_main())
		buf = buf_main;
	else {
		buf = thr_get_storage(&buf_key, LN_BUFSIZE, free);
		if (!buf)
			return (NULL);
	}
	return (nis_name_of_r(s, buf,  LN_BUFSIZE));
}



/*
 * nis_local_directory()
 *
 * Return a pointer to a string with the local directory name in it.
 */
nis_name
nis_local_directory()
{
	struct local_names	*ln = __get_local_names();

	/* LOCK NOTE: Warning, after initialization, "ln" is expected	 */
	/* to stay constant, So no need to lock here. If this assumption */
	/* is changed, this code must be protected.			 */
	if (ln == NULL)
		return (NULL);
	return (ln->domain);
}

/*
 * nis_getprincipal:
 *   Return the prinicipal name of the given uid in string supplied.
 *   Returns status obtained from nis+.
 *
 * Look up the LOCAL mapping in the local cred table. Note that if the
 * server calls this, then the version of nis_list that will
 * will be bound here is the 'safe' one in the server code.
 *
 * The USE_DGRAM + NO_AUTHINFO is required to prevent a
 * recursion through the getnetname() interface which is
 * called by authseccreate_pk and authdes_pk_create().
 *
 * NOTE that if you really want to get the nis+ principal name,
 * you should not use this call.  You should do something similar
 * but use an authenticated handle.
 */


int
__nis_principal(principal_name, uid, directory)
	char *principal_name;
	uid_t uid;
	char *directory;
{
	nis_result	*res;
	char		buf[NIS_MAXNAMELEN];
	int		status;

	if ((strlen(directory)+MAXIPRINT+PKTABLE_LEN+32) >
		(size_t) NIS_MAXNAMELEN) {
		return (NIS_BADNAME);
	}

	sprintf(buf, "[auth_name=%d,auth_type=LOCAL],%s.%s",
		uid, PKTABLE, directory);

	if (buf[strlen(buf)-1] != '.')
		strcat(buf, ".");

	res = nis_list(buf,
			USE_DGRAM+NO_AUTHINFO+FOLLOW_LINKS+FOLLOW_PATH,
			NULL, NULL);
	status = res->status;
	if (status == NIS_SUCCESS || status == NIS_S_SUCCESS) {
		if (res->objects.objects_len > 1) {
			/*
			 * More than one principal with same uid?
			 * something wrong with cred table. Should be unique
			 * Warn user and continue.
			 */
			syslog(LOG_ERR,
		"nis_principal: LOCAL entry for %d in directory %s not unique",
				uid, directory);
		}
		strcpy(principal_name,
			ENTRY_VAL(res->objects.objects_val, 0));
	}
	nis_freeresult(res);

	return (status);
}

/*
 * nis_local_principal()
 * Generate the principal name for this user by looking it up its LOCAL
 * entry in the cred table of the local direectory.
 * Does not use an authenticated call (to prevent recursion because
 * this is called by user2netname).
 */
nis_name
nis_local_principal()
{
	struct local_names *ln = __get_local_names();
	uid_t		uid;
	int 		status;
	char		*dirname;
	static mutex_t local_principal_lock = DEFAULTMUTEX;
	struct principal_list *p, *q;
	sigset_t	oset;

	if (ln == NULL)
		return (NULL);

	thr_sigblock(&oset);
	mutex_lock(&local_principal_lock);
	uid = geteuid();
	p = ln->principal_map;
	while (p) {
		if (p->uid == uid) {
			ASSERT(*(p->principal) != 0);
			mutex_unlock(&local_principal_lock);
			thr_sigsetmask(SIG_SETMASK, &oset, NULL);
			return (p->principal);
		}
		q = p;
		p = p->next;
	}
	if (uid == 0) {
		mutex_unlock(&local_principal_lock);
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
		return (ln->host);
	}
	p = (struct principal_list *) calloc(1, sizeof (*q));
	if (!ln->principal_map) {
		ln->principal_map = p;
	} else  q = p;
	dirname = nis_local_directory();
	if ((dirname == NULL) || (dirname[0] == NULL)) {
		strcpy(p->principal, "nobody");
		p->uid = uid;
		mutex_unlock(&local_principal_lock);
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
		return (p->principal);
	}
	switch (status = __nis_principal(p->principal, uid, dirname)) {
	case NIS_SUCCESS:
	case NIS_S_SUCCESS:
		break;
	case NIS_NOTFOUND:
	case NIS_PARTIAL:
	case NIS_NOSUCHNAME:
	case NIS_NOSUCHTABLE:
		strcpy(p->principal, "nobody");
		break;
	default:
		/*
		 * XXX We should return 'nobody', but
		 * should we be remembering 'nobody' as our
		 * principal name here?  Some errors might be
		 * transient.
		 */
		syslog(LOG_ERR,
			"nis_local_principal: %s",
			nis_sperrno(status));
		strcpy(p->principal, "nobody");
	}
	p->uid = uid;
	mutex_unlock(&local_principal_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	return (p->principal);
}

/*
 * nis_local_host()
 * Generate the principal name for this host, "hostname"+"domainname"
 * unless the hostname already has "dots" in its name.
 */
nis_name
nis_local_host()
{
	struct local_names	*ln = __get_local_names();

	/* LOCK NOTE: Warning, after initialization, "ln" is expected	 */
	/* to stay constant, So no need to lock here. If this assumption */
	/* is changed, this code must be protected.			 */
	if (ln == NULL)
		return (NULL);

	return (ln->host);
}

/*
 * nis_destroy_object()
 * This function takes a pointer to a NIS object and deallocates it. This
 * is the inverse of __clone_object below. It must be able to correctly
 * deallocate partially allocated objects because __clone_object will call
 * it if it runs out of memory and has to abort. Everything is freed,
 * INCLUDING the pointer that is passed.
 */
void
nis_destroy_object(obj)
	nis_object	*obj;	/* The object to clone */
{
	xdr_free(xdr_nis_object, (char *) obj);
	free(obj);
} /* nis_destroy_object */

#define	CLONEBUFSIZ 	8192

static  thread_key_t clone_buf_key;
static	struct nis_sdata clone_buf_main;

static void destroy_nis_sdata(void * p)
{
	free(p);
}

static XDR in_xdrs, out_xdrs;


/*
 * __clone_object_r()
 * This function takes a pointer to a NIS object and clones it. This
 * duplicate object is now available for use in the local context.
 */
nis_object *
nis_clone_object_r(obj, dest, clone_buf_ptr)
	nis_object	*obj;	/* The object to clone */
	nis_object	*dest;	/* Use this pointer if non-null */
	struct nis_sdata *clone_buf_ptr;
{
	nis_object	*result; /* The clone itself */
	int		status; /* a counter variable */

	if (! nis_get_static_storage(clone_buf_ptr, 1,
					    xdr_sizeof(xdr_nis_object, obj)))
		return (NULL);

	xdrmem_create(&in_xdrs, clone_buf_ptr->buf, CLONEBUFSIZ, XDR_ENCODE);
	xdrmem_create(&out_xdrs, clone_buf_ptr->buf, CLONEBUFSIZ, XDR_DECODE);

	/* Allocate a basic NIS object structure */
	if (dest) {
		memset((char *)dest, 0, sizeof (nis_object));
		result = dest;
	} else
		result = (nis_object *)calloc(1, sizeof (nis_object));

	if (result == NULL)
		return (NULL);

	/* Encode our object into the clone buffer */
	xdr_setpos(&in_xdrs, 0);
	status = xdr_nis_object(&in_xdrs, obj);
	if (status == FALSE)
		return (NULL);

	/* Now decode the buffer into our result pointer ... */
	xdr_setpos(&out_xdrs, 0);
	status = xdr_nis_object(&out_xdrs, result);
	if (status == FALSE)
		return (NULL);

	/* presto changeo, a new object */
	return (result);
} /* __clone_object_r */


nis_object *
nis_clone_object(obj, dest)
	nis_object	*obj;	/* The object to clone */
	nis_object	*dest;	/* Use this pointer if non-null */
{
	struct nis_sdata *clone_buf_ptr;

	if (_thr_main())
		clone_buf_ptr = &clone_buf_main;
	else
		clone_buf_ptr = (struct nis_sdata *)
			thr_get_storage(&clone_buf_key,
			sizeof (struct nis_sdata), destroy_nis_sdata);
	return (nis_clone_object_r(obj, dest, clone_buf_ptr));
} /* __clone_object */


/*
 * __break_name() converts a NIS name into it's components, returns an
 * array of char pointers pointing to the components and INVERTS there
 * order so that they are root first, then down. The list is terminated
 * with a null pointer. Returned memory can be freed by freeing the last
 * pointer in the list and the pointer returned.
 */
char	**
__break_name(name, levels)
	nis_name	name;
	int		*levels;
{
	char	**pieces;	/* pointer to the pieces */
	char	*s;		/* Temporary */
	char	*data;		/* actual data and first piece pointer. */
	int	components;	/* Number of estimated components */
	int	i, namelen;	/* Length of the original name. */

	/* First check to see that name is not NULL */
	if (!name)
		return (NULL);
	if ((namelen = strlen(name)) == 0)
		return (NULL);	/* Null string */

	namelen = strlen(name);

	data = strdup(name);
	if (!data)
		return (NULL);	/* No memory! */

	/* Kill the optional trailing dot */
	if (*(data+namelen-1) == '.') {
		*(data+namelen-1) = '\0';
		namelen--;
	}
	s = data;
	components = 1;
	while (*s != '\0') {
		if (*s == '.') {
			*s = '\0';
			components++;
			s++;
		} else if (*s == '"') {
			if (*(s+1) == '"') { /* escaped quote */
				s += 2;
			} else {
				/* skip quoted string */
				s++;
				while ((*s != '"') && (*s != '\0'))
					s++;
				if (*s == '"') {
					s++;
				}
			}
		} else {
			s++;
		}
	}
	pieces = (char **)calloc(components+1, sizeof (char *));
	if (! pieces) {
		free(data);
		return (NULL);
	}

	/* store in pieces in inverted order */
	for (i = (components-1), s = data; i > -1; i--) {
		*(pieces+i) = s;
		while (*s != '\0')
			s++;
		s++;
	}
	*(pieces+components) = NULL;
	*levels = components;

	return (pieces);
}

int
__name_distance(targ, test)
	char	**targ;	/* The target name */
	char	**test; /* the test name */
{
	int	distance = 0;

	/* Don't count common components */
	while ((*targ && *test) && (strcasecmp(*targ, *test) == 0)) {
		targ++;
		test++;
	}

	/* count off the legs of each name */
	while (*test != NULL) {
		test++;
		distance++;
	}

	while (*targ != NULL) {
		targ++;
		distance++;
	}

	return (distance);
}


/*
 * For returning errors in a NIS result structure
 */
nis_result *
nis_make_error(err, aticks, cticks, dticks, zticks)
	nis_error	err;
	u_long		aticks,	/* Profile information for client */
			cticks,
			dticks,
			zticks;
{
	nis_result	*nres;

	nres = (nis_result *)malloc(sizeof (nis_result));
	if (!nres)
		return (NULL);
	memset((char *)nres, 0, sizeof (nis_result));
	nres->status = err;
	nres->aticks = aticks;
	nres->zticks = zticks;
	nres->dticks = dticks;
	nres->cticks = cticks;
	return (nres);
}

#define	ZVAL zattr_val.zattr_val_val
#define	ZLEN zattr_val.zattr_val_len

/*
 * __cvt2attr()
 *
 * This function converts a search criteria of the form :
 *	[ <key>=<value>, <key>=<value>, ... ]
 * Into an array of nis_attr structures.
 */

nis_attr *
__cvt2attr(na, attrs)
	int	*na; 		/* Number of attributes 	*/
	char	**attrs; 	/* Strings associated with them */

{
	int		i;
	nis_attr	*zattrs;
	char		*s, *t;

	zattrs = (nis_attr *)calloc(*na, sizeof (nis_attr));
	if (! zattrs)
		return (NULL);

	for (i = 0; i < *na; i++) {
		zattrs[i].zattr_ndx = *(attrs+i);
		for (s = zattrs[i].zattr_ndx; *s != NUL; s++) {
			if (*s == '=') {
				*s = NUL;
				s++;
				while (isspace(*s)) /* Skip leading spaces */
					s++;
				zattrs[i].ZVAL = s;
				zattrs[i].ZLEN = strlen(s)+1;
				break;
			} else if (*s == '"') {
				if (*(s+1) == '"') { /* escaped quote */
					s += 2;
				} else {
					/* skip quotes */
					s++;
					while ((*s != '"') && (*s != NUL))
						s++;
					if (*s == '"')
						s++;
				}
			}
		}
		/*
		 * POLICY : Missing value for an index name is an
		 *	    error. The other alternative is the missing
		 *	    value means "is present" unfortunately there
		 *	    is no standard "is present" indicator in the
		 *	    existing databases.
		 * ANSWER : Always return an error.
		 */
		if (! zattrs[i].ZVAL) {
			free(zattrs);
			return (NULL);
		} else if ((*zattrs[i].ZVAL == '"') &&
			    (*(zattrs[i].ZVAL+1) != '"')) {
			for (t = zattrs[i].ZVAL+1; *t != NUL; t++)
				*(t-1) = *t; /* left shift by one char */
			*(t-2) = NUL; /* Trailing quote as well. */
			zattrs[i].ZLEN = strlen(zattrs[i].ZVAL)+1;
		}
	}
	return (zattrs);
}

/*
 * nis_free_request()
 *
 * Free memory associated with a constructed list request.
 */
void
nis_free_request(req)
	ib_request	*req;
{
	if (req->ibr_srch.ibr_srch_len) {
		/* free the string memory */
		free(req->ibr_srch.ibr_srch_val[0].zattr_ndx);
		/* free the nis_attr array */
		free(req->ibr_srch.ibr_srch_val);
	}

	if (req->ibr_name)
		free(req->ibr_name);
}

/*
 * nis_get_request()
 *
 * This function takes a NIS name, and converts it into an ib_request
 * structure. The request can then be used in a call to the nis service
 * functions. If the name wasn't parseable it returns an appropriate
 * error. This function ends up allocating an array of nis_attr structures
 * and a duplicate of the name string passed. To free this memory you
 * can call nis_free_request(), or you can simply free the first nis_attr
 * zattr_ndx pointer (the string memory) and the nis_attr pointer which
 * is the array.
 */
nis_error
nis_get_request(name, obj, cookie, req)
	nis_name	name;		/* search criteria + Table name	*/
	nis_object	*obj;		/* Object for (rem/modify/add)	*/
	netobj		*cookie;	/* Pointer to a cookie		*/
	ib_request	*req;		/* Request structure to fill in */
{
	register char	*s, *t; 	/* Some string pointer temps */
	register char	**attr;		/* Intermediate attributes */
	register int	i;		/* Counter variable */
	char		*data;		/* pointer to malloc'd string */
	int		zn = 0,		/* Count of attributes		*/
			datalen;	/* length of malloc'd data	*/

	u_char		within_attr_val;
				/*
				 * a boolean to indicate the current parse
				 * location is within the attribute value
				 * - so that we can stop deleting white
				 * space within an attribute value
				 */

	memset((char *)req, 0, sizeof (ib_request));

	if (!name)
		return (NIS_BADNAME);

	s = name;

	/* Move to the start of the components */
	while (isspace(*s))
		s++;

	if (*s == '[') {

		s++; /* Point past the opening bracket */

		datalen = strlen(s);
		data = (char *)calloc(1, datalen+1);
		if (!data)
			return (NIS_NOMEMORY);

		t = data; /* Point to the databuffer */
		while ((*s != NUL) && (*s != ']')) {
			while (isspace(*s)) {
				s++;
			}
			/* Check to see if we finished off the string */
			if ((*s == NUL) || (*s == ']'))
				break;

			/* If *s == comma its a null criteria */
			if (*s == COMMA) {
				s++;
				continue;
			}
			/* Not a space and not a comma, process an attr */
			zn++;
			within_attr_val = 0; /* not within attr_val right now */
			while ((*s != COMMA) && (*s != ']') && (*s != NUL)) {
				if (*s == '"') {
					if (*(s+1) == '"') { /* escaped quote */
						*t++ = *s; /* copy one quote */
						s += 2;
					} else {
						/* skip quoted string */
						s++;
						while ((*s != '"')&&(*s != NUL))
							*t++ = *s++;
						if (*s == '"') {
							s++;
						}
					}
				} else if (*s == '=') {
					*t++ = *s++;
					within_attr_val = 1;
				} else if (isspace(*s) && !within_attr_val) {
					s++;
				} else
					*t++ = *s++;
			}
			*t++ = NUL; /* terminate the attribute */
			if (*s == COMMA)
				s++;
		}
		if (*s == NUL) {
			free(data);
			return (NIS_BADATTRIBUTE);
		}

		/* It wasn't a NUL so it must be the closing bracket. */
		s++;
		/* Skip any intervening white space and "comma" */
		while (isspace(*s) || (*s == COMMA)) {
			s++;
		}
		strcpy(t, s); /* Copy the name into our malloc'd buffer */

		/*
		 * If we found any attributes we process them, the
		 * data string at this point is completely nulled
		 * out except for attribute data. We recover this
		 * data by scanning the string (we know how long it
		 * is) and saving to each chunk of non-null data.
		 */
		if (zn) {
			/* Save this as the table name */
			req->ibr_name = strdup(t);
			attr = (char **)calloc(zn+1, sizeof (char *));
			if (! attr) {
				free(data);
				free(req->ibr_name);
				req->ibr_name = 0;
				return (NIS_NOMEMORY);
			}

			/* store in pieces in attr array */
			for (i = 0, s = data; i < zn; i++) {
				*(attr+i) = s;
				/* Advance s past this component */
				while (*s != '\0')
					s++;
				s++;
			}
			*(attr+zn) = NULL;
		} else {
			free(data);
			req->ibr_name = strdup(s);
		}
	} else {
		/* Null search criteria */
		req->ibr_name = strdup(s);
	}

	if (zn) {
		req->ibr_srch.ibr_srch_len = zn;
		req->ibr_srch.ibr_srch_val = __cvt2attr(&zn, attr);
		free(attr); /* don't need this any more */
		if (! (req->ibr_srch.ibr_srch_val)) {
			req->ibr_srch.ibr_srch_len = 0;
			free(req->ibr_name);
			req->ibr_name = 0;
			free(data);
			return (NIS_BADATTRIBUTE);
		}
	}

	if (obj) {
		req->ibr_obj.ibr_obj_len = 1;
		req->ibr_obj.ibr_obj_val = obj;
	}
	if (cookie) {
		req->ibr_cookie = *cookie;
	}
	return (NIS_SUCCESS);
}

/* Various subroutines used by the server code */
nis_object *
nis_read_obj(f)
	char	*f;	/* name of the object to read */
{
	FILE	*rootfile;
	int	status;	/* Status of the XDR decoding */
	XDR	xdrs;	/* An xdr stream handle */
	nis_object	*res;

	res = (nis_object *)calloc(1, sizeof (nis_object));
	if (! res)
		return (NULL);

	rootfile = fopen(f, "r");
	if (rootfile == NULL) {
		/* This is ok if we are the root of roots. */
		free(res);
		return (NULL);
	}
	/* Now read in the object */
	xdrstdio_create(&xdrs, rootfile, XDR_DECODE);
	status = xdr_nis_object(&xdrs, res);
	xdr_destroy(&xdrs);
	fclose(rootfile);
	if (!status) {
		syslog(LOG_ERR, "Object file %s is corrupt!", f);
		xdr_free(xdr_nis_object, (char *)res);
		free(res);
		return (NULL);
	}
	return (res);
}

int
nis_write_obj(f, o)
	char	*f;	/* name of the object to read */
	nis_object *o;	/* The object to write */
{
	FILE	*rootfile;
	int	status;	/* Status of the XDR decoding */
	XDR	xdrs;	/* An xdr stream handle */

	rootfile = fopen(f, "w");
	if (rootfile == NULL) {
		return (0);
	}
	/* Now encode the object */
	xdrstdio_create(&xdrs, rootfile, XDR_ENCODE);
	status = xdr_nis_object(&xdrs, o);
	xdr_destroy(&xdrs);
	fclose(rootfile);
	return (status);
}

/*
 * nis_make_rpchandle()
 *
 * This is a generic version of clnt_creat() for NIS. It localizes
 * _all_ of the changes needed to port to TLI RPC into this one
 * section of code.
 */

#ifdef TDRPC

/*
 * Transport dependent code.
 * Look for the target protocol in the host's list of endpoints.
 * If found, decode the uaddr and appropriately
 * set the sin_addr and sin_port of the given 'sockaddr_in' structure.
 * Leaves the rest of the sockaddr_in structure unchanged (i.e. you should
 * do a memset on it if you care about the other components).
 * Also, you need to set sin_family (e.g. addr.sin_family = AF_INET) yourself.
 * Returns RPC_SUCCESS if successful; RPC_CANTSEND otherwise.
 *
 * This is not static so that the TDRPC version of nis_cast can use it.
 */
enum clnt_stat
nis_find_sockaddr(host, addr, target_proto)
	nis_server *host;
	struct sockaddr_in *addr;
	char *target_proto;
{
	int i, num_ends;
	char *uaddr;
	struct endpoint *host_ends;
	unsigned long a1, a2, a3, a4, p1, p2;

	host_ends = host->ep.ep_val;
	num_ends = host->ep.ep_len;
	uaddr = 0;

	for (i = 0; i < num_ends; i++) {
		if (strcasecmp(host_ends[i].family, "inet") == 0 &&
		    strcasecmp(host_ends[i].proto, target_proto) == 0) {
			uaddr = host_ends[i].uaddr;
			break;
		}
	}

	if (uaddr == 0)
		return (RPC_CANTSEND);

	sscanf(uaddr, "%d.%d.%d.%d.%d.%d", &a1, &a2, &a3, &a4, &p1, &p2);
	addr->sin_addr.s_addr = (a1 << 24) | (a2 << 16) | (a3 << 8) | a4;
	addr->sin_port = (p1 << 8) | p2;
	return (RPC_SUCCESS);
}

/*
 * Transport DEPENDENT RPC code. This code assumes you
 * are using the old RPC/sockets code.
 */
CLIENT *
nis_make_rpchandle(srv, cback, prog, ver, flags, inbuf, outbuf)
	nis_server	*srv;	/* NIS Server description 		*/
	int		cback;	/* Callback flag (not needed for TIRPC) */
	u_long		prog,	/* Program number			*/
			ver;	/* Version				*/
	u_long		flags;	/* Flags, {VC, DG, AUTH}  		*/
	int		inbuf,	/* Preferred buffer sizes 		*/
			outbuf;	/* for input and output   		*/
{
	CLIENT	*clnt;
	struct sockaddr_in	addr; /* address 	*/
	int			rpc_fd;
	struct timeval		tv;
	char	*s;
	int	uid, gid;
	char	netname[MAXNETNAMELEN+1];

	rpc_fd = RPC_ANYSOCK;
	memset((char *)&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;

	if (nis_find_sockaddr(srv, &addr, (flags&ZMH_DG)? "udp" : "tcp") !=
						RPC_SUCCESS) {
		syslog(LOG_ERR,
			"nis_make_rpchandle: cannot find socket address");
		return (NULL);
	}
	if (! cback)
		addr.sin_port = 0;
	memset(addr.sin_zero, 0, 8);  /* already did memset above? [RL] */
	if ((flags & ZMH_DG) != 0) {
		tv.tv_sec = 5; /* 5 second retry's */
		tv.tv_usec = 0;
		clnt = clntudp_create(&addr, prog, ver, tv, &rpc_fd,
							outbuf, inbuf);
	} else if ((flags & ZMH_VC) != 0) {
		clnt = clnttcp_create(&addr, prog, ver, &rpc_fd, outbuf, inbuf);
	} else {
		clnt = NULL;
	}

	if (! clnt) {
		syslog(LOG_ERR, "%s", clnt_spcreateerror("nis_make_rpchandle"));
		return (NULL);
	}

	if ((flags & ZMH_AUTH) != 0) {
		switch (srv->key_type) {
		case NIS_PK_DH :
			if (! cback)
				host2netname(netname, srv->name, NULL);
			else
				strcpy(netname, srv->name);
			clnt->cl_auth = (AUTH *)authdes_pk_create(netname,
						&(srv->pkey), 35, &addr, NULL);
			if (clnt->cl_auth)
				break;
			/* if you can't get DES, try SYS creds */
		case NIS_PK_NONE :
			uid = geteuid();
			gid = getegid();
			clnt->cl_auth = authunix_create(nis_local_host(),
					uid, gid, 0, NULL);
			if (clnt->cl_auth)
				break;
			/* if you can't get SYS, try none. */
		default :
			clnt->cl_auth = authnone_create();
			if (clnt->cl_auth)
				break;
			syslog(LOG_CRIT,
			"nis_rpc_makehandle: cannot create cred.");
			abort();
		}
	}

	return (clnt);
}
#else
/*
 * Transport INDEPENDENT RPC code. This code assumes you
 * are using the new RPC/tli code and will build
 * a ping handle on top of a datagram transport.
 */

/*
 * __map_addr()
 *
 * This is our internal function that replaces rpcb_getaddr(). We
 * build our own to prevent calling netdir_getbyname() which could
 * recurse to the nameservice.
 */
static char *
__map_addr(nc, uaddr, prog, ver)
	struct netconfig	*nc;		/* Our transport	*/
	char			*uaddr;		/* RPCBIND address */
	u_long			prog, ver;	/* Name service Prog/vers */
{
	register CLIENT *client;
	RPCB 		parms;		/* Parameters for RPC binder	  */
	struct netbuf	*addr;		/* Address of the rpcbind process */
	enum clnt_stat	clnt_st;	/* Result from the rpc call	  */
	char 		*ua = NULL;	/* Universal address of service	  */
	char		*res = NULL;	/* Our result to the parent	  */
	struct timeval	tv;		/* Timeout for our rpcb call	  */
	int		ilen, olen;	/* buffer length for cli_crate    */

	/*
	 * Get a netbuf that points to the remote rpcbind process
	 */
	addr = uaddr2taddr(nc, uaddr);
	if (! addr) {
		syslog(LOG_ERR,
			"__map_addr: Unable to convert uaddr %s (%d).",
			uaddr, _nderror);
		return (NULL);
	}

	/*
	 * If using "udp", use __nisipbufsize if inbuf and outbuf are set to 0.
	 */
	if (strcmp("udp", nc->nc_netid) == 0) {
			/* for udp only */
		ilen = olen = __nisipbufsize;
	} else {
		ilen = olen = 0;
	}
	client = clnt_tli_create(RPC_ANYFD, nc, addr,
				RPCBPROG, RPCBVERS, ilen, olen);
	netdir_free((char *) addr, ND_ADDR);
	if (! client) {
		return (NULL);
	}

	clnt_control(client, CLSET_FD_CLOSE, NULL);

	/*
	 * Now make the call to get the NIS service address.
	 */
	tv.tv_sec = 10;
	tv.tv_usec = 0;
	parms.r_prog = prog;
	parms.r_vers = ver;
	parms.r_netid = nc->nc_netid;	/* not needed */
	parms.r_addr = "";	/* not needed; just for xdring */
	parms.r_owner = "";	/* not needed; just for xdring */
	clnt_st = clnt_call(client, RPCBPROC_GETADDR, xdr_rpcb, (char *) &parms,
					    xdr_wrapstring, (char *) &ua, tv);

	if (clnt_st == RPC_SUCCESS) {
		clnt_destroy(client);
		if (*ua == '\0') {
			mem_free(ua, 1);
			return (NULL);
		}
		res = strdup(ua);
		xdr_free(xdr_wrapstring, (char *) &ua);
		return (res);
	} else if (((clnt_st == RPC_PROGVERSMISMATCH) ||
			(clnt_st == RPC_PROGUNAVAIL)) &&
			(strcmp(nc->nc_protofmly, NC_INET) == 0)) {
		/*
		 * version 3 not available. Try version 2
		 * The assumption here is that the netbuf
		 * is arranged in the sockaddr_in
		 * style for IP cases.
		 */
		u_short 		port;
		struct sockaddr_in	*sa;
		struct netbuf 		remote;
		int			protocol;
		char			buf[32];

		clnt_control(client, CLGET_SVC_ADDR, (char *) &remote);
		sa = (struct sockaddr_in *)(remote.buf);
		protocol = strcmp(nc->nc_proto, NC_TCP) ?
				IPPROTO_UDP : IPPROTO_TCP;
		port = (u_short) pmap_getport(sa, prog, ver, protocol);

		if (port != 0) {
			port = htons(port);
			sprintf(buf, "%d.%d.%d.%d.%d.%d",
				(sa->sin_addr.s_addr >> 24) & 0xff,
				(sa->sin_addr.s_addr >> 16) & 0xff,
				(sa->sin_addr.s_addr >>  8) & 0xff,
				(sa->sin_addr.s_addr) & 0xff,
				(port >> 8) & 0xff,
				port & 0xff);
			res = strdup(buf);
		} else
			res = NULL;
		clnt_destroy(client);
		return (res);
	}
	if (clnt_st == RPC_TIMEDOUT)
		syslog(LOG_ERR, "NIS+ server not responding");
	else
		syslog(LOG_ERR, "NIS+ server could not be contacted: %s",
					clnt_sperrno(clnt_st));
	clnt_destroy(client);
	return (NULL);
}

CLIENT *
nis_make_rpchandle(srv, cback, prog, ver, flags, inbuf, outbuf)
	nis_server	*srv;	/* NIS Server description 		*/
	int		cback;	/* Boolean indicating callback address	*/
	u_long		prog,	/* Program number			*/
			ver;	/* Version				*/
	u_long		flags;	/* Flags, {VC, DG, AUTH}  		*/
	int		inbuf,	/* Preferred buffer sizes 		*/
			outbuf;	/* for input and output   		*/
{
	CLIENT			*clnt; 		/* Client handle 	*/
	struct netbuf		*addr; 		/* address 		*/
	char			*svc_addr;	/* Real service address */
	void			*nc_handle;	/* Netconfig "state"	*/
	struct netconfig	*nc, *maybe;	/* Various handles	*/
	endpoint		*ep, *mep;	/* useful endpoints	*/
	int			epl, i;		/* counters		*/
	int			fd;		/* file descriptors	*/
	int			uid, gid;	/* Effective uid/gid	*/
	char			netname[MAXNETNAMELEN+1]; /* our netname */
	int		ilen, olen;	/* buffer length for cli_crate    */

	nc_handle = (void *) setnetconfig();
	if (! nc_handle)
		return (NULL);

	ep = srv->ep.ep_val;
	epl = srv->ep.ep_len;

	/*
	 * The transport policies :
	 * Selected transport must be visible.
	 * Must have requested or better semantics.
	 * Must be correct protocol.
	 */
	while ((nc = (struct netconfig *) getnetconfig(nc_handle)) != NULL) {

		/* Is it a visible transport ? */
		if ((nc->nc_flag & NC_VISIBLE) == 0)
			continue;

		/* If we asked for a virtual circuit, is it a vc transport ? */
		if (((flags & ZMH_VC) != 0) &&
			((nc->nc_semantics & NC_TPI_COTS) == 0))
			continue;

		/* Check to see is we talk this protofmly, protocol */
		for (i = 0; i < epl; i++) {
			if ((strcasecmp(nc->nc_protofmly, ep[i].family) == 0) &&
			    (strcasecmp(nc->nc_proto, ep[i].proto) == 0))
				break;
		}

		/* Was it one of our transports ? */
		if (i == epl)
			continue;	/* No */

		/*
		 * If it is one of our supported transports, but it isn't
		 * a datagram and we want a datagram, keep looking but
		 * remember this one as a possibility.
		 */
		if (((flags & ZMH_DG) != 0) &&
			(nc->nc_semantics != NC_TPI_CLTS)) {
			maybe = nc;
			mep = &ep[i]; /* This endpoint */
			continue;
		}

		ep = &ep[i]; /* This endpoint */
		break;
	}

	if (! nc && ! maybe)
		return (NULL);  /* Can't support transport */

	if (! nc && maybe) {
		nc = maybe;	/* Use our second choice */
		ep = mep;
	}

	/*
	 * At this point we have the address of the remote binder
	 * and we have to find out where the service _really_ is.
	 */
	if (cback) {
		addr = uaddr2taddr(nc, ep->uaddr);
	} else {
		svc_addr = __map_addr(nc, ep->uaddr, prog, ver);
		if (! svc_addr) {
			endnetconfig(nc_handle);
			return (NULL);
		} else {
			addr = uaddr2taddr(nc, svc_addr);
			free(svc_addr);
		}
	}
	if (addr == NULL) {
		endnetconfig(nc_handle);
		syslog(LOG_ERR,
	"nis_make_rpchandle: Unable to convert uaddr %s for %s (%d).",
				ep->uaddr, srv->name, _nderror);
		return (NULL);
	}

	/*
	 * Create the client handle.
	 * If using "udp", use __nisipbufsize if inbuf and outbuf are set to 0.
	 */
	if (strcmp("udp", nc->nc_netid) == 0) {
			/* for udp only */
		ilen = (inbuf == 0) ? __nisipbufsize : inbuf;
		olen = (outbuf == 0) ? __nisipbufsize : outbuf;
	} else {
		ilen = inbuf;
		olen = outbuf;
	}
	clnt = clnt_tli_create(RPC_ANYFD, nc, addr, prog, ver, ilen, olen);
	if (clnt) {
		if (clnt_control(clnt, CLGET_FD, (char *)&fd))
			fcntl(fd, F_SETFD, 1); /* make it "close on exec" */
		clnt_control(clnt, CLSET_FD_CLOSE, NULL);
	}

	/* This frees the netconfig data */
	endnetconfig(nc_handle);
	/* This frees the netbuf */
	netdir_free((char *) addr, ND_ADDR);

	if (clnt && ((flags & ZMH_AUTH) != 0)) {
		switch (srv->key_type) {
		case NIS_PK_DH :
			if (! cback)
				host2netname(netname, srv->name, NULL);
			else
				strcpy(netname, srv->name);
			clnt->cl_auth = (AUTH *)authdes_pk_seccreate(netname,
					&srv->pkey, 15, NULL, NULL, srv);
			if (clnt->cl_auth)
				break;
		case NIS_PK_NONE :
			uid = geteuid();
			gid = getegid();
			clnt->cl_auth = authsys_create(nis_local_host(), uid,
						gid, 0, NULL);
			if (clnt->cl_auth)
				break;
		default :
			clnt->cl_auth = authnone_create();
			if (clnt->cl_auth)
				break;
			syslog(LOG_CRIT,
			"nis_rpc_makehandle: cannot create cred.");
			abort();
		}
	}
	if (! clnt)
		syslog(LOG_ERR, "%s", clnt_spcreateerror("nis_make_rpchandle"));

	return (clnt);
}
#endif /* ! TDRPC */

static mutex_t __nis_ss_used_lock = DEFAULTMUTEX; /* lock level 3 */
int	__nis_ss_used = 0;

/*
 * nis_get_static_storage()
 *
 * This function is used by various functions in their effort to minimize the
 * hassles of memory management in an RPC daemon. Because the service doesn't
 * implement any hard limits, this function allows people to get automatically
 * growing buffers that meet their storage requirements. It returns the
 * pointer in the nis_sdata structure.
 *
 */
void *
nis_get_static_storage(bs, el, nel)
	struct nis_sdata 	*bs; 	/* User buffer structure */
	u_long			el;	/* Sizeof elements	 */
	u_long			nel;	/* Number of elements	 */
{
	register u_long	sz;
	sigset_t	oset;

	sz = nel * el;
	if (! bs)
		return (NULL);

	if (! bs->buf) {
		bs->buf = (void *) malloc(sz);
		if (! bs->buf)
			return (NULL);
		thr_sigblock(&oset);
		mutex_lock(&__nis_ss_used_lock);
		__nis_ss_used += sz;
		mutex_unlock(&__nis_ss_used_lock);
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	} else if (bs->size < sz) {
		int 	size_delta;

		free(bs->buf);
		size_delta = - (bs->size);
		bs->buf = (void *) malloc(sz);

		/* check the result of malloc() first	*/
		/* then update the statistic.		*/
		if (! bs->buf)
			return (NULL);
		bs->size = sz;
		size_delta += sz;
		thr_sigblock(&oset);
		mutex_lock(&__nis_ss_used_lock);
		__nis_ss_used += size_delta;
		mutex_unlock(&__nis_ss_used_lock);
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);

	}

	(void *)memset(bs->buf, 0, sz); /* SYSV version of bzero() */
	return (bs->buf);
}

char *
nis_old_data_r(s, bs_ptr)
	char	*s;
	struct nis_sdata	*bs_ptr;
{
	char			*buf;
	char			temp[1024];

	buf = (char *)nis_get_static_storage(bs_ptr, 1, 1024);

	if (! buf)
		return (NULL);

	/*
	 * this saving of 's' is because the routines that call nis_data()
	 * are not very careful about what they pass in.  Sometimes what they
	 * pass in are 'static' returned from some of the routines called
	 * below nis_leaf_of(),  nis_local_host() and so on.
	 */
	if (s)
		sprintf(temp, "/%s", s);
	strcpy(buf, __nis_data_directory);
	strcat(buf, nis_leaf_of(nis_local_host()));
	if (s)
		strcat(buf, temp);

	for (s = buf; *s; s++) {
		if (isupper(*s))
			*s = tolower(*s);
	}

	return (buf);
}

char *
nis_old_data(s)
	char	*s;
{
	static thread_key_t 	bs_key;
	static struct nis_sdata	bs_main;
	struct nis_sdata	*bs_ptr;

	if (_thr_main())
		bs_ptr = &bs_main;
	else
		bs_ptr = (struct nis_sdata *)
			thr_get_storage(&bs_key, sizeof (struct nis_sdata),
			destroy_nis_sdata);
	return (nis_old_data_r(s, bs_ptr));
}


char *
nis_data_r(s, bs_ptr)
	char	*s;
	struct nis_sdata	*bs_ptr;
{
	char			*buf;
	char			temp[1024];

	buf = (char *)nis_get_static_storage(bs_ptr, 1, 1024);

	if (! buf)
		return (NULL);

	/*
	 * this saving of 's' is because the routines that call nis_data()
	 * are not very careful about what they pass in.  Sometimes what they
	 * pass in are 'static' returned from some of the routines called
	 * below nis_leaf_of(),  nis_local_host() and so on.
	 */
	if (s)
		sprintf(temp, "/%s", s);
	strcpy(buf, __nis_data_directory);
	strcat(buf, NIS_DIR);
	if (s)
		strcat(buf, temp);

	for (s = buf; *s; s++) {
		if (isupper(*s))
			*s = tolower(*s);
	}

	return (buf);
}

char *
nis_data(s)
	char	*s;
{
	static thread_key_t 	bs_key;
	static struct nis_sdata	bs_main;
	struct nis_sdata	*bs_ptr;

	if (_thr_main())
		bs_ptr = &bs_main;
	else
		bs_ptr = (struct nis_sdata *)
			thr_get_storage(&bs_key, sizeof (struct nis_sdata),
			destroy_nis_sdata);
	return (nis_data_r(s, bs_ptr));
}

/*
 * Return the directory name of the root_domain of the caller's NIS+
 * domain.
 *
 * This routine is a temporary implementation and should be
 * provided as part of the the NIS+ project.  See RFE:  1103216
 * Required for root replication.
 *
 * XXX MT safing: local_root_lock protects the local_root structure.
 *
 * It tries to determine the root domain
 * name by "walking" the path up the NIS+ directory tree, starting
 * at nis_local_directory() until a NIS_NOSUCHNAME or NIS_NOTFOUND error
 * is obtained.  Returns 0 on fatal errors obtained before this point,
 * or if it exhausts the domain name without ever obtaining one of
 * of these errors.
 */

static nis_name local_root = 0;
static mutex_t local_root_lock = DEFAULTMUTEX;

nis_name
__nis_local_root()
{
	char *dir;
	int found_root = 0;
	int try_count = 0;
	int fatal_error = 0;
	char *prev_testdir;
	char *testdir;
	sigset_t	   oset;

	thr_sigblock(&oset);
	mutex_lock(&local_root_lock);
	if (local_root) {
		mutex_unlock(&local_root_lock);
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
		return (local_root);
	}
	local_root = (nis_name)calloc(1, LN_BUFSIZE);

	if (!local_root) {
		mutex_unlock(&local_root_lock);
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
		return (0);
	}
	/*  walk up NIS+ tree till we find the root. */
	dir = strdup(nis_local_directory());
	prev_testdir = dir;
	testdir = nis_domain_of(prev_testdir);

	while (testdir && !found_root && !fatal_error) {
	    /* try lookup */
	    nis_result* nis_ret = nis_lookup(testdir, (u_long)0);
	    /* handle return status */
	    switch (nis_ret->status) {
	    case NIS_SUCCESS:
	    case NIS_S_SUCCESS:
		try_count = 0;
		prev_testdir = testdir;
		testdir = nis_domain_of(prev_testdir);
		break;
	    case NIS_NOSUCHNAME:
	    case NIS_NOTFOUND:
	    case NIS_NOT_ME:
	    case NIS_FOREIGNNS:
		found_root = 1;
		break;
	    case NIS_TRYAGAIN:
	    case NIS_CACHEEXPIRED:
		/* sleep 1 second and try same name again, up to 10 times */
		/* REMIND: This is arbitrary! BAD! */
		sleep(1);
		fatal_error = (try_count++ > 9);
		break;
	    case NIS_NAMEUNREACHABLE:
	    case NIS_SYSTEMERROR:
	    case NIS_RPCERROR:
	    case NIS_NOMEMORY:
	    default:
		fatal_error = 1;
		break;
	    }
	    if (nis_ret) nis_freeresult(nis_ret);
	}

	if (!found_root) {
		free(dir);
		mutex_unlock(&local_root_lock);
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
		return (0);
	}
	strcpy(local_root, prev_testdir);
	free(dir);
	mutex_unlock(&local_root_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	return (local_root);
}
