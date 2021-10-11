
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef	_MODIFY_PARTITION_H
#define	_MODIFY_PARTITION_H

#pragma ident	"@(#)modify_partition.h	1.3	93/03/25 SMI"

#ifdef	__cplusplus
extern "C" {
#endif


/*
 *	Prototypes for ANSI C compilers
 */
int	p_modify(void);
void	adj_cyl_offset(struct dk_map *map);
int	check_map(struct dk_map *map);
void	get_user_map(struct dk_map *map, int float_part);

#ifdef	__cplusplus
}
#endif

#endif	/* _MODIFY_PARTITION_H */
