/*
 *	keyserv.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)keyserv.c	1.19	94/09/26 SMI"

/*
 * Keyserver
 * Store secret keys per uid. Do public key encryption and decryption
 * operations. Generate "random" keys.
 * Do not talk to anything but a local root
 * process on the local transport only
 */

#include <stdio.h>
#include <rpc/rpc.h>
#include <sys/param.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <pwd.h>
#include <rpc/des_crypt.h>
#include <rpc/key_prot.h>

#ifdef KEYSERV_RANDOM
extern long random();
#endif
#ifndef NGROUPS
#define	NGROUPS 16
#endif

extern keystatus pk_setkey();
extern keystatus pk_encrypt();
extern keystatus pk_decrypt();
extern keystatus pk_netput();
extern keystatus pk_netget();
extern keystatus pk_get_conv_key();
static void randomize();
static void usage();
static int getrootkey();
static int root_auth();

#ifdef DEBUG
static int debugging = 1;
#else
static int debugging = 0;
#endif

static void keyprogram();
static des_block masterkey;
char *getenv();
static char ROOTKEY[] = "/etc/.rootkey";

/*
 * Hack to allow the keyserver to use AUTH_DES (for authenticated
 * NIS+ calls, for example).  The only functions that get called
 * are key_encryptsession_pk, key_decryptsession_pk, and key_gendes.
 *
 * The approach is to have the keyserver fill in pointers to local
 * implementations of these functions, and to call those in key_call().
 */

cryptkeyres *__key_encrypt_pk_2_svc();
cryptkeyres *__key_decrypt_pk_2_svc();
des_block *__key_gen_1_svc();

extern cryptkeyres *(*__key_encryptsession_pk_LOCAL)();
extern cryptkeyres *(*__key_decryptsession_pk_LOCAL)();
extern des_block *(*__key_gendes_LOCAL)();

main(argc, argv)
	int argc;
	char *argv[];
{
	int nflag = 0;
	extern char *optarg;
	extern int optind;
	int c;
	struct rlimit rl;

	/*
	 * Set our allowed number of file descriptors to the max
	 * of what the system will allow, limited by FD_SETSIZE.
	 */
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		rlim_t limit;

		if ((limit = rl.rlim_max) > FD_SETSIZE)
			limit = FD_SETSIZE;
		rl.rlim_cur = limit;
		(void) setrlimit(RLIMIT_NOFILE, &rl);
	}

	__key_encryptsession_pk_LOCAL = &__key_encrypt_pk_2_svc;
	__key_decryptsession_pk_LOCAL = &__key_decrypt_pk_2_svc;
	__key_gendes_LOCAL = &__key_gen_1_svc;

	while ((c = getopt(argc, argv, "ndD")) != -1)
		switch (c) {
		case 'n':
			nflag++;
			break;
		case 'd':
			pk_nodefaultkeys();
			break;
		case 'D':
			debugging = 1;
			break;
		default:
			usage();
		}

	if (optind != argc) {
		usage();
	}

	/*
	 * Initialize
	 */
	(void) umask(066);	/* paranoia */
	if (geteuid() != 0) {
		(void) fprintf(stderr, "%s must be run as root\n", argv[0]);
		exit(1);
	}
	setmodulus(HEXMODULUS);
	getrootkey(&masterkey, nflag);

	if (svc_create_local_service(keyprogram, KEY_PROG, KEY_VERS,
		"netpath", "keyserv") == 0) {
		(void) fprintf(stderr,
			"%s: unable to create service\n", argv[0]);
		exit(1);
	}

	if (svc_create_local_service(keyprogram, KEY_PROG, KEY_VERS2,
		"netpath", "keyserv") == 0) {
		(void) fprintf(stderr,
			"%s: unable to create service\n", argv[0]);
		exit(1);
	}
	if (!debugging) {
		detachfromtty();
	}
	svc_run();
	abort();
	/* NOTREACHED */
}

/*
 * In the event that we don't get a root password, we try to
 * randomize the master key the best we can
 */
static void
randomize(master)
	des_block *master;
{
	int i;
	int seed;
	struct timeval tv;
	int shift;

	seed = 0;
	for (i = 0; i < 1024; i++) {
		(void) gettimeofday(&tv, (struct timezone *) NULL);
		shift = i % 8 * sizeof (int);
		seed ^= (tv.tv_usec << shift) | (tv.tv_usec >> (32 - shift));
	}
#ifdef KEYSERV_RANDOM
	srandom(seed);
	master->key.low = random();
	master->key.high = random();
	srandom(seed);
#else
	/* use stupid dangerous bad rand() */
	srand(seed);
	master->key.low = rand();
	master->key.high = rand();
	srand(seed);
#endif
}

/*
 * Try to get root's secret key, by prompting if terminal is a tty, else trying
 * from standard input.
 * Returns 1 on success.
 */
static
getrootkey(master, prompt)
	des_block *master;
	int prompt;
{
	char *getpass();
	char *passwd;
	char name[MAXNETNAMELEN + 1];
	char secret[HEXKEYBYTES + 1];
	key_netstarg netstore;
	char *crypt();
	char *strrchr();
	int fd;

	if (!prompt) {
		/*
		 * Read secret key out of ROOTKEY
		 */
		fd = open(ROOTKEY, O_RDONLY, 0);
		if (fd < 0) {
			randomize(master);
			return (0);
		}
		if (read(fd, secret, HEXKEYBYTES) < HEXKEYBYTES) {
			(void) fprintf(stderr,
			    "keyserv: the key read from %s was too short.\n",
			    ROOTKEY);
			(void) close(fd);
			return (0);
		}
		(void) close(fd);
		if (!getnetname(name)) {
		    (void) fprintf(stderr, "keyserv: \
failed to generate host's netname when establishing root's key.\n");
		    return (0);
		}
		memcpy(netstore.st_priv_key, secret, HEXKEYBYTES);
		memset(netstore.st_pub_key, 0, HEXKEYBYTES);
		netstore.st_netname = name;
		if (pk_netput(0, &netstore) != KEY_SUCCESS) {
		    (void) fprintf(stderr,
			"keyserv: could not set root's key and netname.\n");
		    return (0);
		}
		return (1);
	}
	/*
	 * Decrypt yellow pages publickey entry to get secret key
	 */
	passwd = getpass("root password:");
	passwd2des(passwd, master);
	getnetname(name);
	if (!getsecretkey(name, secret, passwd)) {
		(void) fprintf(stderr,
		"Can't find %s's secret key\n", name);
		return (0);
	}
	if (secret[0] == 0) {
		(void) fprintf(stderr,
	"Password does not decrypt secret key for %s\n", name);
		return (0);
	}
	(void) pk_setkey(0, secret);
	/*
	 * Store it for future use in $ROOTKEY, if possible
	 */
	fd = open(ROOTKEY, O_WRONLY|O_TRUNC|O_CREAT, 0);
	if (fd > 0) {
		char newline = '\n';

		write(fd, secret, strlen(secret));
		write(fd, &newline, sizeof (newline));
		close(fd);
	}
	return (1);
}

/*
 * Procedures to implement RPC service.  These procedures are named
 * differently from the definitions in key_prot.h (generated by rpcgen)
 * because they take different arguments.
 */
char *
strstatus(status)
	keystatus status;
{
	switch (status) {
	case KEY_SUCCESS:
		return ("KEY_SUCCESS");
	case KEY_NOSECRET:
		return ("KEY_NOSECRET");
	case KEY_UNKNOWN:
		return ("KEY_UNKNOWN");
	case KEY_SYSTEMERR:
		return ("KEY_SYSTEMERR");
	default:
		return ("(bad result code)");
	}
}

keystatus *
__key_set_1_svc(uid, key)
	uid_t uid;
	keybuf key;
{
	static keystatus status;

	if (debugging) {
		(void) fprintf(stderr, "set(%d, %.*s) = ", uid,
				sizeof (keybuf), key);
	}
	status = pk_setkey(uid, key);
	if (debugging) {
		(void) fprintf(stderr, "%s\n", strstatus(status));
		(void) fflush(stderr);
	}
	return (&status);
}

cryptkeyres *
__key_encrypt_pk_2_svc(uid, arg)
	uid_t uid;
	cryptkeyarg2 *arg;
{
	static cryptkeyres res;

	if (debugging) {
		(void) fprintf(stderr, "encrypt(%d, %s, %08x%08x) = ", uid,
				arg->remotename, arg->deskey.key.high,
				arg->deskey.key.low);
	}
	res.cryptkeyres_u.deskey = arg->deskey;
	res.status = pk_encrypt(uid, arg->remotename, &(arg->remotekey),
				&res.cryptkeyres_u.deskey);
	if (debugging) {
		if (res.status == KEY_SUCCESS) {
			(void) fprintf(stderr, "%08x%08x\n",
					res.cryptkeyres_u.deskey.key.high,
					res.cryptkeyres_u.deskey.key.low);
		} else {
			(void) fprintf(stderr, "%s\n", strstatus(res.status));
		}
		(void) fflush(stderr);
	}
	return (&res);
}

cryptkeyres *
__key_decrypt_pk_2_svc(uid, arg)
	uid_t uid;
	cryptkeyarg2 *arg;
{
	static cryptkeyres res;

	if (debugging) {
		(void) fprintf(stderr, "decrypt(%d, %s, %08x%08x) = ", uid,
				arg->remotename, arg->deskey.key.high,
				arg->deskey.key.low);
	}
	res.cryptkeyres_u.deskey = arg->deskey;
	res.status = pk_decrypt(uid, arg->remotename, &(arg->remotekey),
				&res.cryptkeyres_u.deskey);
	if (debugging) {
		if (res.status == KEY_SUCCESS) {
			(void) fprintf(stderr, "%08x%08x\n",
					res.cryptkeyres_u.deskey.key.high,
					res.cryptkeyres_u.deskey.key.low);
		} else {
			(void) fprintf(stderr, "%s\n", strstatus(res.status));
		}
		(void) fflush(stderr);
	}
	return (&res);
}

keystatus *
__key_net_put_2_svc(uid, arg)
	uid_t uid;
	key_netstarg *arg;
{
	static keystatus status;

	if (debugging) {
		(void) fprintf(stderr, "net_put(%s, %.*s, %.*s) = ",
			arg->st_netname, sizeof (arg->st_pub_key),
			arg->st_pub_key, sizeof (arg->st_priv_key),
			arg->st_priv_key);
	};

	status = pk_netput(uid, arg);

	if (debugging) {
		(void) fprintf(stderr, "%s\n", strstatus(status));
		(void) fflush(stderr);
	}

	return (&status);
}

key_netstres *
__key_net_get_2_svc(uid, arg)
	uid_t uid;
	void *arg;
{
	static key_netstres keynetname;

	if (debugging)
		(void) fprintf(stderr, "net_get(%d) = ", uid);

	keynetname.status = pk_netget(uid, &keynetname.key_netstres_u.knet);
	if (debugging) {
		if (keynetname.status == KEY_SUCCESS) {
			fprintf(stderr, "<%s, %.*s, %.*s>\n",
			keynetname.key_netstres_u.knet.st_netname,
			sizeof (keynetname.key_netstres_u.knet.st_pub_key),
			keynetname.key_netstres_u.knet.st_pub_key,
			sizeof (keynetname.key_netstres_u.knet.st_priv_key),
			keynetname.key_netstres_u.knet.st_priv_key);
		} else {
			(void) fprintf(stderr, "NOT FOUND\n");
		}
		(void) fflush(stderr);
	}

	return (&keynetname);

}

cryptkeyres *
__key_get_conv_2_svc(uid, arg)
	uid_t uid;
	keybuf arg;
{
	static cryptkeyres  res;

	if (debugging)
		(void) fprintf(stderr, "get_conv(%d, %.*s) = ", uid,
			sizeof (arg), arg);


	res.status = pk_get_conv_key(uid, arg, &res);

	if (debugging) {
		if (res.status == KEY_SUCCESS) {
			(void) fprintf(stderr, "%08x%08x\n",
				res.cryptkeyres_u.deskey.key.high,
				res.cryptkeyres_u.deskey.key.low);
		} else {
			(void) fprintf(stderr, "%s\n", strstatus(res.status));
		}
		(void) fflush(stderr);
	}
	return (&res);
}


cryptkeyres *
__key_encrypt_1_svc(uid, arg)
	uid_t uid;
	cryptkeyarg *arg;
{
	static cryptkeyres res;

	if (debugging) {
		(void) fprintf(stderr, "encrypt(%d, %s, %08x%08x) = ", uid,
				arg->remotename, arg->deskey.key.high,
				arg->deskey.key.low);
	}
	res.cryptkeyres_u.deskey = arg->deskey;
	res.status = pk_encrypt(uid, arg->remotename, NULL,
				&res.cryptkeyres_u.deskey);
	if (debugging) {
		if (res.status == KEY_SUCCESS) {
			(void) fprintf(stderr, "%08x%08x\n",
					res.cryptkeyres_u.deskey.key.high,
					res.cryptkeyres_u.deskey.key.low);
		} else {
			(void) fprintf(stderr, "%s\n", strstatus(res.status));
		}
		(void) fflush(stderr);
	}
	return (&res);
}

cryptkeyres *
__key_decrypt_1_svc(uid, arg)
	uid_t uid;
	cryptkeyarg *arg;
{
	static cryptkeyres res;

	if (debugging) {
		(void) fprintf(stderr, "decrypt(%d, %s, %08x%08x) = ", uid,
				arg->remotename, arg->deskey.key.high,
				arg->deskey.key.low);
	}
	res.cryptkeyres_u.deskey = arg->deskey;
	res.status = pk_decrypt(uid, arg->remotename, NULL,
				&res.cryptkeyres_u.deskey);
	if (debugging) {
		if (res.status == KEY_SUCCESS) {
			(void) fprintf(stderr, "%08x%08x\n",
					res.cryptkeyres_u.deskey.key.high,
					res.cryptkeyres_u.deskey.key.low);
		} else {
			(void) fprintf(stderr, "%s\n", strstatus(res.status));
		}
		(void) fflush(stderr);
	}
	return (&res);
}

/* ARGSUSED */
des_block *
__key_gen_1_svc(v, s)
	void	*v;
	struct svc_req	*s;
{
	struct timeval time;
	static des_block keygen;
	static des_block key;

	(void) gettimeofday(&time, (struct timezone *) NULL);
	keygen.key.high += (time.tv_sec ^ time.tv_usec);
	keygen.key.low += (time.tv_sec ^ time.tv_usec);
	ecb_crypt((char *)&masterkey, (char *)&keygen, sizeof (keygen),
		DES_ENCRYPT | DES_HW);
	key = keygen;
	des_setparity((char *)&key);
	if (debugging) {
		(void) fprintf(stderr, "gen() = %08x%08x\n", key.key.high,
					key.key.low);
		(void) fflush(stderr);
	}
	return (&key);
}

getcredres *
__key_getcred_1_svc(uid, name)
	uid_t uid;
	netnamestr *name;
{
	static getcredres res;
	static u_int gids[NGROUPS];
	struct unixcred *cred;

	cred = &res.getcredres_u.cred;
	cred->gids.gids_val = gids;
	if (!netname2user(*name, (uid_t *) &cred->uid, (gid_t *) &cred->gid,
			(int *)&cred->gids.gids_len, (gid_t *)gids)) {
		res.status = KEY_UNKNOWN;
	} else {
		res.status = KEY_SUCCESS;
	}
	if (debugging) {
		(void) fprintf(stderr, "getcred(%s) = ", *name);
		if (res.status == KEY_SUCCESS) {
			(void) fprintf(stderr, "uid=%d, gid=%d, grouplen=%d\n",
				cred->uid, cred->gid, cred->gids.gids_len);
		} else {
			(void) fprintf(stderr, "%s\n", strstatus(res.status));
		}
		(void) fflush(stderr);
	}
	return (&res);
}

/*
 * RPC boilerplate
 */
static void
keyprogram(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	union {
		keybuf key_set_1_arg;
		cryptkeyarg key_encrypt_1_arg;
		cryptkeyarg key_decrypt_1_arg;
		netnamestr key_getcred_1_arg;
		cryptkeyarg key_encrypt_2_arg;
		cryptkeyarg key_decrypt_2_arg;
		netnamestr key_getcred_2_arg;
		cryptkeyarg2 key_encrypt_pk_2_arg;
		cryptkeyarg2 key_decrypt_pk_2_arg;
		key_netstarg key_net_put_2_arg;
		netobj  key_get_conv_2_arg;
	} argument;
	char *result;
	bool_t(*xdr_argument)(), (*xdr_result)();
	char *(*local) ();
	uid_t uid;
	int check_auth;
	int need_free = 0;

	switch (rqstp->rq_proc) {
	case NULLPROC:
		svc_sendreply(transp, xdr_void, (char *)NULL);
		return;

	case KEY_SET:
		xdr_argument = xdr_keybuf;
		xdr_result = xdr_int;
		local = (char *(*)()) __key_set_1_svc;
		check_auth = 1;
		break;

	case KEY_ENCRYPT:
		xdr_argument = xdr_cryptkeyarg;
		xdr_result = xdr_cryptkeyres;
		local = (char *(*)()) __key_encrypt_1_svc;
		check_auth = 1;
		break;

	case KEY_DECRYPT:
		xdr_argument = xdr_cryptkeyarg;
		xdr_result = xdr_cryptkeyres;
		local = (char *(*)()) __key_decrypt_1_svc;
		check_auth = 1;
		break;

	case KEY_GEN:
		xdr_argument = xdr_void;
		xdr_result = xdr_des_block;
		local = (char *(*)()) __key_gen_1_svc;
		check_auth = 0;
		break;

	case KEY_GETCRED:
		xdr_argument = xdr_netnamestr;
		xdr_result = xdr_getcredres;
		local = (char *(*)()) __key_getcred_1_svc;
		check_auth = 0;
		break;

	case KEY_ENCRYPT_PK:
		xdr_argument = xdr_cryptkeyarg2;
		xdr_result = xdr_cryptkeyres;
		local = (char *(*)()) __key_encrypt_pk_2_svc;
		check_auth = 1;
		break;

	case KEY_DECRYPT_PK:
		xdr_argument = xdr_cryptkeyarg2;
		xdr_result = xdr_cryptkeyres;
		local = (char *(*)()) __key_decrypt_pk_2_svc;
		check_auth = 1;
		break;


	case KEY_NET_PUT:
		xdr_argument = xdr_key_netstarg;
		xdr_result = xdr_keystatus;
		local = (char *(*)()) __key_net_put_2_svc;
		check_auth = 1;
		break;

	case KEY_NET_GET:
		xdr_argument = (xdrproc_t) xdr_void;
		xdr_result = xdr_key_netstres;
		local = (char *(*)()) __key_net_get_2_svc;
		need_free = 1;		/* need to free netname */
		check_auth = 1;
		break;

	case KEY_GET_CONV:
		xdr_argument = (xdrproc_t) xdr_keybuf;
		xdr_result = xdr_cryptkeyres;
		local = (char *(*)()) __key_get_conv_2_svc;
		check_auth = 1;
		break;

	default:
		svcerr_noproc(transp);
		return;
	}
	if (check_auth) {
		if (root_auth(transp, rqstp) == 0) {
			if (debugging) {
				(void) fprintf(stderr,
				"not local privileged process\n");
			}
			svcerr_weakauth(transp);
			return;
		}
		if (rqstp->rq_cred.oa_flavor != AUTH_SYS) {
			if (debugging) {
				(void) fprintf(stderr,
				"not unix authentication\n");
			}
			svcerr_weakauth(transp);
			return;
		}
		uid = ((struct authsys_parms *)rqstp->rq_clntcred)->aup_uid;
	}

	memset((char *) &argument, 0, sizeof (argument));
	if (!svc_getargs(transp, xdr_argument, (caddr_t)&argument)) {
		svcerr_decode(transp);
		return;
	}
	result = (*local) (uid, &argument);
	if (!svc_sendreply(transp, xdr_result, (char *) result)) {
		if (debugging)
			(void) fprintf(stderr, "unable to reply\n");
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_argument, (caddr_t)&argument)) {
		if (debugging)
			(void) fprintf(stderr,
			"unable to free arguments\n");
		exit(1);
	}
	if (need_free) {	/* free the strdup'd netname */
		key_netstres *tmp = (key_netstres *) result;

		if ((tmp != NULL) &&
			(tmp->key_netstres_u.knet.st_netname != NULL)) {

			free(tmp->key_netstres_u.knet.st_netname);
			tmp->key_netstres_u.knet.st_netname = NULL;
		}
	}
}

static int
root_auth(trans, rqstp)
	SVCXPRT *trans;
	struct svc_req *rqstp;
{
	uid_t uid;

	if (__rpc_get_local_uid(trans, &uid) < 0) {
		if (debugging)
			fprintf(stderr, "__rpc_get_local_uid failed %s %s\n",
				trans->xp_netid, trans->xp_tp);
		return (0);
	}
	if (debugging)
		fprintf(stderr, "local_uid  %d\n", uid);
	if (uid == 0)
		return (1);
	if (rqstp->rq_cred.oa_flavor == AUTH_SYS) {
		if (((uid_t) ((struct authunix_parms *)
			rqstp->rq_clntcred)->aup_uid)
			== uid) {
			return (1);
		} else {
			if (debugging)
				fprintf(stderr,
			"local_uid  %d mismatches auth %d\n", uid,
((uid_t) ((struct authunix_parms *)rqstp->rq_clntcred)->aup_uid));
			return (0);
		}
	} else {
		if (debugging)
			fprintf(stderr, "Not auth sys\n");
		return (0);
	}
}

static void
usage()
{
	(void) fprintf(stderr, "usage: keyserv [-n] [-D] [-d]\n");
	(void) fprintf(stderr, "-d disables the use of default keys\n");
	exit(1);
}
