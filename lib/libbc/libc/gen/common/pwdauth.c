#pragma ident	"@(#)pwdauth.c	1.3	92/07/20 SMI"  /* c2 secure */

#include <stdio.h>
#include <pwd.h>
#include <shadow.h>
#include <errno.h>

/*
 * Version to go in the BCP compatibility library in SVr4 version of
 * SunOS. This does not bother talking to rpc.pwdauthd or looking for the
 * password.adjunct file on the system since they do not exist anymore.
 * They have been effectively replaced by a more robust aging security provided
 * by the combination of /etc/shadow file, shadow support in the NIS+
 * passwd table and the use of secure RPC in NIS+.
 */

pwdauth(name, password)
	char *name;
	char *password;
{
	/*
	 * this routine authenticates a password for the named user.
	 * Assumes the adjunct file does not exist.
	 * and therefore checks the passwd "source" using the standard
	 * getpwnam(3C) routine that uses /etc/nsswitch.conf(4).
	 */

	struct passwd	*pwp = NULL;
	struct spwd	*spwp = NULL;
	char *enpwp;

	if (spwp = getspnam(name))
		enpwp = spwp->sp_pwdp;
	else if (pwp = getpwnam(name))
		enpwp = pwp->pw_passwd;
	else
		/* user is not in main password system */
		return (-1);
	if (enpwp[0] == '#' && enpwp[1] == '#') {
		/* this means that /etc/passwd has problems */
		fprintf(stderr, "pwdauth: bad passwd entry for %s\n",
		    name);
		return (-1);
	}
	if (strcmp(crypt(password, enpwp), enpwp) == 0)
		return (0);
	else
		return (-1);
}
