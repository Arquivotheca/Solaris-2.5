#
# ident	"@(#)Makefile	1.14	94/12/09 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# All rights reserved.
#
# cmd/lp/Makefile
#

include 	./Makefile.lp

#
# for messaging caralog, SUBDIRS2 is defined.
#
SUBDIRS1=               etc       filter crontab terminfo
SUBDIRS2=       lib cmd     model
SUBDIRS =       $(SUBDIRS1) $(SUBDIRS2)

ROOTDIRS =					\
		$(ROOTVAR)/lp			\
		$(ROOTVAR)/lp/logs		\
		$(ROOTLIBLP)			\
		$(ROOTLIBLP)/bin		\
		$(ROOTLIBLP)/model		\
		$(ROOTETCLP)			\
		$(ROOTETCLP)/classes		\
		$(ROOTETCLP)/forms		\
		$(ROOTETCLP)/interfaces	\
		$(ROOTETCLP)/printers		\
		$(ROOTETCLP)/pwheels		\
		$(ROOTVARSP)/lp			\
		$(ROOTVARSP)/lp/admins		\
		$(ROOTVARSP)/lp/requests 	\
		$(ROOTVARSP)/lp/system		\
		$(ROOTVARSP)/lp/fifos		\
		$(ROOTVARSP)/lp/fifos/private 	\
		$(ROOTVARSP)/lp/fifos/public

PRIVATEDIR =	$(ROOTVARSP)/lp/fifos/private
PUBLICDIR =	$(ROOTVARSP)/lp/fifos/public

SYMDIR1 =	$(ROOTVARSP)/lp/admins/lp
SYMDIR2 =	$(ROOTVARSP)/lp/bin
SYMDIR3 =	$(ROOTVARSP)/lp/logs
SYMDIR4 =	$(ROOTETCLP)/logs
SYMDIR5 =	$(ROOTVARSP)/lp/model

$(SYMDIR1) :=	SYMLNKDEST =	../../../../etc/lp
$(SYMDIR2) :=	SYMLNKDEST =	../../../usr/lib/lp/bin
$(SYMDIR3) :=	SYMLNKDEST =	../../lp/logs
$(SYMDIR4) :=	SYMLNKDEST =	../../var/lp/logs
$(SYMDIR5) :=	SYMLNKDEST =	../../../usr/lib/lp/model

ROOTSYMLINKDIRS =	$(SYMDIR1) $(SYMDIR2) $(SYMDIR3) $(SYMDIR4) $(SYMDIR5)

$(PRIVATEDIR) :=		DIRMODE = 0771
$(PUBLICDIR) :=			DIRMODE = 0773
$(ROOTVAR)/lp :=		DIRMODE = 775
$(ROOTVAR)/lp/logs :=		DIRMODE = 775
$(ROOTETCLP) :=			DIRMODE = 775
$(ROOTETCLP)/classes :=		DIRMODE = 775
$(ROOTETCLP)/forms :=		DIRMODE = 775
$(ROOTETCLP)/interfaces :=	DIRMODE = 775
$(ROOTETCLP)/printers :=	DIRMODE = 775
$(ROOTETCLP)/pwheels :=		DIRMODE = 775
$(ROOTVARSP)/lp :=		DIRMODE = 775
$(ROOTVARSP)/lp/admins :=	DIRMODE = 775
$(ROOTVARSP)/lp/requests :=	DIRMODE = 775
$(ROOTVARSP)/lp/system :=	DIRMODE = 775
$(ROOTVARSP)/lp/fifos :=	DIRMODE = 775

#
# for messaging catalog
#
_msg    :=      TARGET = catalog
POFILE= lp.po
POFILES= $(SUBDIRS2:%=%/%.po)

.KEEP_STATE:

all:	$(TXTS) $(SUBDIRS)

# for messaging catalog
_msg:   $(MSGDOMAIN) $(SUBDIRS2)
	$(RM)	$(POFILE)
	cat     $(POFILES)      > $(POFILE)
	$(RM)  $(MSGDOMAIN)/$(POFILE)
	cp $(POFILE)    $(MSGDOMAIN)

install: $(ROOTDIRS) $(ROOTSYMLINKDIRS) $(SUBDIRS)

clean strip lint: $(SUBDIRS)

clobber: $(SUBDIRS) local_clobber

local_clobber:
	$(RM) $(CLOBBERFILES)

$(ROOTDIRS) $(MSGDOMAIN):
	$(INS.dir)

$(ROOTSYMLINKDIRS) :
	-$(RM) $@; $(SYMLINK) $(SYMLNKDEST) $@

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
