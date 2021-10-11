/*
 *	nis_util.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_util.c	1.14	94/05/31 SMI"

#include <rpcsvc/nis.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pwd.h>
#include <shadow.h>

char *
nisname_index(s, c)
	char    *s;
	char	c;
{
	char	*d;
	int	in_quotes = FALSE,
		quote_quote = FALSE;

	if (!s)
		return (NULL);

	for (d = s; (in_quotes && (*d != '\0')) ||
			(!in_quotes && (*d != c) && (*d != '\0'));
			d++)
		if (quote_quote && in_quotes && (*d != '"')) {
			quote_quote = FALSE;
			in_quotes = FALSE;
			if (*d == c)
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
	if (*d != c)
		return (NULL);
	return (d);
}

/*
 * Parse a passed name into a basename and search criteria.
 * if there is no criteria present the *crit == 0. You must
 * pass in allocated data for the three strings.
 */
void
nisname_split(name, base, crit)
	char	*name;
	char	*base;
	char	*crit;
{
	register char	*p, *q;

	p = name;
	while (*p && (isspace(*p)))
		p++;
	if (*p != '[') {
		*crit = 0;
		strcpy(base, p);
		return;
	}

	/* it has a criteria, copy the whole thing in */
	strcpy(crit, p);
	q = nisname_index(crit, ']');
	if (! q) {
		*crit = 0;	/* error condition */
		*base = 0;
		return;
	}
	q++;
	if (*q == ',') {
		*q = 0;
		q++;
	}
	strcpy(base, q);
	*q = 0; /* just in case there wasn't a comma */
}

bool_t
nis_verifycred(n, flags)
	nis_name	n;
	u_long		flags;
{
	nis_result	*res;
	int		err;
	char		dname[NIS_MAXNAMELEN];

	sprintf(dname, "[cname=%s],cred.org_dir.%s", n, nis_domain_of(n));
	res = nis_list(dname, flags, NULL, NULL);
	err = (res->status == NIS_SUCCESS);
	nis_freeresult(res);
	return (err);
}

#define	NIS_ALL_ACC (NIS_READ_ACC|NIS_MODIFY_ACC|NIS_CREATE_ACC|NIS_DESTROY_ACC)

int
parse_rights_field(rights, shift, p)
	u_long *rights;
	int shift;
	char *p;
{
	int set;

	while (*p && (*p != ',')) {
		switch (*p) {
		case '=':
			*rights &= ~(NIS_ALL_ACC << shift);
		case '+':
			set = 1;
			break;
		case '-':
			set = 0;
			break;
		default:
			return (0);
		}
		for (p++; *p && (*p != ',') && (*p != '=') && (*p != '+') &&
							(*p != '-'); p++) {
			switch (*p) {
			case 'r':
				if (set)
					*rights |= (NIS_READ_ACC << shift);
				else
					*rights &= ~(NIS_READ_ACC << shift);
				break;
			case 'm':
				if (set)
					*rights |= (NIS_MODIFY_ACC << shift);
				else
					*rights &= ~(NIS_MODIFY_ACC << shift);
				break;
			case 'c':
				if (set)
					*rights |= (NIS_CREATE_ACC << shift);
				else
					*rights &= ~(NIS_CREATE_ACC << shift);
				break;
			case 'd':
				if (set)
					*rights |= (NIS_DESTROY_ACC << shift);
				else
					*rights &= ~(NIS_DESTROY_ACC << shift);
				break;
			default:
				return (0);
			}
		}
	}
	return (1);
}

#define	NIS_NOBODY_FLD 1
#define	NIS_OWNER_FLD 2
#define	NIS_GROUP_FLD 4
#define	NIS_WORLD_FLD 8
#define	NIS_ALL_FLD NIS_OWNER_FLD|NIS_GROUP_FLD|NIS_WORLD_FLD

int
parse_rights(rights, p)
	u_long *rights;
	char *p;
{
	u_long f;

	if (p)
		while (*p) {
			for (f = 0; (*p != '=') && (*p != '+') && (*p != '-');
									p++)
				switch (*p) {
				case 'n':
					f |= NIS_NOBODY_FLD;
					break;
				case 'o':
					f |= NIS_OWNER_FLD;
					break;
				case 'g':
					f |= NIS_GROUP_FLD;
					break;
				case 'w':
					f |= NIS_WORLD_FLD;
					break;
				case 'a':
					f |= NIS_ALL_FLD;
					break;
				default:
					return (0);
				}
			if (f == 0)
				f = NIS_ALL_FLD;

			if ((f & NIS_NOBODY_FLD) &&
			    !parse_rights_field(rights, 24, p))
				return (0);

			if ((f & NIS_OWNER_FLD) &&
			    !parse_rights_field(rights, 16, p))
				return (0);

			if ((f & NIS_GROUP_FLD) &&
			    !parse_rights_field(rights, 8, p))
				return (0);

			if ((f & NIS_WORLD_FLD) &&
			    !parse_rights_field(rights, 0, p))
				return (0);

			while (*(++p))
				if (*p == ',') {
					p++;
					break;
				}
		}
	return (1);
}


int
parse_flags(flags, p)
	u_long *flags;
	char *p;
{
	if (p) {
		while (*p) {
			switch (*(p++)) {
			case 'B':
				*flags |= TA_BINARY;
				break;
			case 'X':
				*flags |= TA_XDR;
				break;
			case 'S':
				*flags |= TA_SEARCHABLE;
				break;
			case 'I':
				*flags |= TA_CASE;
				break;
			case 'C':
				*flags |= TA_CRYPT;
				break;
			default:
				return (0);
			}
		}
		return (1);
	} else {
		fprintf(stderr,
	"Invalid table schema: At least one column must be searchable.\n");
		exit(1);
	}
}


int
parse_time(time, p)
	u_long *time;
	char *p;
{
	char *s;
	u_long x;

	*time = 0;

	if (p)
		while (*p) {
			if (!isdigit(*p))
				return (0);
			x = strtol(p, &s, 10);
			switch (*s) {
			case '\0':
				(*time) += x;
				p = s;
				break;
			case 's':
			case 'S':
				(*time) += x;
				p = s+1;
				break;
			case 'm':
			case 'M':
				(*time) += x*60;
				p = s+1;
				break;
			case 'h':
			case 'H':
				(*time) += x*(60*60);
				p = s+1;
				break;
			case 'd':
			case 'D':
				(*time) += x*(24*60*60);
				p = s+1;
				break;
			default:
				return (0);
			}
		}

	return (1);
}


int
nis_getsubopt(optionsp, tokens, sep, valuep)
	char **optionsp;
	char * const *tokens;
	const int sep; /* if this is a char we get an alignment error */
	char **valuep;
{
	register char *s = *optionsp, *p, *q;
	register int i, optlen;

	*valuep = NULL;
	if (*s == '\0')
		return (-1);
	q = strchr(s, (char)sep);	/* find next option */
	if (q == NULL) {
		q = s + strlen(s);
	} else {
		*q++ = '\0';		/* mark end and point to next */
	}
	p = strchr(s, '=');		/* find value */
	if (p == NULL) {
		optlen = strlen(s);
		*valuep = NULL;
	} else {
		optlen = p - s;
		*valuep = ++p;
	}
	for (i = 0; tokens[i] != NULL; i++) {
		if ((optlen == strlen(tokens[i])) &&
		    (strncmp(s, tokens[i], optlen) == 0)) {
			/* point to next option only if success */
			*optionsp = q;
			return (i);
		}
	}
	/* no match, point value at option and return error */
	*valuep = s;
	return (-1);
}


nis_object nis_default_obj;

static char *nis_defaults_tokens[] = {
	"owner",
	"group",
	"access",
	"ttl",
	0
};

#define	T_OWNER 0
#define	T_GROUP 1
#define	T_ACCESS 2
#define	T_TTL 3

int
nis_defaults_set(optstr)
	char *optstr;
{
	char str[1024], *p, *v;
	int i;

	strcpy(str, optstr);
	p = str;

	while ((i = nis_getsubopt(&p, nis_defaults_tokens, ':', &v)) != -1) {
		switch (i) {
		case T_OWNER:
			if (v == 0 || v[strlen(v)-1] != '.')
				return (0);
			nis_default_obj.zo_owner = strdup(v);
			break;
		case T_GROUP:
			if (v == 0 || v[strlen(v)-1] != '.')
				return (0);
			nis_default_obj.zo_group = strdup(v);
			break;
		case T_ACCESS:
			if ((v == 0) ||
			    (!parse_rights(&(nis_default_obj.zo_access), v)))
				return (0);
			break;
		case T_TTL:
			if ((v == 0) ||
			    !(parse_time(&(nis_default_obj.zo_ttl), v)))
				return (0);
			break;
		}
	}

	if (*p)
		return (0);

	return (1);
}

extern char *getenv();

int
nis_defaults_init(optstr)
	char *optstr;
{
	char *envstr;

	/* XXX calling this multiple times may leak memory */
	memset((char *)&nis_default_obj, 0, sizeof (nis_default_obj));

	nis_default_obj.zo_owner = nis_local_principal();
	nis_default_obj.zo_group = nis_local_group();
	nis_default_obj.zo_access = DEFAULT_RIGHTS;
	nis_default_obj.zo_ttl = 12 * 60 * 60;

	if (envstr = getenv("NIS_DEFAULTS"))
		if (!nis_defaults_set(envstr)) {
			fprintf(stderr,
			"can't parse NIS_DEFAULTS environment variable.\n");
			return (0);
		}

	if (optstr)
		if (!nis_defaults_set(optstr)) {
			fprintf(stderr, "can't parse nis_defaults argument.\n");
			return (0);
		}

	return (1);
}



/*
 * Converts an NIS+ entry object for a passwd table to its
 * pwent structure.
 * XXX: This function returns a pointer to a static structure.
 */
static struct passwd *
nis_object_to_pwent(obj, error)
	nis_object	*obj;
	nis_error	*error;
{
	static struct passwd	pw;
	static char	spacebuf[1024]; /* The pwent structure points to this */
	static char	nullstring;	/* used for NULL data */
	char		*tmp;

	memset((void *)&pw, 0, sizeof (struct passwd));
	memset((void *)&spacebuf[0], 0, 1024);
	tmp = &spacebuf[0];

	if ((obj->zo_data.zo_type != ENTRY_OBJ) ||
	    (obj->EN_data.en_cols.en_cols_len < 8)) {
		*error = NIS_INVALIDOBJ;
		return (NULL);
	}
	if (ENTRY_LEN(obj, 0) == 0) {
		*error = NIS_INVALIDOBJ;
		return (NULL);
	} else {
		strcpy(tmp, ENTRY_VAL(obj, 0));
		pw.pw_name = tmp;
		tmp += strlen(pw.pw_name) + 1;
	}

	if (ENTRY_LEN(obj, 1) == 0) {
		pw.pw_passwd = &nullstring;
	} else {
		/* XXX: Should I be returning X here? */
		strcpy(tmp, ENTRY_VAL(obj, 1));
		pw.pw_passwd = tmp;
		tmp += strlen(pw.pw_passwd) + 1;
	}

	if (ENTRY_LEN(obj, 2) == 0) {
		*error = NIS_INVALIDOBJ;
		return (NULL);
	}
	pw.pw_uid = atoi(ENTRY_VAL(obj, 2));

	if (ENTRY_LEN(obj, 3) == 0)
		pw.pw_gid = 0; /* Is this default value? */
	else
		pw.pw_gid = atoi(ENTRY_VAL(obj, 3));

	if (ENTRY_LEN(obj, 4) == 0) {
		pw.pw_gecos = &nullstring;
	} else {
		strcpy(tmp, ENTRY_VAL(obj, 4));
		pw.pw_gecos = tmp;
		tmp += strlen(pw.pw_gecos) + 1;
	}

	if (ENTRY_LEN(obj, 5) == 0) {
		pw.pw_dir = &nullstring;
	} else {
		strcpy(tmp, ENTRY_VAL(obj, 5));
		pw.pw_dir = tmp;
		tmp += strlen(pw.pw_dir) + 1;
	}

	if (ENTRY_LEN(obj, 6) == 0) {
		pw.pw_shell = &nullstring;
	} else {
		strcpy(tmp, ENTRY_VAL(obj, 6));
		pw.pw_shell = tmp;
		tmp += strlen(pw.pw_shell) + 1;
	}

	pw.pw_age = &nullstring;
	pw.pw_comment = &nullstring;
	*error = NIS_SUCCESS;
	return (&pw);
}


/*
 * This will go to the NIS+ master to get the data.  This code
 * is ugly because the internals of the switch had to be opened
 * up here.  Wish there was a way to pass a MASTER_ONLY flag
 * to getpwuid() and all such getXbyY() calls.  Some of this code
 * is being copied from the NIS+ switch backend.
 *
 * XXX: We will not bother to make this MT-safe.  If any of the callers
 * for this function want to use getpwuid_r(), then a corresponding
 * function will have to written.
 */
struct passwd *
getpwuid_nisplus_master(domain, uid, error)
	char *domain;
	uid_t	uid;
	nis_error *error;
{
	struct passwd	*passwd_ent;
	nis_result	*res;
	char		namebuf[NIS_MAXNAMELEN];
	u_long		flags;

	sprintf(namebuf, "[uid=%d],passwd.org_dir.%s", uid, domain);
	flags = EXPAND_NAME|FOLLOW_LINKS|FOLLOW_PATH|USE_DGRAM|MASTER_ONLY;
	res = nis_list(namebuf, flags, 0, 0);
	if (res == NULL) {
		*error = NIS_NOMEMORY;
		return (NULL);
	}
	if (res->status != NIS_SUCCESS) {
		nis_freeresult(res);
		*error = res->status;
		return (NULL);
	}
	if (NIS_RES_NUMOBJ(res) == 0) {
		nis_freeresult(res);
		*error = NIS_NOTFOUND;
		return (NULL);
	}

	passwd_ent = nis_object_to_pwent(NIS_RES_OBJECT(res), error);
	nis_freeresult(res);
	return (passwd_ent);
}

/*
 * Converts an NIS+ entry object for a shadow table to its
 * spwent structure.  We only fill in the sp_namp and sp_pwdp fields.
 * XXX: This function returns a pointer to a static structure.
 */
static struct spwd *
nis_object_to_spwent(obj, error)
	nis_object	*obj;
	nis_error	*error;
{
	static struct spwd	spw;
	static char	spacebuf[1024]; /* The pwent structure points to this */
	static char	nullstring;	/* used for NULL data */
	char		*tmp;

	memset((void *)&spw, 0, sizeof (struct spwd));
	memset((void *)&spacebuf[0], 0, 1024);
	tmp = &spacebuf[0];

	if ((obj->zo_data.zo_type != ENTRY_OBJ) ||
	    (obj->EN_data.en_cols.en_cols_len < 8)) {
		*error = NIS_INVALIDOBJ;
		return (NULL);
	}
	if (ENTRY_LEN(obj, 0) == 0) {
		*error = NIS_INVALIDOBJ;
		return (NULL);
	} else {
		strcpy(tmp, ENTRY_VAL(obj, 0));
		spw.sp_namp = tmp;
		tmp += strlen(spw.sp_namp) + 1;
	}

	if (ENTRY_LEN(obj, 1) == 0) {
		spw.sp_pwdp = &nullstring;
	} else {
		strcpy(tmp, ENTRY_VAL(obj, 1));
		spw.sp_pwdp = tmp;
		tmp += strlen(spw.sp_pwdp) + 1;
	}

	*error = NIS_SUCCESS;
	return (&spw);
}


/*
 * This will go to the NIS+ master to get the data.  This code
 * is ugly because the internals of the switch had to be opened
 * up here.  Wish there was a way to pass a MASTER_ONLY flag
 * to getspnam() and all such getXbyY() calls.  Some of this code
 * is being copied from the NIS+ switch backend.
 *
 * XXX: We will not bother to make this MT-safe.  If any of the callers
 * for this function want to use getpwuid_r(), then a corresponding
 * function will have to written.
 */
struct spwd *
getspnam_nisplus_master(domain, name, error)
	char *domain;
	char *name;
	nis_error *error;
{
	struct spwd	*shadow_ent;
	nis_result	*res;
	char		namebuf[NIS_MAXNAMELEN];
	u_long		flags;

	sprintf(namebuf, "[name=%s],passwd.org_dir.%s", name, domain);
	flags = EXPAND_NAME|FOLLOW_LINKS|FOLLOW_PATH|USE_DGRAM|MASTER_ONLY;
	res = nis_list(namebuf, flags, 0, 0);
	if (res == NULL) {
		*error = NIS_NOMEMORY;
		return (NULL);
	}
	if (res->status != NIS_SUCCESS) {
		nis_freeresult(res);
		*error = res->status;
		return (NULL);
	}
	if (NIS_RES_NUMOBJ(res) == 0) {
		nis_freeresult(res);
		*error = NIS_NOTFOUND;
		return (NULL);
	}

	shadow_ent = nis_object_to_spwent(NIS_RES_OBJECT(res), error);
	nis_freeresult(res);
	return (shadow_ent);
}
