#
#ident	"@(#)Makefile	1.11	95/01/09 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# lib/libcrypt/Makefile
#

OBJECTS.i= crypt.o cryptio.o 
OBJECTS= $(OBJECTS.i) des_crypt.o  des.o des_soft.o

LIB_DOMESTIC=	libcrypt_d.a
LIB_INTERNTL=	libcrypt_i.a
LIBCRYPT=	libcrypt.a
LIBRARY=	$(LIB_DOMESTIC)

# include library definitions
include ../Makefile.lib

CPPFLAGS = -D_REENTRANT -Dsparc $(RTLDINC) $(CPPFLAGS.master)

LIBS += $(LIB_INTERNTL)

# one object has an international version; it should not overwrite
# the domestic version
#
OBJ.i=	obj.i/des_crypt.o
OBJS.i= $(OBJECTS.i:%=objs/%) $(OBJ.i)

CLEANFILES= $(OBJ.i)

# conditional assignments
$(LIB_INTERNTL):= AROBJS = $(OBJS.i)
$(OBJ.i):= CPPFLAGS += -DINTERNATIONAL

ROOTLIBSYMLINK=	$(ROOTLIBDIR)/$(LIBCRYPT)

.KEEP_STATE:

all: $(LIBS)

$(LIB_INTERNTL): objs $(OBJS.i)
	$(BUILD.AR)
	$(POST_PROCESS_A)

install: $(ROOTLIBS) $(ROOTLIBSYMLINK)

$(ROOTLIBSYMLINK):
	$(RM) $@; $(SYMLINK) ./$(LIB_INTERNTL) $@

lint:
	$(LINT.c) $(SRCS)

$(OBJ.i): obj.i
	$(COMPILE.c) -o $@ $(@F:.o=.c)
	$(POST_PROCESS_O)

obj.i:
	-@mkdir -p $@

# include library targets
include ../Makefile.targ

ARCHITECTURES = sparc

CSDIR   = .
CSDIRS  = $(ARCHITECTURES) port inc
CSPATHS = $(CSDIRS:%=$(CSDIR)/%)
CSINCS  = $(CSPATHS:%=-I%)
CSCOPE  = cscope
CTAGS   = ctags

cscope.files:
	@-$(RM) cscope.files
	echo "$(CSDIR)" > cscope.files
	find $(CSDIR) -name SCCS -prune -o \
		-type d -name '.del-*' -prune -o -type f \
		\( -name '*.[csh]' -o -name 'Makefile*' -o -name '*.il' \) \
		  -print >> cscope.files
	@wc -l cscope.files

tags: tags.list
	${CTAGS} -wt `cat tags.list`

tags.list: cscope.files
	@$(RM) tags.list
	grep '\.c$$' cscope.files > tags.list
	grep '\.s$$' cscope.files >> tags.list
	grep '\.h$$' cscope.files >> tags.list
