/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _XFN__FN_NAMELIST_SVC_HH
#define	_XFN__FN_NAMELIST_SVC_HH

#pragma ident "@(#)FN_namelist_svc.hh	1.3 94/08/13 SMI"

#include <xfn/xfn.hh>
#include <xfn/FN_nameset.hh>

class FN_namelist_svc : public FN_namelist {
	FN_nameset *names;
	void *iter_pos;
	unsigned int iter_status;
public:
	FN_namelist_svc(FN_nameset *);
	~FN_namelist_svc();
	FN_string* next(FN_status &);
};

#endif /* _XFN__FN_NAMELIST_SVC_HH */
