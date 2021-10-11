#include "synonyms.h"
#include <mtlib.h>
#include <synch.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <dlfcn.h>
#include <nsswitch.h>
#include <syslog.h>

#define	islabel(c) 	(isalnum(c) || (c) == '_')
/*
 * This file has all the routines that access the configuration
 * information.
 */

static struct cons_cell { /* private to the parser */
	struct __nsw_switchconfig *sw;
	struct cons_cell *next;
};

/*
 * Local routines
 */

static char *skip(), *labelskip(), *spaceskip();
static struct __nsw_switchconfig *scrounge_cache();

static struct cons_cell *concell_list; /* stays with add_concell() */

/*
 * Private interface used by nss_common.c, hence this function is not static
 */
struct __nsw_switchconfig *
_nsw_getoneconfig(name, linep, errp)
	const char		*name;
	char			*linep;	/* Nota Bene: not const char *	*/
	enum __nsw_parse_err	*errp;	/* Meanings are abused a bit	*/
{
	struct __nsw_switchconfig *cfp;
	struct __nsw_lookup *lkp, **lkq;
	int act, end_crit;
	char *p, *tokenp;

	*errp = __NSW_CONF_PARSE_SUCCESS;

	if ((cfp = (struct __nsw_switchconfig *)
	    calloc(1, sizeof (struct __nsw_switchconfig))) == NULL) {
		*errp = __NSW_CONF_PARSE_SYSERR;
		return (NULL);
	}
	cfp->dbase = strdup(name);
	lkq = &cfp->lookups;

	/* linep points to a naming service name */
	while (1) {
		int i;

		/* white space following the last service */
		if (*linep == '\0' || *linep == '\n') {
			return (cfp);
		}
		if ((lkp = (struct __nsw_lookup *)
		    calloc(1, sizeof (struct __nsw_lookup))) == NULL) {
			*errp = __NSW_CONF_PARSE_SYSERR;
			freeconf(cfp);
			return (NULL);
		}

		*lkq = lkp;
		lkq = &lkp->next;

		for (i = 0; i < __NSW_STD_ERRS; i++)
			if (i == __NSW_SUCCESS)
				lkp->actions[i] = 1;
			else
				lkp->actions[i] = 0;

		/* get criteria for the naming service */
		if (tokenp = skip(&linep, '[')) { /* got criteria */

			/* premature end, illegal char following [ */
			if (!islabel(*linep))
				goto barf_line;
			lkp->service_name = strdup(tokenp);
			cfp->num_lookups++;
			end_crit = 0;

			/* linep points to a switch_err */
			while (1) {
				if ((tokenp = skip(&linep, '=')) == NULL) {
					goto barf_line;
				}

				/* premature end, ill char following = */
				if (!islabel(*linep))
					goto barf_line;

				/* linep points to the string following '=' */
				p = labelskip(linep);
				if (*p == ']')
					end_crit = 1;
				else if (*p != ' ' && *p != '\t')
					goto barf_line;
				*p++ = '\0'; /* null terminate linep */
				p = spaceskip(p);
				if (!end_crit && *p == ']') {
					end_crit = 1;
					*p++ = '\0';
				} else if (*p == '\0' || *p == '\n')
					return (cfp);
				else if (!islabel(*p))
					/* p better be the next switch_err */
					goto barf_line;
				if (strcasecmp(linep, __NSW_STR_RETURN) == 0)
					act = 1;
				else if (strcasecmp(linep,
					    __NSW_STR_CONTINUE) == 0)
					act = 0;
				else
					goto barf_line;
				if (strcasecmp(tokenp,
					    __NSW_STR_SUCCESS) == 0) {
					lkp->actions[__NSW_SUCCESS] = act;
				} else if (strcasecmp(tokenp,
					    __NSW_STR_NOTFOUND) == 0) {
					lkp->actions[__NSW_NOTFOUND] = act;
				} else if (strcasecmp(tokenp,
					    __NSW_STR_UNAVAIL) == 0) {
					lkp->actions[__NSW_UNAVAIL] = act;
				} else if (strcasecmp(tokenp,
					    __NSW_STR_TRYAGAIN) == 0) {
					lkp->actions[__NSW_TRYAGAIN] = act;
				} else {
					/*
					 * convert string tokenp to integer
					 * and put in long_errs
					 */
				}
				if (end_crit) {
					linep = spaceskip(p);
					if (*linep == '\0' || *linep == '\n')
						return (cfp);
					break; /* process next naming service */
				}
				linep = p;
			} /* end of while loop for a name service's criteria */
		} else {
			/*
			 * no criteria for this naming service.
			 * linep points to name service, but not null
			 * terminated.
			 */
			p = labelskip(linep);
			if (*p == '\0' || *p == '\n') {
				*p = '\0';
				lkp->service_name = strdup(linep);
				cfp->num_lookups++;
				return (cfp);
			}
			if (*p != ' ' && *p != '\t')
				goto barf_line;
			*p++ = '\0';
			lkp->service_name = strdup(linep);
			cfp->num_lookups++;
			linep = spaceskip(p);
		}
	} /* end of while(1) loop for a name service */

barf_line:
	freeconf(cfp);
	*errp = __NSW_CONF_PARSE_NOPOLICY;
	return (NULL);
}

static struct __nsw_switchconfig *
do_getconfig(dbase, errp)
const char *dbase;
enum __nsw_parse_err *errp;
{
	struct __nsw_switchconfig *cfp, *retp = NULL;
	FILE *fp;
	char *linep, *lineq;

/*	openlog("libc", LOG_CONS | LOG_NOWAIT, 0);	*/
	if (cfp = scrounge_cache(dbase)) {
		*errp = __NSW_CONF_PARSE_SUCCESS;
		return (cfp);
	}

	if ((fp = fopen(__NSW_CONFIG_FILE, "r")) == NULL) {
		*errp = __NSW_CONF_PARSE_NOFILE;
		return (NULL);
	}

	if ((lineq = (char *)malloc(BUFSIZ)) == NULL) {
		fclose(fp);
		*errp = __NSW_CONF_PARSE_SYSERR;
		return (NULL);
	}

	*errp = __NSW_CONF_PARSE_NOPOLICY;
	while (linep = fgets(lineq, BUFSIZ, fp)) {
		enum __nsw_parse_err	line_err;
		char			*tokenp, *comment;

		/*
		 * Ignore portion of line following the comment character '#'.
		 */
		if ((comment = strchr(linep, '#')) != NULL) {
			*comment = '\0';
		}
		/*
		 * skip past blank lines.
		 * otherwise, cache as a struct switchconfig.
		 */
		if ((*linep == '\0') || isspace(*linep)) {
			continue;
		}
		if ((tokenp = skip(&linep, ':')) == NULL) {
			continue; /* ignore this line */
		}
		if (cfp = scrounge_cache(tokenp)) {
			continue; /* ? somehow this database is in the cache */
		}
		if (cfp = _nsw_getoneconfig(tokenp, linep, &line_err)) {
			add_concell(cfp);
			if (strcmp(cfp->dbase, dbase) == 0) {
				*errp = __NSW_CONF_PARSE_SUCCESS;
				retp = cfp;
			}
		} else {
			/*
			 * Got an error on this line, if it is a system
			 * error we might as well give right now. If it
			 * is a parse error on the second entry of the
			 * database we are looking for and the first one
			 * was a good entry we end up logging the following
			 * syslog message and using a default policy instead.
			 */
			if (line_err == __NSW_CONF_PARSE_SYSERR) {
				*errp = __NSW_CONF_PARSE_SYSERR;
				break;
			} else if (line_err == __NSW_CONF_PARSE_NOPOLICY &&
				    strcmp(tokenp, dbase) == 0) {
				syslog(LOG_WARNING,
		"libc: bad lookup policy for %s in %s, using defaults..\n",
					dbase, __NSW_CONFIG_FILE);
				*errp = __NSW_CONF_PARSE_NOPOLICY;
				break;
			}
			/*
			 * Else blithely ignore problems on this line and
			 *   go ahead with the next line.
			 */
		}
	}
	free(lineq);
	fclose(fp);
	return (retp);
}

static struct __nsw_switchconfig *
scrounge_cache(dbase)
	char *dbase;
{
	struct cons_cell *cellp = concell_list;

	for (; cellp; cellp = cellp->next)
		if (strcmp(dbase, cellp->sw->dbase) == 0)
			return (cellp->sw);
	return (NULL);
}

static
freeconf(cfp)
	struct __nsw_switchconfig *cfp;
{
	if (cfp) {
		if (cfp->dbase)
			free(cfp->dbase);
		if (cfp->lookups) {
			struct __nsw_lookup *nex, *cur;
			for (cur = cfp->lookups; cur; cur = nex) {
				free(cur->service_name);
				nex = cur->next;
				free(cur);
			}
		}
		free(cfp);
	}
}

action_t
__nsw_extended_action(lkp, err)
	struct __nsw_lookup *lkp;
	int err;
{
	struct __nsw_long_err *lerrp;

	for (lerrp = lkp->long_errs; lerrp; lerrp = lerrp->next) {
		if (lerrp->nsw_errno == err)
			return (lerrp->action);
	}
	return (__NSW_CONTINUE);
}

/* give the next non-alpha character */
static char *
labelskip(cur)
	register char *cur;
{
	char *p = cur;
	while (islabel(*p))
		++p;
	return (p);
}

/* give the next non-space character */
static char *
spaceskip(cur)
	register char *cur;
{
	char *p = cur;
	while (*p == ' ' || *p == '\t')
		++p;
	return (p);
}

/*
 * terminate the *cur pointed string by null only if it is
 * followed by "key" surrounded by zero or more spaces and
 * return value is the same as the original *cur pointer and
 * *cur pointer is advanced to the first non {space, key} char
 * followed by the key. Otherwise, return NULL and keep
 * *cur unchanged.
 */
static char *
skip(cur, key)
	register char **cur;
	register char key;
{
	char *p = *cur, *q = *cur, *tmp;
	int found = 0, tmpfound = 0;

	tmp = labelskip(*cur);
	p = tmp;
	if (found = (*p == key)) {
		*p++ = '\0'; /* overwrite the key */
		p = spaceskip(p);
	} else {
		while (*p == ' ' || *p == '\t')
			if (tmpfound = (*++p == key)) {
				found = tmpfound;
					/* null terminate the return token */
				*tmp = '\0';
				p++; /* skip the key */
			}
	}
	if (!found)
		return (NULL); /* *cur unchanged */
	*cur = p;
	return (q);
}

/* add to the front: LRU */
static int
add_concell(cfp)
	struct __nsw_switchconfig *cfp;
{
	struct cons_cell *cp;

	if (cfp == NULL)
		return (1);
	if ((cp = (struct cons_cell *)malloc(sizeof (struct cons_cell)))
			== NULL)
		return (1);
	cp->sw = cfp;
	cp->next = concell_list;
	concell_list = cp;
	return (0);
}

static mutex_t serialize_config = DEFAULTMUTEX;

struct __nsw_switchconfig *
__nsw_getconfig(dbase, errp)
	const char		*dbase;
	enum __nsw_parse_err	*errp;
{
	struct __nsw_switchconfig	*res;

	/*
	 * ==== I don't feel entirely comfortable disabling signals for the
	 *	duration of this, but maybe we have to.  Or maybe we should
	 *	use mutex_trylock to detect recursion?  (Not clear what's
	 *	the right thing to do when it happens, though).
	 */
	mutex_lock(&serialize_config);
	res = do_getconfig(dbase, errp);
	mutex_unlock(&serialize_config);
	return (res);
}

int
__nsw_freeconfig(conf)
struct __nsw_switchconfig *conf;
{
	struct cons_cell *cellp;

	if (conf == NULL) {
		return (-1);
	}
	/*
	 * Hacked to make life easy for the code in nss_common.c.  Free conf
	 *   iff it was created by calling _nsw_getoneconfig() directly
	 *   rather than by calling nsw_getconfig.
	 */
	mutex_lock(&serialize_config);
	for (cellp = concell_list;  cellp;  cellp = cellp->next) {
		if (cellp->sw == conf) {
			break;
		}
	}
	mutex_unlock(&serialize_config);
	if (cellp == NULL) {
		/* Not in the cache;  free it */
		freeconf(conf);
		return (1);
	} else {
		/* In the cache;  don't free it */
		return (0);
	}
}
