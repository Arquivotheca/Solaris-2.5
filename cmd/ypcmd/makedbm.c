#ident	"@(#)makedbm.c	1.8	95/07/19 SMI"
/*
 *    Copyright (c) 1986-1995 by Sun Microsystems, Inc
 */

#undef NULL
#include <stdio.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/systeminfo.h>

#include "ypdefs.h"
#include "ypsym.h"
USE_YP_MASTER_NAME
USE_YP_LAST_MODIFIED
USE_YP_INPUT_FILE
USE_YP_OUTPUT_NAME
USE_YP_DOMAIN_NAME
USE_YP_SECURE
USE_YP_INTERDOMAIN
USE_DBM

#ifdef SYSVCONFIG
extern void sysvconfig();
#endif
extern int yp_getalias();

#define	MAXLINE 4096		/* max length of input line */
static char *get_date();
static char *any();
static void addpair();
static void unmake();
static void usage();

main(argc, argv)
	int argc;
	char **argv;
{
	FILE *infp;
	datum key, content, tmp;
	char buf[MAXLINE];
	char pagbuf[MAXPATHLEN];
	char tmppagbuf[MAXPATHLEN];
	char dirbuf[MAXPATHLEN];
	char tmpdirbuf[MAXPATHLEN];
	char *p, ic;
	char *infile, *outfile;
	char outalias[MAXPATHLEN];
	char outaliasmap[MAXNAMLEN];
	char outaliasdomain[MAXNAMLEN];
	char *last_slash, *next_to_last_slash;
	char *infilename, *outfilename, *mastername, *domainname,
	    *interdomain_bind, *security, *lower_case_keys;
	char local_host[MAX_MASTER_NAME];
	int cnt, i;
	DBM *fdb;

	/* Ignore existing umask, always force 077 (owner rw only) */
	umask(077);

	infile = outfile = NULL; /* where to get files */
	/* name to imbed in database */
	infilename = outfilename = mastername = domainname = interdomain_bind = 
	    security = lower_case_keys = NULL;
	argv++;
	argc--;
	while (argc > 0) {
		if (argv[0][0] == '-' && argv[0][1]) {
			switch (argv[0][1]) {
				case 'i':
					infilename = argv[1];
					argv++;
					argc--;
					break;
				case 'o':
					outfilename = argv[1];
					argv++;
					argc--;
					break;
				case 'm':
					mastername = argv[1];
					argv++;
					argc--;
					break;
				case 'b':
					interdomain_bind = argv[0];
					break;
				case 'd':
					domainname = argv[1];
					argv++;
					argc--;
					break;
				case 'l':
					lower_case_keys = argv[0];
					break;
				case 's':
					security = argv[0];
					break;
				case 'u':
					unmake(argv[1]);
					argv++;
					argc--;
					exit(0);
				default:
					usage();
			}
		} else if (infile == NULL)
			infile = argv[0];
		else if (outfile == NULL)
			outfile = argv[0];
		else
			usage();
		argv++;
		argc--;
	}
	if (infile == NULL || outfile == NULL)
		usage();
	if (strcmp(infile, "-") != 0)
		infp = fopen(infile, "r");
	else
		infp = stdin;
	if (infp == NULL) {
		fprintf(stderr, "makedbm: can't open %s\n", infile);
		exit(1);
	}

	/*
	 *  do alias mapping if necessary
	 */
	last_slash = strrchr(outfile, '/');
	if (last_slash){
		*last_slash = '\0';
		next_to_last_slash = strrchr(outfile, '/');
		if (next_to_last_slash) *next_to_last_slash = '\0';
	} else next_to_last_slash = NULL;

#ifdef DEBUG
	if (last_slash) printf("last_slash=%s\n", last_slash+1);
	if (next_to_last_slash) printf("next_to_last_slash=%s\n",
		next_to_last_slash+1);
#endif DEBUG

	/* reads in alias file for system v filename translation */
#ifdef SYSVCONFIG
	sysvconfig();
#endif

	if (last_slash && next_to_last_slash) {
		if (yp_getalias(last_slash+1, outaliasmap, MAXALIASLEN) < 0) {
			if ((int)strlen(last_slash+1) <= MAXALIASLEN)
				strcpy(outaliasmap, last_slash+1);
			else
				fprintf(stderr,
				    "makedbm: warning: no alias for %s\n",
				    last_slash+1);
		}
#ifdef DEBUG
		printf("%s\n", last_slash+1);
		printf("%s\n", outaliasmap);
#endif DEBUG
		if (yp_getalias(next_to_last_slash+1, outaliasdomain,
		    NAME_MAX) < 0) {
			if ((int)strlen(last_slash+1) <= NAME_MAX)
				strcpy(outaliasdomain, next_to_last_slash+1);
			else
				fprintf(stderr,
				    "makedbm: warning: no alias for %s\n",
				    next_to_last_slash+1);
		}
#ifdef DEBUG
		printf("%s\n", next_to_last_slash+1);
		printf("%s\n", outaliasdomain);
#endif DEBUG
		sprintf(outalias, "%s/%s/%s", outfile, outaliasdomain,
			outaliasmap);
#ifdef DEBUG
		printf("outlias=%s\n", outalias);
#endif DEBUG

	} else if (last_slash) {
		if (yp_getalias(last_slash+1, outaliasmap, MAXALIASLEN) < 0) {
			if ((int)strlen(last_slash+1) <= MAXALIASLEN)
				strcpy(outaliasmap, last_slash+1);
			else
				fprintf(stderr,
				    "makedbm: warning: no alias for %s\n",
				    last_slash+1);
		}
		if (yp_getalias(outfile, outaliasdomain, NAME_MAX) < 0){
			if ((int)strlen(outfile) <= NAME_MAX)
				strcpy(outaliasdomain, outfile);
			else
				fprintf(stderr,
				    "makedbm: warning: no alias for %s\n",
				    last_slash+1);
		}
		sprintf(outalias, "%s/%s", outaliasdomain, outaliasmap);
	} else {
		if (yp_getalias(outfile, outalias, MAXALIASLEN) < 0){
			if ((int)strlen(last_slash+1) <= MAXALIASLEN)
				strcpy(outalias, outfile);
			else
				fprintf(stderr,
				    "makedbm: warning: no alias for %s\n",
				    outfile);
			}
	}
#ifdef DEBUG
	fprintf(stderr, "outalias=%s\n", outalias);
	fprintf(stderr, "outfile=%s\n", outfile);
#endif DEBUG

	strcpy(tmppagbuf, outalias);
	strcat(tmppagbuf, ".tmp");
	strcpy(tmpdirbuf, tmppagbuf);
	strcat(tmpdirbuf, dbm_dir);
	strcat(tmppagbuf, dbm_pag);
	if (fopen(tmpdirbuf, "w") == (FILE *)NULL) {
		fprintf(stderr, "makedbm: can't create %s\n", tmpdirbuf);
		exit(1);
	}
	if (fopen(tmppagbuf, "w") == (FILE *)NULL) {
		fprintf(stderr, "makedbm: can't create %s\n", tmppagbuf);
		exit(1);
	}
	strcpy(dirbuf, outalias);
	strcat(dirbuf, ".tmp");
	if ((fdb = dbm_open(dirbuf, O_RDWR | O_CREAT, 0644)) == NULL) {
		fprintf(stderr, "makedbm: can't open %s\n", dirbuf);
		exit(1);
	}
	strcpy(dirbuf, outalias);
	strcpy(pagbuf, outalias);
	strcat(dirbuf, dbm_dir);
	strcat(pagbuf, dbm_pag);
	while (fgets(buf, sizeof (buf), infp) != NULL) {
		p = buf;
		cnt = strlen(buf) - 1; /* erase trailing newline */
		while (p[cnt-1] == '\\') {
			p += cnt-1;
			if (fgets(p, sizeof (buf)-(p-buf), infp) == NULL)
				goto breakout;
			cnt = strlen(p) - 1;
		}
		p = any(buf, " \t\n");
		key.dptr = buf;
		key.dsize = p - buf;
		for (;;) {
			if (p == NULL || *p == NULL) {
				fprintf(stderr,
	"makedbm: source files is garbage!\n");
				exit(1);
			}
			if (*p != ' ' && *p != '\t')
				break;
			p++;
		}
		content.dptr = p;
		content.dsize = strlen(p) - 1; /* erase trailing newline */
		if (lower_case_keys) {
			for (i = (strncmp(key.dptr, "YP_MULTI_", 9) ? 0 : 9); i < key.dsize; i++) {
				ic = *(key.dptr+i);
				if (isascii(ic) && isupper(ic))
					*(key.dptr+i) = tolower(ic);
			}
		}
		tmp = dbm_fetch(fdb, key);
		if (tmp.dptr == NULL) {
			if (dbm_store(fdb, key, content, 1) != 0) {
				printf("problem storing %.*s %.*s\n",
				    key.dsize, key.dptr,
				    content.dsize, content.dptr);
				exit(1);
			}
		}
#ifdef DEBUG
		else {
			printf("duplicate: %.*s %.*s\n",
			    key.dsize, key.dptr,
			    content.dsize, content.dptr);
		}
#endif
	}
	breakout:
	addpair(fdb, yp_last_modified, get_date(infile));
	if (infilename)
		addpair(fdb, yp_input_file, infilename);
	if (outfilename)
		addpair(fdb, yp_output_file, outfilename);
	if (domainname)
		addpair(fdb, yp_domain_name, domainname);
	if (security)
		addpair(fdb, yp_secure, "");
	if (interdomain_bind)
	    addpair(fdb, yp_interdomain, "");
	if (!mastername) {
		sysinfo(SI_HOSTNAME, local_host, sizeof (local_host) - 1);
		mastername = local_host;
	}
	addpair(fdb, yp_master_name, mastername);
	(void) dbm_close(fdb);
#ifdef DEBUG
	fprintf(stderr, ".tmp ndbm map closed. ndbm successful !\n");
#endif
	if (rename(tmppagbuf, pagbuf) < 0) {
		perror("makedbm: rename");
		unlink(tmppagbuf);		/* Remove the tmp files */
		unlink(tmpdirbuf);
		exit(1);
	}
	if (rename(tmpdirbuf, dirbuf) < 0) {
		perror("makedbm: rename");
		unlink(tmppagbuf); /* Remove the tmp files */
		unlink(tmpdirbuf);
		exit(1);
	}
/*
	sprintf(buf, "mv %s %s", tmppagbuf, pagbuf);
	if (system(buf) < 0)
		perror("makedbm: rename");
	sprintf(buf, "mv %s %s", tmpdirbuf, dirbuf);
	if (system(buf) < 0)
		perror("makedbm: rename");
*/
	exit(0);
}


/*
 * scans cp, looking for a match with any character
 * in match.  Returns pointer to place in cp that matched
 * (or NULL if no match)
 */
static char *
any(cp, match)
	register char *cp;
	char *match;
{
	register char *mp, c;

	while (c = *cp) {
		for (mp = match; *mp; mp++)
			if (*mp == c)
				return (cp);
		cp++;
	}
	return ((char *)0);
}

static char *
get_date(name)
	char *name;
{
	struct stat filestat;
	static char ans[MAX_ASCII_ORDER_NUMBER_LENGTH];
	/* ASCII numeric string */

	if (strcmp(name, "-") == 0)
		sprintf(ans, "%010ld", (long) time(0));
	else {
		if (stat(name, &filestat) < 0) {
			fprintf(stderr, "makedbm: can't stat %s\n", name);
			exit(1);
		}
		sprintf(ans, "%010ld", (long) filestat.st_mtime);
	}
	return (ans);
}

void
usage()
{
	fprintf(stderr,
	    "usage: makedbm -u file\n	makedbm [-b] [-l] [-s] [-i YP_INPUT_FILE] "
	    "[-o YP_OUTPUT_FILE] [-d YP_DOMAIN_NAME] [-m YP_MASTER_NAME] "
	    "infile outfile\n");
	exit(1);
}

void
addpair(fdb, str1, str2)
DBM *fdb;
char *str1, *str2;
{
	datum key;
	datum content;

	key.dptr = str1;
	key.dsize = strlen(str1);
	content.dptr  = str2;
	content.dsize = strlen(str2);
	if (dbm_store(fdb, key, content, 1) != 0){
		printf("makedbm: problem storing %.*s %.*s\n",
		    key.dsize, key.dptr, content.dsize, content.dptr);
		exit(1);
	}
}

void
unmake(file)
	char *file;
{
	datum key, content;
	DBM *fdb;

	if (file == NULL)
		usage();
	
	if ((fdb = dbm_open(file, O_RDWR | O_CREAT, 0644)) == NULL) {
		fprintf(stderr, "makedbm: couldn't init %s\n", file);
		exit(1);
	}
	for (key = dbm_firstkey(fdb); key.dptr != NULL;
		key = dbm_nextkey(fdb)) {
		content = dbm_fetch(fdb, key);
		printf("%.*s %.*s\n", key.dsize, key.dptr,
		    content.dsize, content.dptr);
	}
	dbm_close(fdb);
}
