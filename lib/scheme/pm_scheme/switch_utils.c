
/*
 * Copyright (c) 1985 by Sun Microsystems, Inc.
 */

#ident  "@(#)switch_utils.c 1.6     94/09/16 SMI"

#include "pam_headers.h"

static void	pr_config();

/*
 * The following is similar to getpwnam() except that it specifies
 * where to get the information. This is modeled after getpwnam_r().
 */
static void
nss_nis_passwd(p)
	nss_db_params_t	*p;
{
	p->name = NSS_DBNAM_PASSWD;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = "nis";
}

static void
nss_nis_shadow(p)
	nss_db_params_t	*p;
{
	p->name = NSS_DBNAM_SHADOW;
	p->config_name    = NSS_DBNAM_PASSWD;	/* Use config for "passwd" */
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = "nis";
}

static void
nss_nisplus_passwd(p)
	nss_db_params_t	*p;
{
	p->name = NSS_DBNAM_PASSWD;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = "nisplus";
}

static void
nss_nisplus_shadow(p)
	nss_db_params_t	*p;
{
	p->name = NSS_DBNAM_SHADOW;
	p->config_name    = NSS_DBNAM_PASSWD;	/* Use config for "passwd" */
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = "nisplus";
}

static char *
gettok(nextpp)
	char	**nextpp;
{
	char	*p = *nextpp;
	char	*q = p;
	char	c;

	if (p == 0) {
		return (0);
	}
	while ((c = *q) != '\0' && c != ':') {
		q++;
	}
	if (c == '\0') {
		*nextpp = 0;
	} else {
		*q++ = '\0';
		*nextpp = q;
	}
	return (p);
}

/*
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be.
 */
static int
str2passwd(instr, lenstr, ent, buffer, buflen)
	const char	*instr;
	int		lenstr;
	void	*ent;
	char	*buffer;
	int	buflen;
{
	struct passwd	*passwd	= (struct passwd *)ent;
	char		*p, *next;
	int		black_magic;	/* "+" or "-" entry */

	if (lenstr + 1 > buflen) {
		return (NSS_STR_PARSE_ERANGE);
	}
	/*
	 * We copy the input string into the output buffer and
	 * operate on it in place.
	 */
	(void) memcpy(buffer, instr, lenstr);
	buffer[lenstr] = '\0';

	next = buffer;

	passwd->pw_name = p = gettok(&next);		/* username */
	if (*p == '\0') {
		/* Empty username;  not allowed */
		return (NSS_STR_PARSE_PARSE);
	}
	black_magic = (*p == '+' || *p == '-');
	if (black_magic) {
		/* Then the rest of the passwd entry is optional */
		passwd->pw_passwd  = 0;
		passwd->pw_uid	= UID_NOBODY;
		passwd->pw_gid	= GID_NOBODY;
		passwd->pw_age	= 0;
		passwd->pw_comment = 0;
		passwd->pw_gecos = 0;
		passwd->pw_dir	= 0;
		passwd->pw_shell = 0;
	}

	passwd->pw_passwd = p = gettok(&next);		/* password */
	if (p == 0) {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}
	for (; *p != '\0'; p++) {			/* age */
		if (*p == ',') {
			*p++ = '\0';
			break;
		}
	}
	passwd->pw_age = p;

	p = next;					/* uid */
	if (p == 0 || *p == '\0') {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}
	if (!black_magic) {
		passwd->pw_uid = strtol(p, &next, 10);
		if (next == p) {
			/* uid field should be nonempty */
			return (NSS_STR_PARSE_PARSE);
		}
		if (passwd->pw_uid < 0 || passwd->pw_uid > MAXUID) {
			/*
			 * ==== Previous code did this, but is it
			 * really a good idea?  What about pw_gid
			 * and gr_gid fields?  (There is no definition
			 * for MAXGID).
			 */
			passwd->pw_uid = UID_NOBODY;
		}
	}
	if (*next++ != ':') {
		/* Parse error, even for a '+' entry (which should have	*/
		/*   an empty uid field, since it's always overridden)	*/
		return (NSS_STR_PARSE_PARSE);
	}

	p = next;					/* gid */
	if (p == 0 || *p == '\0') {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}
	if (!black_magic) {
		passwd->pw_gid = strtol(p, &next, 10);
		if (next == p) {
			/* gid field should be nonempty */
			return (NSS_STR_PARSE_PARSE);
		}
	}
	if (*next++ != ':') {
		/* Parse error, even for a '+' entry (which should have	*/
		/*   an empty gid field, since it's always overridden)	*/
		return (NSS_STR_PARSE_PARSE);
	}

	passwd->pw_gecos = passwd->pw_comment = p = gettok(&next);
	if (p == 0) {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}

	passwd->pw_dir = p = gettok(&next);
	if (p == 0) {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}

	passwd->pw_shell = p = gettok(&next);
	if (p == 0) {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}

	/* Better not be any more fields... */
	if (next == 0) {
		/* Successfully parsed and stored */
		return (NSS_STR_PARSE_SUCCESS);
	}
	return (NSS_STR_PARSE_PARSE);
}

typedef const char *constp;

static bool_t	/* 1 means success and more input, 0 means error or no more */
getfield(nextp, limit, uns, valp)
	constp		*nextp;
	constp		limit;
	int		uns;
	void		*valp;
{
	constp		p = *nextp;
	char		*endfield;
	char		numbuf[12];  /* Holds -2^31 and trailing \0 */
	int		len;

	if (p >= limit) {
		return (0);
	}
	if (*p == ':') {
		p++;
		*nextp = p;
		return (p < limit);
	}
	if ((len = limit - p) > sizeof (numbuf) - 1) {
		len = sizeof (numbuf) - 1;
	}
	/*
	 * We want to use strtol() and we have a readonly non-zero-terminated
	 *   string, so first we copy and terminate the interesting bit.
	 *   Ugh.  (It's convenient to terminate with a colon rather than \0).
	 */
	if ((endfield = memccpy(numbuf, p, ':', len)) == 0) {
		if (len != limit - p) {
			/* Error -- field is too big to be a legit number */
			return (0);
		}
		numbuf[len] = ':';
		p = limit;
	} else {
		p += (endfield - numbuf);
	}
	if (uns) {
		*((unsigned long *)valp) = strtoul(numbuf, &endfield, 10);
	} else {
		*((long *)valp) = strtol(numbuf, &endfield, 10);
	}
	if (*endfield != ':') {
		/* Error -- expected <integer><colon>, got something else */
		return (0);
	}
	*nextp = p;
	return (p < limit);
}

/*
 *  str2spwd() -- convert a string to a shadow passwd entry.  The parser is
 *	more liberal than the passwd or group parsers;  since it's legitimate
 *	for almost all the fields here to be blank, the parser lets one omit
 *	any number of blank fields at the end of the entry.  The acceptable
 *	forms for '+' and '-' entries are the same as those for normal entries.
 *  === Is this likely to do more harm than good?
 *
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be. instring and buffer should be separate areas.
 */
int
str2spwd(instr, lenstr, ent, buffer, buflen)
	const char	*instr;
	int		lenstr;
	void	*ent; /* really (struct spwd *) */
	char	*buffer;
	int	buflen;
{
	struct spwd	*shadow	= (struct spwd *)ent;
	const char	*p = instr, *limit;
	char		*bufp;
	int	lencopy, black_magic;

	limit = p + lenstr;
	if ((p = memchr(instr, ':', lenstr)) == 0 ||
		++p >= limit ||
		(p = memchr(p, ':', limit - p)) == 0) {
		lencopy = lenstr;
		p = 0;
	} else {
		lencopy = p - instr;
		p++;
	}
	if (lencopy + 1 > buflen) {
		return (NSS_STR_PARSE_ERANGE);
	}
	(void) memcpy(buffer, instr, lencopy);
	buffer[lencopy] = 0;

	black_magic = (*instr == '+' || *instr == '-');
	shadow->sp_namp = bufp = buffer;
	shadow->sp_pwdp	= 0;
	shadow->sp_lstchg = -1;
	shadow->sp_min	= -1;
	shadow->sp_max	= -1;
	shadow->sp_warn	= -1;
	shadow->sp_inact = -1;
	shadow->sp_expire = -1;
	shadow->sp_flag	= 0;

	if ((bufp = strchr(bufp, ':')) == 0) {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}
	*bufp++ = '\0';

	shadow->sp_pwdp = bufp;
	if (instr == 0) {
		if ((bufp = strchr(bufp, ':')) == 0) {
			if (black_magic)
				return (NSS_STR_PARSE_SUCCESS);
			else
				return (NSS_STR_PARSE_PARSE);
		}
		*bufp++ = '\0';
		p = bufp;
	} /* else p was set when we copied name and passwd into the buffer */

	if (!getfield(&p, limit, 0, &shadow->sp_lstchg))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 0, &shadow->sp_min))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 0, &shadow->sp_max))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 0, &shadow->sp_warn))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 0, &shadow->sp_inact))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 0, &shadow->sp_expire))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 1, &shadow->sp_flag))
			return (NSS_STR_PARSE_SUCCESS);
	if (p != limit) {
		/* Syntax error -- garbage at end of line */
		return (NSS_STR_PARSE_PARSE);
	}
	return (NSS_STR_PARSE_SUCCESS);
}

static nss_XbyY_buf_t *buffer;
static DEFINE_NSS_DB_ROOT(db_root);

#define	GETBUF()        \
	NSS_XbyY_ALLOC(&buffer, sizeof (struct passwd), NSS_BUFLEN_PASSWD)

struct passwd *
getpwnam_from(name, rep)
	const char	*name;
	int		rep;
{
	nss_XbyY_buf_t  *b = GETBUF();
	nss_XbyY_args_t arg;

	if (b == 0)
		return (0);

	NSS_XbyY_INIT(&arg, b->result, b->buffer, b->buflen, str2passwd);
	arg.key.name = name;
	if (rep == R_NISPLUS)
		nss_search(&db_root, nss_nisplus_passwd, NSS_DBOP_PASSWD_BYNAME,
		    &arg);
	else if (rep == R_NIS)
		nss_search(&db_root, nss_nis_passwd, NSS_DBOP_PASSWD_BYNAME,
		    &arg);
	else
		return (NULL);

	return (struct passwd *) NSS_XbyY_FINI(&arg);
}

static nss_XbyY_buf_t *spbuf;
static DEFINE_NSS_DB_ROOT(spdb_root);

#define	GETSPBUF()        \
	NSS_XbyY_ALLOC(&spbuf, sizeof (struct spwd), NSS_BUFLEN_SHADOW)

struct spwd *
getspnam_from(name, rep)
	const char	*name;
	int		rep;
{
	nss_XbyY_buf_t  *b = GETSPBUF();
	nss_XbyY_args_t arg;

	if (b == 0)
		return (0);

	NSS_XbyY_INIT(&arg, b->result, b->buffer, b->buflen, str2spwd);
	arg.key.name = name;
	if (rep == R_NISPLUS)
		nss_search(&spdb_root, nss_nisplus_shadow,
		    NSS_DBOP_SHADOW_BYNAME, &arg);
	else if (rep == R_NIS)
		nss_search(&spdb_root, nss_nis_shadow,
		    NSS_DBOP_SHADOW_BYNAME, &arg);
	else
		return (NULL);
	return (struct spwd *) NSS_XbyY_FINI(&arg);
}

static void
pr_config(ia_convp)
	struct ia_conv *ia_convp;
{
	char 		messages[MAX_NUM_MSG][MAX_MSG_SIZE];

	sprintf(messages[0],
	"Supported configurations for passwd management are as follows:\n");
	sprintf(messages[1], "\tpasswd: files\n");
	sprintf(messages[2], "\tpasswd: files nis\n");
	sprintf(messages[3], "\tpasswd: files nisplus\n");
	sprintf(messages[4], "\tpasswd: compat\n");
	sprintf(messages[5], "\tpasswd: compat AND\n");
	sprintf(messages[6], "\tpasswd_compat: nisplus\n");
	sprintf(messages[7], "Please check your /etc/nsswitch.conf file\n");

	/* display the above 8 messages */
	(void) display_errmsg(ia_convp->start_conv, 0, 8,
	    messages, NULL);
}

/*
 * get name services (or repositories) of passwd.
 * o_rep: the specified respository in command line. If no repository is
 *	specified in the command line, o_rep is	equal to R_DEFAULT.
 * return value: new repositories
 *	1. In the case of R_DEFAULT, new repositories are from nsswitch
 *	   file (as long as it represents a valid and supported configuration).
 *	2. In the case of specified repository, it should be present as one
 *	   of the valid services (or repositories) in nsswitch file.
 *	   A warning is printed if this happens. Operation is continued.
 */
int
get_ns(o_rep, ia_convp)
	int		o_rep;
	struct ia_conv	*ia_convp;
{
	static struct __nsw_switchconfig *conf = NULL;
	static struct __nsw_switchconfig *confcomp = NULL;
	enum __nsw_parse_err pserr;
	struct __nsw_lookup *lkp;
	struct __nsw_lookup *lkp2;
	int	rep = 0;

	/* yppasswd/nispasswd doesn't care about nsswitch file */
	if (IS_OPWCMD(o_rep))
		return (o_rep);

	if (conf == NULL) {
		conf = __nsw_getconfig("passwd", &pserr);
		if (conf == NULL) {
			(void) fprintf(stderr, gettext(
			    "Can't find name service for passwd\n"));
			if (IS_NISPLUS(o_rep)) {
				(void) fprintf(stderr, gettext(
				    "You may not use nisplus repository\n"));
				return (-1);
			} else if (o_rep != R_DEFAULT)
				return (o_rep);
			else {
				rep = R_FILES | R_NIS;
				return (rep);	/* default */
			}
		}
	}

	dprintf1("number of services is %d\n", conf->num_lookups);
	lkp = conf->lookups;
	if (conf->num_lookups > 2) {
		pr_config(ia_convp);
		return (-1);
	} else if (conf->num_lookups == 1) {
		/* files or compat */
		if (strcmp(lkp->service_name, "files") == 0) {
			rep |= R_FILES;
			if (o_rep == R_NIS || o_rep == R_NISPLUS) {
				(void) fprintf(stderr, gettext(
	"Your specified repository is not defined in the nsswitch file!\n"));
				return (o_rep);
			}
			return (rep);
		} else if (strcmp(lkp->service_name, "compat") == 0) {
			/* get passwd_compat */
			confcomp = __nsw_getconfig("passwd_compat", &pserr);
			if (confcomp == NULL) {
				rep = R_FILES | R_NIS;
				if (o_rep == R_NISPLUS) {
					(void) fprintf(stderr, gettext(
	"Your specified repository is not defined in the nsswitch file!\n"));
					return (o_rep);
				} else if (o_rep != R_DEFAULT)
					return (o_rep);
				else
					return (rep);
			} else {
				/* check the service: nisplus? */
				if (strcmp(confcomp->lookups->service_name,
				    "nisplus") == 0) {
					rep = R_FILES | R_NISPLUS;
					if (o_rep == R_NIS) {
						(void) fprintf(stderr, gettext(
	"Your specified repository is not defined in the nsswitch file!\n"));
						return (o_rep);
					} else if (o_rep != R_DEFAULT)
						return (o_rep);
					else
						return (rep);
				} else {
					/* passwd_compat must be nisplus?? */
					return (-1);
				}
			}
		} else {
			pr_config(ia_convp);
			return (-1);
		}
	} else  { /* two services */
		lkp = conf->lookups;
		lkp2 = lkp->next;
		if (strcmp(lkp->service_name, "files") == 0) {
			/* files nis, or files nisplus */
			rep |= R_FILES;
			/* continue */
		} else {
			pr_config(ia_convp);
			return (-1);
		}
		if (strcmp(lkp2->service_name, "nis") == 0) {
			rep |= R_NIS;
			if (o_rep == R_NISPLUS) {
				(void) fprintf(stderr, gettext(
	"Your specified repository is not defined in the nsswitch file!\n"));
				return (o_rep);
			} else if (o_rep != R_DEFAULT)
				return (o_rep);
			else
				return (rep);
		} else if (strcmp(lkp2->service_name, "nisplus") == 0) {
			rep |= R_NISPLUS;
			if (o_rep == R_NIS) {
				(void) fprintf(stderr, gettext(
	"Your specified repository is not defined in the nsswitch file!\n"));
				return (o_rep);
			} else if (o_rep != R_DEFAULT)
				return (o_rep);
			else
				return (rep);
		} else {
			pr_config(ia_convp);
			return (-1);
		}
	}
}
