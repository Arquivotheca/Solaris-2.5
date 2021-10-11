#ident	"@(#)etherm.m4	1.15	93/04/05 SMI"	/* SunOS 4.1	*/
#
#		Copyright Notice 
#
#Notice of copyright on this source code product does not indicate 
#publication.
#
#	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
#	          All rights reserved.

############################################################
#####
#####		Ethernet Mailer specification
#####
#####	Messages processed by this configuration are assumed to remain
#####	in the same domain.  This really has nothing particular to do
#####   with Ethernet - the name is historical.

Mether,	P=[TCP], F=msDFMuCX, S=11, R=21, A=TCP $h
S11
R$*<@$+>$*		$@$1<@$2>$3			already ok
R$=D			$@$1<@$w>			tack on my hostname
R$+			$@$1<@$k>			tack on my mbox hostname

S21
R$*<@$+>$*		$@$1<@$2>$3			already ok
R$+			$@$1<@$k>			tack on my mbox hostname

