/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ident	"@(#)nfs_dlinet.c	1.41	95/10/10 SMI"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/file.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/debug.h>
#include <sys/tiuser.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/t_kuser.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#include <sys/netconfig.h>
#include <sys/ethernet.h>
#include <sys/dlpi.h>
#include <sys/vfs.h>
#include <sys/sysmacros.h>
#include <sys/bootconf.h>
#include <sys/cmn_err.h>

#include <net/if_arp.h>
#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>

#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_rmt.h>
#include <rpc/pmap_prot.h>
#include <rpc/bootparam.h>
#include <rpc/rpcb_prot.h>

#include <nfs/nfs.h>

#include <sys/kstr.h>
#include <sys/sunddi.h>

#include <sys/errno.h>
#include <sys/modctl.h>

/*
 * These are from the rpcgen'd version of mount.h XXX
 */
#define	MOUNTPROG 100005
#define	MOUNTPROC_MNT		1
#define	MOUNTVERS		1
#define	MOUNTVERS_POSIX		2
#define	MOUNTVERS3		3

struct fhstatus {
	int fhs_status;
	fhandle_t fhs_fh;
};

#define	FHSIZE3 64

struct fhandle3 {
	u_int fhandle3_len;
	char *fhandle3_val;
};

enum mountstat3 {
	MNT_OK = 0,
	MNT3ERR_PERM = 1,
	MNT3ERR_NOENT = 2,
	MNT3ERR_IO = 5,
	MNT3ERR_ACCES = 13,
	MNT3ERR_NOTDIR = 20,
	MNT3ERR_INVAL = 22,
	MNT3ERR_NAMETOOLONG = 63,
	MNT3ERR_NOTSUPP = 10004,
	MNT3ERR_SERVERFAULT = 10006
};

struct mountres3_ok {
	struct fhandle3 fhandle;
	struct {
		u_int auth_flavors_len;
		int *auth_flavors_val;
	} auth_flavors;
};

struct mountres3 {
	enum mountstat3 fhs_status;
	union {
		struct mountres3_ok mountinfo;
	} mountres3_u;
};

/*
 * XXX
 * DLPI address format.
 */
struct	dladdr {
	u_char	dl_phys[6];
	u_short	dl_sap;
};

union dl_fhandle {
	fhandle_t *nfs2_fh;
	nfs_fh3 *nfs3_fh;
};

static struct modlmisc modlmisc = {
	&mod_miscops, "Boot diskless on UDP/IP v1.3"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

/*
 * Too many symbols in rpcmod to make stubs for them all
 */
char _depends_on[] = "strmod/rpcmod";

_init()
{

	return (mod_install(&modlinkage));
}

_fini()
{

	return (mod_remove(&modlinkage));
}

_info(modinfop)
	struct modinfo *modinfop;
{

	return (mod_info(&modlinkage, modinfop));
}


static enum clnt_stat	pmap_rmt_call(struct knetconfig *, struct netbuf *,
	u_long, u_long, u_long, xdrproc_t, caddr_t, xdrproc_t, caddr_t,
	struct timeval, struct netbuf *);
static bool_t		myxdr_rmtcall_args(XDR *, struct rmtcallargs *);
static bool_t		myxdr_rmtcallres(XDR *, struct rmtcallres *);
static bool_t		myxdr_pmap(XDR *, struct pmap *);
static bool_t		myxdr_fhstatus(XDR *xdrs, struct fhstatus *fhsp);
static bool_t		myxdr_fhandle(XDR *xdrs, fhandle_t *fh);
static bool_t		myxdr_mountres3(XDR *xdrs, struct mountres3 *objp);
static bool_t		myxdr_mountstat3(XDR *xdrs, enum mountstat3 *objp);
static bool_t		myxdr_mountres3_ok(XDR *xdrs,
	struct mountres3_ok *objp);
static bool_t		myxdr_fhandle3(XDR *xdrs, struct fhandle3 *objp);
static enum clnt_stat	pmap_kgetport(struct knetconfig *, struct netbuf *,
	u_long, u_long, u_long);
static enum clnt_stat	callrpc(struct knetconfig *, struct netbuf *,
	u_long, u_long, u_long, xdrproc_t, char *, xdrproc_t, char *);
static void		dl_ftp(TIUSER *);
static int		dl_gtp(TIUSER **);
static int		ifioctl(TIUSER *, int, struct netbuf *);
static int		getfile(struct knetconfig *, TIUSER *, char *,
	char *, struct netbuf *, char *);
static int		mountnfs(struct knetconfig *, struct netbuf *,
	char *, char *, fhandle_t *);
static int		mountnfs3(struct knetconfig *, struct netbuf *,
	char *, char *, nfs_fh3 *);
static int		revarp_myaddr(TIUSER *);
static void		revarp_start(vnode_t *);
static void		revarpinput(vnode_t *);
static void		init_netbuf(TIUSER *, struct netbuf *);
static int		rtioctl(TIUSER *, int, struct rtentry *);
static int		dl_info(vnode_t *, dl_info_ack_t *);
static int		dl_attach(vnode_t *, int);
static int		dl_bind(vnode_t *, u_long, u_long, u_long, u_long);
static int		mount_cmn(struct knetconfig *, char *, struct netbuf *,
	union dl_fhandle, char *, char *, int);
static void		init_config(void);

static int	dldebug = 0;

/*
 * Should be in some common
 * ethernet source file.
 */
static struct ether_addr etherbroadcastaddr = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static struct netbuf myaddr;

/*
 * Should go in "arp.h" which doesn't
 * exist yet.
 */
struct	ether_arp {
	struct	arphdr ea_hdr;		/* fixed-size header */
	struct ether_addr arp_sha;	/* sender hardware address */
	u_char	arp_spa[4];		/* sender protocol address */
	struct ether_addr arp_tha;	/* target hardware address */
	u_char	arp_tpa[4];		/* target protocol address */
};
#define	arp_hrd	ea_hdr.ar_hrd
#define	arp_pro	ea_hdr.ar_pro
#define	arp_hln	ea_hdr.ar_hln
#define	arp_pln	ea_hdr.ar_pln
#define	arp_op	ea_hdr.ar_op



/*
 * "ifname" is the interface name/unit as read from the boot
 * arguments.
 * "ndev" is the major device number of the network interface
 * used to boot from.
 *
 * Both of these are initiallized in "init_config()".
 */
static char	ifname[32];
static dev_t	ndev;

#define		CLONE		"clone"
int CLONE_MAJ;

static struct knetconfig dl_netconf = {
	NC_TPI_CLTS,			/* semantics */
	NC_INET,			/* family */
	NC_UDP,				/* protocol */
	0,				/* device */
};

int
mount_root(struct knetconfig *knconf, char *name, struct netbuf *root_addr,
	fhandle_t *root_fhandle, char *root_hostname, char *root_path)
{
	union dl_fhandle root_fh;

	root_fh.nfs2_fh = root_fhandle;
	return (mount_cmn(knconf, name, root_addr, root_fh, root_hostname,
		root_path, NFS_VERSION));
}

int
mount3_root(struct knetconfig *knconf, char *name, struct netbuf *root_addr,
	nfs_fh3 *root_fhandle, char *root_hostname, char *root_path)
{
	union dl_fhandle root_fh;

	root_fh.nfs3_fh = root_fhandle;
	return (mount_cmn(knconf, name, root_addr, root_fh, root_hostname,
		root_path, NFS_V3));
}

static int
mount_cmn(struct knetconfig *cf, char *name, struct netbuf *addr,
	union dl_fhandle fh_u, char *host, char *path, int version)
{
	int rc;
	TIUSER *tiptr;
	static int init_done = 0;

	if (dldebug)
		printf("mount_cmn: name=%s\n", name);

	if (init_done == 0) {
		init_config();
		init_done = 1;
	}

	if (rc = dl_gtp(&tiptr)) {
		nfs_perror(rc, "mount_cmn: dl_gtp failed: %m.\n");
		return (rc);
	}

	/*
	 * Copy knetconfig information from the template, note that the
	 * rdev field has been set by init_config above.
	 */
	ASSERT(cf->knc_protofmly != NULL);
	ASSERT(cf->knc_proto != NULL);
	cf->knc_semantics = dl_netconf.knc_semantics;
	cf->knc_rdev = dl_netconf.knc_rdev;
	(void) strcpy(cf->knc_protofmly, dl_netconf.knc_protofmly);
	(void) strcpy(cf->knc_proto, dl_netconf.knc_proto);

	init_netbuf(tiptr, addr);

	do {
		rc = getfile(cf, tiptr, name, host, addr, path);
	} while (rc == ETIMEDOUT);

	if (rc) {
		dl_ftp(tiptr);
		return (rc);
	}

	switch (version) {
	case NFS_VERSION:
		rc = mountnfs(cf, addr, host, path, fh_u.nfs2_fh);
		break;
	case NFS_V3:
		rc = mountnfs3(cf, addr, host, path, fh_u.nfs3_fh);
		break;
	default:
		rc = ENXIO;
		break;
	}

	/*
	 * Not needed anymore.
	 */
	dl_ftp(tiptr);

	if (rc) {
		if (dldebug) {
			nfs_perror(rc, "mount_cmn: mount %s:%s failed: %m\n",
				host, path);
		}
		return (rc);
	}

	if (dldebug)
		printf("mount_cmn: leaving\n");

	return (0);
}

/*
 * Call mount daemon on server `sa' to mount path.
 * `port' is set to nfs port and fh is the fhandle
 * returned from the server.
 */
static int
mountnfs(struct knetconfig *knconf, struct netbuf *sa, char *server,
	char *path, fhandle_t *fh)
{
	struct fhstatus fhs;
	enum clnt_stat stat;

	if (dldebug)
		printf("mountnfs: entered\n");

	/*
	 * Get the port number for the mount program.
	 * pmap_kgetport first tries a SunOS portmapper
	 * and, if no reply is received, will try a
	 * SVR4 rpcbind. Either way, `sa' is set to
	 * the correct address.
	 */
	do {
		stat = pmap_kgetport(knconf, sa, (u_long)MOUNTPROG,
			(u_long)MOUNTVERS, (u_long)IPPROTO_UDP);

		if (stat == RPC_TIMEDOUT) {
			cmn_err(CE_WARN,
			    "mountnfs: %s:%s portmap not responding",
			    server, path);
		} else if (stat != RPC_SUCCESS) {
			cmn_err(CE_WARN,
			    "mountnfs: pmap_kgetport RPC error %d (%s).",
			    stat, clnt_sperrno(stat));
			return (ENXIO);	/* XXX */
		}
	} while (stat == RPC_TIMEDOUT);

	/*
	 * The correct port number has been
	 * put into `sa' by pmap_kgetport().
	 */
	do {
		stat = callrpc(knconf, sa, (u_long)MOUNTPROG,
			(u_long)MOUNTVERS, (u_long)MOUNTPROC_MNT,
			xdr_bp_path_t, (char *)&path,
			myxdr_fhstatus, (char *)&fhs);
		if (stat == RPC_TIMEDOUT) {
			cmn_err(CE_WARN,
			    "mountnfs: %s:%s mount server not responding",
			    server, path);
		}
	} while (stat == RPC_TIMEDOUT);

	if (stat != RPC_SUCCESS) {
		cmn_err(CE_WARN, "mountnfs: RPC failed: error %d (%s).",
		    stat, clnt_sperrno(stat));
		return (ENXIO);	/* XXX */
	}

	((struct sockaddr_in *)sa->buf)->sin_port = htons(NFS_PORT);
	*fh = fhs.fhs_fh;
	if (fhs.fhs_status != 0) {
		if (dldebug)
			printf("mountnfs: fhs_status %d\n", fhs.fhs_status);
		return (ENXIO);		/* XXX */
	}

	if (dldebug)
		printf("mountnfs: leaving\n");
	return (0);

}

/*
 * Call mount daemon on server `sa' to mount path.
 * `port' is set to nfs port and fh is the fhandle
 * returned from the server.
 */
static int
mountnfs3(struct knetconfig *knconf, struct netbuf *sa, char *server,
	char *path, nfs_fh3 *fh)
{
	struct mountres3 mountres3;
	enum clnt_stat stat;
	int ret = 0;

	if (dldebug)
		printf("mountnfs3: entered\n");

	/*
	 * Get the port number for the mount program.
	 * pmap_kgetport first tries a SunOS portmapper
	 * and, if no reply is received, will try a
	 * SVR4 rpcbind. Either way, `sa' is set to
	 * the correct address.
	 */
	do {
		stat = pmap_kgetport(knconf, sa, (u_long)MOUNTPROG,
			(u_long)MOUNTVERS3, (u_long)IPPROTO_UDP);

		if (stat == RPC_PROGVERSMISMATCH) {
			if (dldebug)
				printf("mountnfs3: program/version mismatch\n");
			return (EPROTONOSUPPORT); /* XXX */
		} else if (stat == RPC_TIMEDOUT) {
			cmn_err(CE_WARN,
			    "mountnfs3: %s:%s portmap not responding",
			    server, path);
		} else if (stat != RPC_SUCCESS) {
			cmn_err(CE_WARN,
			    "mountnfs3: pmap_kgetport RPC error %d (%s).",
			    stat, clnt_sperrno(stat));
			return (ENXIO);	/* XXX */
		}
	} while (stat == RPC_TIMEDOUT);

	mountres3.mountres3_u.mountinfo.fhandle.fhandle3_val = NULL;
	mountres3.mountres3_u.mountinfo.auth_flavors.auth_flavors_val = NULL;

	/*
	 * The correct port number has been
	 * put into `sa' by pmap_kgetport().
	 */
	do {
		stat = callrpc(knconf, sa, (u_long)MOUNTPROG,
			(u_long)MOUNTVERS3, (u_long)MOUNTPROC_MNT,
			xdr_bp_path_t, (char *)&path,
			myxdr_mountres3, (char *)&mountres3);
		if (stat == RPC_TIMEDOUT) {
			cmn_err(CE_WARN,
			    "mountnfs3: %s:%s mount server not responding",
			    server, path);
		}
	} while (stat == RPC_TIMEDOUT);

	if (stat == RPC_PROGVERSMISMATCH) {
		if (dldebug)
			printf("mountnfs3: program/version mismatch\n");
		ret = EPROTONOSUPPORT;
		goto out;
	}
	if (stat != RPC_SUCCESS) {
		cmn_err(CE_WARN, "mountnfs3: RPC failed: error %d (%s).",
		    stat, clnt_sperrno(stat));
		ret = ENXIO;	/* XXX */
		goto out;
	}

	if (mountres3.fhs_status != MNT_OK) {
		if (dldebug)
			printf("mountnfs3: fhs_status %d\n",
					mountres3.fhs_status);
		ret = ENXIO;	/* XXX */
		goto out;
	}

	((struct sockaddr_in *)sa->buf)->sin_port = htons(NFS_PORT);
	fh->fh3_length = mountres3.mountres3_u.mountinfo.fhandle.fhandle3_len;
	bcopy(mountres3.mountres3_u.mountinfo.fhandle.fhandle3_val,
				fh->fh3_u.data,
				fh->fh3_length);

out:
	xdr_free(myxdr_mountres3, (caddr_t) &mountres3);

	if (dldebug)
		printf("mountnfs3: leaving\n");
	return (ret);
}

static struct netbuf bootparam_addr;

/*
 * Returns after filling in the following global variables:
 *	bootparam_addr,
 *	utsname.nodename,
 *	srpc_domain.
 */
static int
whoami(struct knetconfig *knconf, TIUSER *tiptr)
{
	struct netbuf sa;
	struct netbuf req;
	struct bp_whoami_arg arg;
	struct bp_whoami_res res;
	struct timeval tv;
	enum clnt_stat stat;
	int rc;
	int namelen;
	int printed_waiting_msg;

	/*
	 * Find out our local (IP) address.
	 */
	if (rc = revarp_myaddr(tiptr)) {
		nfs_perror(rc, "whoami: revarp_myaddr failed: %m.\n");
		return (rc);
	}

	init_netbuf(tiptr, &bootparam_addr);
	init_netbuf(tiptr, &sa);
	init_netbuf(tiptr, &req);

	/*
	 * Pick up our interface broadcast (IP) address.
	 */
	if (rc = ifioctl(tiptr, SIOCGIFBRDADDR, &sa)) {
		nfs_perror(rc,
			"whoami: couldn't get broadcast IP address: %m.\n");
		return (rc);
	}

	/*
	 * Pick up our local (IP) address.
	 */
	if (rc = ifioctl(tiptr, SIOCGIFADDR, &req)) {
		nfs_perror(rc,
			"whoami: couldn't get my IP address: %m.\n");
		return (rc);
	}

	/*
	 * Set up the arguments expected by bootparamd.
	 */
	arg.client_address.address_type = IP_ADDR_TYPE;
	bcopy((caddr_t)&((struct sockaddr_in *)req.buf)->sin_addr,
		(caddr_t)&arg.client_address.bp_address.ip_addr,
		sizeof (struct in_addr));

	/*
	 * Initial retransmission interval
	 */
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	res.client_name = (bp_machine_name_t)kmem_alloc(SYS_NMLN+1, KM_SLEEP);
	res.domain_name = (bp_machine_name_t)kmem_alloc(SYS_NMLN+1, KM_SLEEP);

	/*
	 * Do a broadcast call to find a bootparam daemon that
	 * will tell us our hostname, domainname and any
	 * router that we have to use to talk to our NFS server.
	 */
	printed_waiting_msg = 0;
	do {
		/*
		 * pmap_rmt_call will first try the SunOS portmapper
		 * and if no reply is received will then try the SVR4
		 * rpcbind.
		 * Either way, `bootparam_addr' will be set to the
		 * correct address for the bootparamd that responds.
		 */
		stat = pmap_rmt_call(knconf, &sa, (u_long)BOOTPARAMPROG,
			(u_long)BOOTPARAMVERS, (u_long)BOOTPARAMPROC_WHOAMI,
			xdr_bp_whoami_arg, (caddr_t)&arg,
			xdr_bp_whoami_res, (caddr_t)&res,
			tv, &bootparam_addr);
		if (stat == RPC_TIMEDOUT && !printed_waiting_msg) {
			cmn_err(CE_WARN,
			    "No bootparam server responding; still trying");
			printed_waiting_msg = 1;
		}
		/*
		 * Retransmission interval for second and subsequent tries.
		 * We expect first pmap_rmt_call to retransmit and backoff to
		 * at least this value.
		 */
		tv.tv_sec = 20;
		tv.tv_usec = 0;
	} while (stat == RPC_TIMEDOUT);

	if (printed_waiting_msg)
		printf("Bootparam response received\n");

	if (stat != RPC_SUCCESS) {
		/*
		 * XXX should get real error here
		 */
		rc = ENXIO;
		cmn_err(CE_WARN,
		    "whoami: bootparam RPC failed: error %d (%s).",
		    stat, clnt_sperrno(stat));
		goto done;
	}

	namelen = strlen(res.client_name);
	if (namelen > sizeof (utsname.nodename)) {
		printf("whoami: hostname too long");
		rc = ENAMETOOLONG;
		goto done;
	}
	if (namelen > 0) {
		bcopy((caddr_t)res.client_name, (caddr_t)&utsname.nodename,
		    (u_int)namelen);
		cmn_err(CE_CONT, "?hostname: %s\n", utsname.nodename);
	} else {
		printf("whoami: no host name\n");
		rc = ENXIO;
		goto done;
	}

	namelen = strlen(res.domain_name);
	if (namelen > SYS_NMLN) {
		printf("whoami: domainname too long");
		rc = ENAMETOOLONG;
		goto done;
	}
	if (namelen > 0) {
		bcopy((caddr_t)res.domain_name, (caddr_t)&srpc_domain,
		    (u_int)namelen);
		cmn_err(CE_CONT, "?domainname: %s\n", srpc_domain);
	} else {
		printf("whoami: no domain name\n");
	}

	if (res.router_address.address_type == IP_ADDR_TYPE) {
		struct rtentry		rtentry;
		struct sockaddr_in	*sin;
		struct in_addr		ipaddr;

		bcopy((caddr_t)&res.router_address.bp_address.ip_addr,
			(caddr_t)&ipaddr,
			sizeof (struct in_addr));

		if (ipaddr.s_addr != (u_long)0) {
			sin = (struct sockaddr_in *)&rtentry.rt_dst;
			bzero((caddr_t)sin, sizeof (*sin));
			sin->sin_family = AF_INET;

			sin = (struct sockaddr_in *)&rtentry.rt_gateway;
			bzero((caddr_t)sin, sizeof (*sin));
			sin->sin_family = AF_INET;
			sin->sin_addr.s_addr = ipaddr.s_addr;

			rtentry.rt_flags = RTF_GATEWAY | RTF_UP;

			if (rc = rtioctl(tiptr, SIOCADDRT, &rtentry)) {
				nfs_perror(rc,
					"whoami: couldn't add route: %m.\n");
				goto done;
			}
		}
	} else	printf("whoami: unknown gateway addr family %d\n",
			    res.router_address.address_type);
done:
	kmem_free(res.client_name, SYS_NMLN+1);
	kmem_free(res.domain_name, SYS_NMLN+1);
	kmem_free(sa.buf, sa.maxlen);
	kmem_free(req.buf, req.maxlen);
	return (rc);
}

/*
 * Returns:
 *	1) The ascii form of our root servers name in `server_name'.
 *	2) Actual network address of our root server in `server_address'.
 *	3) Pathname of our root on the server in `server_path'.
 */
static int
getfile(struct knetconfig *knconf, TIUSER *tiptr, char *fileid,
	char *server_name, struct netbuf *server_address, char *server_path)
{
	struct bp_getfile_arg arg;
	struct bp_getfile_res res;
	enum clnt_stat stat;
	int rc;
	struct in_addr ipaddr;

	if (dldebug)
		printf("getfile: entered\n");

	arg.client_name = (caddr_t)&utsname.nodename;
	arg.file_id = fileid;

	bzero((caddr_t)&res, sizeof (res));
	if (bootparam_addr.len == 0) {
		if (rc = whoami(knconf, tiptr))
			return (rc);
	}
	res.server_name = (bp_machine_name_t)kmem_alloc(SYS_NMLN+1, KM_SLEEP);
	res.server_path = (bp_machine_name_t)kmem_alloc(SYS_NMLN+1, KM_SLEEP);

	/*
	 * bootparam_addr was filled in by the call to
	 * whoami(), so now send an rpc message to the
	 * bootparam daemon requesting our server information.
	 */
	stat = callrpc(knconf, &bootparam_addr, (u_long)BOOTPARAMPROG,
	    (u_long)BOOTPARAMVERS, (u_long)BOOTPARAMPROC_GETFILE,
	    xdr_bp_getfile_arg, (caddr_t)&arg,
	    xdr_bp_getfile_res, (caddr_t)&res);

	if (stat == RPC_SUCCESS) {
		(void) strcpy(server_name, res.server_name);
		(void) strcpy(server_path, res.server_path);
	}

	kmem_free(res.server_name, SYS_NMLN+1);
	kmem_free(res.server_path, SYS_NMLN+1);
	if (stat != RPC_SUCCESS) {
		cmn_err(CE_WARN, "getfile: RPC failed: error %d (%s).",
		    stat, clnt_sperrno(stat));
		return ((stat == RPC_TIMEDOUT) ? ETIMEDOUT : ENXIO); /* XXX */
	}

	if (*server_name == '\0' || *server_path == '\0') {
		return (EINVAL);
	}

	switch (res.server_address.address_type) {
	case IP_ADDR_TYPE:
		/*
		 * server_address is where we will get our root
		 * from.
		 */
		((struct sockaddr_in *)server_address->buf)->sin_family =
					AF_INET;
		bcopy((caddr_t)&res.server_address.bp_address.ip_addr,
				(caddr_t)&ipaddr, sizeof (struct in_addr));
		if (ipaddr.s_addr == 0)
			return (EINVAL);

		((struct sockaddr_in *)server_address->buf)->sin_addr.s_addr =
					ipaddr.s_addr;
		server_address->len = sizeof (struct sockaddr_in);
		break;

	default:
		printf("getfile: unknown address type %d\n",
			res.server_address.address_type);
		return (EPROTONOSUPPORT);
	}
	if (dldebug)
		printf("getfile: leaving\n");
	return (0);
}

/*
 * Create a transport handle.
 */
static int
dl_gtp(TIUSER **tiptr)
{
	int rc;

	if ((rc = t_kopen((file_t *)NULL, dl_netconf.knc_rdev,
				FREAD|FWRITE, tiptr, CRED())) != 0)
		nfs_perror(rc, "dl_getvp: t_kopen failed: %m.\n");
	return (rc);
}

/*
 * Free a transport handle.
 */
static void
dl_ftp(TIUSER *tiptr)
{

	(void) t_kclose(tiptr, 1);
}

/*
 * Initialize a netbuf suitable for
 * describing an address for the
 * transport defined by `tiptr'.
 */
static void
init_netbuf(TIUSER *tiptr, struct netbuf *nbuf)
{

	nbuf->buf = kmem_zalloc(tiptr->tp_info.addr, KM_SLEEP);
	nbuf->maxlen = tiptr->tp_info.addr;
	nbuf->len = 0;
}

static int
rtioctl(TIUSER *tiptr, int cmd, struct rtentry *rtentry)
{
	struct strioctl iocb;
	int rc;
	vnode_t *vp;

	iocb.ic_cmd = cmd;
	iocb.ic_timout = 0;
	iocb.ic_len = sizeof (struct rtentry);
	iocb.ic_dp = (caddr_t)rtentry;

	vp = tiptr->fp->f_vnode;
	rc = kstr_ioctl(vp, I_STR, (int)&iocb);
	if (rc)
		nfs_perror(rc, "rtioctl: kstr_ioctl failed: %m\n");
	return (rc);
}

/*
 * Send an ioctl down the stream defined
 * by `tiptr'.
 *
 * We isolate the ifreq dependencies in here. The
 * ioctl really ought to take a netbuf and be of
 * type TRANSPARENT - one day.
 */
static int
ifioctl(TIUSER *tiptr, int cmd, struct netbuf *nbuf)
{
	struct strioctl iocb;
	int rc;
	vnode_t *vp;
	struct ifreq ifr;

	/*
	 * Now do the one requested.
	 */
	if (nbuf->len)
		ifr.ifr_addr = *(struct sockaddr *)nbuf->buf;
	strncpy((caddr_t)&ifr.ifr_name, ifname, sizeof (ifr.ifr_name));
	iocb.ic_cmd = cmd;
	iocb.ic_timout = 0;
	iocb.ic_len = sizeof (ifr);
	iocb.ic_dp = (caddr_t)&ifr;

	vp = tiptr->fp->f_vnode;
	rc = kstr_ioctl(vp, I_STR, (int)&iocb);
	if (rc) {
		nfs_perror(rc, "ifioctl: kstr_ioctl failed: %m\n");
		return (rc);
	}

	/*
	 * Set reply length.
	 */
	if (nbuf->len == 0) {
		/*
		 * GET type.
		 */
		nbuf->len = sizeof (struct sockaddr);
		*(struct sockaddr *)nbuf->buf = ifr.ifr_addr;
	}

	return (0);
}

static int
setifflags(TIUSER *tiptr, int value)
{
	struct ifreq ifr;
	int rc;
	struct strioctl iocb;

	strncpy((caddr_t)&ifr.ifr_name, ifname, sizeof (ifr.ifr_name));
	iocb.ic_cmd = SIOCGIFFLAGS;
	iocb.ic_timout = 0;
	iocb.ic_len = sizeof (ifr);
	iocb.ic_dp = (caddr_t)&ifr;
	if (rc = kstr_ioctl(tiptr->fp->f_vnode, I_STR, (int)&iocb))
		return (rc);

	ifr.ifr_flags |= value;
	iocb.ic_cmd = SIOCSIFFLAGS;
	return (kstr_ioctl(tiptr->fp->f_vnode, I_STR, (int)&iocb));
}

static kmutex_t		mylock;	/* protects sleep for myaddr */
static kcondvar_t	mycv;	/* wakeup address for myaddr */

/*
 * REVerse Address Resolution Protocol (revarp)
 * is used by a diskless client to find out its
 * IP address when all it knows is its Ethernet address.
 *
 * Basically, we open the ethernet driver, attach and bind
 * (DL_BIND_REQ) it, and then format a broadcast RARP
 * message for it to send. We pick up the reply and
 * let the caller set the interface address using SIOCSIFADDR.
 */
static int
revarp_myaddr(TIUSER *tiptr)
{
	int rc;
	vnode_t *vp;
	dl_info_ack_t info;
	struct sockaddr_in sin;
	struct netbuf sbuf;
	dev_t dev;

	mutex_init(&mylock, "revarp_myaddr: protect myaddr", MUTEX_DEFAULT,
			DEFAULT_WT);

	if (dldebug)
		printf("revarp_myaddr: entered\n");

	if (rc = kstr_open(CLONE_MAJ, ndev, &vp, NULL)) { /* XXX */
		nfs_perror(rc, "revarp_myaddr: kstr_open failed: %m\n");
		return (rc);
	}

	dev = ddi_pathname_to_dev_t(rootfs.bo_name);
#ifdef i386
	if (rc = dl_attach(vp, 0)) {
#else
	if (rc = dl_attach(vp, getminor(dev))) {
#endif
		nfs_perror(rc, "revarp_myaddr: dl_attach failed: %m\n");
		return (rc);
	}

	if (rc = dl_bind(vp, ETHERTYPE_REVARP, 0, DL_CLDLS, 0)) {
		nfs_perror(rc, "revarp_myaddr: dl_bind failed: %m\n");
		return (rc);
	}

	/*
	 * Initialize `myaddr'.
	 */
	if (rc = dl_info(vp, &info)) {
		nfs_perror(rc, "revarp_myaddr: dl_info failed: %m\n");
		return (rc);
	}

	myaddr.maxlen = info.dl_addr_length;
	myaddr.buf = (char *)kmem_alloc(myaddr.maxlen, KM_SLEEP);

	revarp_start(vp);

	mutex_enter(&mylock);
	while (myaddr.len == 0)
		(void) cv_wait_sig(&mycv, &mylock);
	mutex_exit(&mylock);

	bcopy((caddr_t)myaddr.buf, (caddr_t)&sin.sin_addr, myaddr.len);
	sin.sin_family = AF_INET;

	sbuf.buf = (caddr_t)&sin;
	sbuf.maxlen = sbuf.len = sizeof (sin);
	if (rc = ifioctl(tiptr, SIOCSIFADDR, &sbuf)) {
		nfs_perror(rc,
		    "revarp_myaddr: couldn't set interface net address: %m\n");
		return (rc);
	}

	/*
	 * Now turn on the interface.
	 */
	if (rc = setifflags(tiptr, IFF_UP)) {
		nfs_perror(rc,
		    "revarp_myaddr: couldn't enable network interface: %m\n");
		return (rc);
	}

	(void) kstr_close(vp, -1);
	(void) kmem_free(myaddr.buf, myaddr.maxlen);

	return (0);
}

static void
revarp_start(vnode_t *vp)
{
	register struct ether_arp *ea;
	struct ether_addr myether;
	int rc;
	dl_unitdata_req_t *dl_udata;
	mblk_t *bp;
	mblk_t *mp;
	struct dladdr *dlsap;
	static int done = 0;

getreply:
	if (myaddr.len != 0) {
		cmn_err(CE_CONT, "?Found my IP address: %x (%d.%d.%d.%d)\n",
				*(int *)myaddr.buf,
				(u_char)myaddr.buf[0],
				(u_char)myaddr.buf[1],
				(u_char)myaddr.buf[2],
				(u_char)myaddr.buf[3]);
		return;
	}

	(void) localetheraddr((struct ether_addr *)NULL, &myether);
	if (done++ == 0)
		cmn_err(CE_CONT, "?Requesting Internet address for %s\n",
		    ether_sprintf(&myether));

	/*
	 * Send another RARP request.
	 */
	if ((mp = allocb(sizeof (dl_unitdata_req_t) + sizeof (*dlsap),
				BPRI_HI)) == (mblk_t *)NULL) {
		cmn_err(CE_WARN, "revarp_myaddr: allocb no memory");
		return;
	}
	if ((bp = allocb(sizeof (struct ether_arp), BPRI_HI)) ==
					(mblk_t *)NULL) {
		cmn_err(CE_WARN, "revarp_myaddr: allocb no memory");
		return;
	}

	/*
	 * Format the transmit request part.
	 */
	mp->b_datap->db_type = M_PROTO;
	dl_udata = (dl_unitdata_req_t *)mp->b_wptr;
	mp->b_wptr += sizeof (dl_unitdata_req_t) + sizeof (*dlsap);
	dl_udata->dl_primitive = DL_UNITDATA_REQ;
	dl_udata->dl_dest_addr_length = sizeof (*dlsap);
	dl_udata->dl_dest_addr_offset = sizeof (*dl_udata);
	dl_udata->dl_priority.dl_min = 0;
	dl_udata->dl_priority.dl_max = 0;

	dlsap = (struct dladdr *)(mp->b_rptr + sizeof (*dl_udata));
	bcopy((char *)&etherbroadcastaddr, (char *)&dlsap->dl_phys,
				sizeof (struct ether_addr));
	dlsap->dl_sap = htons(ETHERTYPE_REVARP);

	/*
	 * Format the actual REVARP request.
	 */
	ea = (struct ether_arp *)bp->b_wptr;
	bp->b_wptr += sizeof (struct ether_arp);
	ea->arp_hrd = htons(ARPHRD_ETHER);
	ea->arp_pro = htons(ETHERTYPE_IP);
	ea->arp_hln = sizeof (ea->arp_sha);	/* hardware address length */
	ea->arp_pln = sizeof (ea->arp_spa);	/* protocol address length */
	ea->arp_op = htons(REVARP_REQUEST);
	ether_copy(&myether, &ea->arp_sha);
	ether_copy(&myether, &ea->arp_tha);

	mp->b_cont = bp;

	if ((rc = kstr_msg(vp, mp, (mblk_t **)NULL, (timestruc_t *)NULL))
			!= 0) {
		nfs_perror(rc, "revarp_start: kstr_msg failed: %m\n");
		return;
	}
	revarpinput(vp);

	goto getreply;
}

/*
 * Client side Reverse-ARP input
 * Server side is handled by user level server
 */
static void
revarpinput(vnode_t *vp)
{
	register struct ether_arp *ea;
	struct ether_addr myether;
	mblk_t *bp;
	mblk_t *mp;
	int len;
	int rc;
	timestruc_t tv, give_up;

	/*
	 * Choose the time at which we will give up, and resend our
	 * request.
	 */
	give_up = hrestime;
	give_up.tv_sec += 5;
wait:
	/*
	 * Compute new timeout value.
	 */
	tv = give_up;
	timespecsub(&tv, &hrestime);
	/*
	 * If we don't have at least one full second remaining, give up.
	 * This means we might wait only just over 4.0 seconds, but that's
	 * okay.  What isn't okay is that kstr_msg silently doesn't do a
	 * timeout at all if the timestruc_t's value doesn't indicate more
	 * than a full clock tick.  So we take the conservative way out.
	 * XXX Should fix kstr_msg(), then fix this.
	 */
	if (tv.tv_sec <= 0) {
		return;
	}
	if ((rc = kstr_msg(vp, (mblk_t *)NULL, &mp, &tv)) != 0) {
		nfs_perror(rc, "revarpinput: kstr_msg failed: %m\n");
		return;
	}

	if (mp == NULL)
		goto out;

	if (mp->b_cont == NULL) {
		printf("revarpinput: b_cont == NULL\n");
		goto out;
	}

	if (mp->b_datap->db_type != M_PROTO) {
		printf("revarpinput: bad header type %d\n",
			mp->b_datap->db_type);
		goto out;
	}

	bp = mp->b_cont;

	if ((len = bp->b_wptr-bp->b_rptr) < sizeof (*ea)) {
		printf("revarpinput: bad data len %d, expect %d\n",
					len,  sizeof (*ea));
		goto out;
	}

	ea = (struct ether_arp *)bp->b_rptr;

	if ((u_short)ntohs(ea->arp_pro) != ETHERTYPE_IP) {
		/* We could have received another broadcast arp packet. */
		if (dldebug)
			printf("revarpinput: bad type %x\n",
				(u_short)ntohs(ea->arp_pro));
		freemsg(mp);
		goto wait;
	}
	if ((u_short)ntohs(ea->arp_op) != REVARP_REPLY) {
		/* We could have received a broadcast arp request. */
		if (dldebug)
			printf("revarpinput: bad op %x\n",
				(u_short)ntohs(ea->arp_op));
		freemsg(mp);
		goto wait;
	}

	(void) localetheraddr((struct ether_addr *)NULL, &myether);
	if (!ether_cmp(&ea->arp_tha, &myether)) {
		bcopy((char *)&ea->arp_tpa, (caddr_t)myaddr.buf,
				sizeof (ea->arp_tpa));
		myaddr.len = sizeof (ea->arp_tpa);

		mutex_enter(&mylock);
		cv_broadcast(&mycv);
		mutex_exit(&mylock);
	} else {
		/* We could have gotten a broadcast arp response. */
		if (dldebug)
			printf("revarpinput: got reply, but not my address\n");
		freemsg(mp);
		goto wait;
	}
out:
	freemsg(mp);
}

/*
 * From rpcsvc/mountxdr.c in SunOS. We can't
 * put this into the rpc directory because
 * it calls xdr_fhandle() which is in a
 * loadable module.
 */
static bool_t
myxdr_fhstatus(XDR *xdrs, struct fhstatus *fhsp)
{

	if (!xdr_int(xdrs, &fhsp->fhs_status))
		return (FALSE);
	if (fhsp->fhs_status == 0) {
		if (!myxdr_fhandle(xdrs, &fhsp->fhs_fh))
			return (FALSE);
	}
	return (TRUE);
}

/*
 * From nfs_xdr.c.
 *
 * File access handle
 * The fhandle struct is treated a opaque data on the wire
 */
static bool_t
myxdr_fhandle(XDR *xdrs, fhandle_t *fh)
{

	if (xdr_opaque(xdrs, (caddr_t)fh, NFS_FHSIZE)) {
		return (TRUE);
	}
	return (FALSE);
}

static bool_t
myxdr_mountres3(XDR *xdrs, struct mountres3 *objp)
{
	if (!myxdr_mountstat3(xdrs, &objp->fhs_status))
		return (FALSE);
	switch (objp->fhs_status) {
	case MNT_OK:
		if (!myxdr_mountres3_ok(xdrs, &objp->mountres3_u.mountinfo))
			return (FALSE);
		break;
	}
	return (TRUE);
}

static bool_t
myxdr_mountstat3(XDR *xdrs, enum mountstat3 *objp)
{
	if (!xdr_enum(xdrs, (enum_t *)objp))
		return (FALSE);
	return (TRUE);
}

static bool_t
myxdr_mountres3_ok(XDR *xdrs, struct mountres3_ok *objp)
{
	if (!myxdr_fhandle3(xdrs, &objp->fhandle))
		return (FALSE);
	if (!xdr_array(xdrs, (char **)&objp->auth_flavors.auth_flavors_val,
		(u_int *) &objp->auth_flavors.auth_flavors_len, ~0,
		sizeof (int), (xdrproc_t) xdr_int))
		return (FALSE);
	return (TRUE);
}

static bool_t
myxdr_fhandle3(XDR *xdrs, struct fhandle3 *objp)
{
	if (!xdr_bytes(xdrs, (char **)&objp->fhandle3_val,
			(u_int *) &objp->fhandle3_len, FHSIZE3))
		return (FALSE);
	return (TRUE);
}

/*
 * From SunOS pmap_clnt.c
 *
 * Port mapper routines:
 *	pmap_kgetport() - get port number.
 *	pmap_rmt_call()  - indirect call via port mapper.
 *
 */
static enum clnt_stat
pmap_kgetport(struct knetconfig *knconf, struct netbuf *call_addr,
	u_long prog, u_long vers, u_long prot)
{
	u_short port;
	int tries;
	enum clnt_stat stat;

	port = 0;

	((struct sockaddr_in *)call_addr->buf)->sin_port = htons(PMAPPORT);

	{
		struct pmap	parms;

		parms.pm_prog = prog;
		parms.pm_vers = vers;
		parms.pm_prot = prot;
		parms.pm_port = 0;
		for (tries = 0; tries < 5; tries++) {
			stat = callrpc(knconf, call_addr,
					PMAPPROG, PMAPVERS, PMAPPROC_GETPORT,
					myxdr_pmap, (char *)&parms,
					xdr_u_short, (char *)&port);

			if (stat != RPC_TIMEDOUT)
				break;
			cmn_err(CE_WARN,
		"pmap_kgetport: Portmapper not responding; still trying");
		}
	}

	if (stat != RPC_PROGUNAVAIL) {
		goto kgport_out;
	}

	{
		RPCB		parms;
		char		*ua;

		ua = (char *)NULL;

		cmn_err(CE_WARN,
		    "pmap_kgetport: Portmapper failed - trying rpcbind");

		parms.r_prog = prog;
		parms.r_vers = vers;
		parms.r_netid = (caddr_t)NULL;
		parms.r_addr = (caddr_t)NULL;
		parms.r_owner = (caddr_t)NULL;

		for (tries = 0; tries < 5; tries++) {
			stat = callrpc(knconf, call_addr,
					RPCBPROG, RPCBVERS, RPCBPROC_GETADDR,
					xdr_rpcb, (char *)&parms,
					xdr_wrapstring, (char *)&ua);

			if (stat != RPC_TIMEDOUT)
				break;
		cmn_err(CE_WARN,
		    "pmap_kgetport: rpcbind not responding; still trying");
		}

		if (stat == RPC_SUCCESS) {
			if ((ua == NULL) || (ua[0] == NULL)) {
				/* Address unknown */
				stat = RPC_PROGUNAVAIL;
				goto kgport_out;
			}
			port = rpc_uaddr2port(ua);
		}
	}


kgport_out:
	if (stat == RPC_SUCCESS) {
		((struct sockaddr_in *)call_addr->buf)->sin_port = ntohs(port);
	}
	return (stat);
}

/*
 * pmapper remote-call-service interface.
 * This routine is used to call the pmapper remote call service
 * which will look up a service program in the port maps, and then
 * remotely call that routine with the given parameters.  This allows
 * programs to do a lookup and call in one step.
 *
 * On return, `call addr' contains the port number for the
 * service requested, and `resp_addr' contains its IP address.
 */
static enum clnt_stat
pmap_rmt_call(struct knetconfig *knconf, struct netbuf *call_addr,
	u_long progn, u_long versn, u_long procn, xdrproc_t xdrargs,
	caddr_t argsp, xdrproc_t xdrres, caddr_t resp, struct timeval tout,
	struct netbuf *resp_addr)
{
	CLIENT *cl;
	enum clnt_stat stat;
	u_long port;
	int rc;

	((struct sockaddr_in *)call_addr->buf)->sin_port = htons(PMAPPORT);

#define	PMAP_RETRIES 5

	{
		struct rmtcallargs	a;
		struct rmtcallres	r;

		rc = clnt_tli_kcreate(knconf, call_addr, PMAPPROG, PMAPVERS,
					0, PMAP_RETRIES, CRED(), &cl);
		if (rc != 0) {
			nfs_perror(rc,
			    "pmap_rmt_call: clnt_tli_kcreate failed: %m\n");
			return (RPC_SYSTEMERROR);	/* XXX */
		}
		if (cl != (CLIENT *)NULL) {
			a.prog = progn;
			a.vers = versn;
			a.proc = procn;
			a.args_ptr = argsp;
			a.xdr_args = xdrargs;
			r.port_ptr = &port;
			r.results_ptr = resp;
			r.xdr_results = xdrres;
			stat = clnt_clts_kcallit_addr(cl, PMAPPROC_CALLIT,
						myxdr_rmtcall_args, (caddr_t)&a,
						myxdr_rmtcallres, (caddr_t) &r,
						tout, resp_addr);

			if (stat == RPC_SUCCESS)
			((struct sockaddr_in *)resp_addr->buf)->sin_port =
						htons((u_short)port);
			CLNT_DESTROY(cl);
		} else	panic("pmap_rmt_call: clnt_tli_kcreate failed");
	}

	if (stat != RPC_PROGUNAVAIL)
		return (stat);

	{
		struct rpcb_rmtcallargs	a;
		struct rpcb_rmtcallres	r;
		char			ua[100];	/* XXX */

		cmn_err(CE_WARN,
		    "pmap_rmt_call: Portmapper failed - trying rpcbind");

		rc = clnt_tli_kcreate(knconf, call_addr, RPCBPROG, RPCBVERS,
					0, PMAP_RETRIES, CRED(), &cl);
		if (rc != 0) {
			nfs_perror(rc,
			    "pmap_rmt_call: clnt_tli_kcreate failed: %m\n");
			return (RPC_SYSTEMERROR);	/* XXX */
		}
		if (cl != (CLIENT *)NULL) {
			a.prog = progn;
			a.vers = versn;
			a.proc = procn;
			a.args_ptr = argsp;
			a.xdr_args = xdrargs;
			r.addr_ptr = ua;
			r.results_ptr = resp;
			r.xdr_results = xdrres;
			stat = clnt_clts_kcallit_addr(cl, PMAPPROC_CALLIT,
					xdr_rpcb_rmtcallargs, (caddr_t)&a,
					xdr_rpcb_rmtcallres, (caddr_t) &r,
					tout, resp_addr);

			if (stat == RPC_SUCCESS)
			((struct sockaddr_in *)resp_addr->buf)->sin_port =
						rpc_uaddr2port(ua);
			CLNT_DESTROY(cl);
		} else	panic("pmap_rmt_call: clnt_tli_kcreate failed");
	}

	return (stat);
}

/*
 * XDR remote call arguments
 * written for XDR_ENCODE direction only
 */
static bool_t
myxdr_rmtcall_args(XDR *xdrs, struct rmtcallargs *cap)
{
	u_int lenposition;
	u_int argposition;
	u_int position;

	if (xdr_u_long(xdrs, &(cap->prog)) &&
	    xdr_u_long(xdrs, &(cap->vers)) &&
	    xdr_u_long(xdrs, &(cap->proc))) {
		lenposition = XDR_GETPOS(xdrs);
		if (!xdr_u_long(xdrs, &(cap->arglen)))
			return (FALSE);
		argposition = XDR_GETPOS(xdrs);
		if (!(*(cap->xdr_args))(xdrs, cap->args_ptr))
			return (FALSE);
		position = XDR_GETPOS(xdrs);
		cap->arglen = (u_long)position - (u_long)argposition;
		XDR_SETPOS(xdrs, lenposition);
		if (!xdr_u_long(xdrs, &(cap->arglen)))
			return (FALSE);
		XDR_SETPOS(xdrs, position);
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR remote call results
 * written for XDR_DECODE direction only
 */
static bool_t
myxdr_rmtcallres(XDR *xdrs, struct rmtcallres *crp)
{
	caddr_t port_ptr;

	port_ptr = (caddr_t)crp->port_ptr;
	if (xdr_reference(xdrs, &port_ptr, sizeof (u_long),
	    xdr_u_long) && xdr_u_long(xdrs, &crp->resultslen)) {
		crp->port_ptr = (u_long *)port_ptr;
		return ((*(crp->xdr_results))(xdrs, crp->results_ptr));
	}
	return (FALSE);
}

static bool_t
myxdr_pmap(XDR *xdrs, struct pmap *regs)
{

	if (xdr_u_long(xdrs, &regs->pm_prog) &&
		xdr_u_long(xdrs, &regs->pm_vers) &&
		xdr_u_long(xdrs, &regs->pm_prot))
		return (xdr_u_long(xdrs, &regs->pm_port));
	return (FALSE);
}


/*
 * From SunOS callrpc.c
 */
static enum clnt_stat
callrpc(struct knetconfig *knconf, struct netbuf *call_addr, u_long prognum,
	u_long versnum, u_long procnum, xdrproc_t inproc, char *in,
	xdrproc_t outproc, char *out)
{
	CLIENT *cl;
	struct timeval tv;
	enum clnt_stat cl_stat;
	int rc;

	rc = clnt_tli_kcreate(knconf, call_addr, prognum, versnum,
				0, 3, CRED(), &cl);
	if (rc) {
		nfs_perror(rc, "callrpc: clnt_tli_kcreate failed: %m\n");
		return (RPC_SYSTEMERROR);	/* XXX */
	}
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	cl_stat = CLNT_CALL(cl, procnum, inproc, in, outproc, out, tv);
	AUTH_DESTROY(cl->cl_auth);
	CLNT_DESTROY(cl);
	return (cl_stat);
}

static int
dl_info(vnode_t *vp, dl_info_ack_t *info)
{
	dl_info_req_t *info_req;
	dl_error_ack_t *error_ack;
	union DL_primitives *dl_prim;
	mblk_t *mp;
	int error;

	if ((mp = allocb(sizeof (dl_info_req_t), BPRI_MED)) ==
			(mblk_t *)NULL) {
		cmn_err(CE_WARN, "dl_info: allocb failed");
		return (ENOSR);
	}
	mp->b_datap->db_type = M_PROTO;

	info_req = (dl_info_req_t *)mp->b_wptr;
	mp->b_wptr += sizeof (dl_info_req_t);
	info_req->dl_primitive = DL_INFO_REQ;

	if ((error = kstr_msg(vp, mp, &mp, (timestruc_t *)NULL)) != 0) {
		nfs_perror(error, "dl_info: kstr_msg failed: %m\n");
		return (error);
	}

	dl_prim = (union DL_primitives *)mp->b_rptr;
	switch (dl_prim->dl_primitive) {
	case DL_INFO_ACK:
		if ((mp->b_wptr-mp->b_rptr) < sizeof (dl_info_ack_t)) {
			printf("dl_info: DL_INFO_ACK protocol error\n");
			break;
		}
		*info = *(dl_info_ack_t *)mp->b_rptr;
		freemsg(mp);
		return (0);

	case DL_ERROR_ACK:
		if ((mp->b_wptr-mp->b_rptr) < sizeof (dl_error_ack_t)) {
			printf("dl_info: DL_ERROR_ACK protocol error\n");
			break;
		}

		error_ack = (dl_error_ack_t *)dl_prim;
		printf("dl_info: DLPI error %ld\n",
		    error_ack->dl_errno);
		break;

	default:
		printf("dl_bind: bad ACK header %ld\n", dl_prim->dl_primitive);
		break;
	}

	/*
	 * Error return only.
	 */
	freemsg(mp);
	return (-1);
}

static int
dl_attach(vnode_t *vp, int unit)
{
	dl_attach_req_t *attach_req;
	dl_error_ack_t *error_ack;
	union DL_primitives *dl_prim;
	mblk_t *mp;
	int error;

	if ((mp = allocb(sizeof (dl_attach_req_t), BPRI_MED)) ==
			(mblk_t *)NULL) {
		cmn_err(CE_WARN, "dl_attach: allocb failed");
		return (ENOSR);
	}
	mp->b_datap->db_type = M_PROTO;
	mp->b_wptr += sizeof (dl_attach_req_t);

	attach_req = (dl_attach_req_t *)mp->b_rptr;
	attach_req->dl_primitive = DL_ATTACH_REQ;
	attach_req->dl_ppa = unit;

	if ((error = kstr_msg(vp, mp, &mp, (timestruc_t *)NULL)) != 0) {
		nfs_perror(error, "dl_attach: kstr_msg failed: %m\n");
		return (error);
	}

	dl_prim = (union DL_primitives *)mp->b_rptr;
	switch (dl_prim->dl_primitive) {
	case DL_OK_ACK:
		if ((mp->b_wptr-mp->b_rptr) < sizeof (dl_ok_ack_t)) {
			printf("dl_attach: DL_OK_ACK protocol error\n");
			break;
		}
		if (((dl_ok_ack_t *)dl_prim)->dl_correct_primitive !=
				DL_ATTACH_REQ) {
			printf("dl_attach: DL_OK_ACK rtnd prim %ld\n",
				((dl_ok_ack_t *)dl_prim)->dl_correct_primitive);
			break;
		}
		freemsg(mp);
		return (0);

	case DL_ERROR_ACK:
		if ((mp->b_wptr-mp->b_rptr) < sizeof (dl_error_ack_t)) {
			printf("dl_attach: DL_ERROR_ACK protocol error\n");
			break;
		}

		error_ack = (dl_error_ack_t *)dl_prim;
		switch (error_ack->dl_errno) {
		case DL_BADPPA:
			printf("dl_attach: DL_ERROR_ACK bad PPA\n");
			break;

		case DL_ACCESS:
			printf("dl_attach: DL_ERROR_ACK access error\n");
			break;

		default:
			printf("dl_attach: DLPI error %ld\n",
			    error_ack->dl_errno);
			break;
		}

	default:
		printf("dl_attach: bad ACK header %ld\n",
			dl_prim->dl_primitive);
		break;
	}

	/*
	 * Error return only.
	 */
	freemsg(mp);
	return (-1);
}

static int
dl_bind(vnode_t *vp, ulong sap, ulong max_conn, ulong service, ulong conn_mgmt)
{
	dl_bind_req_t *bind_req;
	dl_error_ack_t *error_ack;
	union DL_primitives *dl_prim;
	mblk_t *mp;
	int error;

	if ((mp = allocb(sizeof (dl_bind_req_t), BPRI_MED)) ==
			(mblk_t *)NULL) {
		cmn_err(CE_WARN, "dl_bind: allocb failed");
		return (ENOSR);
	}
	mp->b_datap->db_type = M_PROTO;

	bind_req = (dl_bind_req_t *)mp->b_wptr;
	mp->b_wptr += sizeof (dl_bind_req_t);
	bind_req->dl_primitive = DL_BIND_REQ;
	bind_req->dl_sap = sap;
	bind_req->dl_max_conind = max_conn;
	bind_req->dl_service_mode = service;
	bind_req->dl_conn_mgmt = conn_mgmt;
	bind_req->dl_xidtest_flg = 0;

	if ((error = kstr_msg(vp, mp, &mp, (timestruc_t *)NULL)) != 0) {
		nfs_perror(error, "dl_bind: kstr_msg failed: %m\n");
		return (error);
	}

	dl_prim = (union DL_primitives *)mp->b_rptr;
	switch (dl_prim->dl_primitive) {
	case DL_BIND_ACK:
		if ((mp->b_wptr-mp->b_rptr) < sizeof (dl_bind_ack_t)) {
			printf("dl_bind: DL_BIND_ACK protocol error\n");
			break;
		}
		if (((dl_bind_ack_t *)dl_prim)->dl_sap != sap) {
			printf("dl_bind: DL_BIND_ACK bad sap %ld\n",
				((dl_bind_ack_t *)dl_prim)->dl_sap);
			break;
		}
		freemsg(mp);
		return (0);

	case DL_ERROR_ACK:
		if ((mp->b_wptr-mp->b_rptr) < sizeof (dl_error_ack_t)) {
			printf("dl_bind: DL_ERROR_ACK protocol error\n");
			break;
		}

		error_ack = (dl_error_ack_t *)dl_prim;
		printf("dl_bind: DLPI error %ld\n", error_ack->dl_errno);
		break;

	default:
		printf("dl_bind: bad ACK header %ld\n", dl_prim->dl_primitive);
		break;
	}

	/*
	 * Error return only.
	 */
	freemsg(mp);
	return (-1);
}

/*
 * The network device we will use to boot from is
 * already loaded so all we have to do is determine
 * its name and major device.
 */

static void
init_config(void)
{
	minor_t unit;
	dev_t dev;
	extern	int	prom_devname_from_pathname(char *path, char *buf);
	major_t major;
	char *p;

	bzero(ifname, sizeof (ifname));

	(void) prom_devname_from_pathname(rootfs.bo_name, ifname);

	/*
	 * What we really want is the driver name.  This takes care of booting
	 * from a device named "SUNW,xxx", provided that the xxx driver also
	 * identifies itself by that name.
	 */
	major = ddi_name_to_major(ifname);
	if (major != (major_t) -1)  {
		p = ddi_major_to_name(major);
		(void) strncpy(ifname, p, sizeof (ifname) - 1);
	}

	dev = ddi_pathname_to_dev_t(rootfs.bo_name);
	ndev = (dev_t)getmajor(dev);
#ifdef i386
/*
 * In the new .conf file, we put in multiple entries to support a number
 * of hardware configuration.  The minor number as returned by getminor()
 * below no longer returns the true minor number but rather the entry
 * number in the .conf file.  This will cause subsequent networking code
 * to fail.  Enforce to use minor number equals 0 here.
 */
	unit = 0;
#else
	unit = getminor(dev);
#endif

	if (dldebug)
		printf("init_config: ifname %s, unit %ld\n", ifname, unit);

	/*
	 * Assumes only one linkage array element.
	 */
	CLONE_MAJ = ddi_name_to_major(CLONE);
	dl_netconf.knc_rdev = makedevice(CLONE_MAJ, ddi_name_to_major("udp"));

	if (dldebug)
		printf("init_config: network device major %ld\n", ndev);

	ifname[strlen(ifname)] = '0' + (int)unit;

	/* return (0); */
}
