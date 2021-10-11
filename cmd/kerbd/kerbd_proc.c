#ident	"@(#)kerbd_proc.c	1.3	93/06/08 SMI"
/*
 *  RPC server procedures for the kerberos usermode daemon kerbd.
 *
 *  Copyright 1990,1991 Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <rpc/rpc.h>
#include <sys/param.h>
#include <sys/syslog.h>
#include "kerbd.h"

#define	SRVTAB	""

extern	int	kerbd_debug;		/* declared in kerbd.c */
extern	int	dogrouplist;		/* declared in kerbd.c */
extern	long	sysmaxgroups;		/* declared in kerbd.c */
static	void	__kerbd_getgroups();

kgetkcred_res *
kgetkcred_4(argp, rqstp)
	kgetkcred_arg *argp;
	struct svc_req *rqstp;
{
	static kgetkcred_res res;
	kgetkcred_resd *resd = &res.kgetkcred_res_u.res;
	static char t_inst[INST_SZ];	/* krb_rd_req might update this */
	KTEXT_ST ticket;
	AUTH_DAT adat;
	uid_t uid;
	int rem;

	if (kerbd_debug)
		fprintf(stderr, "\ngetkcred: `%s.%s' from %s (tkt sz %d)\n",
			argp->sname, argp->sinst, inet_ntoa(&argp->faddr),
			argp->ticket.TICKET_len);

	if (checkfrom(rqstp, &uid) == 0)
		return (NULL);

#ifdef BSD
	bzero((char *)&ticket, sizeof (ticket));
#else
	(void) memset((void *)&ticket, 0, sizeof (ticket));
#endif /* BSD */
	ticket.length = argp->ticket.TICKET_len;
#ifdef BSD
	bcopy(argp->ticket.TICKET_val, ticket.dat, ticket.length);
#else
	(void) memcpy((void *)ticket.dat, (void *)argp->ticket.TICKET_val,
			(size_t)ticket.length);
#endif /* BSD */
	ticket.mbz = 0;
	strncpy(t_inst, argp->sinst, INST_SZ);
	rem = krb_rd_req(&ticket, argp->sname, t_inst,
		argp->faddr, &adat, SRVTAB);
	if (rem != KSUCCESS) {
		syslog(LOG_ERR, "krb_rd_req: err %d (%s)",
			rem, rem > 0? krb_err_txt[rem] : "system error");
		res.status = rem;
		goto alldone;
	}

	res.status = KSUCCESS;
	resd->sinst = t_inst;
	resd->k_flags = (u_int)adat.k_flags;
	resd->pname = adat.pname;
	resd->pinst = adat.pinst;
	resd->prealm = adat.prealm;
	resd->checksum = adat.checksum;
#ifdef BSD
	bcopy((char *)adat.session, resd->session.c, sizeof (des_block));
#else
	(void) memcpy((void *)resd->session.c, (void *)adat.session,
			sizeof (des_block));
#endif /* BSD */
	resd->life = adat.life;
	resd->time_sec = adat.time_sec;
	resd->address = adat.address;
	resd->reply.TICKET_len = adat.reply.length;
	resd->reply.TICKET_val = (char *)adat.reply.dat;
	if (kerbd_debug)
	    fprintf(stderr, "getkcred: return server inst `%s' length %d\n",
		resd->sinst, strlen(resd->sinst));

alldone:
	return (&res);
}

ksetkcred_res *
ksetkcred_4(argp, rqstp)
	ksetkcred_arg *argp;
	struct svc_req *rqstp;
{
	static ksetkcred_res res;
	ksetkcred_resd *resd = &res.ksetkcred_res_u.res;
	char krb_tkt_file[MAXPATHLEN];
	KTEXT_ST ticket;
	CREDENTIALS kcred;
	uid_t uid;
	int rem;

	if (kerbd_debug)
	    fprintf(stderr, "\nsetkcred: `%s.%s@%s' chksum %d\n",
		argp->sname, argp->sinst, argp->srealm, argp->cksum);
	if (checkfrom(rqstp, &uid) == 0)
		return (NULL);

	/* construct ticket file name based on uid of client */
	(void) sprintf(krb_tkt_file, "%s%d", TKT_ROOT, uid);
	krb_set_tkt_string(krb_tkt_file);
	if (kerbd_debug)
		fprintf(stderr, "  using tkt file %s for request\n",
			krb_tkt_file);

	rem = krb_mk_req(&ticket, argp->sname, argp->sinst, argp->srealm,
			argp->cksum);
	if (rem != KSUCCESS) {
		syslog(LOG_ERR, "krb_mk_req: err %d (%s)",
			rem, rem > 0? krb_err_txt[rem] : "system error");
		res.status = rem;
		goto alldone;
	}

	rem = krb_get_cred(argp->sname, argp->sinst, argp->srealm, &kcred);
	if (rem != KSUCCESS) {
		syslog(LOG_ERR, "krb_get_cred: err %d (%s)",
			rem, rem > 0? krb_err_txt[rem] : "system error");
		res.status = rem;
		goto alldone;
	}

	/* all done -- return the data */
	res.status = KSUCCESS;
	resd->ticket.TICKET_len = ticket.length;
	resd->ticket.TICKET_val = (char *)ticket.dat;
#ifdef BSD
	bcopy((char *)kcred.session, resd->key.c, sizeof (des_block));
#else
	(void) memcpy((void *)resd->key.c, (void *)kcred.session,
			sizeof (des_block));
#endif /* BSD */

alldone:
	return (&res);
}

kgetucred_res *
kgetucred_4(argp, rqstp)
	kgetucred_arg *argp;
	struct svc_req *rqstp;
{
	static kgetucred_res res;
	static u_int grps[KUCRED_MAXGRPS];
	kerb_ucred *kuc = &res.kgetucred_res_u.cred;
	struct passwd *pw;
	uid_t uid;

	if (kerbd_debug)
		fprintf(stderr, "\ngetucred: lookup `%s' -- \n", argp->pname);
	if (checkfrom(rqstp, &uid) == 0)
		return (NULL);

	if (pw = getpwnam(argp->pname)) {
		res.status = UCRED_OK;
		kuc->uid = pw->pw_uid;
		kuc->gid = pw->pw_gid;
		kuc->grplist.grplist_val = grps;
		__kerbd_getgroups(argp->pname, kuc, KUCRED_MAXGRPS);
		if (kerbd_debug) {
			u_int *gp;
			u_int ngrps = kuc->grplist.grplist_len;

			fprintf(stderr, "    uid %d gid %d\n",
				kuc->uid, kuc->gid);
			if (ngrps) {
				fprintf(stderr, "    %d groups:", ngrps);
				for (gp = grps; gp < &grps[ngrps]; gp++)
					fprintf(stderr, " %d", *gp);
				fprintf(stderr, "\n");
			}
		}
	} else {
		res.status = UCRED_UNKNOWN;
		if (kerbd_debug)
			fprintf(stderr, "    unknown username\n");
	}

	return (&res);
}

/*
 *  Returns 1 if caller is ok, else 0.
 *  If caller ok, the uid is returned in uidp.
 */
checkfrom(rqstp, uidp)
struct svc_req *rqstp;
uid_t *uidp;
{
	SVCXPRT *xprt = rqstp->rq_xprt;
	struct authunix_parms *aup;
	uid_t uid;

	/* check client agent uid to ensure it is privileged */
	if (__rpc_get_local_uid(xprt, &uid) < 0) {
		syslog(LOG_ERR, "__rpc_get_local_uid failed %s %s",
				xprt->xp_netid, xprt->xp_tp);
		goto weakauth;
	}
	if (kerbd_debug)
		fprintf(stderr, "checkfrom: local_uid  %d\n", uid);
	if (uid != 0) {
		syslog(LOG_ERR, "checkfrom: caller (uid %d) not privileged",
			uid);
		goto weakauth;
	}

	/*
	 *  Request came from local privileged process.
	 *  Proceed to get uid of client if needed by caller.
	 */
	if (uidp) {
	    if (rqstp->rq_cred.oa_flavor != AUTH_SYS) {
		syslog(LOG_ERR, "checkfrom: not UNIX credentials");
		goto weakauth;
	    }
	    aup = (struct authunix_parms *)rqstp->rq_clntcred;
	    *uidp = aup->aup_uid;
	    if (kerbd_debug)
		fprintf(stderr, "checkfrom: caller's uid %d\n", *uidp);
	}
	return (1);

weakauth:
	svcerr_weakauth(xprt);
	return (0);
}

/*
 *  Initialize the groups list via getgrent.  Assumes that
 *  kuc->grplist.grplist_val has been initialized by the caller
 *  to point to an array of at least size maxgrps.
 *  kuc->grplist.grplist_len is set to the number of values stored.
 */
static void
__kerbd_getgroups(name, kuc, maxgrps)
char *name;				/* username to look for */
kerb_ucred *kuc;			/* struct to initialize */
u_int maxgrps;				/* max number of groups to list */
{
	register u_int *gp;
	register char **pp;
	u_int count;
	struct group *group;

	if (!dogrouplist)
		maxgrps = 0;
	if (maxgrps > (u_int) sysmaxgroups)
		maxgrps = (u_int)sysmaxgroups;

	count = 0;
	if (maxgrps > 0) {
		setgrent();
		gp = kuc->grplist.grplist_val;
		while (group = getgrent()) {
		    for (pp = group->gr_mem; *pp; pp++) {
			if (strcmp(*pp, name) == 0) {
			    /* found one */
			    *gp++ = (u_int)group->gr_gid;
			    if (++count == maxgrps)
				goto done;		/* filled list */
			    break;			/* go to next entry */
			}
		    }
		}

	    done:
		endgrent();
	}

	kuc->grplist.grplist_len = count;
}
