#
#ident	"@(#)Makefile	1.24	94/10/04 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libw/Makefile
#
LIBRARY= libw.a
VERS = .1

OBJECTS= \
	iswctype.o \
	libwcollate.o \
	strtows.o \
	wcollate.o \
	wscasecmp.o	wscat.o		wschr.o		wscmp.o\
	wscol.o		wscpy.o		wscspn.o	wsdup.o\
	wslen.o		wsncasecmp.o\
	wsncat.o	wsncmp.o	wsncpy.o	wspbrk.o\
	wsrchr.o	wssize.o	wsspn.o			\
	wstok.o		wstol.o		wstoll.o	wstod.o	\
	wsxfrm.o	wcsxfrm.xpg4.o \
	wcstoul.o	wcswcs.o	wcswidth.o\
	fgetwc.o	fgetws.o	fputwc.o	fputws.o\
	getwchar.o	getws.o		putwchar.o	putws.o\
	trwctype.o	mbftowc.o\
	ungetwc.o	wsprintf.o\
	wsscanf.o	scrwidth.o	wcsftime.o

# include library definitions
include ../Makefile.lib

# building an archive
BUILD.AR= $(RM) $@ ; \
	$(AR) q $@ $(OBJECTS:%=$(DIR)/%)

LIBS += $(DYNLIB)
LDLIBS += -ldl 
CPPFLAGS += -D_REENTRANT -I../libc/port/gen  -I../libc/inc

# definitions for install_h target
HDRS=		libw.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
ROOTSHLIBDIR=	$(ROOT)/usr/lib/shlib.etc
ROOTSHLIB=	$(LIB_PIC:%=$(ROOTSHLIBDIR)/%)

CHECKHDRS=	$(HDRS:%.h=%.check)

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)

.KEEP_STATE:

all: $(LIBS) $(LIB_PIC)

install: all $(ROOTLIBS)  $(ROOTLINKS)

install_h: $(ROOTHDRS)

check:	$(CHECKHDRS)

# install rules for lib_pic.a
$(ROOTSHLIBDIR):
	$(INS.dir)

$(ROOTSHLIBDIR)/%: %
	$(INS.file)

# include library targets
include ../Makefile.targ

$(LIB_PIC) := DIR = pics
$(LIB_PIC): pics $$(PICS)
	$(BUILD.AR)
	$(POST_PROCESS_A)

# special cases
objs/wstod.o pics/wstod.o profs/wstod.o := CPPFLAGS += -I../libc/$(MACH)/fp
