#
#ident   "@(#)Makefile 1.33     94/11/30 SMI"
#
# Copyright (c) 1989-1994, by Sun Microsystems, Inc.
#
# uts/sparc/sys/Makefile
#
# include global definitions
include ../../../Makefile.master

HDRS=  \
	archsystm.h	asm_linkage.h	condvar_impl.h	cpu.h		\
	ddi_isa.h	frame.h		fsr.h		inline.h	\
	machlock.h	machsig.h	machtypes.h	pcb.h		\
	procfs.h	psw.h		reg.h		regset.h	\
	rwlock_impl.h	sema_impl.h	spl.h		stack.h		\
	stat.h		trap.h		ucontext.h      vmparam.h

FPUHDRS=   fpu_simulator.h	fpusystm.h	globals.h	ieee.h

ROOTDIR=	$(ROOT)/usr/include/sys
ROOTDIRS=	$(ROOTDIR) $(ROOTDIR)/fpu

ROOTHDRS= $(HDRS:%=$(ROOTDIR)/%)
ROOTFPUHDRS= $(FPUHDRS:%=$(ROOTDIR)/fpu/%)

fpu/%.check:	fpu/%.h
	$(DOT_H_CHECK)

CHECKHDRS= $(HDRS:%.h=%.check) \
	$(FPUHDRS:%.h=fpu/%.check)

# install rules
$(ROOTDIR)/%: %
	$(INS.file)

$(ROOTDIR)/fpu/%: fpu/%
	$(INS.file)

.KEEP_STATE:

.PARALLEL: $(CHECKHDRS) $(ROOTHDRS) $(ROOTFPUHDRS)

install_h: $(ROOTDIRS) .WAIT $(ROOTHDRS) $(ROOTFPUHDRS)

$(ROOTDIRS):
	$(INS.dir)

check:	$(CHECKHDRS)
