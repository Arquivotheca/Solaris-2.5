/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)from_initial.cc	1.9 94/10/17 SMI"

// Copyright (C) 1992 by Sun Microsystems, Inc.


#include "FNSP_InitialContext.hh"
#include <synch.h>
#include <unistd.h>  /* for geteuid */

mutex_t uts_init_lock = DEFAULTMUTEX;   /* user table initialization lock */
mutex_t ht_init_lock = DEFAULTMUTEX;   /* host table initialization lock */
mutex_t gt_init_lock = DEFAULTMUTEX;   /* global table initialization lock */

// Initial Context that only has host bindings
FNSP_InitialContext::FNSP_InitialContext(HostTable *&ht)
{
	if (ht == 0) {
		// REMIND: need to acquire lock here.
		// check again after acquiring lock
		mutex_lock(&ht_init_lock);

		if (ht == 0) {
			ht = new HostTable;
		}

		// REMIND: release lock here
		mutex_unlock(&ht_init_lock);
	}

	tables[FNSP_HOST_TABLE] = ht;
	tables[FNSP_USER_TABLE] = 0;
	tables[FNSP_GLOBAL_TABLE] = 0;
	tables[FNSP_CUSTOM_TABLE] = 0;
}


// Initial context for user related bindings only

FNSP_InitialContext::FNSP_InitialContext(uid_t uid, UserTable *&uts)
{
	UserTable *ut = 0;

	if (uid) {
		// acquire lock here to examine list
		mutex_lock(&uts_init_lock);

		if (uts == 0)
			// never before set
			ut = uts = new UserTable(uid, (UserTable*)0);
		else if (ut = (UserTable*)((uts->find_user_table(uid))));
		// do nothing
		else {
			ut = new UserTable(uid, uts);
			uts = ut;
		}
		mutex_unlock(&uts_init_lock);
	}

	tables[FNSP_HOST_TABLE] = 0;
	tables[FNSP_USER_TABLE] = ut;
	tables[FNSP_GLOBAL_TABLE] = 0;
	tables[FNSP_CUSTOM_TABLE] = 0;
}

// Initial Context for all bindigns

FNSP_InitialContext::FNSP_InitialContext(
    uid_t uid,
    HostTable *&ht,
    UserTable *&uts,
    GlobalTable *&gt,
    CustomTable *& /* ct */)
{
	UserTable *ut = 0;

	// %%% [rl] I don't know what I'm doing here wrt locks;
	// someone better check to make sure this make sense
	// 1. should setting ht and uts share the same lock?
	// 2. is updating the linked list (uts) using this lock OK
	// 3. must be other questions
	// %%%

	if (ht == 0) {
		// REMIND: need to acquire lock here.
		// check again after acquiring lock
		mutex_lock(&ht_init_lock);

		if (ht == 0) {
			ht = new HostTable;
		}

		// REMIND: release lock here
		mutex_unlock(&ht_init_lock);
	}

	if (uid) {
		// acquire lock here to examine list
		mutex_lock(&uts_init_lock);

		if (uts == 0)
			// never before set
			ut = uts = new UserTable(uid, (UserTable*)0);
		else if (ut = (UserTable*)((uts->find_user_table(uid))));
		// do nothing
		else {
			ut = new UserTable(uid, uts);
			uts = ut;
		}
		mutex_unlock(&uts_init_lock);
	}

	if (gt == 0) {
		// REMIND: need to acquire lock here.
		// check again after acquiring lock
		mutex_lock(&gt_init_lock);

		if (gt == 0) {
			gt = new GlobalTable;
		}

		// REMIND: release lock here
		mutex_unlock(&gt_init_lock);
	}

	tables[FNSP_HOST_TABLE] = ht;
	tables[FNSP_USER_TABLE] = ut;
	tables[FNSP_GLOBAL_TABLE] = gt;
	tables[FNSP_CUSTOM_TABLE] = 0;
}

FNSP_InitialContext::~FNSP_InitialContext()
{
	// delete table;
}

FN_ctx_svc*
FNSP_InitialContext_from_initial(
    FNSP_IC_type initial_context_type,
    uid_t uid, FN_status &status)
{
	// All callers in this address space get a different FNSP_InitialContext
	// object but with a pointer to the same bindings table.
	// They share state, but can delete the context object independently
	// without deleting the state.  State remains allocated until process
	// termination.

	// Note: C++ defn. guarantees local static is initialized to 0
	// before the first time through this block.
	static FNSP_InitialContext::UserTable* uts; /* linked list */
	static FNSP_InitialContext::HostTable* ht;
	static FNSP_InitialContext::GlobalTable* gt;
	static FNSP_InitialContext::CustomTable* ct;

	FN_ctx_svc* ctx;
	switch (initial_context_type) {
	case FNSP_HOST_IC:
		ctx = (FN_ctx_svc*) new FNSP_InitialContext(ht);
		break;
	case FNSP_USER_IC:
		ctx = (FN_ctx_svc*) new FNSP_InitialContext(uid, uts);
		break;
	case FNSP_ALL_IC:
		ctx = (FN_ctx_svc*) new FNSP_InitialContext(uid, ht, uts,
		    gt, ct);
		break;
	default:
		ctx = 0;
	}

	if (ctx) {
		status.set_success();
	} else {
		status.set(FN_E_INSUFFICIENT_RESOURCES, 0, 0, 0);
	}
	return (ctx);
}




extern "C"
FN_ctx_svc_t*
initial__fn_ctx_svc_handle_from_initial(FN_status_t *status)
{
	uid_t uid = geteuid();
	FN_ctx_svc* cc_ctx = FNSP_InitialContext_from_initial
	    (FNSP_ALL_IC, uid, *((FN_status *)status));

	return ((FN_ctx_svc_t*)cc_ctx);
}

extern "C"
FN_ctx_t*
initial__fn_ctx_handle_from_initial(FN_status_t *status)
{
	FN_ctx* cc_ctx = (FN_ctx_svc*)
	    initial__fn_ctx_svc_handle_from_initial(status);

	return ((FN_ctx_t*)cc_ctx);
}
