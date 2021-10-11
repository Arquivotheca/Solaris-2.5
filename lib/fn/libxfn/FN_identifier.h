/*
 * Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _XFN_IDENTIFIER_H
#define	_XFN_IDENTIFIER_H

#pragma ident	"@(#)FN_identifier.h	1.3	94/11/20 SMI"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Predefined format values.
 */

enum {
	FN_ID_STRING,
	FN_ID_DCE_UUID,
	FN_ID_ISO_OID_STRING,
	FN_ID_ISO_OID_BER
	/* others... */
};

typedef struct {
	unsigned int	format;
	size_t		length;
	void		*contents;
} FN_identifier_t;

#ifdef __cplusplus
}
#endif

#endif /* _XFN_IDENTIFIER_H */
