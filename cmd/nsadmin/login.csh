#ident	"@(#)login.csh	1.6	93/09/15 SMI"

# The initial machine wide defaults for csh.

if ( $?TERM == 0 ) then
	if { /bin/i386 } then
		setenv TERM AT386
	else
		setenv TERM sun
	endif
else
	if ( $TERM == "" ) then
		if { /bin/i386 } then
			setenv TERM AT386
		else
			setenv TERM sun
		endif
	endif
endif

if (! -e .hushlogin ) then
	/usr/sbin/quota
	/bin/cat -s /etc/motd
	/bin/mail -E
	switch ( $status )
	case 0: 
		echo "You have new mail."
		breaksw;
	case 2: 
		echo "You have mail."
		breaksw;
	endsw
endif
