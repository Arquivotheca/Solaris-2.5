
#include "ktest.h"

extern void authkerb_cache_flush();
#define NGROUPS 32

pass *
test_proc_1(ppass, rqstp)
pass *ppass;
struct svc_req *rqstp;
{
	static pass result;
	static int times = 0;
	uid_t uid;
	gid_t gid;
	int *gp, groups[NGROUPS];
	int credret;
	short grouplen;
	struct authkerb_clnt_cred *cred =
		(struct authkerb_clnt_cred *)rqstp->rq_clntcred;

	if (rqstp->rq_cred.oa_flavor != AUTH_KERB) {
		printf("auth flavor %d used; not AUTH_KERB\n",
			rqstp->rq_cred.oa_flavor);
		svcerr_weakauth(rqstp->rq_xprt);
		return NULL;
	}
	credret = authkerb_getucred(rqstp, &uid, &gid,
				    &grouplen, groups);
#ifdef DEBUG
	printf("test_proc_1: %4d %4d '%s' '%s' '%s'\n",
		ppass->timestamp, ppass->cksum,
		cred->pname, cred->pinst, cred->prealm);
#ifdef DEBUG2
	printf("  checksum %d addr %s nickname %d\n",
		cred->checksum, inet_ntoa(&cred->address), cred->nickname);
	printf("  issue %d life %d expire %d\n",
		cred->time_sec, cred->life * 5, cred->expiry);
#endif DEBUG2
	if (credret) {
		printf("  ** uid %d gid %d #groups %d\n", uid, gid, grouplen);
		if (grouplen) {
			printf("     groups:");
			for (gp = groups; gp < &groups[grouplen]; gp++)
				printf(" %d", *gp);
			printf("\n");
		}
	} else
		printf("  ** uid of principal unknown\n");
#endif DEBUG

#ifdef notdef
	if (times++ > 20) {
		times = 0;
		authkerb_cache_flush(NULL, "", 0);
	}
#endif notdef

	result.cksum = ppass->cksum - 1;
	result.timestamp = ppass->timestamp -1 ;
	return(&result);
}


