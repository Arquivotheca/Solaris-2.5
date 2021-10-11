/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)label_dos.c	1.26	95/01/30 SMI"

#include	<stdio.h>
#include	<errno.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<fcntl.h>
#include	<ctype.h>
#include	<string.h>

#include	<sys/vtoc.h>
#include	<sys/dkio.h>
#include	<sys/dklabel.h>
#include	<sys/fs/pc_label.h>
#include	<sys/fs/pc_fs.h>
#include	<sys/fs/pc_dir.h>

#include	"vold.h"



/*
 * This is the dos label driver.  It attempts to interpret the
 * dos lable on a disk.
 */

/* fwd declarations */
static bool_t 		dos_compare(label *, label *);
static enum laread_res	dos_read(int, label *, struct devs *dp);
static void		dos_setup(vol_t *);
static char		*dos_key(label *);
static void		dos_xdr(label *, enum xdr_op, void **);

/*
 * NOTE: most DOS defs come from <sys/fs/pc_*.h>, to avoid having to
 *  recompile them
 */
#define	DOS_NAMELEN_REG		PCFNAMESIZE
#define	DOS_NAMELEN_EXT		PCFEXTSIZE
#define	DOS_NAMELEN		(DOS_NAMELEN_REG + DOS_NAMELEN_EXT)

/*
 * this is the datastructure that we keep around for each dos
 * label to allow us to identify it.
 */
struct dos_label {
	u_short	dos_version;	/* version of this structure for db */
	u_long	dos_lcrc;			/* crc of label */
	u_long	dos_magic;			/* pseudo rand # from label */
	uchar_t	dos_volname[DOS_NAMELEN+1];	/* name from label */
};

#define	DOS_VERSION	1
#define	DOS_SIZE	sizeof (struct dos_label)

/* Begin definitions from DOS book... */

/*
 * Number of bytes that contain the "volume header" on a dos disk
 * at sector 0.  Note: it's less on earlier version of DOS, but never more.
 */
#define	DOS_LABLEN	0x3e

/*
 * Since we can't read anything but a multiple of sector size out of
 * the raw device, this is the amount that we read.
 *
 * NOTE: sector size is 512 on most formats, but is 1024 on some
 */
#define	DOS_READLEN	(PC_SECSIZE * 2)

/*
 * Offset in the volume header of the pseudo random id.  This is only
 * valid for dos version 4.0 and later.  It took them that long to
 * figure it out.  So, if we look at this and find it to be zero,
 * we just take the time(2) and poke it back out there.
 */
#define	DOS_ID_OFF	0x27

/*
 * Offset in the volume header of the ascii name of the volume. This is
 * only valid for dos 4.0 and later.  We should also read the first
 * FAT and slurp a name out of there, if we can.
 */
#define	DOS_NAME_OFF	0x2b

/*
 * The first byte of a DOS format disk is either 0xe9 or 0xeb.  Why
 * do they have two?  I don't know, and I don't really care, they
 * just do.
 */
#define	DOS_MAGIC1	DOS_ID1
#define	DOS_MAGIC2	DOS_ID2a

#define	DOS_MAGIC_OFF	0x0

/*
 * location/length of the OEM name and version
 */
#define	DOS_OEM_JUNK	0x3
#define	DOS_OEM_LENGTH	8

/*
 * OEM name/version of those lovely NEC 2.0 floppies
 */
#define	DOS_OEM_NEC2	"NEC 2.00"

/* End definitions from DOS book */

static struct label_loc	dos_labelloc = { 0, DOS_LABLEN };

static struct labsw	doslabsw = {
	dos_key, 	/* l_key */
	dos_compare, 	/* l_compare */
	dos_read, 	/* l_read */
	NULL, 		/* l_write */
	dos_setup, 	/* l_setup */
	dos_xdr, 	/* l_xdr */
	DOS_SIZE, 	/* l_size */
	DOS_SIZE, 	/* l_xdrsize */
	PCFS_LTYPE,	/* l_ident */
	1,		/* l_nll */
	&dos_labelloc,	/* l_ll */
};


/*
 * Initialization function, called by the dso loader.
 */
bool_t
label_init()
{
	label_new(&doslabsw);
	return (TRUE);
}


/*
 * legal dos filename/dir char
 *
 * I know it's ugly, but it's copied from pc_validchar() in the kernel (with
 * the exception that I won't allow spaces!)
 *
 * the reason isdigit(), isupper(), ... aren't used is that they are
 * char-set dependent, but DOS isn't
 */
static bool_t
dos_filename_char(char c)
{
	register char	*cp;
	static char valtab[] = {
		"$#&@!%()-{}<>`_\\^~|'"
	};


	/*
	 * Should be "$#&@!%()-{}`_^~' " ??
	 * From experiment in DOSWindows, "*+=|\[];:\",<>.?/" are illegal.
	 * See IBM DOS4.0 Tech Ref. B-57.
	 */

	if ((c >= 'A') && (c <= 'Z')) {
		return (TRUE);
	}
	if ((c >= '0') && (c <= '9')) {
		return (TRUE);
	}
	for (cp = valtab; *cp != NULLC; cp++) {
		if (c == *cp++) {
			return (TRUE);
		}
	}
	return (FALSE);
}


/*
 * check for a legal DOS label char
 *
 * if the char is okay as is, return it
 * if the char is xlateable, then xlate it (i.e. a space becomes an underscore)
 */
static int
dos_label_char(int c)
{
	/* check for alpha-numerics (digits or letters) */
	if (isalnum(c)) {
#ifdef	DEBUG_DOS_LABEL
		debug(1, "dos_label_char: returning alpha-num: '%c'\n", c);
#endif
		return (c);
	}

	/* check for spaces or tabs */
	if (isspace(c)) {
#ifdef	DEBUG_DOS_LABEL
		debug(1, "dos_label_char: returning '_' (from '%c')\n", c);
#endif
		return ('_');
	}

	/* check for other chars */
	switch (c) {
	case '.':
	case '_':
	case '+':
	case '-':
#ifdef	DEBUG_DOS_LABEL
		debug(1, "dos_label_char: returning spcl char '%c'\n", c);
#endif
		return (c);
	}

#ifdef	DEBUG_DOS_LABEL
	debug(1, "dos_label_char: returning NULLC\n");
#endif
	return (NULLC);
}


/*
 * Read the label off the disk and build an sl label structure
 * to represent it.
 */
static enum laread_res
dos_read(int fd, label *la, struct devs *dp)
{
	static char		*dos_rootdir_volname(int, char *);
	extern u_long		unique_key(char *, char *);
	u_char			*dos_buf = NULL;
	char			*type = dp->dp_dsw->d_mtype;
	struct dos_label	*dlp = 0;
	enum laread_res		retval;
	int			i;
	int			c;
	int			err;
	char			*cp;



	debug(1, "dos_read: fd = %d\n", fd);

	(void) lseek(fd, 0, SEEK_SET);		/* start at the beginning */

	/* make room */
	if ((dos_buf = (u_char *)malloc(DOS_READLEN)) == NULL) {
		debug(1, "dos_read: can't allocate %d bytes; %m\n",
		    DOS_READLEN);
		retval = L_UNRECOG;		/* what else to do? */
		goto out;
	}

	/* try to read the first sector */
	if ((err = read(fd, dos_buf, DOS_READLEN)) != DOS_READLEN) {
		if (err == -1) {
			/* couldn't read anything */
			debug(1, "dos_read: %s; %m\n", dp->dp_path);
		} else {
			/* couldn't read a whole sector! */
			debug(1, "dos_read: short read of label\n");
		}
		retval = L_UNFORMATTED;
		goto out;			/* give up */
	}

	/* try to interpret the data from the first sector */
	if ((dos_buf[DOS_MAGIC_OFF] != (u_char)DOS_MAGIC1) &&
	    (dos_buf[DOS_MAGIC_OFF] != (u_char)DOS_MAGIC2)) {
		debug(3, "dos_read: magic number is wrong (%#x)\n",
			(u_int)dos_buf[0]);
		retval = L_UNRECOG;
		goto out;			/* give up */
	}

	/* create a label structure */
	la->l_label = (void *)calloc(1, sizeof (struct dos_label));
	dlp = (struct dos_label *)la->l_label;

	/*
	 * Get the volume name out of the volume label.
	 *
	 * Earlier versions of DOS have the label name (if any) at
	 * DOS_NAME_OFF.  If no label is there (as in newer DOSes), then
	 * look in the root directory.
	 */
	if (dos_filename_char(dos_buf[DOS_NAME_OFF])) {
#ifdef	DEBUG_DOS_LABEL
		debug(1,
		"dos_read: copying dos_volname from byte %d of blk 0\n",
		    DOS_NAME_OFF);
#endif
		/* must be a legal dos name -- xfer it */
		for (i = 0; i < DOS_NAMELEN; i++) {
			if ((c = dos_buf[DOS_NAME_OFF + i]) == NULLC) {
				break;
			}
			if ((c = dos_label_char(c)) == NULLC) {
				break;
			}
			dlp->dos_volname[i] = (uchar_t)c;
#ifdef	DEBUG_DOS_LABEL
			debug(1, "dos_read: added volname char '%c'\n",
			    dlp->dos_volname[i]);
#endif
		}

		/* null terminate */
		dlp->dos_volname[i] = NULLC;

		/* trip off any "spaces" at the end */
		while (--i > 0) {
			if (dlp->dos_volname[i] != '_') {
				break;
			}
			dlp->dos_volname[i] = NULLC;
		}

	} else {

		/* look in the root dir */
		if ((cp = dos_rootdir_volname(fd, (char *)dos_buf)) != NULL) {
			(void) strcpy((char *)(dlp->dos_volname), cp);
		} else {
			dlp->dos_volname[0] = NULLC;
		}
	}

	/*
	 * Check for NEC DOS 2.0 (this should probably just be any DOS
	 * version earlier than 4).  If found then we have a non-unique
	 * piece of ... media
	 *
	 * XXX: this should be more general, but this is the only type of
	 * media I've seen so far that doesn't have the "id" field
	 */
	if (strncmp((char *)(dos_buf + DOS_OEM_JUNK), DOS_OEM_NEC2,
	    DOS_OEM_LENGTH) == 0) {
		retval = L_NOTUNIQUE;
		goto out;
	}

	/*
	 * read the pseudo random thing.  unfortunatly, it's not
	 * aligned on a word boundary otherwise I'd just cast...
	 */
	(void) memcpy(&dlp->dos_magic, &dos_buf[DOS_ID_OFF], sizeof (u_long));

	/*
	 * If it's zero (probably a pre-dos4.0 disk), we'll just
	 * slap a number in there.
	 */
	if (dlp->dos_magic == 0) {
		if (dp->dp_writeprot || never_writeback) {
			retval = L_NOTUNIQUE;
			goto out;
		}
		dlp->dos_magic = unique_key(type, PCFS_LTYPE);
		(void) memcpy(&dos_buf[DOS_ID_OFF], &dlp->dos_magic,
		    sizeof (u_long));
		if (lseek(fd, 0, SEEK_SET) < 0) {
			warning(gettext(
			    "dos read: couldn't seek for write back; %m\n"));
			retval = L_NOTUNIQUE;
			goto out;
		}
		if (write(fd, dos_buf, DOS_READLEN) < 0) {
			warning(gettext(
			    "dos_read: couldn't write back label %m\n"));
			retval = L_NOTUNIQUE;
			goto out;
		}
		debug(6, "label_dos: wroteback %#x as unique thing\n",
			dlp->dos_magic);
	}

	/*
	 * Ok, we're happy now.  We'll just continue along here.
	 */
	dlp->dos_lcrc = calc_crc(dos_buf, DOS_LABLEN);

	retval = L_FOUND;
out:
	if ((retval != L_FOUND) && (retval != L_NOTUNIQUE)) {
		if (dlp) {
			free(dlp);
		}
		la->l_label = 0;
	}
	if (dos_buf != NULL) {
		free(dos_buf);
	}

#ifdef	DEBUG_DOS_LABEL
	{
		char	*retval_str;
		char	retval_buf[80];


		switch (retval) {
		case L_FOUND:
			retval_str = "L_FOUND";
			break;
		case L_NOTUNIQUE:
			retval_str = "L_NOTUNIQUE";
			break;
		case L_UNRECOG:
			retval_str = "L_UNRECOG";
			break;
		case L_UNFORMATTED:
			retval_str = "L_UNFORMATTED";
			break;
		default:
			(void) sprintf(retval_buf, "L_??? (%d)", retval);
			retval_str = retval_buf;
			break;
		}
		debug(1, "dos_read: returning %s\n", retval_str);
		if (retval == L_FOUND) {
			debug(1, "dos_read: dos_volname = \"%s\"\n",
			    dlp->dos_volname);
		}
	}
#endif	/* DEBUG_DOS_LABEL */

	return (retval);
}


static char *
dos_rootdir_volname(int fd, char *boot_buf)
{
	static char	ent_buf[DOS_NAMELEN + 2] = "";	/* room for '.' */
	char		*rp = NULL;		/* result pointer */
	ushort_t	root_sec;
	ushort_t	sec_size;
	uchar_t		*root_dir = NULL;
	struct pcdir	*root_entry;
	ushort_t	root_ind;
	ushort_t	root_dir_entries;
	ulong_t		root_dir_size;
	char		*cp;
	uint_t		i;
	uchar_t		c;



	/* find where the root dir should be */
	root_sec = ltohs(boot_buf[PCB_RESSEC]) +
	    ((ushort_t)boot_buf[PCB_NFAT] * ltohs(boot_buf[PCB_SPF]));
	sec_size = ltohs(boot_buf[PCB_BPSEC]);
	root_dir_entries = ltohs(boot_buf[PCB_NROOTENT]);
	root_dir_size = root_dir_entries * sizeof (struct pcdir);

	/*
	 * read in the root directory
	 */
	if ((root_dir = (uchar_t *)malloc(root_dir_size)) == NULL) {
#ifdef	DEBUG
		debug(1, "dos_rootdir_volname: can't alloc memory; %m\n");
#endif
		goto dun;
	}
	if (lseek(fd, (root_sec * sec_size), SEEK_SET) < 0) {
#ifdef	DEBUG
		debug(1, "dos_rootdir_volname: can't seek; %m\n");
#endif
		goto dun;
	}
	if (read(fd, root_dir, root_dir_size) != root_dir_size) {
#ifdef	DEBUG
		debug(1, "dos_rootdir_volname: can't read root dir; %m\n");
#endif
		goto dun;
	}

	for (root_ind = 0; root_ind < root_dir_entries; root_ind++) {

		root_entry = (struct pcdir *)&root_dir[root_ind *
		    /*LINTED: alignment ok*/
		    sizeof (struct pcdir)];

		if (root_entry->pcd_filename[0] == PCD_UNUSED) {
			break;			/* end of entries */
		}
		if (root_entry->pcd_filename[0] == PCD_ERASED) {
			continue;		/* erased */
		}

		if (root_entry->pcd_attr & PCA_LABEL) {

			/* found it! - now extract it */

			rp = ent_buf;		/* result pointer */
			cp = ent_buf;

			/* treat name+extension as one entity */

			/* get the name part */
			if (dos_filename_char(root_entry->pcd_filename[0])) {
				for (i = 0; i < DOS_NAMELEN_REG; i++) {
					c = root_entry->pcd_filename[i];
					if (!dos_label_char(c)) {
						break;
					}
					*cp++ = (char)c;
				}
			}

			/* if name was full then look at extension field */
			if (i == DOS_NAMELEN_REG) {
				for (i = 0; i < DOS_NAMELEN_EXT; i++) {
					c = root_entry->pcd_ext[i];
					if (!dos_label_char(c)) {
						break;
					}
					*cp++ = (char)c;
				}
			}

			/* null terminate, hoser */
			*cp = NULLC;

			break;
		}

	}

dun:
	if (root_dir != NULL) {
		free(root_dir);
	}
	return (rp);
}


static char *
dos_key(label *la)
{
	char			buf[MAXNAMELEN];
	struct dos_label	*dlp = (struct dos_label *)la->l_label;


	(void) sprintf(buf, "0x%lx", dlp->dos_magic);
	return (strdup(buf));
}


static bool_t
dos_compare(label *la1, label *la2)
{
	struct dos_label	*dlp1;
	struct dos_label	*dlp2;


	dlp1 = (struct dos_label *)la1->l_label;
	dlp2 = (struct dos_label *)la2->l_label;

	/* easy first wack to see if they're different */
	if (dlp1->dos_lcrc != dlp2->dos_lcrc) {
		return (FALSE);
	}
	if (dlp1->dos_magic != dlp2->dos_magic) {
		return (FALSE);
	}
	return (TRUE);
}


static void
dos_setup(vol_t *v)
{
	struct dos_label	*dlp = (struct dos_label *)v->v_label.l_label;
	int			do_unnamed = 0;
	char			unnamed_buf[MAXNAMELEN+1];


	/* name selection... ya gotta love it! */
	if (dlp->dos_volname[0] != NULLC) {
#ifdef	DEBUG_DOS_LABEL
		debug(5,
		"dos_setup: calling makename(\"%s\",  %d) (from volname)\n",
		    dlp->dos_volname, DOS_NAMELEN);
#endif
		v->v_obj.o_name = makename((char *)(dlp->dos_volname),
		    DOS_NAMELEN);
		if (v->v_obj.o_name[0] == NULLC) {
			/* makename() couldn't make a name */
			do_unnamed++;
			free(v->v_obj.o_name);
		}
#ifdef	DEBUG_DOS_LABEL
		else {
			debug(5, "dos_setup: makename() returned \"%s\"\n",
			    v->v_obj.o_name);
		}
#endif
	} else {
		do_unnamed++;
	}

	if (do_unnamed) {
		/*
		 * we could be either floppy or pcmem (or whatever), so
		 * we must use "media type" here
		 */
		(void) sprintf(unnamed_buf, "%s%s", UNNAMED_PREFIX,
		    v->v_mtype);
		v->v_obj.o_name = strdup(unnamed_buf);
	}

#ifdef	DEBUG
	debug(5, "dos_setup: DOS media given name \"%s\"\n",
	    v->v_obj.o_name);
#endif

	/* just one partition ever */
	v->v_ndev = 1;

	/* XXX: say that it is available to the whole domain */
	v->v_flags |= V_NETWIDE;
}


static void
dos_xdr(label *l, enum xdr_op op, void **data)
{
	XDR			xdrs;
	struct dos_label	*dlp;
	struct dos_label	sdl;
	char			*s = NULL;


	/* if size is zero then fill it in */
	if (doslabsw.l_xdrsize == 0) {
		/* add in size of "dos_version" */
		doslabsw.l_xdrsize += xdr_sizeof(xdr_u_short,
		    (void *)&sdl.dos_version);
		/* add in size of "dos_lcrc" */
		doslabsw.l_xdrsize += xdr_sizeof(xdr_u_long,
		    (void *)&sdl.dos_lcrc);
		/* add in size of "dos_magic" */
		doslabsw.l_xdrsize += xdr_sizeof(xdr_u_long,
		    (void *)&sdl.dos_magic);
		/*
		 * add in size of "dos_volname" --
		 * yes, well, this is a bit of a hack here, but
		 * I don't know of any other way to do it.
		 * I know that xdr_string encodes the string
		 * as an int + the bytes, so I just allocate that
		 * much space (silveri said it would work).
		 */
		doslabsw.l_xdrsize += DOS_NAMELEN + sizeof (int);
	}

	if (op == XDR_ENCODE) {

		dlp = (struct dos_label *)l->l_label;
		*data = malloc(doslabsw.l_xdrsize);
		xdrmem_create(&xdrs, *data, doslabsw.l_xdrsize, op);
		dlp->dos_version = DOS_VERSION;
		(void) xdr_u_short(&xdrs, &dlp->dos_version);
		(void) xdr_u_long(&xdrs, &dlp->dos_lcrc);
		(void) xdr_u_long(&xdrs, &dlp->dos_magic);
		s = (char *)(dlp->dos_volname);
		(void) xdr_string(&xdrs, &s, DOS_NAMELEN);
		xdr_destroy(&xdrs);

	} else if (op == XDR_DECODE) {

		xdrmem_create(&xdrs, *data, doslabsw.l_xdrsize, op);
		if (l->l_label == NULL) {
			l->l_label =
			    (void *)calloc(1, sizeof (struct dos_label));
		}
		dlp = (struct dos_label *)l->l_label;
		(void) xdr_u_short(&xdrs, &dlp->dos_version);
		/*
		 * here's where we'll deal with other versions of this
		 * structure...
		 */
		ASSERT(dlp->dos_version == DOS_VERSION);
		(void) xdr_u_long(&xdrs, &dlp->dos_lcrc);
		(void) xdr_u_long(&xdrs, &dlp->dos_magic);
		(void) xdr_string(&xdrs, &s, DOS_NAMELEN);
		/*
		 * xdr_string seems not to allocate any memory for
		 * a null string, and therefore s is null on return...
		 */
		if (s != NULL) {
			(void) strncpy((char *)(dlp->dos_volname), s,
			    DOS_NAMELEN);
			xdr_free(xdr_string, (void *)&s);
		}
		xdr_destroy(&xdrs);
	}
}
