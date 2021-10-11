
/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef	_AUTO_SENSE_H
#define	_AUTO_SENSE_H

#pragma ident	"@(#)auto_sense.h	1.1	93/03/26 SMI"

#ifdef	__cplusplus
extern "C" {
#endif


#ifdef	__STDC__
/*
 *	Prototypes for ANSI C compilers
 */
struct disk_type	*auto_sense(
				int		fd,
				int		can_prompt,
				struct dk_label	*label);

#else

struct disk_type	*auto_sense();

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _AUTO_SENSE_H */
