
/* this file implement %l %y %x matching that is used */
/* in sun's sendmail.cf file 			      */

#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include "sendmail.h"
#include <sys/file.h>
#include <string.h>
#include <fcntl.h>
#include <ndbm.h>
#include <nsswitch.h>
#include <ns_switch.h>
#include <rpcsvc/ypclnt.h>

#include <sys/mnttab.h> /* sysv */
#include <sys/mntent.h>
#define mntent mnttab
#define mnt_fsname mnt_special
#define mnt_dir mnt_mountp
#define mnt_type mnt_fstype
#define mnt_opts mnt_mntopts
#include <string.h>

#ifdef PERCENT_MATCH

#define DOT_TERMINATED(x) ((x)[strlen(x) - 1] == '.')
#define PARTIAL_NAME(x) ((x)[strlen(x) - 1] != '.')

bool lookup();
bool nss_getcanonname();

bool
do_percent_match(avpp, tokbuf, rp)
char ***avpp;
char *tokbuf;
char *rp;
{
	char **oldavp;
	register char **avp;

	/* match any token in (not in) a NIS map. */
	oldavp = avp = *avpp;
	tokbuf[0] = '\0';
	if ((rp[1] == 'y') ||  /* hosts map */
	    (rp[1] == 'x') ||  /* mx record  */
	    (rp[1] == 'l') ) { /* local hosts list */
		/* construct max qualify name   */
		/* string is expected to be 	*/
		/* terminated by:		*/
		/*  ".LOCAL" ".uucp"  or ">"	*/
		/* e.g 				*/
		/* "user@host.sub1.sub2.LOCAL"	*/
		/* "user@host.sub1.sub2.uucp"	*/
		/* or  "user<@host.sub1.sub2>" 	*/
		while (*avp != NULL) {
			if (!strcmp(avp[0], ">"))
				break;
			if (!strcmp(avp[0], ".") &&
			    (!strcmp(avp[1], "LOCAL") ||
			    !strcmp(avp[1], "uucp")))
				break;
			(void) strcat(tokbuf, *avp++);
		}
	} else  { /* other map */
		/* just get the first token */
		if (*avp != NULL)
			(void) strcat(tokbuf, *avp++);
	}

	if (tokbuf[0] != '\0') {
	    char *end;
	    int stat;

	    end = &tokbuf[strlen(tokbuf)];
	    if (tTd(38, 13))
		printf( "trying do_match(%s, %c)...\n", tokbuf, rp[1]);

	    stat = EX_OK;
	    do_match(tokbuf, rp[1], &stat);
	    while (stat == EX_NOTFOUND) {
		if  (avp != oldavp) {
			/* strip one token */
			end =  end - strlen(*(--avp));
			*end = '\0';
		}
		if ((avp != oldavp) &&
		    !strcmp(avp[-1], ".")) {
		    /* if we end up with a  */
		    /* trailing dot, strip  */
		    /* one more token	    */
			end =  end - strlen(*(--avp));
			*end = '\0';
		}
		if (avp == oldavp)
			break;

		if (tTd(38, 13))
			printf("trying do_match(%s, %c)...\n",
				 tokbuf, rp[1]);
	    	do_match(tokbuf, rp[1], &stat);
	    }
	    if ((stat != EX_OK) && (stat != EX_NOTFOUND))
		return -1; /* TEMPFAIL */
	}

	if (avp == oldavp) { /* no match */
		if (tTd(38, 13))
			printf("do_match(): no match found\n");
		if ((*rp  & 0377) == MATCHPERCENT) {
			*avpp = avp;
			return  0; /*backup*/
		} else {
			/* just eat up one token */
			/* for MATCHNPERCENT case */
			avp++;
		}
	} else  { /* got match */
		if (tTd(38, 13))
			printf("do_match(%s, %c) match found\n",
			tokbuf, rp[1]);
		if ((*rp & 0377) == MATCHNPERCENT) {
			avp = oldavp;
			*avpp = avp;
			return  0; /*backup*/
		}
	}
	*avpp = avp;
	return 1;
}
 


/*
** lookup a string in a name service 
**
**/
do_match(string, mac, statp)
    char *string;
    char mac;
    int *statp;
{

	*statp = EX_OK;
	if (mac == 'x') /* mx lookup */
	{
		int rcode;
		char *mxhosts[MAXMXHOSTS + 1];
		int nmx;

		rcode = EX_OK;
		nmx = getmxrr(string, mxhosts, TRUE, &rcode);
		if ((nmx <= 0) || (rcode != EX_OK)) {
			*statp = EX_NOTFOUND;
			return;
		} else	return;
	}
	name_service_match(string,mac, statp);
}

/*
**  NAME_SERVICE_MATCH --look up any token in the Network Information Services.
**			or nis+
**
**	Parameters:
**		string - string to look up
**		mac - macro to use as name of map
**
**	Returns:
**		True if the value was found in the database
**
**	Side Effects:
**		If token is found, enter into cache.
**		Note the careful copying of the string, converting it
**		to lower case before doing the NIS lookup
**
**	Warnings:
**		Will hang forever if no name server responds
*/



name_service_match(string, mac, statp)
char *string;
char mac;
int *statp;
{
	char *mapname;
	STAB *map;
	int stat;
	char *av[2];
	char namebuf[MAXNAME];
	

	*statp = EX_OK;
	strcpy(namebuf, string);
	makelower(namebuf);

	if ((mac == 'y') || (mac == 'l')) 
		mapname = "hosts.byname";
	else 	mapname = macvalue(mac, CurEnv);

	if (mapname == NULL) {
		syserr("name_service_match: Undefine mapname %%%c", mac);
		return;
	}

	if (!strcmp("hosts.byname", mapname)) {
		int len_m, len_n;
		char *m;
		extern h_errno;

		/* if you change this block of code */
		/* make sure it work for the address*/
		/* with abbreviated domain name:    */
		/* e.g awc@estelle.eng		    */
		m = macvalue('m', CurEnv);
		if (mac == 'l') {
			if (!DOT_TERMINATED(namebuf))	
				strcat(namebuf, ".");
			strcat(namebuf, m);
		}

		h_errno = 0;
	 	if (nss_getcanonname(namebuf, MAXNAME-1, TRUE)) {
			*statp = EX_OK;
		} else {
			if (h_errno == 0)
				*statp = EX_OK;
			else if (h_errno == HOST_NOT_FOUND)
				*statp = EX_NOTFOUND;
			else	*statp = EX_TEMPFAIL;
		}
			
		if (mac != 'l')
			return;

		len_m = strlen(m);
			len_n = strlen(namebuf);

		if (len_m >= len_n) {
			*statp = EX_NOTFOUND;
			return;
		}

		/* if namebuf end with $m subfix => local */
		if (!strcasecmp(m, &namebuf[len_n -len_m]))
			return;

		*statp = EX_NOTFOUND;
		return FALSE;
	}

	lookup(mapname, namebuf, NULL, statp);
}

yp_lookup(mapname, keybuf, ansbuf, statp)
char *mapname;
char *keybuf;
char *ansbuf;
int *statp;
{
	char domain[MAXNAME];
	int keylen;
	char *vp;
	int vsize;
	int yperr;

	if (tTd(38, 13))
		printf("yp_lookup(%s, %s)\n", mapname, keybuf);

	getdomainname(domain, sizeof domain); 
	keylen = strlen(keybuf);
	yperr = yp_match(domain, mapname, keybuf, keylen,
			 &vp, &vsize);
	if (yperr == YPERR_KEY) {
		keylen++;
		yperr = yp_match(domain, mapname, keybuf, keylen,
                         &vp, &vsize);
	}
	if (yperr == 0) {
		if (ansbuf) {
			strncpy(ansbuf, vp, vsize);
			ansbuf[vsize] = '\0';
		}
		*statp = EX_OK;
		return;
	}
	if (yperr == YPERR_KEY)
		*statp = EX_NOTFOUND;
	else	*statp = EX_TEMPFAIL;
}


#undef NIS /* symbol conflict in nis.h */
#include <rpcsvc/nis.h>
#include <rpcsvc/nislib.h>
#define EN_col(col) zo_data.objdata_u.en_data.en_cols.en_cols_val[(col)].ec_value.ec_value_val

nisplus_lookup(mapname, keybuf, ansbuf, statp)
char *mapname;
char *keybuf;
char *ansbuf;
int *statp;
{
	char qbuf[MAXLINE + NIS_MAXNAMELEN];
	char *nisplus_mapname;
	char *key_column;
	int value_column_idx;
	char *vp;
        nis_result *result;
 


	value_column_idx = 1;
	fix_mapname(mapname, &nisplus_mapname, 
		    &key_column, &value_column_idx);
#ifdef BUGFIX /* 1194262 */
        /* construct the query */
        if (PARTIAL_NAME(nisplus_mapname))
                sprintf(qbuf, "[%s=%s],%s.%s", key_column, keybuf, nisplus_mapname,
                         nisplus_default_domain());
        else
                sprintf(qbuf, "[%s=%s],%s", key_column, keybuf, nisplus_mapname);
#else
        sprintf(qbuf, "[%s=%s],%s", key_column, keybuf, nisplus_mapname);
#endif
        if (tTd(38, 20))
                printf("nisplus_lookup(), qbuf=%s\n", qbuf);

	result = nis_list(qbuf, FOLLOW_LINKS | FOLLOW_PATH, NULL, NULL);
        if (result->status == NIS_SUCCESS) {
                int count;
		char *str;

        	if ((count = NIS_RES_NUMOBJ(result)) != 1) {
			if (LogLevel > 10)
                        syslog(LOG_WARNING,
                          "%s:Lookup error, expected 1 entry, got (%d)",
                            mapname, count);
			/* ignore second entry */
			if (tTd(38, 20))
				printf("nisplus_lookup(%s), got %d entries, addtional entries ignores\n", keybuf, count);
                } 

                vp = ((NIS_RES_OBJECT(result))->EN_col(value_column_idx));
		strcpy(ansbuf, vp);
		*statp = EX_OK;
		return;
	}
        if (result->status == NIS_NOTFOUND)
		*statp = EX_NOTFOUND;
	else 	*statp = EX_TEMPFAIL;

	if (tTd(38, 13))
		printf(
	"nisplus_lookup(%s, %s) failed, error code = %d\n",
			 mapname, keybuf, result->status);
	return;
}

#endif


#ifdef SUNLOOKUP
bool
lookup(mapname, keybuf, ansbuf, statp)
char *mapname;
char *keybuf;
char *ansbuf;
int *statp;
{
	char *p;
        struct __nsw_switchconfig *nsw_conf;
        enum __nsw_parse_err pserr;
        struct __nsw_lookup *lk;
	char tmpbuf[MAXNAME];
	static struct __nsw_lookup lkp2 =  { "nis", DEF_ACTION, NULL, NULL};
	static struct __nsw_lookup lkp1 =  { "nisplus", DEF_ACTION, NULL, &lkp2};
	static struct __nsw_switchconfig lkp_default = { 0, "aliases" , 2, &lkp1};

	
	if (tTd(38, 20))
		printf("lookup(%s, %s)\n", mapname, keybuf);

	strcpy(tmpbuf, keybuf);
	makelower(tmpbuf);

	/* this is really a hack, we are using the aliases entry in */
	/* /etc/nsswitch.conf, to guess wheather the user want yp   */
	/* or nisplus                                               */
        if ((nsw_conf = __nsw_getconfig("aliases", &pserr)) == NULL)
                nsw_conf = &lkp_default;
        lk = nsw_conf->lookups;

        while (lk) {
                if (!strcmp("nis", lk->service_name)) {
			return yp_lookup(mapname, tmpbuf, ansbuf, statp);
                }
                if (!strcmp("nisplus", lk->service_name)) {
			return nisplus_lookup(mapname, tmpbuf, ansbuf, statp);
                }
                lk = lk->next;
        }
        return;
}


/* map old yp map name to nisplus table name to ease the */
/*  transition pain of going from yp to nisplus		 */
fix_mapname(mapname, nisplus_mapname, key_column_name, value_column_idx)
char *mapname;
char **nisplus_mapname;
char **key_column_name;
int  *value_column_idx;
{
	if (!strcmp("mail.byaddr", mapname) ||
	    !strcmp("REVERSE.mail_aliases.org_dir", mapname)) {
		*nisplus_mapname = "mail_aliases.org_dir";
		*key_column_name = "expansion";
		*value_column_idx = 0;
		return;
	}
	if (!strcmp("hosts.byaddr", mapname)) {
		*nisplus_mapname = "hosts.org_dir";
		*key_column_name = "addr";
		*value_column_idx = 0;
		return;
	}
	if (!strcmp("mail.byname", mapname)) {
		*nisplus_mapname = "mail_aliases.org_dir";
		*key_column_name = "alias";
		*value_column_idx = 1;
		return;
	}
	/* *IMPORTANT* all other user table */
	/* should be a "key"/"vlaue" table  */
	/* "key" column must be column zero */
	*nisplus_mapname = mapname;
	*key_column_name = "key";
	*value_column_idx = 1;
}

#endif


#ifdef REMOTE_MODE
RemoteDefault()
{
	/*
	 * Search through mtab to see which server /var/mail
	 * is mounted from.  Called when remote mode is set, but no
	 * server is specified.  Deliver locally if mailbox is local.
	 */
	FILE *mfp;
	struct mntent *mnt;
	struct mnttab sysvmnt;
	char *endhost;			/* points past the colon in name */
	int bestlen = 0, bestnfs = 0;
	int len;
	char mailboxdir[256];		/* resolved symbolic link */
	static char bestname[256];	/* where the name ends up */
	char linkname[256];		/* for symbolic link chasing */
	struct stat stb;

	(void) strcpy(mailboxdir, "/var/mail");
	while (1) {
		if ((len =
		    readlink(mailboxdir, linkname, sizeof (linkname))) < 0)
			break;
		linkname[len] = '\0';
		(void) strcpy(mailboxdir, linkname);
	}

	stat("/var/mail/:saved", &stb);
	mfp = fopen(MNTTAB, "r");

	if (mfp == NULL)  {
		printf("Unable to open mount table\n");
		return (EX_OSERR);
	}
	(void) strcpy(bestname, "");

	mnt = &sysvmnt;
	while ((getmntent(mfp, mnt)) >= 0) {
		len = preflen(mailboxdir, mnt->mnt_dir);
                if ((len != 0) && (len >= bestlen) &&
                    (strcmp(mnt->mnt_type, "nfs") == 0) ) {
			bestlen = len;
			(void) strcpy(bestname, mnt->mnt_fsname);
		}
	}
	fclose(mfp);
	endhost = strchr(bestname, ':');
	if (endhost && bestlen > 4) {
		/*
		 * We found a remote mount-point for /var/spool/mail --
		 * save the host name. The test against "4" is because we do not
		 * want to be fooled by mounting "/" or "/var" only.
		 */
		RemoteMboxHost = bestname;
		*endhost = 0;
		return (EX_OK);
	}
	/*
	 * No remote mounts - assume local
	 */
	RemoteMboxHost = NULL;
}

/*
 * Returns: length of second argument if it is a prefix of the
 * first argument, otherwise zero.
 */
preflen(str, pref)
char *str, *pref;
{
        int len;

        len = strlen(pref);
        if (strncmp(str, pref, len) == 0)
                return (len);
        return (0);
}
#endif
