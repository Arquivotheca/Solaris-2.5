#
#ident	"@(#)Makefile	1.9	94/11/20 SMI"
#
# Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
#
# lib/fn/libxfn/Makefile
#
LIBRARYCCC= libxfn.a
#LIBRARY= libxfn.a
VERS = .1

HDRS= xfn.h misc_codes.h \
	FN_ctx.h \
	FN_composite_name.h \
	FN_string.h \
	FN_ref.h \
	FN_ref_addr.h \
	FN_attribute.h \
	FN_attrset.h \
	FN_attrmodlist.h \
	FN_compound_name.h \
	FN_status.h \
	FN_identifier.h \
	FN_attrvalue.h \
	FN_nameset.h \
	FN_bindingset.h 

HHDRS= xfn.hh fn_xdr.hh \
	FN_ctx.hh \
	FN_composite_name.hh \
	FN_string.hh \
	FN_ref.hh \
	FN_ref_addr.hh \
	FN_attribute.hh \
	FN_attrset.hh \
	FN_attrmodlist.hh \
	FN_compound_name.hh \
	FN_status.hh \
	FN_identifier.hh \
	FN_attrvalue.hh \
	FN_nameset.hh \
	FN_bindingset.hh \
	fns_symbol.hh

ROOTDIRS= $(ROOT)/usr/include/xfn
ROOTHDRS= $(HDRS:%=$(ROOTDIRS)/%)
FNHHDRS= $(HHDRS:%=$(FNINCDIR)/xfn/%)
CHECKHDRS= $(HDRS:%.h=%.check)

IVYOBJS= FN_string.o FN_string_rep.o FN_string_char.o FN_string_wchar.o \
	FN_identifier.o \
	List.o NameList.o FN_composite_name.o \
	AddressList.o 	Set.o NameSet.o BindingSet.o AttrSet.o AttrValSet.o \
	FN_attrmodlist.o AttrModList.o \
	FN_ctx.o FN_compound_name.o FN_ref.o FN_ref_addr.o FN_attrvalue.o\
	FN_status.o FN_bindingset.o FN_nameset.o FN_attrset.o FN_attribute.o

IVYCOBJS= FN_string_c.o FN_composite_name_c.o FN_ref_c.o FN_ref_addr_c.o\
	FN_status_c.o FN_bindingset_c.o FN_nameset_c.o FN_attrset_c.o \
	FN_attrmodlist_c.o FN_ctx_c.o FN_compound_name_c.o FN_attribute_c.o

UTILOBJS= FN_ref_serial.o FN_ref_serial_xdr.o fns_symbol.o \
	FN_attr_serial.o FN_attr_serial_xdr.o hash.o

OBJECTS= $(IVYOBJS) $(UTILOBJS) $(IVYCOBJS)

# include library definitions
include ../Makefile.libfn

ROOTLIBDIR=	$(ROOT)/usr/lib

LIBS = $(DYNLIBCCC)
#LIBS = $(DYNLIB)

# dl for dlopen; 
# nsl for xdr routines; 
# intl for get_text
# C C++
LDLIBS += -ldl -lnsl -lintl -lw -lC -lc

.KEEP_STATE:

all: install_h private_h $(LIBS)

install debug tcov: install_h private_h $(ROOTLIBDIR) $(ROOTLIBS) \
		$(ROOTLINKSCCC)

#install debug tcov: install_h private_h $(ROOTLIBDIR) $(ROOTLIBS) $(ROOTLINKS)

install_h: $(ROOTDIRS) $(ROOTHDRS)

private_h: $(FNINCDIR) $(FNINCDIR)/xfn $(FNHHDRS)

check: $(CHECKHDRS)

FN_ref_serial.h: FN_ref_serial.x
	$(RPCGEN) -C -h -o FN_ref_serial.h FN_ref_serial.x 
FN_ref_serial_xdr.c: FN_ref_serial.h
	$(RPCGEN) -C -c -o FN_ref_serial_xdr.c FN_ref_serial.x 
FN_ref_serial.cc: FN_ref_serial.h

FN_attr_serial.h: FN_attr_serial.x
	$(RPCGEN) -C -h -o FN_attr_serial.h FN_attr_serial.x 
FN_attr_serial_xdr.c: FN_attr_serial.h
	$(RPCGEN) -C -c -o FN_attr_serial_xdr.c FN_attr_serial.x 
FN_attr_serial.cc: FN_attr_serial.h

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# include library targets
include ../Makefile.targ
