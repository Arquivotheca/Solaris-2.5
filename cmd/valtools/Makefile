#
#ident	"@(#)Makefile	1.6	92/12/15 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/valtools/Makefile
#

PROG= ckint ckitem ckpath ckrange ckstr ckyorn \
      ckkeywd ckdate cktime ckuid ckgid


include ../Makefile.cmd

LDLIBS += -ladm -lpkg -lintl

VCKS=	valint helpint errint  \
	helpitem erritem  \
        valpath helppath errpath  \
	valrange helprange errange  \
	valstr helpstr errstr  \
	valyorn helpyorn erryorn  \
	valtime helptime errtime  \
	valdate helpdate errdate  \
	dispuid valuid helpuid erruid  \
	dispgid valgid helpgid errgid

DISPS=	dispuid dispgid

ROOTSADMDIR=	$(ROOT)/usr/sadm/bin

ROOTPUTTEXT=	$(ROOTSADMDIR)/puttext
ROOTVCKS=	$(VCKS:%=$(ROOTSADMDIR)/%)
ROOTDISPS=	$(DISPS:%=$(ROOTBIN)/%)


# valtool install rules
$(ROOTSADMDIR)/%: %
	$(INS.file)

# there is a special case here for errange
#
$(ROOTSADMDIR)/disp% \
$(ROOTSADMDIR)/val% \
$(ROOTSADMDIR)/help% \
$(ROOTSADMDIR)/err% \
$(ROOTSADMDIR)/er% \
$(ROOTBIN)/disp%: $(ROOTBIN)/ck%
	$(RM) $@; $(LN) $< $@

 
.KEEP_STATE:
 
all: $(PROG) puttext 
 
install: all $(ROOTSADMDIR) $(ROOTPROG) $(ROOTPUTTEXT) $(ROOTVCKS) $(ROOTDISPS)
 
$(ROOTSADMDIR):
	$(INS.dir)

clean:

lint:   lint_SRCS
 
include ../Makefile.targ
