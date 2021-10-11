/*
 *	npd_svc.c
 *	NPD service routines
 *
 *	Copyright (c) 1994 Sun Microsystems, Inc.
 *	All Rights Reserved.
 */

#pragma ident	"@(#)npd_svc.c	1.4	94/05/25 SMI"

#include <syslog.h>
#include <string.h>
#include <ctype.h>
#include <shadow.h>
#include <crypt.h>
#include <rpcsvc/yppasswd.h>
#include <rpcsvc/nis.h>
#include <rpc/key_prot.h>
#include <rpc/des_crypt.h>
#include <rpcsvc/nispasswd.h>
#include "npd_cache.h"
#include <sys/byteorder.h>

extern int max_attempts;
extern int cache_time;
extern int verbose;
extern nis_result *nis_getpwdent();
extern int __npd_upd_cred();
extern int find_upd_item();
extern int __npd_hash_key();
extern void __npd_gen_rval();
extern bool_t __nis_ismaster();
extern bool_t __nis_isadmin();
extern bool_t __npd_has_aged();
#define	_NPD_PASSMAXLEN	16

/*
 * service routine for first part of the nispasswd update
 * protocol.
 */
bool_t
nispasswd_authenticate_1_svc(argp, result, rqstp)
npd_request *argp;
nispasswd_authresult *result;
struct svc_req *rqstp;
{
	bool_t	check_aging = TRUE;	/* default == check */
	bool_t	same_user = TRUE;	/* default == same user */
	bool_t	is_admin = FALSE;	/* default == not an admin */
	bool_t	entry_exp = FALSE;	/* default == not expired */
	bool_t	upd_entry = FALSE;	/* default == do not update */
	int	ans = 0;
	char	prin[NIS_MAXNAMELEN];
	unsigned char	xpass[_NPD_PASSMAXLEN];
	struct	update_item	*entry = NULL;
	nis_result	*pass_res;
	nis_object	*pobj;
	des_block	deskey, ivec, cryptbuf;
	int	status;
	char	*oldpass;
	unsigned long	randval, ident;


	if (verbose == TRUE)
		syslog(LOG_ERR, "received NIS+ auth request for %s",
			argp->username);

	/* check if I'm running on the host == master(domain) */
	if (__nis_ismaster(nis_local_host(), argp->domain) == FALSE) {
		syslog(LOG_ERR, "not master for %s", argp->domain);
		result->status = NPD_FAILED;
		result->nispasswd_authresult_u.npd_err = NPD_NOTMASTER;
		return (TRUE);
	}

	/* get caller info. from auth_handle */
	if (rqstp)
		(void) __nis_auth2princ(prin, rqstp->rq_cred.oa_flavor,
				rqstp->rq_clntcred, 0);
	else
		prin[0] = '\0';

	/* caller == admin ? ; Y -> skip checks, N -> do aging checks */
	if ((*prin != '\0') && strcmp(prin, "nobody") != 0)
		/* authenticated user, check if they are privileged */
		if (__nis_isadmin(prin, "passwd", argp->domain) == TRUE) {
			check_aging = FALSE;
			is_admin = TRUE;
		}

	if ((*prin == '\0') || (strcmp(prin, "nobody") == 0)) {
				/* "." + null + "." = 3 */
		if ((strlen(argp->username) + strlen(argp->domain) + 3) >
			(size_t) NIS_MAXNAMELEN) {
			syslog(LOG_ERR, "buffer too small");
			result->status = NPD_FAILED;
			result->nispasswd_authresult_u.npd_err =
				NPD_BUFTOOSMALL;
			return (TRUE);
		}
		(void) sprintf(prin, "%s.%s", argp->username, argp->domain);
		if (prin[strlen(prin) - 1] != '.')
			(void) strcat(prin, ".");
	}
	if (strncmp(prin, argp->username, strlen(argp->username)) != 0)
		same_user = FALSE;

	/* check if there is a cached entry */
	if ((find_upd_item(prin, &entry)) &&
		(strcmp(entry->ul_user, argp->username) == 0)) {

		/* found an entry - check if it has expired */
		if (entry->ul_expire > DAY_NOW) {
			entry->ul_attempt = entry->ul_attempt + 1;
			/*
			 * check if this attempt > max_attempts.
			 */
			if (entry->ul_attempt > max_attempts) {
				syslog(LOG_ERR,
					"too many failed attempts for %s",
					argp->username);
				result->status = NPD_FAILED;
				result->nispasswd_authresult_u.npd_err =
					NPD_PASSINVALID;
				return (TRUE);
			}
			if (argp->ident == 0) {
				/*
				 * a new session and we have an entry cached
				 * but we have not reached max_attempts, so
				 * just update entry with the new pass
				 */
				upd_entry = TRUE;
			}
		} else {	/* entry has expired */
			(void) free_upd_item(entry);
			entry_exp = TRUE;
			entry = NULL;
		}
	} else {
		entry = (struct update_item *)__npd_item_by_key(argp->ident);
		if (entry == NULL)	/* no cached entry */
			if (argp->ident != 0) {
				syslog(LOG_ERR,
		"no cache entry found for %s but the identifier is %d",
					argp->username, argp->ident);
				result->status = NPD_FAILED;
				result->nispasswd_authresult_u.npd_err =
						NPD_IDENTINVALID;
				return (TRUE);
			}
	}

	/* get passwd info for username */
	pass_res = nis_getpwdent(argp->username, argp->domain);

	if (pass_res == NULL) {
		syslog(LOG_ERR, "invalid args %s and %s",
			argp->username, argp->domain);
		result->status = NPD_FAILED;
		result->nispasswd_authresult_u.npd_err = NPD_NOSUCHENTRY;
		return (TRUE);
	}
	switch (pass_res->status) {
	case NIS_SUCCESS:
		pobj = NIS_RES_OBJECT(pass_res);
		break;
	case NIS_NOTFOUND:
		syslog(LOG_ERR, "no passwd entry found for %s",
			argp->username);
		(void) nis_freeresult(pass_res);
		result->status = NPD_FAILED;
		result->nispasswd_authresult_u.npd_err = NPD_NOSUCHENTRY;
		return (TRUE);
	default:
		syslog(LOG_ERR,
			"NIS+ error (%d) getting passwd entry for %s",
			pass_res->status, argp->username);
		(void) nis_freeresult(pass_res);
		result->status = NPD_FAILED;
		result->nispasswd_authresult_u.npd_err = NPD_NISERROR;
		return (TRUE);
	}

	/* if user check if 'min' days have passed since 'lastchg' */
	if (check_aging) {
		if ((__npd_has_aged(pobj, &ans) == FALSE) &&
				ans == NPD_NOTAGED) {
			syslog(LOG_ERR,
				"password has not aged enough for %s",
				argp->username);
			(void) nis_freeresult(pass_res);
			result->status = NPD_FAILED;
			result->nispasswd_authresult_u.npd_err = ans;
			return (TRUE);
		}
	/* if ans == NPD_NOSHDWINFO then aging cannot be enforced */
	}
	/* generate CK (from P.c and S.d) */
	if (key_get_conv(argp->user_pub_key.user_pub_key_val, &deskey)
			!= 0) {
		syslog(LOG_ERR,
			"cannot generate a common DES key for %s",
			argp->username);
		syslog(LOG_ERR, "is keyserv still running ?");
		syslog(LOG_ERR, "has %s keylogged in ?", nis_local_host());
		(void) nis_freeresult(pass_res);
		result->status = NPD_FAILED;
		result->nispasswd_authresult_u.npd_err = NPD_CKGENFAILED;
		return (TRUE);
	}
	/* decrypt the passwd sent */
	if (argp->npd_authpass.npd_authpass_len != _NPD_PASSMAXLEN) {
		syslog(LOG_ERR, "password length wrong");
		(void) nis_freeresult(pass_res);
		result->status = NPD_TRYAGAIN;
		result->nispasswd_authresult_u.npd_err = NPD_PASSINVALID;
		return (TRUE);
	}
	(void) memcpy(xpass, argp->npd_authpass.npd_authpass_val,
			_NPD_PASSMAXLEN);
	ivec.key.high = ivec.key.low = 0;
	status = cbc_crypt((char *) &deskey, (char *)&xpass, _NPD_PASSMAXLEN,
			DES_DECRYPT | DES_HW, (char *) &ivec);
	if (DES_FAILED(status)) {
		syslog(LOG_ERR, "failed to decrypt password");
		(void) nis_freeresult(pass_res);
		result->status = NPD_FAILED;
		result->nispasswd_authresult_u.npd_err = NPD_DECRYPTFAIL;
		return (TRUE);
	}

	/* assign an ID and generate R on the first call of a session */
	if (argp->ident == 0) {
		ident	= (u_long)__npd_hash_key(prin);
		if (ident == -1) {
			syslog(LOG_ERR, "invalid ident value calculated");
			(void) nis_freeresult(pass_res);
			result->status = NPD_FAILED;
			result->nispasswd_authresult_u.npd_err = NPD_SYSTEMERR;
			return (TRUE);
		}
		(void) __npd_gen_rval(&randval);
	} else {
		/* second or third attempt */
		ident = argp->ident;
		if (entry == NULL) {
			if (entry_exp)	/* gen a new random val */
				(void) __npd_gen_rval(&randval);
			else {
				syslog(LOG_ERR, "cache corrupted");
				(void) nis_freeresult(pass_res);
				result->status = NPD_FAILED;
				result->nispasswd_authresult_u.npd_err =
					NPD_SYSTEMERR;
				return (TRUE);
			}
		} else {
			if (strcmp(entry->ul_user, argp->username) != 0) {
				/* gen a new random val */
				(void) __npd_gen_rval(&randval);
			} else
				randval = entry->ul_rval;
		}
	}

	if (! __npd_ecb_crypt(&ident, &randval, &cryptbuf,
		sizeof (des_block), DES_ENCRYPT, &deskey)) {
		syslog(LOG_ERR, "failed to encrypt verifier");
		(void) nis_freeresult(pass_res);
		result->status = NPD_FAILED;
		result->nispasswd_authresult_u.npd_err = NPD_ENCRYPTFAIL;
		return (TRUE);
	}
	/* encrypt the passwd and compare with that stored in NIS+ */
	if (same_user) {
		if ((oldpass = ENTRY_VAL(pobj, 1)) == NULL) {
			(void) nis_freeresult(pass_res);
			result->status = NPD_FAILED;
			result->nispasswd_authresult_u.npd_err = NPD_NOPASSWD;
			return (TRUE);
		}
		if (strcmp(crypt((char *)xpass, oldpass), oldpass) != 0) {
			(void) nis_freeresult(pass_res);
			result->nispasswd_authresult_u.npd_verf.npd_xid =
					htonl(cryptbuf.key.high);
			result->nispasswd_authresult_u.npd_verf.npd_xrandval =
					htonl(cryptbuf.key.low);
			/* cache relevant info */
			if (entry == NULL) {
				ans = add_upd_item(prin, argp->username,
					same_user, argp->domain, ident, randval,
					&deskey, xpass);
				if (ans <= 0) {
					result->status = NPD_FAILED;
					result->nispasswd_authresult_u.npd_err =
							NPD_SYSTEMERR;
				} else
					result->status = NPD_TRYAGAIN;
			} else {
				/* found an entry, attempt == max_attempts */
				if (entry->ul_attempt == max_attempts) {
					result->status = NPD_FAILED;
					result->nispasswd_authresult_u.npd_err =
							NPD_SYSTEMERR;
				/*
				 * not really a system error but we
				 * want the caller to think that 'cos
				 * they are obviously trying to break-in.
				 * Perhaps, we should not respond at all,
				 * the client side would timeout.
				 */
				}
			}
			if (verbose == TRUE)
				(void) __npd_print_entry(prin);
			return (TRUE);
		}
	} else {
		if (is_admin == FALSE) {	/* not privileged */
			(void) nis_freeresult(pass_res);
			result->status = NPD_FAILED;
			result->nispasswd_authresult_u.npd_err =
					NPD_PERMDENIED;
			return (TRUE);
		}
		/* admin changing another users password */
		if (__authenticate_admin(prin, xpass, "DES") == FALSE) {
			/*
			 * we have no idea where this admin's
			 * passwd record is stored BUT we do know
			 * where their 'DES' cred is stored from
			 * the netname, so lets try to decrypt
			 * the secret key with the passwd that
			 * was sent across.
			 */
			(void) nis_freeresult(pass_res);
			result->status = NPD_TRYAGAIN;
			result->nispasswd_authresult_u.npd_verf.npd_xid =
					htonl(cryptbuf.key.high);
			result->nispasswd_authresult_u.npd_verf.npd_xrandval =
					htonl(cryptbuf.key.low);
			/* cache relevant info */
			if (entry == NULL) {
				ans = add_upd_item(prin, argp->username,
					same_user, argp->domain, ident, randval,
					&deskey, xpass);
				if (ans <= 0) {
					result->status = NPD_FAILED;
					result->nispasswd_authresult_u.npd_err =
							NPD_SYSTEMERR;
				}
			}
			return (TRUE);
		}
	}
	/* done with pass_res */
	(void) nis_freeresult(pass_res);
	/* cache relevant info */
	if (entry == NULL) {
		ans = add_upd_item(prin, argp->username, same_user,
			argp->domain, ident, randval, &deskey, xpass);
		if (ans <= 0) {
			result->status = NPD_FAILED;
			result->nispasswd_authresult_u.npd_err =
					NPD_SYSTEMERR;
			return (TRUE);
		}
	} else {
		if (entry->ul_oldpass != NULL)
			free(entry->ul_oldpass);
		entry->ul_oldpass = strdup((char *)&xpass[0]);
		if (entry->ul_oldpass == NULL) {
			result->status = NPD_FAILED;
			result->nispasswd_authresult_u.npd_err =
					NPD_SYSTEMERR;
			return (TRUE);
		}
		if (upd_entry == TRUE) {
			entry->ul_ident = ident;
			entry->ul_rval = randval;
			entry->ul_key = deskey;
		}
	}
	result->status = NPD_SUCCESS;
	result->nispasswd_authresult_u.npd_verf.npd_xid =
			htonl(cryptbuf.key.high);
	result->nispasswd_authresult_u.npd_verf.npd_xrandval =
			htonl(cryptbuf.key.low);
	if (verbose == TRUE)
		(void) __npd_print_entry(prin);
	return (TRUE);
}

/*
 * service routine for second part of the nispasswd update
 * protocol.
 */
/* ARGSUSED2 */
bool_t
nispasswd_update_1_svc(updreq, res, rqstp)
npd_update		*updreq;
nispasswd_updresult	*res;
struct svc_req	*rqstp;
{
	struct update_item *entry;
	npd_newpass	cryptbuf;
	passbuf	pass;
	char	*newpass, buf[NIS_MAXNAMELEN];
	u_long	rand;
	struct nis_result *pass_res, *mod_res;
	struct nis_object *pobj, *eobj;
	entry_col	ecol[8];
	char	*old_gecos, *old_shell, *sp;
	char	shadow[80];
	static nispasswd_error	errlist[3];
	int status, error = NPD_SUCCESS, i;

	entry = (struct update_item *)__npd_item_by_key(updreq->ident);
	if (entry == NULL) {
		syslog(LOG_ERR, "invalid identifier: %ld", updreq->ident);
		res->status = NPD_FAILED;
		res->nispasswd_updresult_u.npd_err = NPD_IDENTINVALID;
		return (TRUE);
	}
	if (verbose == TRUE)
		syslog(LOG_ERR,
		"received NIS+ passwd update request for %s",
		entry->ul_user);

	/* decrypt R and new passwd */
	cryptbuf.npd_xrandval = ntohl(updreq->xnewpass.npd_xrandval);
	for (i = 0; i < __NPD_MAXPASSBYTES; i++)
		cryptbuf.pass[i] = updreq->xnewpass.pass[i];

	if (! __npd_cbc_crypt(&rand, pass, __NPD_MAXPASSBYTES,
		&cryptbuf, _NPD_PASSMAXLEN, DES_DECRYPT,
		&entry->ul_key)) {
		syslog(LOG_ERR, "failed to decrypt verifier");
		res->status = NPD_FAILED;
		res->nispasswd_updresult_u.npd_err = NPD_DECRYPTFAIL;
		return (TRUE);
	}

	/* check if R sent matches cached R */
	if (rand != entry->ul_rval) {
		syslog(LOG_ERR, "invalid verifier: %ld", rand);
		res->status = NPD_FAILED;
		res->nispasswd_updresult_u.npd_err = NPD_VERFINVALID;
		return (TRUE);
	}

	/* encrypt new passwd */
	if ((newpass = (char *)__npd_encryptpass(pass)) == NULL) {
		syslog(LOG_ERR, "password encryption failed");
		res->status = NPD_FAILED;
		res->nispasswd_updresult_u.npd_err = NPD_ENCRYPTFAIL;
		return (TRUE);
	}
	/* create passwd struct with this pass & gecos/shell */
	pass_res = nis_getpwdent(entry->ul_user, entry->ul_domain);

	switch (pass_res->status) {
	case NIS_SUCCESS:
		pobj = NIS_RES_OBJECT(pass_res);
		break;
	case NIS_NOTFOUND:
		syslog(LOG_ERR, "no passwd entry found for %s",
			entry->ul_user);
		(void) nis_freeresult(pass_res);
		res->status = NPD_FAILED;
		res->nispasswd_updresult_u.npd_err = NPD_NOSUCHENTRY;
		return (TRUE);
	default:
		syslog(LOG_ERR,
			"NIS+ error (%d) getting passwd entry for %s",
			pass_res->status, entry->ul_user);
		(void) nis_freeresult(pass_res);
		res->status = NPD_FAILED;
		res->nispasswd_updresult_u.npd_err = NPD_NISERROR;
		return (TRUE);
	}
	old_gecos = ENTRY_VAL(pobj, 4);
	old_shell = ENTRY_VAL(pobj, 6);

	/* can change passwd, shell or gecos */
	(void) memset(ecol, 0, sizeof (ecol));
	ecol[1].ec_value.ec_value_val = newpass;
	ecol[1].ec_value.ec_value_len = strlen(newpass) + 1;
	ecol[1].ec_flags = EN_CRYPT|EN_MODIFIED;

	(void) memset(errlist, 0, sizeof (errlist));

	if (*updreq->pass_info.pw_gecos != '\0') {
		if (__npd_can_do(NIS_MODIFY_ACC, pobj,
			entry->ul_item.name, 4) == FALSE) {
			syslog(LOG_NOTICE,
		"insufficient permission for %s to change the gecos",
				entry->ul_user);
			res->status = NPD_PARTIALSUCCESS;
			errlist[0].npd_field = NPD_GECOS;
			errlist[0].npd_code = NPD_PERMDENIED;
			errlist[0].next = NULL;
		} else if (old_gecos == NULL ||
			strcmp(updreq->pass_info.pw_gecos, old_gecos) != 0) {

			ecol[4].ec_value.ec_value_val =
				updreq->pass_info.pw_gecos;
			ecol[4].ec_value.ec_value_len =
				strlen(updreq->pass_info.pw_gecos) + 1;
			ecol[4].ec_flags = EN_MODIFIED;
		}
	}

	if (*updreq->pass_info.pw_shell != '\0') {
		if (__npd_can_do(NIS_MODIFY_ACC, pobj,
			entry->ul_item.name, 6) == FALSE) {
			syslog(LOG_NOTICE,
		"insufficient permission for %s to change the shell",
				entry->ul_user);
			if (res->status == NPD_PARTIALSUCCESS) {
				errlist[0].next = &errlist[1];
				errlist[1].npd_field = NPD_SHELL;
				errlist[1].npd_code = NPD_PERMDENIED;
				errlist[1].next = NULL;
			} else {
				res->status = NPD_PARTIALSUCCESS;
				errlist[0].npd_field = NPD_SHELL;
				errlist[0].npd_code = NPD_PERMDENIED;
				errlist[0].next = NULL;
			}
		} else if (old_shell == NULL ||
			strcmp(updreq->pass_info.pw_shell, old_shell) != 0) {
			ecol[6].ec_value.ec_value_val =
				updreq->pass_info.pw_shell;
			ecol[6].ec_value.ec_value_len =
				strlen(updreq->pass_info.pw_shell) + 1;
			ecol[6].ec_flags = EN_MODIFIED;
		}
	}
	/* update lstchg field */
	sp = ENTRY_VAL(pobj, 7);
	if (sp != NULL) {
		if ((sp = strchr(ENTRY_VAL(pobj, 7), ':')) == NULL) {
			syslog(LOG_ERR, "shadow column corrupted");
			(void) nis_freeresult(pass_res);
			res->status = NPD_FAILED;
			res->nispasswd_updresult_u.npd_err = NPD_SHDWCORRUPT;
			return (TRUE);
		}
		(void) sprintf(shadow, "%d%s", DAY_NOW, sp);
		ecol[7].ec_value.ec_value_val = shadow;
		ecol[7].ec_value.ec_value_len = strlen(shadow) + 1;
		ecol[7].ec_flags = EN_CRYPT|EN_MODIFIED;
	}

	/* update passwd entry */
	eobj = nis_clone_object(pobj, NULL);
	if (eobj == NULL) {
		syslog(LOG_CRIT, "out of memory");
		(void) nis_freeresult(pass_res);
		res->status = NPD_FAILED;
		res->nispasswd_updresult_u.npd_err = NPD_SYSTEMERR;
		return (TRUE);
	}
	eobj->EN_data.en_cols.en_cols_val = ecol;
	eobj->EN_data.en_cols.en_cols_len = 8;

	/* strlen("[name=],passwd.") + null + "." = 17 */
	if ((strlen(entry->ul_user) + strlen(pobj->zo_domain) + 17) >
			(size_t) NIS_MAXNAMELEN) {
		syslog(LOG_ERR, "not enough buffer space");
		(void) nis_freeresult(pass_res);
		(void) nis_destroy_object(eobj);
		res->status = NPD_FAILED;
		res->nispasswd_updresult_u.npd_err = NPD_BUFTOOSMALL;
		return (TRUE);
	}
	(void) sprintf(buf, "[name=%s],passwd.%s", entry->ul_user,
				pobj->zo_domain);
	if (buf[strlen(buf) - 1] != '.')
		(void) strcat(buf, ".");

	mod_res = nis_modify_entry(buf, eobj, 0);

	/* set column stuff to NULL so that we can free eobj */
	eobj->EN_data.en_cols.en_cols_val = NULL;
	eobj->EN_data.en_cols.en_cols_len = 0;

	(void) nis_destroy_object(eobj);
	(void) nis_freeresult(pass_res);

	if (mod_res->status != NIS_SUCCESS) {
		syslog(LOG_ERR, "could not update the passwd info");
		res->status = NPD_FAILED;
		res->nispasswd_updresult_u.npd_err = NPD_NISERROR;

		(void) nis_freeresult(mod_res);
		return (TRUE);
	}
	(void) nis_freeresult(mod_res);

	/* attempt to update cred */
	status = __npd_upd_cred(entry->ul_user, entry->ul_domain,
			"DES", entry->ul_oldpass, pass, &error);
	if (error == NPD_KEYSUPDATED && verbose == TRUE)
		syslog(LOG_ERR, "Generated new key-pair for %s in %s",
				entry->ul_user, entry->ul_domain);
	if (error == NPD_KEYNOTREENC && verbose == TRUE)
		syslog(LOG_ERR, "Cannot encrypt secret key for %s in %s",
				entry->ul_user, entry->ul_domain);

	if (error != NPD_SUCCESS) {
		if (res->status == NPD_PARTIALSUCCESS) {
			if (errlist[0].next == NULL) {
				errlist[0].next = &errlist[1];
				errlist[1].npd_field = NPD_SECRETKEY;
				errlist[1].npd_code = error;
				errlist[1].next = NULL;
			} else if (errlist[1].next == NULL) {
				errlist[1].next = &errlist[2];
				errlist[2].npd_field = NPD_SECRETKEY;
				errlist[2].npd_code = error;
				errlist[2].next = NULL;
			}
		} else {
			res->status = NPD_PARTIALSUCCESS;
			errlist[0].npd_field = NPD_SECRETKEY;
			errlist[0].npd_code = error;
			errlist[0].next = NULL;
		}
	}
	/* free the entry if success or partial success */
	if (res->status == NPD_SUCCESS || res->status == NPD_PARTIALSUCCESS)
		(void) free_upd_item(entry);

	res->nispasswd_updresult_u.reason = errlist[0];
	return (TRUE);
}

/*
 * yppasswd update service routine.
 * The error codes returned are from the 4.x rpc.yppasswdd.c,
 * it seems that the client side only checks if the result is
 * non-zero in which case it prints a generic message !
 */
/* ARGSUSED2 */
bool_t
yppasswdproc_update_1_svc(yppass, result, rqstp)
struct yppasswd	*yppass;
int	*result;
struct svc_req	*rqstp;
{
	char	buf[NIS_MAXNAMELEN], *dom;
	struct passwd	*newpass;
	struct nis_result *mod_res;
	struct nis_object *pobj, *eobj;
	entry_col	ecol[8];
	char shadow[80], *sp, *p;
	char	*old_gecos, *old_shell, *old_pass;
	nis_server	*srv;
	nis_tag		tags[2], *tagres;
	int		status, ans;

	newpass = &yppass->newpw;
	if (verbose == TRUE)
		syslog(LOG_ERR,
		"received yp password update request from %s",
		newpass->pw_name);

	srv = (nis_server *)__nis_host2nis_server(NULL, 0, &ans);

	if (srv == NULL) {
		syslog(LOG_ERR, "no host/addr information: %d", ans);
		*result = -1;
		return (TRUE);
	}
	/*
	 * make the nis_stats call to check if the server is running
	 * in compat mode and get the list of directories this server
	 * is serving
	 */
	tags[0].tag_type = TAG_NISCOMPAT;
	tags[0].tag_val = "";
	tags[1].tag_type = TAG_DIRLIST;
	tags[1].tag_val = "";

	status = nis_stats(srv, tags, 2, &tagres);
	if (status != NIS_SUCCESS) {
		syslog(LOG_ERR, "nis_error: %d", status);
		*result = -1;
		return (1);
	}
	if ((strcmp(tagres[0].tag_val, "<Unknown Statistics>") == 0) ||
		(strcmp(tagres[1].tag_val, "<Unknown Statistics>") == 0)) {
		/* old server */
		syslog(LOG_ERR,
		"NIS+ server does not support the new statistics tags");
		*result = -1;
		return (1);
	}

	/* check if server is running in NIS compat mode */
	if (strcasecmp(tagres[0].tag_val, "OFF") == 0) {
		syslog(LOG_ERR,
		"Local NIS+ server is not running in NIS compat mode");
		*result = -1;
		return (1);
	}
	/*
	 * find the dir that has a passwd entry for this user
	 * POLICY: if user has a passwd stored in more then one
	 * dir then do not make an update. if user has an entry
	 * in only one dir, then make an update in that dir.
	 */
	if (! __npd_find_obj(newpass->pw_name, tagres[1].tag_val,
				&pobj)) {
		*result = -1;
		return (1);
	}

	old_pass = ENTRY_VAL(pobj, 1);
	old_gecos = ENTRY_VAL(pobj, 4);
	old_shell = ENTRY_VAL(pobj, 6);

	if (!__npd_has_aged(pobj, &ans) && ans == NPD_NOTAGED) {
		syslog(LOG_ERR, "password has not aged enough for %s",
			newpass->pw_name);
		(void) nis_destroy_object(pobj);
		*result = -1;
		return (1);
	}
	/* if ans == NPD_NOSHDWINFO then aging cannot be enforced */

	/* validate the old passwd */
	if (old_pass == NULL) {
		(void) nis_destroy_object(pobj);
		*result = 7;	/* password incorrect */
		return (1);
	}
	if (strcmp(crypt(yppass->oldpass, old_pass), old_pass) != 0) {
		syslog(LOG_NOTICE, "incorrect passwd for %s",
				newpass->pw_name);
		(void) nis_destroy_object(pobj);
		*result = 7;
		return (1);
	}

	/* can change passwd, gecos or shell */
	(void) memset(ecol, 0, sizeof (ecol));

	if (strcmp(old_pass, newpass->pw_passwd) != 0) {
		ecol[1].ec_value.ec_value_val = newpass->pw_passwd;
		ecol[1].ec_value.ec_value_len =
				strlen(newpass->pw_passwd) + 1;
		ecol[1].ec_flags = EN_CRYPT|EN_MODIFIED;
	}

	if (strcmp(nis_leaf_of(pobj->zo_domain), "org_dir") == 0) {
		/* need to strip org_dir part of the domain */
		dom = strchr(pobj->zo_domain, '.');
		if (dom != NULL)
			dom++;
	} else
		dom = pobj->zo_domain;

			/* "." + null + "." = 3 */
	if ((strlen(newpass->pw_name) + strlen(dom) + 3) >
			(size_t) NIS_MAXNAMELEN) {
		syslog(LOG_ERR, "not enough buffer space");
		(void) nis_destroy_object(pobj);
		*result = -1;
		return (1);
	}
	(void) sprintf(buf, "%s.%s", newpass->pw_name, dom);
	if (buf[strlen(buf) - 1] != '.')
		(void) strcat(buf, ".");

	if (newpass->pw_gecos != NULL &&
		(old_gecos == NULL ||
			strcmp(old_gecos, newpass->pw_gecos) != 0)) {

		if (! __npd_can_do(NIS_MODIFY_ACC, pobj, buf, 4)) {
			syslog(LOG_NOTICE,
		"insufficient permission for %s to change the gecos",
				newpass->pw_name);
			(void) nis_destroy_object(pobj);
			*result = 2;
			return (1);
		}
		ecol[4].ec_value.ec_value_val = newpass->pw_gecos;
		ecol[4].ec_value.ec_value_len =
				strlen(newpass->pw_gecos) + 1;
		ecol[4].ec_flags = EN_MODIFIED;
	}

	if (newpass->pw_shell != NULL &&
		(old_shell == NULL ||
			strcmp(old_shell, newpass->pw_shell) != 0)) {

		if (! __npd_can_do(NIS_MODIFY_ACC, pobj, buf, 6)) {
			syslog(LOG_NOTICE,
		"insufficient permission for %s to change the shell",
				newpass->pw_name);
			(void) nis_destroy_object(pobj);
			*result = 2;
			return (1);
		}
		ecol[6].ec_value.ec_value_val = newpass->pw_shell;
		ecol[6].ec_value.ec_value_len =
				strlen(newpass->pw_shell) + 1;
		ecol[6].ec_flags = EN_MODIFIED;
	}
	/*
	 * from 4.x:
	 * This fixes a really bogus security hole, basically anyone can
	 * call the rpc passwd daemon, give them their own passwd and a
	 * new one that consists of ':0:0:Im root now:/:/bin/csh^J' and
	 * give themselves root access. With this code it will simply make
	 * it impossible for them to login again, and as a bonus leave
	 * a cookie for the always vigilant system administrator to ferret
	 * them out.
	 */

	for (p = newpass->pw_name; (*p != '\0'); p++)
		if ((*p == ':') || !(isprint(*p)))
			*p = '$';	/* you lose ! */
	for (p = newpass->pw_passwd; (*p != '\0'); p++)
		if ((*p == ':') || !(isprint(*p)))
			*p = '$';	/* you lose ! */

	/* update lstchg field */
	sp = ENTRY_VAL(pobj, 7);
	if (sp != NULL) {
		if ((sp = strchr(sp, ':')) == NULL) {
			syslog(LOG_ERR, "shadow column corrupted");
			(void) nis_destroy_object(pobj);
			*result = -1;
			return (1);
		}
		(void) sprintf(shadow, "%d%s", DAY_NOW, sp);
		ecol[7].ec_value.ec_value_val = shadow;
		ecol[7].ec_value.ec_value_len = strlen(shadow) + 1;
		ecol[7].ec_flags = EN_CRYPT|EN_MODIFIED;
	}

	eobj = nis_clone_object(pobj, NULL);
	if (eobj == NULL) {
		syslog(LOG_CRIT, "out of memory");
		(void) nis_destroy_object(pobj);
		*result = -1;
		return (1);
	}
	eobj->EN_data.en_cols.en_cols_val = ecol;
	eobj->EN_data.en_cols.en_cols_len = 8;

	/* strlen("[name=],passwd.") + null + "." = 17 */
	if ((strlen(newpass->pw_name) + strlen(pobj->zo_domain) + 17) >
			(size_t) NIS_MAXNAMELEN) {
		syslog(LOG_ERR, "not enough buffer space");
		(void) nis_destroy_object(pobj);
		(void) nis_destroy_object(eobj);
		*result = -1;
		return (1);
	}
	(void) sprintf(buf, "[name=%s],passwd.%s", newpass->pw_name,
			pobj->zo_domain);
	if (buf[strlen(buf) - 1] != '.')
		(void) strcat(buf, ".");

	mod_res = nis_modify_entry(buf, eobj, 0);

	/* set column stuff to NULL so that we can free eobj */
	eobj->EN_data.en_cols.en_cols_val = NULL;
	eobj->EN_data.en_cols.en_cols_len = 0;

	if (mod_res->status != NIS_SUCCESS) {
		syslog(LOG_ERR,
			"could not update the passwd: %s",
			nis_sperrno(mod_res->status));
		(void) nis_freeresult(mod_res);
		(void) nis_destroy_object(pobj);
		(void) nis_destroy_object(eobj);
		*result = 13;
		return (1);
	}

	(void) nis_freeresult(mod_res);
	(void) nis_destroy_object(pobj);
	(void) nis_destroy_object(eobj);
	/*
	 * cannot re-encrypt cred because we do not have the
	 * clear new password .... :^(
	 */
	*result = 0;
	return (1);
}

/* ARGSUSED */
int
nispasswd_prog_1_freeresult(transp, xdr_result, result)
SVCXPRT *transp;
xdrproc_t xdr_result;
caddr_t result;
{

	/*
	 * (void) xdr_free(xdr_result, result);
	 * Insert additional freeing code here, if needed
	 */

	return (1);
}
