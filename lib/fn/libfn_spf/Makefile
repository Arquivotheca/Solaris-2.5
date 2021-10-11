#
#ident	"@(#)Makefile	1.7	94/11/20 SMI"
#
# Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
#
# lib/fn/libfn_spf/Makefile
#
LIBRARYCCC= libfn_spf.a
VERS = .1

HDRS= fn_spi.h FN_syntax_standard.h

HHDRS= fn_spi.hh \
	FN_syntax_standard.hh\
	FN_status_svc.hh FN_compound_name_standard.hh \
	FN_namelist_svc.hh FN_bindinglist_svc.hh \
	FN_multigetlist_svc.hh FN_valuelist_svc.hh

ROOTDIRS= $(ROOT)/usr/include/xfn
ROOTHDRS= $(HDRS:%=$(ROOTDIRS)/%)
FNHHDRS= $(HHDRS:%=$(FNINCDIR)/xfn/%)
CHECKHDRS= $(HDRS:%.h=%.check)

TRLOBJS= \
	FN_ctx_svc.o \
	FN_ctx_csvc.o \
	FN_ctx_asvc.o \
	FN_ctx_csvc_strong.o \
	FN_ctx_asvc_strong.o \
	FN_ctx_cnsvc.o \
	FN_ctx_cnsvc_impl.o \
	FN_ctx_cnsvc_weak_static.o \
	FN_ctx_cnsvc_weak_dynamic.o \
	FN_ctx_csvc_weak_static.o \
	FN_ctx_csvc_weak_dynamic.o \
	FN_ctx_asvc_weak.o \
	FN_ctx_asvc_weak_dynamic.o \
	FN_ctx_asvc_weak_static.o \
	FN_namelist_svc.o FN_bindinglist_svc.o \
	FN_multigetlist_svc.o FN_valuelist_svc.o \
	FN_status_svc.o \
	FN_compound_name_standard.o FN_syntax_standard.o

OBJECTS= $(TRLOBJS)

# include library definitions
include ../Makefile.libfn

ROOTLIBDIR=	$(ROOT)/usr/lib

LIBS = $(DYNLIBCCC)

# intl for get_text
# C for C++
# xfn for client lib
LDLIBS += -lxfn -lintl -lC -lc 

#CPPFLAGS += -I$(SRC)/lib/libxfn

.KEEP_STATE:

all: install_h private_h $(LIBS)

install debug tcov: install_h private_h $(ROOTLIBDIR) $(ROOTLIBS) \
		$(ROOTLINKSCCC) install_symlink

install_h: $(ROOTDIRS) $(ROOTHDRS)

private_h: $(FNINCDIR) $(FNINCDIR)/xfn $(FNHHDRS)

check: $(CHECKHDRS)

install_symlink: $(FNLIBDIR)
	$(RM) $(FNLIBDIR)/fn_compound_name_standard.so
	$(SYMLINK) ../libfn_spf.so $(FNLIBDIR)/fn_compound_name_standard.so

analyse:
	$(CODEMGR_WS)/test.fns/bin/analyse

# include library targets
include ../Makefile.targ
