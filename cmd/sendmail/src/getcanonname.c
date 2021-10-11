#include "sendmail.h"
#include "ns_switch.h"

/* this file is needed to work around bug 1140279 */

#define DEF_ACTION {1, 0, 0, 0}
static struct __nsw_lookup lkp2 =  { "files", DEF_ACTION, NULL, NULL};
static struct __nsw_lookup lkp1 =  { "nisplus", DEF_ACTION, NULL, &lkp2};
static struct __nsw_lookup lkp0 =  { "nis", DEF_ACTION, NULL, &lkp1};
static struct __nsw_switchconfig lkp_default = { 0, __NSW_HOSTS_DB , 3, &lkp0};
 


extern bool quick_convert();

#ifdef TEXT
files_getcanonname(name, hbsize, nsw_statp)
    	char *name;
        int hbsize;
	int *nsw_statp;
{
	int buflen;
	char linebuf[MAXLINE];
	char buf1[MAXNAME+1];
	char buf2[MAXNAME+1];
	char delim;
	char key[MAXNAME];
	int key_idx;
	char *cname;
	int clen;
	bool found;
	char * get_column();
	char *domain;
	FILE *f;

#define CNAME_IDX 1

	quick_convert(name, key, FALSE);
	if (key[0] == '\0')
		strcpy(key, name);

	/* we only accept single token search key */
	if (strchr(key, '.')) {
		*nsw_statp = __NSW_NOTFOUND;
		return;
	}

	found = FALSE;

	f=fopen("/etc/inet/hosts", "r");

	if (f == NULL) {
#ifdef MAP_EXIT_STAT
		*nsw_statp = __NSW_UNAVAIL;
#endif
		return;
	}
	delim = '\t';
	while (fgets(linebuf, MAXLINE, f)) {
		char *lf;
		char *str;
		if (linebuf[0] == '#')
			continue; /* skip comment */
		if (lf = strchr(linebuf, '\n'))
			*lf = '\0';
		cname = get_column(linebuf, CNAME_IDX, delim, buf1);
		if ((cname != NULL) && !strcasecmp(key,  cname)) {
			fclose(f);
			found = TRUE;
			break;
		}
		
		key_idx = 2;
		while (str = get_column(linebuf, key_idx, delim, buf2)) {
			if (str == NULL)
				break;
			if (!strcasecmp(key, str)) {
				char *d;
				fclose(f);
				found = TRUE;
				break;
			}
			key_idx++;
		}
	}
	
	fclose(f);
	if (!found) {
		*nsw_statp = __NSW_NOTFOUND;
		return;
	}

	clen = strlen(cname);
	if (strchr(cname, '.')) { /* if multi token */
		char shortname[MAXNAME];

		/* see if we need to tag on $m */
		quick_convert(cname, shortname, TRUE);
		if (shortname[0] == '\0') {
			if (hbsize > clen) {
				strcpy(name, cname);
				*nsw_statp = __NSW_SUCCESS;
			} else	*nsw_statp = __NSW_NOTFOUND;
			return;
		}
		strcpy(cname, shortname);
	}

	/* tag on $m */
	domain = macvalue('m', CurEnv);
	if (hbsize > (clen + ((int) strlen(domain)))) {
		sprintf(name, "%s.%s", cname, domain);
		*nsw_statp = __NSW_SUCCESS;
	} else	*nsw_statp = __NSW_NOTFOUND;
}
#endif TEXT

#ifdef NIS
#include <rpcsvc/ypclnt.h>
nis_getcanonname(name, hbsize, nsw_statp)
    	char *name;
        int hbsize;
	int *nsw_statp;
{
	char *vp;
	auto int vsize;
	int keylen;
	int yperr;
	static bool try0null = TRUE;
	static bool try1null = TRUE;
	static char *yp_domain = NULL;
	char *domain, *p;
	char host_record[MAXLINE];
	char buf[MAXNAME];
	char key[MAXNAME];
	char *cname;
	int clen;
	extern char *get_column();

	if (tTd(38, 20))
		printf("nis_getcanonname(%s)\n", name);

	quick_convert(name, key, FALSE);
	if (key[0] == '\0')
		strcpy(key, name);

	/* we only accept single token search key */
	if (strchr(key, '.')) {
		*nsw_statp = __NSW_NOTFOUND;
		return;
	}

	keylen = strlen(key);

	if (yp_domain == NULL)
		yp_get_default_domain(&yp_domain);
	makelower(key);
	yperr = YPERR_KEY;
	if (try0null)
	{
		yperr = yp_match(yp_domain, "hosts.byname", key, keylen,
			     &vp, &vsize);
		if (yperr == 0)
			try1null = FALSE;
	}
	if (yperr == YPERR_KEY && try1null)
	{
		keylen++;
		yperr = yp_match(yp_domain, "hosts.byname", key, keylen,
			     &vp, &vsize);
		if (yperr == 0)
			try0null = FALSE;
	}
	if (yperr != 0)
	{
		if (yperr == YPERR_KEY)
			*nsw_statp = __NSW_NOTFOUND;	
		else if (yperr == YPERR_BUSY)
			*nsw_statp = EX_TEMPFAIL;	
		else *nsw_statp = __NSW_UNAVAIL;	
		return NULL;
	}
	strncpy(host_record, vp, vsize);
	host_record[vsize] = '\0';
	cname = get_column(host_record, 1, '\t', buf);
	if (cname == NULL) {
		/* this should not happen, but.... */
		*nsw_statp = __NSW_NOTFOUND;
		return;
	}
	clen = strlen(cname);
	if (strchr(cname, '.')) { /* if multi token */
		char shortname[MAXNAME];

		/* see if we need to tag on $m */
		quick_convert(cname, shortname, TRUE);
		if (shortname[0] == '\0') {
			if (hbsize > clen) {
				strcpy(name, cname);
				*nsw_statp = __NSW_SUCCESS;
			} else	*nsw_statp = __NSW_NOTFOUND;
			return;
		}
		strcpy(cname, shortname);
	}

	/* tag on $m */
	domain = macvalue('m', CurEnv);
	if (hbsize > (clen + ((int) strlen(domain)))) {
		sprintf(name, "%s.%s", cname, domain);
		*nsw_statp = __NSW_SUCCESS;
	} else	*nsw_statp = __NSW_NOTFOUND;
}
#endif /* NIS */

#undef NIS /* symbol conflict in nis.h */
#include <rpcsvc/nis.h>
#include <rpcsvc/nislib.h>
#define EN_col(col) zo_data.objdata_u.en_data.en_cols.en_cols_val[(col)].ec_value.ec_value_val

nisplus_getcanonname(name, hbsize, nsw_statp)
    	char *name;
        int hbsize;
	int *nsw_statp;
{
	char *vp;
	auto int vsize;
	int buflen;
	char buf1[MAXLINE + NIS_MAXNAMELEN];
	static char *nis_path=NULL;
	static char nis_path_buf[MAXLINE];
        nis_result *result;
	char *p;
	int len;
	char key[MAXNAME];

	if (nis_path == NULL) {
		char *ptr;
		char pathbuf[MAXLINE];

		strcpy(buf1, macvalue('m', CurEnv));
		strcat(buf1, ".");
		/* try the host tabe in $m */
		if (hosts_table_ok(buf1)) 
			strcpy(pathbuf,buf1);
		else 	strcpy(pathbuf, "$"); 

		nis_path = nis_path_buf;
		sprintf(nis_path, "NIS_PATH=%s", pathbuf); 
	}

	if (nis_path[0] != '\0')	
		putenv(nis_path);
	else 	syslog(LOG_WARNING, "no NIS+ path defined");

	quick_convert(name, key, FALSE);
	if (key[0] == '\0')
		strcpy(key, name);

	p = strchr(key, '.');
	if (p == NULL) /* single token */
		sprintf(buf1, "[%s=%s],%s", "name", key, "hosts.org_dir");
	else if (p[1]) { /* multi token */
		*p = '\0'; /* take only first token in key buf */
		sprintf(buf1, "[%s=%s],%s.%s", "name", key, "hosts.org_dir", &p[1]);
	} else { *nsw_statp = __NSW_NOTFOUND;
		 return;
	}
	
	if (tTd(38, 20))
		printf("\nnisplus_getcanoname(%s), qbuf=%s\n%s\n",
			 name, buf1, nis_path);
	
        result = nis_list(buf1, EXPAND_NAME| FOLLOW_LINKS | FOLLOW_PATH, NULL, NULL);
	/* unset NIS_PATH, just in case */
	unsetenv("NIS_PATH");

        if (result->status == NIS_SUCCESS) {
                int count;
		char *str;
		char *domain;

        	if ((count = NIS_RES_NUMOBJ(result)) != 1) {
			if (LogLevel > 10)
                        syslog(LOG_WARNING,
                          "nisplus_getcanonname():Lookup error, expected 1 entry, got (%d)",
                             count);
			/* ignore second entry */
			if (tTd(38, 20))
				printf("nisplus_getcanoname(%s), got %d entries, addtional entries ignores\n", name);
                } 
		if (tTd(38, 20))
			printf( "nisplus_getcanoname(%s), found in directory \"%s\"\n",
			    name, (NIS_RES_OBJECT(result))->zo_domain);
			

                vp = ((NIS_RES_OBJECT(result))->EN_col(0));
                /* set the length of the result */
                vsize = strlen(vp);
		if (tTd(38, 20))
			printf("nisplus_getcanonname(%s), found %s\n", 
				name, vp);
		domain = macvalue('m', CurEnv);
		if (hbsize > (vsize + ((int) strlen(domain)))) {
			sprintf(name, "%s.%s", vp, domain);
			*nsw_statp = __NSW_SUCCESS;
		} else	*nsw_statp = __NSW_NOTFOUND;
        	nis_freeresult(result);
		return;
        } else {
        	if (result->status == NIS_NOTFOUND)
			*nsw_statp = __NSW_NOTFOUND;
        	else if (result->status == NIS_TRYAGAIN)
			*nsw_statp = __NSW_TRYAGAIN;
		else { 
			*nsw_statp = __NSW_UNAVAIL;
	     	}
        }
	if (tTd(38, 20))
		printf("nisplus_getcanonname(%s), failed, status=%d, nsw_stat=%d\n", name, result->status, *nsw_statp);
       	nis_freeresult(result);
	return;
}

#include <netdb.h>
dns_getcanonname(name, hbsize, trymx, nsw_statp)
    	char *name;
        int hbsize;
	int *nsw_statp;
{
	extern int h_errno;

	h_errno = 0;
	if (getcanonname(name, hbsize, trymx)) {
		char *d;

		*nsw_statp = __NSW_SUCCESS;
		/* if returned cname is still single token */
		/* compensate by tagging on $m		   */
		/* this is because some site setup their   */
		/* DNS database wrong.			   */
		if(strchr(name, '.') == NULL) {
			d = macvalue('m', CurEnv);
			if (hbsize > (int) (strlen(name) + strlen(d) + 1)) {
				strcat(name, ".");
				strcat(name, d);
			} else  *nsw_statp = __NSW_NOTFOUND;
		}
		return;
	}
	
	switch (h_errno)
	{ 
		case TRY_AGAIN:
			if (!VERSION(SUN,1)) {
			 	*nsw_statp = __NSW_TRYAGAIN;
			 	break;
			} /* else fall thru */
		case NO_DATA:
			/* fall thur */
		case HOST_NOT_FOUND:
			*nsw_statp = __NSW_NOTFOUND;
			break;
		default: 
			if (tTd(38, 20))
				printf("dns_getcanonname(%s), h_errno=%d\n", name, h_errno);
			*nsw_statp = __NSW_UNAVAIL;
	}
}

bool
nss_getcanonname(host, hbsize, trymx)
	char *host;
	int hbsize;
	bool trymx;
{
	extern int h_errno;
	register MAP *mymap;
	register char *vp;
	STAB *s;
	int nsw_rc;
	char key[MAXNAME];

	/* in solaris, lookup path is defined */
	/* in /etc/nsswitch.conf	      */

	struct __nsw_switchconfig *nsw_conf;
	enum __nsw_parse_err pserr;
	struct __nsw_lookup *lk;

	quick_convert(host, key, FALSE);
	if (key[0] == '\0')
		strcpy(key, host);
		

	if ((nsw_conf = __nsw_getconfig("hosts", &pserr)) == NULL)
                nsw_conf = &lkp_default; 
	lk = nsw_conf->lookups;


	nsw_rc = __NSW_NOTFOUND;
	vp = NULL;
	while (lk) { 
		if (tTd(38, 20))
			printf("nss_getcanonname(%s), trying %s\n", key, lk->service_name);
		if (strcmp("files", lk->service_name) == 0)
			files_getcanonname(key, MAXNAME, &nsw_rc);
		else if (strcmp("nis", lk->service_name) == 0)
			nis_getcanonname(key, MAXNAME, &nsw_rc);
		else if (strcmp("nisplus", lk->service_name) == 0)
			nisplus_getcanonname(key, MAXNAME, &nsw_rc);
		else if (strcmp("dns", lk->service_name) == 0)
			dns_getcanonname(key, MAXNAME, trymx, &nsw_rc);

		if (__NSW_ACTION(lk, nsw_rc) == __NSW_RETURN)
			break;
		lk = lk->next;
	}

	if (nsw_rc  == __NSW_SUCCESS)  {
		if (tTd(38, 20))
			printf("nss_getcanonname(%s), found\n", key);
		if (hbsize > (int) strlen(key)) {
			strcpy(host, key);
			return TRUE;
		} else  return FALSE;
	}

	if (tTd(38, 20))
		printf("nss_getcanonname(%s), failed, nsw_rc=%d\n", key, nsw_rc);

	if (nsw_rc  == __NSW_NOTFOUND) 
		h_errno = HOST_NOT_FOUND;
	else 	h_errno = TRY_AGAIN;

	return FALSE;
}
