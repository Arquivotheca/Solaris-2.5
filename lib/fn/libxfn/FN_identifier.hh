/*
 * Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
 */

#ifndef __XFN_IDENTIFIER_HH__
#define	__XFN_IDENTIFIER_HH__

#pragma ident	"@(#)FN_identifier.hh	1.3	94/08/09 SMI"

#include <stddef.h>

#include <xfn/FN_identifier.h>

struct FN_identifier_rep;

class FN_identifier {
	FN_identifier_rep	*rep;

	void common_init(unsigned int format, size_t length, const void *contents);
public:
	FN_identifier_t	info;

	FN_identifier();
	FN_identifier(unsigned int format, size_t length, const void *contents);
	FN_identifier(const FN_identifier_t &);
	FN_identifier(const FN_identifier &);
	FN_identifier(const unsigned char *);
	~FN_identifier();
	FN_identifier &operator=(const FN_identifier &);

	unsigned int format(void) const;
	size_t length(void) const;
	const void *contents(void) const;
	const unsigned char *str(unsigned int *status = 0) const;

	int operator==(const FN_identifier &id) const;
	int operator!=(const FN_identifier &id) const { return !(*this == id); };
};

#endif // __XFN_IDENTIFIER_HH__
