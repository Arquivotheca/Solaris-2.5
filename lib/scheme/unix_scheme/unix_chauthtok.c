
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_chauthtok.c 1.22     94/06/30 SMI"

#include "unix_headers.h"

char	opwbuf[10];	/* old passwd */
char	orpcpw[10];	/* old rpc passwd */
int	opwlen = 0;	/* old passwd length */

/* nispasswd */
struct nis_result *cred_res = NULL;	/* cred entry nis_list result */

/*
 * Holds the old encrypted secret key obtained from the server.
 */
char curcryptsecret[HEXKEYBYTES+KEYCHECKSUMSIZE+1];
bool_t  privileged = FALSE;
mutex_t _priv_lock;		/* privileged variable lock */

static uid_t	uid;
static bool_t	verify_passwd();
static int	ck_passwd();
static int	turn_on_default_aging();
static int	get_newpasswd();
static bool_t	circ();

/*
 * sa_chauthtok():
 *	To change authentication token.
 *
 * 	This function calls ck_perm() to check the caller's
 *	permission.  If the check succeeds, it will then call
 *	ck_passwd() to validate the old password and password
 *	aging information.  If ck_passwd() succeeds, get_newpasswd()
 *	will then be called to get and check the user's new passwd.
 *	Last, update_authtok_<repository>() will be called to change the user's
 *	password to the new password.
 */

int
sa_chauthtok(iah, ia_statusp, repository, nisdomain)
	void			*iah;
	struct	ia_status	*ia_statusp;
	int			repository;
	char			*nisdomain;
{

	int		retcode;
	int		insist;
	char		pwbuf[10];	/* new passwd */
	char		ypwbuf[10];	/* old yp passwd in clear */
	char		nispwbuf[10];	/* old nis+ passwd in clear */
	char 		saltc[2];	/* crypt() takes 2 char */
					/* string as a salt */
	time_t 		salt;
	int		i, c;
	int		count;		/* count verifications */
	char		messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	char		*pw;
	char 		*usrname;
	char 		*prognamep;
	struct ia_conv	*ia_convp;
	int		re;
	int		newpass;
	char	buf[NIS_MAXNAMELEN+1], *p;	/* nispasswd */

	mutex_init(&_priv_lock, USYNC_THREAD, NULL);
	usrname = NULL;
	prognamep = NULL;
	insist = 0;
	count = 0;
	orpcpw[0] = '\0';

	uid = getuid();
	dprintf1("chauthtok called: %0x \n", repository);
	if ((retcode = sa_getall(iah, &prognamep,
	    &usrname, NULL, NULL, &ia_convp)) != IA_SUCCESS)
		return (retcode);

	/*
	 * get_ns() will consult nsswitch file and set the repository
	 * accordingly.
	 * If specified repository is not defined in switch file, a warning
	 * is printed, e.g. -r nis while "passwd: files, nis+" is defined
	 * in switch file.
	 */
	repository = get_ns(repository, ia_convp);
	if (repository == -1)
		return (IA_FATAL);
	if ((repository & R_NISPLUS) && ((nisdomain == NULL) ||
	    (*nisdomain == '\0')))
		nisdomain = nis_local_directory();

	dprintf1("chauthtok(): repository is %x after get_ns()\n", repository);

	/* if we still get R_DEFAULT after calling get_ns(), error! */
	if (repository == R_DEFAULT)
		return (IA_FATAL);

	retcode = ck_perm(prognamep, usrname, ia_convp, repository, nisdomain);
	if (retcode != 0)
		return (retcode);


	/* nispasswd or -r nisplus */
	if (IS_NISPLUS(repository)) {
		/*
		 * Sets the global variables, cred_res and curcryptsecret.
		 * nisplus_getcredinfo()
		 */

		/* strlen("[cname=.,auth_type=DES],.") + null + "." = 27 */
		if ((27 + strlen(usrname) + 2*strlen(nisdomain) + PKTABLELEN) >
		    (size_t) NIS_MAXNAMELEN) {
			(void) sprintf(messages[0],
			    "%s: not enough internal memory\n", prognamep);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
			return (IA_FATAL);
		}

		(void) sprintf(buf, "[cname=%s.%s,auth_type=DES],%s.%s",
		    usrname, nisdomain, PKTABLE, nisdomain);
		if (buf[strlen(buf) - 1] != '.')
			(void) strcat(buf, ".");

		/*
		 * We must use an authenticated handle to get the cred
		 * table information for the user we want to modify the
		 * cred info for. If we can't even read that info, we
		 * definitely wouldn't have modify permission. Well..
		 */
		cred_res = nis_list(buf, USE_DGRAM+FOLLOW_LINKS+FOLLOW_PATH,
		    NULL, NULL);
		if (cred_res->status != NIS_SUCCESS) {
			if (IS_OPWCMD(repository)) {
				(void) sprintf(messages[0], dgettext(PAMTXD,
				    "nispasswd: user must have credential\n"));
				(void) display_errmsg(ia_convp->start_conv,
				    0, 1, messages, NULL);
				return (IA_FATAL);
			}
			/* continue even if there is no cred */
		} else {
			if ((p = ENTRY_VAL(cred_res->objects.objects_val, 4)) ==
			    NULL)
				(void) strcpy(curcryptsecret, NULLSTRING);
			else
				(void) strncpy(curcryptsecret, p,
				ENTRY_LEN(cred_res->objects.objects_val, 4));
		}
	}

	/*
	 * unix_sp, unix_pwd, yp_sp, yp_pwd and nis_sp, nis_pwd
	 * point to the right structs. These are handled in ck_perm()
	 */

	newpass = 0;
	for (re = R_NISPLUS; re != 0; re = re >> 1) {
		if ((repository & re) == 0)
			continue;

		if ((retcode = ck_passwd(iah, prognamep,
		    ia_convp, re, repository, nisdomain)) != IA_SUCCESS)
			return (retcode);

		/* save the old passwd: useful in update protocol */
		if (IS_NISPLUS(re)) {
			(void) strncpy(nispwbuf, opwbuf, 9);
		} else if (IS_NIS(re)) {
			(void) strncpy(ypwbuf, opwbuf, 9);
		}
		/* else: no need to save local file passwd */
		/* It won't be over written. */

		/*
		 * Once we get our new passwd for remote repository
		 * (nis or nis+), we don't need to prompt again for
		 * local repository (files).
		 * The default behavior is to keep passwds in sync.
		 * However, we did check old passwds in both repositories.
		 */
		if (newpass)
			break;
		do {
			retcode = get_newpasswd(&insist, &count, pwbuf,
					usrname, prognamep, ia_convp, re);
		} while ((retcode == PAM_TRY_AGAIN) && (insist < MAX_INSIST));

		/*
		 * This is for the case that a user only resides in files.
		 * It doesn't make sense to get a new passwd for a user who
		 * doesn't exist in remote repositories.
		 */
		if (retcode == IA_NOENTRY)
			continue;

		dprintf1("new passwd= %s\n", pwbuf);

		/* three chances to meet triviality standard */
		if (insist >= MAX_INSIST) {
			(void) sprintf(messages[0], dgettext(PAMTXD,
			    "Too many failures - try later.\n"));
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
			    messages, NULL);
			/* audit_passwd_main5(insist); */
			return (IA_NOPERM);
		}

		if (retcode != IA_SUCCESS)
			return (retcode);

		/* we got here: it means we had a new passwd from user */
		newpass = 1;
	}

	/* Construct salt, then encrypt the new password */

	(void) time((time_t *)&salt);
	salt += (long)getpid();

	saltc[0] = salt & 077;
	saltc[1] = (salt >> 6) & 077;
	for (i = 0; i < 2; i++) {
		c = saltc[i] + '.';
		if (c > '9') c += 7;
		if (c > 'Z') c += 6;
		saltc[i] = c;
	}
	pw = crypt(pwbuf, saltc);

	/* update remote repositories first */
	/* make sure the user exists before we update the repository */
	if (IS_NIS(repository) && (nis_pwd != NULL)) {
		retcode = update_authtok_nis(prognamep, usrname, "passwd",
		    &pw, ia_convp, ypwbuf, pwbuf);
		if (retcode != IA_SUCCESS)
			return (retcode);
	}

	/*
	 * nis+ update will choose different protocol depending on the
	 * opwcmd flag.
	 */
	if (IS_NISPLUS(repository) && (nisplus_pwd != NULL)) {
		/* nis+ needs clear versions of old and new passwds */
		if (orpcpw[0] == '\0')
			(void) strcpy(orpcpw, nispwbuf);
		retcode = update_authtok_nisplus(prognamep, usrname, nisdomain,
		    "passwd", &pw, ia_convp, nispwbuf, orpcpw, pwbuf,
		    IS_OPWCMD(repository) ? 1 : 0);
		if (retcode != IA_SUCCESS)
			return (retcode);
	}
	if (IS_FILES(repository) && (unix_pwd != NULL)) {
		retcode = update_authtok_file(prognamep, usrname, "passwd",
			&pw, ia_convp);
		if (retcode != IA_SUCCESS)
			return (retcode);
	}

	/* audit_passwd_main10(retval); */
	return (IA_SUCCESS);
}


/*
 * ck_passwd():
 * 	To verify user old password. It also check
 * 	password aging information to verify that user is authorized
 * 	to change password.
 */

static int
ck_passwd(iah, prognamep, ia_convp, repository, real_rep, nisdomain)
	void *iah;
	char *prognamep;
	struct ia_conv *ia_convp;
	int repository;
	int real_rep;
	char *nisdomain;
{
	register int		now;
	int			retcode;
	static struct ia_response	*ret_resp;
	char 			messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	int 			num_msg;
	char			*pswd;		/* user input clear passwd */
	char			*pw;		/* encrypted passwd */
	struct passwd		*curr_pwd = NULL;
	struct spwd		*curr_sp = NULL;
	pid_t			pid;
	int			w;


	if (IS_FILES(repository)) {
		curr_pwd = unix_pwd;
		curr_sp = unix_sp;
	} else if (IS_NIS(repository)) {
		curr_pwd = nis_pwd;
		curr_sp = nis_sp;
	} else if (IS_NISPLUS(repository)) {
		curr_pwd = nisplus_pwd;
		curr_sp = nisplus_sp;
	} else {
		(void) sprintf(messages[0],
		    "%s: System error: repository out of range\n", prognamep);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
	}

	dprintf1("ck_passwd(): repository is %x \n", repository);

	/*
	 * nsswitch file may have two repositories, but it doesn't mean
	 * passwd entry will exist in both repositories.
	 * That's why cur_pwd could be null.
	 */
	if (curr_pwd == NULL || curr_sp == NULL)
		return (IA_SUCCESS);

	mutex_lock(&_priv_lock);
	if ((curr_sp->sp_pwdp[0] && uid != 0 && IS_FILES(repository)) ||
	    (curr_sp->sp_pwdp[0] && IS_NIS(repository)) ||
	    (curr_sp->sp_pwdp[0] && IS_NISPLUS(repository))) {
		/*
		 * For nis+, prompt for passwd even if it's a privileged user.
		 * However, to maintain the old nispasswd behavior. I'll skip
		 * this part if it's a privileged user.
		 */
		if (IS_NISPLUS(repository) && IS_OPWCMD(real_rep) &&
		    privileged == TRUE) {
			opwbuf[0] = '\0';
			goto aging;
		}

		if (opwlen != 0) {
			/* user already input passwd */
			dprintf1("expected old passwd= %s\n",
			    curr_sp->sp_pwdp);
			pw = crypt(opwbuf, curr_sp->sp_pwdp);
			dprintf2("user already input passwd= %s, %s\n",
			    opwbuf, pw);
			if (strcmp(curr_sp->sp_pwdp, pw) == 0) {
				dprintf1("same new/old encrypted passwd= %s\n",
				    pw);
				/* same old passwd: no need to check again */
				goto aging;
			}
		}

		if (IS_NIS(repository))
			(void) sprintf(messages[0], dgettext
			    (PAMTXD, "Enter login(NIS) password: "));
		else if (IS_NISPLUS(repository))
			(void) sprintf(messages[0], dgettext
			    (PAMTXD, "Enter login(NIS+) password: "));
		else
			(void) sprintf(messages[0], dgettext
			    (PAMTXD, "Enter login password: "));
		num_msg = 1;
		retcode = get_authtok(ia_convp->start_conv, 0,
				num_msg, messages, NULL, &ret_resp);
		if (retcode != IA_SUCCESS) {
			mutex_unlock(&_priv_lock);
			return (retcode);
		}
		pswd = ret_resp->resp;

		if (pswd == NULL) {
			(void) sprintf(messages[0],
			    dgettext(PAMTXD, "Sorry.\n"));
			(void) display_errmsg(ia_convp->cont_conv, 0, 1,
					messages, NULL);
			free_resp(num_msg, ret_resp);
			/* audit_passwd_ck_passwd(); */
			mutex_unlock(&_priv_lock);
			return (IA_FMERR);
		} else {
			(void) strcpy(opwbuf, pswd);
			/* get length of old password */
			opwlen = strlen(opwbuf);

			dprintf1("expected old passwd= %s\n",
			    curr_sp->sp_pwdp);
			pw = crypt(opwbuf, curr_sp->sp_pwdp);

			free_resp(num_msg, ret_resp);
		}

		/*
		 * If we really want to maintain the behavior of yppasswd,
		 * we can add a check of R_OPWCMD here for R_NIS.
		 * The behavior is that the user is allowed to change passwd
		 * to the master consecutively. It doesn't check the passwd
		 * at the server that it's bound to. (which may not be in
		 * sync with the master all the time).
		 */

		/*
		 * Privileged user may update someone else's passwd.
		 * It doesn't make sense to compare admin's passwd to
		 * the regular user's passwd.
		 */
		if ((privileged == FALSE) &&
		    strcmp(pw, curr_sp->sp_pwdp) != 0) {
			(void) sprintf(messages[0], dgettext(PAMTXD,
			    "Sorry: wrong passwd\n"));
			(void) display_errmsg(ia_convp->cont_conv, 0, 1,
			    messages, NULL);
			/* audit_passwd_ck_passwd1(); */
			mutex_unlock(&_priv_lock);
			return (IA_NOPERM);
		}

		/* nispasswd or -r nisplus */
		if ((privileged == FALSE) && IS_NISPLUS(repository) &&
		    (cred_res != NULL) && cred_res->status == NIS_SUCCESS) {
		/*
		 * At this point only check if the password matches the
		 * one in passwd table. If it doesn't, don't even bother
		 * about seeing if it can decrypt the secretkey in cred table.
		 * If it does, attempt to decrypt the old secretkey with it.
		 * This only makes sense when users have credentials.
		 */
			if (verify_passwd(opwbuf) == FALSE) {
				/* failed to decrypt the secret key */
				/* ask for Old secure RPC passwd */
				char okeybuf[10];

		(void) sprintf(messages[0],
		    "\tThe password you entered differs from your secure\n");
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
		(void) sprintf(messages[0],
		    "\tRPC password. To reencrypt your credentials with\n");
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
		(void) sprintf(messages[0],
		    "\tthe New login password, please enter your:\n");
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
				(void) sprintf(messages[0], dgettext
				    (PAMTXD, "Old Secure RPC password: "));
				num_msg = 1;
				retcode = get_authtok(ia_convp->start_conv, 0,
					num_msg, messages, NULL, &ret_resp);
				if (retcode != IA_SUCCESS) {
					mutex_unlock(&_priv_lock);
					return (retcode);
				}
				(void) strcpy(okeybuf, ret_resp->resp);

				if (verify_passwd(okeybuf) == FALSE) {
					(void) sprintf(messages[0],
		"\tThis password does not decrypt your secure RPC\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					(void) sprintf(messages[0],
		    "credentials either ...    try again:\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					(void) sprintf(messages[0], dgettext
					(PAMTXD, "Old Secure RPC password: "));
					num_msg = 1;
					retcode = get_authtok(
					    ia_convp->start_conv, 0,
					    num_msg, messages, NULL, &ret_resp);
					if (retcode != IA_SUCCESS) {
						mutex_unlock(&_priv_lock);
						return (retcode);
					}
					(void) strcpy(okeybuf, ret_resp->resp);

					if (verify_passwd(okeybuf) == FALSE) {
						(void) sprintf(messages[0],
						    "\tSorry.\n");
						(void) display_errmsg(
						    ia_convp->start_conv, 0, 1,
						    messages, NULL);
						mutex_unlock(&_priv_lock);
						return (IA_NOPERM);
					}
				}
				(void) strcpy(orpcpw, okeybuf);
				free_resp(num_msg, ret_resp);
			}
		/* continue to construct a new passwd */
		}
	} else
		opwbuf[0] = '\0';
aging:
	mutex_unlock(&_priv_lock);

	/* YP doesn't support aging. */
	if (IS_NIS(repository))
		return (IA_SUCCESS);

	/* password age checking applies */
	if (curr_sp->sp_max != -1 && curr_sp->sp_lstchg != 0) {
		/* If password aging is turned on and the password last */
		/* change date is set */
		now  =  DAY_NOW;
		mutex_lock(&_priv_lock);
		if (curr_sp->sp_lstchg <= now) {
			if (((uid != 0 && IS_FILES(repository)) ||
			    (privileged == FALSE && IS_NISPLUS(repository))) &&
			    (now < curr_sp->sp_lstchg  + curr_sp->sp_min)) {
				(void) sprintf(messages[0], dgettext(PAMTXD,
		"%s:  Sorry: less than %ld days since the last change.\n"),
				prognamep, curr_sp->sp_min);
				(void) display_errmsg(ia_convp->cont_conv, 0, 1,
					messages, NULL);
				/* audit_passwd_ck_passwd2(); */
				mutex_unlock(&_priv_lock);
				return (IA_NOPERM);
			}
			if (curr_sp->sp_min > curr_sp->sp_max) {
				if ((IS_FILES(repository) && uid != 0) ||
				    (IS_NISPLUS(repository) &&
				    (privileged == FALSE))) {
				(void) sprintf(messages[0], dgettext(PAMTXD,
				"%s: You may not change this password.\n"),
				prognamep);
				(void) display_errmsg(ia_convp->cont_conv, 0, 1,
					messages, NULL);
				/* audit_passwd_ck_passwd3(); */
				mutex_unlock(&_priv_lock);
				return (IA_NOPERM);
				}
			}
		}
		mutex_unlock(&_priv_lock);
	} else {
		if (curr_sp->sp_lstchg == 0 &&
		    curr_sp->sp_max > 0 || curr_sp->sp_min > 0) {
			/* If password aging is turned on */
			return (IA_SUCCESS);
		} else {
			/* aging not turned on */
			/* so turn on passwd for user with default values */
			/*
			 * Because we need to run the current process as
			 * regular user for nis+ request, we don't want
			 * to become root at this point. However, in order
			 * to turn on default aging for files, we have
			 * to have root privilege. Thus, we let the child
			 * have root privilege while the current process
			 * remains as regular user process. Refer to ck_perm()
			 * for related uid manipulation.
			 */
			switch (pid = fork()) {
			case -1:
				(void) sprintf(messages[0],
				    "System error: can't create process\n");
				(void) display_errmsg(ia_convp->start_conv,
				    0, 1, messages, NULL);
				return (IA_FATAL);
			case 0: /* child */
				(void) seteuid(0);
				retcode = turn_on_default_aging(iah,
				    repository, nisdomain);
				exit(retcode);
			default:
				/* wait for child */
				while ((w = (int)waitpid(pid, &retcode, 0))
				    != pid && w != -1)
					;
				return ((w == -1) ? w : retcode);
			}
		}
	}
	return (IA_SUCCESS);
}


/*
 * turn_on_default_aging():
 * 	Turn on the default password aging
 */

static int
turn_on_default_aging(iah, repository, nisdomain)
	void *iah;
	int	repository;
	char	*nisdomain;
{
	char			value[MAX_ATTR_SIZE];
	char			*char_p;
	char			*set_attribute[MAX_NUM_ATTR];
	int			retcode;
	int			k;
	struct ia_status	ia_status;
	int 			mindate;	/* password aging information */
	int 			maxdate;	/* password aging information */
	int 			warndate;	/* password aging information */
	int 			minlength;

	/* can't set network policy locally */
	if (IS_NISPLUS(repository))
		return (IA_SUCCESS);

	/* We only process local files. Skip anything else. */
	if (!IS_FILES(repository))
		return (IA_NOPERM);

	k = 0;

	/*
	 * Open "/etc/default/passwd" file,
	 * if password administration file can't be opened
	 * use built in defaults.
	 */
	if ((defopen(PWADMIN)) != 0) { /* M005  start */
		mindate = MINWEEKS * 7;
		maxdate = MAXWEEKS * 7;
		warndate = WARNWEEKS * 7;
		minlength = MINLENGTH;
	} else {
		/* get minimum length of password */
		if ((char_p = defread("PASSLENGTH=")) == NULL)
			minlength = MINLENGTH;
		else {
			minlength = atoi(char_p);
			if (minlength < 6 || minlength > 8)
				minlength = MINLENGTH;
		}

		/* get minimum date before password can be changed */
		if ((char_p = defread("MINWEEKS=")) == NULL)
			mindate = 7 * MINWEEKS;
		else {
			mindate = 7 * atoi(char_p);
			if (mindate < 0)
				mindate = 7 * MINWEEKS;
		}

		/* get warn date before password is expired */
		if ((char_p = defread("WARNWEEKS=")) == NULL)
			warndate = 7 * WARNWEEKS;
		else {
			warndate = 7 * atoi(char_p);
			if (warndate < 0)
				warndate = 7 * WARNWEEKS;
		}

		/* get max date that password is valid */
		if ((char_p = defread("MAXWEEKS=")) == NULL)
			maxdate = 7 * MAXWEEKS;
		else if ((maxdate = atoi(char_p)) == -1) {
			mindate = -1;
			warndate = -1;
		} else if (maxdate < -1)
				maxdate = 7 * MAXWEEKS;
			else
				maxdate *= 7;

		/* close defaults file */
		defopen(NULL);
	}

	dprintf2("turn: maxdate == %d, mindate == %d\n", maxdate, mindate);

	/* set up the attribute/value pairs, then call ia_set_authtokattr() */
	/* to change the attribute values				    */

	(void) sprintf(value, "%d", maxdate);
	setup_setattr(set_attribute, k++, "AUTHTOK_MAXAGE=", value);
	(void) sprintf(value, "%d", mindate);
	setup_setattr(set_attribute, k++, "AUTHTOK_MINAGE=", value);
	(void) sprintf(value, "%d", warndate);
	setup_setattr(set_attribute, k++, "AUTHTOK_WARNDATE=", value);
	setup_setattr(set_attribute, k, NULL, NULL);

	retcode = ia_set_authtokattr(iah, &set_attribute[0], &ia_status,
	    repository, nisdomain);
	free_setattr(set_attribute);
	return (retcode);
}


/*
 * get_newpasswd():
 * 	Get user's new password. It also does the syntax check for
 * 	the new password.
 */

static int
get_newpasswd(insistp, countp, pwbuf, usrname, prognamep, ia_convp, re)
	int *insistp;
	int *countp;
	char pwbuf[10];
	char *usrname;
	char *prognamep;
	struct ia_conv *ia_convp;
	int re;
{

	char			buf[10];
	int 			pwlen;
	int			tmpflag, flags;
	char			*p, *o;
	int			c;
	int 			i, j, k;	/* for triviality checks */
	static struct ia_response	*ret_resp;
	int			retcode;
	char 			messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	int 			num_msg;
	char			*pswd;
	struct passwd		*curr_pwd = NULL;
	struct spwd		*curr_sp = NULL;

	if (IS_FILES(re)) {
		curr_pwd = unix_pwd;
		curr_sp = unix_sp;
	} else if (IS_NIS(re)) {
		curr_pwd = nis_pwd;
		curr_sp = nis_sp;
	} else if (IS_NISPLUS(re)) {
		curr_pwd = nisplus_pwd;
		curr_sp = nisplus_sp;
	} else {
		(void) sprintf(messages[0],
		    "%s: System error: repository out of range\n", prognamep);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
	}

	/*
	 * nsswitch file may have two repositories, but it doesn't mean
	 * passwd entry will exist in both repositories.
	 * That's why cur_pwd could be null.
	 */
	if (curr_pwd == NULL || curr_sp == NULL)
		return (IA_NOENTRY);

	(void) sprintf(messages[0], dgettext(PAMTXD, "New password:"));
	num_msg = 1;
	retcode = get_authtok(ia_convp->start_conv, 0, num_msg,
				messages, NULL, &ret_resp);
	if (retcode != IA_SUCCESS)
		return (retcode);
	pswd = ret_resp->resp;
	if (pswd == NULL) {
		(void) sprintf(messages[0], dgettext(PAMTXD, "Sorry.\n"));
		(void) display_errmsg(ia_convp->cont_conv, 0, 1,
				messages, NULL);
		free_resp(num_msg, ret_resp);
		/* audit_passwd_main6(); */
		return (IA_FMERR);
	} else {
		(void) strcpy(pwbuf, pswd);
		pwlen = strlen(pwbuf);
		free_resp(num_msg, ret_resp);
	}

	/* Make sure new password is long enough */
	/* if not privileged user */

	mutex_lock(&_priv_lock);
	if (!((uid == 0 && IS_FILES(re)) || (privileged && IS_NISPLUS(re))) &&
	    (pwlen < MINLENGTH)) {
		(void) sprintf(messages[0], dgettext(PAMTXD,
			"Password is too short - must be "
			"at least 6 characters.\n"));
		retcode = display_errmsg(ia_convp->cont_conv, 0,
						1, messages, NULL);
		if (retcode != IA_SUCCESS) {
			mutex_unlock(&_priv_lock);
			return (retcode);
		}
		(*insistp)++;
		mutex_unlock(&_priv_lock);
		return (PAM_TRY_AGAIN);
	}
	mutex_unlock(&_priv_lock);

	/* Check the circular shift of the logonid */

	mutex_lock(&_priv_lock);
	if (!((uid == 0 && IS_FILES(re)) || (privileged && IS_NISPLUS(re))) &&
	    circ(usrname, pwbuf)) {
		(void) sprintf(messages[0], dgettext(PAMTXD,
			"Password cannot be circular shift of logonid.\n"));
		retcode = display_errmsg(ia_convp->cont_conv, 0,
						1, messages, NULL);
		if (retcode != IA_SUCCESS) {
			mutex_unlock(&_priv_lock);
			return (retcode);
		}
		(*insistp)++;
		mutex_unlock(&_priv_lock);
		return (PAM_TRY_AGAIN);
	}
	mutex_unlock(&_priv_lock);

	/* Insure passwords contain at least two alpha characters */
	/* and one numeric or special character */

	flags = 0;
	tmpflag = 0;
	p = pwbuf;
	mutex_lock(&_priv_lock);
	if (!((uid == 0 && IS_FILES(re)) || (privileged && IS_NISPLUS(re)))) {
		c = *p++;
		while (c != '\0') {
			if (isalpha(c) && tmpflag)
				flags |= 1;
			else if (isalpha(c) && !tmpflag) {
				flags |= 2;
				tmpflag = 1;
			} else if (isdigit(c))
				flags |= 4;
			else
				flags |= 8;
			c = *p++;
		}

		/*
		 * 7 = lca, lca, num
		 * 7 = lca, uca, num
		 * 7 = uca, uca, num
		 * 11 = lca, lca, spec
		 * 11 = lca, uca, spec
		 * 11 = uca, uca, spec
		 * 15 = spec, num, alpha, alpha
		 */

		if (flags != 7 && flags != 11 && flags != 15) {
			(void) sprintf(messages[0], dgettext(PAMTXD,
			"Password must contain at least "
			"two alphabetic characters and \n"
			"at least one numeric or special character.\n"));
			retcode = display_errmsg(ia_convp->cont_conv,
							0, 1, messages, NULL);
			if (retcode != IA_SUCCESS) {
				mutex_unlock(&_priv_lock);
				return (retcode);
			}
			(*insistp)++;
			mutex_unlock(&_priv_lock);
			return (PAM_TRY_AGAIN);
		}
	}
	mutex_unlock(&_priv_lock);

	mutex_lock(&_priv_lock);
	if (!((uid == 0 && IS_FILES(re)) || (privileged && IS_NISPLUS(re)))) {
		p = pwbuf;
		o = opwbuf;
		if (pwlen >= opwlen) {
			i = pwlen;
			k = pwlen - opwlen;
		} else {
			i = opwlen;
			k = opwlen - pwlen;
		}
		for (j = 1; j <= i; j++)
			if (*p++ != *o++)
				k++;
		if (k  <  3) {
			(void) sprintf(messages[0], dgettext(PAMTXD,
				"Passwords must differ "
				"by at least 3 positions \n"));
			retcode = display_errmsg(ia_convp->cont_conv, 0,
							1, messages, NULL);
			if (retcode != IA_SUCCESS) {
				mutex_unlock(&_priv_lock);
				return (retcode);
			}
			(*insistp)++;
			mutex_unlock(&_priv_lock);
			return (PAM_TRY_AGAIN);
		}
	}
	mutex_unlock(&_priv_lock);

	/* Ensure password was typed correctly, user gets three chances */

	(void) sprintf(messages[0],
	    dgettext(PAMTXD, "Re-enter new password:"));
	num_msg = 1;
	retcode = get_authtok(ia_convp->cont_conv, 0, num_msg,
				messages, NULL, &ret_resp);
	if (retcode != IA_SUCCESS)
		return (retcode);
	pswd = ret_resp->resp;

	if (pswd == NULL) {
		(void) sprintf(messages[0], dgettext(PAMTXD, "Sorry.\n"));
		(void) display_errmsg(ia_convp->cont_conv, 0, 1,
				messages, NULL);
		/* audit_passwd_main7(); */
		free_resp(num_msg, ret_resp);
		return (IA_FMERR);
	} else {
		(void) strcpy(buf, pswd);
		free_resp(num_msg, ret_resp);
	}

	if (strcmp(buf, pwbuf)) {
		if (++(*countp) > 2) {
			(void) sprintf(messages[0], dgettext(PAMTXD,
				"%s: Too many tries; try again later.\n"),
				prognamep);
			(void) display_errmsg(ia_convp->cont_conv, 0, 1,
					messages, NULL);
			/* audit_passwd_main8(); */
			return (IA_NOPERM);
		} else {
			(void) sprintf(messages[0], dgettext(PAMTXD,
					"They don't match; try again.\n"));
			retcode = display_errmsg(ia_convp->cont_conv, 0,
							1, messages, NULL);
			if (retcode != IA_SUCCESS)
				return (retcode);
		}
		/* audit_passwd_main9(); */
		return (PAM_TRY_AGAIN);
	}

	return (IA_SUCCESS);

}


/*
 * circ():
 * 	This function return 1 if string "t" is a circular shift of
 *	string "s", else it returns 0.
 */

static bool_t
circ(s, t)
	char *s, *t;
{
	char c, *p, *o, *r, buff[25], ubuff[25], pubuff[25];
	int i, j, k, l, m;

	m = 2;
	i = strlen(s);
	o = &ubuff[0];
	for (p = s; c = *p++; *o++ = c)
		if (islower(c))
			c = toupper(c);
	*o = '\0';
	o = &pubuff[0];
	for (p = t; c = *p++; *o++ = c)
		if (islower(c))
			c = toupper(c);

	*o = '\0';

	p = &ubuff[0];
	while (m--) {
		for (k = 0; k  <=  i; k++) {
			c = *p++;
			o = p;
			l = i;
			r = &buff[0];
			while (--l)
				*r++ = *o++;
			*r++ = c;
			*r = '\0';
			p = &buff[0];
			if (strcmp(p, pubuff) == 0)
				return (TRUE);
		}
		p = p + i;
		r = &ubuff[0];
		j = i;
		while (j--)
			*--p = *r++;
	}
	return (FALSE);
}

/*
 * verify that the given passwd decrypts the secret key
 */
static bool_t
verify_passwd(oldpass)
	char *oldpass;
{
	char oldsecret[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];

	(void) memcpy(oldsecret, curcryptsecret,
			HEXKEYBYTES + KEYCHECKSUMSIZE + 1);

	if (xdecrypt(oldsecret, oldpass) &&
		(memcmp(oldsecret, &(oldsecret[HEXKEYBYTES]),
				KEYCHECKSUMSIZE) == 0))
		return (TRUE);	/* successfully decrypted and */
				/* the decrypted key is correct */
	return (FALSE);
}
