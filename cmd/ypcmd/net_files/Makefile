#
#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved
#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.
#ident	"@(#)Makefile	1.5	92/07/14 SMI"        /* SMI4.1 1.32  */
#+++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#	PROPRIETARY NOTICE (Combined)
#
# This source code is unpublished proprietary information
# constituting, or derived under license from AT&T's UNIX(r) System V.
# In addition, portions of such source code were derived from Berkeley
# 4.3 BSD under license from the Regents of the University of
# California.
#
#
#
#	Copyright Notice 
#
# Notice of copyright on this source code product does not indicate 
#  publication.
#
#	(c) 1986,1987,1988,1989,1990  Sun Microsystems, Inc
#	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
#          All rights reserved.
# 
DIR =/etc
PWDIR =/var/yp/src
DOM = `domainname`
NOPUSH = ""
YPDIR=/usr/sbin
YPDBDIR=/var/yp
YPPUSH=$(YPDIR)/yppush
MAKEDBM=$(YPDIR)/makedbm
MKNETID=$(YPDIR)/mknetid
ALIASES = /etc/aliases
REVNETGROUP=$(YPDIR)/revnetgroup
STDETHERS=$(YPDIR)/stdethers
STDHOSTS=$(YPDIR)/stdhosts
MKKRBCONF=$(YPDIR)/mkkrbconf
MKALIAS=$(YPDIR)/mkalias
ALIASFILE=$(YPDBDIR)/aliases
ALIAS=$(YPDIR)/ypalias

.KEEP_STATE:

CHKPIPE=  || (  echo "NIS make terminated:" $@ 1>&2; kill -TERM 0; )

k:
	@if [ ! $(NOPUSH) ]; then $(MAKE)  $(MFLAGS) -k all; \
	else $(MAKE) $(MFLAGS) -k all NOPUSH=$(NOPUSH);fi

# The following is a list of ALL default maps. Some of them are commented
# out because the respective $DIR files do not yet exist. Eventually, all
# the $DIR files should be around.
#all: passwd group hosts ethers networks rpc services protocols \
#	bootparams netgroup aliases publickey netid netmasks kerberos

all: passwd group hosts ethers networks rpc services protocols \
	netgroup aliases publickey netid 

setup:

passwd.time: $(PWDIR)/passwd
	@if [ -f $(PWDIR)/passwd ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo passwd.byname `$(ALIAS) passwd.byname` >> $(ALIASFILE); \
		echo passwd.byuid `$(ALIAS) passwd.byuid` >> $(ALIASFILE); \
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE); \
		(awk 'BEGIN { FS = ":"; OFS = "\t"; } /^[a-zA-Z0-9_]/ { print $$1, $$0 }' $(PWDIR)/passwd $(CHKPIPE)) \
		| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) passwd.byname`; \
		(awk 'BEGIN { FS = ":"; OFS = "\t"; } /^[a-zA-Z0-9_]/ { printf("%-10d ", $$3); print $$0 }' $(PWDIR)/passwd $(CHKPIPE))\
		| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) passwd.byuid`; \
		touch passwd.time;  \
		echo "updated passwd"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) passwd.byname; fi;\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) passwd.byuid; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed passwd"; fi;\
	else \
		echo "couldn't find $(PWDIR)/passwd"; \
	fi

group.time: $(DIR)/group
	@if [ -f $(DIR)/group ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo group.byname `$(ALIAS) group.byname` >> $(ALIASFILE);\
		echo group.byuid `$(ALIAS) group.byuid` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		(awk 'BEGIN { FS=":"; OFS="\t"; } { print $$1, $$0 }' $(DIR)/group $(CHKPIPE))| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) group.byname` ; \
		(awk 'BEGIN { FS=":"; OFS="\t"; } { printf("%-10d ", $$3); print $$0 }' $(DIR)/group $(CHKPIPE)) \
		| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) group.bygid` ;\
		touch group.time;\
		echo "updated group";\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) group.byname; fi;\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) group.bygid; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed group"; fi;\
	else \
		echo "couldn't find $(DIR)/group"; \
	fi

hosts.time: $(DIR)/hosts
	@if [ -f $(DIR)/hosts ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo hosts.byname `$(ALIAS) hosts.byname` >> $(ALIASFILE);\
		echo hosts.byaddr `$(ALIAS) hosts.byaddr` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		(sed -e "/^#/d" -e "s/#.*$$//" < $(DIR)/hosts $(CHKPIPE)) | \
		($(STDHOSTS) $(CHKPIPE)) | \
		(awk '{for (i = 2; i <= NF; i++) print $$i, $$0}' $(CHKPIPE))| \
		$(MAKEDBM) $(B) -l - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) hosts.byname`;\
		($(STDHOSTS) $(DIR)/hosts $(CHKPIPE))| \
		(awk 'BEGIN { OFS="\t"; } $$1 !~ /^#/ { print $$1, $$0 }' $(CHKPIPE)) | \
		$(MAKEDBM) $(B) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) hosts.byaddr` ; \
		touch hosts.time; \
		echo "updated hosts"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) hosts.byname; fi;\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) hosts.byaddr; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed hosts"; fi;\
	else \
		echo "couldn't find $(DIR)/hosts"; \
	fi

ethers.time: $(DIR)/ethers
	@if [ -f $(DIR)/ethers ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo ethers.byname `$(ALIAS) ethers.byname` >> $(ALIASFILE);\
		echo ethers.byaddr `$(ALIAS) ethers.byaddr` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		($(STDETHERS) $(DIR)/ethers $(CHKPIPE)) \
		|(awk '{print $$1, $$0; for (i = 3;i <= NF;i++) print $$i,$$0}' $(CHKPIPE)) \
		| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) ethers.byaddr` ; \
		(awk 'BEGIN { OFS="\t"; } $$1 !~ /^#/ { print $$2, $$0 }' \
		   $(DIR)/ethers $(CHKPIPE)) | \
		(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) ethers.byname` ;\
		touch ethers.time; \
		echo "updated ethers"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) ethers.byname; fi;\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) ethers.byaddr; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed ethers"; fi;\
	else \
		echo "couldn't find $(DIR)/ethers"; \
	fi

networks.time: $(DIR)/networks
	@if [ -f $(DIR)/networks ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo networks.byname `$(ALIAS) networks.byname` >> $(ALIASFILE);\
		echo networks.byaddr `$(ALIAS) networks.byaddr` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		(sed -e "/^#/d" -e s/#.*$$// $(DIR)/networks $(CHKPIPE)) |( awk \
		    '{print $$1, $$0; for (i = 3;i <= NF;i++) print $$i,$$0}' \
		    $(CHKPIPE) )| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) networks.byname` ; \
		(awk 'BEGIN { OFS="\t"; } $$1 !~ /^#/ { print $$2, $$0 }' \
		   $(DIR)/networks $(CHKPIPE)) | $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) networks.byaddr` ;\
		touch networks.time; \
		echo "updated networks"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) networks.byname; fi;\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) networks.byaddr; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed networks"; fi;\
	else \
		echo "couldn't find $(DIR)/networks"; \
	fi

services.time: $(DIR)/services
	@if [ -f $(DIR)/services ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo services.byname `$(ALIAS) services.byname` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		(awk 'BEGIN { OFS="\t"; } $$1 !~ /^#/ { print $$2, $$0 }' \
		    $(DIR)/services $(CHKPIPE))| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) services.byname` ;\
		touch services.time; \
		echo "updated services"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) services.byname; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed services"; fi;\
	else \
		echo "couldn't find $(DIR)/services"; \
	fi

rpc.time: $(DIR)/rpc
	@if [ -f $(DIR)/rpc ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo rpc.bynumber `$(ALIAS) rpc.bynumber` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		(awk 'BEGIN { OFS="\t"; } $$1 !~ /^#/ { print $$2, $$0 }' \
		    $(DIR)/rpc $(CHKPIPE))| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) rpc.bynumber` ;\
		touch rpc.time; \
		echo "updated rpc"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) rpc.bynumber; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed rpc"; fi;\
	else \
		echo "couldn't find $(DIR)/rpc"; \
	fi

protocols.time: $(DIR)/protocols
	@if [ -f $(DIR)/protocols ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo protocols.byname `$(ALIAS) protocols.byname` >> $(ALIASFILE);\
		echo protocols.bynumber `$(ALIAS) protocols.bynumber` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		(awk 'BEGIN { OFS="\t"; } $$1 !~ /^#/ { print $$2, $$0 }' \
		    $(DIR)/protocols $(CHKPIPE)) | $(MAKEDBM) - \
		    $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) protocols.bynumber` ; \
		(sed -e "/^#/d" -e s/#.*$$// $(DIR)/protocols $(CHKPIPE)) |( awk \
		    '{print $$1,$$0; for (i = 3;i <= NF;i++) print $$i, $$0}' \
		    $(CHKPIPE))| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) protocols.byname` ; \
		touch protocols.time; \
		echo "updated protocols"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) protocols.byname; fi;\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) protocols.bynumber; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed protocols"; fi;\
	else \
		echo "couldn't find $(DIR)/rpc"; \
	fi

netgroup.time: $(DIR)/netgroup
	@if [ -f $(DIR)/netgroup ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo netgroup `$(ALIAS) netgroup` >> $(ALIASFILE);\
		echo netgroup.byuser `$(ALIAS) netgroup.byuser` >> $(ALIASFILE);\
		echo netgroup.byhost `$(ALIAS) netgroup.byhost` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		$(MAKEDBM) $(DIR)/netgroup $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) netgroup` ;\
		($(REVNETGROUP) < $(DIR)/netgroup -u $(CHKPIPE))| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) netgroup.byuser` ;\
		($(REVNETGROUP) < $(DIR)/netgroup -h $(CHKPIPE))| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) netgroup.byhost` ;\
		touch netgroup.time; \
		echo "updated netgroup"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) netgroup; fi;\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) netgroup.byuser; fi;\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) netgroup.byhost; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed netgroup"; fi;\
	else \
		echo "couldn't find $(DIR)/netgroup"; \
	fi

bootparams.time: $(DIR)/bootparams
	@if [ -f $(DIR)/bootparams ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo bootparams `$(ALIAS) bootparams` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		(sed -e '/^#/d' -e s/#.*$$// -e 's/[    ][  ]*$$//' \
			-e '/\\$$/s/\\$$/ /' $(DIR)/bootparams $(CHKPIPE))\
		|( awk '/ $$/ {printf "%s", $$0} !/ $$/ {print}' $(CHKPIPE))\
		|( sed -e 's/[   ][  ]*/ /g' $(CHKPIPE))\
		| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) bootparams` ;\
		touch bootparams.time;\
		echo "updated bootparams";\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) bootparams; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed bootparams"; fi;\
	else \
		echo "couldn't find $(DIR)/bootparams"; \
	fi

aliases.time: $(ALIASES)
	@if [ -f $(ALIASES) ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo mail.aliases `$(ALIAS) mail.aliases` >> $(ALIASFILE);\
		echo mail.byaddr `$(ALIAS) mail.byaddr` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		cp $(ALIASES) $(YPDBDIR)/$$ALDOMAIN/mail.aliases; \
		/usr/lib/sendmail -bi -oA$(YPDBDIR)/$$ALDOMAIN/mail.aliases; \
		$(MKALIAS) $(YPDBDIR)/$$ALDOMAIN/mail.aliases $(YPDBDIR)/$$ALDOMAIN/mail.byaddr; \
		rm $(YPDBDIR)/$$ALDOMAIN/mail.aliases; \
		touch aliases.time; \
		echo "updated aliases"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) mail.aliases; fi;\
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) mail.byaddr; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed aliases"; fi;\
	else \
		echo "couldn't find $(ALIASES)"; \
	fi

netmasks.time: $(DIR)/netmasks
	@if [ -f $(DIR)/netmasks ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo netmasks.byaddr `$(ALIAS) netmasks.byaddr` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		$(MAKEDBM) $(DIR)/netmasks $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) netmasks.byaddr` ; \
		touch netmasks.time; \
		echo "updated netmasks"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) netmasks.byaddr; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed netmasks"; fi;\
	else \
		echo "couldn't find $(DIR)/netmasks"; \
	fi

publickey.time: $(DIR)/publickey
	@if [ -f $(DIR)/publickey ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo publickey.byname `$(ALIAS) publickey.byname` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		(sed "/^#/d" < $(DIR)/publickey $(CHKPIPE))| $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) publickey.byname` ; \
		touch publickey.time; \
		echo "updated publickey"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) publickey.byname; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed publickey"; fi;\
	else \
		echo "couldn't find $(DIR)/publickey"; \
	fi

netid.time: $(DIR)/passwd $(DIR)/group $(DIR)/hosts $(DIR)/netid
	@if [ -f $(DIR)/netid ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		echo netid.byname `$(ALIAS) netid.byname` >> $(ALIASFILE);\
		sort $(ALIASFILE) | uniq > .ypaliases; mv .ypaliases $(ALIASFILE);\
		$(MKNETID) -q -p $(DIR)/passwd -g $(DIR)/group -h $(DIR)/hosts -m $(DIR)/netid > .ypjunk; \
		$(MAKEDBM) .ypjunk $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) netid.byname` ; \
		rm -f .ypjunk; \
		touch netid.time; \
		echo "updated netid"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) netid.byname; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed netid"; fi;\
	else \
		echo "couldn't find $(DIR)/netid"; \
	fi

kerberos.time: $(DIR)/krb.conf
	@if [ -f $(DIR)/krb.conf ]; then \
		DOMAIN=`domainname`;\
		ALDOMAIN=`ypalias -d $$DOMAIN`;\
		($(MKKRBCONF) $(DIR)/krb.conf $(CHKPIPE)) | \
		    $(MAKEDBM) - $(YPDBDIR)/$$ALDOMAIN/`$(ALIAS) krb.conf`;\
		touch kerberos.time; \
		echo "updated kerberos"; \
		if [ ! $(NOPUSH) ]; then $(YPPUSH) -d $(DOM) krb.conf; fi;\
		if [ ! $(NOPUSH) ]; then echo "pushed kerberos"; fi;\
	else \
		echo "couldn't find $(DIR)/krb.conf"; \
	fi

passwd: passwd.time
group: group.time
hosts: hosts.time
ethers: ethers.time
networks: networks.time
rpc: rpc.time
services: services.time
protocols: protocols.time
netgroup: netgroup.time
bootparams: bootparams.time
aliases: aliases.time
publickey: publickey.time
netid: netid.time
netmasks: netmasks.time
kerberos: kerberos.time
$(DIR)/netid:
$(DIR)/krb.conf:
