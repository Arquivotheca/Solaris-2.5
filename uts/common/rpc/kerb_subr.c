#ident	"@(#)kerb_subr.c	1.8	95/01/26 SMI"
/*
 * kerberos specific routines
 */


#include <sys/types.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/tiuser.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/auth.h>
#include <rpc/auth_kerb.h>
#include <rpc/kerb_private.h>

/*
 * called by client when generating handle in authkerb_create()
 *
 * actions:
 *	1. get a valid ticket for the service; store in ak->ak_ticket
 *	2. return the new session key in pkey
 */

int
kerb_get_session_key(struct _ak_private *ak, des_block *pkey)
{
	u_long cksum;

#ifdef KERB_DEBUG
	kprint("get_key: service='%s', instance='%s', realm='%s'\n",
		ak->ak_service, ak->ak_srv_inst, ak->ak_realm);
#endif /* KERB_DEBUG */

	cksum = 0;		/* XXX checksum is currently unused */

	/* get the ticket for the requested service */
	return (kerb_mkcred(ak->ak_service, ak->ak_srv_inst, ak->ak_realm,
			cksum, &ak->ak_ticket, pkey, NULL));
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
kerb_get_session_cred(char *service, char *instance, u_long faddr,
	KTEXT ticket, authkerb_clnt_cred *cred)
{
	int rem;
	AUTH_DAT au_data;
	struct timeval current;

#ifdef KERB_DEBUG
	kprint("get_session_cred: Myname '%s', inst '%s' faddr %d.%d.%d.%d\n",
		service, instance,
		(faddr >> 24) & 0xff, (faddr >> 16) & 0xff,
		(faddr >> 8) & 0xff, (faddr) & 0xff);
#endif /* KERB_DEBUG */

	rem = kerb_rdcred(ticket, service, instance, faddr, &au_data, NULL);
	if (rem != RD_AP_OK) {
#ifdef KERB_DEBUG
		kprint("get_session_cred: kerberos error %d (%s)\n",
			rem, rem > 0 ? krb_err_txt[rem] : "system error");
#endif /* KERB_DEBUG */
		return (kerb_error(rem));
	}


#ifdef KERB_DEBUG
	kprint("get_session_cred: Client:\n");
	kprint("   Principal='%s', Instance='%s', Realm='%s'\n",
		    au_data.pname, au_data.pinst, au_data.prealm);
#endif
	current.tv_sec  = hrestime.tv_sec;
	current.tv_usec = hrestime.tv_nsec/1000;

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
			((au_data.life * 60) + au_data.time_sec));
#endif /* KERB_DEBUG */
		return (AUTH_TIMEEXPIRE);
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
	bcopy((caddr_t)au_data.session, (caddr_t)cred->session,
		sizeof (cred->session));
	strncpy(cred->pname,  au_data.pname,  ANAME_SZ);
	strncpy(cred->pinst,  au_data.pinst,  INST_SZ);
	strncpy(cred->prealm, au_data.prealm, REALM_SZ);

	return (AUTH_OK);
}

/*
 * takes in a kerberos error and returns a rpc auth error
 */

AUTH_STAT
kerb_error(int kerror)
{
	AUTH_STAT error;

	switch (kerror) {

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
	return (error);
}

/* PRINTFLIKE1 */
void
_kmsgout(char *fmt, ...)
{
	va_list adx;

#ifndef LOCKNEST
	va_start(adx, fmt);
	vcmn_err(CE_NOTE, fmt, adx);
	va_end(adx);
#endif
}

#ifdef KERB_DEBUG
static int kerb_rpc_debug = 0;

void
kprint(char *fmt, ...)
{
	va_list adx;

#ifndef LOCKNEST
	va_start(adx, fmt);
	vprintf(fmt, adx);
	va_end(adx);
#endif
}
#endif /* KERB_DEBUG */

int
krb_get_lrealm(char *r, int n)
{
	char *rlm = KRB_REALM;

	if (n > 1 || strlen(rlm) >= REALM_SZ)
		return (KFAILURE);  /* Temporary restriction */

	(void) strcpy(r, rlm);
	return (KSUCCESS);
}

/*
 *  Return the kerberos realm as set via systeminfo.
 *  Invoked by KRB_REALM macro, defined in kerberos/krb.h
 */
char *
krb_get_default_realm(void)
{

#ifdef KERB_DEBUG
	kprint("krb_get_default_realm: realm `%s' len %d\n",
		kerb_realm, strlen(kerb_realm));
#endif KERB_DEBUG
	return (kerb_realm);			/* systeminfo.h, scalls.c */
}

/*  Kerberos error strings for kernel mode */
/*
 * $Source: /mit/kerberos/src/lib/krb/RCS/krb_err_txt.c,v $
 * $Author: jtkohl $
 * $Header: krb_err_txt.c,v 4.7 88/12/01 14:10:14 jtkohl Exp $
 *
 * Copyright 1988 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>.
 */

/*
 * This file contains an array of error text strings.
 * The associated error codes (which are defined in "krb.h")
 * follow the string in the comments at the end of each line.
 */

char *krb_err_txt[KRB_ERRORS_TABLE_SIZE] = {
	"OK",							/* 000 */
	"Principal expired (kerberos)",				/* 001 */
	"Service expired (kerberos)",				/* 002 */
	"Authentication expired (kerberos)",			/* 003 */
	"Unknown protocol version number (kerberos)", 		/* 004 */
	"Principal: Incorrect master key version (kerberos)",	/* 005 */
	"Service: Incorrect master key version (kerberos)",	/* 006 */
	"Bad byte order (kerberos)",				/* 007 */
	"Principal unknown (kerberos)",				/* 008 */
	"Principal not unique (kerberos)",			/* 009 */
	"Principal has null key (kerberos)",			/* 010 */
	"Reserved error message 11 (kerberos)",			/* 011 */
	"Reserved error message 12 (kerberos)",			/* 012 */
	"Reserved error message 13 (kerberos)",			/* 013 */
	"Reserved error message 14 (kerberos)",			/* 014 */
	"Reserved error message 15 (kerberos)",			/* 015 */
	"Reserved error message 16 (kerberos)",			/* 016 */
	"Reserved error message 17 (kerberos)",			/* 017 */
	"Reserved error message 18 (kerberos)",			/* 018 */
	"Reserved error message 19 (kerberos)",			/* 019 */
	"Permission Denied (kerberos)",				/* 020 */
	"Can't read ticket file (krb_get_cred)",		/* 021 */
	"Can't find ticket (krb_get_cred)",			/* 022 */
	"Reserved error message 23 (krb_get_cred)",		/* 023 */
	"Reserved error message 24 (krb_get_cred)",		/* 024 */
	"Reserved error message 25 (krb_get_cred)",		/* 025 */
	"Ticket granting ticket expired (krb_mk_req)",		/* 026 */
	"Reserved error message 27 (krb_mk_req)",		/* 027 */
	"Reserved error message 28 (krb_mk_req)",		/* 028 */
	"Reserved error message 29 (krb_mk_req)",		/* 029 */
	"Reserved error message 30 (krb_mk_req)",		/* 030 */
	"Can't decode authenticator (krb_rd_req)",		/* 031 */
	"Ticket expired (krb_rd_req)",				/* 032 */
	"Ticket issue date too far in the future (krb_rd_req)",	/* 033 */
	"Repeat request (krb_rd_req)",				/* 034 */
	"Ticket for wrong server (krb_rd_req)",			/* 035 */
	"Request inconsistent (krb_rd_req)",			/* 036 */
	"Time is out of bounds (krb_rd_req)",			/* 037 */
	"Incorrect network address (krb_rd_req)",		/* 038 */
	"Protocol version mismatch (krb_rd_req)",		/* 039 */
	"Illegal message type (krb_rd_req)",			/* 040 */
	"Message integrity error (krb_rd_req)",			/* 041 */
	"Message duplicate or out of order (krb_rd_req)",	/* 042 */
	"Unauthorized request (krb_rd_req)",			/* 043 */
	"Reserved error message 44 (krb_rd_req)",		/* 044 */
	"Reserved error message 45 (krb_rd_req)",		/* 045 */
	"Reserved error message 46 (krb_rd_req)",		/* 046 */
	"Reserved error message 47 (krb_rd_req)",		/* 047 */
	"Reserved error message 48 (krb_rd_req)",		/* 048 */
	"Reserved error message 49 (krb_rd_req)",		/* 049 */
	"Reserved error message 50 (krb_rd_req)",		/* 050 */
	"Current password is NULL (get_pw_tkt)",		/* 051 */
	"Current password incorrect (get_pw_tkt)",		/* 052 */
	"Protocol error (gt_pw_tkt)",				/* 053 */
	"Error returned by KDC (gt_pw_tkt)",			/* 054 */
	"Null ticket returned by KDC (gt_pw_tkt)",		/* 055 */
	"Retry count exceeded (send_to_kdc)",			/* 056 */
	"Can't send request (send_to_kdc)",			/* 057 */
	"Reserved error message 58 (send_to_kdc)",		/* 058 */
	"Reserved error message 59 (send_to_kdc)",		/* 059 */
	"Reserved error message 60 (send_to_kdc)",		/* 060 */
	"Warning: Not ALL tickets returned",			/* 061 */
	"Password incorrect",					/* 062 */
	"Protocol error (get_intkt)",				/* 063 */
	"Reserved error message 64 (get_in_tkt)",		/* 064 */
	"Reserved error message 65 (get_in_tkt)",		/* 065 */
	"Reserved error message 66 (get_in_tkt)",		/* 066 */
	"Reserved error message 67 (get_in_tkt)",		/* 067 */
	"Reserved error message 68 (get_in_tkt)",		/* 068 */
	"Reserved error message 69 (get_in_tkt)",		/* 069 */
	"Generic error (get_intkt)",				/* 070 */
	"Don't have ticket granting ticket (get_ad_tkt)",	/* 071 */
	"Reserved error message 72 (get_ad_tkt)",		/* 072 */
	"Reserved error message 73 (get_ad_tkt)",		/* 073 */
	"Reserved error message 74 (get_ad_tkt)",		/* 074 */
	"Reserved error message 75 (get_ad_tkt)",		/* 075 */
	"No ticket file (tf_util)",				/* 076 */
	"Can't access ticket file (tf_util)",			/* 077 */
	"Can't lock ticket file; try later (tf_util)",		/* 078 */
	"Bad ticket file format (tf_util)",			/* 079 */
	"Read ticket file before tf_init (tf_util)",		/* 080 */
	"Bad Kerberos name format (kname_parse)",		/* 081 */
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"(reserved)",
	"Generic kerberos error (kfailure)",			/* 255 */
};
