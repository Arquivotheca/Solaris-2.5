#
#ident	"@(#)Makefile	1.5	94/08/12 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/face/src/filecab/fileb/Makefile

PROG=		chkperm creaserve delserve face listserv \
		mnucheck modserv termtest
SCRIPTS=	basename chexec chkterm color_chk dir_copy \
		dir_creat dir_move frame_chk ichexec \
		identify setenv
DATAFILE=	.facerc

include		../../../../Makefile.cmd

SRCS=		$(PROG:%=%.c)


CPPFLAGS = 	-I../inc $(CPPFLAGS.master)
CLOBBERFILES=	$(SCRIPTS)

# include common face definitions and install rules
include ../../Makefile.com

VMLIBF=		services
OAPROG=		termtest identify setenv
VMPROG=		face dir_move dir_copy dir_creat chkperm creaserve \
		listserv delserve ichexec chexec chkterm basename \
		mnucheck modserv color_chk frame_chk

ROOTDATAFILE=	$(DATAFILE:%=$(ROOTVMLIB)/%)
ROOTVMLIBF=	$(VMLIBF:%=$(ROOTVMLIB)/%)
ROOTVMPROG=	$(VMPROG:%=$(ROOTVMBIN)/%)
ROOTOAPROG=	$(OAPROG:%=$(ROOTOABIN)/%)

FILEMODE=	755

# conditional assignments
#
termtest := LDLIBS += -lgen

$(ROOTVMLIBF):=		FILEMODE=	644
$(ROOTDATAFILE):=	FILEMODE=	600
$(ROOTVMBIN)/chkperm:=	FILEMODE=	6755

.KEEP_STATE:

all: $(PROG) $(SCRIPTS) $(DATAFILE) $(VMLIBF) 

$(DATAFILE):
	$(RM) $@; touch $@

install: all $(ROOTVMLIBF) $(ROOTVMPROG) $(ROOTOAPROG) \
	$(ROOTDATAFILE)

$(ROOTDATAFILE) $(ROOTVMLIBF): $(ROOTVMLIB)

$(ROOTVMPROG): $(ROOTVMBIN)

$(ROOTOAPROG): $(ROOTOABIN)

clean:

lint:	lint_SRCS

include ../../../../Makefile.targ
