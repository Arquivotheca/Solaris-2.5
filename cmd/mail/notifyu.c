/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)notifyu.c	1.4	93/08/26 SMI" 	/* SVr4.0 1.4	*/
/* notify(u,m): notifies user "u" with message "m"
 * J. A. Kutsch  ho 43233  x-3059
 * January 1981
 *
 * Converted to C++
 * Tony Hansen
 * January 1989
 *
 * Added a timeout around the open and write to the tty device.
 * This prevents hanging the process if a person logs off at
 * just the wrong moment (datakit seems to have problems here).
 * Tony Hansen
 * March 1989
 *
 * If user is logged in more than once, notification is made to all terminals.
 * notification is given without "Message from ..." preface.
 * If messages are being denied, notify ignores that terminal
 *
 * Converted to common K&R C, ANSI C and C++
 * Tony Hansen
 * April 1989
 *
 */

#include "mail.h"
#include <utmp.h>
#include <syslog.h>
#if !defined(__cplusplus) && !defined(c_plusplus)
# ifdef SIGPOLL
typedef void (*SIG_PF) ();
# else
typedef int (*SIG_PF) ();
# endif
#endif
#if defined(__STDC__)
# include <unistd.h>
#else
# if defined(__cplusplus) || defined(c_plusplus)
#  include <unistd.h>
# endif
#endif

static SIG_PF catcher()
{
    /* do nothing, but allow the write() to break */
    return 0;
}

#if defined(__STDC__) || defined(__cplusplus) || defined(c_plusplus)
void notify(char *user, char *msg, int check_mesg_y, char *etcdir)
#else
void notify(user, msg, check_mesg_y, etcdir)
    char *user;
    char *msg;
    int check_mesg_y;
    char *etcdir;
#endif
{
    /* search the utmp file for this user */
    SIG_PF old;
    unsigned int oldalarm;
    register FILE *utfp;
    char buf[MAXFILENAME];

    /* break out if fopen() of /etc/utmp hangs */
    old = (SIG_PF)signal(SIGALRM, (SIG_PF)catcher);
    oldalarm = alarm(300);

    /* open /etc/utmp */
    sprintf(buf, "%s/etc/utmp", etcdir);
    utfp = fopen(buf, "r");

    /* clean up our alarm */
    alarm(0);
    signal(SIGALRM, old);
    alarm(oldalarm);

    if (utfp != 0)
	{
	struct utmp utmp;
	while (fread((char*)&utmp, sizeof(utmp), 1, utfp))
	    /* grab the tty name */
	    if (strncmp(user, utmp.ut_name, sizeof(utmp.ut_name)) == 0)
		{
		char tty[sizeof(utmp.ut_line)+1];
		char dev[MAXFILENAME];
		FILE *port;
		int i;
		int fd;

		for (i = 0; i < sizeof(utmp.ut_line); i++)
		    tty[i] = utmp.ut_line[i];
		tty[i] = '\0';

		/* stick /dev/ in front */
		sprintf(dev, "%s/dev/%s", etcdir, tty);

		/* stat dir to make certain 'mesg y' is set */
		if (check_mesg_y)
		    {
		    }

		/* break out if write() to the tty hangs */
		old = (SIG_PF)signal(SIGALRM, (SIG_PF)catcher);
		oldalarm = alarm(300);

		/* check if device is really a tty */
		if ((fd = open(dev, O_WRONLY|O_NOCTTY)) == -1) {
			fprintf(stderr, "Cannot open %s.\n", dev);
			continue;
		} else {
		    if (!isatty(fd)) {
			fprintf(stderr, "%s in utmp is not a tty\n", tty);
			openlog("mail", 0, LOG_AUTH);
			syslog(LOG_CRIT, "%s in utmp is not a tty\n", tty);
			closelog();
			close(fd);
			continue;
		    }
		}
		close(fd);

		/* write to the tty */
		port = fopen(dev, "w");
		if (port != 0) {
		    (void) fprintf(port,"\r\n%s\r\n",msg);
		    (void) fclose (port);
		}

		/* clean up our alarm */
		alarm(0);
		signal(SIGALRM, old);
		alarm(oldalarm);
	    }

	(void) fclose (utfp);
    }
}
