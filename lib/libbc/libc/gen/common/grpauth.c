#pragma ident	"@(#)grpauth.c	1.3	92/07/20 SMI" /* c2 secure */

#include <stdio.h>
#include <signal.h>
#include <grp.h>
#include <sys/time.h>
#include <errno.h>

/*
 * Version to go in the BCP compatibility library in SVr4 version of
 * SunOS. This does not bother talking to rpc.pwdauthd or looking for the
 * password.adjunct file on the system since they do not exist anymore.
 * They have been effectively replaced by a more robust aging security provided
 * by the combination of /etc/shadow file, shadow support in the NIS+
 * passwd table and the use of secure RPC in NIS+.
 */

grpauth(name, password)
	char *name;
	char *password;
{

	/*
	 * this routine authenticates a password for the named user.
	 * Assumes the adjunct file does not exist.
	 * and therefore checks the group "source" using the standard
	 * getgrnam(3C) routine that uses /etc/nsswitch.conf(4).
	 */
	struct group	gr;
	struct group	*grp;

	if ((grp = getgrnam(name)) == NULL)
		/* group is not in main password system */
		return (-1);
	gr = *grp;
	if (gr.gr_passwd[0] == '#' && gr.gr_passwd[1] == '$') {
		/* this means that /etc/group has problems */
		fprintf(stderr, "grpauth: bad group entry for %s\n",
			gr.gr_name);
		return (-1);
	}
	if (strcmp(crypt(password, gr.gr_passwd), gr.gr_passwd) == 0)
		return (0);
	else
		return (-1);
}
