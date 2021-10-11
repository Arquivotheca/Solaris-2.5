#ident	"@(#)unix_login.c	1.22	95/02/14 SMI"

#define BSD_COMP
# include <errno.h>
# include <fcntl.h>
# include <pwd.h>
# include <signal.h>
# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <utmp.h>

#include <sac.h>		/* for SC_WILDC */
#include <utmpx.h>

# include <rpc/rpc.h>
# include <sys/file.h>
# include <sys/filio.h>
# include <sys/ioctl.h>
# include <sys/signal.h>
# include <sys/stat.h>
# include <sys/types.h>

/*
/* # include <sys/label.h>
/* # include <sys/audit.h>
/* 
/* 
/* 
/* # include <pwdadj.h>
/* */

#include <sys/ttold.h>
#include <stropts.h>
#include <sys/stream.h>



# include "rex.h"

#include <security/ia_appl.h>
void *iah;
struct ia_status  out;


#define	NTTYDISC	2	/* New ttydiscipline: stolen from ttold.h */

/*
 * unix_login - hairy junk to simulate logins for Unix
 *
 * Copyright (c) 1989 Sun Microsystems, Inc.
 */

char	Ttys[] = "/etc/ttys";		/* file to get index of utmp */
char	Utmp[] = "/etc/utmp";		/* the tty slots */
char	Wtmp[] = "/var/adm/wtmp";	/* the log information */

int	Master,	Slave;			/* sides of the pty */


static char	*slavename;
extern char *ptsname();


int	InputSocket,			/* Network sockets */
	OutputSocket;
int	Helper1,			/* pids of the helpers */
	Helper2;
char	UserName[256];			/* saves the user name for loging */
char	HostName[256];			/* saves the host name for loging */


static	int	TtySlot;		/* slot number in Utmp */

extern	fd_set svc_fdset;	/* master file descriptor set */
extern	int child;		/* pid of the executed process */
extern	int ChildDied;		/* flag */
extern	int HasHelper;		/* flag */

extern	void setproctitle( char *user, char *host );
extern int Debug;

#define bzero(s,n)	memset((s), 0, (n))
#define bcopy(a,b,c)	memcpy((b),(a),(c))


  /*
	* The convention used in auditing is:
	*  Argument	Value
	* --------- --------
	*   0	rpc.rexd
	*   1	user name
	*   2	hostname
	*   3	command
	*   4	optional error string
	*/
# define AuditCount 5	/* number of audit parameters */
static char *audit_argv[AuditCount] = {"rpc.rexd", "", "", "", ""}; 

/*
 * Check for user being able to run on this machine.
 * returns 0 if OK, TRUE if problem, error message in "error"
 * copies name of shell and home directory if user is valid.
 */
int	ValidUser(host, uid, error, shell, dir, cmd)
	char *host;		/* passed in */
	int uid;
	char *error;		/* filled in on return */
	char *shell;		/* filled in on return */
	char *dir;		/* filled in on return */
	char *cmd;		/* passed in */
{
	struct passwd *pw, *getpwuid();
	
#ifdef	NOWAY
	if (issecure())
	{
		setauid(0);
		audit_state.as_success = AU_LOGIN;
		audit_state.as_failure = AU_LOGIN;
		setaudit(&audit_state);
	}
	audit_argv[2] = host;
	if (cmd != NULL)
		audit_argv[3] = cmd;
	if (uid == 0)
	{
		errprintf(error,"rexd: root execution not allowed\n",uid);
		audit_argv[1] = "root";
		audit_note(1, error);
		return(1);
	}
#endif	/* NOWAY	*/

	pw = getpwuid(uid);
	if (pw == NULL || pw->pw_name == NULL)
	{
		errprintf(error,"rexd: User id %d not valid\n",uid);

#ifdef	NOWAY
		audit_note(1, error);
#endif	/* NOWAY	*/

		return(1);
	}
	strncpy(UserName, pw->pw_name, sizeof(UserName)-1 );
	strncpy(HostName, host, sizeof(HostName)-1 );
	strcpy(shell,pw->pw_shell);
	strcpy(dir,pw->pw_dir);
	setproctitle(pw->pw_name, host);
	  /*
	   * User has been validated, now do some auditing work
	   */
#ifdef	NOWAY
	audit_argv[1] = pw->pw_name;
	if (cmd == NULL)
		audit_argv[3] = shell;
	setauid(pw->pw_uid);
	if (issecure() == 0)
	return(0);
	if ((apw = getpwanam(pw->pw_name)) != NULL)
	{
		audit_state.as_success = 0;
		audit_state.as_failure = 0;
		if ((getfauditflags(&apw->pwa_au_always, 
				&apw->pwa_au_never, &audit_state)) == 0)
		{
			/*
			 * if we can't tell how to audit from the flags, 
			 * audit everything that's not never for this user.
			 */
			audit_state.as_success = apw->pwa_au_never.as_success ^ (-1);
			audit_state.as_failure = apw->pwa_au_never.as_success ^ (-1);
		}
	}
	else
	{
		audit_state.as_success = -1;
		audit_state.as_failure = -1;
	}
	setaudit(&audit_state);
#endif	/* NOWAY	*/

        if ( ia_start ("rpc.rexd", pw->pw_name, NULL, host, NULL, &iah )!=
            IA_SUCCESS || ia_auth_user(iah, 0, &pw, &out) != IA_SUCCESS) {
                errprintf(error,"rexd: User id %d not valid\n",uid);
		if (iah) {
			ia_end(iah);
			iah = (void *) 0;
		}
                return(1);
        }

	return(0);
}

/*
 * Add an audit record with argv that was pre-set, plus the given string
 */
#ifdef	NOWAY
void
audit_note(retcode, s)
	int retcode;
	char *s;
{
	audit_argv[4] = s;
	audit_text(AU_LOGIN, retcode, retcode, AuditCount, audit_argv);
}
#endif	/* NOWAY	*/


/*
 * Allocate a pseudo-terminal
 * sets the global variables Master and Slave.
 * returns 1 on error, 0 if OK
 */
int
AllocatePty(socket0, socket1)
	int socket0, socket1;
{

    int on = 1;

    sigset(SIGHUP,SIG_IGN);
    sigset(SIGTTOU,SIG_IGN);
    sigset(SIGTTIN,SIG_IGN);

    if ((Master = open("/dev/ptmx",O_RDWR)) == -1) {
	    if (Debug)
		    printf("open-ptmx-failure\n");
	    perror("AloocatePtyMaster fails");
	    return(1);		/* error could not open /dev/ptmx */
    }
    if (Debug)
	    printf("open-ptmx success Master =%d\n",Master);
    if (Debug)
	    printf("Before grantpt...Master=%d\n",Master);
    
    if (grantpt(Master) == -1) {
	    perror("could not grant slave pty");
	    exit (1);
    }
    if (unlockpt(Master) == -1) {
	    perror ("could not unlock slave pty");
	    exit(1);
    }
    if ((slavename = ptsname(Master)) == NULL) {
	    perror ("could not enable slave pty");
	    exit(1);
    }
    if ((Slave = open(slavename, O_RDWR)) == -1) {
	    perror("could not open slave pty");
	    exit (1);
    }
    if (ioctl(Slave, I_PUSH, "ptem") == -1) {
	    perror("ioctl I_PUSH ptem");
	    exit(1);
    }
    if (ioctl(Slave, I_PUSH, "ldterm") == -1) {
	    perror("ioctl I_PUSH ldterm");
	    exit(1);
    }
    if (ioctl(Slave, I_PUSH, "ttcompat") == -1) {
	    perror("ioctl I_PUSH ttcompat");
	    exit(1);
    }


    setsid(); /* get rid of controlling terminal */
/*     LoginUser(); */

    InputSocket = socket0;
    OutputSocket = socket1;
    ioctl(Master, FIONBIO, &on);
    FD_SET(InputSocket, &svc_fdset);
    FD_SET(Master, &svc_fdset);
    return(0);

}

void OpenPtySlave()
{
	close(Slave);
	Slave = open (slavename, O_RDWR);
	if (Slave < 0)
	{
		perror (slavename);
		exit (1);
	}
}



	/*
	 * Special processing for interactive operation.
	 * Given pointers to three standard file descriptors,
	 * which get set to point to the pty.
	 */
void	DoHelper(pfd0, pfd1, pfd2)
	int *pfd0, *pfd1, *pfd2;
{
	int pgrp;


	sigset( SIGINT, SIG_IGN);
	close(Master);
	close(InputSocket);
	close(OutputSocket);

	*pfd0 = Slave;
	*pfd1 = Slave;
	*pfd2 = Slave;
}


/*
 * destroy the helpers when the executing process dies
 */
KillHelper(grp)
	int grp;
{
	if (Debug)
		printf("Enter KillHelper\n");
	close(Master);
	FD_CLR(Master, &svc_fdset);
	close(InputSocket);
	FD_CLR(InputSocket, &svc_fdset);
	close(OutputSocket);
	LogoutUser();

	if (grp)
	    kill((-grp),SIGKILL);
}


/*
 * edit the Unix traditional data files that tell who is logged
 * into "the system"
 */
unsigned char   utid[] = {'o','n', SC_WILDC, SC_WILDC};
 
LoginUser()
{
 
        /*
         * We're pretty drastic here, exiting if an error is detected
         */
        if (ia_set_item(iah, IA_TTYN, slavename) != IA_SUCCESS ||
            ia_open_session(iah, 0, USER_PROCESS, (char *) utid, &out)
							!= IA_SUCCESS ) {
                /*
                 * XXX should print something but for now we exit
                 */
                exit(1);
        }          
}

/*
 * edit the Unix traditional data files that tell who is logged
 * into "the system".
 */

LogoutUser()
{
    struct utmpx *up;

    sighold(SIGCHLD);		/* no disruption during cleanup */

    if (iah) {
	ia_end(iah);
	iah = (void *) 0;
    }

    (void) ia_start ("rpc.rexd", NULL, NULL, NULL, NULL, &iah);

    /* ia_close_session will get userID, hostID, etc. from utmp entry */
    (void) ia_close_session(iah, 0, child, 0, NULL, &out);

    if (iah) {
	ia_end(iah);
	iah = (void *) 0;
    }

    sigrelse(SIGCHLD);		/* ??? */
}

/*
 * set the pty modes to the given values
 */
SetPtyMode(mode)
	struct rex_ttymode *mode;
{
	struct sgttyb svr4_sgttyb_var;
	int ldisc = NTTYDISC;

	if (Debug)
		printf("Enter SetPtyMode\n");
	
	if (Debug)
		printf("SetPtyMode:opened slave\n");
	ioctl(Slave, TIOCSETD, &ldisc);
	if (Debug)
		printf("SetPtyMode:Slave TIOCSETD done\n");

	/*
	 * Copy from over-the-net(bsd) to SVR4 format
	 */
	svr4_sgttyb_var.sg_ispeed = mode->basic.sg_ispeed;
	svr4_sgttyb_var.sg_ospeed = mode->basic.sg_ospeed;
	svr4_sgttyb_var.sg_erase  = mode->basic.sg_erase;
	svr4_sgttyb_var.sg_kill = mode->basic.sg_kill;
	svr4_sgttyb_var.sg_flags = (int) mode->basic.sg_flags;
	/*
	 * Clear any possible sign extension caused by (int)
	 * typecast
	 */
	svr4_sgttyb_var.sg_flags &= 0xFFFF;

	ioctl(Slave, TIOCSETN, &svr4_sgttyb_var);
	if (Debug)
		printf("SetPtyMode:Slave TIOCSETN done\n");
	ioctl(Slave, TIOCSETC, &mode->more);
	if (Debug)
		printf("SetPtyMode:Slave TIOCSETC done\n");
	ioctl(Slave, TIOCSLTC, &mode->yetmore);
	if (Debug)
		printf("SetPtyMode:Slave TIOCSLTC done\n");
	ioctl(Slave, TIOCLSET, &mode->andmore);
	if (Debug)
		printf("SetPtyMode:Slave TIOCSET done\n");
 	close(Slave);		/* Opened in AllocPty for parent, still open in child */
}

/*
 * set the pty window size to the given value
 */
SetPtySize(sizep)
	struct rex_ttysize *sizep;
{
	struct winsize newsize;

	/* if size has changed, this ioctl changes it */
	/* *and* sends SIGWINCH to process group */

	newsize.ws_row = (unsigned short) sizep->ts_lines;
	newsize.ws_col = (unsigned short) sizep->ts_cols;

	(void) ioctl(Master, TIOCSWINSZ, &newsize);
	
}


/*
 * send the given signal to the group controlling the terminal
 */
SendSignal(sig)
	int sig;
{
	pid_t pgrp;

	pgrp = getpgid(child);
	(void) kill((-pgrp), sig);
}

/*
 * called when the main select loop detects that we might want to
 * read something.
 */
void
HelperRead(fdp)
	struct	fd_set	*fdp;	/* XXX -- fd_set *fdp;	*/
{
	char buf[128];
	int cc;
	extern int errno;
	int mask;

/*	mask = sigsetmask (sigmask (SIGCHLD));	*/
	mask = sighold(SIGCHLD);
	if (FD_ISSET(Master, fdp))
	{
		FD_CLR(Master, fdp);
		cc = read(Master, buf, sizeof buf);
		if (cc > 0)
			(void) write(OutputSocket, buf, cc);
		else
		{
			shutdown(OutputSocket, 1); /* 1=>further sends disallowed */
			FD_CLR(Master, &svc_fdset);
			if (cc < 0 && errno != EINTR && errno != EWOULDBLOCK &&
					errno != EIO)
				perror("pty read");
			if (ChildDied)
			{
				KillHelper (child);
				HasHelper = 0;
				if (FD_ISSET(InputSocket, fdp))
				FD_CLR(InputSocket, fdp);
				goto done;
			}
		}
	}
	if (FD_ISSET(InputSocket, fdp))
	{
		FD_CLR(InputSocket, fdp);
		cc = read(InputSocket, buf, sizeof buf);
		if (cc > 0)
			(void) write(Master, buf, cc);
		else
		{
			if (cc < 0 && errno != EINTR && errno != EWOULDBLOCK)
			perror("socket read");
			FD_CLR(InputSocket, &svc_fdset);
		}
	}
	done:
/*	sigsetmask (mask);	*/
	sigrelse(SIGCHLD);
}
