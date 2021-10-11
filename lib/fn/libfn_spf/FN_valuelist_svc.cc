/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FN_valuelist_svc.cc	1.5 95/02/27 SMI"

#include <xfn/FN_valuelist_svc.hh>

enum {
	FN_ITER_NOT_STARTED = 0,
	FN_ITER_IN_PROGRESS = 1,
	FN_ITER_COMPLETED = 2
	};

FN_valuelist_svc::FN_valuelist_svc(FN_attribute *a)
{
	attribute = a;
	iter_status = FN_ITER_NOT_STARTED;
	iter_pos = 0;
}

FN_valuelist_svc::~FN_valuelist_svc()
{
	if (attribute)
		delete attribute;
}

FN_attrvalue*
FN_valuelist_svc::next(FN_identifier **id,
    FN_status &status)
{
	const FN_attrvalue *answer = 0;
	FN_attrvalue *final_answer = 0;

	if (attribute) {
		if (id)
			(*id) = new FN_identifier(*(attribute->syntax()));
		switch (iter_status) {
		case FN_ITER_NOT_STARTED:
			answer = attribute->first(iter_pos);
			iter_status = FN_ITER_IN_PROGRESS;
			status.set_success();
			break;
		case FN_ITER_IN_PROGRESS:
			answer = attribute->next(iter_pos);
			status.set_success();
			break;
		default:
			answer = 0;
			status.set(FN_E_INVALID_ENUM_HANDLE);
		}
	}

	if (answer == 0) {
		iter_status = FN_ITER_COMPLETED;
	} else {
		final_answer = new FN_attrvalue(*answer);  // make copy
	}
	return (final_answer);
}
