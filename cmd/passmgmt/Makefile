#
#ident	"@(#)Makefile	1.6	94/08/26 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/passmgmt/Makefile
#

PROG= 		passmgmt
FILE= 		datemsk
TXTS= 		$(FILE)

include ../Makefile.cmd

LDLIBS += 	-lgen -lintl
FILEMODE= 	0555
OWNER= 		root
GROUP= 		sys
RELSBIN=	../sbin
ROOTFILE=	$(ROOTETC)/$(FILE)
ROOTSYMLINK=	$(ROOTBIN)/$(PROG)

# conditional assignment
$(ROOTFILE):=	FILEMODE= $(LIBFILEMODE)

.KEEP_STATE:

all: $(PROG) $(TXTS)

install: all $(ROOTUSRSBINPROG) $(ROOTFILE) $(ROOTSYMLINK)

$(ROOTSYMLINK):
	-$(RM) $@; $(SYMLINK) $(RELSBIN)/$(PROG) $@

# datemsk is generated from datemsk.template because of a side-effect of
# SCCS.  Some of the datemsk format strings include "%<letter>%", which
# SCCS confuses for ID keywords.  datemsk.template should quote the "%"
# with "\" and code below will filter out the "\".  Only datemsk format
# strings next to each other need to be quoted.

datemsk: datemsk.template
	@while read i; do echo $$i; done < datemsk.template > $@

clean:
	$(RM) datemsk

lint:	lint_PROG

include ../Makefile.targ
