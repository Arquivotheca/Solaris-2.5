/*
 * $Source: /afs/athena.mit.edu/astaff/project/kerberos/src/lib/krb/RCS/get_tf_realm.c,v $
 * $Author: jtkohl $
 *
 * Copyright 1987, 1988 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>.
 */

#ifndef lint
static char rcsid_get_tf_realm_c[] =
"$Id: get_tf_realm.c,v 4.2 90/01/02 13:40:19 jtkohl Exp $";
#endif /* lint */

#include <kerberos/mit-copyright.h>
#include <kerberos/krb.h>
#ifdef SYSV
#include <string.h>
#else
#include <strings.h>
#endif /* SYSV */

/*
 * This file contains a routine to extract the realm of a kerberos
 * ticket file.
 */

/*
 * krb_get_tf_realm() takes two arguments: the name of a ticket 
 * and a variable to store the name of the realm in.
 * 
 */

krb_get_tf_realm(ticket_file, realm)
  char *ticket_file;
  char *realm;
{
    return(krb_get_tf_fullname(ticket_file, 0, 0, realm));
}
