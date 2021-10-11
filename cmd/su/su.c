/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)su.c 1.23	94/07/17 SMI"	/* SVr4.0 1.9.5.16	*/

/*	Copyright (c) 1987, 1988 Microsoft Corporation	*/
/*	  All Rights Reserved	*/

/*	This Module contains Proprietary Information of Microsoft  */
/*	Corporation and should be treated as Confidential.	   */
/*
 *	su [-] [name [arg ...]] change userid, `-' changes environment.
 *	If SULOG is defined, all attempts to su to another user are
 *	logged there.
 *	If CONSOLE is defined, all successful attempts to su to uid 0
 *	are also logged there.
 *
 *	If su cannot create, open, or write entries into SULOG,
 *	(or on the CONSOLE, if defined), the entry will not
 *	be logged -- thus losing a record of the su's attempted
 *	during this period.
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <stdlib.h>
#include <crypt.h>
#include <pwd.h>
#include <shadow.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <locale.h>
#include <syslog.h>

#include <security/ia_appl.h>

#define	PATH	"/usr/bin:"		/* path for users other than root */
#define	SUPATH	"/usr/sbin:/usr/bin"	/* path for root */
#define	SUPRMT	"PS1=# "		/* primary prompt for root */
#define	ELIM 128
#define	ROOT 0

#define	DEFFILE "/etc/default/su"		/* default file M000 */
char	*Sulog, *Console;
char	*Path, *Supath;			/* M004 */
extern char *defread();
extern int defopen();

static void envalt(void);
static void log(char *where, char *towho, int how);
static void to(int sig);

static void expired(char *usernam, struct ia_status ia_status);

static int su_conv(int, int, struct ia_message **, struct ia_response **,
    void *);
static void dummy_conv(int, void *);
static struct ia_conv ia_conv = {
	su_conv, su_conv, dummy_conv, NULL
};
static void	*iah;			/* Authentication handle */

struct	passwd *pwd, *getpwnam();
char	shell[] = "/usr/bin/sh";	/* default shell */
char	su[16] = "su";		/* arg0 for exec of shprog */
char	homedir[MAXPATHLEN] = "HOME=";
char	logname[20] = "LOGNAME=";
char	*suprmt = SUPRMT;
char	termtyp[40] = "TERM=";			/* M002 */
char	*term;
char	shelltyp[40] = "SHELL=";		/* M002 */
char	*hz, *tz;
char	tznam[15] = "TZ=";
char	hzname[10] = "HZ=";
char	path[MAXPATHLEN] = "PATH=";		/* M004 */
char	supath[MAXPATHLEN] = "PATH=";		/* M004 */
char	*envinit[ELIM];
extern	char **environ;
char *ttyn;
char *username;					/* the invoker */
static	int	dosyslog = 0;			/* use syslog? */

int
main(int argc, char **argv)
{
	char *nptr;
	char	*pshell;
	int eflag = 0;
	int envidx = 0;
	uid_t uid;
	gid_t gid;
	char *dir, *shprog, *name;
	struct ia_status ia_status;
	int flags = 0;
	int retcode;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it wasn't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	if (argc > 1 && *argv[1] == '-') {
		eflag++;	/* set eflag if `-' is specified */
		argv++;
		argc--;
	}

	/*
	 * determine specified userid, get their password file entry,
	 * and set variables to values in password file entry fields
	 */
	nptr = (argc > 1) ? argv[1] : "root";
	if (defopen(DEFFILE) == 0) {
		char *ptr;

		if (Sulog = defread("SULOG="))
			Sulog = strdup(Sulog);
		if (Console = defread("CONSOLE="))
			Console = strdup(Console);
		if (Path = defread("PATH="))
			Path = strdup(Path);
		if (Supath = defread("SUPATH="))
			Supath = strdup(Supath);
		if ((ptr = defread("SYSLOG=")) != NULL)
			dosyslog = strcmp(ptr, "YES") == 0;

		defopen(NULL);
	}
	(void) strcat(path, (Path) ? Path : PATH);
	(void) strcat(supath, (Supath) ? Supath : SUPATH);
	if ((ttyn = ttyname(0)) == NULL)
		if ((ttyn = ttyname(1)) == NULL)
			if ((ttyn = ttyname(2)) == NULL)
				ttyn = "/dev/???";
	if ((username = cuserid(NULL)) == NULL)
		username = "(null)";

	/*
	 * if Sulog defined, create SULOG, if it does not exist, with
	 * mode read/write user. Change owner and group to root
	 */
	if (Sulog != NULL) {
		(void) close(open(Sulog, O_WRONLY | O_APPEND | O_CREAT,
		    (S_IRUSR|S_IWUSR)));
		(void) chown(Sulog, (uid_t)ROOT, (gid_t)ROOT);
	}

	if (ia_start("su", nptr, ttyn, NULL, &ia_conv, &iah) != IA_SUCCESS)
		exit(1);

	if (getuid() != (uid_t)0)
		flags = AU_CHECK_PASSWD;

	/*
	 * Save away the following for writing user-level audit records:
	 *	username desired
	 *	current tty
	 *	whether or not username desired is expired
	 *	auditinfo structure of current process
	 *	uid's and gid's of current process
	 */
	audit_su_init_info(nptr, ttyn);
	openlog("su", LOG_CONS, LOG_AUTH);

	/*
	 * call ia_auth_user() to authenticate the user through PAM
	 */
	retcode = ia_auth_user(iah, flags, &pwd, &ia_status);
	switch (retcode) {
	case IA_SUCCESS:
		if (flags & AU_CHECK_PASSWD) /* check if not super-user */
			expired(nptr, ia_status);
		audit_su_reset_ai();
		audit_su_success();
		if (dosyslog)
			syslog(getuid() == 0 ? LOG_INFO : LOG_NOTICE,
			    "'su %s' succeeded for %s on %s",
			    pwd->pw_name, username, ttyn);
		closelog();
		break;

	case IA_NO_PWDENT:
		(void) fprintf(stderr, gettext("su: Unknown id: %s\n"), nptr);
		audit_su_bad_username();
		closelog();
		exit(1);
		/*NOTREACHED*/

	case IA_AUTHTEST_FAIL:
		if (Sulog != NULL)
			log(Sulog, nptr, 0);	/* log entry */
		(void) fprintf(stderr, gettext("su: Sorry\n"));
		audit_su_bad_authentication();
		if (dosyslog)
			syslog(LOG_CRIT, "'su %s' failed for %s on %s",
			    pwd->pw_name, username, ttyn);
		closelog();
		exit(2);
		/*NOTREACHED*/

	case IA_CONV_FAILURE:
	default:
		audit_su_unknown_failure();
		if (dosyslog)
			syslog(LOG_CRIT, "'su %s' failed for %s on %s",
			    pwd->pw_name, username, ttyn);
		closelog();
		exit(1);
	}

	uid = pwd->pw_uid;
	gid = pwd->pw_gid;
	dir = strdup(pwd->pw_dir);
	shprog = strdup(pwd->pw_shell);
	name = strdup(pwd->pw_name);

	if (Sulog != NULL)
		log(Sulog, nptr, 1);	/* log entry */

	/* set user and group ids to specified user */

	flags = SC_INITGPS | SC_SETRID;
	retcode = ia_setcred(iah, flags, uid, gid, 0, NULL, &ia_status);
	switch (retcode) {
	case IA_SUCCESS:
		break;

	case IA_BAD_UID:
		(void) printf(gettext("su: Invalid ID\n"));
		exit(2);
		/*NOTREACHED*/

	case IA_BAD_GID:
		(void) printf(gettext("su: Invalid ID\n"));
		exit(2);
		/*NOTREACHED*/

	case IA_INITGP_FAIL:
		exit(2);
		/*NOTREACHED*/

	default:
		exit(1);
		/*NOTREACHED*/
	}

	if (iah)
		(void) ia_end(iah);

	/*
	 * If new user's shell field is neither NULL nor equal to /usr/bin/sh,
	 * set:
	 *
	 *	pshell = their shell
	 *	su = [-]last component of shell's pathname
	 *
	 * Otherwise, set the shell to /usr/bin/sh and set argv[0] to '[-]su'.
	 */
	if (shprog[0] != '\0' && strcmp(shell, shprog) != 0) {
		char *p;

		pshell = shprog;
		(void) strcpy(su, eflag ? "-" : "");

		if ((p = strrchr(pshell, '/')) != NULL)
			(void) strcat(su, p + 1);
		else
			(void) strcat(su, pshell);
	} else {
		pshell = shell;
		(void) strcpy(su, eflag ? "-su" : "su");
	}

	/*
	 * set environment variables for new user;
	 * arg0 for exec of shprog must now contain `-'
	 * so that environment of new user is given
	 */
	if (eflag) {
		(void) strcat(homedir, dir);
		(void) strcat(logname, name);		/* M003 */
		if (hz = getenv("HZ"))
			(void) strcat(hzname, hz);
		if (tz = getenv("TZ"))
			(void) strcat(tznam, tz);
		(void) strcat(shelltyp, pshell);
		(void) chdir(dir);
		envinit[envidx = 0] = homedir;
		envinit[++envidx] = ((uid == (uid_t)0) ? supath : path);
		envinit[++envidx] = logname;
		envinit[++envidx] = hzname;
		envinit[++envidx] = tznam;
		if ((term = getenv("TERM")) != NULL) {
			(void) strcat(termtyp, term);
			envinit[++envidx] = termtyp;
		}
		envinit[++envidx] = shelltyp;
		envinit[++envidx] = NULL;
		environ = envinit;
	} else {
		char **pp = environ, **qq, *p;

		while ((p = *pp) != NULL) {
			if (*p == 'L' && p[1] == 'D' && p[2] == '_') {
				for (qq = pp; (*qq = qq[1]) != NULL; qq++)
					;
				/* pp is not advanced */
			} else {
				pp++;
			}
		}
	}

	/*
	 * if new user is root:
	 *	if CONSOLE defined, log entry there;
	 *	if eflag not set, change environment to that of root.
	 */
	if (uid == (uid_t)0) {
		if (Console != NULL)
			if (strcmp(ttyn, Console) != 0) {
				(void) signal(SIGALRM, to);
				(void) alarm(30);
				log(Console, nptr, 1);
				(void) alarm(0);
			}
		if (!eflag)
			envalt();
	}

	/*
	 * if additional arguments, exec shell program with array
	 * of pointers to arguments:
	 *	-> if shell = default, then su = [-]su
	 *	-> if shell != default, then su = [-]last component of
	 *						shell's pathname
	 *
	 * if no additional arguments, exec shell with arg0 of su
	 * where:
	 *	-> if shell = default, then su = [-]su
	 *	-> if shell != default, then su = [-]last component of
	 *						shell's pathname
	 */
	if (argc > 2) {
		argv[1] = su;
		(void) execv(pshell, &argv[1]);
	} else
		(void) execl(pshell, su, 0);

	(void) fprintf(stderr, gettext("su: No shell\n"));
	exit(3);
}

/*
 * Environment altering routine -
 *	This routine is called when a user is su'ing to root
 *	without specifying the - flag.
 *	The user's PATH and PS1 variables are reset
 *	to the correct value for root.
 *	All of the user's other environment variables retain
 *	their current values after the su (if they are exported).
 */
static void
envalt(void)
{
	/*
	 * If user has PATH variable in their environment, change its value
	 *		to /bin:/etc:/usr/bin ;
	 * if user does not have PATH variable, add it to the user's
	 *		environment;
	 * if either of the above fail, an error message is printed.
	 */
	if (putenv(supath) != 0) {
		(void) printf(gettext(
		    "su: unable to obtain memory to expand environment"));
		exit(4);
	}

	/*
	 * If user has PROMPT variable in their environment, change its value
	 *		to # ;
	 * if user does not have PROMPT variable, add it to the user's
	 *		environment;
	 * if either of the above fail, an error message is printed.
	 */
	if (putenv(suprmt) != 0) {
		(void) printf(gettext(
		    "su: unable to obtain memory to expand environment"));
		exit(4);
	}
}

/*
 * Logging routine -
 *	where = SULOG or CONSOLE
 *	towho = specified user ( user being su'ed to )
 *	how = 0 if su attempt failed; 1 if su attempt succeeded
 */
static void
log(char *where, char *towho, int how)
{
	FILE *logf;
	long now;
	struct tm *tmp;

	/*
	 * open SULOG or CONSOLE - if open fails, return
	 */
	if ((logf = fopen(where, "a")) == NULL)
		return;

	now = time(0);
	tmp = localtime(&now);

	/*
	 * write entry into SULOG or onto CONSOLE - if write fails, return
	 */
	(void) fprintf(logf, "SU %.2d/%.2d %.2d:%.2d %c %s %s-%s\n",
	    tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min,
	    how ? '+' : '-', ttyn + sizeof ("/dev/") - 1, username, towho);

	(void) fclose(logf);	/* close SULOG or CONSOLE */
}

/*ARGSUSED*/
static void
to(int sig)
{}

/* ******************************************************************** */
/*									*/
/* su_conv():								*/
/*	This is the conv (conversation) function called from		*/
/*	a PAM authentication scheme to print error messages		*/
/*	or garner information from the user.				*/
/*									*/
/* ******************************************************************** */
/*ARGSUSED*/
static int
su_conv(
	int		conv_id,
	int		num_msg,
	struct ia_message **msg,
	struct ia_response **response,
	void		*appdata_ptr)
{
	struct ia_message	*m;
	struct ia_response	*r;
	char			*temp;
	int			k;

	if (num_msg <= 0)
		return (IA_CONV_FAILURE);

	*response = (struct ia_response *)calloc(num_msg,
	    sizeof (struct ia_response));
	if (*response == NULL)
		return (IA_CONV_FAILURE);

	(void) memset(*response, 0, sizeof (struct ia_response));

	k = num_msg;
	m = *msg;
	r = *response;
	while (k--) {

		switch (m->msg_style) {

		case IA_PROMPT_ECHO_OFF:
			temp = getpass(m->msg);
			if (temp != NULL) {
				r->resp = (char *)malloc(strlen(temp) + 1);
				if (r->resp == NULL) {
					free_resp(num_msg, *response);
					*response = NULL;
					return (IA_CONV_FAILURE);
				}
				(void) strcpy(r->resp, temp);
				r->resp_len  = strlen(r->resp);
			}
			m++;
			r++;
			break;

		case IA_PROMPT_ECHO_ON:
			if (m->msg != NULL) {
				(void) fputs(m->msg, stdout);
			}
			r->resp = (char *)malloc(MAX_RESP_SIZE);
			if (r->resp == NULL) {
				free_resp(num_msg, *response);
				*response = NULL;
				return (IA_CONV_FAILURE);
			}
			if (fgets(r->resp, MAX_RESP_SIZE, stdin) != NULL)
				r->resp_len  = strlen(r->resp);
			m++;
			r++;
			break;

		case IA_ERROR_MSG:
			if (m->msg != NULL)
				(void) fputs(m->msg, stderr);
			m++;
			r++;
			break;

		case IA_TEXTINFO:
			if (m->msg != NULL)
				(void) fputs(m->msg, stdout);
			m++;
			r++;
			break;

		default:
			break;
		}
	}
	return (IA_SUCCESS);
}

/*ARGSUSED*/
static void
dummy_conv(int value, void *args)
{}

/*							*/
/*	expired - calls exec_pass() to prompt user for  */
/*		 a passwd if the passwd has expired	*/
/*							*/
static void
expired(char *usernam, struct ia_status ia_status)
{
	int flags = AU_CHECK_PASSWD;
	int error = 0;

	if (error = ia_auth_acctmg(iah, flags, &pwd, &ia_status)) {
		if (error == IA_NEWTOK_REQD) {
		    (void) fprintf(stderr, "%s '%s' %s\n",
			gettext("Passwd for user"), usernam,
			gettext("has expired - use passwd(1) to update it"));
		    audit_su_bad_authentication();
		    if (dosyslog)
			syslog(LOG_CRIT, "'su %s' failed for %s on %s",
			    pwd->pw_name, usernam, ttyn);
		    closelog();
		    exit(1);
		} else {
			(void) fprintf(stderr, gettext("su: Sorry\n"));
			audit_su_bad_authentication();
			if (dosyslog)
			    syslog(LOG_CRIT, "'su %s' failed for %s on %s",
				pwd->pw_name, usernam, ttyn);
			closelog();
			exit(3);
		}
	}
}
