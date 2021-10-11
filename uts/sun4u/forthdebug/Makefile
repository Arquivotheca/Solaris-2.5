#
# uts/sun4u/forthdebug/Makefile
# Copyright (c) 1995 by Sun Microsystems, Inc.
#
#ident   "@(#)Makefile 1.27     95/06/29 SMI"
#
#       This makefile drives the production of the pci driver kernel module
#
#       sun4u implementation architecture dependent
#

#
#       Path to the base of the uts directory tree (usually /usr/src/uts).
#
#
UTSBASE = ../..

#
#       Define the module and object file sets.
#
MODULE		= forthdebug
ROOTMODULE      = $(ROOT_PSM_MISC_DIR)/$(MODULE)

SUBDIRS		= forthgen fdbg

#
#       Include common rules.
#
include $(UTSBASE)/sun4u/Makefile.sun4u


# conditional assignments
all :=          TARGET= all
install :=      TARGET= all
clean :=        TARGET= clean
clean.lint :=	TARGET= clean.lint
clobber :=      TARGET= clobber
lint :=         TARGET= lint

.KEEP_STATE:

all clean clobber clean.lint lint: $(SUBDIRS)

modlintlib:

install: 	all $(ROOTMODULE)

$(ROOT_PSM_MISC_DIR)/%:		% $(ROOT_PSM_MISC_DIR)
	$(INS.file)

$(ROOT_PSM_MISC_DIR):	$(ROOT_PSM_MOD_DIR)
	-$(INS.dir.root.sys)

$(ROOT_PSM_MOD_DIR):	$(ROOT_PSM_DIR)
	-$(INS.dir.root.sys)

$(ROOT_PSM_DIR):	$(ROOT_PLAT_DIR)
	-$(INS.dir.root.sys)

$(ROOT_PLAT_DIR):
	-$(INS.dir.root.sys)

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:


