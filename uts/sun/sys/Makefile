#
# Copyright (c) 1992 by Sun Microsystems, Inc.
#
#pragma ident	"@(#)Makefile	1.76	95/05/23 SMI"
#
# uts/sun/sys/Makefile
#
# include global definitions
include ../../../Makefile.master

# NOTE: pm.h is not shipped.  It should be shipped when power management
# is a real feature of Solaris.

HDRS=  \
aflt.h \
audio_4231.h		audio_79C30.h		audiodebug.h \
audioio.h		audiovar.h \
avintr.h \
be.h			bmac.h \
bootconf.h		bpp_io.h		bpp_reg.h \
bpp_var.h \
bw2reg.h		bw2var.h \
cg2reg.h		cg2var.h \
cg3var.h		cg4reg.h		cg4var.h \
cg6fbc.h		cg6reg.h		cg6tec.h \
cg6thc.h		cg6var.h \
cg8-p4reg.h		cg8-p4var.h		cg8reg.h \
cg8var.h		cg9reg.h \
cg9var.h		cg14io.h		cg14reg.h \
cms.h			comvec.h \
cursor_impl.h \
dbriio.h		dbrireg.h		dbrivar.h \
dkbad.h			dkio.h \
dkmpio.h		dmaga.h			dumphdr.h \
eeprom_com.h \
fdreg.h			fdvar.h \
gp1cmds.h		gp1reg.h \
gp1var.h		gpio.h \
hdio.h			hme.h			hme_mac.h \
hme_phy.h		i82586.h \
idprom.h		idvar.h			ie.h \
ieeefp.h		ipi3.h \
ipi_chan.h		ipi_driver.h		ipi_error.h \
isdev.h			isdnio.h		isvar.h \
lance.h			le.h \
mace.h \
memfb.h			memreg.h		memvar.h \
mmcodecreg.h \
msreg.h			obpdefs.h		openprom.h \
p4reg.h			pixrect.h  \
pr_impl_util.h		pr_planegroups.h \
promif.h		promimpl.h		pw_dblbuf.h \
qe.h			qec.h \
ramdac.h 		ser_async.h		ser_zscc.h \
soc_cq_defs.h		socmap.h		socreg.h \
socvar.h 		stcconf.h \
stcio.h 		stcreg.h 		stcvar.h \
stp4020_reg.h		stp4020_var.h \
xdcreg.h		xderr.h			xdreg.h	\
xdvar.h			xtreg.h			xtvar.h \
xycom.h			xycreg.h		xyerr.h \
xyreg.h			xyvar.h			zsdev.h

DBHDRS=			debug.h			debugger.h

SCSIADHDRS= \
espcmd.h	espreg.h	espvar.h	fascmd.h	fasdma.h	\
fasreg.h	fasvar.h	ispcmd.h	ispmail.h	ispreg.h	\
ispvar.h	ncrctl.h	ncrreg.h	ncrsbc.h	\
plndef.h	plnvar.h	reset_notify.h

SCSITARGHDRS= \
sddef.h		stdef.h		pln_ctlr.h	ssddef.h \
sdwatch.h

FCHDRS= \
fc_transport.h	linkapp.h	fc.h		fcp.h

ROOTDIR=	$(ROOT)/usr/include/sys
ROOTDIRS=	$(ROOTDIR) \
		$(ROOTDIR)/debug \
		$(ROOTDIR)/scsi/adapters \
		$(ROOTDIR)/scsi/targets \
		$(ROOTDIR)/fc4

ROOTHDRS= $(HDRS:%=$(ROOTDIR)/%)
ROOTDBHDRS= $(DBHDRS:%=$(ROOTDIR)/debug/%)
ROOTFCHDRS= $(FCHDRS:%=$(ROOTDIR)/fc4/%)
ROOTSCSIADHDRS= $(SCSIADHDRS:%=$(ROOTDIR)/scsi/adapters/%)
ROOTSCSITARGHDRS= $(SCSITARGHDRS:%=$(ROOTDIR)/scsi/targets/%)

# install rules
$(ROOTDIR)/%: %
	$(INS.file)

$(ROOTDIR)/debug/%: debug/%
	$(INS.file)

$(ROOTDIR)/scsi/adapters/%: scsi/adapters/%
	$(INS.file)

$(ROOTDIR)/scsi/targets/%: scsi/targets/%
	$(INS.file)

# check files really don't exist

debug/%.check:		debug/%.h
	$(DOT_H_CHECK)

scsi/adapters/%.check:	scsi/adapters/%.h
	$(DOT_H_CHECK)

scsi/targets/%.check:	scsi/targets/%.h
	$(DOT_H_CHECK)

CHECKHDRS= \
	$(HDRS:%.h=%.check) \
	$(DBHDRS:%.h=debug/%.check) \
	$(FCHDRS:%.h=fc4/%.check) \
	$(SCSIADHDRS:%.h=scsi/adapters/%.check) \
	$(SCSITARGHDRS:%.h=scsi/targets/%.check)

# headers which won't quite meet the standards...
#
# devops.h has a macro where the formal parameters to the macro are greater
# than 80 characters.  cpp (or the equivalent built into acomp) does not allow
# continuation line breaks in the formal parameter list.  This could be fixed
# by giving shorter names to the formal parameters, but the right fix is to
# fix cpp.  (Also, /* CSTYLED */ doesn't seem to fix this.
#
devops.check    := CSTYLE_TAIL = | grep -v "line > 80 characters" | true

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS) $(ROOTHDRS) $(ROOTDBHDRS) $(ROOTSCSIADHDRS) \
	   $(ROOTSCSITARGHDRS) $(ROOTXHDRS) $(ROOTFCHDRS)

install_h: $(ROOTDIRS) .WAIT $(ROOTHDRS) $(ROOTDBHDRS) $(ROOTSCSIADHDRS) \
	   $(ROOTSCSITARGHDRS) $(ROOTFCHDRS)

$(ROOTDIRS):
	$(INS.dir)

check:	$(CHECKHDRS)
