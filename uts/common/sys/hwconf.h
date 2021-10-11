/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_HWCONF_H
#define	_SYS_HWCONF_H

#pragma ident	"@(#)hwconf.h	1.8	93/10/15 SMI"

#include <sys/dditypes.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	MAX_HWC_LINESIZE 1024

struct hwc_spec {
	struct hwc_spec *hwc_next;
	char		*hwc_parent_name;
	char		*hwc_class_name;
	dev_info_t	*hwc_proto;
};

/*
 * used to create sorted linked lists of hwc_spec structs for loading parents
 */
struct par_list {
	struct par_list	*par_next;
	major_t		par_major;		/* Simple name of parent */
	int		par_childs_unit;	/* Child's unit number */
	struct hwc_spec	*par_specs;		/* List of prototype nodes */
};

struct bind {
	struct bind 	*b_next;
	char		*b_name;
	int		b_num;
};

struct mperm {
	struct mperm	*mp_next;
	char		*mp_drvname;
	char		*mp_minorname;
	int		mp_perm;
	char		*mp_owner;
	char		*mp_group;
	uid_t		mp_uid;
	gid_t		mp_gid;
};

#ifdef _KERNEL

extern struct bind *mb_hashtab[];
extern struct bind *sb_hashtab[];

extern struct hwc_spec *hwc_parse(char *);
extern struct par_list *sort_hwc(struct hwc_spec *);
extern struct par_list *impl_make_parlist(major_t major);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_HWCONF_H */
