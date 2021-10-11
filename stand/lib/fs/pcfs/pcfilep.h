/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)pcfilep.h	1.1	94/07/19 SMI"

/*
 *  These structs should be invisible to the caller of
 *  the user-level routines
 */

typedef struct dev_ident {	/* device identifier block */
	char	*di_desc;
	int	di_dcookie;
	char	di_taken;
	union {
		char	dummy[PC_SECSIZE+sizeof (struct pcfs)];
		struct	pcfs	di_pcfs;
	} un_fs;
} devid_t;

typedef struct file_ident {	/* file identifier block */
	u_int		fi_filedes;
	char		*fi_path;
	u_int		fi_blocknum;
	u_int		fi_count;
	u_int		fi_offset;
	caddr_t		fi_memp;
	char		fi_taken;
	devid_t		*fi_devp;
	char		fi_buf[MAXBSIZE];
	struct	inode	*fi_inode;
	void		*fi_xnode;
	struct file_ident *fi_forw;
	struct file_ident *fi_back;
} fileid_t;
