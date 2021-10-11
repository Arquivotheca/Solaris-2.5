/*
 * Copyright (c) 1991-92 by Sun Microsystems, Inc.
 */

#ident	"@(#)mkkrbconf.c	1.3	92/07/14 SMI"

/*
 *  Filter to convert kerberos configuration file in /etc/krb.conf
 */

#include <stdio.h>
#include <ctype.h>

char buf[BUFSIZ];

struct key {
	char *name;
	int   count;
	struct key *next;
};

struct key *Keys = NULL;
char *Prog;

#define	KRB_REALM_DEFKEY "DEFAULT_REALM"

main(argc, argv)
int argc;
char *argv[];
{
	register char *p;
	char *configfile;
	char name[BUFSIZ];
	FILE *conf;
	int lineno;

	Prog = argv[0];
	if (argc != 2) {
		fprintf(stderr, "%s configfile\n", Prog);
		exit(1);
	}
	configfile = argv[1];

	if ((conf = fopen(configfile, "r")) == NULL) {
		perror(configfile);
		exit(1);
	}

	for (lineno = 1; fgets(buf, BUFSIZ, conf); lineno++) {
		if (buf[strlen(buf) - 1] != '\n') {
			fprintf(stderr, "%s: line %d too long (max %d)\n",
				Prog, lineno, BUFSIZ);
			exit(1);
		}

		/* make sure realm names are all upper case */
		for (p = buf; *p; p++) {
			if (lineno > 1 && (*p == ' ' || *p == '\t'))
				break;
			if (islower(*p))
				*p = toupper(*p);
		}

		if (lineno == 1) {
			printf("%s %s", KRB_REALM_DEFKEY, buf);
		} else {
			if (buf[0] == '+')
				continue;
			if (sscanf(buf, "%s", name) != 1)
				continue;
			printf("%s.%d %s", name, keycount(name), buf);
		}
	}

	(void) fclose(conf);
	exit(0);
}

/*
 *  Search the keylist looking for name.  If found, bump the count and
 *  return it.  If not found, create new key struct, set count to 1
 *  and return it.
 */
keycount(name)
char *name;
{
	register struct key *kp;

	for (kp = Keys; kp; kp = kp->next) {
		if (!strcmp(name, kp->name)) {
			/* found match */
			kp->count++;
			return (kp->count);
		}
	}

	/* no match found */
	kp = (struct key *)malloc(sizeof (struct key));
	if (kp == NULL) {
		fprintf(stderr, "%s: can't allocate memory: ", Prog);
		perror("");
		exit(1);
	}
	if ((kp->name = (char *)malloc(strlen(name) + 1)) == NULL) {
		fprintf(stderr, "%s: can't allocate memory: ", Prog);
		perror("");
		exit(1);
	}
	strcpy(kp->name, name);
	kp->count = 1;
	kp->next = Keys;
	Keys = kp;

	return (kp->count);
}
