/*
 *	setkey.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)setkey.c	1.11	94/10/14 SMI"

/*
 * Do the real work of the keyserver.
 * Store secret keys. Compute common keys,
 * and use them to decrypt and encrypt DES keys.
 * Cache the common keys, so the expensive computation is avoided.
 */
#include <stdio.h>
#include <mp.h>
#include <rpc/rpc.h>
#include <rpc/key_prot.h>
#include <rpc/des_crypt.h>
#include <sys/errno.h>
#include <string.h>

extern char *malloc();
extern char ROOTKEY[];

static MINT *MODULUS;
static char *fetchsecretkey();
static keystatus pk_crypt();
static int nodefaultkeys = 0;

/*
 * prohibit the nobody key on this machine k (the -d flag)
 */
pk_nodefaultkeys()
{
	nodefaultkeys = 1;
}

/*
 * Set the modulus for all our Diffie-Hellman operations
 */
setmodulus(modx)
	char *modx;
{
	MODULUS = xtom(modx);
}

/*
 * Set the secretkey key for this uid
 */
keystatus
pk_setkey(uid, skey)
	uid_t uid;
	keybuf skey;
{
	if (!storesecretkey(uid, skey)) {
		return (KEY_SYSTEMERR);
	}
	return (KEY_SUCCESS);
}

/*
 * Encrypt the key using the public key associated with remote_name and the
 * secret key associated with uid.
 */
keystatus
pk_encrypt(uid, remote_name, remote_key, key)
	uid_t uid;
	char *remote_name;
	netobj	*remote_key;
	des_block *key;
{
	return (pk_crypt(uid, remote_name, remote_key, key, DES_ENCRYPT));
}

/*
 * Decrypt the key using the public key associated with remote_name and the
 * secret key associated with uid.
 */
keystatus
pk_decrypt(uid, remote_name, remote_key, key)
	uid_t uid;
	char *remote_name;
	netobj *remote_key;
	des_block *key;
{
	return (pk_crypt(uid, remote_name, remote_key, key, DES_DECRYPT));
}

int store_netname(), fetch_netname();

keystatus
pk_netput(uid, netstore)
	uid_t uid;
	key_netstarg *netstore;
{
	if (!store_netname(uid, netstore)) {
		return (KEY_SYSTEMERR);
	}
	return (KEY_SUCCESS);
}

keystatus
pk_netget(uid, netstore)
	uid_t uid;
	key_netstarg *netstore;
{
	if (!fetch_netname(uid, netstore)) {
		return (KEY_SYSTEMERR);
	}
	return (KEY_SUCCESS);
}


/*
 * Do the work of pk_encrypt && pk_decrypt
 */
static keystatus
pk_crypt(uid, remote_name, remote_key, key, mode)
	uid_t uid;
	char *remote_name;
	netobj *remote_key;
	des_block *key;
	int mode;
{
	char *xsecret;
	char xpublic[1024];
	char xsecret_hold[1024];
	des_block deskey;
	int err;
	MINT *public;
	MINT *secret;
	MINT *common;
	char zero[8];

	xsecret = fetchsecretkey(uid);
	if (xsecret == NULL || xsecret[0] == 0) {
		memset(zero, 0, sizeof (zero));
		xsecret = xsecret_hold;
		if (nodefaultkeys)
			return (KEY_NOSECRET);

		if (!getsecretkey("nobody", xsecret, zero) || xsecret[0] == 0) {
			return (KEY_NOSECRET);
		}
	}
	if (remote_key) {
		memcpy(xpublic, remote_key->n_bytes, remote_key->n_len);
	} else {
		if (!getpublickey(remote_name, xpublic)) {
			if (nodefaultkeys || !getpublickey("nobody", xpublic))
				return (KEY_UNKNOWN);
		}
	}

	if (!readcache(xpublic, xsecret, &deskey)) {
		public = xtom(xpublic);
		secret = xtom(xsecret);
		/* Sanity Check on public and private keys */
		if ((public == NULL) || (secret == NULL))
			return (KEY_SYSTEMERR);

		common = itom(0);
		pow(public, secret, MODULUS, common);
		extractdeskey(common, &deskey);
		writecache(xpublic, xsecret, &deskey);
		mfree(secret);
		mfree(public);
		mfree(common);
	}
	err = ecb_crypt((char *)&deskey, (char *)key, sizeof (des_block),
		DES_HW | mode);
	if (DES_FAILED(err)) {
		return (KEY_SYSTEMERR);
	}
	return (KEY_SUCCESS);
}

keystatus
pk_get_conv_key(uid, xpublic, result)
	uid_t uid;
	keybuf xpublic;
	cryptkeyres *result;
{
	char *xsecret;
	char xsecret_hold[1024];
	MINT *public;
	MINT *secret;
	MINT *common;
	char zero[8];


	xsecret = fetchsecretkey(uid);

	if (xsecret == NULL || xsecret[0] == 0) {
		memset(zero, 0, sizeof (zero));
		xsecret = xsecret_hold;
		if (nodefaultkeys)
			return (KEY_NOSECRET);

		if (!getsecretkey("nobody", xsecret, zero) ||
			xsecret[0] == 0)
			return (KEY_NOSECRET);
	}

	if (!readcache(xpublic, xsecret, &result->cryptkeyres_u.deskey)) {
		public = xtom(xpublic);
		secret = xtom(xsecret);
		/* Sanity Check on public and private keys */
		if ((public == NULL) || (secret == NULL))
			return (KEY_SYSTEMERR);

		common = itom(0);
		pow(public, secret, MODULUS, common);
		extractdeskey(common, &result->cryptkeyres_u.deskey);
		writecache(xpublic, xsecret, &result->cryptkeyres_u.deskey);
		mfree(secret);
		mfree(public);
		mfree(common);
	}

	return (KEY_SUCCESS);
}

/*
 * Choose middle 64 bits of the common key to use as our des key, possibly
 * overwriting the lower order bits by setting parity.
 */
static
extractdeskey(ck, deskey)
	MINT *ck;
	des_block *deskey;
{
	MINT *a;
	short r;
	int i;
	short base = (1 << 8);
	char *k;

	a = itom(0);
	_mp_move(ck, a);
	for (i = 0; i < ((KEYSIZE - 64) / 2) / 8; i++) {
		sdiv(a, base, a, &r);
	}
	k = deskey->c;
	for (i = 0; i < 8; i++) {
		sdiv(a, base, a, &r);
		*k++ = r;
	}
	mfree(a);
	des_setparity((char *)deskey);
}

/*
 * Key storage management
 */

#define	KEY_ONLY 0
#define	KEY_NAME 1
struct secretkey_netname_list {
	uid_t uid;
	key_netstarg keynetdata;
	u_char sc_flag;
	struct secretkey_netname_list *next;
};



static struct secretkey_netname_list *g_secretkey_netname;

/*
 * Store the keys and netname for this uid
 */
static int
store_netname(uid, netstore)
	uid_t uid;
	key_netstarg *netstore;
{
	struct secretkey_netname_list *new;
	struct secretkey_netname_list **l;
	int len;

	for (l = &g_secretkey_netname; *l != NULL && (*l)->uid != uid;
			l = &(*l)->next) {
	}
	if (*l == NULL) {
		new = (struct secretkey_netname_list *)malloc(sizeof (*new));
		if (new == NULL) {
			return (0);
		}
		new->uid = uid;
		new->next = NULL;
		*l = new;
	} else {
		new = *l;
		if (new->keynetdata.st_netname)
			(void) free(new->keynetdata.st_netname);
	}
	memcpy(new->keynetdata.st_priv_key, netstore->st_priv_key,
		HEXKEYBYTES);
	memcpy(new->keynetdata.st_pub_key, netstore->st_pub_key, HEXKEYBYTES);

	if (netstore->st_netname)
		new->keynetdata.st_netname = strdup(netstore->st_netname);
	else
		new->keynetdata.st_netname = (char *)NULL;
	new->sc_flag = KEY_NAME;
	return (1);

}

/*
 * Fetch the keys and netname for this uid
 */

static int fetch_netname(uid, key_netst)
	uid_t uid;
	struct key_netstarg *key_netst;
{
	struct secretkey_netname_list *l;
	int len;

	for (l = g_secretkey_netname; l != NULL; l = l->next) {
		if ((l->uid == uid) && (l->sc_flag == KEY_NAME)) {

			memcpy(key_netst->st_priv_key,
				l->keynetdata.st_priv_key, HEXKEYBYTES);

			memcpy(key_netst->st_pub_key,
				l->keynetdata.st_pub_key, HEXKEYBYTES);

			if (l->keynetdata.st_netname)
				key_netst->st_netname =
					strdup(l->keynetdata.st_netname);
			else
				key_netst->st_netname = NULL;
		return (1);
		}
	}

	return (0);
}

static char *
fetchsecretkey(uid)
	uid_t uid;
{
	struct secretkey_netname_list *l;

	for (l = g_secretkey_netname; l != NULL; l = l->next) {
		if (l->uid == uid) {
			return (l->keynetdata.st_priv_key);
		}
	}
	return (NULL);
}

/*
 * Store the secretkey for this uid
 */
storesecretkey(uid, key)
	uid_t uid;
	keybuf key;
{
	struct secretkey_netname_list *new;
	struct secretkey_netname_list **l;

	for (l = &g_secretkey_netname; *l != NULL && (*l)->uid != uid;
			l = &(*l)->next) {
	}
	if (*l == NULL) {
		if (key[0] == '\0')
			return (0);

		new = (struct secretkey_netname_list *) malloc(sizeof (*new));
		if (new == NULL) {
			return (0);
		}
		new->uid = uid;
		new->sc_flag = KEY_ONLY;
		memset(new->keynetdata.st_pub_key, 0, HEXKEYBYTES);
		new->keynetdata.st_netname = NULL;
		new->next = NULL;
		*l = new;
	} else {
		new = *l;
		if (key[0] == '\0')
			removecache(new->keynetdata.st_priv_key);
	}

	memcpy(new->keynetdata.st_priv_key, key,
		HEXKEYBYTES);
	return (1);
}

static
hexdigit(val)
	int val;
{
	return ("0123456789abcdef"[val]);
}

bin2hex(bin, hex, size)
	unsigned char *bin;
	unsigned char *hex;
	int size;
{
	int i;

	for (i = 0; i < size; i++) {
		*hex++ = hexdigit(*bin >> 4);
		*hex++ = hexdigit(*bin++ & 0xf);
	}
}

static
hexval(dig)
	char dig;
{
	if ('0' <= dig && dig <= '9') {
		return (dig - '0');
	} else if ('a' <= dig && dig <= 'f') {
		return (dig - 'a' + 10);
	} else if ('A' <= dig && dig <= 'F') {
		return (dig - 'A' + 10);
	} else {
		return (-1);
	}
}

hex2bin(hex, bin, size)
	unsigned char *hex;
	unsigned char *bin;
	int size;
{
	int i;

	for (i = 0; i < size; i++) {
		*bin = hexval(*hex++) << 4;
		*bin++ |= hexval(*hex++);
	}
}

/*
 * Exponential caching management
 */
struct cachekey_list {
	keybuf secret;
	keybuf public;
	des_block deskey;
	struct cachekey_list *next;
};
static struct cachekey_list *g_cachedkeys;

/*
 * cache result of expensive multiple precision exponential operation
 */
static
writecache(pub, sec, deskey)
	char *pub;
	char *sec;
	des_block *deskey;
{
	struct cachekey_list *new;

	new = (struct cachekey_list *) malloc(sizeof (struct cachekey_list));
	if (new == NULL) {
		return;
	}
	memcpy(new->public, pub, sizeof (keybuf));
	memcpy(new->secret, sec, sizeof (keybuf));
	new->deskey = *deskey;
	new->next = g_cachedkeys;
	g_cachedkeys = new;
}

#define	cachehit(pub, sec, list)	\
		(memcmp(pub, (list)->public, sizeof (keybuf)) == 0 && \
		memcmp(sec, (list)->secret, sizeof (keybuf)) == 0)

/*
 * Try to find the common key in the cache
 */
static
readcache(pub, sec, deskey)
	char *pub;
	char *sec;
	des_block *deskey;
{
	struct cachekey_list *found;
	register struct cachekey_list **l;

	for (l = &g_cachedkeys; (*l) != NULL && !cachehit(pub, sec, *l);
		l = &(*l)->next)
		;
	if ((*l) == NULL) {
		return (0);
	}
	found = *l;
	(*l) = (*l)->next;
	found->next = g_cachedkeys;
	g_cachedkeys = found;
	*deskey = found->deskey;
	return (1);
}

#define	findsec(sec, list)	\
		(memcmp(sec, (list)->secret, sizeof (keybuf)) == 0)

/*
 * Remove common keys from the cache.
 */
static
removecache(sec)
	char *sec;
{
	struct cachekey_list *found;
	register struct cachekey_list **l;

	for (l = &g_cachedkeys; (*l) != NULL; ) {
		if (findsec(sec, *l)) {
			found = *l;
			*l = (*l)->next;
			memset((char *) found, 0,
					sizeof (struct cachekey_list));
			free(found);
		} else {
			l = &(*l)->next;
		}
	}
	return (1);
}
