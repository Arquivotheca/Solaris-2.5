#include <sys/param.h>
#include <sys/types.h>
#include <sys/ts.h>

tsproc
./"timeleft"16t"dispwait"16t"cpupri"8t"uprilim"8t"upri"n{ts_timeleft,D}{ts_dispwait,D}{ts_cpupri,d}{ts_uprilim,d}{ts_upri,d}
+/"umdpri"8t"nice"8t"boost"8t"flags"8t"tp"n{ts_umdpri,d}{ts_nice,v}{ts_boost,v}{ts_flags,B}{ts_tp,X}
+/"next"16t"prev"n{ts_next,X}{ts_prev,X}{END}
