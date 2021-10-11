/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)getexports.cc	1.1	94/12/05 SMI"

#include <stdlib.h>
#include <string.h>
#include <synch.h>
#include <time.h>
#include <rpc/rpc.h>
#include <rpcsvc/mount.h>
#include <xfn/xfn.hh>
#include "List.hh"
#include "Export.hh"
#include "getexports.hh"


// Add the named directory to the export tree.
//
static int add_export(ExportNode *node, const FN_composite_name &name);


// Using the mount protocol, return a list of the directories exported
// by hostname, or NULL if none found.

static exports get_export_list(const char *hostname);


// Free a list of exported directories as returned by get_export_list().

static void free_export_list(exports ex);


// Time-to-live (in seconds) of an ExportTree.

#define	TTL 180

// The following is used to adjust the timeouts used for calls to the
// remote rpcbind.  See lib/libnsl/rpc/rpcb_clnt.c.

extern "C" bool_t __rpc_control(int request, void *info);

#define	CLCR_GET_RPCB_TIMEOUT 1
#define	CLCR_SET_RPCB_TIMEOUT 2
#define	CLCR_SET_RPCB_RMTTIME 5
#define	CLCR_GET_RPCB_RMTTIME 6

#define	RPCB_TIMEOUT 15		// seconds
#define	RPCB_RETRANS 15		// seconds

// Timeout for mount protocol calls.

#define	MOUNT_TIMEOUT 25	// seconds


// A list of all the ExportTree's.  The lock is held while reading
// or modifying the list, and while incrementing any tree's reference
// count.

static List host_list;
static rwlock_t host_list_lock = DEFAULTRWLOCK;


ExportTree *
export_tree(const char *hostname)
{
	ExportTree *tree;
	void *iter;
	time_t now = time(NULL);
	bool_t cleanup = FALSE;	// Are there any trees whose times have come?

	// See if the export tree is cached.  It is safe to test
	// if the refcount is zero, since setting "cleanup" is only
	// advisory.  When a cached tree is returned, its refcount
	// is incremented.

	rw_rdlock(&host_list_lock);
	for (tree = (ExportTree *)host_list.first(iter);
	    tree != NULL;
	    tree = (ExportTree *)host_list.next(iter)) {
		if (tree->refcount() == 0 &&
		    (tree->hostname == NULL ||
		    tree->creation_time + TTL < now)) {
			cleanup = TRUE;
		} else if (strcmp(tree->hostname, hostname) == 0) {
			tree->increment_refcount();
			break;	// found it
		}
	}
	rw_unlock(&host_list_lock);

	// Uproot the trees that have been around for too long.  It is
	// safe to destroy a tree whose refcount is zero, since
	// refcounts are never incremented outside the lock.

	if (cleanup) {
		rw_wrlock(&host_list_lock);
		ExportTree *tree2;
		for (tree2 = (ExportTree *)host_list.first(iter);
		    tree2 != NULL;
		    tree2 = (ExportTree *)host_list.next(iter)) {
			if (tree2->refcount() == 0 &&
			    (tree2->hostname == NULL ||
			    tree2->creation_time + TTL < now)) {
				host_list.delete_item(iter);
			}
		}
		rw_unlock(&host_list_lock);
	}

	if (tree != NULL) {
		return (tree);
	}

	// Plant a new tree and add it to host_list.

	exports ex = get_export_list(hostname);
	if (ex != NULL) {
		tree = new ExportTree(hostname);
		if (tree != NULL) {
			exports e;
			for (e = ex; e != NULL; e = e->ex_next) {
				char *dir = e->ex_dir;
				if (dir == NULL) {
					continue;
				} else if (dir[0] == '/') {
					dir++;
				}
				size_t len = strlen(dir);
				if (dir[len - 1] == '/') {
					dir[len - 1] = '\0';
				}
				FN_composite_name name((unsigned char *)dir);
				if (add_export(&tree->root, name) == 0) {
					delete tree;
					tree = NULL;
					break;
				}
			}
		}
	}
	if (ex != NULL) {
		free_export_list(ex);
	}
	if (tree != NULL) {
		rw_wrlock(&host_list_lock);
		// refcount was initialized to 1
		host_list.prepend_item(tree);
		rw_unlock(&host_list_lock);
	}
	return (tree);
}


void
release_export_tree(ExportTree *tree)
{
	if (tree != NULL) {
		tree->decrement_refcount();
	}
}


static int
add_export(ExportNode *node, const FN_composite_name &name)
{
	if (name.is_empty()) {	// root
		node->exported = TRUE;
		return (1);
	}
	void *iter;
	const FN_string *atom;
	for (atom = name.first(iter); atom != NULL; atom = name.next(iter)) {
		node = node->insert(*atom);
		if (node == NULL) {
			return (0);
		}
	}
	node->exported = TRUE;
	return (1);
}


static exports
get_export_list(const char *hostname)
{
	// %%% Adjust timeout and retransmission time for calls to the
	// remote rpcbind.  Setting/restoring the timeouts in this way
	// may not be so hot in an MT environment.

	struct timeval rpcb_timeout_old;
	struct timeval rpcb_timeout_new = {RPCB_TIMEOUT, 0};
	struct timeval rpcb_retrans_old;
	struct timeval rpcb_retrans_new = {RPCB_RETRANS, 0};
	__rpc_control(CLCR_GET_RPCB_TIMEOUT, &rpcb_timeout_old);
	__rpc_control(CLCR_SET_RPCB_TIMEOUT, &rpcb_timeout_new);
	__rpc_control(CLCR_GET_RPCB_RMTTIME, &rpcb_retrans_old);
	__rpc_control(CLCR_SET_RPCB_RMTTIME, &rpcb_retrans_new);

	// Circuit is preferable since it can handle arbitrarily long
	// export lists.  Fall back on datagram if necessary.
	CLIENT *cl = clnt_create(hostname, MOUNTPROG, MOUNTVERS, "circuit_v");
	if (cl == NULL) {
		cl = clnt_create(hostname, MOUNTPROG, MOUNTVERS, "datagram_v");
	}
	__rpc_control(CLCR_SET_RPCB_TIMEOUT, &rpcb_timeout_old);
	__rpc_control(CLCR_SET_RPCB_RMTTIME, &rpcb_retrans_old);

	if (cl == NULL) {
		return (NULL);
	}

	exports ex = NULL;	/* export list returned by clnt_call */
	struct timeval mount_timeout = {MOUNT_TIMEOUT, 0};
	enum clnt_stat err;
	err = clnt_call(cl, MOUNTPROC_EXPORT, xdr_void, NULL,
			(xdrproc_t)xdr_exports, (caddr_t)&ex, mount_timeout);
	clnt_destroy(cl);
	return (ex);
}


static void
free_export_list(exports ex)
{
	while (ex != NULL) {
		exports next = ex->ex_next;
		free(ex->ex_dir);
		groups gr = ex->ex_groups;
		while (gr != NULL) {
			groups next = gr->gr_next;
			free(gr->gr_name);
			free(gr);
			gr = next;
		}
		free(ex);
		ex = next;
	}
}
