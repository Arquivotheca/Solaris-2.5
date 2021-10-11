#include <sys/types.h>
#include <sys/proc.h>
#include <sys/disp.h>

dispq
(*dispq)+((<q)*{SIZEOF})>D
{*dq_first,<D}>f
{*dq_last,<D}>l
,#(#(<f))$<<dispqtrace.list
<q+1>q
,#(<Q-<q)$<
$<dispqtrace.nxt
