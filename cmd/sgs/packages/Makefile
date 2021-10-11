#
#ident	"@(#)Makefile	1.18	95/08/17 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# cmd/sgs/packages/Makefile

include		../../../Makefile.master

PKG_DEPS= 	../../../lib/libelf \
		../../../lib/libdl \
		../liblddbg \
		.WAIT \
		../libld \
		.WAIT \
		../ld \
		../rtld \
		../rtld.4.x \
		../libldstab \
		../libldmake/$(MACH) \
		../ldd \
		../tools \
		../ar \
		../pvs \
		../gprof

../%:		FRC
		cd $@; pwd; $(MAKE) install

.KEEP_STATE:
.PARALLEL:	$(PKG_DEPS)

all clean clobber install lint: FRC
		@ cd $(MACH); pwd; $(MAKE) $@

# 
# invoke `package' to build this package from scratch.  It will 
# perform make install in the directories that contains components 
# that this package is dependent upon 
# 
package:	package_dep .WAIT install

package_dep:	package_dep_ .WAIT $(PKG_DEPS)

package_dep_:	FRC
		@cd ../tools; pwd; $(MAKE) proto && ./proto
		@cd $(SRC)/head; pwd; $(MAKE) \
			$(ROOT)/usr/include/dlfcn.h \
			$(ROOT)/usr/include/libelf.h \
			$(ROOT)/usr/include/link.h
		@cd $(SRC)/uts/common/sys; pwd; $(MAKE) \
			$(ROOT)/usr/include/sys/elf.h \
			$(ROOT)/usr/include/sys/elf_386.h \
			$(ROOT)/usr/include/sys/elf_M32.h \
			$(ROOT)/usr/include/sys/elf_SPARC.h \
			$(ROOT)/usr/include/sys/elf_notes.h \
			$(ROOT)/usr/include/sys/elftypes.h \
			$(ROOT)/usr/include/sys/auxv.h \
			$(ROOT)/usr/include/sys/link.h \
			$(ROOT)/usr/include/sys/systeminfo.h
		@cd ..; pwd; $(MAKE) native-add

FRC:
