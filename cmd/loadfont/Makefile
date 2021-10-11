#
#pragma ident "@(#)Makefile	1.12	93/09/08 SMI"
#
# Copyright (c) 1993, by Sun Microsystems, Inc.
#
# cmd/loadfont/Makefile

include ../Makefile.cmd

PROG = loadfont
DEFAULTFONT = defaultfont

FONTS	=	437.bdf		646g.bdf	646y.bdf	850.bdf	   \
		861.bdf		863.bdf		865.bdf		866.bdf	   \
		8859-1.bdf 	8859-2.bdf	8859-3.bdf	8859-4.bdf \
		8859-5.bdf	8859-7.bdf	8859-9.bdf		   \
		csfr.bdf	greek.bdf

OBJS = loadfont.o

SRCS = $(OBJS:%.o=%.c)

# CPPFLAGS += -D_EGA
LDLIBS += -lintl

ROOTFONTDIR = $(ROOTSHLIB)/fonts
ROOTDEFAULTFONT = $(DEFAULTFONT:%=$(ROOTETC)/%)
ROOTFONTS   = $(FONTS:%=$(ROOTFONTDIR)/%)
ROOTFONTSLN = $(ROOTFONTDIR)/8859.bdf

$(ROOTDEFAULTFONT) := FILEMODE = 0644
$(ROOTDEFAULTFONT) := OWNER = root
$(ROOTDEFAULTFONT) := GROUP = sys

$(ROOTFONTDIR) := DIRMODE = 0555
$(ROOTFONTS) := FILEMODE = 0444

.KEEP_STATE:

all:  $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG) $(ROOTDEFAULTFONT)	\
	 $(ROOTFONTDIR) $(ROOTFONTS) $(ROOTFONTSLN)

$(ROOTFONTDIR): 
	$(INS.dir)

$(ROOTFONTDIR)/%: %
	$(INS.file)

$(ROOTFONTSLN):
	$(RM) $@; $(SYMLINK) 8859-1.bdf $@

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

include ../Makefile.targ
