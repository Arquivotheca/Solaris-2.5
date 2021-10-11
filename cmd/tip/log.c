#ident	"@(#)log.c	1.10	92/07/14 SMI"	/* from UCB 4.6 6/25/83 */

#include "tip.h"

static	FILE *flog = NULL;

/*
 * Log file maintenance routines
 */

logent(group, num, acu, message)
	char *group, *num, *acu, *message;
{
	char *user, *timestamp;
	struct passwd *pwd;
	long t;

	if (flog == NULL)
		return;
#ifndef USG
	if (flock(fileno(flog), LOCK_EX) < 0) {
		perror("tip: flock");
		return;
	}
#endif
	if ((user = getlogin()) == NOSTR)
		if ((pwd = getpwuid(uid)) == NOPWD)
			user = "???";
		else
			user = pwd->pw_name;
	t = time(0);
	timestamp = ctime(&t);
	timestamp[24] = '\0';
	fprintf(flog, "%s (%s) <%s, %s, %s> %s\n",
		user, timestamp, group,
#ifdef PRISTINE
		"",
#else
		num,
#endif
		acu, message);
	fflush(flog);
#ifndef USG
	(void) flock(fileno(flog), LOCK_UN);
#endif
}

loginit()
{

#ifdef ACULOG
	flog = fopen(value(LOG), "a");
	if (flog == NULL)
		fprintf(stderr, "tip: can't open log file %s\r\n", value(LOG));
#endif
}
