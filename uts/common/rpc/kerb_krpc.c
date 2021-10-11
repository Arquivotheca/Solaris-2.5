#ident	"@(#)kerb_krpc.c	1.11	95/04/04 SMI"
/*
 *  Routines for kernel kerberos implementation to talk to usermode
 *  kerb daemon.
 *  This file is not needed in userland.
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <rpc/rpc.h>
#include <rpc/kerbd_prot.h>

#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/pathname.h>
#include <sys/debug.h>
#include <sys/utsname.h>
#include <sys/cmn_err.h>

#define	KERB_TIMEOUT	30	/* per-try timeout in seconds */
#define	KERB_NRETRY	6	/* number of retries */

static struct timeval trytimeout = { KERB_TIMEOUT, 0 };
static enum clnt_stat kerb_call(u_long, bool_t (*)(), char *,
				bool_t (*)(), char *);

/*
 *  Called by client rpc to set up authenticator for the requested
 *  service.  Returns ticket and kerberos authenticator information.
 */
int
kerb_mkcred(char *service, char *inst, char *realm, u_long cksum,
	KTEXT ticket, des_block *pkey, enum clnt_stat *rpcstat)
{
	ksetkcred_arg    ska;
	ksetkcred_res    skr;
	ksetkcred_resd  *skd = &skr.ksetkcred_res_u.res;
	enum clnt_stat	stat;

	ska.sname = service;
	ska.sinst = inst;
	ska.srealm = realm;
	ska.cksum = cksum;

	bzero((char *)&skr, sizeof (skr));
	stat = kerb_call((u_long)KSETKCRED, xdr_ksetkcred_arg,
			(char *)&ska, xdr_ksetkcred_res, (char *)&skr);
	if (rpcstat)
		*rpcstat = stat;

	if (stat != RPC_SUCCESS)
		return (-1);

	if (skr.status == KSUCCESS) {
		bzero((char *)ticket, sizeof (*ticket));
		ticket->length = skd->ticket.TICKET_len;
		bcopy(skd->ticket.TICKET_val, (char *)ticket->dat,
		    ticket->length);
		ticket->mbz = 0;

		*pkey = skd->key;
	}
	return (skr.status);
}

/*
 *  Called by server rpc to check the authenticator presented by the client.
 */
int
kerb_rdcred(KTEXT ticket, char *service, char *inst, u_long faddr,
	AUTH_DAT *kcred, enum clnt_stat *rpcstat)
{
	kgetkcred_arg    gka;
	kgetkcred_res    gkr;
	kgetkcred_resd  *gkd = &gkr.kgetkcred_res_u.res;
	enum clnt_stat	stat;

	gka.ticket.TICKET_len = ticket->length;
	gka.ticket.TICKET_val = (char *)ticket->dat;
	gka.sname = service;
	gka.sinst = inst;
	gka.faddr = faddr;

	bzero((char *)&gkr, sizeof (gkr));
	stat = kerb_call((u_long)KGETKCRED, xdr_kgetkcred_arg,
			(char *)&gka, xdr_kgetkcred_res, (char *)&gkr);
	if (rpcstat)
		*rpcstat = stat;

	if (stat != RPC_SUCCESS)
		return (-1);

	if (gkr.status == KSUCCESS) {
		strncpy(inst, gkd->sinst, INST_SZ);
		kcred->k_flags = (u_char)gkd->k_flags;
		strncpy(kcred->pname, gkd->pname, ANAME_SZ);
		strncpy(kcred->pinst, gkd->pinst, INST_SZ);
		strncpy(kcred->prealm, gkd->prealm, REALM_SZ);
		kcred->checksum = gkd->checksum;
		bcopy((char *)&gkd->session, (char *)kcred->session,
			    sizeof (des_block));
		kcred->life = gkd->life;
		kcred->time_sec = gkd->time_sec;
		kcred->address = gkd->address;

		bzero((char *)&kcred->reply, sizeof (kcred->reply));
		kcred->reply.length = gkd->reply.TICKET_len;
		bcopy(gkd->reply.TICKET_val, (char *)kcred->reply.dat,
			kcred->reply.length);
		kcred->reply.mbz = 0;
	}

	return (gkr.status);
}

/*
 *  Get the user's unix credentials.  Return 1 if cred ok, else 0 or -1.
 */
int
kerb_getpwnam(char *name, uid_t *uid, gid_t *gid, short *grouplen,
	register int *groups, enum clnt_stat *rpcstat)
{
	kgetucred_arg	gua;
	kgetucred_res	gur;
	kerb_ucred	*ucred = &gur.kgetucred_res_u.cred;
	enum clnt_stat	stat;
	int		ret = 0;
	u_int		ngrps, *gp, *grpend;

	gua.pname = name;
	bzero((char *)&gur, sizeof (gur));
	stat = kerb_call((u_long)KGETUCRED, xdr_kgetucred_arg,
			(char *)&gua, xdr_kgetucred_res, (char *)&gur);
	if (rpcstat)
		*rpcstat = stat;

	if (stat != RPC_SUCCESS)
		return (-1);

	if (gur.status == UCRED_OK) {
		*uid = (uid_t)ucred->uid;
		*gid = (gid_t)ucred->gid;
		if ((ngrps = ucred->grplist.grplist_len) > 0) {
		    if (ngrps > ngroups_max)
			ngrps = ngroups_max;
		    gp	   =  ucred->grplist.grplist_val;
		    grpend = &ucred->grplist.grplist_val[ngrps];
		    while (gp < grpend)
			*groups++ = (int)*gp++;
		}
		*grouplen = (short)ngrps;
		ret = 1;
	}
	return (ret);
}

/*
 *  send request to usermode kerb daemon.
 *  returns RPC_SUCCESS if ok, else error stat.
 */
static enum clnt_stat
kerb_call(u_long procn, bool_t (*xdr_args)(), char *args,
	bool_t (*xdr_rslt)(), char *rslt)
{
	static struct knetconfig	config; /* avoid lookupname next time */
	struct netbuf			netaddr;
	CLIENT				*client;
	enum clnt_stat			stat;
	struct vnode			*vp;
	int				error;
	static char			kerbname[SYS_NMLN+16];

	strcpy(kerbname, utsname.nodename);
	netaddr.len = strlen(kerbname);
	strcpy(&kerbname[netaddr.len], ".kerbd");

	netaddr.buf = kerbname;
	/*
	 *  6 = strlen(".kerbd");
	 */
	netaddr.len = netaddr.maxlen = netaddr.len + 6;

	/*
	 *  filch a knetconfig structure.
	 */
	if (config.knc_rdev == 0) {
		if ((error = lookupname("/dev/ticlts", UIO_SYSSPACE,
					    FOLLOW, NULLVPP, &vp)) != 0) {
			RPCLOG(1, "kerb_call: lookupname: %d\n", error);
			return (RPC_UNKNOWNPROTO);
		}
		config.knc_rdev = vp->v_rdev;
		config.knc_protofmly = loopback_name;
		VN_RELE(vp);
	}
	config.knc_semantics = NC_TPI_CLTS;
	RPCLOG(8, "kerb_call: procn %d, ", procn);
	RPCLOG(8, "rdev %x, ", config.knc_rdev);
	RPCLOG(8, "len %d, ", netaddr.len);
	RPCLOG(8, "maxlen %d, ", netaddr.maxlen);
	RPCLOG(8, "name %x\n", netaddr.buf);

	/*
	 *  now call the proper stuff.
	 */
	error = clnt_tli_kcreate(&config, &netaddr, (u_long)KERBPROG,
		(u_long)KERBVERS, 0, KERB_NRETRY, CRED(), &client);

	if (error != 0) {
		RPCLOG(1, "kerb_call: clnt_tli_kcreate: error %d", error);
		switch (error) {
		case EINTR:		return (RPC_INTR);
		case ETIMEDOUT:		return (RPC_TIMEDOUT);
		default:		return (RPC_FAILED);	/* XXX */
		}
	}
	stat = clnt_call(client, procn, (xdrproc_t) xdr_args, args,
			    (xdrproc_t) xdr_rslt, rslt, trytimeout);
	auth_destroy(client->cl_auth);
	clnt_destroy(client);
	if (stat != RPC_SUCCESS) {
		cmn_err(CE_WARN,
			"kerb_call: can't contact kerbd: RPC stat %d (%s)",
			stat, clnt_sperrno(stat));
		return (stat);
	}
	RPCLOG(8, "kerb call: (%d) ok\n", procn);
	return (RPC_SUCCESS);
}
