#include	<sys/types.h>
#include	<sys/proc.h>
#include	<sys/disp.h>

dispq
.>p
,#(<p+1)$<
*dispq+(<p*{SIZEOF})>q
<p-1>p
<p,#{*dq_first,<q}$<dispq.nxt
<q/n"first"16t"last"16t"sruncnt"n{dq_first,X}{dq_last,X}{dq_sruncnt,D}
<p$<dispq.nxt
