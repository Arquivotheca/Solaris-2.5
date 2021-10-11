/*LINTLIBRARY*/
#ident	"@(#)dbserv_xdr.c 1.18 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <rpc/rpc.h>
#include <libintl.h>
#include <database/dbserv.h>
#include <database/backupdb.h>
#include <database/handles.h>
#include <database/dir.h>
#include <database/instance.h>
#include <database/dnode.h>
#include <database/header.h>
#include <database/activetape.h>
#include <database/batchfile.h>

#ifdef __STDC__
static int file_header(XDR *, FILE *, struct bu_header *);
static int do_names(XDR *, FILE *, int num, int *);
static int do_dnodes(XDR *, FILE *, int);
static int do_header(XDR *, FILE *);
static int do_tapes(XDR *, FILE *, int);
static bool_t xdr_dirblock(XDR *, struct dir_block *);
static bool_t xdr_instrec(XDR *, struct instance_record *, int);
static bool_t xdr_dnodeblk(XDR *, struct dnode *);
static bool_t xdr_dnode(XDR *, struct dnode *);
static bool_t xdr_acttape(XDR *, struct active_tape *);
#else
static int file_header();
static int do_names();
static int do_dnodes();
static int do_header();
static int do_tapes();
static bool_t xdr_dirblock();
static bool_t xdr_instrec();
static bool_t xdr_dnodeblk();
static bool_t xdr_dnode();
static bool_t xdr_acttape();
#endif

bool_t
xdr_nametype(xdrs, objp)
	XDR *xdrs;
	nametype *objp;
{
	if (!xdr_string(xdrs, objp, MAXDBNAMELEN)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_process(xdrs, objp)
	XDR *xdrs;
	process *objp;
{
	if (!xdr_int(xdrs, &objp->handle)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->filesize)) {
		return (FALSE);
	}
	return (TRUE);
}

xdr_datafile(xdrs, bp)
	XDR *xdrs;
	struct blast_arg *bp;
{
	FILE *fp;
	int myhandle;
	int writing = 0;
	struct bu_header buh;
	int dnodecnt;

	if (xdrs->x_op == XDR_FREE) {
		return (1);
	} else if (xdrs->x_op == XDR_ENCODE) {
		/*
		 * client passes the file server's file handle as
		 * well as the file pointer associated with a open file.
		 */
		myhandle = bp->handle;
		if (!xdr_u_long(xdrs, (u_long *)&myhandle))
			return (FALSE);
		fp = bp->fp;
	} else if (xdrs->x_op == XDR_DECODE) {
		/*
		 * server gets a file handle as its first piece of data,
		 * followed by the actual data file.  Use the handle to
		 * locate a file pointer where we will write the data.
		 */
		struct file_handle *h;
		char filename[256];

		if (!xdr_int(xdrs, &myhandle))
			return (FALSE);
		if ((h = handle_lookup(myhandle)) == NULL_HANDLE)
			return (FALSE);
		(void) sprintf(filename, "%s/%s%s.%d", h->host,
				TEMP_PREFIX, UPDATE_FILE, myhandle);
		if ((fp = fopen(filename, "w")) == NULL) {
			if (mkdir(h->host, 0700) == -1) {
				return (FALSE);
			}
			if ((fp = fopen(filename, "w")) == NULL) {
				return (FALSE);
			}
		}
		if (fchmod(fileno(fp), 0600) == -1) {
			(void) fclose(fp);
			(void) unlink(filename);
			return (FALSE);
		}
		writing = 1;
	}

	if (file_header(xdrs, fp, &buh)) {
		if (writing)
			(void) fclose(fp);
		return (FALSE);
	}
	if (do_header(xdrs, fp)) {
		if (writing)
			(void) fclose(fp);
		return (FALSE);
	}
	dnodecnt = buh.dnode_cnt;
	if (do_names(xdrs, fp, (int) buh.name_cnt, &dnodecnt)) {
		if (writing)
			(void) fclose(fp);
		return (FALSE);
	}
	if (do_dnodes(xdrs, fp, dnodecnt)) {
		if (writing)
			(void) fclose(fp);
		return (FALSE);
	}
	if (do_tapes(xdrs, fp, (int) buh.tape_cnt)) {
		if (writing)
			(void) fclose(fp);
		return (FALSE);
	}
	if (file_header(xdrs, fp, &buh)) {
		if (writing)
			(void) fclose(fp);
		return (FALSE);
	}

	if (writing) {
		(void) fflush(fp);
		(void) fsync(fileno(fp));
		(void) fclose(fp);
	}
	return (TRUE);
}

static int
file_header(xdrs, fp, buhp)
	XDR *xdrs;
	FILE *fp;
	struct bu_header *buhp;
{
	if (xdrs->x_op == XDR_ENCODE) {
		if (fread((char *)buhp, sizeof (struct bu_header),
				1, fp) != 1) {
			return (1);
		}
	}
	if (!xdr_bu_header(xdrs, buhp))
		return (1);
	if (xdrs->x_op == XDR_DECODE) {
		if (fwrite((char *)buhp, sizeof (struct bu_header),
					1, fp) != 1) {
			return (1);
		}
	}
	return (0);
}

static int
do_names(xdrs, fp, num, dnodecnt)
	XDR *xdrs;
	FILE *fp;
	int num;
	int *dnodecnt;
{
	int cnt = 0;
	struct bu_name bun;
	char name[MAXDBNAMELEN], *p;
	struct dnode dnode;

	while (cnt < num) {
		if (xdrs->x_op == XDR_ENCODE) {
			if (fread((char *)&bun, sizeof (struct bu_name),
					1, fp) != 1) {
				return (1);
			}
			if (bun.namelen) {
				if (bun.namelen >= MAXDBNAMELEN)
					return (1);
				if (fread(name, (int)bun.namelen, 1, fp) != 1) {
					return (1);
				}
			}
			if (bun.type == DIRECTORY) {
				if (fread((char *)&dnode, sizeof (struct dnode),
							1, fp) != 1) {
					return (1);
				}
			}
			cnt++;
		}
		if (!xdr_bu_name(xdrs, &bun))
			return (1);

		if (bun.namelen) {
			p = name;
			if (!xdr_string(xdrs, &p, MAXDBNAMELEN))
				return (1);
		}

		if (bun.type == DIRECTORY) {
			if (!xdr_dnode(xdrs, &dnode))
				return (1);
			(*dnodecnt)--;
		}

		if (xdrs->x_op == XDR_DECODE) {
			if (fwrite((char *)&bun, sizeof (struct bu_name),
					1, fp) != 1) {
				return (1);
			}
			if (bun.namelen) {
				if (fwrite(name, (int)bun.namelen,
						1, fp) != 1) {
					return (1);
				}
			}
			if (bun.type == DIRECTORY) {
				if (fwrite((char *)&dnode,
						sizeof (struct dnode),
						1, fp) != 1) {
					return (1);
				}
			}
			cnt++;
		}
	}
	return (0);
}

static int
do_dnodes(xdrs, fp, num)
	XDR *xdrs;
	FILE *fp;
	int num;
{
	int cnt = 0;
	int len;
	struct dnode dn;
	char linkval[MAXPATHLEN], *p;

	while (cnt < num) {
		if (xdrs->x_op == XDR_ENCODE) {
			if (fread((char *)&dn, sizeof (struct dnode),
					1, fp) != 1)
				return (1);
			cnt++;
			if (S_ISLNK(dn.dn_mode)) {
				/*
				 * A zero-length symbolic link does not put
				 * any data into the database temporary file.
				 */
				if (dn.dn_size > 0) {
					len = dn.dn_symlink;
					if (fread(linkval, len, 1, fp) != 1)
						return (1);
				} else {
					/*
					 * For a zero-length symbolic link, we
					 * just send a NULL byte, with a
					 * symlink size of 1.  This provides
					 * for maximum interoperability.
					 */
					linkval[0] = '\0';
					dn.dn_symlink = 1;
				}
			}
		}
		if (!xdr_dnode(xdrs, &dn))
			return (1);
		if (S_ISLNK(dn.dn_mode)) {
			p = linkval;
			if (!xdr_string(xdrs, &p, MAXPATHLEN))
				return (1);
			if (strlen(linkval)+1 != dn.dn_symlink) {
				syslog(LOG_WARNING, gettext(
				"Symlink name length mismatch: inode %d"),
				    dn.dn_inode);
				return (1);
			}
		}
		if (xdrs->x_op == XDR_DECODE) {
			if (fwrite((char *)&dn, sizeof (struct dnode),
					1, fp) != 1)
				return (1);
			if (S_ISLNK(dn.dn_mode)) {
				if (fwrite(linkval, (int)dn.dn_symlink,
						1, fp) != 1)
					return (1);
			}
			cnt++;
		}
	}
	return (0);
}

static int
do_header(xdrs, fp)
	XDR *xdrs;
	FILE *fp;
{
	struct dheader dh;

	if (xdrs->x_op == XDR_ENCODE) {
		if (fread((char *)&dh, sizeof (struct dheader), 1, fp) != 1) {
			return (1);
		}
	}
	if (!xdr_dheader(xdrs, &dh))
		return (1);
	if (xdrs->x_op == XDR_DECODE) {
		if (fwrite((char *)&dh, sizeof (struct dheader), 1, fp) != 1) {
			return (1);
		}
	}
	return (0);
}

static int
do_tapes(xdrs, fp, num)
	XDR *xdrs;
	FILE *fp;
	int num;
{
	int cnt = 0;
	struct bu_tape but;

	while (cnt < num) {
		if (xdrs->x_op == XDR_ENCODE) {
			if (fread((char *)&but, sizeof (struct bu_tape),
					1, fp) != 1)
				return (1);
			cnt++;
		}
		if (!xdr_bu_tape(xdrs, &but))
			return (1);
		if (xdrs->x_op == XDR_DECODE) {
			if (fwrite((char *)&but, sizeof (struct bu_tape),
					1, fp) != 1)
				return (1);
			cnt++;
		}
	}
	return (0);
}

bool_t
xdr_bu_header(xdrs, objp)
	XDR *xdrs;
	struct bu_header *objp;
{
	if (!xdr_u_long(xdrs, &objp->name_cnt))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->dnode_cnt))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->tape_cnt))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_bu_name(xdrs, objp)
	XDR *xdrs;
	struct bu_name *objp;
{
	if (!xdr_u_long(xdrs, &objp->inode))
		return (FALSE);

	/*
	 * XXX: lint lib defines arg2 as (char *)
	 */
	if (!xdr_u_char(xdrs, (u_char *)&objp->type))
		return (FALSE);
	if (!xdr_u_short(xdrs, &objp->namelen))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_bu_tape(xdrs, objp)
	XDR *xdrs;
	struct bu_tape *objp;
{
	u_int  labelsize = LBLSIZE;
	char *p = objp->label;

	if (!xdr_bytes(xdrs, &p, &labelsize, LBLSIZE))
		return (FALSE);
	if (labelsize != LBLSIZE)
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->first_inode))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->last_inode))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->filenum))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_blkread(xdrs, objp)
	XDR *xdrs;
	struct blk_readargs *objp;
{
	if (!xdr_string(xdrs, &objp->host, MAXDBNAMELEN)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->recnum)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->blksize)) {
		return (FALSE);
	}
	if (!xdr_long(xdrs, &objp->cachetime)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_dnodeargs(xdrs, objp)
	XDR *xdrs;
	struct dnode_readargs *objp;
{
	if (!xdr_string(xdrs, &objp->host, MAXDBNAMELEN)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->dumpid)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->recnum)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_headerargs(xdrs, objp)
	XDR *xdrs;
	struct header_readargs *objp;
{
	if (!xdr_string(xdrs, &objp->host, MAXDBNAMELEN))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->dumpid))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_fsheaderargs(xdrs, objp)
	XDR *xdrs;
	struct fsheader_readargs *objp;
{
	if (!xdr_string(xdrs, &objp->host, MAXDBNAMELEN))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->mntpt, MAXDBNAMELEN))
		return (FALSE);
	if (!xdr_long(xdrs, &objp->time))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_tapeargs(xdrs, objp)
	XDR *xdrs;
	struct tape_readargs *objp;
{
	if (!xdr_tapelabel(xdrs, &objp->label))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_dbfindargs(xdrs, objp)
	XDR *xdrs;
	struct db_findargs *objp;
{
	u_int size;
	int *p;

	if (!xdr_string(xdrs, &objp->host, MAXDBNAMELEN))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->opaque_mode))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->arg, MAXDBNAMELEN))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->expand))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->curdir, MAXDBNAMELEN))
		return (FALSE);
	if (!xdr_long(xdrs, &objp->timestamp))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->myhost, MAXDBNAMELEN))
		return (FALSE);
	if (!xdr_long(xdrs, &objp->uid))
		return (FALSE);
	if (!xdr_long(xdrs, &objp->gid))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->ngroups))
		return (FALSE);
	size = objp->ngroups;
	p = objp->gidlist;
	if (xdrs->x_op != XDR_FREE) {
		/*
		 * this array is not dynamically allocated -- don't
		 * attempt to free it.
		 */
		if (!xdr_array(xdrs, (caddr_t *)&p, &size,
		    MAXGROUPS, sizeof (int), xdr_int))
			return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_tapelistargs(xdrs, objp)
	XDR *xdrs;
	struct tapelistargs *objp;
{
	if (!xdr_string(xdrs, &objp->label, MAXDBNAMELEN))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->verbose))
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_dirread(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	struct dir_block *bp;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);
	/*LINTED [retdata properly aligned]*/
	bp = (struct dir_block *)objp->retdata;
	if (bp && objp->readrc == DBREAD_SUCCESS) {
		if (!xdr_dirblock(xdrs, bp))
			return (FALSE);
	}

	return (TRUE);
}

static bool_t
xdr_dirblock(xdrs, objp)
	XDR *xdrs;
	struct dir_block *objp;
{
	struct dir_entry *ep;
	char *p;

	if (!xdr_u_long(xdrs, &objp->db_next))
		return (FALSE);
	if (!xdr_u_short(xdrs, &objp->db_flags))
		return (FALSE);
	if (!xdr_u_short(xdrs, &objp->db_spaceavail))
		return (FALSE);
	/*LINTED [db_data properly aligned]*/
	ep = (struct dir_entry *)objp->db_data;
	/*LINTED [db_data properly aligned]*/
	while (ep != DE_END(objp)) {
		if (!xdr_u_long(xdrs, &ep->de_instances))
			return (FALSE);
		if (!xdr_u_long(xdrs, &ep->de_directory))
			return (FALSE);
		if (!xdr_u_short(xdrs, &ep->de_name_len))
			return (FALSE);
		p = ep->de_name;
		if (!xdr_string(xdrs, &p, MAXDBNAMELEN))
			return (FALSE);
		ep = DE_NEXT(ep);
	}
	return (TRUE);
}

bool_t
xdr_instread(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	struct instance_record *ip;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->blksize))
		return (FALSE);
	/*LINTED [retdata properly aligned]*/
	ip = (struct instance_record *)objp->retdata;
	if (ip && objp->readrc == DBREAD_SUCCESS) {
		if (!xdr_instrec(xdrs, ip, objp->blksize))
			return (FALSE);
	}

	return (TRUE);
}

static bool_t
xdr_instrec(xdrs, objp, recsize)
	XDR *xdrs;
	struct instance_record *objp;
	int recsize;
{
	register int i;
	int nentries;

	nentries = (recsize - sizeof (u_long)) / sizeof (struct instance_entry);

	if (!xdr_u_long(xdrs, &objp->ir_next))
		return (FALSE);
	for (i = 0; i < nentries; i++) {
		if (!xdr_u_long(xdrs, &objp->i_entry[i].ie_dumpid))
			return (FALSE);
		if (!xdr_u_long(xdrs, &objp->i_entry[i].ie_dnode_index))
			return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_dnodeblkread(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	struct dnode *dp;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);

	/*LINTED [retdata properly aligned]*/
	dp = (struct dnode *)objp->retdata;
	if (dp && objp->readrc == DBREAD_SUCCESS) {
		if (!xdr_dnodeblk(xdrs, dp))
			return (FALSE);
	}
	return (TRUE);
}

static bool_t
xdr_dnodeblk(xdrs, objp)
	XDR *xdrs;
	struct dnode *objp;
{
	register int i;
	register struct dnode *p;

	for (i = 0, p = objp; i < DNODE_READBLKSIZE; i++, p++) {
		if (!xdr_dnode(xdrs, p))
			return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_dnoderead(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	struct dnode *dp;
	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);

	/*LINTED [retdata properly aligned]*/
	dp = (struct dnode *)objp->retdata;
	if (dp && objp->readrc == DBREAD_SUCCESS) {
		if (!xdr_dnode(xdrs, dp))
			return (FALSE);
	}
	return (TRUE);
}

static bool_t
xdr_dnode(xdrs, objp)
	XDR *xdrs;
	struct dnode *objp;
{
	if (!xdr_u_long(xdrs, &objp->dn_mode)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->dn_uid)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->dn_gid)) {
		return (FALSE);
	}
	if (!xdr_long(xdrs, &objp->dn_size)) {
		return (FALSE);
	}
	if (!xdr_long(xdrs, &objp->dn_atime)) {
		return (FALSE);
	}
	if (!xdr_long(xdrs, &objp->dn_mtime)) {
		return (FALSE);
	}
	if (!xdr_long(xdrs, &objp->dn_ctime)) {
		return (FALSE);
	}
	if (!xdr_long(xdrs, &objp->dn_blocks)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->dn_flags)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->dn_filename)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->dn_parent)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->dn_volid)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->dn_vol_position)) {
		return (FALSE);
	}
	if (!xdr_u_long(xdrs, &objp->dn_inode)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_dheaderread(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	struct dheader *dhp;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);

	/*LINTED [retdata properly aligned]*/
	dhp = (struct dheader *)objp->retdata;
	if (dhp && objp->readrc == DBREAD_SUCCESS) {
		if (!xdr_dheader(xdrs, dhp))
			return (FALSE);
	}
	return (TRUE);
}

/* static */
bool_t
xdr_dheader(xdrs, objp)
	XDR *xdrs;
	struct dheader *objp;
{
	char *p;
	u_int labelsize = LBLSIZE;

	p = objp->dh_host;
	if (!xdr_string(xdrs, &p, MAXDBNAMELEN))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->dh_netid))
		return (FALSE);
	p = objp->dh_dev;
	if (!xdr_string(xdrs, &p, MAXDBNAMELEN))
		return (FALSE);
	p = objp->dh_mnt;
	if (!xdr_string(xdrs, &p, MAXDBNAMELEN))
		return (FALSE);
	if (!xdr_long(xdrs, &objp->dh_time))
		return (FALSE);
	if (!xdr_long(xdrs, &objp->dh_prvdumptime))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->dh_level))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->dh_flags))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->dh_position))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->dh_ntapes))
		return (FALSE);
	p = &objp->dh_label[0][0];
	if (!xdr_bytes(xdrs, &p, &labelsize, LBLSIZE))
		return (FALSE);
	if (labelsize != LBLSIZE)
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_fullheaderread(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	struct dheader *dhp;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);

	/*LINTED [retdata properly aligned]*/
	dhp = (struct dheader *)objp->retdata;
	if (dhp && objp->readrc == DBREAD_SUCCESS) {
		if (!xdr_fullheader(xdrs, dhp))
			return (FALSE);
	}
	return (TRUE);
}

/* static */
bool_t
xdr_fullheader(xdrs, objp)
	XDR *xdrs;
	struct dheader *objp;
{
	char *p;
	register int i;
	u_int labelsize = LBLSIZE;

	if (!xdr_dheader(xdrs, objp))
		return (FALSE);

	for (i = 1; i < objp->dh_ntapes; i++) {
		p = objp->dh_label[i];
		if (!xdr_bytes(xdrs, &p, &labelsize, LBLSIZE))
			return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_acttaperead(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	struct active_tape *atp;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);

	/*LINTED [retdata properly aligned]*/
	atp = (struct active_tape *)objp->retdata;
	if (atp && objp->readrc == DBREAD_SUCCESS) {
		if (!xdr_acttape(xdrs, atp))
			return (FALSE);
	}
	return (TRUE);
}

static bool_t
xdr_acttape(xdrs, objp)
	XDR *xdrs;
	struct active_tape *objp;
{
	char *p;
	register int i;

	if (!xdr_u_long(xdrs, &objp->tape_next))
		return (FALSE);
	p = objp->tape_label;
	if (!xdr_tapelabel(xdrs, &p))
		return (FALSE);
	if (!xdr_u_long(xdrs, &objp->tape_status))
		return (FALSE);
	for (i = 0; i < DUMPS_PER_TAPEREC; i++) {
		if (!xdr_u_long(xdrs, &objp->dumps[i].host))
			return (FALSE);
		if (!xdr_u_long(xdrs, &objp->dumps[i].dump_id))
			return (FALSE);
		if (!xdr_u_long(xdrs, &objp->dumps[i].tapepos))
			return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_tapelabel(xdrs, objp)
	XDR *xdrs;
	char **objp;
{
	u_int labelsize = LBLSIZE;

	if (!xdr_bytes(xdrs, objp, &labelsize, LBLSIZE))
		return (FALSE);
	if (labelsize != LBLSIZE)
		return (FALSE);
	return (TRUE);
}

bool_t
xdr_linkval(xdrs, objp)
	XDR *xdrs;
	struct readdata *objp;
{
	char *p;

	if (!xdr_int(xdrs, &objp->readrc))
		return (FALSE);

	p = objp->retdata;
	if (p && objp->readrc == DBREAD_SUCCESS) {
		if (!xdr_string(xdrs, &p, MAXPATHLEN))
			return (FALSE);
	}

	return (TRUE);
}
