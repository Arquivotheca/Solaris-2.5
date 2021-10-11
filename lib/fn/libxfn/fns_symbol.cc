/*
 * Copyright (c) 1993 - 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)fns_symbol.cc	1.7 94/11/16 SMI"

#include <stdio.h>
#include <dlfcn.h>
#include <sys/param.h>
#include <string.h>
#include <synch.h>

#include "fns_symbol.hh"


// When compiled with -DDEBUG:
//
//    - User may add directories (such as $ROOT/usr/lib/fn) to
//	$LD_LIBRARY_PATH; they will be searched before /usr/lib/fn.
//
//    - FNS_library_path is global, so we can use nm to see if we compiled the
//	library with DEBUG.


#ifndef DEBUG
static
#endif
const char	*FNS_library_path = "/usr/lib/fn";

void *
fns_link_symbol(const char *function_name, const char *module_name)
{
	static mutex_t	self_lock = DEFAULTMUTEX;
	static void	*self = 0;
	void		*mh, *fh;

	mutex_lock(&self_lock);
	if (self == 0)
		self = dlopen(0, RTLD_LAZY);
	mh = self;
	mutex_unlock(&self_lock);

#ifdef DEBUG
	if (module_name != 0) {
		/* look in LD_LIBRARY_PATH */
		if ((mh = dlopen(module_name, RTLD_LAZY)) != 0) {
			if ((fh = dlsym(mh, function_name)) != 0)
				return (fh);
			else
				dlclose(mh);
		}
	}
#endif

	char mname[MAXPATHLEN];

	if (module_name != 0) {
		sprintf(mname, "%s/%s", FNS_library_path, module_name);
		if ((mh = dlopen(mname, RTLD_LAZY)) != 0) {
			if ((fh = dlsym(mh, function_name)) != 0)
				return (fh);
			else
				dlclose(mh);
		}
	} else {
		if (mh && (fh = dlsym(mh, function_name)) != 0)
			return (fh);
	}
	return (0);
}
