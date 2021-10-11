#
# uts/sun4u/forthdebug/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.3	95/08/28 SMI"
#
#       Path to the base of the uts directory tree (usually /usr/src/uts).
#

UTSBASE = ../../..

MODULE	= $(UTSBASE)/sun4u/forthdebug/forthdebug
FORTHGEN = $(UTSBASE)/sun4u/forthdebug/forthgen/forthgen

#
#       Include common rules.
#
include $(UTSBASE)/sun4u/Makefile.sun4u

# Noted: fas.fth is taken out

SUN4U_FTH_FILES = \
	common.fth	scsi.fth	sfmmu.fth 	fas.fth \
	esp.fth		isp.fth		sd.fth		sparc.fth \
	st.fth 		sun4u.fth 

KDBG		= ./kdbg.fth

.SUFFIXES: .o .c .fdbg .fth


#
#       Default build targets.
#
.KEEP_STATE:

all:		$(MODULE)

install:	$(MODULE)

lint:

clean:
	$(RM) $(SUN4U_FTH_FILES) 

clobber:	clean
	$(RM) $(MODULE)

clean.lint:

$(MODULE):	$(KDBG) $(SUN4U_FTH_FILES)
	cat $(KDBG) $(SUN4U_FTH_FILES) > fth.tmp 
	@wc -c fth.tmp | \
		awk '{size = $$1 + 32; printf("\ %d\n", size)}' > $(MODULE)
	cat fth.tmp >> $(MODULE)
	@rm fth.tmp

.fdbg.fth:	$(FORTHGEN)
	@grep "^#" $< > ${*F}.c
	$(CC) $(CPPFLAGS) -g -S -o ${*F}.s ${*F}.c
	@grep -v "^#" $< > ${*F}.tmp 
	$(FORTHGEN) ${*F}.tmp < ${*F}.s > ${*F}.fth 
	@rm ${*F}.s ${*F}.c ${*F}.tmp
