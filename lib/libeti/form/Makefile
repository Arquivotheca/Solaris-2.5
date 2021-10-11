#
#ident	"@(#)Makefile	1.6	93/02/17 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libeti/form/Makefile
#
LIBRARY=	libform.a

OBJECTS=  \
chg_char.o     field_buf.o    field_term.o   form_term.o    ty_alnum.o \
chg_data.o     field_fore.o   field_user.o   form_user.o    ty_alpha.o \
chg_field.o    field_init.o   fieldtype.o    form_win.o     ty_enum.o \
chg_page.o     field_just.o   form.o         ty_int.o \
driver.o       field_opts.o   form_init.o    post.o         ty_num.o \
field.o        field_pad.o    form_opts.o    regcmp.o       ty_regexp.o \
field_back.o   field_stat.o   form_sub.o     regex.o        utility.o \

# include library definitions
include ../../Makefile.lib

ROOTLIBDIR= $(ROOT)/usr/ccs/lib

# TXTS allows the AT&T makefile to be bootstrapped into the NSE.
TXTS=

# definitions for lint target
LIBS += $(LINTLIB)
LINTSRC= $(LINTLIB:%.ln=%)
ROOTLINT= $(LINTSRC:%=$(ROOTLIBDIR)/%)

# definitions for install_h target
HDRS=		form.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)
CHECKHDRS=	$(HDRS:%.h=%.check)

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)

.KEEP_STATE:

all: $(TXTS) $(LIBS)

llib-lform: llib-lform.c
	    $(RM) $@
	    cp llib-lform.c $@

$(LINTLIB):= SRCS=llib-lform
$(LINTLIB):= LINTFLAGS=-nvx
$(LINTLIB):= TARGET_ARCH=

install: all $(ROOTLIBS) $(ROOTLINT)

install_h: $(ROOTHDRS)

check:	$(CHECKHDRS)

# include library targets
include ../../Makefile.targ
