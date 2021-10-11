#
#ident	"@(#)Makefile	1.9	95/01/06 SMI"        
#
# Copyright (c) 1990, 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/iconv/Makefile

PROG=kbdcomp 
IPROG=iconv

NATIVE=native
NPROG=$(NATIVE)/$(PROG)

IOBJS= iconv.o gettab.o process.o
KOBJS= main.o gram.o lexan.o output.o reach.o sort.o sym.o tree.o
KSRCS= $(KOBJS:%.o=%.c)
NKOBJS= $(KOBJS:%=$(NATIVE)/%)
ROOTIPROG= $(IPROG:%=$(ROOTBIN)/%)

CLOBBERFILES= $(NPROG) $(IPROG)

include ../Makefile.cmd

#
# Message catalog
#
POFILES= $(IOBJS:%.o=%.po)
POFILE= iconv_.po

CPPFLAGS= -I. $(CPPFLAGS.master)

ROOTDIRS= $(ROOTLIB)/iconv

GENCODESETS=\
646da.8859.t 646de.8859.t 646en.8859.t \
646es.8859.t 646fr.8859.t 646it.8859.t \
646sv.8859.t 8859.646.t   8859.646da.t \
8859.646de.t 8859.646en.t 8859.646es.t \
8859.646fr.t 8859.646it.t 8859.646sv.t

CODESETS= $(GENCODESETS) iconv_data

ROOTCODESETS= $(CODESETS:%=$(ROOTDIRS)/%)

# conditional assignments
#
$(ROOTCODESETS) := FILEMODE = 444
$(NPROG) := CPPFLAGS += -DNATIVE
$(IPROG) := LDLIBS += -lintl

.SUFFIXES: .p .t

# build rule
#
.p.t:
	$(NPROG) -o $@ $<

# install rule
# 
$(ROOTDIRS)/%: %
	$(INS.file)

.KEEP_STATE:

.PARALLEL: $(IOBJS) $(KOBJS) $(NKOBJS)

all: $(PROG) $(IPROG) $(NPROG) $(CODESETS)

$(CODESETS): $(NPROG)

$(IPROG): $(IOBJS)
	$(LINK.c) $(IOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(PROG): $(KOBJS)
	$(LINK.c) $(KOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(NPROG): $(NATIVE) .WAIT $(NKOBJS)
	$(NATIVECC) $(CPPFLAGS) -o $@ $(NKOBJS)

$(NKOBJS): $(NATIVE) .WAIT $$(@F:.o=.c)
	$(NATIVECC) $(CPPFLAGS) -c -o $@ $(@F:.o=.c)

#
# Message catalog
#
$(POFILE):  $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@

gram.c: gram.y 
	yacc -vd gram.y
	mv y.tab.c gram.c

$(KOBJS) $(NKOBJS): gram.c

install: all .WAIT $(ROOTDIRS) $(ROOTPROG) $(ROOTIPROG) $(ROOTCODESETS)

$(ROOTDIRS):
	$(INS.dir)
 
$(NATIVE):
	-@mkdir -p $@
 
lint: lint_SRCS

clean:
	$(RM) $(IOBJS) $(KOBJS) $(NKOBJS) $(GENCODESETS) \
		gram.c y.tab.c y.tab.h y.output

include ../Makefile.targ
