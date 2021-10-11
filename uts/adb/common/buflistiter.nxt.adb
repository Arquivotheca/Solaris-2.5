#include <sys/types.h>
#include <sys/buf.h>

buf
<S/{b_edev,X}{b_blkno,X}{b_un.b_addr,X}{b_flags,X}
{*b_forw,<S}>S
<C+1>C
,#((<C#10)-<C)="edev"16t"blkno"16t"addr"16t"flags"n
,#(#(<B-<S))$<buflistiter.nxt
$<buflist.nxt
