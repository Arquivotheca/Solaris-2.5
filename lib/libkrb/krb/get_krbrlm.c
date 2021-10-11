/*
 * $Source: /mit/kerberos/src/lib/krb/RCS/get_krbrlm.c,v $
 * $Author: rfrench $
 *
 * Copyright 1985, 1986, 1987, 1988 by the Massachusetts Institute
 * of Technology.
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>.
 */

#ifndef lint
static char *rcsid_get_krbrlm_c =
"$Header: get_krbrlm.c,v 4.8 89/01/22 20:02:54 rfrench Exp $";
#endif /* lint */

#include <kerberos/mit-copyright.h>
#include <stdio.h>
#include <ctype.h>
#include <kerberos/krb.h>
#ifdef SYSV
#include <string.h>
#else
#include <strings.h>
#endif /* SYSV */

#ifdef NIS
#include <rpcsvc/ypclnt.h>
extern int krb_use_nis;		/* defined in get_krbhst.c */
#endif /* NIS */

extern int krb_debug;

/*
 * krb_get_lrealm takes a pointer to a string, and a number, n.  It fills
 * in the string, r, with the name of the nth realm specified on the
 * first line of the kerberos config file (KRB_CONF, defined in "krb.h").
 * It returns 0 (KSUCCESS) on success, and KFAILURE on failure.  If the
 * config file does not exist, and if n=1, a successful return will occur
 * with r = KRB_REALM (also defined in "krb.h").
 *
 * NOTE: for archaic & compatibility reasons, this routine will only return
 * valid results when n = 1.
 *
 * For the format of the KRB_CONF file, see comments describing the routine
 * krb_get_krbhst().
 *
#ifdef NIS
 * If NIS support is provided, then check the KRB_CONF_NISMAP map (using
 * key DEFAULT_REALM) if KRB_CONF does not exist.  If NIS is not running
 * and KRB_CONF does not exist, then use KRB_REALM to set default realm.
 * This actually calls krb_get_default_realm() below.
#endif
 */

krb_get_lrealm(r,n)
    char *r;
    int n;
{
    FILE *cnffile, *fopen();
#ifdef NIS
    char *domain;
    char *lookup;
    int err;
    int len;
#endif /* NIS */

    if (n > 1)
	return(KFAILURE);  /* Temporary restriction */

    if ((cnffile = fopen(KRB_CONF, "r")) != NULL) {
	if (fscanf(cnffile,"%s",r) != 1) {
	    (void) fclose(cnffile);
	    return(KFAILURE);
	}
	(void) fclose(cnffile);
	return(KSUCCESS);
    }

#ifdef NIS
  if (krb_use_nis) {
    /* no local conf file; try NIS */
    if (krb_debug)
	fprintf(stderr, "krb_get_lrealm: trying NIS to get realm; n=%d\n", n);
    if (yp_get_default_domain(&domain) == 0) {
	if (krb_debug)
	    fprintf(stderr, " domain `%s' looking for key `%s' map `%s'\n",
		    domain, KRB_REALM_DEFKEY, KRB_CONF_MAP);
	lookup = NULL;
	err = yp_match(domain, KRB_CONF_MAP, KRB_REALM_DEFKEY,
			strlen(KRB_REALM_DEFKEY), &lookup, &len);
	if (err && krb_debug)
	    fprintf(stderr, " yp_match ret %d (%s)\n", err, yperr_string(err));
	switch(err) {
	    case 0:
		lookup[len] = 0;
		if (krb_debug)
		    fprintf(stderr, " yp ret string `%s'\n", lookup);
		if (sscanf(lookup, "%s", r) != 1) {
		    /* line syntax is hosed; return error */
		    free(lookup);
		    return(KFAILURE);
		}
	        free(lookup);
		return(KSUCCESS);	/* realm in r */
		break;
	    case YPERR_KEY:
		/* map exists but key doesn't; return error */
		return(KFAILURE);

	    /* other NIS errors are treated as non-fatal errors; fall through */
	}
    }
  }
#endif /* NIS */

    if (n == 1) {
	(void) strcpy(r, KRB_REALM);
	return(KSUCCESS);
    }
    else
	return(KFAILURE);
}

/*
 *  Get the default realm from the domainname of the local machine.
 *  Convert to upper case before returning it.
 *  This routine must always return a valid pointer, even if it
 *  points to a NULL string.
 */
char *
krb_get_default_realm()
{
	static char *defrealm = "";
        char temp[256];
	char *p;

	if (*defrealm == '\0') {
		if (getdomainname(temp, sizeof(temp)) < 0)
			goto done;
		if ((int) strlen(temp) > 0) {
			p = (char *)malloc((strlen(temp)+(unsigned)1));
			if (p) {
				(void) strcpy(p, temp);
				defrealm = p;
				while (*p) {
					if (islower(*p))
						*p = toupper(*p);
					p++;
				}
			}
		}
	}

    done:
	if (krb_debug)
		fprintf(stderr,"krb_get_default_realm: ret defrealm `%s'\n",
			defrealm);
	return defrealm;
}
