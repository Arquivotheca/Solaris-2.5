/*
 * Copyright (c) 1992, by Sun Microsystems Inc.
 */

#ifndef	_SYS_INSTANCE_H
#define	_SYS_INSTANCE_H

#pragma ident	"@(#)instance.h	1.6	93/02/04 SMI"

/*
 * Instance number assignment data strutures
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/dditypes.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	INSTANCE_FILE	"/etc/path_to_inst"

#if	defined(_KERNEL) || defined(_KMEMUSER)

/*
 * The form of a node;  These form a tree that is equivalent to the
 * dev_info tree, but always fully populated.
 */

typedef struct in_node {
	int		in_instance;	/* current instance number	*/
	int		in_state;	/* see below			*/
	char		*in_name;	/* name of this node		*/
	char		*in_addr;	/* address part of name		*/
	struct in_node	*in_next;	/* next for this driver		*/
	struct in_node	*in_child;	/* children of this node	*/
	struct in_node	*in_sibling;	/* siblings of this node	*/
	/*
	 * This element is not currently used, but is reserved for future
	 * expansion (would be used to improve error messages--by printing
	 * path of node which conflicts with an instance number assignment)
	 */
	struct in_node	*in_parent;	/* parent of this node		*/
} in_node_t;

/*
 * This plus devnames defines the entire software state of the instance world.
 */
typedef struct in_softstate {
	in_node_t	*in_root;	/* the root of our instance tree */
	in_node_t	*in_no_major;	/* majorless nodes for later	 */
	in_node_t	*in_no_instance; /* IN_UNKNOWN nodes for later	 */
	/*
	 * Used to serialize access to data structures
	 */
	kmutex_t	in_serial;
	kcondvar_t	in_serial_cv;
	int		in_busy;
} in_softstate_t;


/*
 * Values for in_state
 */
#define	IN_PROVISIONAL	0x1	/* provisional instance number assigned */
#define	IN_PERMANENT	0x2	/* instance number has been confirmed */
#define	IN_UNKNOWN	0x3	/* instance number not yet assigned */

/*
 * special value for dn_instance
 */
#define	IN_SEARCHME (-1)

#endif	/* defined(_KERNEL) || defined(_KMEMUSER) */

#ifdef	_KERNEL
void e_ddi_instance_init(void);
u_int e_ddi_assign_instance(dev_info_t *dip);
void e_ddi_keep_instance(dev_info_t *dip);
void e_ddi_free_instance(dev_info_t *dip);
void e_ddi_orphan_instance_nos(in_node_t *np);
void e_ddi_unorphan_instance_nos();
extern in_softstate_t e_ddi_inst_state;
#else	/* _KERNEL */
#ifdef __STDC__
extern int inst_sync(char *pathname, int flags);
#else
extern int inst_sync();
#endif	/* __STDC__ */
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_INSTANCE_H */
