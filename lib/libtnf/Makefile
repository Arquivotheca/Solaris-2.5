#
#ident	"@(#)Makefile	1.6	95/01/26 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libtnf/Makefile
#

LIBRARY=	libtnf.a
VERS=		.1
OBJECTS=	$(MACH)dep.o	\
		ref.o		\
		abi.o		\
		util.o		\
		info.o		\
		datum.o		\
		record.o	\
		scalar.o	\
		array.o		\
		struct.o	\
		type.o		\
		error.o		\
		access.o	\
		reader.o

include ../Makefile.lib

LIBS=		$(DYNLIB)

LDLIBS += -lc -lintl

HDRS=		tnf.h
ROOTHDRDIR=	$(ROOT)/usr/include/tnf
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS=	$(HDRS:%.h=%.check)
$(ROOTHDRS) := 	FILEMODE = 0644
CHECKHDRS =	$(HDRS:%.h=%.check)

$(ROOTHDRS) :=	FILEMODE = 644

CPPFLAGS +=	-D_DATUM_MACROS -D_REENTRANT

LINTFLAGS +=	-y

.KEEP_STATE:

all: $(LIBS)

install: all $(ROOTLIBDIR) $(ROOTLIBS) $(ROOTLINKS)

install_h: $(ROOTHDRDIR) $(ROOTHDRS)

lint:
	$(LINT.c) $(SRCS)

check: $(CHECKHDRS)

$(ROOTLIBDIR) $(ROOTHDRDIR):
	$(INS.dir)

$(ROOTHDRDIR)/% : %
	$(INS.file)

ASFLAGS=	-K pic -P -D_SYS_SYS_S -D_LOCORE -D_ASM -DPIC -DLOCORE $(CPPFLAGS)
BUILD.s=	$(AS) $(ASFLAGS) $< -o $@

pics/%.o:	./%.s
		$(BUILD.s)
		$(POST_PROCESS_O)

objs/%.o:	./%.s
		$(BUILD.s)
		$(POST_PROCESS_O)

include ../Makefile.targ
