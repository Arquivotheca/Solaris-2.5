
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_get_authtokattr.c 1.12     94/05/23 SMI"

#include	"unix_headers.h"

/*
 * sa_get_authtokattr():
 *	To get authentication token attribute values.
 *
 * 	This function calls ck_perm() first to check the caller's
 * 	permission.  If the check succeeds, it will read the
 * 	attribute/value pairs from the shadow password entry of
 *	the user specified by the authentication handle "iah"
 * 	and store them into a character array and return.
 */

#define	PRT_PWD(pwdp)	{\
	if (*pwdp == NULL) \
		(void) fprintf(stderr, "NP  "); \
	else if ((int)strlen(pwdp) < NUMCP) \
		(void) fprintf(stderr, "LK  "); \
	else\
		(void) fprintf(stderr, "PS  "); \
}

#define	PRT_AGE(sp)	{\
	if (sp->sp_max != -1) { \
		if (sp->sp_lstchg) {\
			lstchg = sp->sp_lstchg * DAY; \
			tmp = gmtime(&lstchg); \
			(void) fprintf(stderr, "%.2d/%.2d/%.2d  ", \
			(tmp->tm_mon + 1), tmp->tm_mday, tmp->tm_year); \
		} else\
			(void) fprintf(stderr, "00/00/00  "); \
		if ((sp->sp_min >= 0) && (sp->sp_warn > 0))\
			(void) fprintf(stderr, "%d  %d  %d ", \
			sp->sp_min, sp->sp_max, sp->sp_warn); \
		else if (sp->sp_min >= 0) \
			(void) fprintf(stderr, "%d  %d  ", \
			sp->sp_min, sp->sp_max); \
		else if (sp->sp_warn > 0) \
			(void) fprintf(stderr, "    %d  %d ", \
			sp->sp_max, sp->sp_warn); \
		else \
			(void) fprintf(stderr, "    %d  ", sp->sp_max); \
	}\
}

/*
 * XXX: We use our own version of the shadow passwd getent routine.
 * See below for details.  Compatible with version 2 of the name service
 * switch.  In the future, the name service switch implementation may
 * change and these functions and the Makefile may have to
 * be modified.
 */

int
sa_get_authtokattr(iah, ga_getattr, ia_statusp, repository, nisdomain)
	void			*iah;
	char			***ga_getattr;
	struct ia_status	*ia_statusp;
	int			repository;
	char			*nisdomain;
{
	register int		k;
	char 			value[MAX_ATTR_SIZE];
	int 			retcode;
	long 			lstchg;
	char 			*usrname;
	char 			*prognamep;
	struct ia_conv		*ia_convp;
	static char		*get_attributes[MAX_NUM_ATTR];
	struct spwd		*curr_sp = NULL;
	int			found = 0;
	struct spwd 		*psp;
	struct tm		*tmp;
	char 			messages[MAX_NUM_MSG][MAX_MSG_SIZE];

	dprintf1("get_authtokattr() called: repository=%x\n", repository);
	if ((retcode = sa_getall(iah, &prognamep,
	    &usrname, NULL, NULL, &ia_convp)) != IA_SUCCESS)
		return (retcode);

	/* repository must be specified in the command line. */
	if (repository == R_DEFAULT) {
		(void) fprintf(stderr,
	"You must specify repository when displaying passwd attributes\n");
		return (IA_FATAL);
	}

	if (usrname == NULL || *usrname == NULL) {
		/* print nis+ table */
		/*
		 * Cat the table using our private _np_getspent()
		 */
		if (!IS_NISPLUS(repository)) {
			(void) fprintf(stderr,
			    "internal error: wrong repository\n");
			return (IA_FATAL);
		}

		_np_setspent();
		while ((psp = _np_getspent()) != NULL) {
			found++;
			(void) fprintf(stderr, "%s  ", psp->sp_namp);
			PRT_PWD(psp->sp_pwdp);
			PRT_AGE(psp);
			(void) fprintf(stderr, "\n");
		}
		_np_endspent();

		/*
		 * If password table does not have any entries or is missing,
		 * return fatal error.
		 */
		if (found == 0) {
			(void) fprintf(stderr, "%s: %s\n", prognamep, MSG_FE);
			return (IA_FATAL);
		}
		return (IA_SUCCESS);
	}

	retcode = ck_perm(prognamep, usrname, ia_convp, repository, nisdomain);
	if (retcode != 0) {
		ga_getattr = NULL;
		return (retcode);
	}

	if (IS_FILES(repository)) {
		curr_sp = unix_sp;
	} else if (IS_NIS(repository)) {
		curr_sp = nis_sp;
	} else if (IS_NISPLUS(repository)) {
		curr_sp = nisplus_sp;
	} else {
		sprintf(messages[0],
		    "%s: System error: repository out of range\n", prognamep);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
	}

	k = 0;

	/* get attribute "AUTHTOK_STATUS" */
	if (*curr_sp->sp_pwdp == NULL)
		(void) strcpy(value, "NP  ");
	else if ((int)strlen(curr_sp->sp_pwdp) < NUMCP)
		(void) strcpy(value, "LK  ");
	else
		(void) strcpy(value, "PS  ");
	setup_getattr(get_attributes, k++, "AUTHTOK_STATUS=", value);


	if (curr_sp->sp_max != -1) {
		/* get attribute "AUTHTOK_LASTCHANGE" */
		if (curr_sp->sp_lstchg) {
			lstchg = curr_sp->sp_lstchg * DAY;
			sprintf(value, "%d", lstchg);
		} else {
			sprintf(value, "%d", curr_sp->sp_lstchg);
		}
		setup_getattr(get_attributes, k++,
			"AUTHTOK_LASTCHANGE=", value);

		/* get attribute "AUTHTOK_MINAGE"		*/
		/* "AUTHTOK_MAXAGE", and "AUTHTOK_WARNDATE"	*/
		if ((curr_sp->sp_min >= 0) && (curr_sp->sp_warn > 0)) {
			sprintf(value, "%d", curr_sp->sp_min);
			setup_getattr(get_attributes, k++,
				"AUTHTOK_MINAGE=", value);
			sprintf(value, "%d", curr_sp->sp_max);
			setup_getattr(get_attributes, k++,
				"AUTHTOK_MAXAGE=", value);
			sprintf(value, "%d", curr_sp->sp_warn);
			setup_getattr(get_attributes, k++,
				"AUTHTOK_WARNDATE=", value);
		} else {
			if (curr_sp->sp_min >= 0) {
				sprintf(value, "%d", curr_sp->sp_min);
				setup_getattr(get_attributes, k++,
					"AUTHTOK_MINAGE=", value);
				sprintf(value, "%d", curr_sp->sp_max);
				setup_getattr(get_attributes, k++,
					"AUTHTOK_MAXAGE=", value);
			} else {
				if (curr_sp->sp_warn > 0) {
					sprintf(value, "%d", curr_sp->sp_max);
					setup_getattr(get_attributes, k++,
						"AUTHTOK_MAXAGE=", value);
					sprintf(value, "%d", curr_sp->sp_warn);
					setup_getattr(get_attributes, k++,
						"AUTHTOK_WARNDATE=", value);
				} else {
					sprintf(value, "%d", curr_sp->sp_max);
					setup_getattr(get_attributes, k++,
						"AUTHTOK_MAXAGE=", value);
				}
			}
		}
	}
	/* terminate with NULL */
	setup_getattr(get_attributes, k, NULL, NULL);

	*ga_getattr = &get_attributes[0];
	return (IA_SUCCESS);

}


/*
 * XXX Our private version of the switch frontend for getspent.  We want to
 * search just the nisplus sp file, so we want to bypass normal nsswitch.conf
 * based processing.  This implementation compatible with version 2 of the
 * name service switch.
 */

#define	NSS_NISPLUS_ONLY	"nisplus"

int str2spwd(const char *, int, void *, char *, int);

static DEFINE_NSS_DB_ROOT(db_root);
static DEFINE_NSS_GETENT(context);

void
_np_nss_initf_shadow(p)
	nss_db_params_t	*p;
{
	p->name	= NSS_DBNAM_SHADOW;
	p->config_name    = NSS_DBNAM_PASSWD;	/* Use config for "passwd" */
	p->default_config = NSS_NISPLUS_ONLY;   /* Use nisplus only */
	p->flags = NSS_USE_DEFAULT_CONFIG;
}

void
_np_setspent()
{
	nss_setent(&db_root, _np_nss_initf_shadow, &context);
}

void
_np_endspent()
{
	nss_endent(&db_root, _np_nss_initf_shadow, &context);
	nss_delete(&db_root);
}

struct spwd *
_np_getspent_r(result, buffer, buflen)
	struct spwd	*result;
	char		*buffer;
	int		buflen;
{
	nss_XbyY_args_t arg;
	char		*nam;

	/* In getXXent_r(), protect the unsuspecting caller from +/- entries */

	do {
		NSS_XbyY_INIT(&arg, result, buffer, buflen, str2spwd);
			/* No key to fill in */
		nss_getent(&db_root, _np_nss_initf_shadow, &context, &arg);
	} while (arg.returnval != 0 &&
			(nam = ((struct spwd *)arg.returnval)->sp_namp) != 0 &&
			(*nam == '+' || *nam == '-'));

	return (struct spwd *) NSS_XbyY_FINI(&arg);
}

static nss_XbyY_buf_t *buffer;

struct spwd *
_np_getspent()
{
	nss_XbyY_buf_t	*b;

	b = NSS_XbyY_ALLOC(&buffer, sizeof (struct spwd), NSS_BUFLEN_SHADOW);

	return (b == 0 ? 0 : _np_getspent_r(b->result, b->buffer, b->buflen));
}
