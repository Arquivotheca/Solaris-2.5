#include <sys/types.h>
#include <sys/buf.h>

buf
{*b_flags,<S}>F
{*b_edev,<S}>D
{*b_un.b_addr,<S}>C
{*b_forw,<S}>S
,#(#(<F&8))$<<cglistchk.nxt
,#(#(<B-<S))$<cglistiter.nxt
$<cglist.nxt
