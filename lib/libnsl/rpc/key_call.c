/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#ident	"@(#)key_call.c	1.33	95/09/19 SMI"

/*
 * key_call.c, Interface to keyserver
 *
 * setsecretkey(key) - set your secret key
 * encryptsessionkey(agent, deskey) - encrypt a session key to talk to agent
 * decryptsessionkey(agent, deskey) - decrypt ditto
 * gendeskey(deskey) - generate a secure des key
 */

#include "rpc_mt.h"
#include <errno.h>
#include <rpc/rpc.h>
#include <rpc/trace.h>
#include <rpc/key_prot.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <netconfig.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef i386
extern int _uname();
#endif

#if defined(sparc)
#define _FSTAT _fstat
#else  /* !sparc */
#define _FSTAT fstat
#endif /* sparc */

#define	KEY_TIMEOUT	5	/* per-try timeout in seconds */
#define	KEY_NRETRY	12	/* number of retries */

#ifdef DEBUG
#define	debug(msg)	(void) fprintf(stderr, "%s\n", msg);
#else
#define	debug(msg)
#endif /* DEBUG */

int key_call();

/*
 * Hack to allow the keyserver to use AUTH_DES (for authenticated
 * NIS+ calls, for example).  The only functions that get called
 * are key_encryptsession_pk, key_decryptsession_pk, and key_gendes.
 *
 * The approach is to have the keyserver fill in pointers to local
 * implementations of these functions, and to call those in key_call().
 */

cryptkeyres *(*__key_encryptsession_pk_LOCAL)() = 0;
cryptkeyres *(*__key_decryptsession_pk_LOCAL)() = 0;
des_block *(*__key_gendes_LOCAL)() = 0;


key_setsecret(secretkey)
	const char *secretkey;
{
	keystatus status;

	trace1(TR_key_setsecret, 0);
	if (!key_call((u_long) KEY_SET, xdr_keybuf, (char *) secretkey,
			xdr_keystatus, &status)) {
		trace1(TR_key_setsecret, 1);
		return (-1);
	}
	if (status != KEY_SUCCESS) {
		debug("set status is nonzero");
		trace1(TR_key_setsecret, 1);
		return (-1);
	}
	trace1(TR_key_setsecret, 1);
	return (0);
}


/*
 * key_secretkey_is_set() returns 1 if the keyserver has a secret key
 * stored for the caller's effective uid; it returns 0 otherwise
 *
 * N.B.:  The KEY_NET_GET key call is undocumented.  Applications shouldn't
 * be using it, because it allows them to get the user's secret key.
 */
int
key_secretkey_is_set(void)
{
	struct key_netstres 	kres;

	trace1(TR_key_secretkey_is_set, 0);
	memset((void*)&kres, 0, sizeof (kres));
	if (key_call((u_long) KEY_NET_GET, xdr_void, (char *)NULL,
			xdr_key_netstres, (char *) &kres) &&
	    (kres.status == KEY_SUCCESS) &&
	    (kres.key_netstres_u.knet.st_priv_key[0] != 0)) {
		/* avoid leaving secret key in memory */
		memset(kres.key_netstres_u.knet.st_priv_key, 0, HEXKEYBYTES);
		trace1(TR_key_secretkey_is_set, 1);
		return (1);
	}
	trace1(TR_key_secretkey_is_set, 1);
	return (0);
}


key_encryptsession_pk(remotename, remotekey, deskey)
	char *remotename;
	netobj *remotekey;
	des_block *deskey;
{
	cryptkeyarg2 arg;
	cryptkeyres res;

	trace1(TR_key_encryptsession_pk, 0);
	arg.remotename = remotename;
	arg.remotekey = *remotekey;
	arg.deskey = *deskey;
	if (!key_call((u_long)KEY_ENCRYPT_PK, xdr_cryptkeyarg2, (char *)&arg,
			xdr_cryptkeyres, (char *)&res)) {
		trace1(TR_key_encryptsession_pk, 1);
		return (-1);
	}
	if (res.status != KEY_SUCCESS) {
		debug("encrypt status is nonzero");
		trace1(TR_key_encryptsession_pk, 1);
		return (-1);
	}
	*deskey = res.cryptkeyres_u.deskey;
	trace1(TR_key_encryptsession_pk, 1);
	return (0);
}

key_decryptsession_pk(remotename, remotekey, deskey)
	char *remotename;
	netobj *remotekey;
	des_block *deskey;
{
	cryptkeyarg2 arg;
	cryptkeyres res;

	trace1(TR_key_decryptsession_pk, 0);
	arg.remotename = remotename;
	arg.remotekey = *remotekey;
	arg.deskey = *deskey;
	if (!key_call((u_long)KEY_DECRYPT_PK, xdr_cryptkeyarg2, (char *)&arg,
			xdr_cryptkeyres, (char *)&res)) {
		trace1(TR_key_decryptsession_pk, 1);
		return (-1);
	}
	if (res.status != KEY_SUCCESS) {
		debug("decrypt status is nonzero");
		trace1(TR_key_decryptsession_pk, 1);
		return (-1);
	}
	*deskey = res.cryptkeyres_u.deskey;
	trace1(TR_key_decryptsession_pk, 1);
	return (0);
}

key_encryptsession(remotename, deskey)
	const char *remotename;
	des_block *deskey;
{
	cryptkeyarg arg;
	cryptkeyres res;

	trace1(TR_key_encryptsession, 0);
	arg.remotename = (char *) remotename;
	arg.deskey = *deskey;
	if (!key_call((u_long)KEY_ENCRYPT, xdr_cryptkeyarg, (char *)&arg,
			xdr_cryptkeyres, (char *)&res)) {
		trace1(TR_key_encryptsession, 1);
		return (-1);
	}
	if (res.status != KEY_SUCCESS) {
		debug("encrypt status is nonzero");
		trace1(TR_key_encryptsession, 1);
		return (-1);
	}
	*deskey = res.cryptkeyres_u.deskey;
	trace1(TR_key_encryptsession, 1);
	return (0);
}

key_decryptsession(remotename, deskey)
	const char *remotename;
	des_block *deskey;
{
	cryptkeyarg arg;
	cryptkeyres res;

	trace1(TR_key_decryptsession, 0);
	arg.remotename = (char *) remotename;
	arg.deskey = *deskey;
	if (!key_call((u_long)KEY_DECRYPT, xdr_cryptkeyarg, (char *)&arg,
			xdr_cryptkeyres, (char *)&res)) {
		trace1(TR_key_decryptsession, 1);
		return (-1);
	}
	if (res.status != KEY_SUCCESS) {
		debug("decrypt status is nonzero");
		trace1(TR_key_decryptsession, 1);
		return (-1);
	}
	*deskey = res.cryptkeyres_u.deskey;
	trace1(TR_key_decryptsession, 1);
	return (0);
}

key_gendes(key)
	des_block *key;
{
	trace1(TR_key_gendes, 0);
	if (!key_call((u_long)KEY_GEN, xdr_void, (char *)NULL,
			xdr_des_block, (char *)key)) {
		trace1(TR_key_gendes, 1);
		return (-1);
	}
	trace1(TR_key_gendes, 1);
	return (0);
}

key_setnet(arg)
struct key_netstarg *arg;
{
	keystatus status;


	if (!key_call((u_long) KEY_NET_PUT, xdr_key_netstarg, (char *) arg,
		xdr_keystatus, (char *) &status)) {
		return (-1);
	}

	if (status != KEY_SUCCESS) {
		debug("key_setnet status is nonzero");
		return (-1);
	}
	return (1);
}


int
key_get_conv(pkey, deskey)
	char *pkey;
	des_block *deskey;
{
	cryptkeyres res;

	trace1(TR_key_get_conv, 0);
	if (!key_call((u_long) KEY_GET_CONV, xdr_keybuf, pkey,
		xdr_cryptkeyres, (char *)&res)) {
		trace1(TR_key_get_conv, 1);
		return (-1);
	}
	if (res.status != KEY_SUCCESS) {
		debug("get_conv status is nonzero");
		trace1(TR_key_get_conv, 1);
		return (-1);
	}
	*deskey = res.cryptkeyres_u.deskey;
	trace1(TR_key_get_conv, 1);
	return (0);
}

struct  key_call_private {
	CLIENT	*client;	/* Client handle */
	pid_t	pid;		/* process-id at moment of creation */
	uid_t	uid;		/* user-id at last authorization */
	int	fd;		/* client handle fd */
	dev_t	rdev;		/* device client handle is using */
};
static struct key_call_private *key_call_private_main;

static void set_rdev(struct key_call_private *);
static int check_rdev(struct key_call_private *);

static void
key_call_destroy(void *vp)
{
	register struct key_call_private *kcp = (struct key_call_private *)vp;

	if (kcp) {
		if (kcp->client) {
			if (kcp->client->cl_auth)
				auth_destroy(kcp->client->cl_auth);
			clnt_destroy(kcp->client);
		}
		free(kcp);
	}
}

/*
 * Keep the handle cached.  This call may be made quite often.
 */
static CLIENT *
getkeyserv_handle(vers, stale)
int	vers;
int	stale;
{
	void *localhandle;
	struct netconfig *nconf;
	struct netconfig *tpconf;
	struct key_call_private *kcp = (struct key_call_private *) NULL;
	static thread_key_t key_call_key;
	struct timeval wait_time;
	struct utsname u;
	int main_thread;
	int fd;
	extern mutex_t tsd_lock;

#define	TOTAL_TIMEOUT	30	/* total timeout talking to keyserver */
#define	TOTAL_TRIES	5	/* Number of tries */

	trace1(TR_getkeyserv_handle, 0);

	if ((main_thread = _thr_main())) {
		kcp = key_call_private_main;
	} else {
		if (key_call_key == 0) {
			mutex_lock(&tsd_lock);
			if (key_call_key == 0)
				thr_keycreate(&key_call_key, key_call_destroy);
			mutex_unlock(&tsd_lock);
		}
		thr_getspecific(key_call_key, (void **) &kcp);
	}
	if (kcp == (struct key_call_private *)NULL) {
		kcp = (struct key_call_private *)malloc(sizeof (*kcp));
		if (kcp == (struct key_call_private *)NULL) {
			trace2(TR_getkeyserv_handle, 1, clnt);
			syslog(LOG_CRIT, "getkeyserv_handle: out of memory");
			return ((CLIENT *) NULL);
		}
		if (main_thread)
			key_call_private_main = kcp;
		else
			thr_setspecific(key_call_key, (void *) kcp);
		kcp->client = NULL;
	}

	/*
	 * if pid has changed, destroy client and rebuild
	 * or if stale is '1' then destroy client and rebuild
	 */
	if (kcp->client &&
	    (kcp->pid != getpid() || stale || !check_rdev(kcp))) {
		if (kcp->client->cl_auth)
			auth_destroy(kcp->client->cl_auth);
		clnt_destroy(kcp->client);
		kcp->client = NULL;
	}
	if (kcp->client) {
		/* if uid has changed, build client handle again */
		if (kcp->uid != geteuid()) {
			kcp->uid = geteuid();
			auth_destroy(kcp->client->cl_auth);
			kcp->client->cl_auth =
				authsys_create("", kcp->uid, 0, 0, NULL);
			if (kcp->client->cl_auth == NULL) {
				clnt_destroy(kcp->client);
				kcp->client = NULL;
				trace2(TR_getkeyserv_handle, 1, kcp->client);
				return ((CLIENT *) NULL);
			}
		}
		/* Change the version number to the new one */
		clnt_control(kcp->client, CLSET_VERS, (void *)&vers);
		trace2(TR_getkeyserv_handle, 1, kcp->client);
		return (kcp->client);
	}
	if (!(localhandle = setnetconfig())) {
		trace2(TR_getkeyserv_handle, 1, kcp->client);
		return ((CLIENT *) NULL);
	}
	tpconf = NULL;

#if defined(i386)
	if (_nuname(&u) == -1)
#elif defined(sparc)
	if (_uname(&u) == -1)
#else
#error Unknown architecture!
#endif
	{
		endnetconfig(localhandle);
		trace2(TR_getkeyserv_handle, 1, kcp->client);
		return ((CLIENT *) NULL);
	}

	while (nconf = getnetconfig(localhandle)) {
		if (strcmp(nconf->nc_protofmly, NC_LOOPBACK) == 0) {
			if (nconf->nc_semantics == NC_TPI_CLTS) {
				kcp->client = clnt_tp_create(u.nodename,
					KEY_PROG, vers, nconf);
				if (kcp->client)
					break;
			} else {
				tpconf = nconf;
			}
		}
	}
	if ((kcp->client == (CLIENT *) NULL) && (tpconf))
		/* Now, try the COTS loopback transport */
		kcp->client = clnt_tp_create(u.nodename,
			KEY_PROG, vers, tpconf);
	endnetconfig(localhandle);

	if (kcp->client == (CLIENT *) NULL) {
		trace2(TR_getkeyserv_handle, 1, kcp->client);
		return ((CLIENT *) NULL);
	}
	kcp->uid = geteuid();
	kcp->pid = getpid();
	set_rdev(kcp);
	kcp->client->cl_auth = authsys_create("", kcp->uid, 0, 0, NULL);
	if (kcp->client->cl_auth == NULL) {
		clnt_destroy(kcp->client);
		kcp->client = NULL;
		trace2(TR_getkeyserv_handle, 1, kcp->client);
		return ((CLIENT *) NULL);
	}
	wait_time.tv_sec = TOTAL_TIMEOUT/TOTAL_TRIES;
	wait_time.tv_usec = 0;
	(void) clnt_control(kcp->client, CLSET_RETRY_TIMEOUT,
		(char *)&wait_time);
	if (clnt_control(kcp->client, CLGET_FD, (char *)&fd))
		fcntl(fd, F_SETFD, 1);	/* make it "close on exec" */

	trace2(TR_getkeyserv_handle, 1, kcp->client);
	return (kcp->client);
}

/* returns  0 on failure, 1 on success */

key_call(proc, xdr_arg, arg, xdr_rslt, rslt)
	u_long proc;
	xdrproc_t xdr_arg;
	char *arg;
	xdrproc_t xdr_rslt;
	char *rslt;
{
	CLIENT	*clnt;
	struct	timeval wait_time;
	enum	clnt_stat	status;
	int	vers;

	if (proc == KEY_ENCRYPT_PK && __key_encryptsession_pk_LOCAL) {
		cryptkeyres *res;
		res = (*__key_encryptsession_pk_LOCAL)(geteuid(), arg);
		*(cryptkeyres*)rslt = *res;
		return (1);
	} else if (proc == KEY_DECRYPT_PK && __key_decryptsession_pk_LOCAL) {
		cryptkeyres *res;
		res = (*__key_decryptsession_pk_LOCAL)(geteuid(), arg);
		*(cryptkeyres*)rslt = *res;
		return (1);
	} else if (proc == KEY_GEN && __key_gendes_LOCAL) {
		des_block *res;
		res = (*__key_gendes_LOCAL)(geteuid(), 0);
		*(des_block*)rslt = *res;
		return (1);
	}

	trace2(TR_key_call, 0, proc);

	if ((proc == KEY_ENCRYPT_PK) || (proc == KEY_DECRYPT_PK) ||
	    (proc == KEY_NET_GET) || (proc == KEY_NET_PUT) ||
	    (proc == KEY_GET_CONV))
		vers = 2;	/* talk to version 2 */
	else
		vers = 1;	/* talk to version 1 */

	clnt = getkeyserv_handle(vers, 0);
	if (clnt == NULL) {
		trace3(TR_key_call, 1, proc, clnt);
		return (0);
	}

	wait_time.tv_sec = TOTAL_TIMEOUT;
	wait_time.tv_usec = 0;

	status = CLNT_CALL(clnt, proc, xdr_arg, arg, xdr_rslt,
			rslt, wait_time);

	switch (status) {
	case RPC_SUCCESS:
		trace3(TR_key_call, 1, proc, clnt);
		return (1);

	case RPC_CANTRECV:	/* probably keyserv was restarted */
	case RPC_TIMEDOUT:
		clnt = getkeyserv_handle(vers, 1);
		if (clnt == NULL) {
			trace3(TR_key_call, 1, proc, clnt);
			return (0);
		}
		if (CLNT_CALL(clnt, proc, xdr_arg, arg, xdr_rslt, rslt,
			wait_time) == RPC_SUCCESS) {

			trace3(TR_key_call, 1, proc, clnt);
			return (1);
		} else {
			trace3(TR_key_call, 1, proc, clnt);
			return (0);
		}
	default:
		trace3(TR_key_call, 1, proc, clnt);
		return (0);
	}
}

static
void
set_rdev(kcp)
	struct key_call_private *kcp;
{
	int fd;
	int st;
	struct stat stbuf;

	if (clnt_control(kcp->client, CLGET_FD, (char *)&fd) != TRUE ||
	    _FSTAT(fd, &stbuf) == -1) {
		syslog(LOG_DEBUG, "keyserv_client:  can't get info");
		kcp->fd = -1;
		return;
	}
	kcp->fd = fd;
	kcp->rdev = stbuf.st_rdev;
}

static
int
check_rdev(kcp)
	struct key_call_private *kcp;
{
	struct stat stbuf;

	if (kcp->fd == -1)
		return (1);    /* can't check it, assume it is okay */

	if (_FSTAT(kcp->fd, &stbuf) == -1) {
		syslog(LOG_DEBUG, "keyserv_client:  can't stat %d", kcp->fd);
		/* could be because file descriptor was closed */
		return (0);
	}
	if (kcp->rdev != stbuf.st_rdev) {
		syslog(LOG_DEBUG,
		    "keyserv_client:  fd %d changed, old=0x%x, new=0x%x",
		    kcp->fd, kcp->rdev, stbuf.st_rdev);
		/* it's not our file descriptor, so don't try to close it */
		clnt_control(kcp->client, CLSET_FD_NCLOSE, (char *)NULL);
		return (0);
	}
	return (1);    /* fd is okay */
}
