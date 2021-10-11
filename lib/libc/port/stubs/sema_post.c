/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)sema_post.c 1.5	94/01/26 SMI"

#pragma weak _sema_post = _sema_post_stub
#pragma weak sema_post = _sema_post_stub

int
_sema_post_stub()
{
	return (0);
}
