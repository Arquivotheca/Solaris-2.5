
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_utils.c 1.18     94/08/29 SMI"

#include "unix_headers.h"

static  struct  passwd nouser = { "", "no:password", ~ROOTUID };
static  struct  spwd noupass = { "", "no:password" };

struct passwd	*unix_pwd = NULL;
struct spwd	*unix_sp = NULL;
struct passwd	*nis_pwd = NULL;
struct spwd	*nis_sp = NULL;
struct passwd	*nisplus_pwd = NULL;
struct spwd	*nisplus_sp = NULL;
nis_result	*passwd_res;

static char 	*spskip();
static int	special_case();
static int	illegal_input();

/* ******************************************************************** */
/*									*/
/* 		Utilities Functions					*/
/*									*/
/* ******************************************************************** */

/*
 * sa_get_pwd():
 *	To get the passwd and shadow entry for specified user
 *	It returns ERROR if the user can't be found, else it
 *	returns OK.
 */

int
sa_get_pwd(pwd, shpwd, user)
	struct 	passwd **pwd;
	struct 	spwd **shpwd;	/* Shadow password structure */
	char 	*user;
{
	static struct passwd lpwd;
	static struct spwd   lspwd;
	struct passwd *p;
	struct spwd   *sp;

	setpwent();
	if ((p = getpwnam(user)) == NULL) {
		endpwent();
		goto err;
	}
	endpwent();

		/* make private copy of passwd entry */
	if (lpwd.pw_name) {
		free(lpwd.pw_name);
		free(lpwd.pw_passwd);
		free(lpwd.pw_age);
		free(lpwd.pw_comment);
		free(lpwd.pw_gecos);
		free(lpwd.pw_dir);
		free(lpwd.pw_shell);
	}
	lpwd = *p;

	if ((lpwd.pw_name = strdup(p->pw_name)) == (char *)0)
		goto err;

	if (p->pw_passwd != NULL) {
		if ((lpwd.pw_passwd = strdup(p->pw_passwd)) == (char *)0)
			goto err;
	} else {
		if ((lpwd.pw_passwd = strdup("")) == (char *)0)
			goto err;
	}

	if ((lpwd.pw_age = strdup(p->pw_age)) == (char *)0)
		goto err;
	if ((lpwd.pw_comment = strdup(p->pw_comment)) == (char *)0)
		goto err;
	if ((lpwd.pw_gecos = strdup(p->pw_gecos)) == (char *)0)
		goto err;
	if ((lpwd.pw_dir = strdup(p->pw_dir)) == (char *)0)
		goto err;
	if ((lpwd.pw_shell = strdup(p->pw_shell)) == (char *)0)
		goto err;
	*pwd = &lpwd;

	(void) setspent();	/* Setting the shadow password file */
	if ((sp = getspnam(user)) == NULL) {
		(void) endspent();	/* Closing the shadow password file */
		goto err;
	}
	(void) endspent();	/* Closing the shadow password file */

		/* make private copy of shadow entry */
	if (lspwd.sp_namp) {
		free(lspwd.sp_namp);
		free(lspwd.sp_pwdp);
	}
	lspwd = *sp;

	if ((lspwd.sp_namp = strdup(sp->sp_namp)) == (char *)0)
		goto err;
	if (sp->sp_pwdp != NULL) {
		if ((lspwd.sp_pwdp = strdup(sp->sp_pwdp)) == (char *)0)
			goto err;
	} else {
		if ((lspwd.sp_pwdp = strdup("")) == (char *)0)
			goto err;
	}

	*shpwd = &lspwd;

	return (OK);

err:
	free(lpwd.pw_name);
	free(lpwd.pw_passwd);
	free(lpwd.pw_age);
	free(lpwd.pw_comment);
	free(lpwd.pw_gecos);
	free(lpwd.pw_dir);
	free(lpwd.pw_shell);
	memset(&lpwd, 0, sizeof (lpwd));

	free(lspwd.sp_namp);
	free(lspwd.sp_pwdp);
	memset(&lspwd, 0, sizeof (lspwd));

	*pwd = &nouser;
	*shpwd = &noupass;
	return (ERROR);
}

/*
 * ck_perm():
 * 	Check the permission of the user specified by "usrname".
 *
 * 	It returns IA_NOPERM if (1) the user has a NULL pasword or
 * 	shadow password file entry, or (2) the caller is not root and
 *	its uid is not equivalent to the uid specified by the user's
 *	password file entry.
 */

int
ck_perm(prognamep, usrname, ia_convp, repository, nisdomain)
	char *prognamep;
	char *usrname;
	struct ia_conv *ia_convp;
	int	repository;
	char *nisdomain;
{
	FILE	*pwfp, *spfp;
	char	messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	uid_t	uid;
	char    buf[NIS_MAXNAMELEN+1];
	nis_name local_principal;


	uid = getuid();
	dprintf1("ck_perm() called: repository=%d\n", repository);

	if ((repository & R_FILES) == R_FILES) {
		if (((pwfp = fopen(PASSWD, "r")) == NULL) ||
		    ((spfp = fopen(SHADOW, "r")) == NULL)) {
			sprintf(messages[0], dgettext(PAMTXD,
			    "%s:  %s does not exist\n"),
			    prognamep,  usrname);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
			/* audit_passwd_main1(unix_pwd, unix_sp); */
			return (IA_NOENTRY);
		}
		while ((unix_pwd = fgetpwent(pwfp)) != NULL)
			if (strcmp(unix_pwd->pw_name, usrname) == 0)
				break;
		while ((unix_sp = fgetspent(spfp)) != NULL)
			if (strcmp(unix_sp->sp_namp, usrname) == 0)
				break;
		(void) fclose(pwfp);
		(void) fclose(spfp);
		if ((unix_pwd == NULL) || (unix_sp == NULL)) {
			if (repository == R_FILES) {
				/* just R_FILES */
				sprintf(messages[0], dgettext(PAMTXD,
				    "%s: %s does not exist.\n"),
				    prognamep,  usrname);
				(void) display_errmsg(ia_convp->start_conv, 0,
				    1, messages, NULL);
				/* audit_passwd_main1(unix_pwd, unix_sp); */

				return (IA_NOENTRY);
			}
			/* passwd may be in other repository: continue */
		}

		if (uid != 0 && unix_pwd != NULL && uid != unix_pwd->pw_uid) {
			/* if (repository == R_FILES) { */
			/*
			 * Change passwd for another person:
			 * Even if you are nis+ admin, you can't do anything
			 * locally. Don't bother to continue.
			 */
				sprintf(messages[0], dgettext(PAMTXD,
				    "%s: %s\n"), prognamep, MSG_NP);
				sprintf(messages[1], dgettext(PAMTXD,
				    "%s: %s\n"), prognamep,
				    "Can't change local passwd file\n");
				(void) display_errmsg(ia_convp->start_conv, 0,
				    2, messages, NULL);
				/* audit_passwd_main2(); */
				return (IA_NOPERM);
			/* } */
		}
		if (repository == R_FILES)
			return (IA_SUCCESS);
		/* else continue: could have some remote repository */
	}

	if ((repository & R_NIS) == R_NIS) {
		/* get pwd struct from yp */

		nis_pwd = getpwnam_from(usrname, R_NIS);
		nis_sp = getspnam_from(usrname, R_NIS);

		if (nis_pwd == NULL || nis_sp == NULL) {
			/* we don't want to report error if user is in files */
			if (unix_pwd == NULL || unix_sp == NULL) {
				sprintf(messages[0], dgettext(PAMTXD,
				    "%s:  %s does not exist\n"),
				    prognamep,  usrname);
				(void) display_errmsg(ia_convp->start_conv, 0,
				    1, messages, NULL);
				return (IA_NOENTRY);
			} else
				return (IA_SUCCESS);
		}
		if (uid != 0 && uid != nis_pwd->pw_uid) {
			/* This check is copied from yppasswd */
			/* Is uid != 0 really necessary? */

			sprintf(messages[0], dgettext(PAMTXD,
			    "%s: %s\n"), prognamep, MSG_NP);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
			    messages, NULL);
			/* audit_passwd_main2(); */

			/*
			 * This is it. It can't be other repository.
			 * R_FILES is already checked. YP and NIS+ can't
			 * be valid at the same time.
			 */
			return (IA_NOPERM);
		}
		return (IA_SUCCESS);
	}

	if ((repository & R_NISPLUS) == R_NISPLUS) {
		/*
		 * Special case root: don't bother to get root from nis+.
		 */
		if (strcmp(usrname, "root") == 0) {
			if (unix_pwd == NULL || unix_sp == NULL) {
				sprintf(messages[0], dgettext(PAMTXD,
				    "%s:  %s does not exist\n"),
				    prognamep,  usrname);
				(void) display_errmsg(ia_convp->start_conv, 0,
				    1, messages, NULL);
				return (IA_NOENTRY);
			} else
				return (IA_SUCCESS);
		}

		dprintf1("ck_perm(): nisdomain=%s\n", nisdomain);

		/*
		 * This is setuid program. We need to use user id to
		 * make any nis+ request. But don't give up the super
		 * user power yet. It is needed in local passwd file
		 * manipulation.
		 */
		(void) setuid(0);	/* keep real user id as root */
		(void) seteuid(uid);

		nisplus_pwd = getpwnam_from(usrname, R_NISPLUS);
		nisplus_sp = getspnam_from(usrname, R_NISPLUS);
		if (nisplus_pwd == NULL || nisplus_sp == NULL) {
			/* we don't want to report error if user is in files */
			if (unix_pwd == NULL || unix_sp == NULL) {
				sprintf(messages[0], dgettext(PAMTXD,
				    "%s:  %s does not exist\n"),
				    prognamep,  usrname);
				(void) display_errmsg(ia_convp->start_conv, 0,
				    1, messages, NULL);
				return (IA_NOENTRY);
			} else
				return (IA_SUCCESS);
		}

		/*
		 * ck_priv() from nispasswd.c is replaced by __nis_isadmin()
		 * local_principal is internal, it is not meant to be free()ed
		 */
		local_principal = nis_local_principal();

		mutex_lock(&_priv_lock);
		privileged = __nis_isadmin(local_principal, "passwd",
		    nisdomain);
		mutex_unlock(&_priv_lock);

		/* from nispasswd.c nisplus_getpwinfo() */
		if ((9 + strlen(usrname) + strlen(nisdomain) + PASSTABLELEN) >
		    (size_t) NIS_MAXNAMELEN) {
			sprintf(messages[0], dgettext(PAMTXD,
			    "%s: %s\n"), prognamep, MSG_NP);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
			    messages, NULL);
			return (IA_NOPERM);
		}

		sprintf(buf, "[name=%s],%s.%s", usrname, PASSTABLE, nisdomain);
		if (buf[strlen(buf) - 1] != '.')
			(void) strcat(buf, ".");

		/*
		 * We must use an authenticated handle to get the cred
		 * table information for the user we want to modify the
		 * cred info for. If we can't even read that info, we
		 * definitely wouldn't have modify permission. Well..
		 */
		passwd_res = nis_list(buf, USE_DGRAM+FOLLOW_LINKS+FOLLOW_PATH,
		    NULL, NULL);
		if (passwd_res->status != NIS_SUCCESS) {
			sprintf(messages[0], dgettext(PAMTXD,
			    "%s: %s\n"), prognamep, MSG_NP);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
			    messages, NULL);
			return (IA_NOPERM);
		}
		return (IA_SUCCESS);
	}

	sprintf(messages[0],
	    "%s: System error: repository out of range\n", prognamep);
	(void) display_errmsg(ia_convp->start_conv, 0, 1,
	    messages, NULL);
	return (IA_FATAL);
}





/*
 * Establish the Secure RPC secret key for the given uid using
 * the given password to decrypt the secret key, and store it with
 * the key service.
 *
 * If called with a nonzero 'reestablish' parameter, the key
 * is obtained from the name service, decrypted, and stored
 * even if the keyserver already has a key stored for the uid.
 * If the 'reestablish' parameter is zero, the function will not
 * try to reset the key.  It will return immediately with
 * SA_ESTKEY_ALREADY.
 *
 * Returns one of the following codes:
 *   SA_ESTKEY_ALREADY - reestablish flag was zero, and key was already set.
 *   SA_ESTKEY_SUCCESS - successfully obtained, decrypted, and set the key
 *   SA_ESTKEY_NOCREDENTIALS - the user has no credentials.
 *   SA_ESTKEY_BADPASSWD - the password supplied didn't decrypt the key
 *   SA_ESTKEY_CANTSETKEY - decrypted the key, but couldn't store key with
 *			    the key service.
 *
 * If netnamebuf is a non-NULL pointer, the netname will be returned in
 * netnamebuf, provided that the return status is not
 * SA_ESTKEY_NOCREDENTIALS or SA_ESTKEY_ALREADY.  If non-NULL, the
 * netnamebuf pointer must point to a buffer of length at least
 * MAXNETNAMELEN+1 characters.
 */

int
sa_establish_key(uid, password, reestablish, netnamebuf)
	uid_t   uid;
	int	reestablish;
	char 	*password;
	char    *netnamebuf;
{
	char    netname[MAXNETNAMELEN+1];
	struct 	key_netstarg netst;
	uid_t   orig_uid;

	orig_uid = geteuid();
	if (seteuid(uid) == -1)
		/* can't set uid */
		return (SA_ESTKEY_NOCREDENTIALS);


	if (!reestablish && key_secretkey_is_set()) {
		/* key is already established and we are not to reestablish */
		(void) seteuid(orig_uid);
		return (SA_ESTKEY_ALREADY);
	}


	if (!getnetname(netname)) {
		/* can't construct netname */
		(void) seteuid(orig_uid);
		return (SA_ESTKEY_NOCREDENTIALS);
	}

	if (!getsecretkey(netname, &(netst.st_priv_key), password)) {
		/* no secret key */
		(void) seteuid(orig_uid);
		return (SA_ESTKEY_NOCREDENTIALS);
	}

	if (netnamebuf) {
		/* return copy of netname in caller's buffer */
		(void) strcpy(netnamebuf, netname);
	}

	if (netst.st_priv_key[0] == 0) {
		/* password does not decrypt secret key */
		(void) seteuid(orig_uid);
		return (SA_ESTKEY_BADPASSWD);
	}


	/* secret key successfully decrypted at this point */

	/* store with key service */

	netst.st_netname = strdup(netname);
	(void) memset(netst.st_pub_key, 0, HEXKEYBYTES);

	if (key_setnet(&netst) < 0) {
		free(netst.st_netname);
		(void) seteuid(orig_uid);
		return (SA_ESTKEY_CANTSETKEY);
	}

	free(netst.st_netname);
	(void) seteuid(orig_uid);
	return (SA_ESTKEY_SUCCESS);
}






/*
 * attr_match():
 *
 *	Check if the attribute name in string s1 is equivalent to
 *	that in string s2.
 *	s1 is either name, or name=value
 *	s2 is name=value
 *	if names match, return value of s2, else NULL
 */

char *
attr_match(s1, s2)
	register char *s1, *s2;
{
	while (*s1 == *s2++)
		if (*s1++ == '=')
		return (s2);
	if (*s1 == '\0' && *(s2-1) == '=')
		return (s2);
	return (NULL);
}

/*
 * attr_find():
 *
 *	Check if the attribute name in string s1 is present in the
 *	attribute=value pairs array pointed by s2.
 *	s1 is name
 *	s2 is an array of name=value pairs
 *	if s1 match the name of any one of the name in the name=value pairs
 *	pointed by s2, then 1 is returned; else 0 is returned
 */

int
attr_find(s1, s2)
	register char *s1, *s2[];
{
	int 	i;
	char 	*sa, *sb;

	i = 0;
	while (s2[i] != NULL) {
		sa = s1;
		sb = s2[i];
		while (*sa++ == *sb++) {
			if ((*sa == '\0') && (*sb == '='))
				return (1); /* find */
		}
		i++;
	}

	return (0); /* not find */
}

/*
 * reverse():
 *	To reverse a string
 */

void
reverse(s)
	char s[];
{
	int c, i, j;

	for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
}


/*
 * free_setattr():
 *	free storage pointed by "setattr"
 */

void
free_setattr(setattr)
	char * setattr[];
{
	int i;

	for (i = 0; setattr[i] != NULL; i++)
		free(setattr[i]);

}

/*
 * setup_getattr():
 *	allocate memory and copy in attribute=value pair
 *	into the array of attribute=value pairs pointer
 *	by "getattr"
 */

void
setup_getattr(getattr, k, attr, value)
	char *getattr[];
	int k;
	char attr[];
	char value[];
{
	if (attr != NULL) {
		getattr[k] = (char *)malloc(MAX_ATTR_SIZE);
		(void) strcpy(getattr[k], attr);
		(void) strcat(getattr[k], value);
	} else
		getattr[k] = NULL;
}

/*
 * setup_setattr():
 *	allocate memory and copy in attribute=value pair
 *	into the array of attribute=value pairs pointer
 *	by "setattr"
 */

void
setup_setattr(setattr, k, attr, value)
	char *setattr[];
	int k;
	char attr[];
	char value[];
{

	if (attr != NULL) {
		setattr[k] = (char *)malloc(MAX_ATTR_SIZE);
		(void) strcpy(setattr[k], attr);
		(void) strcat(setattr[k], value);
	} else
		setattr[k] = NULL;
}

/*
 * free_msg():
 *	free storage for messages used in the call back "ia_conv" functions
 */

void
free_msg(num_msg, msg)
	int num_msg;
	struct ia_message *msg;
{
	int 			i;
	struct ia_message 	*m;

	m = msg;
	for (i = 0; i < num_msg; i++, m++)
		free(m->msg);
	free(msg);
}

/*
 * free_resp():
 *	free storage for responses used in the call back "ia_conv" functions
 */

void
free_resp(num_msg, resp)
	int num_msg;
	struct ia_response *resp;
{
	int			i;
	struct ia_response	*r;

	r = resp;
	for (i = 0; i < num_msg; i++, r++)
		free(r->resp);
	free(resp);
}

/*
 * display_errmsg():
 *	display error message by calling the call back functions
 *	provided by the application through "ia_conv" structure
 */

int
display_errmsg(conv_funp, conv_id, num_msg, messages, conv_apdp)
	int (*conv_funp)();
	int conv_id;
	int num_msg;
	char messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	void *conv_apdp;
{
	struct ia_message	*msg;
	struct ia_message	*m;
	struct ia_response	*resp;
	int			i;
	int			k;
	int			retcode;

	msg = (struct ia_message *)calloc(num_msg, sizeof (struct ia_message));
	if (msg == NULL) {
		return (IA_CONV_FAILURE);
	}
	m = msg;

	i = 0;
	k = num_msg;
	resp = NULL;
	while (k--) {
		/*
		 * fill out the ia_message structure to display error message
		 */
		m->msg_style = IA_ERROR_MSG;
		m->msg = (char *)malloc(MAX_MSG_SIZE);
		if (m->msg != NULL)
			(void) strcpy(m->msg, (const char *)messages[i]);
		else
			continue;
		m->msg_len = strlen(m->msg);
		m++;
		i++;
	}

	/*
	 * Call conv function to display the message,
	 * ignoring return value for now
	 */
	retcode = conv_funp(conv_id, num_msg, &msg, &resp, conv_apdp);
	free_msg(num_msg, msg);
	free_resp(num_msg, resp);
	return (retcode);
}

/*
 * get_authtok():
 *	get authentication token by calling the call back functions
 *	provided by the application through "ia_conv" structure
 */

int
get_authtok(conv_funp, conv_id, num_msg, messages, conv_apdp, ret_respp)
	int (*conv_funp)();
	int conv_id;
	int num_msg;
	char messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	void *conv_apdp;
	struct ia_response	**ret_respp;
{
	struct ia_message	*msg;
	struct ia_message	*m;
	int			i;
	int			k;
	int			retcode;

	i = 0;
	k = num_msg;

	msg = (struct ia_message *)calloc(num_msg, sizeof (struct ia_message));
	if (msg == NULL) {
		return (IA_CONV_FAILURE);
	}
	m = msg;

	while (k--) {
		/*
		 * fill out the message structure to display error message
		 */
		m->msg_style = IA_PROMPT_ECHO_OFF;
		m->msg = (char *)malloc(MAX_MSG_SIZE);
		if (m->msg != NULL)
			(void) strcpy(m->msg, (char *)messages[i]);
		else
			continue;
		m->msg_len = strlen(m->msg);
		m++;
		i++;
	}

	/*
	 * Call conv function to display the prompt,
	 * ignoring return value for now
	 */
	retcode = conv_funp(conv_id, num_msg, &msg, ret_respp, conv_apdp);
	free_msg(num_msg, msg);
	return (retcode);
}


void
populate_age(enobj, sp)
	struct nis_object *enobj;
	struct spwd *sp;
{
	char *oldage, *p, *end;
	long x;

	/*
	 * shadow (col 7)
	 */

	sp->sp_lstchg = -1;
	sp->sp_min = -1;
	sp->sp_max = -1;
	sp->sp_warn = -1;
	sp->sp_inact = -1;
	sp->sp_expire = -1;
	sp->sp_flag = 0;

	if ((p = ENTRY_VAL(enobj, 7)) == NULL)
		return;
	oldage = strdup(p);

	p = oldage;

	x = strtol(p, &end, 10);
	if (end != memchr(p, ':', strlen(p)))
		return;
	if (end != p)
		sp->sp_lstchg = x;

	p = spskip(p);
	x = strtol(p, &end, 10);
	if (end != memchr(p, ':', strlen(p)))
		return;
	if (end != p)
		sp->sp_min = x;

	p = spskip(p);
	x = strtol(p, &end, 10);
	if (end != memchr(p, ':', strlen(p)))
		return;
	if (end != p)
		sp->sp_max = x;

	p = spskip(p);
	x = strtol(p, &end, 10);
	if (end != memchr(p, ':', strlen(p)))
		return;
	if (end != p)
		sp->sp_warn = x;

	p = spskip(p);
	x = strtol(p, &end, 10);
	if (end != memchr(p, ':', strlen(p)))
		return;
	if (end != p)
		sp->sp_inact = x;

	p = spskip(p);
	x = strtol(p, &end, 10);
	if (end != memchr(p, ':', strlen(p)))
		return;
	if (end != p)
		sp->sp_expire = x;

	p = spskip(p);
	x = strtol(p, &end, 10);
	if ((end != memchr(p, ':', strlen(p))) &&
	    (end != memchr(p, '\n', strlen(p))))
		return;
	if (end != p)
		sp->sp_flag = x;

	free(oldage);
}


static char *
spskip(p)
	register char *p;
{
	while (*p && *p != ':' && *p != '\n')
		++p;
	if (*p == '\n')
		*p = '\0';
	else if (*p)
		*p++ = '\0';
	return (p);
}


#define	STRSIZE	100
#define	DEFSHELL "/bin/sh"

/*
 * getloginshell() displays old login shell and asks for new login shell.
 *	The new login shell is then returned to calling function.
 */
char *
getloginshell(oldshell)
	char	*oldshell;
{
	static char newshell[STRSIZE];
	char *cp, *valid, *getusershell();

	if (oldshell == 0 || *oldshell == '\0')
		oldshell = DEFSHELL;

	mutex_lock(&_priv_lock);
	if (privileged == FALSE) {
		for (valid = getusershell(); valid; valid = getusershell())
			if (strcmp(oldshell, valid) == 0)
				break;
		if (valid == NULL) {
			(void) printf("\tCannot change from \
restricted shell %s.\n", oldshell);
			mutex_unlock(&_priv_lock);
			return (NULL);
		}
	}
	mutex_unlock(&_priv_lock);
	(void) printf("Old shell: %s\nNew shell: ", oldshell);
	(void) fgets(newshell, sizeof (newshell) - 1, stdin);
	cp = strchr(newshell, '\n');
	if (cp)
		*cp = '\0';
	if (newshell[0] == '\0' || strcmp(newshell, oldshell) == 0) {
		(void) printf("Login shell unchanged.\n");
		return (NULL);
	}
	/*
	 * Allow user to give shell name w/o preceding pathname.
	 */
	mutex_lock(&_priv_lock);
	if (privileged) {
		valid = newshell;
	} else {
		setusershell();
		/*
		 * XXX:
		 * Keep in mind that, for whatever this validation is worth,
		 * a root on a machine can edit /etc/shells and get any shell
		 * accepted as a valid shell in the NIS+ table.
		 */
		for (valid = getusershell(); valid; valid = getusershell()) {
			if (newshell[0] == '/') {
				cp = valid;
			} else {
				cp = strrchr(valid, '/');
				if (cp == 0)
					cp = valid;
				else
					cp++;
			}
			if (strcmp(newshell, cp) == 0)
				break;
		}
	}
	mutex_unlock(&_priv_lock);
	if (valid == 0) {
		(void) printf("%s is unacceptable as a new shell.\n",
		    newshell);
		return (NULL);
	}
	if (access(valid, X_OK) < 0) {
		(void) fprintf(stderr,
			"warning: %s is unavailable on this machine.\n", valid);
	}
	return (valid);
}

/*
 * Get name.
 */
char *
getfingerinfo(old_gecos)
	char	*old_gecos;
{
	char 		in_str[STRSIZE];
	static char	answer[STRSIZE];

	answer[0] = '\0';
	(void) printf("Default values are printed inside of '[]'.\n");
	(void) printf("To accept the default, type <return>.\n");
	(void) printf("To have a blank entry, type the word 'none'.\n");

	/*
	 * Get name.
	 */
	do {
		(void) printf("\nName [%s]: ", old_gecos);
		(void) fgets(in_str, STRSIZE, stdin);
		if (special_case(in_str, old_gecos))
			break;
	} while (illegal_input(in_str));
	(void) strcpy(answer, in_str);
	if (strcmp(answer, old_gecos) == 0) {
		(void) printf("Finger information unchanged.\n");
		return (NULL);
	}
	return (answer);
}

/*
 * Get Home Dir.
 */
char *
gethomedir(olddir)
	char	*olddir;
{
	char in_str[STRSIZE];
	static char answer[STRSIZE];

	answer[0] = '\0';
	(void) printf("Default values are printed inside of '[]'.\n");
	(void) printf("To accept the default, type <return>.\n");
	(void) printf("To have a blank entry, type the word 'none'.\n");
	do {
		(void) printf("\nHome Directory [%s]: ", olddir);
		(void) fgets(in_str, STRSIZE, stdin);
		if (special_case(in_str, olddir))
			break;
	} while (illegal_input(in_str));
	(void) strcpy(answer, in_str);
	if (strcmp(answer, olddir) == 0) {
		(void) printf("Homedir information unchanged.\n");
		return (NULL);
	}
	return (answer);
}


/*
 * Prints an error message if a ':' or a newline is found in the string.
 * A message is also printed if the input string is too long.
 * The password sources use :'s as seperators, and are not allowed in the "gcos"
 * field.  Newlines serve as delimiters between users in the password source,
 * and so, those too, are checked for.  (I don't think that it is possible to
 * type them in, but better safe than sorry)
 *
 * Returns '1' if a colon or newline is found or the input line is too long.
 */
static int
illegal_input(input_str)
	char *input_str;
{
	char *ptr;
	int error_flag = 0;
	int length = (int)strlen(input_str);

	if (strchr(input_str, ':')) {
		(void) printf("':' is not allowed.\n");
		error_flag = 1;
	}
	if (input_str[length-1] != '\n') {
		/* the newline and the '\0' eat up two characters */
		(void) printf("Maximum number of characters allowed is %d\n",
			STRSIZE-2);
		/* flush the rest of the input line */
		while (getchar() != '\n')
			/* void */;
		error_flag = 1;
	}
	/*
	 * Delete newline by shortening string by 1.
	 */
	input_str[length-1] = '\0';
	/*
	 * Don't allow control characters, etc in input string.
	 */
	for (ptr = input_str; *ptr != '\0'; ptr++) {
		/* 040 is ascii char "space" */
		if ((int) *ptr < 040) {
			(void) printf("Control characters are not allowed.\n");
			error_flag = 1;
			break;
		}
	}
	return (error_flag);
}



/*
 *  special_case returns true when either the default is accepted
 *  (str = '\n'), or when 'none' is typed.  'none' is accepted in
 *  either upper or lower case (or any combination).  'str' is modified
 *  in these two cases.
 */
static int
special_case(str, default_str)
	char *str, *default_str;
{
	static char word[] = "none\n";
	char *ptr, *wordptr;

	/*
	 *  If the default is accepted, then change the old string do the
	 *  default string.
	 */
	if (*str == '\n') {
		(void) strcpy(str, default_str);
		return (1);
	}
	/*
	 *  Check to see if str is 'none'.  (It is questionable if case
	 *  insensitivity is worth the hair).
	 */
	wordptr = word - 1;
	for (ptr = str; *ptr != '\0'; ++ptr) {
		++wordptr;
		if (*wordptr == '\0')	/* then words are different sizes */
			return (0);
		if (*ptr == *wordptr)
			continue;
		if (isupper(*ptr) && (tolower(*ptr) == *wordptr))
			continue;
		/*
		 * At this point we have a mismatch, so we return
		 */
		return (0);
	}
	/*
	 * Make sure that words are the same length.
	 */
	if (*(wordptr+1) != '\0')
		return (0);
	/*
	 * Change 'str' to be the null string
	 */
	*str = '\0';
	return (1);
}
