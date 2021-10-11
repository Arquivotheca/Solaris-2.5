
/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident	"@(#)ia_framework.c 1.37	95/02/17 SMI"	/*	*/

#include <syslog.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utmpx.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include <security/ia_appl.h>
#include <security/ia_schemes.h>
#include <security/ia_switch.h>

#define	dlopen 	_dlopen
#define	dlclose _dlclose
#define	dlsym 	_dlsym
#define	dlerror _dlerror

/*	PAM debugging	*/

#define	PAM_DEBUG	"/usr/lib/security/pam_debug"

int pam_debug = 0;

static struct scheme scheme;		/* The scheme structure */

/* Function declaration */

/* lint: used by unix scheme */
int		sa_getall();
int		ia_set_item();
int		ia_get_item();

static struct pam_sdata	*alloc_pam_buf();
static int	istrlen();
static int	open_scheme();
static int	load_function();
static void	clean_up();

/*
 * load_scheme		- Open and load the specified scheme, storing the
 *			  address of the function at the address of **func.
 *			  This code assumes that func is an address in
 *			  the scheme structure.  Note that *func
 *			  also gets cleared.  Also note that the code
 *			  performs recovery by copying over the scheme structure
 *			  if a dlopen or dlsym failure occurs.
 */

static int
load_scheme(type)
	int  type;
{
	int  error;
	int  err;

	if (pam_debug)
		syslog(LOG_DEBUG, "load_scheme(%d)\n", type);

	/*
	 * Check in two places for the scheme (usr/lib/security or /etc/lib)
	 * (In single user mode /usr might not be mounted) and load all
	 * scheme functions in, if open scheme succeeds.  Use default scheme
	 * if the scheme failed on dlopen() or if any one of the scheme
	 * function address cannot be resolved.
	 */

	err = IA_SUCCESS;

	switch (type) {
	case AM_SCHEME:
		if ((error = open_scheme(AM_SCHEME_LIB)) == 0 ||
		    (error = open_scheme(AM_SCHEME_ETC)) == 0) {
			err = load_function(SA_AUTH_USER,
				&scheme.sa_auth_user);
			if (err != IA_SUCCESS)
				return (err);
			err = load_function(SA_SETCRED,
				&scheme.sa_setcred);
			if (err != IA_SUCCESS)
				return (err);

			if (pam_debug)
				syslog(LOG_DEBUG, "AM_SCHEME load success\n");

			return (IA_SUCCESS);
		}

		if (pam_debug)
			syslog(LOG_DEBUG, "AM_SCHEME load failure\n");
		break;

	case EM_SCHEME:
		if ((error = open_scheme(EM_SCHEME_LIB)) == 0 ||
		    (error = open_scheme(EM_SCHEME_ETC)) == 0) {
			err = load_function(SA_AUTH_ACCTMG,
				&scheme.sa_auth_acctmg);
			if (err != IA_SUCCESS)
				return (err);

			if (pam_debug)
				syslog(LOG_DEBUG, "EM_SCHEME load success\n");

			return (IA_SUCCESS);
		}

		if (pam_debug)
			syslog(LOG_DEBUG, "EM_SCHEME load failure\n");
		break;

	case PM_SCHEME:
		if ((error = open_scheme(PM_SCHEME_LIB)) == 0 ||
		    (error = open_scheme(PM_SCHEME_ETC)) == 0) {
			err = load_function(SA_SET_AUTHTOKATTR,
				&scheme.sa_set_authtokattr);
			if (err != IA_SUCCESS)
				return (err);
			err = load_function(SA_GET_AUTHTOKATTR,
				&scheme.sa_get_authtokattr);
			if (err != IA_SUCCESS)
				return (err);
			err = load_function(SA_CHAUTHTOK,
				&scheme.sa_chauthtok);
			if (err != IA_SUCCESS)
				return (err);

			if (pam_debug)
				syslog(LOG_DEBUG, "PM_SCHEME load success\n");

			return (IA_SUCCESS);
		}

		if (pam_debug)
			syslog(LOG_DEBUG, "PM_SCHEME load failure\n");
		break;

	case SM_SCHEME:
		if ((error = open_scheme(SM_SCHEME_LIB)) == 0 ||
		    (error = open_scheme(SM_SCHEME_ETC)) == 0) {
			err = load_function(SA_OPEN_SESSION,
				&scheme.sa_open_session);
			if (err != IA_SUCCESS)
				return (err);
			err = load_function(SA_CLOSE_SESSION,
				&scheme.sa_close_session);
			if (err != IA_SUCCESS)
				return (err);

			if (pam_debug)
				syslog(LOG_DEBUG, "SM_SCHEME load success\n");

			return (IA_SUCCESS);
		}

		if (pam_debug)
			syslog(LOG_DEBUG, "SM_SCHEME load failure\n");
		break;

	case XM_SCHEME:
		if ((error = open_scheme(XM_SCHEME_LIB)) == 0 ||
		    (error = open_scheme(XM_SCHEME_ETC)) == 0) {
			err = load_function(SA_AUTH_NETUSER,
				&scheme.sa_auth_netuser);
			if (err != IA_SUCCESS)
				return (err);
			err = load_function(SA_AUTH_PORT,
				&scheme.sa_auth_port);
			if (err != IA_SUCCESS)
				return (err);

			if (pam_debug)
				syslog(LOG_DEBUG, "XM_SCHEME load success\n");

			return (IA_SUCCESS);
		}

		if (pam_debug)
			syslog(LOG_DEBUG, "XM_SCHEME load failure\n");
		break;

	default:
		return (IA_DLFAIL);
	}

	return (error);
}


/*
 * open_scheme		- Open the scheme AUTH_SCHEME, first checking for
 *			  propers modes and ownerships on the file.
 */


static int
open_scheme(scheme_so)
	char *scheme_so;
{
	struct stat stb;
	char *errmsg;

	if (pam_debug)
		syslog(LOG_DEBUG, "open_scheme(%s)", scheme_so);

	/*
	 * Stat the file so we can check modes and ownerships
	 */
	if (stat(scheme_so, &stb) < 0) {
		if (pam_debug)
			syslog(LOG_DEBUG, "open_scheme: stat() failed\n");
		return (1);
	}

	/*
	 * Check the ownership of the file
	 */
	if (stb.st_uid != (uid_t)0) {
		if (pam_debug)
			syslog(LOG_DEBUG, "open_scheme: owner not root\n");
		return (1);
	}

	/*
	 * Check the modes on the file
	 */

	if (stb.st_mode&S_IWGRP) {
		if (pam_debug)
			syslog(LOG_DEBUG,
				"open_scheme: file writeable by group\n");
		return (1);
	}

	if (stb.st_mode&S_IWOTH) {

		if (pam_debug)
			syslog(LOG_DEBUG,
				"open_scheme: file writeable by others\n");
		return (1);
	}

	/*
	 * Perform the dlopen()
	 */

	scheme.handle = (void *) dlopen(scheme_so, RTLD_LAZY);
	if (scheme.handle == NULL) {
		errmsg = (char *) dlerror();

		if (pam_debug)
			syslog(LOG_DEBUG, "open_scheme: dlopen() failed\n");
		return (1);
	}
	return (0);
}

/*
 * load_function -	Call dlsym() to resolve the function address
 */

static int
load_function(name, func)
	char	*name;
	int	(**func)();
{
	char *errmsg = NULL;

	if (pam_debug)
		syslog(LOG_DEBUG, "load_function(%s)\n", name);

	*func = (int (*)())dlsym(scheme.handle, name);
	if (*func == NULL) {
		errmsg = (char *) dlerror();
		if (pam_debug)
			syslog(LOG_DEBUG, "load_function: dlsym() failed, %s\n",
		    errmsg != NULL ? errmsg : "");
		return (IA_SYMFAIL);
	}
	return (IA_SUCCESS);
}

/*
 *			ia_XXXXX routines
 *
 *	These are the entry points to the authentication switch
 *	When adding a new ia_ function add it here.
 */

int
ia_set_authtokattr(iah, sa_setattr, ia_statusp, rep, domain)
	void			*iah;
	char			**sa_setattr;
	struct ia_status 	*ia_statusp;
	int			rep;
	char			*domain;
{
	int 	error;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_set_authtokattr()\n");

	ia_statusp->iast_status = 0;
	(void) memset((void *) ia_statusp->iast_info, 0, 128);

	if (scheme.sa_set_authtokattr == NULL)
		if ((error = load_scheme(PM_SCHEME)) != IA_SUCCESS) {
			scheme.sa_set_authtokattr = NULL;
			return (error);
		}

	error = (scheme.sa_set_authtokattr(iah, sa_setattr, ia_statusp, rep,
	    domain));

	return (error);
}

int
ia_get_authtokattr(iah, ga_getattr, ia_statusp, rep, domain)
	void			*iah;
	char			***ga_getattr;
	struct ia_status 	*ia_statusp;
	int			rep;
	char			*domain;
{
	int 	error;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_get_authtokattr()\n");

	ia_statusp->iast_status = 0;
	(void) memset((void *) ia_statusp->iast_info, 0, 128);

	if (scheme.sa_get_authtokattr == NULL)
		if ((error = load_scheme(PM_SCHEME)) != IA_SUCCESS) {
			scheme.sa_get_authtokattr = NULL;
			return (error);
		}

	error = (scheme.sa_get_authtokattr(iah, ga_getattr, ia_statusp, rep,
	    domain));

	return (error);
}

int
ia_chauthtok(iah, ia_statusp, rep, domain)
	void			*iah;
	struct ia_status 	*ia_statusp;
	int			rep;
	char			*domain;
{
	int 	error;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_chauthtok()\n");

	ia_statusp->iast_status = 0;
	(void) memset((void *) ia_statusp->iast_info, 0, 128);

	if (scheme.sa_chauthtok == NULL)
		if ((error = load_scheme(PM_SCHEME)) != IA_SUCCESS) {
			scheme.sa_chauthtok = NULL;
			return (error);
		}

	error = (scheme.sa_chauthtok(iah, ia_statusp, rep, domain));

	return (error);
}

int
ia_auth_netuser(iah, ruser, ia_statusp)
	void	*iah;
	char	*ruser;
	struct ia_status *ia_statusp;
{
	int 	error;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_auth_netuser()\n");

	ia_statusp->iast_status = 0;
	(void) memset((void *) ia_statusp->iast_info, 0, 128);

	if (scheme.sa_auth_netuser == NULL)
		if ((error = load_scheme(XM_SCHEME)) != IA_SUCCESS) {
			scheme.sa_auth_netuser = NULL;
			return (error);
		}

	error = (scheme.sa_auth_netuser(iah, ruser, ia_statusp));

	return (error);
}

int
ia_auth_user(iah, flags, pwd, ia_statusp)
	void	*iah;
	int	flags;
	struct 	passwd	**pwd;
	struct ia_status *ia_statusp;
{
	int 	error;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_auth_user(%x, %x, %x, %x)\n",
			iah, flags, pwd, ia_statusp);

	ia_statusp->iast_status = 0;
	(void) memset((void *) ia_statusp->iast_info, 0, 128);

	if (scheme.sa_auth_user == NULL)
		if ((error = load_scheme(AM_SCHEME)) != IA_SUCCESS) {
			scheme.sa_auth_user = NULL;
			return (error);
		}

	error = (scheme.sa_auth_user(iah, flags, pwd, ia_statusp));

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_auth_user: error %d\n", error);

	return (error);
}

int
ia_auth_port(iah, flags, ia_statusp)
	void	*iah;
	int	flags;
	struct	ia_status *ia_statusp;
{
	int 	error;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_auth_port()\n");

	ia_statusp->iast_status = 0;
	(void) memset((void *) ia_statusp->iast_info, 0, 128);

	if (scheme.sa_auth_port == NULL)
		if ((error = load_scheme(XM_SCHEME)) != IA_SUCCESS) {
			scheme.sa_auth_port = NULL;
			return (error);
		}

	error = (scheme.sa_auth_port(iah, flags, ia_statusp));

	return (error);
}

int
ia_auth_acctmg(iah, flags, pwd, ia_statusp)
	void	*iah;
	int 	flags;
	struct  passwd	**pwd;
	struct 	ia_status *ia_statusp;
{
	int 	error;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_auth_acctmg()\n");

	ia_statusp->iast_status = 0;
	(void) memset((void *) ia_statusp->iast_info, 0, 128);

	if (scheme.sa_auth_acctmg == NULL)
		if ((error = load_scheme(EM_SCHEME)) != IA_SUCCESS) {
			scheme.sa_auth_acctmg = NULL;
			return (error);
		}

	error = (scheme.sa_auth_acctmg(iah, flags, pwd, ia_statusp));

	return (error);
}

#define	INIT_PROC_PID	1
#define	SCPYN(a, b)	(void) strncpy(a, b, sizeof (a))
#define	min(a, b)	(((a) < (b)) ? (a) : (b))

int
ia_open_session(iah, flags, type, id, ia_statusp)
	void	*iah;
	int	flags;
	int	type;
	char	id[];
	struct 	ia_status	*ia_statusp;
{
	int		error;
	int		tmplen;
	struct utmpx    *u = (struct utmpx *)0;
	struct utmpx	utmp;
	char	    *ttyntail;
	char	    *program, *user, *ttyn, *rhost;
	struct ia_conv  *ia_convp;
	int		err = 0;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_open_session(%d)\n", type);

	ia_statusp->iast_status = 0;
	(void) memset((void *) ia_statusp->iast_info, 0, 128);

	if (scheme.sa_open_session == NULL)
		if ((error = load_scheme(SM_SCHEME)) != IA_SUCCESS) {
			scheme.sa_open_session = NULL;
			return (error);
		}

	if (error = (scheme.sa_open_session(iah, flags, type, id, ia_statusp)))
		return (error);

	(void) memset((void *)&utmp, 0, sizeof (utmp));

	if ((err = sa_getall(iah, &program, &user, &ttyn, &rhost, &ia_convp))
		    != IA_SUCCESS)
		return (err);

	(void) time(&utmp.ut_tv.tv_sec);
	utmp.ut_pid = getpid();
	if (rhost != NULL && rhost[0] != '\0') {
		(void) strcpy(utmp.ut_host, rhost);
		utmp.ut_syslen = (tmplen = strlen(rhost)) ?
		    min(tmplen + 1, sizeof (utmp.ut_host)) : 0;
	} else {
		(void) memset(utmp.ut_host, 0, sizeof (utmp.ut_host));
		utmp.ut_syslen = 0;
	}

	(void) strcpy(utmp.ut_user, user);
	/*
	 * Copy in the name of the tty minus the "/dev/" if a /dev/ is
	 * in the path name.
	 */

	if (!(flags&IS_LOGIN))
		SCPYN(utmp.ut_line, ttyn);

	ttyntail = ttyn;

	utmp.ut_type = type;

	if (id != NULL)
		(void) memcpy(utmp.ut_id, id, sizeof (utmp.ut_id));

	if (flags == IS_NOLOG) {
		updwtmpx(WTMPX_FILE, &utmp);
	} else {
		/*
		 * Go through each entry one by one, looking only at INIT,
		 * LOGIN or USER Processes.  Use the entry found if flags == 0
		 * and the line name matches, or if the process ID matches if
		 * the UPDATE_ENT flag is set.  The UPDATE_ENT flag is mainly
		 * for login which normally only wants to update an entry if
		 * the pid fields matches.
		 */

		if (flags & IS_LOGIN) {
		    while ((u = getutxent()) != NULL) {
			if ((u->ut_type == INIT_PROCESS ||
			    u->ut_type == LOGIN_PROCESS ||
			    u->ut_type == USER_PROCESS) &&
			    ((flags == IS_LOGIN && ttyn != NULL &&
					strncmp(u->ut_line, ttyntail,
						sizeof (u->ut_line)) == 0) ||
			    u->ut_pid == utmp.ut_pid)) {
				if (ttyn)
					SCPYN(utmp.ut_line,
					    (ttyn + sizeof ("/dev/") - 1));
				if (id == NULL) {
					(void) memcpy(utmp.ut_id, u->ut_id,
						    sizeof (utmp.ut_id));
				}
				(void) pututxline(&utmp);
				break;
			}
		    }	   /* end while */
		    endutxent();		/* Close utmp file */
		}

		if (u == (struct utmpx *)NULL) {
			/* audit_login_main11(); */
			if (flags & IS_UPDATE_ENT)
				err =  IA_NOENTRY;
			else
				(void) makeutx(&utmp);
		}
		else
			updwtmpx(WTMPX_FILE, &utmp);
	}
	return (err);
}

int
ia_close_session(iah, flags, pid, status, id, ia_statusp)
	void	*iah;
	int	flags;
	pid_t	pid;
	int	status;
	char	id[];
	struct 	ia_status	*ia_statusp;
{
	int 	error;
	struct utmpx *up;
	struct utmpx  ut;
	int	   err = 0;
	char	 *ttyn;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_close_session()\n");

	ia_statusp->iast_status = 0;
	(void) memset(ia_statusp->iast_info, 0, 128);


	if (scheme.sa_close_session == NULL)
		if ((error = load_scheme(SM_SCHEME)) != IA_SUCCESS) {
			scheme.sa_close_session = NULL;
			return (error);
		}

	/*
	 * do we want to do any utmp processing?
	 */
	if (flags & IS_NOOP) {
		if (error = (scheme.sa_close_session(iah, flags, pid, status,
							id, ia_statusp))) {
			return (error);
		}
		return (IA_SUCCESS);
	}

	if (pid == 0)
		pid = (int) getpid();

	if (flags == IS_NOLOG) {	/* only write to wtmp files */
			/* clear utmpx entry */
		(void) memset((char *)&ut, 0, sizeof (ut));

		if (id != NULL)
			(void) memcpy(ut.ut_id, id, sizeof (ut.ut_id));

		if ((err = ia_get_item(iah, IA_TTYN, (void **) &ttyn)) < 0)
			return (-err);

		if (ttyn != NULL && *ttyn != '\0') {
			if (strstr(ttyn, "/dev/") != NULL)
			(void) strncpy(ut.ut_line, (ttyn+sizeof ("/dev/")-1),
							sizeof (ut.ut_line));
			else
				(void) strncpy(ut.ut_line, ttyn,
							sizeof (ut.ut_line));
		}
		ut.ut_pid  = pid;
		ut.ut_type = DEAD_PROCESS;
		ut.ut_exit.e_termination = 0;
		ut.ut_exit.e_exit = 0;
		ut.ut_syslen = 1;
		(void) gettimeofday(&ut.ut_tv, NULL);
		updwtmpx(WTMPX_FILE, &ut);
		if (error = (scheme.sa_close_session(iah, flags, pid, status,
							id, ia_statusp))) {
			if (pam_debug)
				syslog(LOG_DEBUG,
				"ia_close: close session error: %d\n",
				error);

			return (error);
		}
		return (IA_SUCCESS);
	} else {
		setutxent();
		while (up = getutxent()) {
			if (up->ut_pid == pid) {
				if (up->ut_type == DEAD_PROCESS) {
					/*
					 * Cleaned up elsewhere.
					 */
					endutxent();
					return (0);
				}
				(void) ia_set_item(iah, IA_USER, up->ut_user);
				(void) ia_set_item(iah, IA_TTYN, up->ut_line);
				(void) ia_set_item(iah, IA_RHOST, up->ut_host);
				up->ut_type = DEAD_PROCESS;
				up->ut_exit.e_termination = status & 0xff;
				up->ut_exit.e_exit = (status >> 8) & 0xff;
				if (id != NULL)
					(void) memcpy(up->ut_id, id,
						sizeof (up->ut_id));
				(void) time(&up->ut_tv.tv_sec);

				/*
				 * For init (Process ID 1) we don't write to
				 * init's pipe, since we are init.
				 */
				if (getpid() == INIT_PROC_PID) {
					(void) pututxline(up);
					/*
					 * Now attempt to add to the end of the
					 * wtmp and wtmpx files.  Do not create
					 * if they don't already exist.
					 */
					updwtmpx("wtmpx", up);
				} else {
					if (modutx(up) == NULL) {
						syslog(LOG_INFO,
							    "\tmodutx failed");
						/*
						 * Since modutx failed we'll
						 * write out the new entry
						 * ourselves.
						 */
						(void) pututxline(up);
						updwtmpx("wtmpx", up);
					}
				}

				endutxent();
				if (error = (scheme.sa_close_session(iah, flags,
						pid, status, id, ia_statusp))) {
					if (pam_debug)
						syslog(LOG_DEBUG,
					"ia_close: close session error: %d\n",
						error);

					return (error);
				}
				return (0);
			}
		}

		endutxent();
		return (IA_NOENTRY);
	}

}

int
ia_setcred(iah, flags, uid, gid, ngroups, grouplist, ia_statusp)
	void	*iah;
	int	flags;
	uid_t	uid;
	gid_t	gid;
	int	ngroups;
	gid_t	*grouplist;
	struct ia_status *ia_statusp;
{
	int 	error;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_setcred()\n");

	ia_statusp->iast_status = 0;
	(void) memset(ia_statusp->iast_info, 0, 128);

	if (scheme.sa_setcred == NULL)
		if ((error = load_scheme(AM_SCHEME)) != IA_SUCCESS) {
			scheme.sa_setcred = NULL;
			return (error);
		}

	error = (scheme.sa_setcred(iah, flags, uid, gid, ngroups,
		    grouplist, ia_statusp));

	return (error);
}

/*
 * ia_start		- initiate an authentication transaction and
 *			  set parameter values to be used during the
 *			  transaction
 */

int
ia_start(program, user, ttyn, rhost, ia_conv, iah)
	char *program, *user, *ttyn, *rhost;
	struct ia_conv	*ia_conv;
	void **iah;
{
	struct	stat	statbuf;
	int	err;

	/*  turn on PAM debug if "magic" file exists  */

	if (stat(PAM_DEBUG, &statbuf) == 0) {
		pam_debug = 1;
		openlog("PAM", LOG_CONS|LOG_NDELAY, LOG_AUTH);
	}

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_start(%s %s %s %s %x %x)\n",
			program, (user)?user:"no-user",
			(ttyn)?ttyn:"no-ttyn",
			(rhost)?rhost:"no-rhost",
			ia_conv, iah);

	if ((*iah = (void *) alloc_pam_buf()) == NULL)
		return (IA_BUFERR);

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_start: alloc_pam_buf() completed\n");

	if ((err = ia_set_item((void *) *iah, IA_PROGRAM, (void *)program))
		    != 0) {
		clean_up(*iah);
		return (err);
	}

	if ((err = ia_set_item((void *) *iah, IA_USER, (void *) user)) != 0) {
		clean_up(*iah);
		return (err);
	}

	if ((err = ia_set_item((void *) *iah, IA_TTYN, (void *) ttyn)) != 0) {
		clean_up(*iah);
		return (err);
	}

	if ((err = ia_set_item((void *) *iah, IA_RHOST, (void *) rhost)) != 0) {
		clean_up(*iah);
		return (err);
	}

	if ((err = ia_set_item((void *) *iah, IA_CONV, (void *) ia_conv))
	    != 0) {
		clean_up(*iah);
		return (err);
	}

	return (IA_SUCCESS);
}

/*
 * sa_getall		- read the value of all parameters specified in
 *			  the initial call to ia_start(), this is
 *			  called only by the scheme
 */

int
sa_getall(iah, program, user, ttyn, rhost, ia_conv)
	void	* iah;
	char **program, **user, **ttyn, **rhost;
	struct ia_conv	**ia_conv;
{
	int	err = 0;

	if (pam_debug)
		syslog(LOG_DEBUG, "sa_getall()\n");

	if (program != NULL)
		if ((err = ia_get_item(iah, IA_PROGRAM, (void **) program)) < 0)
			return (-err);

	if (user != NULL)
		if ((err = ia_get_item(iah, IA_USER, (void **) user)) < 0)
			return (-err);

	if (ttyn != NULL)
		if ((err = ia_get_item(iah, IA_TTYN, (void **) ttyn)) < 0)
			return (-err);

	if (rhost != NULL)
		if ((err = ia_get_item(iah, IA_RHOST, (void **) rhost)) < 0)
			return (-err);

	if (ia_conv != NULL)
		if ((err = ia_get_item(iah, IA_CONV, (void **) ia_conv)) < 0)
			return (-err);

	if (err < 0)
		return (-err);
	else
		return (0);
}

/*
 * ia_get_item		- read the value of  a parameter specified in
 *			  the initial call to ia_start()
 */

int
ia_get_item(iahp, item_type, item)
	void  	*iahp;
	int 	item_type;
	void 	**item;
{
	struct pam_item *pip;
	struct	pam_sdata *iah = (struct pam_sdata *) iahp;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_get_item(%d)\n", item_type);

	/*
	 * Get the buffer specified by this ia descriptor
	 */

	if (iah == NULL)
		return (-IA_BUFERR);

	if (item_type <= 0 || item_type >= IA_MAX_ITEMS)
		return (-IA_BUFERR);

	pip = &(iah->ps_item[item_type]);

	*item = pip->pi_addr;

	return (pip->pi_size);
}

/*
 * ia_set_item		- set the value of  a parameter specified in
 *			  the initial call to ia_start()
 */

int
ia_set_item(iahp, item_type, item)
	void 	*iahp;
	int 	item_type;
	void 	*item;
{
	struct pam_item *pip;
	struct	pam_sdata *iah = (struct pam_sdata *) iahp;
	int	size;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_set_item(%d)\n", item_type);

	/*
	 * Check that this iah is in range
	 */

	if (iah == NULL)
		return (IA_BUFERR);
	/*
	 * Check that item_type is within valid range
	 */

	if (item_type <= 0 || item_type >= IA_MAX_ITEMS)
		return (IA_BUFERR);

	switch (item_type) {
		case IA_PROGRAM:
		case IA_USER:
		case IA_TTYN:
		case IA_RHOST:
		case IA_AUTHTOK:
			size = istrlen((char *) item);
			break;
		case IA_CONV:
			size = sizeof (struct ia_conv);
			break;
		default:
			return (IA_BUFERR);
	}

	pip = &(iah->ps_item[item_type]);

	/*
	 * Allocate space if this is the first time we're storing the object
	 * else call realloc() if space is already allocated
	 */
	if (pip->pi_addr == NULL) {
		if ((pip->pi_addr = (void *) malloc(
		    (unsigned int) size+1)) == NULL)
			return (IA_BUFERR);
	} else
		if ((pip->pi_addr = (void *)realloc(pip->pi_addr,
		    (unsigned int) size+1)) == NULL)
			return (IA_BUFERR);

	if (item != NULL) {
		(void) memcpy(pip->pi_addr, item, (unsigned int) size);
		((char *) pip->pi_addr)[size] = '\0';
	} else
		((char *)pip->pi_addr)[0] = '\0';

	pip->pi_size = size;

	return (IA_SUCCESS);
}

/*
 * ia_end		- terminate an authentication transaction
 */

int
ia_end(iahp)
	void *iahp;
{
	int i;

	struct pam_sdata *iah = (struct pam_sdata *) iahp;

	if (pam_debug)
		syslog(LOG_DEBUG, "ia_end()\n");

	for (i = 0; i < IA_MAX_ITEMS; i++)
		free(iah->ps_item[i].pi_addr);

	free(iahp);

	/*  end syslog reporting  */

	if (pam_debug)
		closelog();

	return (IA_SUCCESS);
}


static struct pam_sdata  *
alloc_pam_buf()
{
	struct pam_sdata *pdp;

	if (pam_debug)
		syslog(LOG_DEBUG, "alloc_pam_buf()\n");

	pdp = (struct pam_sdata *) malloc(sizeof (struct pam_sdata));
	if (pdp == NULL)
		return (NULL);

	if (pam_debug) {
		syslog(LOG_DEBUG, "alloc_pam_buf(): almost completed\n");
		syslog(LOG_DEBUG, "alloc_pam_buf(): sizeof(pdp) = %d\n",
					sizeof (pdp));
	}

	(void) memset(pdp, 0, sizeof (struct pam_sdata));

	return (pdp);
}

static int
istrlen(str)
	char *str;
{
	if (str == NULL)
		return (1);
	else
		return (strlen(str));
}

/*
 * clean_up -  free allocated storage before retrun when ia_start() failed
 */

static void
clean_up(iahp)
	void *iahp;
{
	int i;
	struct pam_sdata *iah = (struct pam_sdata *) iahp;

	for (i = 0; i < IA_MAX_ITEMS; i++) {
		if (iah->ps_item[i].pi_addr != NULL)
			free(iah->ps_item[i].pi_addr);
	}

	free(iahp);
}

/*
 * free_resp():
 *	free storage for responses used in the call back "ia_conv" functions
 * 	Bug fix due to program *_conv() routines calling scheme free_resp().
 */

void
free_resp(num_msg, resp)
	int num_msg;
	struct ia_response *resp;
{
	int			i;
	struct ia_response	*r;

	r = resp;
	for (i = 0; i < num_msg; i++, r++) {
		free(r->resp);
	}
	free(resp);
}
