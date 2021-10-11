/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _FNSP_ENTRIES_HH
#define	_FNSP_ENTRIES_HH

#pragma ident "@(#)FNSP_entries.hh	1.3 94/08/11 SMI"


#include "FNSP_InitialContext.hh"

// These are the definitions of the subclasses of FNSP_InitialContext::Entry
// that define specific resolution methods.  The code implementing these
// subclasses is in entries.cc.


// ******************** Host-related entries *****************************

class FNSP_InitialContext_ThisHostEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_ThisHostEntry::
	    FNSP_InitialContext_ThisHostEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_HostOrgUnitEntry :
public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_HostOrgUnitEntry::
	    FNSP_InitialContext_HostOrgUnitEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_HostENSEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_HostENSEntry::FNSP_InitialContext_HostENSEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_HostSiteEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_HostSiteEntry::
	    FNSP_InitialContext_HostSiteEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_HostSiteRootEntry :
public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_HostSiteRootEntry::
	    FNSP_InitialContext_HostSiteRootEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_HostOrgEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_HostOrgEntry::FNSP_InitialContext_HostOrgEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_HostUserEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_HostUserEntry::
	FNSP_InitialContext_HostUserEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_HostHostEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_HostHostEntry::
	FNSP_InitialContext_HostHostEntry();
	// non-virtual definition of resolution method
	void resolve();
};

// ******************** User-related entries *****************************


class FNSP_InitialContext_UserOrgUnitEntry :
public FNSP_InitialContext::UserEntry {
public:
	FNSP_InitialContext_UserOrgUnitEntry::
	FNSP_InitialContext_UserOrgUnitEntry(uid_t);
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_UserSiteEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_UserSiteEntry::
	FNSP_InitialContext_UserSiteEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_UserENSEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_UserENSEntry::
	FNSP_InitialContext_UserENSEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_ThisUserEntry :
public FNSP_InitialContext::UserEntry {
public:
	FNSP_InitialContext_ThisUserEntry::
	FNSP_InitialContext_ThisUserEntry(uid_t);
	// non-virtual definition of resolution method
	void resolve();
};

#ifdef FN_IC_EXTENSIONS

class FNSP_InitialContext_UserUserEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_UserUserEntry::
	FNSP_InitialContext_UserUserEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_UserHostEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_UserHostEntry::
	FNSP_InitialContext_UserHostEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_UserOrgEntry :
public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_UserOrgEntry::FNSP_InitialContext_UserOrgEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_UserSiteRootEntry :
public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_UserSiteRootEntry::
	FNSP_InitialContext_UserSiteRootEntry();
	// non-virtual definition of resolution method
	void resolve();
};

#endif /* FN_IC_EXTENSIONS */


/* ******************** Global entries ***************************** */

class FNSP_InitialContext_GlobalEntry : public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_GlobalEntry::FNSP_InitialContext_GlobalEntry();
	// non-virtual definition of resolution method
	void resolve();
};

#ifdef DEBUG
class FNSP_InitialContext_GlobalDNSEntry :
public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_GlobalDNSEntry::
	FNSP_InitialContext_GlobalDNSEntry();
	// non-virtual definition of resolution method
	void resolve();
};

class FNSP_InitialContext_GlobalX500Entry :
public FNSP_InitialContext::Entry {
public:
	FNSP_InitialContext_GlobalX500Entry::
	FNSP_InitialContext_GlobalX500Entry();
	// non-virtual definition of resolution method
	void resolve();
};

#endif /* DEBUG */

#endif /* _FNSP_ENTRIES_HH */
