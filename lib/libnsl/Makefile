#
#ident	"@(#)Makefile	1.51	95/08/07 SMI"
#
# Copyright (c) 1995, by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/libnsl/Makefile
#
LIBRARY= libnsl.a
VERS = .1
PROTOCOL_DIR= $(ROOT)/usr/include/rpcsvc

# objects are listed by source directory

# common utility code used in more than one directory
COMMON=		common.o

DES=		des_crypt.o des_soft.o

DIAL= dial.o

NETDIR= netdir.o

NSS= \
gethostbyname_r.o gethostent.o gethostent_r.o gethostby_door.o \
getrpcent.o  getrpcent_r.o netdir_inet.o netdir_inet_sundry.o

NETSELECT= netselect.o

NSL=  \
_conn_util.o    _data.o		_data2.o        _errlst.o \
_utility.o      t_accept.o	t_alloc.o       t_bind.o        t_close.o \
t_connect.o     t_error.o	t_free.o        t_getinfo.o     t_getname.o \
t_getstate.o    t_listen.o	t_look.o        t_open.o        t_optmgmt.o \
t_rcv.o         t_rcvconnect.o	t_rcvdis.o      t_rcvrel.o      t_rcvudata.o \
t_rcvuderr.o    t_snd.o		t_snddis.o      t_sndrel.o      t_sndudata.o \
t_sync.o        t_unbind.o	t_strerror.o

RPC=  \
auth_des.o      auth_none.o     auth_sys.o      authdes_prot.o  authsys_prot.o \
clnt_bcast.o    clnt_dg.o       clnt_generic.o  clnt_perror.o  \
clnt_raw.o      clnt_simple.o   clnt_vc.o       getdname.o \
gethostname.o   auth_time.o \
inet_ntoa.o     key_call.o  \
key_prot.o      netname.o       netnamer.o      openchild.o     pmap_clnt.o  \
pmap_prot.o     rpc_callmsg.o   rpc_comdata.o   rpc_generic.o \
rpc_prot.o      rpc_soc.o       rpcb_clnt.o     rpcb_prot.o     rpcdname.o  \
rtime_tli.o     svc.o           svc_auth.o      svc_auth_sys.o  svc_dg.o  \
svc_generic.o   svc_raw.o       svc_run.o       svc_simple.o    svc_vc.o  \
svcauth_des.o   xdr.o           xdr_array.o     xdr_float.o  \
xdr_mem.o       xdr_rec.o       xdr_refer.o     xdr_sizeof.o    xdr_stdio.o \
ti_opts.o       svid_funcs.o    rpcb_st_xdr.o   rpc_sel2poll.o  rpc_td.o \
rpc_trace.o     mt_misc.o

SAF= checkver.o  doconfig.o

YP=  \
dbm.o           yp_all.o        yp_b_clnt.o     yp_b_xdr.o      yp_bind.o  \
yp_enum.o       yp_master.o     yp_match.o      yp_order.o      yp_update.o \
yperr_string.o  yp_xdr.o        ypprot_err.o    ypupd.o 	\
yp_rsvd.o \
yppasswd_xdr.o

NIS_GEN=  \
nislib.o          nis_callback.o   nis_xdr.o      nis_subr.o     nis_names.o  \
nis_cback_xdr.o   print_obj.o      nis_perror.o   nis_groups.o   nis_tags.o   \
nis_misc.o        nis_lookup.o     nis_rpc.o      nis_clnt.o	 nis_cast.o   \
nis_hash.o	  thr_misc.o       nis_misc_proc.o npd_lib.o

NIS_CACHE_C= cache_clnt.o cache_getclnt.o md5.o

NIS_CACHE_CC=  \
client_cache.o            client_search.o  \
util.o           local_cache.o     client_cache_interface.o  dircache.o       \
dircache_lock.o  cold_start.o      cache_entry.o             externs.o

NIS_CACHE= $(NIS_CACHE_C) $(NIS_CACHE_CC)

NIS= $(NIS_GEN) $(NIS_CACHE)

RPC_DERIVED_FILES= \
	rpc/key_prot.c

GEN_DERIVED_FILES= \
	nis/gen/nis_clnt.h      \
	nis/gen/nis_clnt.c	\
	nis/gen/nis_xdr.c       \
	nis/gen/nis_cback_xdr.c

CACHE_DERIVED_FILES= \
	nis/cache/cache_clnt.c

DERIVED_FILES= $(GEN_DERIVED_FILES) $(CACHE_DERIVED_FILES) $(RPC_DERIVED_FILES)

#
# Make sure they get cleaned when necessary
#
CLEANFILES += $(DERIVED_FILES)

KEY= publickey.o xcrypt.o gen_dhkeys.o

# magic for including nss/mtlib.h for mutex_lock stubs
%/netdir_inet_sundry.o := CPPFLAGS += -I./nss


OBJECTS= $(COMMON) $(DES) $(DIAL) $(NETDIR) $(NSS) $(NETSELECT) $(NSL) $(RPC) \
	 $(SAF) $(YP) $(NIS) $(KEY)

# libnsl build rules
objs/%.o profs/%.o pics/%.o: common/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: des/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: dial/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: netdir/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: nss/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: netselect/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: nsl/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: rpc/%.c
	$(COMPILE.c) -DPORTMAP -DNIS  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: saf/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: yp/%.c
	$(COMPILE.c)   -o $@ $<
	$(POST_PROCESS_O)

objs/%.o profs/%.o pics/%.o: key/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o profs/%.o: nis/gen/%.c nis/gen/nis_clnt.h
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o profs/%.o: nis/cache/%.c nis/gen/nis_clnt.h
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o profs/%.o: nis/cache/%.cc nis/gen/nis_clnt.h
	$(COMPILE.cc) -o $@ $<
	$(POST_PROCESS_O)

# include library definitions
include ../Makefile.lib

MAPFILE_REORDER = Mapfile.reorder.libnsl

CPPFLAGS += 	-D_REENTRANT
$(PICS) := 	CFLAGS += -xF
$(PICS) := 	CCFLAGS += -xF

LIBS += $(DYNLIB)

DYNFLAGS += -M $(MAPFILE_REORDER)

LDLIBS += -ldl -lc -lintl -lmp

TXTS +=	dial/dk.h dial/uucp.h dial/parms.h dial/sysfiles.h

RPCGEN += -C

SED=	sed
CP=	cp
GREP=	grep

TEXT_DOMAIN= SUNW_OST_NETRPC
POFILE= $(LIBRARY:.a=.po)
POFILES= generic.po _errlst.po

.KEEP_STATE:

all: $(MAPFILE_REORDER) $(DERIVED_FILES) txts $(LIBS)

txts: $(TXTS)

install: all $(ROOTLIBS) $(ROOTLINKS)

#
# Rules for building the derived files
#
# Derived header files
#
nis/gen/nis_clnt.h: $(PROTOCOL_DIR)/nis.x
	$(RPCGEN) -C -h $(PROTOCOL_DIR)/nis.x |\
	$(SED) -n -e '/EDIT_START/,$$ p' |\
	$(SED) -e 's/_3_svc/_svc/' |\
	$(SED) -e 's/_3/_clnt/' > $@
#
# Derived source files
#
nis/gen/nis_clnt.c: $(PROTOCOL_DIR)/nis.x
	$(RPCGEN) -l $(PROTOCOL_DIR)/nis.x |\
	$(SED) -e 's/_3/_clnt/' |\
	$(SED) -e 's!\"$(PROTOCOL_DIR)/nis.h\"!\<rpcsvc/nis.h\>!' > $@

nis/gen/nis_xdr.c: $(PROTOCOL_DIR)/nis.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/nis.x |\
	$(SED) -e 's!\"$(PROTOCOL_DIR)/nis.h\"!\<rpcsvc/nis.h\>!' > $@

nis/gen/nis_cback_xdr.c: $(PROTOCOL_DIR)/nis_callback.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/nis_callback.x |\
	$(SED) -e 's!\"$(PROTOCOL_DIR)/nis_callback.h\"!\<rpcsvc/nis_callback.h\>!' > $@

nis/cache/cache_clnt.c: $(PROTOCOL_DIR)/nis_cache.x
	$(RPCGEN) -l $(PROTOCOL_DIR)/nis_cache.x |\
        $(SED) -e 's!\"$(PROTOCOL_DIR)/nis_cache.h\"!<rpcsvc/nis_cache.h>!' > $@

rpc/key_prot.c: $(PROTOCOL_DIR)/key_prot.x
	$(RPCGEN) -c $(PROTOCOL_DIR)/key_prot.x |\
        $(SED) -e 's!"$(PROTOCOL_DIR)/key_prot.h"!<rpc/key_prot.h>!' > $@

SRCS= $(DES:%.o=des/%.c) $(DIAL:%.o=dial/%.c) $(NETDIR:%.o=netdir/%.c)\
	$(NSS:%.o=nss/%.c) $(NETSELECT:%.o=netselect/%.c) $(NSL:%.o=nsl/%.c)\
	$(RPC:%.o=rpc/%.c) $(SAF:%.o=saf/%.c) $(YP:%.o=yp/%.c)\
	$(NIS_GEN:%.o=nis/gen/%.c) $(NIS_CACHE_C:%.o=nis/cache/%.c)\
	$(COMMON:%.o=common/%.c)

# include library targets
include ../Makefile.targ


lint: $(SRCS:.c=.ln) $(LINTLIB)

_msg: $(MSGDOMAIN) $(POFILE)
	$(RM) $(MSGDOMAIN)/$(POFILE)
	$(CP) $(POFILE) $(MSGDOMAIN)

$(POFILE): $(DERIVED_FILES) .WAIT $(POFILES)
	$(RM) $@
	$(CAT) $(POFILES) > $@

_errlst.po:
	$(RM) messages.po
	$(XGETTEXT) -a nsl/_errlst.c
	$(SED) -e '/^# msg/d' -e '/^domain/d' messages.po > $@
	$(RM) messages.po

generic.po:
	$(RM) messages.po
	$(XGETTEXT) $(XGETFLAGS) `$(GREP) -l gettext */*.[ch] nis/*/*.[ch]*`
	$(SED) "/^domain/d" messages.po > $@
	$(RM) messages.po

$(MSGDOMAIN):
	$(INS.dir)
