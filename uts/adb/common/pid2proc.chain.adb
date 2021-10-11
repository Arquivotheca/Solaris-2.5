#include <sys/proc.h>

pid
.>P
,#(<P)$<
{*pid_link,<P},#(#(<p-{*pid_id,<P}))$<pid2proc.chain
#if defined(_BIG_ENDIAN)
*(*procdir+((*(<P)&0xffffff)*4))$<proc
#elif defined(_LITTLE_ENDIAN)
*(*procdir+((*(<P)&0xffffff00)%40))$<proc
#else
#error Byte ordering not defined!
#endif
