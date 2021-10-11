/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FN_ctx_svc.cc	1.8 94/11/20 SMI"

#include <xfn/fn_spi.hh>

static const FN_string FN_RELATIVE_INDICATOR((unsigned char *)".");
static int FN_SPI_LINK_LIMIT  = 4;

static int
is_relative_link(const FN_composite_name &lname, FN_composite_name **rest = 0)
{
	void *iter_pos;
	const FN_string *fnstr = lname.first(iter_pos);
	if (fnstr == 0) {
		return (0);
	}
	if (fnstr->compare(FN_RELATIVE_INDICATOR) == 0) {
		if (rest)
			*rest = lname.suffix(iter_pos);
		return (1);
	}
	return (0);
}

class FN_link_stack
{
private:
	int stack_top;
	FN_composite_name **stack;
public:
	FN_link_stack();
	~FN_link_stack();

	FN_composite_name *pop();
	int push(const FN_composite_name &);
	int is_empty(void);
};

FN_link_stack::FN_link_stack()
{
	stack_top = -1;  /* empty */
	stack = new FN_composite_name*[FN_SPI_LINK_LIMIT];
}

FN_link_stack::~FN_link_stack()
{
	FN_composite_name *stack_val;

	// clear stack contents first
	while (stack_val = pop())
		delete stack_val;

	delete [] stack;
}

FN_composite_name *
FN_link_stack::pop()
{
	if (stack_top >= 0) {
		FN_composite_name *stack_val = stack[stack_top--];
		return (stack_val);
	}
	return (0);
}

int
FN_link_stack::push(const FN_composite_name &item)
{
	if (stack_top < (FN_SPI_LINK_LIMIT - 1)) {
		stack[++stack_top] = new FN_composite_name(item);
		return (1);
	}
	return (0);
}

int
FN_link_stack::is_empty()
{
	return (stack_top == -1);
}


static inline int
should_follow_link(const FN_status &s)
{
	return (s.code() == FN_E_SPI_FOLLOW_LINK);
}

// Resolve given name
// If resolved successfully,
//	return resolved reference as return value
// 	set following_link to TRUE if resolved referece is another link
//
// %%% need to make sure that 'status' contain
// %%% information for processing relative link
//
static
FN_ref*
resolve_in_ctx(FN_ctx* ctx, const FN_composite_name &name,
    FN_status &status, unsigned int &following_link,
    FN_ref *&relative_ref)
{
	FN_ref *ref = ctx->lookup(name, status);

	following_link = (ref && ref->is_link());
	relative_ref = 0; // %%% need to figure a way to set this

	return (ref);
}

// Resolve given name until one of the following conditions are met
// 1.  given name is completely resolved
// 		return resolved reference as return value;
//		following_link is set to TRUE if resolved reference is a link
// 2.  another link is encountered in the middle of given name
//		return NULL as return value
//		following_link is set to TRUE;
// 		status contains information for following the link
// 3.  an error (other than FN_E_SPI_CONTINUE) is encountered
//		return NULL as return value
// 		following_link is set to FALSE
// 		status contains information on error
//
static
FN_ref*
resolve_in_ctx_svc(FN_ctx_svc* ctx, const FN_composite_name &name,
		    FN_status &status, unsigned int &following_link,
		    FN_ref *&relative_ref)
{
	FN_status_psvc pstatus(status);
	FN_ref *ref = ctx->p_lookup(name,
				    FN_SPI_LEAVE_TERMINAL_LINK,
				    pstatus);

	while (pstatus.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *nctx;
		FN_status from_stat;
		if (!(nctx = FN_ctx_svc::from_ref(*pstatus.resolved_ref(),
						    from_stat))) {
			pstatus.set_code(from_stat.code());
			break;
		}
		FN_composite_name rn = *pstatus.remaining_name();
		ref = nctx->p_lookup(rn, FN_SPI_LEAVE_TERMINAL_LINK, pstatus);
		delete nctx;
	}

	// %%% need to fix case where looked up ref succeeded
	// %%% need to get at resolved_ref and put it in link_resolved_ref
	// %%% do that either here or make lookup operation always return it

	following_link = ((ref && ref->is_link()) ||
	    should_follow_link(pstatus));

	if (should_follow_link(pstatus)) {
		if (pstatus.resolved_ref())
			ref = new FN_ref(*pstatus.resolved_ref());
		else
			ref = 0;  // should never happen
		if (pstatus.link_resolved_ref())
			relative_ref = new FN_ref(*pstatus.link_resolved_ref());
		else
			relative_ref = 0; // for links relative to IC
	} else {
		relative_ref = 0; // %%% need to set this from info from pstatus
	}
	return (ref);
}


// Do link resolution with limit detection for those contexts that
// use FN_ctx_svc and its subclasses.
// For those contexts that use other service classes, link loop/limit
// detection is delegated and controlled by those classes.
//
// This routine takes the XFN link in 'link_ref' and resolves it.
// If it is a relative link, it is resolved relative to 'relative_ref'.
static FN_ref *
follow_link(const FN_ref *link_ref,
	    const FN_ref *relative_ref,
	    FN_status_psvc &ps)
{
	FN_link_stack lstack;
	unsigned int following_link = 1;
	unsigned int link_count = 0;
	FN_ref *last_resolved_ref = 0;
	FN_ref *last_relative_ref = 0;
	FN_ref *ret = 0;
	FN_status rs;
	ps.set_resolved_ref(link_ref);
	ps.set_link_resolved_ref(relative_ref);

	// keep resolving links while stack is not empty (indicates
	// trailing unresolved components -- link occurred in middle of name)
	// or a link is being followed (in case of terminal link)
	//
	while (!lstack.is_empty() ||
	    ((link_count < FN_SPI_LINK_LIMIT) && following_link)) {
		FN_ctx_svc *sctx = 0;
		FN_ctx *ctx = 0;

		if (following_link) {
			++link_count;
			// 1.  Push unresolved part onto stack for
			// later processing
			const FN_composite_name *rn = ps.remaining_name();
			if (rn) {
				lstack.push(*rn);
			}

			// 2.  Get link name
			const FN_ref *ref = 0;
			FN_composite_name *lname = 0;
			FN_composite_name *rest = 0;

			if ((ref = ps.resolved_ref()) &&
			    (lname = ref->link_name()))
				;
			else {
				// something is wrong; could not get link
				// name from status
				// %%% rl: what else need to be passed back
				ps.set(FN_E_MALFORMED_LINK);
				break;
			}

			// 3. Obtain context for resolving link name
			// If first component is ".", resolve relative to
			// link_resolved_ref
			// Otherwise, resolve relative to Initial Context
			//
			// If context does not use framework (i.e. cannot get
			// FN_ctx_svc handle), use FN_ctx handle (this will
			// not allow for complete link limit detection)
			//
			const FN_ref *rel_ref = ps.link_resolved_ref();
			if (is_relative_link(*lname, &rest)) {
				// for relative links, the context in which to
				// start resolution is stored in the
				// link_resolved_ref field
				if (rel_ref == 0) {
					// no starting point;
					// %%% more appropriate error code?
					ps.set(FN_E_MALFORMED_LINK);
					// %%% what other status fields need
					// to be passed back?
					break;
				}
				if ((sctx = FN_ctx_svc::from_ref(*rel_ref, rs))
				    == 0)
					ctx = FN_ctx::from_ref(*rel_ref, rs);
				// skip over relative indicator in link name
				delete lname;
				lname = rest;
			} else {
				// Absolute link to be resolved relative to IC
				FN_status_psvc ps2(rs);
				if ((sctx = FN_ctx_svc::from_initial(ps2)) == 0)
					ctx = FN_ctx::from_initial(rs);
			}

			// Could not get any handle; give up
			if (sctx == 0 && ctx == 0) {
				ps.set_link_error(rs);
				break;
			}

			// 4.  Resolve link name
			if (sctx) {
				last_resolved_ref = resolve_in_ctx_svc(
				    sctx, *lname, rs, following_link,
				    last_relative_ref);
				delete sctx;

			} else if (ctx) {
				// this lookup may involve links
				// but those links will not be 'detected'
				last_resolved_ref = resolve_in_ctx(
				    ctx, *lname, rs,
				    following_link, last_relative_ref);
				delete ctx;
			}
			if (lname)
				delete lname;

			if (following_link && last_resolved_ref) {
				ps.set_resolved_ref(last_resolved_ref);
				ps.set_link_resolved_ref(last_relative_ref);
				delete last_resolved_ref;
				last_resolved_ref = 0;
				delete last_relative_ref;
				last_relative_ref = 0;
			}

			// if not making progress
			if (!following_link && !last_resolved_ref) {
				ps.set_link_error(rs);
			}

		} else if (!lstack.is_empty() && last_resolved_ref) {
			FN_composite_name *next_name = lstack.pop();
			if (next_name) {
				// resolve top item on stack in context of
				// last resolved reference
				sctx = FN_ctx_svc::from_ref(*last_resolved_ref,
				    rs);
				if (sctx == 0)
					ctx = FN_ctx_svc::from_ref(
					    *last_resolved_ref, rs);
				if (sctx == 0 && ctx == 0)
					ps.set_link_error(rs);
				if (sctx) {
					last_resolved_ref = resolve_in_ctx_svc(
					    sctx, *next_name, rs,
					    following_link, last_relative_ref);
					delete sctx;
				} else if (ctx) {
					// this lookup may involve links
					// that are not counted here
					last_resolved_ref = resolve_in_ctx(
					    ctx, *next_name, rs, following_link,
					    last_relative_ref);
					delete ctx;
				}

				if (following_link && last_resolved_ref) {
					ps.set_resolved_ref(last_resolved_ref);
					ps.set_link_resolved_ref(
					    last_relative_ref);
					delete last_resolved_ref;
					last_resolved_ref = 0;
					delete last_relative_ref;
					last_relative_ref = 0;
				}

				// if not making progress
				if (!following_link && !last_resolved_ref) {
					ps.set_link_error(rs);
				}
				delete next_name;
			} else {
				// probably something wrong here
				// null item on stack
				// %%% more appropriate error code here?
				ps.set(FN_E_LINK_ERROR);
				if (last_resolved_ref) {
					delete last_resolved_ref;
					last_resolved_ref = 0;
				}
				break;
			}
		} else if (lstack.is_empty())
			break; // all done
		else {
			// something is wrong here
			// we are not following links,
			// or, either no trailing components left,
			// or, previous resolve failed.
			break;
		}
	}

	if (link_count >= FN_SPI_LINK_LIMIT && following_link) {
		ps.set(FN_E_LINK_LOOP_LIMIT, last_resolved_ref);
		// %%% what other information is needed here
		ret = 0;
	} else if (lstack.is_empty() && last_resolved_ref) {
		ps.set_success();
		ret = last_resolved_ref;
	} else {
		if (last_resolved_ref)
			delete last_resolved_ref;
	}

	return (ret);
}


// It does not change s.remaining_name(); that needs to be deal with
// by the caller of this routine how to continue the operation on
// the remaining name.  This is so that this routine does not resolve
// more than it intends to (it does not know which is the target context
// and when to stop resolution).
//
static FN_composite_name empty_name((unsigned char *)"");

static int
process_link(FN_status &s,
	    unsigned int continue_code = FN_E_SPI_CONTINUE,
	    FN_ref **answer = 0)
{
	if (should_follow_link(s)) {
		FN_status_psvc lstatus;
		FN_ref *lref = follow_link(s.resolved_ref(),
					    s.link_resolved_ref(),
					    lstatus);

		if (lstatus.is_success()) {
			s.set_resolved_ref(lref);
			if (s.remaining_name())
				s.set_code(FN_E_SPI_CONTINUE);
			else {
				s.set_code(continue_code);
				if (answer)
					*answer = lref;
				else
					delete lref;
				// for continue case; need to supply empty name
				if (s.code() == FN_E_SPI_CONTINUE)
					s.append_remaining_name(empty_name);
			}
		} else {
			// use psvc so that we can use set_link_error
			FN_status_psvc temp(s);
			temp.set_link_error(lstatus);
		}
	}
	return (1);
}

static int
set_status_resolved_name(const FN_composite_name &n, FN_status &s)
{
	void *ip;
	FN_composite_name *res = new FN_composite_name();
	const FN_string *name = n.first(ip);
	const FN_string *remaining_first = 0;

	const FN_composite_name *rem = s.remaining_name();
	if (rem) {
		void *ip1;
		remaining_first = rem->first(ip1);
		while ((name) &&
		       (remaining_first->compare(*name))) {
			res->append_comp(*name);
			name = n.next(ip);
		}
		s.set_resolved_name(res);
	} 
	delete res;
	return (1);
}

FN_ref*
FN_ctx_svc::lookup(const FN_composite_name &n, FN_status &s)
{
	s.set_success(); // clear all fields
	FN_status_psvc ps(s);
	FN_ref *ret = p_lookup(n, 0, ps);

	process_link(s, FN_SUCCESS, &ret);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_lookup(rn, 0, ps);
		delete rc;

		process_link(s, FN_SUCCESS, &ret);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

FN_ref*
FN_ctx_svc::lookup_link(const FN_composite_name &n, FN_status &s)
{
	s.set_success(); // clear all fields
	FN_status_psvc ps(s);
	FN_ref *ret = p_lookup(n, FN_SPI_LEAVE_TERMINAL_LINK, ps);

	process_link(s, FN_SUCCESS, &ret);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_lookup(rn, FN_SPI_LEAVE_TERMINAL_LINK, ps);
		delete rc;
		process_link(s, FN_SUCCESS, &ret);
	}
	if (ret) {
		if (ret->is_link()) {
			set_status_resolved_name(n, s);
			return (ret);
		} else {
			s.set_code(FN_E_MALFORMED_LINK);
			s.set_resolved_ref(ret);
			s.set_resolved_name(&n);
			s.set_remaining_name(0);
			delete ret;
			ret = 0;
		}
	}
	set_status_resolved_name(n, s);
	return (ret);
}

FN_namelist*
FN_ctx_svc::list_names(const FN_composite_name &n, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	FN_namelist* ret = p_list_names(n, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_list_names(rn, ps);
		delete rc;
		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

FN_bindinglist*
FN_ctx_svc::list_bindings(const FN_composite_name &n, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	FN_bindinglist *ret = p_list_bindings(n, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_list_bindings(rn, ps);
		delete rc;
		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

int FN_ctx_svc::bind(const FN_composite_name &n,
    const FN_ref &r, unsigned int f, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	int ret = p_bind(n, r, f, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_bind(rn, r, f, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

int
FN_ctx_svc::unbind(const FN_composite_name &n, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	int ret = p_unbind(n, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_unbind(rn, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

FN_ref*
FN_ctx_svc::create_subcontext(const FN_composite_name &n,
    FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	FN_ref *ret = p_create_subcontext(n, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_create_subcontext(rn, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

int
FN_ctx_svc::destroy_subcontext(const FN_composite_name &n, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	int ret = p_destroy_subcontext(n, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_destroy_subcontext(rn, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

int
FN_ctx_svc::rename(const FN_composite_name &oldname,
    const FN_composite_name &newname, unsigned int flag, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	int ret = p_rename(oldname, newname, flag, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(oldname, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_rename(rn, newname, flag, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(oldname, s);
	return (ret);
}

FN_attrset*
FN_ctx_svc::get_syntax_attrs(const FN_composite_name &n,
    FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	FN_attrset *ret = p_get_syntax_attrs(n, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_get_syntax_attrs(rn, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

FN_attribute*
FN_ctx_svc::attr_get(const FN_composite_name &n,
    const FN_identifier &i, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	FN_attribute* ret = p_attr_get(n, i, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_attr_get(rn, i, ps);
		delete rc;

		process_link(s);

	}
	set_status_resolved_name(n, s);
	return (ret);
}

int
FN_ctx_svc::attr_modify(const FN_composite_name &n, unsigned int i,
    const FN_attribute &a, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	int ret = p_attr_modify(n, i, a, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_attr_modify(rn, i, a, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

FN_valuelist *FN_ctx_svc::attr_get_values(const FN_composite_name &n,
    const FN_identifier &i, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	FN_valuelist *ret = p_attr_get_values(n, i, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_attr_get_values(rn, i, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

FN_attrset*
FN_ctx_svc::attr_get_ids(const FN_composite_name &n, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	FN_attrset *ret = p_attr_get_ids(n, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_attr_get_ids(rn, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

FN_multigetlist*
FN_ctx_svc::attr_multi_get(const FN_composite_name &n, const FN_attrset *a,
    FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	FN_multigetlist *ret = p_attr_multi_get(n, a, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_attr_multi_get(rn, a, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

int
FN_ctx_svc::attr_multi_modify(const FN_composite_name &n,
    const FN_attrmodlist &m, FN_attrmodlist **a, FN_status &s)
{
	s.set_success();  // clear all fields
	FN_status_psvc ps(s);
	int ret = p_attr_multi_modify(n, m, a, ps);

	process_link(s);

	// loop on FN_E_SPI_CONTINUE (guaranteed to make progress or fail)
	while (ps.code() == FN_E_SPI_CONTINUE) {
		FN_ctx_svc *rc;
		FN_status rs;
		if (!(rc = from_ref(*ps.resolved_ref(), rs))) {
			ps.set_code(rs.code());
			set_status_resolved_name(n, s);
			return (0);
		}
		FN_composite_name rn = *ps.remaining_name();
		ret = rc->p_attr_multi_modify(rn, m, a, ps);
		delete rc;

		process_link(s);
	}
	set_status_resolved_name(n, s);
	return (ret);
}

FN_ctx_svc::FN_ctx_svc()
{
}

FN_ctx_svc::~FN_ctx_svc()
{
}

#include <stdio.h>
#include <sys/param.h>

extern void *
fns_link_symbol(const char *function_name, const char *module_name = 0);

// construct from a reference
FN_ctx_svc*
FN_ctx_svc::from_ref(const FN_ref &r, FN_status &s)
{
	// %%% need to support use of reference type for module
	// selection as well
	FN_ctx_svc_t *cp;
	const FN_ref_addr *ap;
	const char *t_cstr;
	void *ip, *fh;
	char mname[MAXPATHLEN], fname[MAXPATHLEN];

	// prime status for case of no supported addresses
	s.set(FN_E_NO_SUPPORTED_ADDRESS);

	// look for supported addresses (and try them)
	for (ap = r.first(ip); ap; ap = r.next(ip)) {
		t_cstr = (const char *)(ap->type()->str());
		if (t_cstr == 0)
			continue;
		sprintf(fname,
		    "%s__fn_ctx_svc_handle_from_ref_addr", t_cstr);
		// look in executable (and linked libraries)
		if (fh = fns_link_symbol(fname)) {
			if (cp = (*((FN_ctx_svc_from_ref_addr_func)fh))
			    ((const FN_ref_addr_t*)ap, (const FN_ref_t*)&r,
			    (FN_status_t *)&s))
				return ((FN_ctx_svc*)cp);
			continue;
		}
		// look in loadable module
		sprintf(mname, "fn_ctx_%s.so", t_cstr);
		if (fh = fns_link_symbol(fname, mname)) {
			if (cp = (*((FN_ctx_svc_from_ref_addr_func)fh))
			    ((const FN_ref_addr_t*)ap, (const FN_ref_t*) &r,
			    (FN_status_t *)&s))
				return ((FN_ctx_svc*)cp);
			continue;
		}
	}
	// give up
	return (0);
}

// get initial context for FN_ctx_svc
FN_ctx_svc*
FN_ctx_svc::from_initial(FN_status &s)
{
	void *fh;
	FN_ctx_svc_t *cp;
	FN_status_t *st = (FN_status_t *)&s;

	// look in executable (and linked libraries)
	if (fh = fns_link_symbol("initial__fn_ctx_svc_handle_from_initial")) {
		cp = ((*((FN_ctx_svc_from_initial_func)fh))(st));
		return ((FN_ctx_svc*)cp);
	}

	// look in loadable module
	if (fh = fns_link_symbol("initial__fn_ctx_svc_handle_from_initial",
	    "fn_ctx_initial.so")) {
		cp = ((*((FN_ctx_svc_from_initial_func)fh))(st));
		return ((FN_ctx_svc*)cp);
	}
	// configuration error
	s.set(FN_E_CONFIGURATION_ERROR);
	return ((FN_ctx_svc*)0);
}

// A subclass of FN_ctx_svc may provide alternate implementation for these
// By default, these are no-ops.
FN_ctx_svc_data_t *
FN_ctx_svc::p_get_ctx_svc_data()
{
	return ((FN_ctx_svc_data_t *)0);
}

int
FN_ctx_svc::p_set_ctx_svc_data(FN_ctx_svc_data_t *)
{
	return (0);
}
