#
#ident  "@(#)Makefile.com 1.3     94/09/07 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# Makefile to support tools used for linker development:
#
#  o	elfdump provides a mechanism of dumping the information within elf
#	files (see also dump(1)).
#
#  o	0@0.so.1 provides for preloading a zero at zero.
#
# Note, these tools are not part of the product.
#
# cmd/sgs/tools/Makefile.com

include		../../../Makefile.cmd

SGSPROTO=	../../proto/$(MACH)

COMOBJS=	elfdump.o

LIBOBJS=	0@0.o

OBJECTS=	$(COMOBJS)  $(LIBOBJS)

PROGS=		$(COMOBJS:%.o=%)
LIBS=		$(LIBOBJS:%.o=%.so.1)
SRCS=		$(COMOBJS:%.o=../common/%.c)  $(LIBOBJS:%.o=../common/%.c)

CPPFLAGS +=	-I../../include -I../../include/$(MACH)
LDFLAGS +=	-Yl,$(SGSPROTO)
CLEANFILES +=	$(LINTOUT)
LINTFLAGS=	-ax

ROOTDIR=	$(ROOT)/opt/SUNWonld
ROOTPROGS=	$(PROGS:%=$(ROOTDIR)/bin/%)
ROOTLIBS=	$(LIBS:%=$(ROOTDIR)/lib/%)

$(LIBOBJS) :=	CPPFLAGS += -K pic
$(LIBS) :=	LDFLAGS += -G -ztext

FILEMODE=	0755
