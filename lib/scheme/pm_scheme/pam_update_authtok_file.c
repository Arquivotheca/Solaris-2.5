
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)pam_update_authtok_file.c 1.14     94/12/01 SMI"

#include "pam_headers.h"


static int	passwd_flag = 0;	/* attrs in shadow or passwd file */

static int	update_spent();
static int	process_passwd();

/*
 * update_authtok_file():
 * 	To update the authentication token file.
 *
 *	This function is called by either ia_set_authtokattr() to
 *	update the token attributes or ia_chauthtok() to update the
 *	authentication token.  The parameter "field" has to be specified
 * 	as "attr" if the caller wants to update token attributes, and
 *	the attribute-value pairs to be set needs to be passed in by parameter
 * 	"data".  If the function is called to update authentication
 *	token itself, then "field" needs to be specified as "passwd"
 * 	and the new authentication token has to be passed in by "data".
 */

int
update_authtok_file(prognamep, usrname, field, data, ia_convp)
	char *prognamep;
	char *usrname;
	char *field;
	char *data[];
	struct ia_conv *ia_convp;
{
	struct stat	buf;
	register int	found;
	FILE 		*tsfp, *spfp;
	char 		messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	int		sp_error;
	int		retcode;

	/* This will be the last update (after nis/nis+) */
	if (seteuid(0) == -1) {
		(void) fprintf(stderr,
		    "Can't become super user to update passwd file\n");
		return (IA_FATAL);
	}

	found = 0;

	/* lock the password file */
	if (lckpwdf() != 0) {
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FB);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		return (IA_FBUSY);
	}

	/* Mode  of the shadow file should be 400 or 000 */
	if (stat(SHADOW, &buf) < 0) {
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}

	(void) umask(S_IAMB & ~(buf.st_mode & S_IRUSR));
	if ((tsfp = fopen(SHADTEMP, "w")) == NULL) {
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}

	/*
	 *	copy passwd  files to temps, replacing matching lines
	 *	with new password attributes.
	 */

	errno = 0;
	sp_error = 0;

	if ((spfp = fopen(SHADOW, "r")) == NULL) {
			(void) ulckpwdf();
			return (IA_FMERR);
	}
	while ((unix_sp = fgetspent(spfp)) != NULL) {
		if (strcmp(unix_sp->sp_namp, usrname) == 0) {
			found = 1;
			retcode = update_spent(field, data, ia_convp);
			if (retcode != IA_SUCCESS) {
				(void) unlink(SHADTEMP);
				(void) ulckpwdf();
				return (retcode);
			}
			if (passwd_flag) {
				/* The attributes are in passwd file */
				(void) unlink(SHADTEMP);
				retcode = process_passwd(prognamep, usrname,
				    ia_convp);

				if (retcode != IA_SUCCESS) {
					(void) unlink(SHADTEMP);
					(void) ulckpwdf();
					return (retcode);
				}

				(void) ulckpwdf();
				return (IA_SUCCESS);
			}
		}
		if (putspent(unix_sp, tsfp) != 0) {
			(void) unlink(SHADTEMP);
			sprintf(messages[0], dgettext(PAMTXD,
				"%s: %s\n"),
				prognamep, MSG_FE);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
					messages, NULL);
			(void) ulckpwdf();
			return (IA_FMERR);
		}

	} /* end of while */

	if (sp_error >= 1) {
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: Bad entry found in the password file.\n"),
			prognamep);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
	}

	if ((fclose(tsfp)) || (fclose(spfp))) {
		(void) unlink(SHADTEMP);
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}


	/* Check if user name exists */
	if (found == 0) {
		(void) unlink(SHADTEMP);
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}

	/*
	 *	Rename temp file back to  appropriate passwd file.
	 */

	/* remove old passwd file */
	if (unlink(OSHADOW) && access(OSHADOW, 0) == 0) {
		(void) unlink(SHADTEMP);
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}

	/* rename password file to old password file */
	if (rename(SHADOW, OSHADOW) == -1) {
		(void) unlink(SHADTEMP);
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}

	/* rename temparory password file to password file */
	if (rename(SHADTEMP, SHADOW) == -1) {
		(void) unlink(SHADOW);
		if (link(OSHADOW, SHADOW)) {
			sprintf(messages[0], dgettext(PAMTXD,
				"%s: %s\n"),
				prognamep, MSG_FF);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
					messages, NULL);
			(void) ulckpwdf();
			return (IA_FATAL);
		}
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}

	(void) ulckpwdf();
	return (IA_SUCCESS);
}


/*
 * update_spent():
 * 	To update a shadow password file entry in the Unix
 *	authentication token file.
 *	This function is called by update_authtok_file() to
 *	update the token to token attributes.
 *	The parameter "field" indicates whenther token attributes or
 *	token itself will be changes, and the parameter "data" has
 *	the new values for the attributes or token.
 *
 */

static int
update_spent(field, data, ia_convp)
	char *field;
	char **data;
	struct ia_conv *ia_convp;
{
	char		*value;
	char		*char_p;
	static char	**data_p;
	char		*pw;
	int		mindate;
	int		maxdate;
	int		warndate;
	static char	nullstr[] = "";
	static char	lkstring[] = "*LK*";	/* lock string  to lock */
						/*  user's password */
	char 		messages[MAX_NUM_MSG][MAX_MSG_SIZE];

	data_p = data;

	if (strcmp(field, "attr") == 0) {
		while (*data != NULL) {
			/* check attribute: AUTHTOK_DEL */
			if ((value =
				attr_match("AUTHTOK_DEL", *data)) != NULL) {
				if (strcmp(value, "1") == 0) {

					/* delete password */
					unix_sp->sp_pwdp = nullstr;

					/*
					 * set "AUTHTOK_EXT" will clear
					 * the sp_lstchg field. We do not
					 * want sp_lstchg field to be set
					 * if one execute passwd -d -f
					 * name	or passwd -l -f name.
					 */
					if (attr_find("AUTHTOK_EXP",
					    data_p) == FALSE)
						unix_sp->sp_lstchg = DAY_NOW;
				}
				data++;
				continue;
			}

			/* check attribute: AUTHTOK_LK */
			if ((value =
				attr_match("AUTHTOK_LK", *data)) != NULL) {
				if (strcmp(value, "1") == 0) {
					/* lock password */
					pw = &lkstring[0];
					unix_sp->sp_pwdp = pw;
					if (attr_find("AUTHTOK_EXP",
					    data_p) == FALSE)
						unix_sp->sp_lstchg = DAY_NOW;
				}
				data++;
				continue;
			}

			/* check attribute: AUTHTOK_EXP */
			if ((value =
				attr_match("AUTHTOK_EXP", *data)) != NULL) {
				if (strcmp(value, "1") == 0) {
					/* expire password */
					unix_sp->sp_lstchg = (long) 0;
				}
				data++;
				continue;
			}

			/* check attribute: AUTHTOK_MAXAGE */
			if ((value =
				attr_match("AUTHTOK_MAXAGE", *data)) != NULL) {
				/* set max field */
				maxdate = (int)strtol(value, &char_p, 10);
				if ((attr_find("AUTHTOK_MINAGE", data_p) ==
				    FALSE) && unix_sp->sp_min == -1)
					unix_sp->sp_min = 0;
				if (maxdate == -1) {	/* turn off aging */
					unix_sp->sp_min = -1;
					unix_sp->sp_warn = -1;
				} else if (unix_sp->sp_max == -1)
					/*
					 * It was set to 0 before. That
					 * will force passwd change at the
					 * next login. There are several
					 * ways to force passwd change. I don't
					 * think turning on aging should imply
					 * that.
					 */
					unix_sp->sp_lstchg = DAY_NOW;

				unix_sp->sp_max = maxdate;
				data++;
				continue;
			}

			/* check attribute: AUTHTOK_MINAGE */
			if ((value =
				attr_match("AUTHTOK_MINAGE", *data)) != NULL) {
				/* set min field */
				mindate = (int)strtol(value, &char_p, 10);
				if ((attr_find("AUTHTOK_MAXAGE", data_p) ==
				    FALSE) &&
				    unix_sp->sp_max == -1 && mindate != -1) {
					(void) unlink(SHADTEMP);
					(void) ulckpwdf();
					return (IA_BADAGE);
				}
				unix_sp->sp_min = mindate;
				data++;
				continue;
			}

			/* check attribute: AUTHTOK_WARNDATE */
			if ((value =
				attr_match
				("AUTHTOK_WARNDATE", *data)) != NULL) {
				/* set warn field */
				warndate = (int)strtol(value, &char_p, 10);
				if (unix_sp->sp_max == -1 && warndate != -1) {
					(void) unlink(SHADTEMP);
					return (IA_BADAGE);
				}
				unix_sp->sp_warn = warndate;
				data++;
				continue;
			}

			/* new shell */
			if ((value = attr_match("AUTHTOK_SHELL", *data))
			    != NULL) {
				if (unix_pwd == NULL) {
					sprintf(messages[0],
					    "No local passwd record\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				unix_pwd->pw_shell =
				    getloginshell(unix_pwd->pw_shell);
				if (unix_pwd->pw_shell == NULL)
					return (IA_FATAL);
				passwd_flag = 1;
				data++;
				continue;
			}

			/* new homedir */
			if ((value = attr_match("AUTHTOK_HOMEDIR", *data))
			    != NULL) {
				if (unix_pwd == NULL) {
					sprintf(messages[0],
					    "No local passwd record\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				unix_pwd->pw_dir =
				    gethomedir(unix_pwd->pw_dir);
				if (unix_pwd->pw_dir == NULL)
					return (IA_FATAL);
				passwd_flag = 1;
				data++;
				continue;
			}

			/* new gecos */
			if ((value = attr_match("AUTHTOK_GECOS", *data))
			    != NULL) {
				if (unix_pwd == NULL) {
					sprintf(messages[0],
					    "No local passwd record\n");
					(void) display_errmsg(
					    ia_convp->start_conv, 0, 1,
					    messages, NULL);
					return (IA_FATAL);
				}
				unix_pwd->pw_gecos =
				    getfingerinfo(unix_pwd->pw_gecos);
				if (unix_pwd->pw_gecos == NULL)
					return (IA_FATAL);
				passwd_flag = 1;
				data++;
				continue;
			}
		}
	} else {
		if (strcmp(field, "passwd") == 0) { /* change password */
			unix_sp->sp_pwdp = *data_p;
			/* update the last change field */
			unix_sp->sp_lstchg = DAY_NOW;
			if (unix_sp->sp_max == 0) {   /* turn off aging */
				unix_sp->sp_max = -1;
				unix_sp->sp_min = -1;
			}
		}
	}
	return (IA_SUCCESS);
}

/*
 * shell, homedir and gecos are in passwd file. The update is modeled
 * after shadow file.
 */
static int
process_passwd(prognamep, usrname, ia_convp)
	char 		*prognamep;
	char 		*usrname;
	struct ia_conv 	*ia_convp;
{
	FILE		*tpfp;	/* tmp passwd file pointer */
	FILE		*pwfp;	/* passwd file pointer */
	char		messages[MAX_NUM_MSG][MAX_MSG_SIZE];
	struct passwd	*unix_p;
	struct passwd	unix_tmp;
	int		found;
	char		*buf;

	/*
	 * allocate 4k: should be large enough
	 * 1k for gecos, 1k for shell, 1k for homedir, 1k for the rest
	 */
	buf = malloc(4 * BUFSIZ);
	if (buf == NULL) {
		sprintf(messages[0], "Can't allocate memory\n");
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
		return (IA_FATAL);
	}

	if ((tpfp = fopen(PASSTEMP, "w")) == NULL) {
		sprintf(messages[0], dgettext(PAMTXD,
		    "%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
		    messages, NULL);
		(void) ulckpwdf();
		free(buf);
		return (IA_FMERR);
	}

	if ((pwfp = fopen(PASSWD, "r")) == NULL) {
		(void) ulckpwdf();
		free(buf);
		return (IA_FMERR);
	}


	while ((unix_p = fgetpwent_r(pwfp, &unix_tmp, buf, 4*BUFSIZ)) != NULL) {
		if (strcmp(unix_p->pw_name, usrname) == 0) {
			found = 1;
			unix_p->pw_gecos = unix_pwd->pw_gecos;
			unix_p->pw_dir = unix_pwd->pw_dir;
			unix_p->pw_shell = unix_pwd->pw_shell;
		}
		if (putpwent(unix_p, tpfp) != 0) {
			(void) unlink(PASSTEMP);
			sprintf(messages[0], dgettext(PAMTXD,
				"%s: %s\n"),
				prognamep, MSG_FE);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
					messages, NULL);
			(void) ulckpwdf();
			free(buf);
			return (IA_FMERR);
		}

	} /* end of while */

	if ((fclose(tpfp)) || (fclose(pwfp))) {
		(void) unlink(PASSTEMP);
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}

	/* Check if user name exists */
	if (found == 0) {
		(void) unlink(PASSTEMP);
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}

	/*
	 *	Rename temp file back to  appropriate passwd file.
	 */

	/* remove old passwd file */
	if (unlink(OPASSWD) && access(OPASSWD, 0) == 0) {
		(void) unlink(PASSTEMP);
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}

	/* rename password file to old password file */
	if (rename(PASSWD, OPASSWD) == -1) {
		(void) unlink(PASSTEMP);
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		return (IA_FMERR);
	}

	/* rename temporary password file to password file */
	if (rename(PASSTEMP, PASSWD) == -1) {
		(void) unlink(PASSWD);
		if (link(OPASSWD, PASSWD)) {
			sprintf(messages[0], dgettext(PAMTXD,
				"%s: %s\n"),
				prognamep, MSG_FF);
			(void) display_errmsg(ia_convp->start_conv, 0, 1,
					messages, NULL);
			(void) ulckpwdf();
			free(buf);
			return (IA_FATAL);
		}
		sprintf(messages[0], dgettext(PAMTXD,
			"%s: %s\n"), prognamep, MSG_FE);
		(void) display_errmsg(ia_convp->start_conv, 0, 1,
				messages, NULL);
		(void) ulckpwdf();
		free(buf);
		return (IA_FMERR);
	}

	(void) chmod(PASSWD, 0644);
	(void) ulckpwdf();
	return (IA_SUCCESS);
}
