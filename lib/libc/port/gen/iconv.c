/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ident  "@(#)iconv.c 1.7     94/11/09 SMI"      

#pragma weak iconv_open = _iconv_open
#pragma weak iconv_close = _iconv_close
#pragma weak iconv = _iconv

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <iconv.h>
#include "iconvP.h"

#define	IMD	"UTF2"

static	iconv_p iconv_open_private();

/*
 * These functions are implemented using a shared object and the dlopen()
 * functions.   Then, the actual conversion  algorithmn for a particular
 * conversion is implemented as a shared object in a separate file in
 * a loadable conversion module and linked dynamically at run time.
 * The loadable conversion module resides in
 *	/usr/lib/iconv/fromcode%tocode.so
 * where fromcode is the source encoding and tocode is the target encoding.
 * The module has 3 entries: _icv_open(), _icv_iconv(),  _icv_close().
 */

iconv_t
_iconv_open
(
	const char *tocode,
	const char *fromcode
)
{
	char 		lib_f[MAXPATHLEN];
	char 		lib_t[MAXPATHLEN];
	struct stat	statbuf;
	iconv_t 	cd;


	/*
	 * First, try using one direct conversion with
	 * lib, which is set to /usr/lib/iconv/fromcode%%tocode.so
	 * If direct conversion cannot be done, use the intermediate
	 * conversion.
	 */

	if ((cd = (iconv_t) malloc(sizeof (struct _iconv_info))) == NULL)
		return ((iconv_t)-1);

	sprintf(lib_f, "/usr/lib/iconv/%s%%%s.so", fromcode, tocode);
	cd->_to = NULL;

	if ((cd->_from = iconv_open_private(lib_f)) == (iconv_p)-1) {
		sprintf(lib_f, "/usr/lib/iconv/%s%%%s.so", fromcode, IMD);
		sprintf(lib_t, "/usr/lib/iconv/%s%%%s.so", IMD, tocode);

		if ((cd->_from = iconv_open_private(lib_f)) == (iconv_p)-1) {
			free(cd);
			return ((iconv_t)-1);
		}
		if ((cd->_to = iconv_open_private(lib_t)) == (iconv_p)-1) {
			free(cd->_from);
			free(cd);
			return ((iconv_t)-1);
		}
	}

	return (cd);
}

static iconv_p
iconv_open_private(const char *lib)
{
	int 	(*fptr)();
	iconv_p cdpath;

	if ((cdpath = (iconv_p)malloc(sizeof (struct _iconv_fields))) == NULL)
		return ((iconv_p)-1);

	if ((cdpath->_icv_handle = dlopen(lib, RTLD_LAZY)) == 0) {
		errno = EBADF;
		free(cdpath);
		return ((iconv_p)-1);
	}

	/* gets address of _icv_open */
	if ((fptr = (int (*)())dlsym(cdpath->_icv_handle, "_icv_open")) == 0) {
		dlclose(cdpath->_icv_handle);
		free(cdpath);
		return ((iconv_p)-1);
	}

	/*
	 * gets address of _icv_iconv in the loadable conversion module
	 * and stores it in cdpath->_icv_iconv
	 */

	if ((cdpath->_icv_iconv = (size_t(*)(size_t)) dlsym(cdpath->_icv_handle,
				"_icv_iconv")) == 0) {
		dlclose(cdpath->_icv_handle);
		free(cdpath);
		return ((iconv_p)-1);
	}

	/*
	 * gets address of _icv_close in the loadable conversion module
	 * and stores it in cd->_icv_close
	 */
	if ((cdpath->_icv_close = (void(*)(void)) dlsym(cdpath->_icv_handle,
				"_icv_close")) == 0) {
		dlclose(cdpath->_icv_handle);
		free(cdpath);
		return ((iconv_p)-1);
	}

	/* initialize the state of the actual _icv_iconv conversion routine */
	if ((cdpath->_icv_state = (void *)(*fptr)()) == (struct _icv_state *)-1) {
		errno = ELIBACC;
		dlclose(cdpath->_icv_handle);
		free(cdpath);
		return ((iconv_p)-1);
	}

	return (cdpath);
}

void
_iconv_close(iconv_t cd)
{
	(*(cd->_from)->_icv_close)(cd->_from->_icv_state);
	dlclose(cd->_from->_icv_handle);
	free(cd->_from);
	if (cd->_to != NULL) {
		(*(cd->_to)->_icv_close)(cd->_to->_icv_state);
		dlclose(cd->_to->_icv_handle);
		free(cd->_to);
	}
	free(cd);
}

size_t
_iconv
(
	iconv_t cd,
	const char **inbuf,
	size_t *inbytesleft,
	const char **outbuf,
	size_t *outbytesleft
)
{
	char 	tmp_buf[BUFSIZ];
	char 	*tmp_ptr;
	size_t	tmp_size;
	size_t	ret;

	/* direct conversion */
	if (cd->_to == NULL)
		return ((*(cd->_from)->_icv_iconv)(cd->_from->_icv_state,
			inbuf, inbytesleft, outbuf, outbytesleft));

	/*
	 * use intermediate conversion codeset.  tmp_buf contains the
	 * results from the intermediate conversion.
	 *
	 * the premature or incomplete conversion will be done in the actual
	 * conversion routine itself.
	 */
	tmp_ptr = tmp_buf;
	tmp_size = BUFSIZ;	 /* number of bytes left in the output buffer */

	ret = (*(cd->_from)->_icv_iconv)(cd->_from->_icv_state, inbuf,
		inbytesleft, &tmp_ptr, &tmp_size);

	if (ret == (size_t)-1)
		return ((size_t)-1);
	else {
		tmp_ptr = tmp_buf;
		tmp_size = BUFSIZ - tmp_size;

		return ((*(cd->_to)->_icv_iconv)(cd->_to->_icv_state, &tmp_ptr,
			&tmp_size, outbuf, outbytesleft));
	}
}
