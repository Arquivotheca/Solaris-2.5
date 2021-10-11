#ident "@(#)devlink.tab.sh	1.51	94/12/12 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# This is the script that generates the devlink.tab file. It is
# architecture-aware, and dumps different stuff for x86 and sparc.
# There is a lot of common entries, which are dumped first.
#
# the SID of this script, and the SID of the dumped script are
# always the same.
#

cat <<EOM
#ident "@(#)devlink.tab	1.51	94/12/12 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#
# This is the table used by devlinks
#
# Each entry should have 2 fields; but may have 3.  Fields are separated
# by single tab ('\t') characters.
#
# The fields are:
#
# devfs-spec: a keyword-value set of devfs specifications, describing the set
#	of devfs node entries to be linked.
#
#	The keywords are:
#
#	type - The devinfo node type (see <sys/sunddi.h> for possible values)
#
#	name - the devinfo node name (the part of a /devices entry that appears
#		before the '@' or ':').
#
#	addr - the devinfo node address part (the portion of the name between
#		the '@' and the ':').
#
#	minor - the minor-attributes (the portion of a /devices name after the
#		':').
#
#	The keywords are separated from their valuse by an equals ('=') sign;
#	keyword-value pairs are separated from each other by semicolons (';').
#
# dev name - the /dev name corresponding to the devfs node described by
#	the devfs-spec field.  This specification is assume to start rooted at
#	/dev; THE INITIAL /dev/ SHOULD NOT BE SPECIFIED!
#	The name can contain a number of escape-sequences to include parts of
#	the devfs-name in the /dev/-name.  These escape-sequences all start with
#	a backslash ('\') character.  The current sequences are:
#
#	\D - the devfs 'name' field
#
#	\An - the 'n'th component of the address field (n=0 means the whole
#		address field)
#
#	\Mn - the 'n'th component of the minor field (n=0 means the entire
#		minor field).
#
#	\Nn - a sequential counter, starting at n (a *single* digit, giving
#		a starting range of 0 through 9).
#
# extra dev link - a few devices need a second link; that is, a second link
#	pointing to the first link.  This optional field specifies the /dev
#	format of this second link.  This entry can also use the above-described
#	escape-sequences.
#
# Fields can be blank; seperated by single tab characters,
# Spaces are significant, and are considered part of a field. IN GENERAL THIS
# MEANS THERE SHOULD BE NO SPACE CHARACTERS IN THIS FILE!
# All fields must be present (even if blank)
#
#
# devfs-spec	Dev-Namespec	Extra-Link
#
type=ddi_pseudo;name=winlock	\D
type=ddi_pseudo;name=mm	\M0
type=ddi_pseudo;name=conskbd	kbd
type=ddi_pseudo;name=consms	mouse
type=ddi_pseudo;name=wc	\M0
type=ddi_pseudo;name=dump	\M0
type=ddi_pseudo;name=cn	\M0
type=ddi_pseudo;name=lo	\M0
type=ddi_pseudo;name=ptm	\M0
type=ddi_pseudo;name=ptc	\M0
type=ddi_pseudo;name=pts	pts/\M0
type=ddi_pseudo;name=ptsl	\M0
type=ddi_pseudo;name=log	\M0
type=ddi_pseudo;name=sad	sad/\M0
type=ddi_pseudo;name=sy	\M0
type=ddi_pseudo;name=clone	\M0
type=ddi_network	\M0
type=ddi_pseudo;name=openeepr	\M0
type=ddi_pseudo;name=kstat	\M0
type=ddi_pseudo;name=ksyms	\M0
type=ddi_display	fbs/\M0	fb\N0
type=ddi_pseudo;name=clone;minor=icmp	rawip
type=ddi_pseudo;name=SUNW,bpp	\M0
type=ddi_pseudo;name=eeprom	\M0
type=ddi_pseudo;name=clone;minor=ipdcm	ipdcm
type=ddi_pseudo;name=vol	\M0
type=ddi_pseudo;name=profile	\M0
type=ddi_parallel;name=mcpp	mcpp\N0
type=ddi_pseudo;name=zsh	zsh\M0
type=ddi_pseudo;name=clone;minor=zsh	zsh
type=ddi_pseudo;name=SUNW,sx	\M0
type=ddi_pseudo;name=sx_cmem	\D
type=ddi_parallel;name=SUNW,spif;minor=stclp	printers/\N0
type=ddi_pseudo;name=SUNW,spif;minor=stc	sad/stc\N0
type=ddi_pseudo;name=tl;minor=ticots	ticots
type=ddi_pseudo;name=tl;minor=ticotsord	ticotsord
type=ddi_pseudo;name=tl;minor=ticlts	ticlts
type=ddi_pseudo;name=md;minor=admin	md/admin
type=ddi_pseudo;name=md;minor2=blk	md/dsk/d\M1
type=ddi_pseudo;name=md;minor2=raw	md/rdsk/d\M1
type=ddi_pseudo;name=tnf	\M0
EOM

#
# These are the x86 specific entries
# It depends on the build machine being an x86
#
if i386
then
cat <<EOM
type=ddi_audio;minor1=audio	audio
type=ddi_audio;minor1=audioctl	audioctl
type=ddi_pseudo;name=kdmouse	\D
type=ddi_pseudo;name=logi	\D
type=ddi_pseudo;name=lp;addr=3bc,0	lp0
type=ddi_pseudo;name=lp;addr=378,0	lp1
type=ddi_pseudo;name=lp;addr=278,0	lp2
type=ddi_pseudo;name=rootprop	\D
type=ddi_pseudo;name=tiqmouse	\D
type=ddi_serial:mb;name=asy;minor=a	tty00
type=ddi_serial:mb;name=asy;minor=b	tty01
type=ddi_serial:mb;name=asy;minor=c	tty02
type=ddi_serial:mb;name=asy;minor=d	tty03
type=ddi_serial:dialout,mb;name=asy;minor=a,cu	ttyd0
type=ddi_serial:dialout,mb;name=asy;minor=b,cu	ttyd1
type=ddi_serial:dialout,mb;name=asy;minor=c,cu	ttyd2
type=ddi_serial:dialout,mb;name=asy;minor=d,cu	ttyd3
type=ddi_serial:mb;name=asy	tty\M0
type=ddi_serial:dialout,mb;name=asy;minor=a,cu	cua0
type=ddi_serial:dialout,mb;name=asy;minor=b,cu	cua1
type=ddi_serial:dialout,mb;name=asy;minor=c,cu	cua2
type=ddi_serial:dialout,mb;name=asy;minor=d,cu	cua3
type=ddi_pseudo;name=msm	\D
type=ddi_audio;name=clone;minor=sbpro	\M0
type=ddi_audio;name=sbpro;minor=sbproctl	\M0
type=ddi_block:diskette;addr=0,0;minor=c	diskette
type=ddi_block:diskette;addr=0,0;minor=c,raw	rdiskette
type=ddi_block:diskette;addr1=0;minor=c	diskette\A2
type=ddi_block:diskette;addr1=0;minor=c,raw	rdiskette\A2
type=ddi_pseudo;name=chanmux;minor=chanmux	vt00
type=ddi_pseudo;name=chanmux;minor=chanmux	fb
EOM
fi

#
# These are the sparc specific entries
# It depends on the build machine being an sparc
#
if sparc
then
cat <<EOM
type=ddi_block:diskette;minor=c	diskette
type=ddi_block:diskette;minor=c,raw	rdiskette
type=ddi_block:diskette;minor=c	diskette0
type=ddi_block:diskette;minor=c,raw	rdiskette0
EOM
fi
