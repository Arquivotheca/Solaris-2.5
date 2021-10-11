/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)Entry.cc	1.5 94/10/07 SMI"

#include "FNSP_InitialContext.hh"
#include <synch.h>


// This file contains the code implementing the HELIX_InitialContext::Entry
// base class.  Definitions and code for the resolve() methods of the
// specific subclasses is in entries.hh/cc


FNSP_InitialContext::Entry::Entry()
{
	stored_ref = 0;
	stored_status_code = FN_E_NAME_NOT_FOUND;
	stored_names = 0;
	rwlock_init(&entry_lock, USYNC_THREAD, 0);
}

FNSP_InitialContext::Entry::~Entry()
{
	if (stored_names) {
		int i;
		for (i = 0; i < num_names; i++)
			delete stored_names[i];
		delete [] stored_names;
	}
	delete stored_ref;
}

// Our locking policy is as follows:
// No lock is needed to inspect the member 'stored_name', which is
// set once at entry construction time and is never modified.
//
// A reader's lock is needed to inspect the 'stored_status_code' or
// 'stored_ref' member.
//
// The writer's lock is needed to modify the 'stored_status_code' or
// 'stored_ref' member.
//
// Our resolution policy is that we attempt to resolve the name whenever
// a reference() call is made for the entry and the current stored_status
// code is not success.  The resolve() method is called and should attempt
// to resolve the name and set the new stored_status, which is returned to
// by the reference() call.
//
// Note: Once the status is success, no further attempt made to resolve the
// entry.  The current implementation does not support repeated successful
// dynamic resolution.


int
FNSP_InitialContext::Entry::find_name(const FN_string &name)
{
	int i;

	// find first entry with given name
	for (i = 0; (i < num_names) && (name.compare(*(stored_names[i]),
	    FN_STRING_CASE_INSENSITIVE) != 0); i++) {
		// empty loop
	}

	return (i < num_names);
}


const FN_string *
FNSP_InitialContext::Entry::first_name(void *&iter_pos)
{
	iter_pos = (void *)1;
	return (stored_names[0]);
}


const FN_string *
FNSP_InitialContext::Entry::next_name(void *&iter_pos)
{
	const FN_string *answer = 0;
	size_t pos = (size_t)iter_pos;
	if (pos < num_names) {
		answer = stored_names[pos];
		iter_pos = (void *)++pos;
	}
	return (answer);
}


void
FNSP_InitialContext::Entry::lock_and_resolve()
{
	// Get exclusive access
	get_writer_lock();

	// Yes, we must check FN_status again at this point!
	// Some other thread may have gotten the lock just before us
	// and resolved the name for us.

	// if the entry has not yet been resolved, call the specific
	// resolution method for this entry it will also set resolved,
	// as appropriate.

	if (stored_status_code != FN_SUCCESS) resolve();

	// release lock
	release_writer_lock();
}



FN_ref *
FNSP_InitialContext::Entry::reference(unsigned &scode)
{
	FN_ref *retref = 0;

	get_reader_lock();

	if (stored_status_code != FN_SUCCESS) {
		release_reader_lock();
		lock_and_resolve();
		get_reader_lock();
	}

	scode = stored_status_code;

	if (stored_status_code == FN_SUCCESS) {
		retref = new FN_ref(*stored_ref);
	}

	release_reader_lock();

	return (retref);
}

void
FNSP_InitialContext::Entry::get_writer_lock()
{
	rw_wrlock(&entry_lock);
}

void
FNSP_InitialContext::Entry::release_writer_lock()
{
	rw_unlock(&entry_lock);
}


void
FNSP_InitialContext::Entry::get_reader_lock()
{
	rw_rdlock(&entry_lock);
}

void
FNSP_InitialContext::Entry::release_reader_lock()
{
	rw_unlock(&entry_lock);
}
