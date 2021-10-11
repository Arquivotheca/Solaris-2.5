/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _XFN__FN_VALUELIST_SVC_HH
#define	_XFN__FN_VALUELIST_SVC_HH

#pragma ident "@(#)FN_valuelist_svc.hh	1.4 94/10/04 SMI"

#include <xfn/xfn.hh>

class FN_valuelist_svc : public FN_valuelist {
	FN_attribute *attribute;
	void *iter_pos;
	unsigned int iter_status;
public:
	FN_valuelist_svc(FN_attribute *);
	~FN_valuelist_svc();
	FN_attrvalue *next(FN_identifier **,
	    FN_status &);
};

#endif /* _XFN__FN_VALUELIST_SVC_HH */
