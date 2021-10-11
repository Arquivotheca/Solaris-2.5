/*
 *	util.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)util.cc	1.14	94/11/07 SMI"

/* 
 * Ported from SCCS Version : 
 *	"@(#)util.cc  1.15  91/03/19  Copyr 1988 Sun Micro";
 */

/* NisSharedCache  class  functions */

#include "../gen/nis_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

#ifdef TDRPC
#include <des_crypt.h>
#include  <sys/socket.h>
#include  <arpa/inet.h>
#include  <netinet/in.h>
#else
#include <rpc/des_crypt.h>
#endif


#include <rpcsvc/nis.h>
#include <rpcsvc/nis_cache.h>
#include "client.h"
#include "md5.h"


#define nilstring(s)        ((s) ? (s) : "(nil)")


CLIENT* 
NisSharedCache :: getCacheHandle()
{

	if (lCache != NULL)  {
		/* the local cache is valid and the shared cache is invalid */
		return (NULL);
	}
	struct timeval timeout;

#ifdef TDRPC
	/*
	 * Before this code is reactivated, it must be made MT-safe.
	 * The alternate code, below, has been made MT-safe by using
	 * the 'pid' member of the class.   raf - 23 Sep 93
	 */
	struct sockaddr_in addr;
	static oldport = 0;
	int len = sizeof (struct sockaddr_in);
	int sock = RPC_ANYSOCK;
	unsigned long a1, a2, a3, a4, p1, p2;
	u_short port;

	/* get the port number */
	sscanf(uaddr, "%d.%d.%d.%d.%d.%d", &a1, &a2, &a3, &a4, &p1, &p2);
	port = (u_short) ((p1 << 8) | p2);
	if(oldport == port && cacheclnt) 
		return (cacheclnt);

	if (cacheclnt)
		clnt_destroy(cacheclnt);
	memset( (void*)addr.sin_zero, 0, 8 );
	addr.sin_addr.s_addr = INADDR_LOOPBACK;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	timeout.tv_sec = 25;
	timeout.tv_usec = 0;
	
	cacheclnt = clntudp_create(&addr, CACHEPROG, CACHE_VER_1, 
				   timeout, &sock );
	if (cacheclnt == NULL) 
		return (NULL);
	oldport = port;

#else 
	/* this is being done in a separate function call in a .c file 
         * because of the inability at the
	 * present time to get a consistent set of TIRPC and C++ header files
	 */
	CLIENT *tmp_clnt;
	mutex_lock(&cacheclnt_lock);
	cacheclnt = __get_ti_clnt(uaddr, cacheclnt, &last_uaddr, &pid, &rdev);
	if (!cacheclnt) {
		mutex_unlock(&cacheclnt_lock);
		return (NULL);
	}

#endif
	// set timeout for clnt_calls to 0 - to achieve message passing semantics 
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	
	clnt_control(cacheclnt, CLSET_TIMEOUT, (char *)&timeout);
	tmp_clnt = cacheclnt;
	mutex_unlock(&cacheclnt_lock);
	return (tmp_clnt);
}







/* compares the two directory objects to see if they have exactly
 * the same information.
 * This routine is used when the cachemgr gets a request to delete a
 * directory object. As the dir_obj could have been updated by another
 * client in the meantime, the cachemgr checks to make sure it is deleting the
 * old dir_obj.
 * it compares all fields except the TTL - because this could vary slightly
 * as each client subtracts the current timestamp  from the one in the
 * cache to figure out the TTL.
 *
 * returns TRUE if o1 and o2 are the same, FALSE otherwise
 */


static bool_t
nis_servers_equal(nis_server *s1, nis_server *s2)
{
	int n1, n2;
	int i;
	endpoint *e1, *e2;
	
	if ( strcmp(s1->name, s2->name) ||
	    s1->key_type != s2->key_type ||
	    s1->pkey.n_len != s2->pkey.n_len ||
	    memcmp(s1->pkey.n_bytes, s2->pkey.n_bytes, s1->pkey.n_len) )
		return (FALSE);
	
	n1 = s1->ep.ep_len;
	n2 = s2->ep.ep_len;
	if (n1 != n2)
		return (FALSE);
	for (i = 0; i < n1; i++) {
		e1 = &s1->ep.ep_val[i];
		e2 = &s2->ep.ep_val[i];
		
		if (strcmp(e1->uaddr, e2->uaddr) ||
		    strcmp(e1->family, e2->family) ||
		    strcmp(e1->proto, e2->proto))
			return (FALSE);
	}

	return (TRUE);
}




bool_t
__nis_directory_objs_equal( directory_obj *o1, directory_obj *o2)
{
	int n1, n2;
	int i;
	oar_mask *m1, *m2;

	if ( strcmp(o1->do_name, o2->do_name) ||
	    o1->do_type != o2->do_type )
		return (FALSE);
	
	n1 = o1->do_servers.do_servers_len;
	n2 = o2->do_servers.do_servers_len;
	if (n1 != n2)
		return (FALSE);
	for (i = 0; i < n1; i++) {
		
		if (nis_servers_equal(&o1->do_servers.do_servers_val[i],
				      &o2->do_servers.do_servers_val[i]) == FALSE)
			return (FALSE);
	}

	n1 = o1->do_armask.do_armask_len;
	n2 = o2->do_armask.do_armask_len;
	if (n1 != n2)
		return (FALSE);
	for (i = 0; i < n1; i++) {
		m1 = &o1->do_armask.do_armask_val[i];
		m2 = &o2->do_armask.do_armask_val[i];
		if (m1->oa_rights != m2->oa_rights ||
		    m1->oa_otype != m2->oa_otype)
			return (FALSE);
	}

	return (TRUE);
}



/* only used by nisshowcache in special mode - for testing */
void
__nis_print_directory_special( directory_obj *r)
{
	int		i;

	printf("'%s':", nilstring(r->do_name));
	switch (r->do_type) {
		case NIS :
			printf("NIS:");
			break;
		case SUNYP : 
			printf("YP:");
			break;
		case DNS :
			printf("DNS:");
			break;
		default :
			printf("%d:", r->do_type);
	}
	printf("\"%d:%d:%d\"", r->do_ttl/3600, 
	       (r->do_ttl - (r->do_ttl/3600)*3600)/60,
	       (r->do_ttl % 60));
	for (i=0; i< r->do_servers.do_servers_len; i++) {
		if (i == 0 )
			printf(":");
		else
			printf(",");
		printf("%s", nilstring(r->do_servers.do_servers_val[i].name));
	}
	printf("\n");
}



/* returns the digest in 'digest'
 * allocates memory for digest.
 * This should be freed by the caller
 */

static void
__nis_calculate_cksum( unsigned int datalen, unsigned char *data, 
		   unsigned int *digestlen, unsigned char **digest)
{
	
	MD5_CTX mdContext;

	MD5Init (&mdContext);
	MD5Update (&mdContext, data, datalen);
	MD5Final (&mdContext);
	if ( (*digest = (unsigned char*) malloc(16)) == NULL)
		return;
	memcpy( (char*) *digest, mdContext.digest, 16 );
	*digestlen = 16;
	
}


/* calculate the encrypted checksum */
extern "C" void 
__nis_calculate_encrypted_cksum( unsigned datalen, unsigned char *data, 
				char *deskey, unsigned *digestlen, 
				unsigned char **digest )
{
	int stat;

	__nis_calculate_cksum(datalen, data, digestlen, digest);
	if (*digest == NULL)
		return;

	// encrypt it with the key 
	des_setparity(deskey);
	stat = ecb_crypt(deskey, (char*)*digest, *digestlen, DES_ENCRYPT);
	if (DES_FAILED(stat)) {
		syslog(LOG_INFO,
	       "__nis_calculate_cksum: ecb_crypt() failed, error = %d", stat);
		return;
	}
}

