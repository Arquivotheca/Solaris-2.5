#
# uts/sparc/audio/Makefile
# Copyright (c) 1991-1994, by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.8	94/04/20 SMI"
#
#	This makefile drives the production of the audio driver kernel
#	module.
#
#	sparc architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= audio
OBJECTS		= $(AUDIO_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(AUDIO_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(USR_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sparc/Makefile.sparc

#
#	Overrides
#
$(OBJS_DIR)/audio_79C30_intr.o := AS_CPPFLAGS += -I$(UTSBASE)/sparc/audio
CLOBBERFILES	+= audio_genassym
CLEANFILES	+= audio_assym.h

CLOBBERFILES	+= $(MODULE)
CFLAGS		+= -v

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(MODULE)

lint:		$(MODULE).lint

install:  	$(MODULE) $(ROOTMODULE)

$(MODULE):	audio_assym.h

#
#	Include common targets.
#
include $(UTSBASE)/sparc/Makefile.targ

#
#	Assym targets
#
audio_genassym:	$(AUDIOGENASSYM_SRC)
	$(NATIVECC) $(ALWAYS_DEFS) $(NATIVE_INC_PATH) -o audio_genassym \
	$(AUDIOGENASSYM_SRC)

audio_assym.h:	audio_genassym
	./audio_genassym > ./audio_assym.h
