#include <sys/param.h>
#include <sys/types.h>
#include <sys/proc.h>

#ifdef this_is_a_comment_to_adbgen
#/*
# * The construct {p_user} emits the offset of the user structure within
# * the proc structure (in decimal) followed by a + sign.  
# * The 0 is appended so that the whole thing generates 
# * something like '.+0t432+0$<u'
# */
#endif

proc
.+0t{p_user}0$<u
