/* LINTLIBRARY */
/* PROTOLIB1 */

/*
 * Copyright (c) 1984 by Sun Microsystems, Inc.
 */

#ident	"@(#)nfsstat.c	1.22	95/08/31 SMI"	/* SVr4.0 1.9	*/

/*
 * nfsstat: Network File System statistics
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <nlist.h>
#include <fcntl.h>
#include <kvm.h>
#include <kstat.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/tiuser.h>
#include <sys/statvfs.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>

static struct nlist nl[] = {
#define	X_ROOTVFS	0
			{ "rootvfs" },
#define	X_NFS_VFSOPS	1
			{ "nfs_vfsops" },
#define	X_NFS3_VFSOPS	2
			{ "nfs3_vfsops" },
#define	X_END		3
			{ "" }
};

static kvm_t *kd;			/* kernel id from kvm_open */
static char *vmunix = NULL;		/* name for /vmunix */
static char *core = NULL;		/* name for /dev/kmem */

static kstat_ctl_t *kc = NULL;		/* libkstat cookie */
static kstat_t *rpc_clts_client_kstat, *rpc_clts_server_kstat;
static kstat_t *rpc_cots_client_kstat, *rpc_cots_server_kstat;
static kstat_t *nfs_client_kstat, *nfs_server_kstat;
static kstat_t *rfsproccnt_v2_kstat, *rfsproccnt_v3_kstat;
static kstat_t *rfsreqcnt_v2_kstat, *rfsreqcnt_v3_kstat;
static kstat_t *aclproccnt_v2_kstat, *aclproccnt_v3_kstat;
static kstat_t *aclreqcnt_v2_kstat, *aclreqcnt_v3_kstat;

static void kio(int, int, char *, int);
static void getstats(void);
static void putstats(void);
static void setup(int);
static void cr_print(int);
static void sr_print(int);
static void cn_print(int);
static void sn_print(int);
static void ca_print(int);
static void sa_print(int);
static void req_print(kstat_t *, ulong_t);
static void stat_print(kstat_t *);

static void fail(int, char *, ...);
static kid_t safe_kstat_read(kstat_ctl_t *, kstat_t *, void *);
static kid_t safe_kstat_write(kstat_ctl_t *, kstat_t *, void *);

static void usage(void);
static void mi_print(void);
static int ignore(char *);
static int get_fsid(char *);

main(int argc, char *argv[])
{
	int c;
	int cflag = 0;		/* client stats */
	int sflag = 0;		/* server stats */
	int nflag = 0;		/* nfs stats */
	int rflag = 0;		/* rpc stats */
	int zflag = 0;		/* zero stats after printing */
	int mflag = 0;		/* mount table stats */
	int aflag = 0;		/* print acl statistics */

	while ((c = getopt(argc, argv, "cnrsmza")) != EOF) {
		switch (c) {
		case 'c':
			cflag++;
			break;
		case 'n':
			nflag++;
			break;
		case 'm':
			mflag++;
			break;
		case 'r':
			rflag++;
			break;
		case 's':
			sflag++;
			break;
		case 'z':
			if (geteuid()) {
				fprintf(stderr, "Must be root for z flag\n");
				exit(1);
			}
			zflag++;
			break;
		case 'a':
			aflag++;
			break;
		case '?':
		default:
			usage();
		}
	}

	if (argc - optind >= 1) {
		vmunix = argv[optind];
		if (argc - optind >= 2)
			core = argv[optind + 1];
	}

	setup(zflag);

	if (mflag) {
		mi_print();
		exit(0);
	}

	getstats();

	if (sflag &&
	    (rpc_clts_server_kstat == NULL || nfs_server_kstat == NULL)) {
		fprintf(stderr,
			"nfsstat: kernel is not configured with "
			"the server nfs and rpc code.\n");
	}
	if ((sflag || (!sflag && !cflag))) {
		if (rflag || (!rflag && !nflag && !aflag))
			sr_print(zflag);
		if (nflag || (!rflag && !nflag && !aflag))
			sn_print(zflag);
		if (aflag || (!rflag && !nflag && !aflag))
			sa_print(zflag);
	}
	if (cflag &&
	    (rpc_clts_client_kstat == NULL || nfs_client_kstat == NULL)) {
		fprintf(stderr,
			"nfsstat: kernel is not configured with "
			"the client nfs and rpc code.\n");
	}
	if (cflag || (!sflag && !cflag)) {
		if (rflag || (!rflag && !nflag && !aflag))
			cr_print(zflag);
		if (nflag || (!rflag && !nflag && !aflag))
			cn_print(zflag);
		if (aflag || (!rflag && !nflag && !aflag))
			ca_print(zflag);
	}

	if (zflag)
		putstats();

	return (0);
	/* NOTREACHED */
}

static void
kio(int rdwr, int id, char *buf, int len)
{

	if (nl[id].n_type == 0) {
		fprintf(stderr, "nfsstat: '%s' not in namelist\n",
			nl[id].n_name);
		memset(buf, 0, len);
		return;
	}
	if (rdwr == 0) {
		if (kvm_read(kd, nl[id].n_value, buf, len) != len) {
			fprintf(stderr, "nfsstat: kernel read error\n");
			exit(1);
		}
	} else {
		if (kvm_write(kd, nl[id].n_value, buf, len) != len) {
			fprintf(stderr, "nfsstat: kernel write error\n");
			exit(1);
		}
	}
}

#define	kread(id, buf, len)  kio(0, id, (char *)(buf), len)
#define	kwrite(id, buf, len) kio(1, id, (char *)(buf), len)

static void
getstats(void)
{

	if (rpc_clts_client_kstat != NULL)
		safe_kstat_read(kc, rpc_clts_client_kstat, NULL);
	if (rpc_cots_client_kstat != NULL)
		safe_kstat_read(kc, rpc_cots_client_kstat, NULL);
	if (nfs_client_kstat != NULL)
		safe_kstat_read(kc, nfs_client_kstat, NULL);
	if (rpc_clts_server_kstat != NULL)
		safe_kstat_read(kc, rpc_clts_server_kstat, NULL);
	if (rpc_cots_server_kstat != NULL)
		safe_kstat_read(kc, rpc_cots_server_kstat, NULL);
	if (nfs_server_kstat != NULL)
		safe_kstat_read(kc, nfs_server_kstat, NULL);
	if (rfsproccnt_v2_kstat != NULL)
		safe_kstat_read(kc, rfsproccnt_v2_kstat, NULL);
	if (rfsproccnt_v3_kstat != NULL)
		safe_kstat_read(kc, rfsproccnt_v3_kstat, NULL);
	if (rfsreqcnt_v2_kstat != NULL)
		safe_kstat_read(kc, rfsreqcnt_v2_kstat, NULL);
	if (rfsreqcnt_v3_kstat != NULL)
		safe_kstat_read(kc, rfsreqcnt_v3_kstat, NULL);
	if (aclproccnt_v2_kstat != NULL)
		safe_kstat_read(kc, aclproccnt_v2_kstat, NULL);
	if (aclproccnt_v3_kstat != NULL)
		safe_kstat_read(kc, aclproccnt_v3_kstat, NULL);
	if (aclreqcnt_v2_kstat != NULL)
		safe_kstat_read(kc, aclreqcnt_v2_kstat, NULL);
	if (aclreqcnt_v3_kstat != NULL)
		safe_kstat_read(kc, aclreqcnt_v3_kstat, NULL);
}

static void
putstats(void)
{

	if (rpc_clts_client_kstat != NULL)
		safe_kstat_write(kc, rpc_clts_client_kstat, NULL);
	if (rpc_cots_client_kstat != NULL)
		safe_kstat_write(kc, rpc_cots_client_kstat, NULL);
	if (nfs_client_kstat != NULL)
		safe_kstat_write(kc, nfs_client_kstat, NULL);
	if (rpc_clts_server_kstat != NULL)
		safe_kstat_write(kc, rpc_clts_server_kstat, NULL);
	if (rpc_cots_server_kstat != NULL)
		safe_kstat_write(kc, rpc_cots_server_kstat, NULL);
	if (nfs_server_kstat != NULL)
		safe_kstat_write(kc, nfs_server_kstat, NULL);
	if (rfsproccnt_v2_kstat != NULL)
		safe_kstat_write(kc, rfsproccnt_v2_kstat, NULL);
	if (rfsproccnt_v3_kstat != NULL)
		safe_kstat_write(kc, rfsproccnt_v3_kstat, NULL);
	if (rfsreqcnt_v2_kstat != NULL)
		safe_kstat_write(kc, rfsreqcnt_v2_kstat, NULL);
	if (rfsreqcnt_v3_kstat != NULL)
		safe_kstat_write(kc, rfsreqcnt_v3_kstat, NULL);
	if (aclproccnt_v2_kstat != NULL)
		safe_kstat_write(kc, aclproccnt_v2_kstat, NULL);
	if (aclproccnt_v3_kstat != NULL)
		safe_kstat_write(kc, aclproccnt_v3_kstat, NULL);
	if (aclreqcnt_v2_kstat != NULL)
		safe_kstat_write(kc, aclreqcnt_v2_kstat, NULL);
	if (aclreqcnt_v3_kstat != NULL)
		safe_kstat_write(kc, aclreqcnt_v3_kstat, NULL);
}

static void
setup(int zflag)
{

	kd = kvm_open(vmunix, core, NULL, zflag ? O_RDWR : O_RDONLY, "nfsstat");
	if (kd == NULL)
		exit(1);

	if (kvm_nlist(kd, nl) < 0) {
		fprintf(stderr, "nfsstat: bad namelist\n");
		exit(1);
	}

	if ((kc = kstat_open()) == NULL)
		fail(1, "kstat_open(): can't open /dev/kstat");

	rpc_clts_client_kstat = kstat_lookup(kc, "unix", 0, "rpc_clts_client");
	rpc_clts_server_kstat = kstat_lookup(kc, "unix", 0, "rpc_clts_server");
	rpc_cots_client_kstat = kstat_lookup(kc, "unix", 0, "rpc_cots_client");
	rpc_cots_server_kstat = kstat_lookup(kc, "unix", 0, "rpc_cots_server");
	nfs_client_kstat = kstat_lookup(kc, "nfs", 0, "nfs_client");
	nfs_server_kstat = kstat_lookup(kc, "nfs", 0, "nfs_server");
	rfsproccnt_v2_kstat = kstat_lookup(kc, "nfs", 0, "rfsproccnt_v2");
	rfsproccnt_v3_kstat = kstat_lookup(kc, "nfs", 0, "rfsproccnt_v3");
	rfsreqcnt_v2_kstat = kstat_lookup(kc, "nfs", 0, "rfsreqcnt_v2");
	rfsreqcnt_v3_kstat = kstat_lookup(kc, "nfs", 0, "rfsreqcnt_v3");
	aclproccnt_v2_kstat = kstat_lookup(kc, "nfs_acl", 0, "aclproccnt_v2");
	aclproccnt_v3_kstat = kstat_lookup(kc, "nfs_acl", 0, "aclproccnt_v3");
	aclreqcnt_v2_kstat = kstat_lookup(kc, "nfs_acl", 0, "aclreqcnt_v2");
	aclreqcnt_v3_kstat = kstat_lookup(kc, "nfs_acl", 0, "aclreqcnt_v3");
}

static void
cr_print(int zflag)
{
	int i;
	kstat_named_t *kptr;

	printf("\nClient rpc:\n");

	if (rpc_cots_client_kstat != NULL) {
		printf("Connection oriented:\n");
		stat_print(rpc_cots_client_kstat);
		if (zflag) {
			kptr = KSTAT_NAMED_PTR(rpc_cots_client_kstat);
			for (i = 0; i < rpc_cots_client_kstat->ks_ndata; i++)
			    kptr[i].value.ul = 0;
		}
	}
	if (rpc_clts_client_kstat != NULL) {
		printf("Connectionless:\n");
		stat_print(rpc_clts_client_kstat);
		if (zflag) {
			kptr = KSTAT_NAMED_PTR(rpc_clts_client_kstat);
			for (i = 0; i < rpc_clts_client_kstat->ks_ndata; i++)
			    kptr[i].value.ul = 0;
		}
	}
}

static void
sr_print(int zflag)
{
	int i;
	kstat_named_t *kptr;

	printf("\nServer rpc:\n");

	if (rpc_cots_server_kstat != NULL) {
		printf("Connection oriented:\n");
		stat_print(rpc_cots_server_kstat);
		if (zflag) {
			kptr = KSTAT_NAMED_PTR(rpc_cots_server_kstat);
			for (i = 0; i < rpc_cots_server_kstat->ks_ndata; i++)
				kptr[i].value.ul = 0;
		}
	}
	if (rpc_clts_server_kstat != NULL) {
		printf("Connectionless:\n");
		stat_print(rpc_clts_server_kstat);
		if (zflag) {
			kptr = KSTAT_NAMED_PTR(rpc_clts_server_kstat);
			for (i = 0; i < rpc_clts_server_kstat->ks_ndata; i++)
				kptr[i].value.ul = 0;
		}
	}
}

static void
cn_print(int zflag)
{
	int i;
	ulong_t tot;
	kstat_named_t *kptr;

	if (nfs_client_kstat == NULL)
		return;

	printf("\nClient nfs:\n");

	stat_print(nfs_client_kstat);
	if (zflag) {
		kptr = KSTAT_NAMED_PTR(nfs_client_kstat);
		for (i = 0; i < nfs_client_kstat->ks_ndata; i++)
			kptr[i].value.ul = 0;
	}

	if (rfsreqcnt_v2_kstat != NULL) {
		tot = 0;
		kptr = KSTAT_NAMED_PTR(rfsreqcnt_v2_kstat);
		for (i = 0; i < rfsreqcnt_v2_kstat->ks_ndata; i++)
			tot += kptr[i].value.ul;
		printf("Version 2: (%d calls)\n", (int) tot);
		req_print(rfsreqcnt_v2_kstat, tot);
		if (zflag) {
			for (i = 0; i < rfsreqcnt_v2_kstat->ks_ndata; i++)
				kptr[i].value.ul = 0;
		}
	}

	if (rfsreqcnt_v3_kstat) {
		tot = 0;
		kptr = KSTAT_NAMED_PTR(rfsreqcnt_v3_kstat);
		for (i = 0; i < rfsreqcnt_v3_kstat->ks_ndata; i++)
			tot += kptr[i].value.ul;
		printf("Version 3: (%d calls)\n", (int) tot);
		req_print(rfsreqcnt_v3_kstat, tot);
		if (zflag) {
			for (i = 0; i < rfsreqcnt_v3_kstat->ks_ndata; i++)
				kptr[i].value.ul = 0;
		}
	}
}

static void
sn_print(int zflag)
{
	int i;
	ulong_t tot;
	kstat_named_t *kptr;

	if (nfs_server_kstat == NULL)
		return;

	printf("\nServer nfs:\n");

	stat_print(nfs_server_kstat);
	if (zflag) {
		kptr = KSTAT_NAMED_PTR(nfs_server_kstat);
		for (i = 0; i < nfs_server_kstat->ks_ndata; i++)
			kptr[i].value.ul = 0;
	}

	if (rfsproccnt_v2_kstat != NULL) {
		tot = 0;
		kptr = KSTAT_NAMED_PTR(rfsproccnt_v2_kstat);
		for (i = 0; i < rfsproccnt_v2_kstat->ks_ndata; i++)
			tot += kptr[i].value.ul;
		printf("Version 2: (%d calls)\n", (int) tot);
		req_print(rfsproccnt_v2_kstat, tot);
		if (zflag) {
			for (i = 0; i < rfsproccnt_v2_kstat->ks_ndata; i++)
				kptr[i].value.ul = 0;
		}
	}

	if (rfsproccnt_v3_kstat) {
		tot = 0;
		kptr = KSTAT_NAMED_PTR(rfsproccnt_v3_kstat);
		for (i = 0; i < rfsproccnt_v3_kstat->ks_ndata; i++)
			tot += kptr[i].value.ul;
		printf("Version 3: (%d calls)\n", (int) tot);
		req_print(rfsproccnt_v3_kstat, tot);
		if (zflag) {
			for (i = 0; i < rfsproccnt_v3_kstat->ks_ndata; i++)
				kptr[i].value.ul = 0;
		}
	}
}

static void
ca_print(int zflag)
{
	int i;
	ulong_t tot;
	kstat_named_t *kptr;

	if (nfs_client_kstat == NULL)
		return;

	printf("\nClient nfs_acl:\n");

	if (aclreqcnt_v2_kstat != NULL) {
		tot = 0;
		kptr = KSTAT_NAMED_PTR(aclreqcnt_v2_kstat);
		for (i = 0; i < aclreqcnt_v2_kstat->ks_ndata; i++)
			tot += kptr[i].value.ul;
		printf("Version 2: (%d calls)\n", (int) tot);
		req_print(aclreqcnt_v2_kstat, tot);
		if (zflag) {
			for (i = 0; i < aclreqcnt_v2_kstat->ks_ndata; i++)
				kptr[i].value.ul = 0;
		}
	}

	if (aclreqcnt_v3_kstat) {
		tot = 0;
		kptr = KSTAT_NAMED_PTR(aclreqcnt_v3_kstat);
		for (i = 0; i < aclreqcnt_v3_kstat->ks_ndata; i++)
			tot += kptr[i].value.ul;
		printf("Version 3: (%d calls)\n", (int) tot);
		req_print(aclreqcnt_v3_kstat, tot);
		if (zflag) {
			for (i = 0; i < aclreqcnt_v3_kstat->ks_ndata; i++)
				kptr[i].value.ul = 0;
		}
	}
}

static void
sa_print(int zflag)
{
	int i;
	ulong_t tot;
	kstat_named_t *kptr;

	if (nfs_server_kstat == NULL)
		return;

	printf("\nServer nfs_acl:\n");

	if (aclproccnt_v2_kstat != NULL) {
		tot = 0;
		kptr = KSTAT_NAMED_PTR(aclproccnt_v2_kstat);
		for (i = 0; i < aclproccnt_v2_kstat->ks_ndata; i++)
			tot += kptr[i].value.ul;
		printf("Version 2: (%d calls)\n", (int) tot);
		req_print(aclproccnt_v2_kstat, tot);
		if (zflag) {
			for (i = 0; i < aclproccnt_v2_kstat->ks_ndata; i++)
				kptr[i].value.ul = 0;
		}
	}

	if (aclproccnt_v3_kstat) {
		tot = 0;
		kptr = KSTAT_NAMED_PTR(aclproccnt_v3_kstat);
		for (i = 0; i < aclproccnt_v3_kstat->ks_ndata; i++)
			tot += kptr[i].value.ul;
		printf("Version 3: (%d calls)\n", (int) tot);
		req_print(aclproccnt_v3_kstat, tot);
		if (zflag) {
			for (i = 0; i < aclproccnt_v3_kstat->ks_ndata; i++)
				kptr[i].value.ul = 0;
		}
	}
}

#define	MIN(a, b)	((a) < (b) ? (a) : (b))

#define	FIELD_WIDTH	11

static void
req_print(kstat_t *req, ulong_t tot)
{
	int i, j, nreq, per, over, len;
	char fixlen[128];
	kstat_named_t *knp;

	knp = kstat_data_lookup(req, "null");
	nreq = req->ks_ndata - (knp - KSTAT_NAMED_PTR(req));

	for (i = 0; i < (nreq + 6) / 7; i++) {
		for (j = i * 7; j < MIN(i * 7 + 7, nreq); j++) {
			printf("%-*s", FIELD_WIDTH, knp[j].name);
		}
		printf("\n");
		over = 0;
		for (j = i * 7; j < MIN(i * 7 + 7, nreq); j++) {
			if (tot)
				per = (int)(knp[j].value.ul * 100 / tot);
			else
				per = 0;
			sprintf(fixlen, "%d %d%% ", (int)knp[j].value.ul, per);
			len = strlen(fixlen);
			if (len > FIELD_WIDTH) {
				over += (len - FIELD_WIDTH);
			} else if (over > 0) {
				if (over >= FIELD_WIDTH - len) {
					over -= (FIELD_WIDTH - len);
				} else {
					len += (FIELD_WIDTH - len - over);
					over = 0;
				}
			} else {
				len = FIELD_WIDTH;
			}
			printf("%-*s", len, fixlen);
		}
		printf("\n");
	}
}

static void
stat_print(kstat_t *req)
{
	int i, j, nreq;
	char fixlen[128];
	kstat_named_t *knp;

	knp = KSTAT_NAMED_PTR(req);
	nreq = req->ks_ndata;

	for (i = 0; i < (nreq + 6) / 7; i++) {
		for (j = i * 7; j < MIN(i * 7 + 7, nreq); j++) {
			printf("%-*s", FIELD_WIDTH, knp[j].name);
		}
		printf("\n");
		for (j = i * 7; j < MIN(i * 7 + 7, nreq); j++) {
			sprintf(fixlen, "%d ", (int)knp[j].value.ul);
			printf("%-*s", FIELD_WIDTH, fixlen);
		}
		printf("\n");
	}
}

/*
 * Print the mount table info
 */
static struct vfs vfsrec;
static struct mntinfo mi;


/*
* my_dir and my_path could be pointers
*/
struct myrec {
	u_long my_fsid;
	char my_dir[MAXPATHLEN];
	char my_path[MAXPATHLEN];
	char *ig_path;
	struct myrec *next;
};


static void
mi_print(void)
{
	struct vfs *vfs;	/* "current" VFS pointer */
	struct vfsops *nfs_vfsops;
	struct vfsops *nfs3_vfsops;
	FILE *mt;
	struct mnttab m;
	struct myrec *list, *mrp, *pmrp;
	struct knetconfig knc;
	char proto[KNC_STRSIZE];
	int psize;
	char *flavor;
	int ignored = 0;

	mt = fopen(MNTTAB, "r");
	if (mt == NULL) {
		perror(MNTTAB);
		exit(0);
	}

	list = NULL;
	while (getmntent(mt, &m) == 0) {
		/* ignore non "nfs" and save the "ignore" entries */
		if (strcmp(m.mnt_fstype, MNTTYPE_NFS) != 0)
			continue;

		if ((mrp = (struct myrec *) malloc(sizeof (struct myrec)))
			== 0) {
			fprintf(stderr, "nfsstat: not enough memory\n");
			exit(1);
		}
		mrp->my_fsid = get_fsid(m.mnt_mntopts);
		if (ignore(m.mnt_mntopts)) {
			/*
			* ignored entries cannot be ignored for this
			* option. We have to display the info for this
			* nfs mount. The ignore is an indication
			* that the actual mount point is different and
			* something is in between the nfs mount.
			* So save the mount point now
			*/
			if ((mrp->ig_path = (char *)malloc(
					strlen(m.mnt_mountp) + 1)) == 0) {
				fprintf(stderr, "nfsstat: not enough memory\n");
				exit(1);
			}
			(void) strcpy(mrp->ig_path, m.mnt_mountp);
			ignored++;
		} else {
			mrp->ig_path = 0;
			(void) strcpy(mrp->my_dir, m.mnt_mountp);
		}
		(void) strcpy(mrp->my_path, m.mnt_special);
		mrp->next = list;
		list = mrp;
	}

	/*
	* If something got ignored, go to the beginning of the mnttab
	* and look for the cachefs entries since they are the one
	* causing this. The mount point saved for the ignored entries
	* is matched against the special to get the actual mount point.
	* We are interested in the acutal mount point so that the output
	* look nice too.
	*/
	if (ignored) {
		rewind(mt);
		while (getmntent(mt, &m) == 0) {

			/* ignore non "cachefs" */
			if (strcmp(m.mnt_fstype, MNTTYPE_CACHEFS) != 0)
				continue;

			for (mrp = list; mrp; mrp = mrp->next) {
				if (mrp->ig_path == 0)
					continue;
				if (strcmp(mrp->ig_path, m.mnt_special) == 0) {
					mrp->ig_path = 0;
					(void) strcpy(mrp->my_dir,
							m.mnt_mountp);
				}
			}
		}
		/*
		* Now ignored entries which do not have
		* the my_dir initialized are really ignored; This never
		* happens unless the mnttab is corrupted.
		*/
		for (pmrp = 0, mrp = list; mrp; mrp = mrp->next) {
			if (mrp->ig_path == 0)
				pmrp = mrp;
			else if (pmrp)
				pmrp->next = mrp->next;
			else
				list = mrp->next;
		}
	}

	(void) fclose(mt);


	nfs_vfsops = (struct vfsops *)nl[X_NFS_VFSOPS].n_value;
	nfs3_vfsops = (struct vfsops *)nl[X_NFS3_VFSOPS].n_value;

	kread(X_ROOTVFS, &vfs, sizeof (vfs));

	for (; vfs != NULL; vfs = vfsrec.vfs_next) {
		if (kvm_read(kd, (u_long)vfs, (char *)&vfsrec,
		    sizeof (vfsrec)) != sizeof (vfsrec)) {
			fprintf(stderr, "nfsstat: kernel read error\n");
			exit(1);
		}
		if (vfsrec.vfs_data == NULL)
			continue;
		if (vfsrec.vfs_op != nfs_vfsops &&
		    vfsrec.vfs_op != nfs3_vfsops) {
			continue;
		}
		if (kvm_read(kd, (u_long)vfsrec.vfs_data, (char *)&mi,
		    sizeof (mi)) != sizeof (mi)) {
			fprintf(stderr, "nfsstat: kernel read error\n");
			exit(1);
		}
		for (mrp = list; mrp; mrp = mrp->next) {
			if (mrp->my_fsid == vfsrec.vfs_fsid.val[0])
				break;
		}
		if (mrp == 0)
			continue;

		/*
		 * Now that we've found the file system,
		 * read the netconfig information.
		 */
		if (kvm_read(kd, (u_long)mi.mi_knetconfig, (char *)&knc,
		    sizeof (knc)) != sizeof (knc)) {
			fprintf(stderr, "nfsstat: kernel read error\n");
			exit(1);
		}

		psize = kvm_read(kd, (u_long)knc.knc_proto, proto, KNC_STRSIZE);
		if (psize != KNC_STRSIZE) {
			/*
			 * On diskless systems, the proto in
			 * root's knentconfig could be a
			 * statically allocated string which
			 * will undoubtably be shorter than
			 * KNC_STRSIZE.
			 */
			psize = 0;
			while (kvm_read(kd, (u_long)&knc.knc_proto[psize],
			    &proto[psize], 1) == 1) {
				if (proto[psize++] == '\0')
					break;
				if (psize >= KNC_STRSIZE)
					break;
			}
			if (psize == 0) {
				fprintf(stderr, "nfsstat: kernel read error\n");
				exit(1);
			}
		}

		printf("%s from %s\n", mrp->my_dir, mrp->my_path);

		printf(" Flags:   vers=%lu,proto=%s", mi.mi_vers, proto);
		switch (mi.mi_authflavor) {
		case AUTH_NONE:
			flavor = "none";
			break;
		case AUTH_UNIX:
			flavor = "unix";
			break;
		case AUTH_SHORT:
			flavor = "short";
			break;
		case AUTH_DES:
			flavor = "des";
			break;
		case AUTH_KERB:
			flavor = "kerb";
			break;
		default:
			flavor = NULL;
			break;
		}
		if (flavor != NULL)
			printf(",auth=%s", flavor);
		else
			printf(",auth=<%d>", mi.mi_authflavor);
		printf(",%s", (mi.mi_flags & MI_HARD) ? "hard" : "soft");
		if (mi.mi_flags & MI_PRINTED)
			printf(",printed");
		printf(",%s", (mi.mi_flags & MI_INT) ? "intr" : "nointr");
		if (mi.mi_flags & MI_DOWN)
			printf(",down");
		if (mi.mi_flags & MI_NOAC)
			printf(",noac");
		if (mi.mi_flags & MI_NOCTO)
			printf(",nocto");
		if (mi.mi_flags & MI_DYNAMIC)
			printf(",dynamic");
		if (mi.mi_flags & MI_LLOCK)
			printf(",llock");
		if (mi.mi_flags & MI_GRPID)
			printf(",grpid");
		if (mi.mi_flags & MI_RPCTIMESYNC)
			printf(",rpctimesync");
		if (mi.mi_flags & MI_LINK)
			printf(",link");
		if (mi.mi_flags & MI_SYMLINK)
			printf(",symlink");
		if (mi.mi_flags & MI_READDIR)
			printf(",readdir");
		if (mi.mi_flags & MI_ACL)
			printf(",acl");
		printf(",rsize=%ld,wsize=%ld", mi.mi_curread, mi.mi_curwrite);
		printf(",retrans=%d", mi.mi_retrans);
		printf("\n");

#define	srtt_to_ms(x) x, (x * 2 + x / 2)
#define	dev_to_ms(x) x, (x * 5)

		if (mi.mi_timers[0].rt_srtt || mi.mi_timers[0].rt_rtxcur) {
			printf(
		" Lookups: srtt=%d (%dms), dev=%d (%dms), cur=%d (%dms)\n",
				srtt_to_ms(mi.mi_timers[0].rt_srtt),
				dev_to_ms(mi.mi_timers[0].rt_deviate),
				(int)mi.mi_timers[0].rt_rtxcur,
				(int)mi.mi_timers[0].rt_rtxcur * 20);
		}
		if (mi.mi_timers[1].rt_srtt || mi.mi_timers[1].rt_rtxcur) {
			printf(
		" Reads:   srtt=%d (%dms), dev=%d (%dms), cur=%d (%dms)\n",
				srtt_to_ms(mi.mi_timers[1].rt_srtt),
				dev_to_ms(mi.mi_timers[1].rt_deviate),
				(int)mi.mi_timers[1].rt_rtxcur,
				(int)mi.mi_timers[1].rt_rtxcur * 20);
		}
		if (mi.mi_timers[2].rt_srtt || mi.mi_timers[2].rt_rtxcur) {
			printf(
		" Writes:  srtt=%d (%dms), dev=%d (%dms), cur=%d (%dms)\n",
				srtt_to_ms(mi.mi_timers[2].rt_srtt),
				dev_to_ms(mi.mi_timers[2].rt_deviate),
				(int)mi.mi_timers[2].rt_rtxcur,
				(int)mi.mi_timers[2].rt_rtxcur * 20);
		}
		printf(
		" All:     srtt=%d (%dms), dev=%d (%dms), cur=%d (%dms)\n",
			srtt_to_ms(mi.mi_timers[3].rt_srtt),
			dev_to_ms(mi.mi_timers[3].rt_deviate),
			(int)mi.mi_timers[3].rt_rtxcur,
			(int)mi.mi_timers[3].rt_rtxcur * 20);
		printf("\n");
	}
}

static char *mntopts[] = { MNTOPT_IGNORE, MNTOPT_DEV, NULL };
#define	IGNORE  0
#define	DEV	1

/*
 * Return 1 if "ignore" appears in the options string
 */
static int
ignore(char *opts)
{
	char *value;
	char *s = strdup(opts);

	if (s == NULL)
		return (0);
	opts = s;

	while (*opts != '\0') {
		if (getsubopt(&opts, mntopts, &value) == IGNORE) {
			free(s);
			return (1);
		}
	}

	free(s);
	return (0);
}

/*
 * Get the fsid from the "dev=" option
 * actually, the device number.
 */
static int
get_fsid(char *opts)
{
	char *devid;
	char *s = strdup(opts);
	int dev;

	if (s == NULL)
		return (0);
	opts = s;

	while (*opts != '\0') {
		if (getsubopt(&opts, mntopts, &devid) == DEV)
			goto found;
	}

	free(s);
	return (0);

found:
	dev = strtol(devid, (char **) NULL, 16);
	free(s);
	return (dev);
}

void
usage(void)
{

	fprintf(stderr, "Usage: nfsstat [-cnrsmz] [unix] [core]\n");
	exit(1);
}

static void
fail(int do_perror, char *message, ...)
{
	va_list args;

	va_start(args, message);
	fprintf(stderr, "nfsstat: ");
	vfprintf(stderr, message, args);
	va_end(args);
	if (do_perror)
		fprintf(stderr, ": %s", strerror(errno));
	fprintf(stderr, "\n");
	exit(2);
}

kid_t
safe_kstat_read(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kstat_chain_id = kstat_read(kc, ksp, data);

	if (kstat_chain_id == -1)
		fail(1, "kstat_read(%x, '%s') failed", kc, ksp->ks_name);
	return (kstat_chain_id);
}

kid_t
safe_kstat_write(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kstat_chain_id = kstat_write(kc, ksp, data);

	if (kstat_chain_id == -1)
		fail(1, "kstat_write(%x, '%s') failed", kc, ksp->ks_name);
	return (kstat_chain_id);
}
