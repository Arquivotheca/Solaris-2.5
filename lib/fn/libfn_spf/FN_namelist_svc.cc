/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FN_namelist_svc.cc	1.2 94/08/04 SMI"

#include <xfn/FN_namelist_svc.hh>

enum {
	FN_ITER_NOT_STARTED = 0,
	FN_ITER_IN_PROGRESS = 1,
	FN_ITER_COMPLETED = 2
	};

FN_namelist_svc::FN_namelist_svc(FN_nameset *ns)
{
	names = ns;
	iter_status = FN_ITER_NOT_STARTED;
	iter_pos = 0;
}

FN_namelist_svc::~FN_namelist_svc()
{
	if (names)
		delete names;
}

FN_string*
FN_namelist_svc::next(FN_status &status)
{
	const FN_string *answer = 0;
	FN_string *final_answer = 0;

	switch (iter_status) {
	case FN_ITER_NOT_STARTED:
		answer = names->first(iter_pos);
		iter_status = FN_ITER_IN_PROGRESS;
		status.set_success();
		break;
	case FN_ITER_IN_PROGRESS:
		answer = names->next(iter_pos);
		status.set_success();
		break;
	default:
		answer = 0;
		status.set(FN_E_INVALID_ENUM_HANDLE);
	}

	if (answer == 0) {
		iter_status = FN_ITER_COMPLETED;
	} else {
		final_answer = new FN_string(*answer);  // make copy
	}
	return (final_answer);
}
