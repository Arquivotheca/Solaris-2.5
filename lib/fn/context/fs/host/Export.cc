/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)Export.cc	1.1	94/12/05 SMI"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <synch.h>
#include <xfn/xfn.hh>
#include <rpc/rpc.h>
#include "List.hh"
#include "Export.hh"


static FN_string empty_string;


// Implementation of class ExportNode.


ExportNode::ExportNode(const FN_string &name, ExportTree *tree)
{
	this->name = name;
	this->exported = FALSE;
	this->tree = tree;
}

ExportNode::~ExportNode()
{
}

ExportNode *
ExportNode::find_child(const FN_string &name)
{
	ExportNode *child;
	void *iter;
	for (child = (ExportNode *)List::first(iter);
	    child != NULL;
	    child = (ExportNode *)List::next(iter)) {
		if (child->name.compare(name) == 0) {
			return (child);
		}
	}
	return (NULL);
}

ExportNode *
ExportNode::insert(const FN_string &name)
{
	ExportNode *child = find_child(name);
	if (child == NULL) {
		child = new ExportNode(name, tree);
		if (child == NULL ||
		    append_item(child) == 0) {
			delete child;
			child = NULL;
		}
	}
	return (child);
}

void
ExportNode::print(unsigned int depth)
{
	const unsigned int offset = 4;

	const char *namestr = (const char *)name.str();
	namestr = (namestr != NULL) ? namestr : "[??]";

	printf("%*s%s %s\n", depth * offset, "", namestr, exported ? "X" : "");
	ExportNode *child;
	void *iter;
	for (child = (ExportNode *)List::first(iter);
	    child != NULL;
	    child = (ExportNode *)List::next(iter)) {
		child->print(depth + 1);
	}
}

ListItem *
ExportNode::copy()
{
	return (NULL);	/* not implemented */
}


// Implementation of class ExportTree.


ExportTree::ExportTree(const char *hostname)
	: root(empty_string)
{
	root.tree = this;
	this->hostname = new char[strlen(hostname) + 1];
	if (this->hostname != NULL) {
		strcpy(this->hostname, hostname);
	}
	creation_time = time(NULL);
	_refcount = 1;
	mutex_init(&refcount_lock, USYNC_THREAD, NULL);
}

ExportTree::~ExportTree()
{
	delete[] hostname;
	mutex_destroy(&refcount_lock);
}

unsigned int
ExportTree::refcount()
{
	mutex_lock(&refcount_lock);
	unsigned int rc = _refcount;
	mutex_unlock(&refcount_lock);
	return (rc);
}


void
ExportTree::increment_refcount()
{
	mutex_lock(&refcount_lock);
	_refcount++;
	mutex_unlock(&refcount_lock);
}

void
ExportTree::decrement_refcount()
{
	mutex_lock(&refcount_lock);
	_refcount--;
	mutex_unlock(&refcount_lock);
}

void
ExportTree::print()
{
	printf("%s (%u):\n", hostname, creation_time);
	root.print();
}

ListItem *
ExportTree::copy()
{
	return (NULL);	/* not implemented */
}
