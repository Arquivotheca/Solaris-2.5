###########################################################
#
#	SENDMAIL CONFIGURATION FILE FOR SUBSIDIARY MACHINES
#
#	You should install this file as /etc/sendmail.cf
#	if your machine is a subsidiary machine (that is, some
#	other machine in your domain is the main mail-relaying
#	machine).  Then edit the file to customize it for your
#	network configuration.
#
#	@(#)subsidiary.mc 1.11 88/02/08 SMI; from UCB arpa.mc 3.25 2/24/83
#

# delete the following if you have no sendmailvars table
Lmmaildomain

# local UUCP connections -- not forwarded to mailhost
CV

# my official hostname
Dj$w.$m

# major relay mailer
DMether

# major relay host
DRmailhost
CRmailhost

include(sunbase.m4)

include(uucpm.m4)

include(zerobase.m4)

################################################
###  Machine dependent part of ruleset zero  ###
################################################

# resolve names we can handle locally
R<@$=V.uucp>:$+		$:$>9 $1			First clean up, then...
R<@$=V.uucp>:$+		$#uucp  $@$1 $:$2		@host.uucp:...
R$+<@$=V.uucp>		$#uucp  $@$2 $:$1		user@host.uucp

# optimize names of known ethernet hosts
R$*<@$%l.LOCAL>$*	$#ether $@$2 $:$1<@$2>$3	user@host.here
# local host that has a MX record
R$*<@$%x.LOCAL>$*	$#ether $@$2 $:$1<@$2>$3	user@host.here

# other non-local names will be kicked upstairs
R$+			$:$>9 $1			Clean up, keep <>
R$*<@$+>$*		$#$M    $@$R $:$1<@$2>$3	user@some.where
R$*@$*			$#$M    $@$R $:$1<@$2>		strangeness with @

# Local names with % are really not local!
R$+%$+			$@$>30$1@$2			turn % => @, retry

# everything else is a local name
R$+			$#local $:$1			local names

# Ruleset 33 is used in remote mode only 
S33
R$+<@$=w.LOCAL>		$1				
R$+<@$=w>		$1
R$*<@$+>$*		$#ether $@$k $:$1<@$2>$3	forward to $k
R$+			$#local $:$1			local names

