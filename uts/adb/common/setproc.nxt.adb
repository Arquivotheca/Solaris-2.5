#include <sys/proc.h>

pid
.>P
,#(<P)$<setproc.nop
{*pid_link,<P},#(#(<p-{*pid_id,<P}))$<setproc.nxt
#if defined(_BIG_ENDIAN)
*(*procdir+((*(<P)&0xffffff)*4))$<setproc.done
#elif defined(_LITTLE_ENDIAN)
*(*procdir+((*(<P)&0xffffff00)%40))$<setproc.done
#else
#error Byte ordering not defined!
#endif
