/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sulogin.c	1.21	95/06/16 SMI"	/* SVr4.0 1.9	*/
/*	Copyright (c) 1987, 1988 Microsoft Corporation	*/
/*	  All Rights Reserved	*/

/*	This Module contains Proprietary Information of Microsoft  */
/*	Corporation and should be treated as Confidential.	   */


/*
 *	@(#) sulogin.c 1.2 88/05/05 sulogin:sulogin.c
 */
/*
 * sulogin - special login program exec'd from init to let user
 * come up single user, or go multi straight away.
 *
 *	Explain the scoop to the user, and prompt for
 *	root password or ^D. Good root password gets you
 *	single user, ^D exits sulogin, and init will
 *	go multi-user.
 *
 *	If /etc/passwd is missing, or there's no entry for root,
 *	go single user, no questions asked.
 *
 * Anthony Short, 11/29/82
 */

/*
 *	MODIFICATION HISTORY
 *
 *	M000	01 May 83	andyp	3.0 upgrade
 *	- index ==> strchr.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <termio.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <signal.h>
#include <utmpx.h>
#include <unistd.h>
#include <string.h>
#include <deflt.h>

#include <security/ia_appl.h>

/*
 * Intervals to sleep after failed login
 */
#ifndef SLEEPTIME
#define	SLEEPTIME	4	/* sleeptime before login incorrect msg */
#endif

/*
 *	the name of the file containing the login defaults we deliberately
 *	use the same file as login(1)
 */
#define	DEFAULT_LOGIN	"/etc/default/login"
#define	DEFAULT_SULOGIN	"/etc/default/sulogin"

#define	FAIL    (-1)
#define	USE_PAM (1)
#define	ROOT    "root"

#define	FALSE	0
#define	TRUE	1

void *iah;
int   is_ctrl_d;
int   login_conv();
void  dummy_conv();
struct ia_conv   ia_conv = { login_conv, login_conv, dummy_conv, &is_ctrl_d };
struct ia_status ia_status;

#ifdef	M_V7
#define	strchr	index
#define	strrchr	rindex
#endif

char	minus[]	= "-";
char	shell[]	= "/sbin/sh";
char	su[]	= "/sbin/su";

char	*crypt();
static	char	*getpass();
struct utmpx *getutxent(), *pututxline();
char	*strchr(), *strrchr();
static struct utmpx *u;
char	*ttyn = NULL;
#define	SCPYN(a, b)	strncpy(a, b, sizeof (a))
extern char *findttyname();

/*	the following should be in <deflt.h>	*/
extern	int	defcntl();
extern	int	defopen(char *filename);

static	void	noop(int signal);


main()
{
	struct stat info;			/* buffer for stat() call */
	register struct spwd *shpw;
	struct passwd *pw;
	register char *pass;			/* password from user	  */
	register char *namep;
	int err;
	int	sleeptime = SLEEPTIME;
	int	passreq = TRUE;
	register int  flags;
	register char *ptr;

	signal(SIGINT, noop);
	signal(SIGQUIT, noop);
	if (ia_start("sulogin", "root", "/dev/console", NULL, &ia_conv, &iah)
		!= IA_SUCCESS) {
		single(shell);
	}

	/*
	 * Use the same value of sleeptime that login(1) uses. This is obtained
	 * by reading the file /etc/default/login using the def*() functions
	 */
	if (defopen(DEFAULT_LOGIN) == 0) {
		/*
		 * ignore case
		 */
		flags = defcntl(DC_GETFLAGS, 0);
		TURNOFF(flags, DC_CASE);
		defcntl(DC_SETFLAGS, flags);

		if ((ptr = defread("SLEEPTIME=")) != NULL)
			sleeptime = atoi(ptr);

		(void) defopen((char *)NULL);
	}

	(void) defopen((char *)NULL);

	/*
	 * Use the same value of sleeptime that login(1) uses. This is obtained
	 * by reading the file /etc/default/sulogin using the def*() functions
	 */
	if (defopen(DEFAULT_SULOGIN) == 0) {
		if ((ptr = defread("PASSREQ=")) != (char *)NULL)
			if (strcmp("NO", ptr) == 0)
				passreq = FALSE;

		(void) defopen((char *)NULL);
	}

	(void) defopen((char *)NULL);

	if (passreq == FALSE)
		single(shell);

	/*
	 * Drop into main-loop
	 */

	while (1) {
		printf("\nType Ctrl-d to proceed with normal startup,\n");
		printf("(or give root password for system maintenance): ");
		/*
		 * To check for ctrl-d we clear this flag
		 * its set in login_conv.
		 */
		is_ctrl_d = 0;
		err = ia_auth_user(iah, AU_CHECK_PASSWD, &pw, &ia_status);

		switch (err) {
			case IA_SUCCESS:
				single(su);
				break;
			case IA_AUTHTEST_FAIL:
				if (is_ctrl_d == 1) {
					/*
					 * Ctrl-d was entered so go multi.
					 */
					ia_end(iah);
					exit(0);
				} else
					break;
			default:
				/*
				 * Any other error we stay in the loop
				 */
				break;
		}
		(void) sleep(sleeptime);
		printf("Login incorrect\n");
	}
}



/*
 * single() - exec shell for single user mode
 */
single(cmd)
char *cmd;
{
	/*
	 * update the utmpx file.
	 */
	ttyn = findttyname(0);
	if (ttyn == NULL)
		ttyn = "/dev/???";
	while ((u = getutxent()) != NULL) {
		if (strcmp(u->ut_line, (ttyn + sizeof ("/dev/") -1)) == 0) {
			time(&u->ut_tv.tv_sec);
			u->ut_type = INIT_PROCESS;
			if (strcmp(u->ut_user, "root")) {
				u->ut_pid = getpid();
				SCPYN(u->ut_user, "root");
			}
			break;
		}
	}
	if (u != NULL)
		pututxline(u);
	endutxent();		/* Close utmp file */
	printf("Entering System Maintenance Mode\n\n");
	execl(cmd, cmd, minus, (char *)0);
	exit(0);
}



/*
 * getpass() - hacked from the stdio library
 * version so we can distinguish newline and EOF.
 * Also don't need this routine to give a prompt.
 *
 * RETURN:	(char *)address of password string
 *			(could be null string)
 *
 *	   or	(char *)0 if user typed EOF
 */
static char *
getpass()
{
	struct termio ttyb;
	int flags;
	register char *p;
	register c;
	FILE *fi;
	static char pbuf[9];
	void (*signal())();
	void (*sig)();
	char *rval;		/* function return value */

	if ((fi = fopen("/dev/tty", "r")) == NULL)
		fi = stdin;
	else
		setbuf(fi, (char *)NULL);
	sig = signal(SIGINT, SIG_IGN);
	(void) ioctl(fileno(fi), TCGETA, &ttyb);
	flags = ttyb.c_lflag;
	ttyb.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
	(void) ioctl(fileno(fi), TCSETAF, &ttyb);
	p = pbuf;
	rval = pbuf;
	while ((c = getc(fi)) != '\n') {
		if (c == EOF) {
			if (p == pbuf)		/* ^D, No password */
				rval = (char *)0;
			break;
		}
		if (p < &pbuf[8])
			*p++ = c;
	}
	*p = '\0';			/* terminate password string */
	fprintf(stderr, "\n");		/* echo a newline */
	ttyb.c_lflag = flags;
	(void) ioctl(fileno(fi), TCSETAW, &ttyb);
	signal(SIGINT, sig);
	if (fi != stdin)
		fclose(fi);
	return (rval);
}

char *
findttyname(fd)
int	fd;
{
	extern char *ttyname();
	char *ttyn;

	ttyn = ttyname(fd);

/* do not use syscon or contty if console is present, assuming they are links */
	if (((strcmp(ttyn, "/dev/syscon") == 0) ||
	    (strcmp(ttyn, "/dev/contty") == 0)) &&
	    (access("/dev/console", F_OK)))
		ttyn = "/dev/console";

	return (ttyn);
}


/* ******************************************************************** */
/*									*/
/* login_conv():							*/
/*	This is the conv (conversation) function called from		*/
/*	a PAM authentication scheme to print error messages		*/
/*	or garner information from the user.				*/
/*									*/
/* ******************************************************************** */

static int
login_conv(conv_id, num_msg, msg, response, appdata_ptr)
	int conv_id;
	int num_msg;
	struct ia_message **msg;
	struct ia_response **response;
	void *appdata_ptr;
{
	struct ia_message	*m;
	struct ia_response	*r;
	char 			*temp;
	int			k;

	if (num_msg <= 0)
		return (IA_CONV_FAILURE);

	*response = (struct ia_response *)calloc(num_msg,
						sizeof (struct ia_response));
	if (*response == NULL)
		return (IA_CONV_FAILURE);

	memset(*response, 0, sizeof (struct ia_response));

	k = num_msg;
	m = *msg;
	r = *response;
	while (k--) {

		switch (m->msg_style) {

		case IA_PROMPT_ECHO_OFF:
			if ((temp = getpass()) == (char *)0) {
				exit(0);   /* ^D, so straight to multi-user */
			}
			if (temp != NULL) {
				r->resp = (char *)malloc(strlen(temp)+1);
				if (r->resp == NULL) {
					free_resp(num_msg, *response);
					*response = NULL;
					return (IA_CONV_FAILURE);
				}
				strcpy(r->resp, temp);
				r->resp_len  = strlen(r->resp);
			}

			m++;
			r++;
			break;

		case IA_PROMPT_ECHO_ON:
			if (m->msg != NULL) {
				fputs(m->msg, stdout);
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
			if (m->msg != NULL) {
				fputs(m->msg, stderr);
			}
			m++;
			r++;
			break;

		case IA_TEXTINFO:
			if (m->msg != NULL) {
				fputs(m->msg, stdout);
			}
			m++;
			r++;
			break;

		default:
			break;
		}
	}
	return (IA_SUCCESS);
}

void
dummy_conv()
{
}

/*
 * If /usr/lib is available then use PAM to do the password look-up.
 * Otherwise check that there is a root user in /etc/password, if there
 * is not then drop into a shell. Next check that root is in /etc/shadow
 * to obtain the encrypted password.  Again if unable to do this then
 * drop into a shell. Then obtain the password typed
 * in by the user. If ^D was entered instead then exit(2) so that the
 * system can come up multi-user. Finally validate the password typed
 * in. If it is correct then drop the user into a shell, otherwise return a
 * error so that an error message is displayed and the whole user
 * validation process is restarted.
 * We us fgetpwent(3C) and fgetspent(3C) instad of getpwnam(3C) and
 * getspnam(3C) to parse /etc/passwd and /etc/shadow because the former
 * pair of functions are independent of the switch file lookup. Whereas
 * the second pair are dependent of the switch file lookup.
 */
int
check_root_pass()
{
	FILE	*pwfp;
	FILE	*shfp;
	struct stat	buf;
	struct passwd	*p;
	struct spwd	*s;
	char	*pass;
	char	*password;

	/*
	 * Check to see if /usr is mounted
	 * If it is then have PAM do the password checking
	 */

	if (stat("/usr/lib", &buf) == 0) {
		return (USE_PAM);
	}

	if ((pwfp = fopen(PASSWD, "r")) == NULL) {
		/*
		 * If there's no password file invoke the shell
		 */
		printf("\n**** NO PASSWORD FILE FOUND! ****\n\n");
		/* audit_sulogin_main2(); */
		single(shell);

	}

	while ((p = fgetpwent(pwfp)) != NULL)
		if (strcmp(p->pw_name, ROOT) == 0)
			break;
	fclose(pwfp);

	if (p == NULL) {
		/*
		 * If there's no password entry we invoke the shell
		 */
		printf("\n**** NO ENTRY FOR root %s",
						"IN PASSWORD FILE! ****\n\n");
		/* audit_sulogin_main2(); */
		single(shell);
	}

	if ((shfp = fopen(SHADOW, "r")) == NULL) {
		/*
		 * If there's no shadow file invoke the shell
		 */
		printf("\n**** NO SHADOW FILE FOUND! ****\n\n");
		/* audit_sulogin_main2(); */
		single(shell);

	}

	while ((s = fgetspent(shfp)) != NULL)
		if (strcmp(s->sp_namp, ROOT) == 0)
			break;
	fclose(shfp);

	if (s == NULL) {
		/*
		 * If there's no shadow entry we invoke the shell
		 */
		printf("\n**** NO ENTRY FOR root %s",
						"IN SHADOW FILE! ****\n\n");
		/* audit_sulogin_main2(); */
		single(shell);
	}

	if ((pass = getpass()) == (char *)0) {
		/*
		 * Ctrl-d was entered so go multi.
		 */

		/* audit_sulogin_main4(); */
		exit(0);
	} else {
		password = strdup(pass);
		if (strcmp(s->sp_pwdp, crypt(password, s->sp_pwdp)) == 0) {
			/*
			 * Password check was sucessful, so invoke su(1)
			 *
			 */
			free(password);
			/* audit_sulogin_main6(); */
			single(shell);
		}
		free(password);
	}


	/*
	 * If we have made it this far then not only are /etc/passwd
	 * and /etc/shadow OK, but the user has entered an invalid
	 * root password
	 *
	 */

	return (FAIL);
}


/* ARGSUSED */
static	void
noop(int sig)
{

	/*
	 * This signal handler does nothing except return
	 * We use it as the signal disposition in this program
	 * instead of SIG_IGN so that we do not have to restore
	 * the disposition back to SIG_DFL. Instead we allow exec(2)
	 * to set the dispostion to SIG_DFL to avoid a race condition
	 *
	 */
}
