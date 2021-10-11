/*
 * $Source: /mit/kerberos/src/lib/krb/RCS/get_admhst.c,v $
 * $Author: jtkohl $
 *
 * Copyright 1985, 1986, 1987, 1988 by the Massachusetts Institute
 * of Technology.
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>.
 */

#ifndef lint
static char *rcsid_get_admhst_c =
"$Header: get_admhst.c,v 4.0 89/01/23 10:08:55 jtkohl Exp $";
#endif /* lint */

#include <kerberos/mit-copyright.h>
#include <stdio.h>
#include <kerberos/krb.h>
#include <string.h>
#ifdef SYSV
#define index strchr
#endif /* SYSV */

#ifdef NIS
#include <ctype.h>
#include <rpcsvc/ypclnt.h>
extern int krb_use_nis;			/* defined in get_krbhst.c */
#endif /* NIS */

extern int krb_debug;

/*
 * Given a Kerberos realm, find a host on which the Kerberos database
 * administration server can be found.
 *
 * krb_get_admhst takes a pointer to be filled in, a pointer to the name
 * of the realm for which a server is desired, and an integer n, and
 * returns (in h) the nth administrative host entry from the configuration
 * file (KRB_CONF, defined in "krb.h") associated with the specified realm.
 *
 * On error, get_admhst returns KFAILURE. If all goes well, the routine
 * returns KSUCCESS.
 *
 * For the format of the KRB_CONF file, see comments describing the routine
 * krb_get_krbhst().
 *
 * This is a temporary hack to allow us to find the nearest system running
 * a Kerberos admin server.  In the long run, this functionality will be
 * provided by a nameserver.
 *
#ifdef NIS
 * When NIS is used, the local KRB_CONF file is searched first.  If this
 * file is not present, then the NIS KRB_CONF_NISMAP map (defined in krb.h)
 * is used.  If KRB_CONF is present and the desired server is not located
 * in it, then the NIS map is used to continue the search if there is
 * a line in KRB_CONF starting with a '+' character.  Everything in
 * KRB_CONF after the + is ignored by this routine.
 *
 * If n==1, there is no match in KRB_CONF, and the NIS is not running,
 * then return KRB_HOST if requested realm matches local realm.
#endif 
 */

krb_get_admhst(h, r, n)
    char *h;
    char *r;
    int n;
{
    FILE *cnffile;
    char tr[REALM_SZ];
    char linebuf[BUFSIZ];
    char scratch[64];
    register int i = 0;
#ifdef NIS
    register char *p;
    char *domain;
    char *lookup;
    int err;
    int len;
    int nr;			/* counter of matching realm lines found */
#endif /* NIS */

    if (n < 1)
	return(KFAILURE);

    if ((cnffile = fopen(KRB_CONF,"r")) != NULL) {
	    if (fgets(linebuf, BUFSIZ, cnffile) == NULL) {
		/* error reading */
		(void) fclose(cnffile);
		return(KFAILURE);
	    }
	    if (!index(linebuf, '\n')) {
		/* didn't all fit into buffer, punt */
		(void) fclose(cnffile);
		return(KFAILURE);
	    }
	    for (i = 0; i < n; ) {
		/* run through the file, looking for admin host */
		if (fgets(linebuf, BUFSIZ, cnffile) == NULL) {
		    (void) fclose(cnffile);
		    return(KFAILURE);
		}
#ifdef NIS
		/* continue in NIS if + line; rest of file is ignored */
		if (krb_use_nis && linebuf[0] == '+')
		    goto donis;
#endif /* NIS */
		/* need to scan for a token after 'admin' to make sure that
		   admin matched correctly */
		if (sscanf(linebuf, "%s %s admin %s", tr, h, scratch) != 3)
		    continue;
		if (!strcmp(tr,r))
		    i++;
	    }
	    (void) fclose(cnffile);
	    return(KSUCCESS);
    }

#ifdef NIS
  donis:
  if (krb_use_nis) {
    /* no local conf file; try NIS */
    if (krb_debug)
	fprintf(stderr, "krb_get_admhst: trying NIS for realm `%s' n=%d\n",r,n);
    if (yp_get_default_domain(&domain) == 0) {
	nr = 1;
	while (i < n) {
	    /*
	     *  run through the map, looking for admin host n
	     *  keys in the NIS map are ordered by appending ".n" after them
	     */
	    sprintf(linebuf, "%s.%d", r, nr);
	    for (p = linebuf; *p; p++) {
		    if (islower(*p))
			    *p = toupper(*p);
	    }
	    if (krb_debug)
	        fprintf(stderr, " domain `%s' key `%s' map `%s' [i=%d n=%d]\n",
		        domain, linebuf, KRB_CONF_MAP, i, n);
	    lookup = NULL;
	    err = yp_match(domain, KRB_CONF_MAP, linebuf, strlen(linebuf),
			    &lookup, &len);
	    if (err && krb_debug)
                fprintf(stderr, " yp_match ret %d (%s)\n", err,
			yperr_string(err));
	    switch(err) {
	        case 0:
		    lookup[len] = 0;
		    if (krb_debug)
		        fprintf(stderr, " yp ret string `%s'\n", lookup);
		    /* realm match found -- is it an admin server? */
		    nr++;
		    if (sscanf(lookup, "%*s %s admin %s", h, scratch) == 2) {
		        /* found ith admin host for realm r */
			i++;
		    }
	            free(lookup);
		    break;
	        case YPERR_KEY:
		    /* map exists but key doesn't; return error */
		    return(KFAILURE);

		default:
		    /*
		     *  other NIS errors are treated as non-fatal errors;
		     *  go try default action
		     */
		    goto trydef;
	    }
	}
        return(KSUCCESS);	/* hostname in h */
    }
  }
#endif /* NIS */

  trydef:
#ifdef sun
    /*
     *  There is no conf file.  If the requested realm matches
     *  the default realm, then return the default kerberos hostname.
     */
    if (n == 1 && krb_get_lrealm(tr, 1) == KSUCCESS) {
        if (!strcmp(tr, r)) {
	    strcpy(h, KRB_HOST);
	    if (krb_debug) {
	        fprintf(stderr,
		    "krb_get_admhst: def realm `%s' matches; ret host `%s'\n",
		    tr, h);
	    }
	    return(KSUCCESS);
	}
	if (krb_debug) {
	    fprintf(stderr,
		"krb_get_admhst: def realm `%s' does not match `%s'\n",
		tr, r);
	}
    }
#endif sun
    return(KFAILURE);
}
