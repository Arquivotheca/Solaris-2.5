#ifndef lint
static char sccsid[] = "@(#)audit_user.c 1.12 93/09/16 SMI";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

/*
 * Interfaces to audit_user(5)  (/etc/security/audit_user)
 */

#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <string.h>
#include <bsm/audit.h>
#include <bsm/libbsm.h>
#include <synch.h>

static char	au_user_fname[PATH_MAX] = AUDITUSERFILE;
static FILE *au_user_file = (FILE *) 0;
static mutex_t mutex_userfile = DEFAULTMUTEX;

#ifdef __STDC__
int	setauuserfile(char *fname)
#else
int	setauuserfile(fname)
	char	*fname;
#endif
{
	_mutex_lock(&mutex_userfile);
	if (fname) {
		(void) strcpy(au_user_fname, fname);
	}
	_mutex_unlock(&mutex_userfile);
	return (0);
}


void	setauuser()
{
	_mutex_lock(&mutex_userfile);
	if (au_user_file) {
		(void) fseek(au_user_file, 0L, 0);
	}
	_mutex_unlock(&mutex_userfile);
}


void	endauuser()
{
	_mutex_lock(&mutex_userfile);
	if (au_user_file) {
		(void) fclose(au_user_file);
		au_user_file = ((FILE *) 0);
	}
	_mutex_unlock(&mutex_userfile);
}

au_user_ent_t *
getauuserent()
{
	static au_user_ent_t au_user_entry;
	static char	logname[LOGNAME_MAX+1];

	/* initialize au_user_entry structure */
	au_user_entry.au_name = logname;

	return (getauuserent_r(&au_user_entry));

}

au_user_ent_t *
getauuserent_r(au_user_ent_t *au_user_entry)
{
	int	i, error = 0, found = 0;
	char	*s, input[256];

	_mutex_lock(&mutex_userfile);

	/* open audit user file if it isn't already */
	if (!au_user_file)
		if (!(au_user_file = fopen(au_user_fname, "r"))) {
			_mutex_unlock(&mutex_userfile);
			return ((au_user_ent_t *) 0);
		}

	while (fgets(input, 256, au_user_file)) {
		if (input[0] != '#') {
			found = 1;
			s = input;

			/* parse login name */
			i = strcspn(s, ":");
			s[i] = '\0';
			(void) strncpy(au_user_entry->au_name, s, LOGNAME_MAX);
			s = &s[i+1];

			/* parse first mask */
			i = strcspn(s, ":");
			s[i] = '\0';
			if (getauditflagsbin(s,
			    &au_user_entry->au_always) < 0)
				error = 1;
			s = &s[i+1];

			/* parse second mask */
			i = strcspn(s, "\n\0");
			s[i] = '\0';
			if (getauditflagsbin(s,
			    &au_user_entry->au_never) < 0)
				error = 1;

			break;
		}
	}
	_mutex_unlock(&mutex_userfile);

	if (!error && found) {
		return (au_user_entry);
	} else {
		return ((au_user_ent_t *) 0);
	}
}


#ifdef __STDC__
au_user_ent_t *getauusernam(char *name)
#else
au_user_ent_t *getauusernam(name)
	char *name;
#endif
{
	static au_user_ent_t u;
	static char	logname[LOGNAME_MAX+1];

	/* initialize au_user_entry structure */
	u.au_name = logname;

	return (getauusernam_r(&u, name));
}

#ifdef __STDC__
au_user_ent_t *getauusernam_r(au_user_ent_t *u, char *name)
#else
au_user_ent_t *getauusernam_r(u, name)
	au_user_ent_t *u;
	char *name;
#endif
{
	while (getauuserent_r(u) != NULL) {
		if (strcmp(u->au_name, name) == 0) {
			return (u);
		}
	}
	return ((au_user_ent_t *)NULL);
}
