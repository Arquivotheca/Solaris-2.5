/*
 *	cachesvc.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)cachesvc.cc	1.8	93/07/15 SMI"

#include 	<stdlib.h>
#include 	<sys/types.h>
#include 	<sys/mman.h>
#include 	<string.h>
#include 	<syslog.h>
#include 	<string.h>
#include 	<rpcsvc/nis.h>
#include 	<rpcsvc/nis_cache.h>
#include	<rpc/key_prot.h>
#ifndef TDRPC
#include 	<rpc/des_crypt.h>
#else
#include 	<des_crypt.h>
#endif
#include 	"cachemgr.h"
#include 	"pkeyhash.h"




extern "C" int key_get_conv(char *pkey,	des_block *deskey);

extern PkeyHashtable *pkeyTbl;
extern bool_t insecureMode;

static bool_t verf_sign(nis_name, fd_result*);

/* global variable that points to the shared cache */
MgrCache 	*mgrCache;


extern "C" void
initCacheMgrCache(bool_t init, int init_filesize, int max_filesize,  char* uaddr)
{
	bool_t		manager = TRUE;
	
	mgrCache = new MgrCache(CACHEFILE, init, init_filesize, max_filesize, 
				uaddr);

	if (!mgrCache) 
		exit(1);

	// read in the cold start entry
	if (!mgrCache->initColdStart()) {
		syslog(LOG_ERR, "Error in reading NIS cold start file : '%s'",
		       COLD_START_FILE);
		exit(1);
	}

	// clean out the expired entries
	mgrCache->purge();

	// add the publickeys of the servers in the cache file to 
	// hashtable
	mgrCache->addPublickeys();

	if (!mgrCache->markMgrUp()) 
		exit(1);

#ifdef DEBUG
	print();
	if (__nis_debuglevel)
		pkeyTbl->print();
#endif

}


/* 
 * this function implements  message passing - using the rpc layer.
 * it therefore returns NULL which makes the rpcgen generated code
 * ignore the result.
 */

extern "C" void*
nis_cache_add_entry_1_svc(fd_result *f_result, struct svc_req*)
{
	XDR 		xdrs;
	directory_obj 	d_obj;
	
	if (!insecureMode) {
		/* 
		 * verify that the signature on the directory object
		 * is valid before adding it to the shared cache.
		 */
		if (!verf_sign(f_result->source, f_result)) 
			return(NULL); 
	}
	
	memset((void*)&d_obj, 0, sizeof(d_obj));
	xdrmem_create(&xdrs, 
		      (char *)f_result->dir_data.dir_data_val,
		      f_result->dir_data.dir_data_len, 
		      XDR_DECODE);

	if (!xdr_directory_obj(&xdrs, &d_obj)) {
		syslog(LOG_ERR, "add_entry(): xdr_dir_obj failed");
		return(NULL);
	}

	mgrCache->add( (NisDirCacheEntry*)&d_obj );

	xdrs.x_op = XDR_FREE;
	xdr_directory_obj( &xdrs, &d_obj);
	return( NULL );
}


extern "C" void * 
nis_cache_remove_entry_1_svc(directory_obj *slistp, struct svc_req*)
{
	mgrCache->remove(slistp->do_name, slistp); 
	return(NULL);
}



extern "C" void * 
nis_cache_refresh_entry_1_svc(char **d_name, struct svc_req*)
{
	if(mgrVerbose)
		syslog(LOG_INFO, "updating directory object: '%s'", *d_name);
	update_cached_directory_object(*d_name);
	return(NULL);
}




/* Reinitializes from the cold start file. */


extern "C" void*
nis_cache_read_coldstart_1_svc( void *, struct svc_req*)
{
	mgrCache->initColdStart();
	return(NULL);
}




/* 
* verifies that the signature is valid.
* decrypts the signature using Pkey(serverName) & Skey(own)
* computes signature on data and compares.
* returns TRUE if data is valid else returns FALSE.
*/

static bool_t 
verf_sign(nis_name server_machine, fd_result* frp)
{
	u_long 		key_type;
	netobj 		*pkey;
	unsigned char 	*digest;
	unsigned int 	digestlen;
	keybuf		serv_key;
	des_block	deskey;

	if (mgrCache->get_server_publickey(server_machine, &key_type, &pkey) 
	    == FALSE ) {
		if (mgrVerbose)
			syslog(LOG_ERR,
		       "verf_signature: could not get publickey for NIS+ server '%s'",
			       server_machine);
		return (FALSE); 
	} 
	if (key_type != NIS_PK_DH) {
		// don't know how to deal with anything else for now
		// this is for future extensions
		if (mgrVerbose)
			syslog(LOG_ERR, 
			       "No publickey for NIS+ server: '%s'", server_machine);
		return(FALSE);
	}

	// get the conversationm key from the keyserv
	memset((void*)serv_key, 0, HEXKEYBYTES);
	memcpy((char*)serv_key, pkey->n_bytes, HEXKEYBYTES);

	if (key_get_conv(serv_key, &deskey) != 0) {
		if (mgrVerbose)
			syslog(LOG_ERR, 
		       "Could not get conversation key for NIS+ server: '%s'", 
			       server_machine);
		return (FALSE);
	} 

	// calculate the checksum  on the incoming message, encrypt it with the
	// the converstation key and verify that with the signature on the message
	digest = NULL;
	__nis_calculate_encrypted_cksum(frp->dir_data.dir_data_len,  
					(unsigned char*) frp->dir_data.dir_data_val,  
					(char*)&deskey, &digestlen, &digest);
	
	if (digest == NULL)
		return(FALSE);

	// compare our signature with the one sent from server
	if ( (digestlen != frp->signature.signature_len) ||
	    (memcmp((char*)digest, 
		    (char*) frp->signature.signature_val, digestlen) != 0) ) {
		if (mgrVerbose) {
			/* break up syslog message because of a bug in syslog */
			syslog(LOG_ERR, 
			       "verf_sign(): bad checksum on directory object ");
			syslog(LOG_ERR,
			       "sent by NIS+ server: '%s'", server_machine);
		}
		free((char *)digest);
		return(FALSE);    
	}
	free((char *)digest);
	return(TRUE);
}




extern "C"
void cache_purge()
{
	mgrCache->purge();
}



extern "C"
void mgr_cache_dumpstats()
{
	int t;
	mgrCache->dumpStatistics();
	t = __nis_debuglevel;
	__nis_debuglevel = 3;
	pkeyTbl->print();
	__nis_debuglevel = t;
}


// signal handler clean up and return
extern "C"
void cache_cleanup_onsig()
{
	mgrCache->cleanup();
}



// cleanup action before exiting in case we recieve a signal that
// cannot be blocked

void
MgrCache :: cleanup()
{
	struct sembuf 	buf;
	
	// msync the cachefile
	if (msync(base, *mapLen, 0)) 
		syslog(LOG_ERR, "msync operation failed : %m");

	// indicate that the cachegmr is NOT running 
	buf.sem_num = NIS_SEM_MGR_UP;
	buf.sem_op = -1;
	buf.sem_flg = SEM_UNDO | IPC_NOWAIT;  

	if ( semop(sem_writer, &buf, 1) == -1) 
		syslog(LOG_ERR, "semop failed while unsetting NIS_SEM_MGR_UP: %m");
}




