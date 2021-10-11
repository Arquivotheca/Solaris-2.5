#
#ident	"@(#)Makefile	1.5	94/08/12 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/face/src/filecab/Makefile

# include global definitions
include ../../../../Makefile.master

SUBDIRS = fileb

SFILES=		.faceprofile
WFILES=		WASTEBASKET/.pref
PFILES=		pref/.environ pref/.variables pref/.colorpref
STDFILES=	$(SFILES) $(WFILES) $(PFILES)
EXTERNFILES=	allobjs detect.tab
VMFILES=	pathalias
TERRLOG=	TERRLOG

APPLFILES=	$(STDFILES:%=standard/%) \
		$(EXTERNFILES:%=oasys/info/OH/externals/%) \
		$(VMFILES) $(TERRLOG)

# include common face definitions and install rules
include ../Makefile.com

ROOTDIRS=	$(ROOTSTD)/WASTEBASKET $(ROOTSTD)/pref \
		$(ROOTOASYS)/tmp

ROOTTERRLOG=	$(ROOTOASYS)/tmp/$(TERRLOG)
ROOTSTDF=	$(STDFILES:%=$(ROOTSTD)/%)
ROOTEXTERNF=	$(EXTERNFILES:%=$(ROOTEXTERN)/%)
ROOTVMSYSF=	$(VMFILES:%=$(ROOTVMSYS)/%)

DIRMODE=	775
FILEMODE=	664

# conditional assignments
#
all :=          TARGET = all
install :=      TARGET = install
clean :=        TARGET = clean
clobber :=      TARGET = clobber
lint :=         TARGET = lint

$(ROOTTERRLOG) :=	FILEMODE=	622

# installation rule
$(ROOTOASYS)/tmp/% : %
	$(INS.file)

.KEEP_STATE:

all: 	$(APPLFILES) $(SUBDIRS)

$(TERRLOG):
	$(RM) $@; touch $@

install: all $(ROOTDIRS) \
	$(ROOTTERRLOG) $(ROOTSTDF) $(ROOTEXTERNF) $(ROOTVMSYSF) $(SUBDIRS)

$(ROOTTERRLOG): $(ROOTOASYS)

$(ROOTDIRS): $(ROOTSTD) $(ROOTOASYS) $(ROOTEXTERN) $(ROOTVMSYS)
	$(INS.dir)

clean clobber : $(SUBDIRS)
	$(RM) $(TERRLOG)

lint: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
