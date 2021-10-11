/* Copyright (c) 1991 Sun Microsystems */
/* All Rights Reserved */

#ident  "@(#)gettext.c 1.21     95/06/05 SMI"

#ifdef __STDC__
#pragma weak bindtextdomain = _bindtextdomain
#pragma weak textdomain = _textdomain
#pragma weak gettext = _gettext
#pragma weak dgettext = _dgettext
#pragma weak dcgettext = _dcgettext
#endif

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <libintl.h>
#include <thread.h>
#include <synch.h>
#include "mtlibintl.h"
#include <limits.h>
#include <nl_types.h>
#include <unistd.h>

#define	DEFAULT_DOMAIN		"messages"
#define	DEFAULT_BINDING		"/usr/lib/locale"
#define	BINDINGLISTDELIM	':'
#define	MAX_DOMAIN_LENGTH	TEXTDOMAINMAX + 1 /* 256 + Null terminator */
#define	MAX_MSG			64

#define	LEAFINDICATOR		-99 /* must match with msgfmt.h */

struct domain_binding {
	char	*domain;	/* domain name */
	char	*binding;	/* binding directory */
	struct	domain_binding *next;
};

struct struct_mo_info {
	int	message_mid;		/* middle message id */
	int	message_count;		/* Total no. of messages */
	int	string_count_msgid;	/* total msgid length */
	int	string_count_msg;	/* total msgstr length */
	int	message_struct_size;	/* size of message_struct_size */
};

struct message_struct {
	int	less;		/* index of left leaf */
	int	more;		/* index of right leaf */
	int	msgid_offset;	/* msgid offset in mo file. */
	int	msg_offset;	/* msgstr offset in mo file */
};

struct message_so {
	char 	*path;			/* name of message shared object */
	int	fd;			/* file descriptor */
	struct struct_mo_info *mess_file_info;
					/* information of message file */
	struct message_struct *message_list;
					/* message list */
	char	*msg_ids;		/* actual message ids */
	char	*msgs;			/* actual messages */
};

/*
 * The category_to_name array is used in build_path.
 * The orderring of these names must correspond to the order
 * of the LC_* categories in locale.h.
 * i.e., category_to_name[LC_CTYPE] = "LC_CTYPE".
 */
const char *category_to_name[] =  {   /* must correspont to locale.h order */
	"LC_CTYPE",
	"LC_NUMERIC",
	"LC_TIME",
	"LC_COLLATE",
	"LC_MONETARY",
	"LC_MESSAGES",
	""		/* LC_ALL */
	};


/*
 * this structure is used for preserving nlspath templates before
 * passing them to bindtextdomain():
 */
static struct nlstmp {
	char	pathname[PATH_MAX + 1];	/* the full pathname to file	*/
	struct nlstmp *next;		/* head of the chain		*/
	struct nlstmp *last;		/* end of the chain		*/
} nlstmp;
static int	nlscount;		/* # of templates on this chain	*/


#ifndef __STDC__
#define	_textdomain textdomain
#define	_bindtextdomain bindtextdomain
#define	_dcgettext dcgettext
#define	_dgettext dgettext
#define	_gettext gettext
#ifndef const
#define	const
#endif /* const */
#endif /* __STDC__ */

char * _textdomain(const char *);
char * _bindtextdomain(const char *, const char *);
char * _dcgettext(const char *, const char *, const int);
char * _dgettext(const char *, const char *);
char * _gettext(const char *);

static char * _textdomain_u(const char *, char *);
static char * _bindtextdomain_u(const char *, const char *);
static char * dcgettext_u(const char *, const char *, const int);
static char *key_2_text(struct message_so *, const char *);
static int process_nlspath(const char *domain, const int category);
static char *replace_nls_option(char *s, char *name, char *pathname,
		char *locale, char *lang, char *territory, char *codeset);
static void build_path(char *buf, char *predicate, char *locale,
		const int category, char *domain);

static struct domain_binding *defaultbind = 0;
static struct domain_binding *firstbind = 0;

#ifdef _REENTRANT
static mutex_t gt_lock = DEFAULTMUTEX;
#endif _REENTRANT

#ifdef DEBUG
#define	ASSERT_LOCK(x)		assert(MUTEX_HELD(x))
#else
#define	ASSERT_LOCK(x)
#endif /* DEBUG */

/*
 * Build path.
 * This does simple insertion of the string representation of the
 * category and ".mo".
 * <predicate>/<locale>/<category_to_name[category]>/<domain>.mo
 */
static void
build_path(buf, predicate, locale, category, domain)
	char	*buf, *predicate, *locale, *domain;
{
	ASSERT_LOCK(&gt_lock);
	strcpy(buf, predicate);
	strcat(buf, "/");
	strcat(buf, locale);
	strcat(buf, "/");
	strcat(buf, category_to_name[category]);
	strcat(buf, "/");
	strcat(buf, domain);
	strcat(buf, ".mo");

} /* build_path */

/*
 * This builds initial default bindlist.
 */
static void
initbindinglist()
{
	ASSERT_LOCK(&gt_lock);
	defaultbind =
	(struct domain_binding *) malloc(sizeof (struct domain_binding));
	defaultbind->domain = strdup("");
	defaultbind->binding = strdup(DEFAULT_BINDING);
	defaultbind->next = 0;

} /* initbindinglist */

char *
_bindtextdomain(domain, binding)
	const char	*domain;
	const char	*binding;
{
	char *res;
	mutex_lock(&gt_lock);
	res = _bindtextdomain_u(domain, binding);
	mutex_unlock(&gt_lock);
	return (res);
}

static char *
_bindtextdomain_u(domain, binding)
	const char *domain;
	const char *binding;
{
	struct domain_binding	*bind, *prev;

	ASSERT_LOCK(&gt_lock);
	/*
	 * Initialize list
	 * If list is empty, create a default binding node.
	 */
	if (!defaultbind) {
		initbindinglist();
	}

	/*
	 * If domain is a NULL pointer, no change will occure regardless
	 * of binding value. Just return NULL.
	 */
	if (!domain) {
		return (NULL);
	}

	/*
	 * Global Binding is not supported any more.
	 * Just return NULL if domain is NULL string.
	 */
	if (*domain == '\0') {
		return (NULL);
	}

	/* linear search for binding, rebind if found, add if not */
	bind = firstbind;
	prev = 0;	/* Two pointers needed for pointer operations */

	while (bind) {
		if (strcmp(domain, bind->domain) == 0) {
			/*
			 * Domain found.
			 * If binding is NULL, then Query
			 */
			if (!binding) {
				return (bind->binding);
			}

			/* replace existing binding with new binding */
			if (bind->binding) {
				free(bind->binding);
			}
			bind->binding = strdup(binding);
#ifdef DEBUG
			printlist();
#endif
			return (bind->binding);
		}
		prev = bind;
		bind = bind->next;
	} /* while (bind) */

	/* domain has not been found in the list at this point */
	if (binding) {
		/*
		 * domain is not found, but binding is not NULL.
		 * Then add a new node to the end of linked list.
		 */
		bind = (struct domain_binding *)
			malloc(sizeof (struct domain_binding));
		bind->domain = strdup(domain);
		bind->binding = strdup(binding);
		bind->next = 0;
		if (prev) {
			/* reached the end of list */
			prev->next = bind;
		} else {
			/* list was empty */
			firstbind = bind;
		}
#ifdef DEBUG
		printlist();
#endif
		return (bind->binding);
	} else {
		/* Query of domain which is not found in the list */
		return (defaultbind->binding);
	} /* if (binding) */

	/* Must not reach here */

} /* _bindtextdomain_u */


/*
 * textdomain() sets or queries the name of the current domain of
 * the active LC_MESSAGES locale category.
 */

static char	current_domain[MAX_DOMAIN_LENGTH + 1] = DEFAULT_DOMAIN;

char *
_textdomain(domain)
	const char	*domain;
{
	char *res;

	_mutex_lock(&gt_lock);
	res = _textdomain_u(domain, current_domain);
	_mutex_unlock(&gt_lock);
	return (res);
}


static char *
_textdomain_u(domain, result)
	const char	*domain;
	char *result;
{

	ASSERT_LOCK(&gt_lock);
	/* Query is performed for NULL domain pointer */
	if (domain == NULL) {
		if (current_domain != result)
			strcpy(result, current_domain);
		return (result);
	}

	/* check for error. */
	if (strlen(domain) > (unsigned int) MAX_DOMAIN_LENGTH) {
		return (NULL);
	}

	/*
	 * Calling textdomain() with a null domain string sets
	 * the domain to the default domain.
	 * If non-null string is passwd, current domain is changed
	 * to the new domain.
	 */
	if (*domain == '\0') {
		strcpy(current_domain, DEFAULT_DOMAIN);
	} else {
		strcpy(current_domain, domain);
	}

	if (current_domain != result)
		strcpy(result, current_domain);
	return (result);
} /* textdomain */


/*
 * gettext() is a pass-thru to dcgettext() with a NULL pointer passed
 * for domain and LC_MESSAGES passed for category.
 */
char *
_gettext(msg_id)
	const char	*msg_id;
{
	char *return_str;

	mutex_lock(&gt_lock);
	return_str = dcgettext_u(NULL, msg_id, LC_MESSAGES);
	mutex_unlock(&gt_lock);
	return (return_str);
}


/*
 * In dcgettext() call, domain is valid only for this call.
 */
char *
_dgettext(domain, msg_id)
	const char	*domain;
	const char	*msg_id;
{
	char *res;
	mutex_lock(&gt_lock);
	res = dcgettext_u(domain, msg_id, LC_MESSAGES);
	mutex_unlock(&gt_lock);
	return (res);
}

char *
_dcgettext(domain, msg_id, category)
	const char	*domain;
	const char	*msg_id;
	const int	category;
{
	char *res;
	mutex_lock(&gt_lock);
	res = dcgettext_u(domain, msg_id, category);
	mutex_unlock(&gt_lock);
	return (res);
}



static char *
dcgettext_u(domain, msg_id, category)
	const char	*domain;
	const char	*msg_id;
	const int	category;
{
	char	msgfile[MAXPATHLEN];
	char	binding[MAXPATHLEN + 1], *bptr;
	char	mydomain[MAX_DOMAIN_LENGTH + 1];

	char	*cur_locale;
	char	*cur_domain;
	char	*orig_ptr; /* binding before process_nlspath() changes it */
	char	*orig_binding; /* binding before process_nlspath() changes it */
	char	*save_cur_binding; /* binding after calling process_nlspath() */
	char	*cur_binding;	/* points to current binding in list */

	struct stat	statbuf;
	int		fd = -1;
	caddr_t		addr;

	int		msg_inc;
	int		msg_count;
	int		path_found;
	int		errno_save = errno;
	char		*result;
	int		pnp; 		/* # nls paths to process	*/

	static int	top_list = 0;	/* top of message_so list */
	static int	first_free = 0;	/* first free entry in list */
	static int	last_entry_seen = -1;	/* try this one first */
	static struct message_so mess_so[MAX_MSG];
	static struct message_so cur_mess_so;	/* holds current message */

	ASSERT_LOCK(&gt_lock);

	cur_locale = setlocale(category, NULL);


	mydomain[0] = (char) NULL;

	/*
	 * Query the current domain if domain argument is NULL pointer
	 */
	if (domain == NULL) {
		cur_domain = _textdomain_u(NULL, mydomain);

	} else if (strlen(domain) > (unsigned int)MAX_DOMAIN_LENGTH) {
		errno = errno_save;
		return ((char *)msg_id);
	} else if (*domain == '\0') {
		cur_domain = DEFAULT_DOMAIN;
	} else {
		cur_domain = (char *)domain;
	}

	/*
	 * Spec1170 requires that we use NLSPATH if it's defined, to
	 * override any system default variables.  If NLSPATH is not
	 * defined or if a message catalog is not found in any of the
	 * components (bindings) specified by NLSPATH, dcgettext_u() will
	 * search for the message catalog in either a) the binding path set
	 * by any previous application calls to bindtextdomain() or
	 * b) the default binding path (/usr/lib/locale).  Save the original
	 * binding path so that we can search it if the message catalog
	 * is not found via NLSPATH.  The original binding is restored before
	 * returning from this routine because the gettext routines should
	 * not change the binding set by the application.  This allows
	 * bindtextdomain() to be called once for all gettext() calls in the
	 * application.
	 */
	orig_ptr = _bindtextdomain_u(cur_domain, NULL);
	/*
	 * copy orig_ptr because it could be changed when process_nlspath()
	 * calls _bindtextdomain_u()
	 */
	orig_binding = strdup(orig_ptr);

	/* if NLSPATH isn't useful, check for C locale:			*/
	if ((pnp = process_nlspath(cur_domain, category)) == 0) {
		/* If C locale, return the original msgid immediately. */
		if ((cur_locale[0] == 'C') && (cur_locale[1] == 0)) {
			errno = errno_save;
			/* restore binding changed by process_nlspath() */
			_bindtextdomain_u(cur_domain, orig_binding);
			free(orig_binding);
			return ((char *)msg_id);
		}
	}

	/*
	 * Query the current binding.
	 * If there is no binding, restore the original binding.
	 */
	if ((cur_binding = _bindtextdomain_u(cur_domain, NULL)) == NULL)
		cur_binding = orig_binding;
	/* Save cur_binding because it will be incremented. */
	save_cur_binding = cur_binding;

	/*
	 * The following while loop is entered whether or not
	 * NLSPATH is set, ie: cur_binding points to either NLSPATH
	 * or textdomain path.
	 * binding is the form of "bind1:bind2:bind3:"
	 */
	while (*cur_binding) {
		/* skip empty binding */
		while (*cur_binding == ':') {
			cur_binding++;
		}

		memset(binding, 0, sizeof (binding));
		bptr = binding;

		/* get binding */

		/*
		 * the previous version of this loop would leave
		 * cur_binding inc'd past the NULL character.
		 */
		while (*cur_binding != ':') {
			if ((*bptr = *cur_binding) == (char) NULL) {
				break;
			}
			bptr++;
			cur_binding++;
		}

		if (binding[0] == (char) NULL) {
			/*
			 * A message catalog wasn't found in any
			 * of the binding paths specified by cur_binding,
			 * and cur_binding now points to ":".
			 * If we haven't already done so, loop through
			 * the bindings specified by orig_binding
			 * to search for the message catalog in the
			 * textdomain path.
			 */
			if (strcmp(save_cur_binding, orig_binding))
				cur_binding = save_cur_binding = orig_binding;
			continue;
		}

		if (pnp-- > 0) { /* if we're looking at nlspath */
			/*
			 * process_nlspath() will already have built
			 * up an absolute pathname. So here, we're
			 * dealing with a binding that was in NLSPATH.
			 */
			strcpy(msgfile, binding);
		} else {
			/*
			 * Build textdomain path (regular locale), ie:
			 * <binding>/<locale>/<category_name>/<domain>.mo
			 * where <binding> could be a) set by a previous
			 * call by the application to bindtextdomain(), or
			 * b) the default binding "/usr/lib/locale".
			 * <domain> could be a) set by a previous call by the
			 * application to textdomain(), or b) the default
			 * domain "messages".
			 */
			if (binding[0]) {
				build_path(msgfile, binding, cur_locale,
				    category, cur_domain);
			} else {
				continue;
			}
		}

		/*
		 * At this point, msgfile contains full path for
		 * domain.
		 * Look up cache entry first. If cache misses,
		 * then search domain look-up table.
		 */
		path_found = 0;
		if ((last_entry_seen >= 0) &&
		(strcmp(msgfile, mess_so[last_entry_seen].path) == 0)) {
			path_found = 1;
			msg_inc = last_entry_seen;
		} else {
			msg_inc = top_list;
			while (msg_inc < first_free) {
			if (strcmp(msgfile, mess_so[msg_inc].path)
			    == 0) {
					path_found = 1;
					break;
				} /* if */
				msg_inc++;
			} /* while */
		} /* if */

		/*
		 * Even if msgfile was found in the table,
		 * It is not guaranteed to be a valid file.
		 * To be a valid file, fd must not be -1 and
		 * mmaped address (mess_file_info) must have
		 * valid contents.
		 */
		if (path_found) {
			last_entry_seen = msg_inc;
			if (mess_so[msg_inc].fd != -1 &&
			    mess_so[msg_inc].mess_file_info !=
			    (struct struct_mo_info *) - 1) {
				cur_mess_so = mess_so[msg_inc];
				/* file is valid */
				result = key_2_text(&cur_mess_so,
						msg_id);
				errno = errno_save;
				_bindtextdomain_u(cur_domain, orig_binding);
				free(orig_binding);
				return (result);
			} else {
				/* file is not valid */
				continue;
			}
		}

		/*
		 * Been though entire table and not found.
		 * Open a new entry if there is space.
		 */
		if (msg_inc == MAX_MSG) {
			/* not found, no more space */
			errno = errno_save;
			_bindtextdomain_u(cur_domain, orig_binding);
			free(orig_binding);
			return ((char *)msg_id);
		}
		if (first_free == MAX_MSG) {
			/* no more space */
			errno = errno_save;
			_bindtextdomain_u(cur_domain, orig_binding);
			free(orig_binding);
			return ((char *)msg_id);
		}

		/*
		 * There is an available entry in the table, so make
		 * a message_so for it and put it in the table,
		 * return msg_id if message file isn't opened -or-
		 * isn't mmap'd correctly
		 */
		fd = open(msgfile, O_RDONLY);

		mess_so[first_free].fd = fd;
		mess_so[first_free].path = strdup(msgfile);

		if (fd == -1) {
			/* unable to open message file */
			first_free++;
			continue;
		}
		fstat(fd, &statbuf);
		addr = mmap(0, statbuf.st_size, PROT_READ,
				MAP_SHARED, fd, 0);
		close(fd);

		mess_so[first_free].mess_file_info =
				(struct struct_mo_info *) addr;
		if (addr == (caddr_t) - 1) {
			first_free++;
			continue;
		}

		/* get message_list array start address */
		mess_so[first_free].message_list =
			(struct message_struct *)
			& mess_so[first_free].mess_file_info[1];

		/* find how many messages in file */
		msg_count = mess_so[first_free].mess_file_info->
						message_count;
		/* get msgid string pool start address */
		mess_so[first_free].msg_ids =
			(char *) &mess_so[first_free].
					message_list[msg_count];

		/* get msgstr string pool start address */
		mess_so[first_free].msgs = (char *)
			mess_so[first_free].msg_ids +
			mess_so[first_free].mess_file_info->
						string_count_msgid;

		cur_mess_so = mess_so[first_free];
		first_free++;

		result = key_2_text(&cur_mess_so, msg_id);
		errno = errno_save;
		_bindtextdomain_u(cur_domain, orig_binding);
		free(orig_binding);
		return (result);

	} /* while cur_binding */

	errno = errno_save;
	_bindtextdomain_u(cur_domain, orig_binding);
	free(orig_binding);
	return ((char *)msg_id);
} /* dcgettext_u */


/*
 * key_2_text() translates msd_id into target string.
 */
static char *
key_2_text(messages, key_string)
	struct message_so	*messages;
	const char		*key_string;
{
	register int		check;
	register int		val;
	register char		*msg_id_str;
	struct message_struct	check_mess_list;

	ASSERT_LOCK(&gt_lock);
	check = (*messages).mess_file_info->message_mid;
	for (;;) {
		check_mess_list = (*messages).message_list[check];
		msg_id_str = (*messages).msg_ids
				+ check_mess_list.msgid_offset;

		/*
		 * To maintain the compatibility with Zeus mo file,
		 * msg_id's are stored in descending order.
		 * If the ascending order is desired, change "msgfmt.c"
		 * and switch msg_id_str and key_string in the following
		 * strcmp() statement.
		 */
		val = strcmp(msg_id_str, key_string);
		if (val < 0) {
			if (check_mess_list.less == LEAFINDICATOR) {
				return ((char *)key_string);
			} else {
				check = check_mess_list.less;
			}
		} else if (val > 0) {
			if (check_mess_list.more == LEAFINDICATOR) {
				return ((char *)key_string);
			} else {
				check = check_mess_list.more;
			}
		} else {
			return ((*messages).msgs + check_mess_list.msg_offset);
		} /* if ((val= ...) */
	} /* for (;;) */

} /* key_2_string */


#ifdef DEBUG
printlist()
{
	struct domain_binding	*ppp;

	fprintf(stderr, "===Printing default list and regural list\n");
	fprintf(stderr, "   Default domain=<%s>, binding=<%s>\n",
		defaultbind->domain, defaultbind->binding);

	ppp = firstbind;
	while (ppp) {
		fprintf(stderr, "   domain=<%s>, binding=<%s>\n",
			ppp->domain, ppp->binding);
		ppp = ppp->next;
	}
}
#endif


/*
 * process_nlspath(): process the NLSPATH environment variable.
 *	output: # of paths in NLSPATH.
 *	description:
 *		this routine looks at NLSPATH in the environment,
 *		and will try to build up the binding list based
 *		on the settings of NLSPATH.
 */
static int
process_nlspath(domain, category)
	const char	*domain;
	const int	category;
{
	char	*name;			/* name of the file to open	*/
	char	*current_domain;	/* current domain (E.g. "messages") */
	char	*nlspath;		/* ptr to NLSPATH env variable	*/
	char 	*s;			/* generic string ptr		*/
	char	*territory;		/* our current territory element */
	char	*codeset;		/* our current codeset element	*/
	char	*local;			/* our current locale element	*/
	char	*lang;			/* our current language element	*/
	char	*current_locale;	/* what setlocale() tells us	*/
	char	*locale;		/* what setlocale() tells us	*/
	char	*s1;			/* for handling territory	*/
	char	*s2;			/* for handling codeset		*/
	int	errno_save = errno;	/* preserve errno		*/
	int	rv = 0;			/* our return value		*/
	char	pathname[PATH_MAX + 1];	/* the full pathname to the file */
	char	*ppaths;		/* ptr to all of the templates	*/
	struct nlstmp *pnlstmp;		/* ptr to current nls template	*/
	char	mydomain[MAX_DOMAIN_LENGTH + 1];

	/* Since setlocale() *can* fail, we better check */
	ASSERT_LOCK(&gt_lock);

	current_locale = setlocale(category, NULL);

	/*
	 * if NLSPATH isn't in the env, or if it is set to "", there's nothing
	 * further to do.
	 */
	nlspath = getenv("NLSPATH");
	if (nlspath == (char *) NULL || nlspath[0] == '\0') {
#ifdef __STDC__
		return ((int) 0);
#else
		return (0);
#endif
	}

	lang = (char *) NULL;
	territory = (char *) NULL;
	codeset = (char *) NULL;
	locale = setlocale(LC_MESSAGES, NULL);

	/*
	 * extract lang, territory and codeset from locale name
	 */
	if (locale) {
		lang = s = strdup(locale);
		s1 = s2 = NULL;
		while (s && *s) {
			if (*s == '_') {
				s1 = s;
				*s1++ = NULL;
			} else if (*s == '.') {
				s2 = s;
				*s2++ = NULL;
			}
			s++;
		}
		territory = s1;
		codeset   = s2;
	} /* if (locale) */

	mydomain[0] = (char) NULL;

	/*
	 * Now we get the domain (E.g. .../locale/category/domain).
	 * Query the current domain if domain argument is NULL pointer.
	 * Note that this is equivalent to the %N argument in
	 * NLSPATH for catopen().
	 */
	if (domain == NULL) {
		/* get the default domainname:				*/
		current_domain = _textdomain_u(NULL, mydomain);
	} else if (strlen(domain) > (unsigned int)MAX_DOMAIN_LENGTH) {
		/* if domain name is too long, give up:			*/
		errno = errno_save;
		if (lang) {
			free(lang);
		}
		return ((int) 0);
	} else if (*domain == '\0') {
		current_domain = DEFAULT_DOMAIN;
	} else {
		current_domain = (char *)domain;
	}

	name = current_domain;

	nlstmp.next = nlstmp.last = &nlstmp;	/* init our list	*/
	nlscount = 0;

	/*
	 * now that we have the name (domain), we first look through NLSPATH,
	 * in an attempt to get the locale. A locale may be completely
	 * specified as "language_territory.codeset". NLSPATH consists
	 * of templates separated by ":" characters. The following are
	 * the substitution values within NLSPATH:
	 *	%N = DEFAULT_DOMAIN
	 *	%L = The value of the LC_MESSAGES category.
	 *	%I = The language element from the LC_MESSAGES category.
	 *	%t = The territory element from the LC_MESSAGES category.
	 *	%c = The codeset element from the LC_MESSAGES category.
	 *	%% = A single character.
	 * if we find one of these characters, we will carry out the
	 * appropriate substitution.
	 */
	s = nlspath;
	while (*s) {				/* march through NLSPATH  */
		memset(pathname, 0, sizeof (pathname));
		if (*s == ':') {
			/*
			 * this loop only occurs if we have to replace
			 * ":" by "name". replace_nls_option() below
			 * will handle the subsequent ":"'s.
			 */
			pnlstmp = malloc(sizeof (struct nlstmp));
			strcpy(pnlstmp->pathname, name);

			pnlstmp->last = nlstmp.last;
			pnlstmp->next = nlstmp.last->next;
			nlstmp.last->next = pnlstmp;
			nlstmp.last = pnlstmp;
			nlscount++;

			++s;
			continue;
		}

		/* replace Substitution field */
		s = replace_nls_option(s, name, pathname, locale,
					lang, territory, codeset);

		/* if we've found a valid file:			*/
		if (pathname[0] != (char) NULL) {
			/* add template to end of chain of pathnames:	*/
			pnlstmp = malloc(sizeof (struct nlstmp));
			strcpy(pnlstmp->pathname, pathname);

			pnlstmp->last = nlstmp.last;
			pnlstmp->next = nlstmp.last->next;
			nlstmp.last->next = pnlstmp;
			nlstmp.last = pnlstmp;
			nlscount++;
		}


		if (*s)
			++s;
	}

	/*
	 * now that we've handled the pathname templates, concatenate them
	 * all into the form "template1:template2:..." for _bindtextdomain_u()
	 */

	if (nlscount > 0) {
		ppaths = malloc(nlscount * (PATH_MAX + 1));
		ppaths[0] = (char) NULL;
	}

	/*
	 * extract the path templates (fifo), and concatenate them
	 * all into a ":" separated string for _bindtextdomain_u()
	 */
	for (rv = nlscount; nlscount > 0; nlscount--) {
		pnlstmp = nlstmp.next;
		strcat(ppaths, pnlstmp->pathname);
		strcat(ppaths, ":");
		if (pnlstmp != &nlstmp) {
			nlstmp.next = pnlstmp->next;
			free(pnlstmp);
		} else {
			break;
		}
	}

	_bindtextdomain_u(domain, ppaths);

	if (lang) {
		free(lang);
	}

	if (ppaths)
		free(ppaths);

	return (rv);
}



/*
 * This routine will replace substitution parameters in NLSPATH
 * with appropiate values.
 */
static char *
replace_nls_option(s, name, pathname, locale, lang, territory, codeset)
	char	*s;		/* nlspath */
	char	*name;		/* name of catalog file. */
	char	*pathname;	/* To be returned. Expanded path name */
	char	*locale;	/* locale name */
	char	*lang;		/* language element  */
	char	*territory;	/* territory element */
	char	*codeset;	/* codeset element   */
{
	char	*t, *u;

	t = pathname;
	while (*s && *s != ':') {
		if (t < pathname + PATH_MAX) {
			/*
			 * %% is considered a single % character (XPG).
			 * %L : LC_MESSAGES (XPG4) LANG(XPG3)
			 * %l : The language element from the current locale.
			 *	(XPG3, XPG4)
			 */
			if (*s != '%')
				*t++ = *s;
			else if (*++s == 'N') {
				if (name) {
					u = name;
					while (*u && t < pathname + PATH_MAX)
						*t++ = *u++;
				}
			} else if (*s == 'L') {
				if (locale) {
					u = locale;
					while (*u && t < pathname + PATH_MAX)
						*t++ = *u++;
				}
			} else if (*s == 'l') {
				if (lang) {
					u = lang;
					while (*u && *u != '_' &&
						t < pathname + PATH_MAX)
						*t++ = *u++;
				}
			} else if (*s == 't') {
				if (territory) {
					u = territory;
					while (*u && *u != '.' &&
						t < pathname + PATH_MAX)
						*t++ = *u++;
				}
			} else if (*s == 'c') {
				if (codeset) {
					u = codeset;
					while (*u && t < pathname + PATH_MAX)
						*t++ = *u++;
				}
			} else {
				if (t < pathname + PATH_MAX)
					*t++ = *s;
			}
		}
		++s;
	}
	*t = NULL;
	return (s);
}
