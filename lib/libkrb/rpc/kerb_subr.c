#ident	"@(#)kerb_subr.c	1.1	91/06/24 SMI"
/*
 * kerberos specific routines
 */


#include <sys/types.h>
#include <sys/time.h>
#include <rpc/types.h>
#include <sys/param.h>
#include <sys/syslog.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/auth_kerb.h>
#include <rpc/kerb_private.h>

#define SRVTAB		""



/*
 * called by client when generating handle in authkerb_create()
 *
 * actions:
 *	1. get a valid ticket for the service; store in ak->ak_ticket
 *	2. return the new session key in pkey
 */

kerb_get_session_key(ak, pkey)
struct _ak_private *ak;
des_block *pkey;
{
    int cc, rem;
    u_long cksum;
    CREDENTIALS kcred;		/* temporary kerberos credentials */

#ifdef KERB_DEBUG
    kprint("get_key: service='%s', instance='%s', realm='%s'\n",
	   ak->ak_service, ak->ak_srv_inst, ak->ak_realm);
#endif KERB_DEBUG

    cksum = 32;		/* XXX */

    /* get the ticket for the requested service */
    rem = krb_mk_req(&ak->ak_ticket, ak->ak_service, ak->ak_srv_inst,
			ak->ak_realm, cksum);
    if (rem != KSUCCESS){
	    _kmsgout("get_session_key: krb_mk_req: err %d (%s)", rem);
	    /* error 76 */
	    return(rem);
    }
    rem = krb_get_cred(ak->ak_service, ak->ak_srv_inst, ak->ak_realm, &kcred);
    if (rem != GC_OK) {
	    _kmsgout("get_session_key: krb_get_cred: error = %d", rem);
	    return(rem);
    }
    
    /* copy kerberos session key --> private key */
    memcpy( (char *)pkey, (char *)kcred.session, sizeof(des_block));

    return(KSUCCESS);

}


/*
 *  Called by server to get the credentials of the client.
 *  The caller specifies the (server's) service and instance names,
 *  and the kerberos ticket received from the client.
 *  The caller's network address is specified in faddr.
 *  The routine returns the credentials in cred.
 *  Calls kerbros library krb_rd_req().
 */

AUTH_STAT
kerb_get_session_cred(service, instance, faddr, ticket, cred)
char *service;
char *instance;
u_long faddr;
KTEXT ticket;
authkerb_clnt_cred *cred;
{
	int rem;
	AUTH_DAT au_data;
	struct timeval current;
	struct sockaddr_in peername;
	int namelen = sizeof(peername);

#ifdef KERB_DEBUG
	kprint("get_session_cred: Myname '%s', inst '%s' faddr %d.%d.%d.%d\n",
		service, instance,
		(faddr >> 24) & 0xff, (faddr >> 16) & 0xff,
		(faddr >>  8) & 0xff, (faddr      ) & 0xff  );
#endif KERB_DEBUG

	rem = krb_rd_req(ticket, service, instance, faddr, &au_data, SRVTAB);
	if (rem != RD_AP_OK) {
#ifdef KERB_DEBUG
		kprint("get_session_cred: kerberos error %d (%s)\n",
			rem, rem > 0 ? krb_err_txt[rem] : "system error");
#endif KERB_DEBUG
		return(kerb_error(rem));
	}


#ifdef KERB_DEBUG
	kprint("get_session_cred: Client:\n\
   Principal='%s', Instance='%s', Realm='%s'\n",
		au_data.pname, au_data.pinst, au_data.prealm);
#endif
	(void) gettimeofday(&current, (struct timezone *)NULL);

	/*
	 *  kerberos ticket lifetimes are in units of 5 minutes
	 */
	if (current.tv_sec > ((au_data.life * 60 * 5) + au_data.time_sec)) {
		/* ticket not valid */
		_kmsgout("*** kerberos ticket not valid - expired");
#ifdef KERB_DEBUG
		kprint("  curr %d; issue %d life %d expire %d\n",
			current.tv_sec, au_data.time_sec,
			au_data.life * 60,
			((au_data.life * 60 ) + au_data.time_sec));
#endif KERB_DEBUG
		return(AUTH_TIMEEXPIRE);
	}

	/*
	 *  Copy kerberos auth data into rpc client cred.
	 *  The structures are slightly different, so we must
	 *  do this the painful way.
	 *
	 *  Note that kerberos lifetimes are in units of 5 minutes,
	 *  so we must convert to seconds here.
	 */
	cred->k_flags	= au_data.k_flags;
	cred->checksum	= au_data.checksum;
	cred->life	= au_data.life;
	cred->time_sec	= au_data.time_sec;
	cred->address	= au_data.address;
	cred->expiry	= cred->time_sec + (5 * 60 * cred->life);
	memcpy(cred->session, au_data.session, sizeof(cred->session));
	strncpy(cred->pname,  au_data.pname,  ANAME_SZ);
	strncpy(cred->pinst,  au_data.pinst,  INST_SZ);
	strncpy(cred->prealm, au_data.prealm, REALM_SZ);

	return(AUTH_OK);
}


/*
 * takes in a kerberos error and returns a rpc auth error
 */

AUTH_STAT
kerb_error(kerror)
    int kerror;
{
	AUTH_STAT error;

	switch(kerror) {

	  case RD_AP_UNDEC:
		error = AUTH_DECODE;
		break;
	  case RD_AP_EXP:
		error = AUTH_TIMEEXPIRE;
		break;
	  case NO_TKT_FIL:
		error = AUTH_TKT_FILE;
		break;
	  case RD_AP_BADD:
		error = AUTH_NET_ADDR;
		break;
		
	 default:
		error = AUTH_KERB_GENERIC;
	}		  
	return(error);
}

_kmsgout(fmt, a,b,c)
	char *fmt;
{
#if defined(_KERNEL) || defined(KERB_DEBUG)
	(void) printf(fmt, a, b, c);
	(void) printf("\n");
#else
	char msg[200];
	(void) sprintf(msg, fmt, a, b, c);
	(void) syslog(LOG_ERR, "%s", msg);
#endif
}

#ifdef KERB_DEBUG
int kerb_rpc_debug = 0;

kprint(fmt, a,b,c,d,e,f)
char *fmt;
{
	if (kerb_rpc_debug)
		printf(fmt, a,b,c,d,e,f);
}
#endif KERB_DEBUG
