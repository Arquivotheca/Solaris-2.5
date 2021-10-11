/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _FNSP_INITIALCONTEXT_HH
#define	_FNSP_INITIALCONTEXT_HH

#pragma ident "@(#)FNSP_InitialContext.hh	1.8 94/11/20 SMI"

#include <xfn/fn_spi.hh>
#include <synch.h>
#include <stdlib.h>   /* for uid_t */

#define	FNSP_HOST_TABLE 0
#define	FNSP_USER_TABLE 1
#define	FNSP_GLOBAL_TABLE 2
#define	FNSP_CUSTOM_TABLE 3

#define	FNSP_NUMBER_TABLES 4

typedef enum FNSP_IC_type {
	FNSP_ALL_IC,
	FNSP_HOST_IC,
	FNSP_USER_IC} FNSP_IC_type;

// Use csvc_weak_static in order to support "/..."

class FNSP_InitialContext : public FN_ctx_csvc_weak_static {
public:

	FN_composite_name *p_component_parser(const FN_composite_name &,
	    FN_composite_name **rest,
	    FN_status_psvc& s);

	// non-virtual declarations of the context service operations
	FN_ref *get_ref(FN_status &)const;
	FN_ref *c_lookup(const FN_string &name, unsigned int f,
	    FN_status_csvc&);
	FN_namelist* c_list_names(const FN_string &name, FN_status_csvc&);
	FN_bindinglist* c_list_bindings(const FN_string &name,
	    FN_status_csvc&);
	int c_bind(const FN_string &name, const FN_ref &,
	    unsigned BindFlags, FN_status_csvc&);
	int c_unbind(const FN_string &name, FN_status_csvc&);
	int c_rename(const FN_string &name, const FN_composite_name &,
	    unsigned BindFlags, FN_status_csvc&);
	FN_ref *c_create_subcontext(const FN_string &name, FN_status_csvc&);
	int c_destroy_subcontext(const FN_string &name, FN_status_csvc&);
	FN_attrset* c_get_syntax_attrs(const FN_string &name,
	    FN_status_csvc&);
	// Attribute Operations
	FN_attribute *c_attr_get(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	int c_attr_modify(const FN_string &,
	    unsigned int,
	    const FN_attribute&,
	    FN_status_csvc&);
	FN_valuelist *c_attr_get_values(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	FN_attrset *c_attr_get_ids(const FN_string &,
	    FN_status_csvc&);
	FN_multigetlist *c_attr_multi_get(const FN_string &,
	    const FN_attrset *, FN_status_csvc&);
	int c_attr_multi_modify(const FN_string &,
	    const FN_attrmodlist&,
	    FN_attrmodlist **,
	    FN_status_csvc&);

	FN_ref *c_lookup_nns(const FN_string &name, unsigned int f,
	    FN_status_csvc&);
	FN_namelist* c_list_names_nns(const FN_string &name,
	    FN_status_csvc&);
	FN_bindinglist* c_list_bindings_nns(const FN_string &name,
	    FN_status_csvc&);
	FN_attrset* c_get_syntax_attrs_nns(const FN_string &name,
	    FN_status_csvc&);
	// Attribute Operations
	FN_attribute *c_attr_get_nns(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	int c_attr_modify_nns(const FN_string &,
	    unsigned int,
	    const FN_attribute&,
	    FN_status_csvc&);
	FN_valuelist *c_attr_get_values_nns(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	FN_attrset *c_attr_get_ids_nns(const FN_string &,
	    FN_status_csvc&);
	FN_multigetlist *c_attr_multi_get_nns(const FN_string &,
	    const FN_attrset *, FN_status_csvc&);
	int c_attr_multi_modify_nns(const FN_string &,
	    const FN_attrmodlist&,
	    FN_attrmodlist **,
	    FN_status_csvc&);

	int c_bind_nns(const FN_string &name, const FN_ref &,
	    unsigned BindFlags, FN_status_csvc&);
	int c_rename_nns(const FN_string &name, const FN_composite_name &,
	    unsigned BindFlags, FN_status_csvc&);
	int c_unbind_nns(const FN_string &name, FN_status_csvc&);
	FN_ref *c_create_subcontext_nns(const FN_string &name,
	    FN_status_csvc&);
	int c_destroy_subcontext_nns(const FN_string &name, FN_status_csvc&);

	// The state consists of a read-only table in
	// which each entry records a binding.
	// Note that the binding that is recorded is
	// the binding of "<name>:".
	// e.g. the binding of "org:" not "org" itself.

	// The name portion of each entry in the table is set up
	// at the time the entry is made and, the entry is
	// placed in the table
	// at table construction time.  The table interface supports only
	// "read"-like operations thereafter.

	// We defer actually resolving each name until there is a
	// an operation that forces us to determine the
	// reference or simply whether or not the name is bound.

	// This is done by making the implementation of
	// Entry objects (defined
	// just below) determine the reference for the entry on the first
	// get_ref() or is_bound() call.  When this happens,
	// the entry object calls a protected virtual
	// resolve() method to actually
	// resolve its name.  Each actual entry in the table is an instance
	// of a subclass of Entry, and defines its particular resolve method.

	// This implementation is MT-safe: many threads may safely share
	// the same initial context object.  Locking for multiple threads is
	// handled by private methods on each Entry.  Subclasses of class
	// Entry need not worry about locking in their
	// specific resolve() methods.
	// The locking policy is described further in Entry.cc

	class Entry {
	public:
		const FN_string *first_name(void *&iter_pos);
		const FN_string *next_name(void *&iter_pos);

		// returns whether the given name is in this entry
		int find_name(const FN_string &name);

		// Forces resolution of the name, and if successful,
		// allocates a copy of the reference and returns it;
		// returns 0 if the FN_status_code is not success.
		FN_ref *reference(unsigned &FN_status_code);

		// constructor
		Entry();
		virtual ~Entry();

	protected:
		virtual void resolve() = 0;
		size_t num_names;
		FN_string **stored_names;
		unsigned stored_status_code;
		FN_ref *stored_ref;

	private:
		rwlock_t entry_lock;
		void lock_and_resolve();
		void get_reader_lock();
		void release_reader_lock();
		void get_writer_lock();
		void release_writer_lock();
	};

	class UserEntry : public Entry {
	public:
		UserEntry(uid_t);

	protected:
		uid_t my_uid;
	};

	// forward declaration
	class IterationPosition;

private:
	class Table {
	public:
		// find first entry with given name, return a pointer to it
		// if not found return 0
		virtual Entry* find(const FN_string &name);
		// iterator functions
		// first puts iteration position at beginning of table
		// next advances the iteration position by one
		// both return a pointer to the entry at the new position
		// next returns a 0 pointer if already at the end
		virtual Entry* first(IterationPosition& iter_pos);
		virtual Entry* next(IterationPosition& iter_pos);
		Table();
	protected:
		Entry** entry;
		int size;
	};

	class UserTable : public Table {
	private:
		uid_t my_uid;
		UserTable* next_table;
	public:
		UserTable(uid_t, UserTable*);
		~UserTable();

		const UserTable* find_user_table(uid_t) const;
	};

	class HostTable : public Table {
	public:
		HostTable();
		~HostTable();
	};

	class GlobalTable : public Table {
	public:
		GlobalTable();
		~GlobalTable();
	};

	class CustomTable : public Table {
		/* *** probably will have storage location */
	public:
		CustomTable();
		~CustomTable();

		// %%% probably will define own methods for find,
		// first/next
	};

	class IterationPosition {
	public:
		IterationPosition();
		IterationPosition(const IterationPosition&);
		IterationPosition& operator=(const IterationPosition&);
		~IterationPosition();
		friend Entry* Table::first(IterationPosition& iter_pos);
		friend Entry* Table::next(IterationPosition& iter_pos);

	private:
		void *position;
	};

	Table* tables[FNSP_NUMBER_TABLES];

	friend FN_ctx_svc* FNSP_InitialContext_from_initial(
	    FNSP_IC_type ic_type, uid_t uid, FN_status &);

	// constructor for use by from_initial();
	FNSP_InitialContext(
	    uid_t uid,
	    HostTable*& hosts,
	    UserTable*&,
	    GlobalTable*& global,
	    CustomTable*&);

	// for host entries only
	FNSP_InitialContext(HostTable*& hosts);

	// for user entries only
	FNSP_InitialContext(
	    uid_t uid,
	    UserTable*&);

	virtual ~FNSP_InitialContext();
};

#endif /* _FNSP_INITIALCONTEXT_HH */
