/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _XFN__FN_MULTIGETLIST_SVC_HH
#define	_XFN__FN_MULTIGETLIST_SVC_HH

#pragma ident "@(#)FN_multigetlist_svc.hh	1.3 94/08/13 SMI"

#include <xfn/xfn.hh>

class FN_multigetlist_svc : public FN_multigetlist {
	FN_attrset *attribute_set;
	void *iter_pos;
	unsigned int iter_status;
public:
	FN_multigetlist_svc(FN_attrset *);
	~FN_multigetlist_svc();
	FN_attribute *next(FN_status &);
};

#endif /* _XFN__FN_MULTIGETLIST_SVC_HH */
