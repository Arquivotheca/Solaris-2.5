#
#ident	"@(#)Makefile.com	1.3	94/10/11 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#

include $(SRC)/Makefile.master

PKGARCHIVE=./
DATAFILES=copyright prototype preinstall postremove
README=SUNWonld-README
FILES=$(DATAFILES) pkginfo
PACKAGE= 	SUNWonld
ROOTONLD=$(ROOT)/opt/SUNWonld
ROOTREADME=$(README:%=$(ROOTONLD)/%)

CLEANFILES= $(FILES) awk_pkginfo ../bld_awk_pkginfo
CLOBBERFILES= $(PACKAGE)

../%:	../common/%.ksh
	$(RM) $@
	cp $< $@
	chmod +x $@

$(ROOTONLD)/%: ../common/%
	$(RM) $@
	cp $< $@
	chmod +x $@

