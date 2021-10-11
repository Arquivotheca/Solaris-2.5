#ident	"@(#)ddnm.m4	1.8	93/06/30 SMI"	/* SunOS 4.1	*/
#
#
#		Copyright Notice 
#
#Notice of copyright on this source code product does not indicate 
#publication.
#
#	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
#	          All rights reserved.

############################################################
#
#		DDN Mailer specification
#
#	Send mail on the Defense Data Network
#	   (such as Arpanet or Milnet)

Mddn,	P=[TCP], F=msDFMuCX, S=22, R=22, A=TCP $h, E=\r\n

# map containing the inverse of mail.aliases
# Note that there is a special case mail.byaddr will cause reverse
# lookups in both Nis+ and NIS.
# If you want to use ONLY Nis+ for alias inversion comment out the next line
# and uncomment the line after that
DZmail.byaddr
#DZREVERSE.mail_aliases.org_dir

S22
R$*<@LOCAL>$*		$:$1
R$-<@$->		$:$>3${Z$1@$2$}			invert aliases
R$*<@$+.$*>$*		$@$1<@$2.$3>$4			already ok
R$+<@$+>$*		$@$1<@$2.$m>$3			tack on our domain
R$+			$@$1<@$w.$m>			tack on our full name 
