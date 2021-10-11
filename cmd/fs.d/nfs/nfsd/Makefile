#
#ident	"@(#)Makefile	1.11	92/09/15 SMI"	/* SVr4.0 1.	*/
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

FSTYPE=		nfs
TYPEPROG=	nfsd
ATTMK=		$(TYPEPROG)

include		../../Makefile.fstype

LDLIBS +=	-lnsl

SRCS=		nfsd.c

lint:	lint_SRCS

