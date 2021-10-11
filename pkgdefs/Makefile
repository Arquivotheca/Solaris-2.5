#
#ident	"@(#)Makefile	1.36	95/04/26 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# pkgdefs/Makefile

include $(SRC)/Makefile.master

sparc_SUBDIRS= \
	SUNWbcp  \
	SUNWcar.c  \
	SUNWcar.d  \
	SUNWcar.m  \
	SUNWcar.u  \
	SUNWcg6.c  \
	SUNWcg6.d  \
	SUNWcg6.m  \
	SUNWcg6.u  \
	SUNWcg6h  \
	SUNWdfb.c  \
	SUNWdfb.d  \
	SUNWdfb.m  \
	SUNWdfb.u  \
	SUNWkvm.c  \
	SUNWkvm.d  \
	SUNWkvm.m  \
	SUNWkvm.u  \
	SUNWssadv  \
	SUNWssaop  \
	SUNWsxr.m  \
	SUNWhmd    \
	SUNWhmdu

i386_SUBDIRS= \
	SUNWcar.i  \
	SUNWcoff  \
	SUNWdfb.i  \
	SUNWkvm.i  \
	SUNWs53 \
	SUNWpsdcr \
	SUNWpsder \
	SUNWpsdir \
	SUNWpsdmr \
	SUNWpsh

COMMON_SUBDIRS= \
	SUNWaccr  \
	SUNWaccu  \
	SUNWapppr  \
	SUNWapppu  \
	SUNWarc  \
	SUNWast  \
	SUNWbnur  \
	SUNWbnuu  \
	SUNWbtool  \
	SUNWcsd  \
	SUNWcsr  \
	SUNWcsu  \
	SUNWdfbh  \
	SUNWdoc  \
	SUNWesu  \
	SUNWfac  \
	SUNWfns  \
	SUNWfnspr  \
	SUNWfnsx5  \
	SUNWhea  \
	SUNWipc  \
	SUNWkey  \
	SUNWloc  \
	SUNWlpr  \
	SUNWlps  \
	SUNWlpu  \
	SUNWnisr  \
	SUNWnisu  \
	SUNWpcmci  \
	SUNWpcmcu  \
	SUNWpppk  \
	SUNWscpr  \
	SUNWscpu  \
	SUNWsra  \
	SUNWsrh  \
	SUNWsutl  \
	SUNWter  \
	SUNWtnfc  \
	SUNWtnfd  \
	SUNWtoo  \
	SUNWvolr  \
	SUNWvolu  \
	SUNWxcu4  \
	SUNWxwdv

SUBDIRS= $($(MACH)_SUBDIRS) $(COMMON_SUBDIRS)

XMODS= \
	SUNWcry  \
	SUNWstatl  \
	SUNWsvvs

all :=         		TARGET= all
install := 		TARGET= install
install_h :=            TARGET= install_h
clean :=                TARGET= clean
clobber :=              TARGET= clobber

%: %.ksh
	$(RM) $@
	cp $< $@
	chmod +x $@

.KEEP_STATE:

.PARALLEL: $(SUBDIRS) $(XMODS)

all install: awk_pkginfo $(SUBDIRS) $(XMODS)

clean clobber: $(SUBDIRS) $(XMODS)
	$(RM) awk_pkginfo bld_awk_pkginfo

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) -i $(TARGET)

$(XMODS):       FRC
	@if [ -f $@/Makefile  ]; then \
		cd $@; pwd; $(MAKE) $(TARGET); \
	else \
		true; \
	fi

awk_pkginfo: bld_awk_pkginfo
	./bld_awk_pkginfo -m $(MACH) -p "$(RELEASE)/$(VERSION)" -o $@

FRC:
