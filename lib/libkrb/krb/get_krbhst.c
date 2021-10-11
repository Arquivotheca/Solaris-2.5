/*
 * $Source: /mit/kerberos/src/lib/krb/RCS/get_krbhst.c,v $
 * $Author: rfrench $
 *
 * Copyright 1985, 1986, 1987, 1988 by the Massachusetts Institute
 * of Technology.
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>.
 */

#ifndef lint
static char *rcsid_get_krbhst_c =
"$Header: get_krbhst.c,v 4.8 89/01/22 20:00:29 rfrench Exp $";
#endif /* lint */

#include <kerberos/mit-copyright.h>
#include <stdio.h>
#include <kerberos/krb.h>
#ifdef SYSV
#include <string.h>
#else
#include <strings.h>
#endif /* SYSV */

#ifdef NIS
#include <ctype.h>
#include <rpcsvc/ypclnt.h>
int krb_use_nis = 1;
#endif /* NIS */

extern int krb_debug;

/*
 * Given a Kerberos realm, find a host on which the Kerberos authenti-
 * cation server can be found.
 *
 * krb_get_krbhst takes a pointer to be filled in, a pointer to the name
 * of the realm for which a server is desired, and an integer, n, and
 * returns (in h) the nth entry from the configuration file (KRB_CONF,
 * defined in "krb.h") associated with the specified realm.
 *
 * On end-of-file, krb_get_krbhst returns KFAILURE.  If n=1 and the
 * configuration file does not exist, krb_get_krbhst will return KRB_HOST
 * (also defined in "krb.h").  If all goes well, the routine returnes
 * KSUCCESS.
 *
 * The KRB_CONF file contains the name of the local realm in the first
 * line (not used by this routine), followed by lines indicating realm/host
 * entries.  The words "admin server" following the hostname indicate that 
 * the host provides an administrative database server.
 *
 * For example:
 *
 *	ATHENA.MIT.EDU
 *	ATHENA.MIT.EDU kerberos-1.mit.edu admin server
 *	ATHENA.MIT.EDU kerberos-2.mit.edu
 *	LCS.MIT.EDU kerberos.lcs.mit.edu admin server
 *
 * This is a temporary hack to allow us to find the nearest system running
 * kerberos.  In the long run, this functionality will be provided by a
 * nameserver.
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
 * then return KRB_HOST.
#endif 
 */

krb_get_krbhst(h,r,n)
    char *h;
    char *r;
    int n;
{
    FILE *cnffile;
    char tr[REALM_SZ];
    char linebuf[BUFSIZ];
    register int i;
#ifdef NIS
    register char *p;
    char *domain;
    char *lookup;
    int err;
    int len;
#endif /* NIS */

    if ((cnffile = fopen(KRB_CONF,"r")) != NULL) {
	if (fscanf(cnffile,"%s",tr) == EOF)
	    return(KFAILURE);
	/*
	 *  run through the file, looking for the nth server
	 *  for this realm
	 */
	for (i = 1; i <= n;) {
	    if (fgets(linebuf, BUFSIZ, cnffile) == NULL) {
		(void) fclose(cnffile);
		return(KFAILURE);
	    }
#ifdef NIS
	    /* continue in NIS if + line; rest of file is ignored */
	    if (krb_use_nis && linebuf[0] == '+') {
		n -= (i - 1);		/* adjust n for NIS */
		goto donis;
	    }
#endif NIS
	    if (sscanf(linebuf, "%s %s", tr, h) != 2)
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
	fprintf(stderr, "krb_gethst: trying NIS for realm `%s' n=%d\n", r, n);
    if (yp_get_default_domain(&domain) == 0) {
	/* keys in the NIS map are ordered by appending ".n" after them */
	sprintf(linebuf, "%s.%d", r, n);
	for (p = linebuf; *p; p++) {
		if (islower(*p))
			*p = toupper(*p);
	}
	if (krb_debug)
	    fprintf(stderr, " domain `%s' looking for key `%s' map `%s'\n",
		    domain, linebuf, KRB_CONF_MAP);
	lookup = NULL;
	err = yp_match(domain, KRB_CONF_MAP, linebuf, strlen(linebuf),
			&lookup, &len);
	if (err && krb_debug)
            fprintf(stderr, " yp_match ret %d (%s)\n", err, yperr_string(err));
	switch(err) {
	    case 0:
		lookup[len] = 0;
		if (krb_debug)
		    fprintf(stderr, " yp ret string `%s'\n", lookup);
		if (sscanf(lookup, "%s %s", tr, h) != 2) {
		    /* line syntax is hosed; return error */
		    free(lookup);
		    return(KFAILURE);
		}
	        free(lookup);
		return(KSUCCESS);	/* hostname in h */
		break;
	    case YPERR_KEY:
		/* map exists but key doesn't; return error */
		return(KFAILURE);

	    /* other NIS errors are treated as non-fatal errors; fall through */
	}
    }
  }
#endif /* NIS */

    /* last resort: return compiled-in name */
    if (n==1) {
	(void) strcpy(h,KRB_HOST);
	if (krb_debug)
	    fprintf(stderr, "krb_gethst: default: ret hostname `%s'\n", h);
	return(KSUCCESS);
    }
    else
	return(KFAILURE);
}
