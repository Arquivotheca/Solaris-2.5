#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.5	94/08/25 SMI"

PROG= ucblinks
SHLIBEXFILES= ucblinks.sh
SHLIBFILES= ucblinks.awk

include ../Makefile.ucbcmd

ROOTLIBEXFILES= $(ROOTLIB)/$(SHLIBEXFILES)
ROOTLIBFILES= $(ROOTLIB)/$(SHLIBFILES)
OWNER = root
GROUP = sys
$(ROOTLIBEXFILES)	:= FILEMODE = 555
$(ROOTLIBFILES)		:= FILEMODE = 644
$(ROOTLIB)	:= OWNER = bin
$(ROOTLIB)	:= GROUP = bin
$(PROG)	:= OWNER = bin
$(PROG)	:= GROUP = bin

LDLIBS += -lintl

.KEEP_STATE:

all: $(PROG) $(SHLIBEXFILES) $(SHLIBFILES)

install: all $(ROOTPROG) $(ROOTLIB) $(ROOTLIBEXFILES) $(ROOTLIBFILES)

$(ROOTLIB):
	$(INS.dir)

clean:

lint:

include ../Makefile.ucbtarg
