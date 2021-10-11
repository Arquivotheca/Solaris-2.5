#ident	"@(#)batch_update.c 1.30 93/07/07"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#include <signal.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/vfs.h>
#include <sys/resource.h>
#include "dboper.h"

/*
 * internal error codes
 */
#define	ERR_GENERIC	-1
#define	ERR_EMPTYFILE	-2

static int hashsize;
#define	HASH(a)		(a & (hashsize-1))
static struct bucket {
	struct node *nxt;
} *buckets;

#define	ROOT_INODE	2

struct node {
	struct dnode *dn;
	char *name;
	int	namelen;
	int	flags;
	u_long	dnode_num;
	struct node *sibling;
	struct node *child;
	struct node *hashnxt;
};
/* node flags */
#define	NODE_ISDIR	1

#define	STDIO_BUFSIZE	32768
static char *stdiobuf1;
static char *stdiobuf2;

static struct node *root;
static struct node *alloc_node();

static struct dheader dumphead;
static struct tape_data {
	struct bu_tape bu_tape;
	u_long	maxdirinode;
	struct tape_data *nxt;
} *tapehead;
static struct tape_data *alloc_tape();

#define	NULLNODE (struct node *)0

static struct dnode *alloc_dnode();
static char *alloc_name();

static int ndnodes;
static int pathoffset;
static int linkoffset;

#ifdef __STDC__
static void handler(int);
static int empty_update(const char *, u_long);
static int build_updatefile(const char *, char *, u_long);
static int buildtree(FILE *, FILE *);
static int getdirs(FILE *, int, int *);
static int order_tree(struct node *);
static struct node *hash_lookup(u_long);
static struct node *link_lookup(u_long);
static struct node *hash_exchange(struct node *);
static int add_dnodes(FILE *, int, FILE *);
static void makelink(struct dnode *, const char *, FILE *);
static int process_tapes(FILE *, int, int);
static void locate_tape(struct dnode *);
static int build_root(const char *);
static void dump_dnodes(struct node *, u_long, FILE *, FILE *);
static int output_dumpheader(char *, const char *, u_long);
static void dump_dir_and_instance(struct node *, u_long, u_long);
static int update_activetapes(const char *, u_long);
static int getmemory(long filesize, const struct bu_header *);
static void freemem1(void);
static void freemem2(void);
static void alloc_iobuffers(void);
static struct dnode *alloc_dnode(void);
static struct node *alloc_node(void);
static struct tape_data *alloc_tape(void);
static char *alloc_name(int);
static void start_msg(void);
static void success_msg(const char *);
static void update_stats(const char *);
static void fail_msg(void);
#else
static void handler();
static int empty_update();
static int build_updatefile();
static int buildtree();
static int getdirs();
static int order_tree();
static struct node *hash_lookup();
static struct node *link_lookup();
static struct node *hash_exchange();
static int add_dnodes();
static void makelink();
static int process_tapes();
static void locate_tape();
static int build_root();
static void dump_dnodes();
static int output_dumpheader();
static void dump_dir_and_instance();
static int update_activetapes();
static int getmemory();
static void freemem1();
static void freemem2();
static void alloc_iobuffers();
static struct dnode *alloc_dnode();
static struct node *alloc_node();
static struct tape_data *alloc_tape();
static char *alloc_name();
static void start_msg();
static void success_msg();
static void update_stats();
static void fail_msg();
#endif

/*ARGSUSED*/
static void
handler(sig)
	int sig;
{
	fail_msg();
	exit(1);
}

#ifdef __STDC__
batch_update(const char *host,
	const char *file)
#else
batch_update(host, file)
	char *host;
	char *file;
#endif
{
	FILE *fp, *pcfp, *lnkfp;
	char path[256];
	u_long dumpid;
	char dnode_name[256], pcomp_name[256], header_name[256], lnk_name[256],
		updatefile[256], newname[256];
	struct sigvec vec;
	struct stat statbuf;
	int rc;

	startupreg(file);
	vec.sv_handler = handler;
#ifdef USG
	vec.sa_flags = SA_RESTART;
	(void) sigemptyset(&vec.sa_mask);
#else
	vec.sv_flags = 0;
	vec.sv_mask = 0;
#endif
	(void) sigvec(SIGHUP, &vec, (struct sigvec *)NULL);
	(void) sigvec(SIGINT, &vec, (struct sigvec *)NULL);
	(void) sigvec(SIGQUIT, &vec, (struct sigvec *)NULL);
	(void) sigvec(SIGBUS, &vec, (struct sigvec *)NULL);
	(void) sigvec(SIGSEGV, &vec, (struct sigvec *)NULL);
	(void) sigvec(SIGTERM, &vec, (struct sigvec *)NULL);

	dumphead.dh_host[0] = 0;
	root = NULLNODE;
	tapehead = (struct tape_data *)0;
	linkoffset = pathoffset = ndnodes = 0;
	(void) sprintf(path, "%s/%s", host, UPDATE_FILE);
	(void) strcat(path, ".%lu");
	if (sscanf(file, path, &dumpid) != 1) {
		(void) fprintf(stderr, gettext(
		    "unknown update file name format `%s'\n"), file);
		fail_msg();
		return (-1);
	}

	/*
	 * open the file created by dump.  The structure of this file
	 * is defined in "batchfile.h"
	 */
	if ((fp = fopen(file, "r")) == NULL) {
		perror(file);
		fail_msg();
		return (-1);
	}
	alloc_iobuffers();
	(void) setvbuf(fp, stdiobuf1, _IOFBF, STDIO_BUFSIZE);

	/*
	 * build the file that indicates an update is in progress for
	 * the given dumpid.  This causes the startup code to retry this
	 * update if we crash somewhere in the middle of it.
	 */
	if (build_updatefile(host, updatefile, dumpid)) {
		(void) fprintf(stderr, gettext("cannot create update file\n"));
		(void) fclose(fp);
		fail_msg();
		return (-1);
	}

	(void) sprintf(lnk_name,
		"%s/%s%s.%lu", host, TEMP_PREFIX, LINKFILE, dumpid);
	if ((lnkfp = fopen(lnk_name, "w")) == NULL) {
		(void) fprintf(stderr,
			gettext("cannot open link file `%s'\n"), lnk_name);
		(void) fclose(fp);
		fail_msg();
		return (-1);
	} else if (fchmod(fileno(lnkfp), 0600) == -1) {
		(void) fprintf(stderr, gettext(
			"cannot chmod link file `%s'\n"), lnk_name);
		(void) fclose(fp);
		(void) fclose(lnkfp);
		fail_msg();
		return (-1);
	}

	if (rc = buildtree(fp, lnkfp)) {
		(void) fclose(fp);
		(void) fclose(lnkfp);
		(void) unlink(file);
		(void) unlink(lnk_name);
		if (rc == ERR_EMPTYFILE) {
			return (empty_update(host, dumpid));
		} else {
			(void) fprintf(stderr, gettext("%s error\n"),
				"build_tree");
			fail_msg();
			return (-1);
		}
	}
	(void) fclose(fp);
	if (fflush(lnkfp) != 0) {
		(void) fprintf(stderr, gettext("link file %s failure\n"),
			"fflush");
		(void) fclose(lnkfp);
		(void) unlink(lnk_name);
		fail_msg();
		return (-1);
	}
	(void) fsync(fileno(lnkfp));
	if (fstat(fileno(lnkfp), &statbuf) == -1 || statbuf.st_size == 0) {
		(void) unlink(lnk_name);
		lnk_name[0] = 0;
	}
	(void) fclose(lnkfp);

	if (root == NULLNODE) {
		(void) fprintf(stderr, gettext("input file `%s' is empty!\n"),
			file);
		(void) unlink(lnk_name);
		fail_msg();
		return (-1);
	}

	/*
	 * produce dnode and pathcomponent files from our in-memory
	 * representation of the dump file.
	 */
	(void) sprintf(dnode_name,
		"%s/%s%s.%lu", host, TEMP_PREFIX, DNODEFILE, dumpid);
	if ((fp = fopen(dnode_name, "w")) == NULL) {
		(void) fprintf(stderr, gettext("cannot create `%s'\n"),
			dnode_name);
		(void) unlink(lnk_name);
		fail_msg();
		return (-1);
	} else if (fchmod(fileno(fp), 0600) == -1) {
		perror("fchmod");
		(void) fprintf(stderr, gettext("cannot chmod `%s'\n"),
			dnode_name);
		(void) fclose(fp);
		(void) unlink(lnk_name);
		(void) unlink(dnode_name);
		fail_msg();
		return (-1);
	}
	(void) setvbuf(fp, stdiobuf1, _IOFBF, STDIO_BUFSIZE);

	(void) sprintf(pcomp_name,
		"%s/%s%s.%lu", host, TEMP_PREFIX, PATHFILE, dumpid);
	if ((pcfp = fopen(pcomp_name, "w")) == NULL) {
		(void) fprintf(stderr, gettext("cannot create `%s'\n"),
			pcomp_name);
		(void) fclose(fp);
		(void) unlink(dnode_name);
		(void) unlink(lnk_name);
		fail_msg();
		return (-1);
	} else if (fchmod(fileno(pcfp), 0600) == -1) {
		perror("fchmod");
		(void) fprintf(stderr, gettext("cannot chmod `%s'\n"),
			pcomp_name);
		(void) fclose(fp);
		(void) fclose(pcfp);
		(void) unlink(lnk_name);
		(void) unlink(dnode_name);
		(void) unlink(pcomp_name);
		fail_msg();
		return (-1);
	}
	(void) setvbuf(pcfp, stdiobuf2, _IOFBF, STDIO_BUFSIZE);

	dump_dnodes(root, NONEXISTENT_BLOCK, fp, pcfp);

	/*
	 * make sure that all dnode and pathcomponent data is flushed
	 * to disk.  Note that these files continue to have temporary
	 * names until the `dir' and `instance' transactions have
	 * all been processed.
	 */
	if (fflush(fp) != 0 || fflush(pcfp) != 0) {
		(void) fprintf(stderr, gettext("cannot %s %s\n"),
			"fflush", "dnode/pathcomponent");
		(void) fclose(fp);
		(void) fclose(pcfp);
		(void) unlink(lnk_name);
		(void) unlink(dnode_name);
		(void) unlink(pcomp_name);
		fail_msg();
		return (-1);
	}
	(void) fsync(fileno(fp));
	(void) fclose(fp);
	(void) fsync(fileno(pcfp));
	(void) fclose(pcfp);

	freemem1();

	/*
	 * create a dump header file
	 */
	if (output_dumpheader(header_name, host, dumpid)) {
		(void) unlink(lnk_name);
		(void) unlink(dnode_name);
		(void) unlink(pcomp_name);
		(void) unlink(header_name);
		fail_msg();
		return (-1);
	}

	/*
	 * now traverse our in-memory representation once more to
	 * update the directory and instance files.
	 */
	if (instance_open(host)) {
		(void) fprintf(stderr,
			gettext("cannot open instance for `%s'\n"), host);
		(void) unlink(lnk_name);
		(void) unlink(dnode_name);
		(void) unlink(pcomp_name);
		(void) unlink(header_name);
		fail_msg();
		return (-1);
	}
	if (dir_open(host)) {
		(void) fprintf(stderr,
			gettext("cannot open dir for `%s'\n"), host);
		(void) unlink(lnk_name);
		(void) unlink(dnode_name);
		(void) unlink(pcomp_name);
		(void) unlink(header_name);
		instance_close(host);
		fail_msg();
		return (-1);
	}
	dump_dir_and_instance(root, DIR_ROOTBLK, dumpid);
	instance_close(host);
	dir_close(host);

	freemem2();

	(void) update_activetapes(host, dumpid);

	/*
	 * Here we have safely acquired all the data.  Now we can
	 * rename the `update.inprogress' file, remove the original
	 * update data file, rename temporary files and
	 * apply transaction files.
	 */
	(void) sprintf(newname, "%s/%s.%lu", host, UPDATE_DONE, dumpid);
	if (rename(updatefile, newname) == -1) {
		perror("rename");
		(void) fprintf(stderr, gettext("cannot rename `%s'\n"),
			updatefile);
		/* XXX: what do we do now??? */
	}
	startupreg(NULL);
	(void) strcpy(updatefile, newname);
	(void) sprintf(newname, "%s/%s.%lu", host, DNODEFILE, dumpid);
	if (rename(dnode_name, newname) == -1) {
		perror("rename");
		(void) fprintf(stderr, gettext("cannot rename `%s'\n"),
			dnode_name);
		/* XXX ??? */
	}
	(void) sprintf(newname, "%s/%s.%lu", host, PATHFILE, dumpid);
	if (rename(pcomp_name, newname) == -1) {
		perror("rename");
		(void) fprintf(stderr, gettext("cannot rename `%s'\n"),
			pcomp_name);
		/* XXX ??? */
	}
	(void) sprintf(newname, "%s/%s.%lu", host, HEADERFILE, dumpid);
	if (rename(header_name, newname) == -1) {
		perror("rename");
		(void) fprintf(stderr, gettext("cannot rename `%s'\n"),
			header_name);
	}
	if (lnk_name[0]) {
		(void) sprintf(newname, "%s/%s.%lu", host, LINKFILE, dumpid);
		if (rename(lnk_name, newname) == -1) {
			perror("rename");
			(void) fprintf(stderr, gettext("cannot rename `%s'\n"),
				lnk_name);
		}
	}
	if (unlink(file) == -1) {
		perror("unlink");
		(void) fprintf(stderr,
			gettext("cannot remove update file `%s'\n"), file);
	}
	if (dir_trans(host) != 0) {
		(void) fprintf(stderr, gettext("%s failure\n"), "dir_trans");
		fail_msg();
		return (-1);
	}
	if (instance_trans(host) != 0) {
		(void) fprintf(stderr, gettext("%s failure\n"),
			"instance_trans");
		fail_msg();
		return (-1);
	}
	if (tape_trans(host) != 0) {
		(void) fprintf(stderr, gettext("%s failure\n"), "tape_trans");
		fail_msg();
		return (-1);
	}
	if (unlink(updatefile) == -1) {
		perror("unlink");
		(void) fprintf(stderr, gettext("cannot remove `%s'\n"),
			updatefile);
	}
	success_msg(host);
	return (0);
}

static int
#ifdef __STDC__
empty_update(const char *host,
	u_long dumpid)
#else
empty_update(host, dumpid)
	char *host;
	u_long dumpid;
#endif
{
	char header_name[256], update_name[256], newname[256];

	if (output_dumpheader(header_name, host, dumpid)) {
		(void) fprintf(stderr, gettext("empty dump/header write\n"));
		(void) unlink(header_name);
		fail_msg();
		return (-1);
	}
	if (build_updatefile(host, update_name, dumpid)) {
		(void) unlink(header_name);
		fail_msg();
		return (-1);
	}
	if (update_activetapes(host, dumpid)) {
		(void) unlink(header_name);
		fail_msg();
		return (-1);
	}
	(void) sprintf(newname, "%s/%s.%lu", host, UPDATE_DONE, dumpid);
	if (rename(update_name, newname) == -1) {
		perror("rename");
		(void) unlink(header_name);
		(void) unlink(update_name);
		fail_msg();
		return (-1);
	}
	(void) strcpy(update_name, newname);
	(void) sprintf(newname, "%s/%s.%lu", host, HEADERFILE, dumpid);
	if (rename(header_name, newname) == -1) {
		perror("rename");
		(void) unlink(header_name);
		fail_msg();
		return (-1);
	}
	if (tape_trans(host) != 0) {
		(void) fprintf(stderr, gettext("%s failure\n"), "tape_trans");
		fail_msg();
		return (-1);
	}
	(void) unlink(update_name);
	success_msg(host);
	return (0);
}

static int
#ifdef __STDC__
build_updatefile(const char *host,
	char *ufile,
	u_long dumpid)
#else
build_updatefile(host, ufile, dumpid)
	char *host;
	char *ufile;
	u_long dumpid;
#endif
{
	char name[256];
	struct stat stbuf;
	int filesizes[3];
	int fd;

	(void) sprintf(name, "%s/%s", host, DIRFILE);
	if (stat(name, &stbuf) == -1) {
		stbuf.st_size = 0;
	}
	filesizes[0] = stbuf.st_size;
	(void) sprintf(name, "%s/%s", host, INSTANCEFILE);
	if (stat(name, &stbuf) == -1) {
		stbuf.st_size = 0;
	}
	filesizes[1] = stbuf.st_size;
	(void) sprintf(name, "%s", TAPEFILE);
	if (stat(name, &stbuf) == -1) {
		stbuf.st_size = 0;
	}
	filesizes[2] = stbuf.st_size;
	(void) sprintf(ufile, "%s/%s.%lu", host, UPDATE_INPROGRESS, dumpid);
	if ((fd = open(ufile, O_RDWR|O_CREAT, 0600)) == -1) {
		perror("open");
		(void) fprintf(stderr, gettext("cannot create `%s'\n"), ufile);
		return (-1);
	}
	if (write(fd, (char *)filesizes, 3*sizeof (int)) != 3*sizeof (int)) {
		perror("write");
		(void) fprintf(stderr, gettext("cannot write file sizes\n"));
		(void) close(fd);
		(void) unlink(ufile);
		return (-1);
	}
	(void) fsync(fd);
	(void) close(fd);
	return (0);
}

static int
buildtree(fp, lnkfp)
	FILE *fp;
	FILE *lnkfp;
{
	struct bu_header buh, buh1;
	struct stat stbuf;
	int dnodecnt;
	int rc = 0;

	if (fstat(fileno(fp), &stbuf) == -1) {
		perror("fstat");
		(void) fprintf(stderr,
			gettext("%s: %s error\n"), "buildtree", "fstat");
		return (-1);
	}


	/* read the update file header */
	if (fread((char *)&buh, sizeof (struct bu_header), 1, fp) != 1) {
		(void) fprintf(stderr, gettext("%s: cannot read file header\n"),
			"buildtree");
		return (-1);
	}

	if (getmemory(stbuf.st_size, &buh))
		return (-1);

	/* read the dump header */
	if (fread((char *)&dumphead, sizeof (struct dheader), 1, fp) != 1) {
		(void) fprintf(stderr,
			gettext("%s: cannot read dump header\n"), "buildtree");
		return (-1);
	}
	dumphead.dh_ntapes = buh.tape_cnt;

	start_msg();
	if (buh.name_cnt == 0 || buh.dnode_cnt == 0) {
		dumphead.dh_flags |= DH_EMPTY;
		rc = ERR_EMPTYFILE;
		goto gettapes;
	}

	/*
	 * build up directory information
	 */
	dnodecnt = buh.dnode_cnt;
	if (getdirs(fp, (int)buh.name_cnt, &dnodecnt))
		return (-1);
	root = hash_lookup((u_long)ROOT_INODE);
	if (order_tree(root))
		return (-1);

	/*
	 * now add dnode info to our tree
	 */
	if (add_dnodes(fp, dnodecnt, lnkfp))
		return (-1);

	/*
	 * build name for root node.  Note that there may be multiple
	 * levels depending on the mount point...
	 */
	if (build_root(dumphead.dh_mnt))
		return (-1);

	/*
	 * get tape data
	 */
gettapes:
	if (process_tapes(fp, (int)buh.tape_cnt, rc))
		return (-1);


	/*
	 * and finally the file header again...
	 */
	if (fread((char *)&buh1, sizeof (struct bu_header), 1, fp) != 1) {
		(void) fprintf(stderr, gettext("%s: cannot read file header\n"),
			"buildtree");
		return (-1);
	}

	if (bcmp((char *)&buh1, (char *)&buh, sizeof (struct bu_header))) {
		(void) fprintf(stderr,
			gettext("bad update file: header mismatch\n"));
		return (-1);
	}

	return (rc);
}

static int
getdirs(fp, num, dncnt)
	FILE *fp;
	int num;
	int *dncnt;
{
	struct bu_name bun;
	int cnt = 0;
	char name[256];
	struct node *n, *parent;
	struct bucket *b;

	while (cnt < num) {
		if (fread((char *)&bun, sizeof (struct bu_name), 1, fp) != 1) {
			(void) fprintf(stderr, gettext(
				"%s: cannot read name struct\n"), "getdirs");
			return (-1);
		}
		n = alloc_node();
		n->flags = 0;
		n->dn = NULL_DNODE;
		n->name = NULL;
		n->sibling = n->child = n->hashnxt = NULLNODE;
		n->dnode_num = bun.inode;
		if (bun.type == DIRECTORY) {
			parent = n;
			n->child = NULLNODE;
			n->name = NULL;
			/* add dir to hash list */
			b = &buckets[HASH(bun.inode)];
			n->hashnxt = b->nxt;
			b->nxt = n;

			/*
			 * directories carry their dnodes right
			 * with them.
			 */
			n->dn = alloc_dnode();
			if (fread((char *)n->dn,
					sizeof (struct dnode), 1, fp) != 1) {
				(void) fprintf(stderr, gettext(
				    "%s: bad dnode fread\n"), "getdirs");
				return (-1);
			}
			(*dncnt)--;
		} else {
			if ((int)bun.namelen < 0 ||
			    (int)bun.namelen > MAXNAMLEN+1) {
				(void) fprintf(stderr,
				    gettext("%s: bad namelen %d\n"),
					"getdirs", bun.namelen);
				return (-1);
			}
			if (fread((char *)name, (int)bun.namelen, 1, fp) != 1) {
				(void) fprintf(stderr, gettext(
				    "%s: cannot read name\n"), "getdirs");
				return (-1);
			}
			n->name = alloc_name((int)bun.namelen);
			(void) strcpy(n->name, name);
			n->namelen = bun.namelen-1;
			n->sibling = parent->child;
			parent->child = n;
		}
		cnt++;
	}
	return (0);
}

static int
order_tree(troot)
	struct node *troot;
{
	register struct node *n, *c;
	int rc;
	struct bucket *b;

	if (troot == NULLNODE) {
		(void) fprintf(stderr, gettext("%s: bad root\n"), "order_tree");
		return (-1);
	}
	rc = 0;
	for (n = troot->child; n; n = n->sibling) {
		if (c = hash_exchange(n)) {
			n->child = c->child;
			n->dn = c->dn;
			if (rc = order_tree(n))
				break;
		} else {
			b = &buckets[HASH(n->dnode_num)];
			n->hashnxt = b->nxt;
			b->nxt = n;
		}
	}
	return (rc);
}

static struct node *
hash_lookup(num)
	u_long num;
{
	struct bucket *b;
	register struct node *p;

	b = &buckets[HASH(num)];
	for (p = b->nxt; p != (struct node *)b; p = p->hashnxt)
		if (p->dnode_num == num) {
			return (p);
		}
	return (NULLNODE);
}

static struct node *
link_lookup(num)
	u_long num;
{
	struct bucket *b;
	register struct node *p;

	/*
	 * caller has a inode number (from a directory)
	 * that has no dnode.  See if there is another name (hard link)
	 * for this inode.
	 */
	b = &buckets[HASH(num)];
	for (p = b->nxt; p != (struct node *)b; p = p->hashnxt)
		if (p->dn && p->dn->dn_inode == num) {
			return (p);
		}
	return (NULLNODE);
}

static struct node *
hash_exchange(n)
	struct node *n;
{
	struct bucket *b;
	register struct node *p, *p1;

	b = &buckets[HASH(n->dnode_num)];
	p1 = NULLNODE;
	for (p = b->nxt; p != (struct node *)b; p = p->hashnxt) {
		if (p->dnode_num == n->dnode_num) {
			n->hashnxt = p->hashnxt;
			if (p1)
				p1->hashnxt = n;
			else
				b->nxt = n;
			return (p);
		}
		p1 = p;
	}
	return (NULLNODE);
}

static int
add_dnodes(fp, num, lnkfp)
	FILE *fp;
	int num;
	FILE *lnkfp;
{
	int cnt = 0;
	struct dnode *dnp;
	struct node *p;
	char linkbuf[MAXPATHLEN];

	dnp = NULL_DNODE;
	while (cnt < num) {
		if (dnp == NULL_DNODE) {
			dnp = alloc_dnode();
		}
		if (fread((char *)dnp, sizeof (struct dnode), 1, fp) != 1) {
			(void) fprintf(stderr, gettext("%s: %s error\n"),
				"add_dnodes", "fread");
			return (-1);
		}
		cnt++;
		if (S_ISLNK(dnp->dn_mode)) {
			if (dnp->dn_symlink < 0 ||
					dnp->dn_symlink > MAXPATHLEN) {
				(void) fprintf(stderr, gettext(
				    "%s: bad symlink\n"), "add_dnodes");
				return (-1);
			}
			if (fread((char *)linkbuf,
					(int)dnp->dn_symlink, 1, fp) != 1) {
				(void) fprintf(stderr, gettext(
				    "%s: %s error\n"), "add_dnodes", "fread");
				return (-1);
			}
		}
		if ((p = hash_lookup(dnp->dn_inode)) == NULLNODE) {
			(void) fprintf(stderr,
			    gettext("%s: cannot find %lu\n"),
				"add_dnodes", dnp->dn_inode);
			/* return (-1); */
			continue;
		}
		if (S_ISLNK(dnp->dn_mode)) {
			makelink(dnp, linkbuf, lnkfp);
		}
		p->dn = dnp;
		dnp = NULL_DNODE;
	}
	return (0);
}

static void
#ifdef __STDC__
makelink(struct dnode *dnp,
	const char *name,
	FILE *lnkfp)
#else
makelink(dnp, name, lnkfp)
	struct dnode *dnp;
	char *name;
	FILE *lnkfp;
#endif
{
	int len;

	len = dnp->dn_symlink;
	dnp->dn_symlink = linkoffset;
	(void) fputs(name, lnkfp);
	(void) putc('\0', lnkfp);
	linkoffset += len;
}

static int
process_tapes(fp, num, empty)
	FILE *fp;
	int num;
	int empty;
{
	int cnt = 0;
	struct tape_data *t, *last;
	struct node *n;

	last = (struct tape_data *)0;
	while (cnt < num) {
		t = alloc_tape();
		t->nxt = (struct tape_data *)0;
		if (fread((char *)&t->bu_tape,
				sizeof (struct bu_tape), 1, fp) != 1) {
			(void) fprintf(stderr, gettext("%s: %s error\n"),
				"process_tapes", "fread");
			return (-1);
		}

		if (empty) {
			/*
			 * XXX: empty dumps cannot span tapes, right?
			 */
			tapehead = t;
			(void) memcpy(&dumphead.dh_label[0][0],
					t->bu_tape.label, LBLSIZE);
			dumphead.dh_position = t->bu_tape.filenum;
			if (t->bu_tape.filenum == 1) {
				/*
				 * If the first file on tape was (re)written,
				 * we scratch any previous database
				 * information for this tape.
				 */
				(void) scratch_tape(t->bu_tape.label,
						dumphead.dh_time);
			}
			return (0);
		}

		/*
		 * use link_lookup() rather than hash_lookup() since we
		 * need to be sure we get a dnode here
		 */
		if ((n = link_lookup(t->bu_tape.first_inode)) == NULLNODE) {
			(void) fprintf(stderr, gettext(
			    "%s: cannot lookup node\n"), "process_tapes");
			return (-1);
		}

		/*
		 * there can be a break in the inode number sequence
		 * between the dumping of dirs and the dumping of
		 * regular files.  We don't want to be tricked into
		 * thinking that a dir appears on a later tape than
		 * it actually does...
		 */
		t->maxdirinode = 0;
		if (S_ISDIR(n->dn->dn_mode)) {
			/*
			 * first inode on tape is a directory.  Check
			 * the last one.  Use link_lookup() rather than
			 * hash_lookup() since we need to be sure that
			 * we get a dnode here.
			 */
			n = link_lookup(t->bu_tape.last_inode);
			if (n == NULLNODE) {
				(void) fprintf(stderr,
				    gettext("%s: %s error\n"),
					"process_tapes", "link_lookup");
				return (-1);
			}
			if (S_ISDIR(n->dn->dn_mode)) {
				/* last one is also a directory */
				t->maxdirinode = t->bu_tape.last_inode;
			} else {
				/* all directories are on this tape */
				t->maxdirinode = 0x7fffffff;
			}
		}
		if (last) {
			last->nxt = t;
			if (t->bu_tape.filenum != 1) {
				(void) fprintf(stderr, gettext(
				    "dump not first file on volume `%s'\n"),
					t->bu_tape.label);
			}
		} else {
			tapehead = t;
			(void) memcpy(&dumphead.dh_label[0][0],
					t->bu_tape.label, LBLSIZE);
			dumphead.dh_position = t->bu_tape.filenum;
		}
		last = t;
		cnt++;

		if (t->bu_tape.filenum == 1) {
			/*
			 * If the first file on tape was (re)written,
			 * we scratch any previous database information
			 * for this tape.
			 */
			(void) scratch_tape(t->bu_tape.label, dumphead.dh_time);
		}
	}
	return (0);
}

static void
locate_tape(dnp)
	struct dnode *dnp;
{
	register struct tape_data *t;
	register int count, first, last;

	for (t = tapehead, count = 0; t; t = t->nxt, count++) {
		first = t->bu_tape.first_inode;
		last = t->bu_tape.last_inode;
		if (S_ISDIR(dnp->dn_mode)) {
			if (t->maxdirinode == 0) {
				(void) fprintf(stderr, gettext(
				    "cannot find tape for directory inode\n"));
				return;
			}
			if (t->maxdirinode >= dnp->dn_inode) {
				dnp->dn_volid = count;
				if (last == dnp->dn_inode && t->nxt &&
						t->nxt->bu_tape.first_inode ==
							dnp->dn_inode) {
					dnp->dn_flags |= DN_MULTITAPE;
				}
				return;
			} else {
				continue;
			}
		}
		if (first <= dnp->dn_inode && last >= dnp->dn_inode) {
			dnp->dn_volid = count;
#if 0
/*
 * dn_vol_position will be filled in by dump.  It is likely to
 * be unused as long as we maintain media compatibility with the
 * old dump program.
 */
			dnp->dn_vol_position = t->bu_tape.filenum;
#endif

			if (last == dnp->dn_inode && t->nxt &&
				t->nxt->bu_tape.first_inode == dnp->dn_inode)
				dnp->dn_flags |= DN_MULTITAPE;
			return;
		}
	}
}

static int
#ifdef __STDC__
build_root(const char *path)
#else
build_root(path)
	char *path;
#endif
{
	char *p;
	struct node *n;
	char rootpath[MAXPATHLEN];

	(void) strcpy(rootpath, path);
	p = strrchr(rootpath, '/');
	if (!p) {
		(void) fprintf(stderr,
			gettext("%s: bad mount point\n"), "dir_root");
		return (-1);
	}
	root->namelen = strlen(p+1);
	if (root->namelen == 0) {
		root->namelen = 1;
		root->name = alloc_name(2);
		root->name[0] = '.';
		root->name[1] = '\0';
		return (0);
	}
	root->name = alloc_name(root->namelen+1);
	(void) strcpy(root->name, p+1);
	while (p != rootpath) {
		*p = '\0';
		p = strrchr(rootpath, '/');
		n = alloc_node();
		n->flags = 0;
		n->dn = NULL_DNODE;
		n->sibling = n->child = n->hashnxt = NULLNODE;
		n->namelen = strlen(p+1);
		n->name = alloc_name(n->namelen+1);
		(void) strcpy(n->name, p+1);
		n->child = root;
		root = n;
	}

	return (0);
}

static void
dump_dnodes(troot, parent_block, fp, pcfp)
	struct node *troot;
	u_long	parent_block;
	FILE *fp;
	FILE *pcfp;
{
	register struct node *p;
	static struct dnode dummy_dnode;
	struct dnode *dnp;

	for (p = troot; p; p = p->sibling) {
		if (p->dn == NULL_DNODE) {
			register struct node *linkp;
			/*
			 * check for a hard link.
			 */
			linkp = link_lookup(p->dnode_num);
			if (linkp != (struct node *)0 && linkp != p) {
				bcopy((char *)linkp->dn, (char *)&dummy_dnode,
						sizeof (struct dnode));
				/* make sure it gets in instance file too */
				p->dn = dnp = &dummy_dnode;
				locate_tape(p->dn);
			} else {
				/*
				 * we write a empty dnode for files in the
				 * tree which were not actually dumped so that
				 * we maintain hierarchy information for
				 * rebuild purposes.
				 *
				 * XXX: can this even happen?
				 */
				(void) bzero((char *)&dummy_dnode,
						sizeof (struct dnode));
				dnp = &dummy_dnode;
			}
		} else {
			locate_tape(p->dn);
			dnp = p->dn;
		}
		dnp->dn_filename = (u_long) pathoffset;
		dnp->dn_parent = parent_block;
		if (fwrite((char *)dnp, sizeof (struct dnode), 1, fp) != 1) {
			(void) fprintf(stderr, gettext(
				"%s: %s error\n"), "dump_dnodes", "fwrite");
		}
		p->dnode_num = ndnodes++;
		(void) fputs(p->name, pcfp);
		(void) putc('\0', pcfp);
		pathoffset += p->namelen+1;
		if (S_ISDIR(dnp->dn_mode))
			p->flags |= NODE_ISDIR;
	}

	for (p = troot; p; p = p->sibling)
		dump_dnodes(p->child, p->dnode_num, fp, pcfp);
}

static int
#ifdef __STDC__
output_dumpheader(char *header_name,
	const char *host,
	u_long dumpid)
#else
output_dumpheader(header_name, host, dumpid)
	char *header_name;
	char *host;
	u_long dumpid;
#endif
{
	FILE *fp;
	struct tape_data *t;

	(void) sprintf(header_name, "%s/%s%s.%lu", host, TEMP_PREFIX,
		HEADERFILE, dumpid);
	if ((fp = fopen(header_name, "w")) == NULL) {
		(void) fprintf(stderr,
			gettext("cannot create `%s'\n"), header_name);
		return (-1);
	}
	if (fchmod(fileno(fp), 0600) == -1) {
		(void) fprintf(stderr,
			gettext("cannot chmod `%s'\n"), header_name);
		(void) fclose(fp);
		return (-1);
	}
	if (fwrite((char *)&dumphead, sizeof (struct dheader), 1, fp) != 1) {
		(void) fprintf(stderr, gettext(
			"%s: %s error\n"), "output_dumpheader", "fwrite");
		(void) fclose(fp);
		return (-1);
	}
	if (dumphead.dh_ntapes > 1) {
		for (t = tapehead->nxt; t; t = t->nxt) {
			if (fwrite(t->bu_tape.label, LBLSIZE, 1, fp) != 1) {
				(void) fprintf(stderr, gettext(
				    "%s: %s volume label error\n"),
					"output_dumpheader", "fwrite");
				(void) fclose(fp);
				return (-1);
			}
		}
	}
	if (fflush(fp) != 0) {
		(void) fprintf(stderr, gettext("%s: %s failure\n"),
			"output_header", "fflush");
		return (-1);
	}
	(void) fsync(fileno(fp));
	(void) fclose(fp);
	return (0);
}

static void
dump_dir_and_instance(troot, dirblock, dfile_id)
	struct node *troot;
	u_long dirblock;
	u_long	dfile_id;
{
	struct dir_entry *ep;
	struct dir_block *bp;
	struct node *p;
	u_long instancerec;
	u_long	curdirblk;
	int dummyinstance;

	/*
	 * NOTE:
	 *
	 * We cannot reference dnode structures here because they have
	 * already been freed.  We can check the dnode pointer to see
	 * if one existed for this file, but we cannot dereference the
	 * pointer.
	 */
	curdirblk = dirblock;
	for (p = troot; p; p = p->sibling) {
		dummyinstance = 0;
		instancerec = NONEXISTENT_BLOCK;
		curdirblk = dirblock;
		ep = dir_name_getblock(&curdirblk, &bp, p->name, p->namelen);
		if (ep == NULL_DIRENTRY) {
			if (p->dn != NULL_DNODE || p->child) {
				instancerec =
					instance_newrec(NONEXISTENT_BLOCK);
				if (p->dn == NULL_DNODE)
					dummyinstance = 1;
			}
			curdirblk = dirblock;
			ep = dir_addent(&curdirblk, &bp, p->name,
						p->namelen, instancerec);
			if (ep == NULL_DIRENTRY) {
				(void) fprintf(stderr, gettext(
					"cannot add name `%s'\n"),
				    p->name);
				exit(1);
			}

		} else {
			instancerec = ep->de_instances;
			if ((p->dn != NULL_DNODE || p->child) &&
					instancerec == NONEXISTENT_BLOCK) {
				instancerec =
					instance_newrec(NONEXISTENT_BLOCK);
				if (p->dn == NULL_DNODE)
					dummyinstance = 1;
				if (dir_add_instance(curdirblk,
							ep, instancerec)) {
					(void) fprintf(stderr, gettext(
					    "cannot add instance\n"));
				}
			}
		}
		if (p->dn != NULL_DNODE && instancerec != NONEXISTENT_BLOCK) {
			(void) instance_addent(instancerec,
				dfile_id, p->dnode_num);
		} else if (dummyinstance) {
			/*
			 * we add an empty instance record for any
			 * non-dumped directory that has descendents
			 * (since we believe it could be a mount point
			 * and we will later get a dump that contains
			 * the mounted-on directory).  By having the
			 * instance record already allocated, we don't
			 * have to worry about updating the `.' and `..'
			 * directory entries later when the full
			 * hierarchy is in place.
			 */
			(void) instance_addent(instancerec,
				(u_long)0, (u_long)0);
		}

		if ((p->dn != NULL_DNODE && (p->flags & NODE_ISDIR)) ||
				p->child) {
			/*
			 * make a subdir for any entry that had a dnode
			 * and was a directory (even if it may be empty)
			 */
			if (ep->de_directory == NONEXISTENT_BLOCK) {
				if (dir_newsubdir(curdirblk, ep) == 0) {
					(void) fprintf(stderr, gettext(
					    "cannot make sub-directory\n"));
					exit(1);
				}
			}
		}

		if (p->child) {
			dump_dir_and_instance(p->child,
					ep->de_directory, dfile_id);
		}
	}
}

static int
#ifdef __STDC__
update_activetapes(const char *host,
	u_long dumpid)
#else
update_activetapes(host, dumpid)
	char *host;
	u_long dumpid;
#endif
{
	struct tape_data *t;
	u_long recnum, hostnum, inet_addr();
	char *p;

	/* extract host internet number from the passed string */
	if ((p = strchr(host, '.')) == NULL) {
		(void) fprintf(stderr, gettext(
		    "%s: cannot get hostnum\n"), "update_activetapes");
		return (-1);
	}
	hostnum = inet_addr(++p);
#ifdef NOTYET
	if (hostnum != dumphead.dh_netid) {
		(void) fprintf(stderr, gettext(
		    "%s: hostnum discrepancy\n"), "update_activetapes");
		return (-1);
	}
#endif

	if (tape_open(host)) {
		(void) fprintf(stderr, gettext(
			"%s: %s error\n"), "update_activetapes", "tape_open");
		return (-1);
	}

	for (t = tapehead; t; t = t->nxt) {
		if (tape_lookup(t->bu_tape.label, &recnum) == NULL_TREC) {
			recnum = tape_newrec(t->bu_tape.label,
					NONEXISTENT_BLOCK);
		}
		(void) tape_addent(recnum, hostnum, dumpid, t->bu_tape.filenum);
	}

	tape_close(host);
	return (0);
}

static	caddr_t	pool1;
#ifdef notdef
static	caddr_t	pool1end;
#endif
static	caddr_t	pool2;
static	caddr_t	pool2end;

#ifdef PERFSTATS
static int filecnt;
#endif

static struct   dnode *dnodebase, *nextdnode;
static struct	node *nodebase, *nextnode;
static struct	tape_data *tapebase, *nexttape;
static caddr_t	namebase, nextname;

static int
#ifdef __STDC__
getmemory(long filesize,		/* size of data file */
	const struct bu_header *hd)	/* data file header */
#else
getmemory(filesize, hd)
	long filesize;			/* size of data file */
	struct bu_header *hd;		/* data file header */
#endif
{
	int pool1size, pool2size;
	int nodecnt;
	unsigned int highbit = 0x80000000U;
	register int i;

	/*
	 * allocate space for:
	 *
	 *	names + dnodes + tape info  (based on size of data file)
	 *	stdio buffers		(fixed size)
	 *	tree data structures	(based on # of entries in data file)
	 *
	 * We allocate dnodes and hash buckets together since they may
	 * be freed before the update of dir and instance files.
	 *
	 * Names, tree structures and tape structures are allocated in
	 * a separate pool since they may not be freed until all is
	 * complete...
	 */
	dnodebase = nextdnode = (struct dnode *)0;
	nodebase = nextnode = (struct node *)0;
	tapebase = nexttape = (struct tape_data *)0;
	namebase = nextname = (caddr_t)0;

	pool1size = (hd->dnode_cnt * sizeof (struct dnode));

	/*
	 * nearest power of 2 which is less than the number of files
	 * we're processing.  This insures a max of two entries per
	 * bucket...
	 */
	for (i = 0; i < 31; i++) {
		hashsize = highbit >> i;
		if (hd->name_cnt & hashsize)
			break;
	}
	if (hashsize & (hashsize-1)) {
		(void) fprintf(stderr, gettext("bad hash size\n"));
		exit(1);
	}

	/*
	 * XXX: should we fix an upper bound on number of buckets?
	 * Note that buckets are only 4 bytes each...
	 */

#ifdef PERFSTATS
	filecnt = hd->name_cnt;
#endif

	pool1size += (hashsize * sizeof (struct bucket));
	if ((pool1 = (caddr_t)malloc((unsigned)pool1size)) == NULL) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "getmemory");
		exit(1);
	}
	/*LINTED [pool1 was malloc'ed]*/
	dnodebase = (struct dnode *)pool1;
	buckets = (struct bucket *)(pool1 +
			/*LINTED [pool1 was malloc'ed]*/
			(hd->dnode_cnt * sizeof (struct dnode)));
#ifdef notdef
	pool1end = pool1 + pool1size;
#endif

	for (i = 0; i < hashsize; i++)
		buckets[i].nxt = (struct node *)&buckets[i];

	/* allocate some extra nodes in case `build_root' needs them */
	nodecnt = hd->name_cnt + 10;
	pool2size = sizeof (struct node) * nodecnt;
	pool2size += (filesize - (hd->dnode_cnt * sizeof (struct dnode)));

	pool2 = (caddr_t) malloc((unsigned)pool2size);
	if (pool2 == NULL) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "getmemory");
		exit(1);
	}

	/*LINTED [pool2 was malloc'ed]*/
	nodebase = (struct node *)pool2;
	tapebase = (struct tape_data *)((char *)nodebase +
			/*LINTED [pool2 was malloc'ed]*/
			(nodecnt * sizeof (struct node)));
	namebase = (char *)tapebase +
			(hd->tape_cnt * sizeof (struct tape_data));
	pool2end = pool2 + pool2size;
	return (0);
}

static void
#ifdef __STDC__
freemem1(void)
#else
freemem1()
#endif
{
	free(stdiobuf1);
	free(pool1);
}

static void
#ifdef __STDC__
freemem2(void)
#else
freemem2()
#endif
{
	free(pool2);
}

static void
#ifdef __STDC__
alloc_iobuffers(void)
#else
alloc_iobuffers()
#endif
{
	stdiobuf1 = (char *)malloc(2*STDIO_BUFSIZE);
	if (stdiobuf1 == NULL) {
		(void) fprintf(stderr, gettext(
			"%s: cannot allocate io buffers\n"), "alloc_iobuffers");
		exit(1);
	}
	stdiobuf2 = stdiobuf1 + STDIO_BUFSIZE;
}

static struct dnode *
#ifdef __STDC__
alloc_dnode(void)
#else
alloc_dnode()
#endif
{

	if (!nextdnode)
		nextdnode = dnodebase;

#if 1	/* DEBUG */
	if (((char *)nextdnode + sizeof (struct dnode)) > (char *)buckets) {
		(void) fprintf(stderr, gettext("%s: too many dnodes\n"),
			"alloc_dnode");
		exit(1);
	}
#endif
	return (nextdnode++);
}

static struct node *
#ifdef __STDC__
alloc_node(void)
#else
alloc_node()
#endif
{

	if (!nextnode)
		nextnode = nodebase;

#if 1 /* DEBUG */
	if (((char *)nextnode + sizeof (struct node)) > (char *)tapebase) {
		(void) fprintf(stderr, gettext("%s: too many nodes\n"),
			"alloc_dnode");
		exit(1);
	}
#endif
	return (nextnode++);
}

static struct tape_data *
#ifdef __STDC__
alloc_tape(void)
#else
alloc_tape()
#endif
{

	if (!nexttape)
		nexttape = tapebase;

#if 1 /* DEBUG */
	if (((char *)nexttape + sizeof (struct tape_data)) > namebase) {
		(void) fprintf(stderr, gettext("%s: too many tapes\n"),
			"alloc_tape");
		exit(1);
	}
#endif
	return (nexttape++);
}

static char *
alloc_name(size)
	int size;
{
	caddr_t ret;

	if (!nextname)
		nextname = namebase;

#if 1 /* DEBUG */
	if ((nextname + size) > pool2end) {
		(void) fprintf(stderr, gettext("%s: too many names\n"),
			"alloc_name");
		exit(1);
	}
#endif
	ret = nextname;
	nextname += size;
	return (ret);
}

#ifdef PERFSTATS
static time_t starttime;
#endif

static void
#ifdef __STDC__
start_msg(void)
#else
start_msg()
#endif
{
	char msg[MAXMSGLEN];

#ifdef PERFSTATS
	starttime = time(0);
#endif
	(void) sprintf(msg, gettext(
		"Started database update for level %lu dump of %s:%s"),
		dumphead.dh_level, dumphead.dh_host, dumphead.dh_mnt);
	(void) oper_send(DBOPER_TTL, LOG_NOTICE, DBOPER_FLAGS, msg);
	(void) fprintf(stderr, "%s\n", msg);
}

static void
#ifdef __STDC__
success_msg(const char *host)
#else
success_msg(host)
	char *host;
#endif
{
	char msg[MAXMSGLEN];

	update_stats(host);
	(void) sprintf(msg, gettext(
		"Completed database update for level %lu dump of %s:%s"),
		dumphead.dh_level, dumphead.dh_host, dumphead.dh_mnt);
	(void) oper_send(DBOPER_TTL, LOG_NOTICE, DBOPER_FLAGS, msg);
	(void) fprintf(stderr, "%s\n", msg);
}

/*ARGSUSED*/
static void
#ifdef __STDC__
update_stats(const char *host)
#else
update_stats(host)
	char *host;
#endif
{
#ifdef PERFSTATS
	char statfile[256];
	struct rusage r;
	FILE *fp;
	time_t now;

	(void) strcpy(statfile, host);
	strcat(statfile, "/.updatestats");
	if (getrusage(RUSAGE_SELF, &r) == -1) {
		perror("getrusage");
		return;
	}

	if ((fp = fopen(statfile, "a")) == NULL) {
		(void) fprintf(stderr,
			gettext("cannot open/append `%s'\n"), statfile);
		return;
	}
	now = time(0);
	(void) fprintf(fp,
	    "-----------------------------------------------------\n");
	(void) fprintf(fp, gettext("Start: %s"), lctime(&starttime));
	(void) fprintf(fp, gettext("Finish: %s"), lctime(&now));
	(void) fprintf(fp, gettext("Elapsed Time: %d seconds\n"),
		now - starttime);
	(void) fprintf(fp, gettext("Number of files processed: %d\n"), filecnt);
	(void) fprintf(fp, gettext("User time: %d seconds\n"),
		r.ru_utime.tv_sec);
	(void) fprintf(fp, gettext("System time: %d seconds\n"),
		r.ru_stime.tv_sec);
	(void) fprintf(fp, gettext("Voluntary Context Switches: %d\n"),
		r.ru_nvcsw);
	(void) fprintf(fp, gettext("Involuntary Context Switches: %d\n"),
		r.ru_nivcsw);
	(void) fprintf(fp, gettext("Major Page Faults: %d\n"), r.ru_majflt);
	(void) fprintf(fp,
	    "-----------------------------------------------------\n");
	(void) fclose(fp);
#endif
}

static void
#ifdef __STDC__
fail_msg(void)
#else
fail_msg()
#endif
{
	char msg[MAXMSGLEN];
	struct statfs buf;

	if (dumphead.dh_host[0])
		(void) sprintf(msg, gettext(
			"Database update failed for level %lu dump of %s:%s"),
			dumphead.dh_level, dumphead.dh_host, dumphead.dh_mnt);
	else
		(void) sprintf(msg, gettext("Database update failed"));
	(void) oper_send(DBOPER_TTL, LOG_WARNING, DBOPER_FLAGS, msg);
	(void) fprintf(stderr, "%s\n", msg);

	if (statfs(".", &buf) != -1) {
		if (buf.f_bfree == 0 || buf.f_bavail == 0) {
			(void) fprintf(stderr, gettext(
				"Database file system is full\n"));
			(void) oper_send(DBOPER_TTL, LOG_ALERT, DBOPER_FLAGS,
				gettext("Database file system is full"));
		}
	}
}
