#pragma ident	"@(#)in.uucpd.c	1.18	93/09/28 SMI"
	  /* from UCB 5.4 6/23/85 */

/*
 * 4.2BSD, 2.9BSD, or ATTSVR4 TCP/IP server for uucico
 * uucico's TCP channel causes this server to be run at the remote end.
 */

#include "uucp.h"
#include <netdb.h>
#ifdef BSD2_9
#include <sys/localopts.h>
#include <sys/file.h>
#endif BSD2_9
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#ifdef ATTSVTTY
#include <sys/termio.h>
#else
#include <sys/ioctl.h>
#endif
#include <pwd.h>
#ifdef ATTSVR4
#include <shadow.h>
#endif
#include <lastlog.h>

#include <security/ia_appl.h>

int uucp_conv();
void dummy_conv();
struct ia_conv conv = { uucp_conv, uucp_conv, dummy_conv, NULL };
void    *iah;

#if !defined(BSD4_2) && !defined(BSD2_9) && !defined(ATTSVR4)
--- You must have either BSD4_2, BSD2_9, or ATTSVR4 defined for this to work
#endif !BSD4_2 && !BSD2_9
#if defined(BSD4_2) && defined(BSD2_9)
--- You may not have both BSD4_2 and BSD2_9 defined for this to work
#endif	/* check for stupidity */

char lastlog[] = "/var/adm/lastlog";
struct	passwd nouser = { "", "nope", -1, -1, "", "", "", "", "" };
#ifdef ATTSVR4
struct	spwd noupass = { "", "nope" };
#endif
struct	sockaddr_in hisctladdr;
int hisaddrlen = sizeof hisctladdr;
struct	sockaddr_in myctladdr;
int nolog;		/* don't log in utmp or wtmp */

char Username[64];
char Loginname[64];
char *nenv[] = {
	Username,
	Loginname,
	NULL,
};
extern char **environ;

main(argc, argv)
int argc;
char **argv;
{
#ifndef BSDINETD
	register int s, tcp_socket;
	struct servent *sp;
#endif !BSDINETD
	extern int errno;
	int dologout();

	if (argc > 1 && strcmp(argv[1], "-n") == 0)
		nolog = 1;
	environ = nenv;
#ifdef BSDINETD
	close(1); close(2);
	dup(0); dup(0);
	hisaddrlen = sizeof (hisctladdr);
	if (getpeername(0, (struct sockaddr *)&hisctladdr, &hisaddrlen) < 0) {
		fprintf(stderr, "%s: ", argv[0]);
		perror("getpeername");
		_exit(1);
	}
	if (fork() == 0)
		doit(&hisctladdr);
	dologout();
	exit(1);
#else !BSDINETD
	sp = getservbyname("uucp", "tcp");
	if (sp == NULL){
		perror("uucpd: getservbyname");
		exit(1);
	}
	if (fork())
		exit(0);
#ifdef ATTSVR4
	setsid();
#else
	if ((s=open("/dev/tty", 2)) >= 0){
		ioctl(s, TIOCNOTTY, (char *)0);
		close(s);
	}
#endif

#ifdef ATTSVR4
	memset((void *)&myctladdr, 0, sizeof (myctladdr));
#else
	bzero((char *)&myctladdr, sizeof (myctladdr));
#endif
	myctladdr.sin_family = AF_INET;
	myctladdr.sin_port = sp->s_port;
#if defined(BSD4_2) || defined(ATTSVR4)
	tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_socket < 0) {
		perror("uucpd: socket");
		exit(1);
	}
	if (bind(tcp_socket, (char *)&myctladdr, sizeof (myctladdr)) < 0) {
		perror("uucpd: bind");
		exit(1);
	}
	listen(tcp_socket, 3);	/* at most 3 simultaneuos uucp connections */
	signal(SIGCHLD, dologout);

	for(;;) {
		s = accept(tcp_socket, &hisctladdr, &hisaddrlen);
		if (s < 0){
			if (errno == EINTR) 
				continue;
			perror("uucpd: accept");
			exit(1);
		}
		if (fork() == 0) {
			close(0); close(1); close(2);
			dup(s); dup(s); dup(s);
			close(tcp_socket); close(s);
			doit(&hisctladdr);
			exit(1);
		}
		close(s);
	}
#endif BSD4_2

#ifdef BSD2_9
	for(;;) {
		signal(SIGCHLD, dologout);
		s = socket(SOCK_STREAM, 0,  &myctladdr,
			SO_ACCEPTCONN|SO_KEEPALIVE);
		if (s < 0) {
			perror("uucpd: socket");
			exit(1);
		}
		if (accept(s, &hisctladdr) < 0) {
			if (errno == EINTR) {
				close(s);
				continue;
			}
			perror("uucpd: accept");
			exit(1);
		}
		if (fork() == 0) {
			close(0); close(1); close(2);
			dup(s); dup(s); dup(s);
			close(s);
			doit(&hisctladdr);
			exit(1);
		}
	}
#endif BSD2_9
#endif	!BSDINETD

	/* NOTREACHED */
}

doit(sinp)
struct sockaddr_in *sinp;
{
	char user[64], passwd[64];
	char *xpasswd, *crypt();
	struct passwd *pw, *getpwnam();
#ifdef ATTSVR4
	struct spwd *spw;
#endif
	struct  ia_status       out;
	int     error;
	alarm(60);
	printf("login: "); fflush(stdout);
	if (readline(user, sizeof user) < 0) {
		fprintf(stderr, "user read\n");
		return;
	}

        /*
         * Call ia_start to initiate a PAM authentication operation
         */
 
        if ((ia_start("in.uucpd",user,ttyname(0),NULL,&conv, &iah)) !=
            IA_SUCCESS)
                return;
 
 
        if ((error=ia_auth_user(iah,AU_CHECK_PASSWD,&pw,&out)) != IA_SUCCESS) {
                fprintf(stderr, "Login incorrect.");
                if (error == IA_MAXTRYS)
                        _exit(1);
                return;
        }
 
        if (strcmp(pw->pw_shell, UUCICO)) {
                fprintf(stderr, "Login incorrect.");
                return;
        }
        alarm(0);
 
        sprintf(Username, "USER=%s", user);
        sprintf(Loginname, "LOGNAME=%s", user);
        if (!nolog)
                if (dologin(pw, sinp))
			_exit(1);
                   
        chdir(pw->pw_dir);
        if (ia_setcred(iah, SC_INITGPS|SC_SETRID, pw->pw_uid,
                                                pw->pw_gid, 0, NULL, &out))
        {
                fprintf(stderr, "Login incorrect.");
                return;
        }

	ia_end(iah);

#if defined(BSD4_2) || defined(ATTSVR4)
	execl(UUCICO, "uucico", "-u", user, (char *)0);
#endif BSD4_2
#ifdef BSD2_9
	sprintf(passwd, "-h%s", inet_ntoa(sinp->sin_addr));
	execl(UUCICO, "uucico", passwd, (char *)0);
#endif BSD2_9
	perror("uucico server: execl");
}

readline(p, n)
register char *p;
register int n;
{
	char c;

	while (n-- > 0) {
		if (read(0, &c, 1) <= 0)
			return(-1);
		c &= 0177;
		if (c == '\n' || c == '\r') {
			*p = '\0';
			return(0);
		}
		*p++ = c;
	}
	return(-1);
}

#ifdef ATTSVR4
#include <sac.h>	/* for SC_WILDC */
#include <utmpx.h>
#else !ATTSVR4
#include <utmp.h>
#endif !ATTSVR4
#if defined(BSD4_2) || defined(ATTSVR4)
#include <fcntl.h>
#endif BSD4_2

#ifdef BSD2_9
#define O_APPEND	0 /* kludge */
#define wait3(a,b,c)	wait2(a,b)
#endif BSD2_9

#define	SCPYN(a, b)	strncpy(a, b, sizeof (a))

#ifdef ATTSVR4
struct	utmpx utmp;
#else !ATTSVR4
struct	utmp utmp;
#endif !ATTSVR4

dologout()
{
#ifdef ATTSVR4
	int status;
#else !ATTSVR4
	union wait status;
#endif !ATSVR4
	int pid, wtmp;
	struct ia_status out;

#ifdef BSDINETD
	while ((pid=wait(&status)) > 0) {
#else  !BSDINETD
	while ((pid=wait3(&status,WNOHANG,0)) > 0) {
#endif !BSDINETD
		if (nolog)
			continue;
#ifdef ATTSVR4
		utmp.ut_id[0] = 'u';
		utmp.ut_id[1] = 'u';
		utmp.ut_id[2] = SC_WILDC;
		utmp.ut_id[3] = SC_WILDC;
		sprintf(utmp.ut_line, "uucp%.4d", pid);
		if ((ia_start("in.uucpd",NULL,utmp.ut_line,NULL,&conv,&iah))
		    != IA_SUCCESS)
			return;
                (void) ia_close_session(iah, IS_NOLOG,
                    pid, status, utmp.ut_id, &out);
 
                ia_end(iah);
#else !ATTSVR4
		wtmp = open("/usr/adm/wtmp", O_WRONLY|O_APPEND);
		if (wtmp >= 0) {
			sprintf(utmp.ut_line, "uucp%.4d", pid);
			SCPYN(utmp.ut_name, "");
			SCPYN(utmp.ut_host, "");
			(void) time(&utmp.ut_time);
#ifdef BSD2_9
			(void) lseek(wtmp, 0L, 2);
#endif BSD2_9
			(void) write(wtmp, (char *)&utmp, sizeof (utmp));
			(void) close(wtmp);
		}
#endif !ATTSVR4
	}
}

/*
 * Record login in wtmp file.
 */
dologin(pw, sin)
struct passwd *pw;
struct sockaddr_in *sin;
{
	char line[32];
	char remotehost[32];
	int wtmp, f;
        struct ia_status out;
	struct hostent *hp = gethostbyaddr((const char *)&sin->sin_addr,
		sizeof (struct in_addr), AF_INET);

	if (hp) {
		strncpy(remotehost, hp->h_name, sizeof (remotehost));
		endhostent();
	} else
		strncpy(remotehost, (char *)inet_ntoa(sin->sin_addr),
		    sizeof (remotehost));
#ifdef ATTSVR4
	SCPYN(utmp.ut_user, pw->pw_name);
	utmp.ut_id[0] = 'u';
	utmp.ut_id[1] = 'u';
	utmp.ut_id[2] = SC_WILDC;
	utmp.ut_id[3] = SC_WILDC;
	/* hack, but must be unique and no tty line */
	sprintf(line, "uucp%.4d", getpid());
	SCPYN(utmp.ut_host, remotehost);
	utmp.ut_syslen = strlen(remotehost) + 1;
        if (ia_set_item(iah,IA_RHOST,remotehost) != IA_SUCCESS)
                return (1);
 
        sprintf(line, "uucp%.4d", getpid());
 
        if (ia_set_item(iah,IA_TTYN, line))
                return (1);
 
         if (ia_open_session(iah, IS_NOLOG, USER_PROCESS, utmp.ut_id, &out))
                return (1);
#else !ATTSVR4
	wtmp = open("/usr/adm/wtmp", O_WRONLY|O_APPEND);
	if (wtmp >= 0) {
		/* hack, but must be unique and no tty line */
		sprintf(line, "uucp%.4d", getpid());
		SCPYN(utmp.ut_line, line);
		SCPYN(utmp.ut_name, pw->pw_name);
		SCPYN(utmp.ut_host, remotehost);
		time(&utmp.ut_time);
#ifdef BSD2_9
		(void) lseek(wtmp, 0L, 2);
#endif BSD2_9
		(void) write(wtmp, (char *)&utmp, sizeof (utmp));
		(void) close(wtmp);
	}
#endif !ATTSVR4
	if ((f = open(lastlog, 2)) >= 0) {
		struct lastlog ll;

		time(&ll.ll_time);
		lseek(f, (long)pw->pw_uid * sizeof(struct lastlog), 0);
		strcpy(line, remotehost);
		SCPYN(ll.ll_line, line);
		SCPYN(ll.ll_host, remotehost);
		(void) write(f, (char *) &ll, sizeof ll);
		(void) close(f);
	}

	return (0);
}

/*
 * uucp_conv           - This is the conv (conversation) function called from
 *                        a PAM authentication scheme to print error messages
 *                        or garner information from the user.
 */

static int
uucp_conv(conv_id, num_msg, msg, response, appdata_ptr)
        int conv_id;
        int num_msg;
        struct ia_message **msg;
        struct ia_response **response;
        void *appdata_ptr;
{
        struct ia_message       *m;
        struct ia_response      *r;
        char                    *temp;
	static char		passwd[64];
        int                     k;
 
        if (num_msg <= 0)
                return(IA_CONV_FAILURE);
 
        *response = (struct ia_response *)calloc(num_msg,
                                                sizeof (struct ia_response));
        if (*response == NULL)
                return(IA_CONV_FAILURE);
 
	memset(*response, 0, sizeof(struct ia_response));

        k = num_msg;
        m = *msg;
        r = *response;
        while (k--) {
 
                switch (m->msg_style) {
 
                case IA_PROMPT_ECHO_OFF:
			printf("Password: "); fflush(stdout);
			if (readline(passwd, sizeof passwd) < 0) {
				fprintf(stderr, "passwd read\n");
				return(IA_SUCCESS);
			}
			temp = passwd;
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
			if (m->msg != NULL){
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
        return(IA_SUCCESS);
}        
 
void
dummy_conv()
{
}

