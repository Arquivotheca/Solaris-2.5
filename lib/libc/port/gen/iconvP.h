/*	Copyright (c) 1993 SMI  */
/*	All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)iconvP.h	1.8	93/10/04 SMI"

struct _iconv_fields {
	void *_icv_handle;
	size_t (*_icv_iconv)();
	void (*_icv_close)();
	void *_icv_state;
};

typedef struct _iconv_fields *iconv_p;

struct _iconv_info {
	iconv_p _from;		/* conversion codeset for source code to UTF2 */
	iconv_p _to;		/* conversion codeset for UTF2 to target code */
	size_t  bytesleft;    	/* used for premature/incomplete conversion */
};
