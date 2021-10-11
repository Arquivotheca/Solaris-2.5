/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#if ! defined(_HEADER_H)
#define _HEADER_H

#pragma	ident	"@(#)prototype.h	1.1	93/12/09 SMI"

/*
 * Block comment which describes the contents of this file.
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <includes.h>

#define MACROS values

struct tag {
	type_t member;
};

typedef predefined_types new_types;

type_t global_variables;

void functions(void);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* ! defined(_HEADER_H) */
