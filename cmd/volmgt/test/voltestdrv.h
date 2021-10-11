#ifndef lint
#ident  "@(#)voltestdrv.h 1.10     94/05/10 SMI"
#endif  /* lint */

/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef __voltestdrv_h
#define __voltestdrv_h

#include <sys/cdio.h>
#include <sys/ioccom.h>
#include <sys/vtoc.h>


struct vt_name {
	minor_t	vtn_unit;	/* vt unit to map */
	char	*vtn_name;	/* "label" of the device */
};
struct vt_tag {
	minor_t	vtt_unit;	/* vt unit to tag */
	u_int	vtt_tag;	/* magic cookie to return */
};

struct vt_event {
	minor_t	vte_dev;	/* inserted minor number */
};

struct vt_lab {
	minor_t	vtl_unit;	/* unit to assign label to */
	int	vtl_errno;	/* error to generate on block 0 reads. */
	size_t	vtl_readlen;	/* byte offset for generating error. */
	size_t	vtl_len;	/* number of bytes in label */
	char	*vtl_label;	/* pointer to label */
};

enum vt_evtype {
	VSE_WRTERR
};

struct vt_status {
	enum vt_evtype	vte_type;
	union {
		/* write error message */
		struct ve_wrterr {
			minor_t	vwe_unit;	/* unit bad data was on */
			int	vwe_want;	/* data that we wanted */
			int	vwe_got;	/* data that we got */
		} vse_u_wrterr;
	} vse_un;
};

/*
 * vt_vtoc contains information that the vt driver will use to respond to
 * DKIOCGVTOC ioctls.
 */

struct vt_vtoc {
	int		vtvt_errno;	/* error to return on DKIOCGVTOC */
	struct vtoc	vtvt_vtoc;	/* vol. table of contents. */
};

/*
 * Use vt_vtdes to load the vt_vtoc structure into the driver.
 */

struct vt_vtdes {
	minor_t		vtvd_unit;	/* test unit to load */
	struct vt_vtoc	vtvd_vtoc;	/* table of contents info. */
};

/*
 * vt_hdrinfo contains information that the vt driver will use to
 * respond to the CDROMREADTOCHDR ioctl.
 */

struct vt_hdrinfo {
    	int			vttoc_errno;	/* error for CDROMREADTOCHDR */
	struct cdrom_tochdr	vttoc_hdr;	/* TOC hdader information */
};

/*
 * Use vt_tochdr to load vt_hdrinfo into the driver using the
 * VTIOCSTOCHDR ioctl.
 */

struct vt_tochdr {
    	minor_t			vtt_unit;	/* unit to load. */
    	struct vt_hdrinfo	vtt_toc;	/* CDROMREADTOCHDR info. */
};

/*
 * Use vt_tedes to load cdrom toc entries and error generation information
 * into the vt driver using the VTIOCSTOCENTRIES ioctl.
 */

struct vt_tedes {
	minor_t			vttd_unit;	/* test unit to load. */
	int			vttd_errno;	/* error to return. */
	unsigned char		vttd_err_track;	/* track to gen. error for. */
	size_t			vttd_count;	/* number of toc entries. */
	struct cdrom_tocentry	*vttd_entries;	/* array of toc entries. */
};

#define vse_wrterr	vse_un.vse_u_wrterr

#define VTIOCNAME 	_IOR('v', 1, struct vt_name)
#define VTKIOCEVENT	_IOW('v', 2, struct vt_event)
#define VTIOCEVENT	_IOW('v', 3, struct vt_event)
#define VTIOCUNITS	_IOW('v', 4, u_int)
#define VTIOCTAG	_IOR('v', 5, struct vt_tag)
#define VTIOCLABEL	_IOR('v', 6, struct vt_lab)
#define VTIOCSTATUS	_IOW('v', 7, struct vt_status)
#define	VTIOCSVTOC	_IOR('v', 8, struct vt_vtdes)
#define VTIOCSTOCHDR	_IOR('v', 9, struct vt_tochdr)
#define VTIOCSTOCENTRIES _IOR('v', 10, struct vt_tedes)

#define VTCTLNAME	"voltestdrvctl"

#endif /* __voltestdrv_h */
