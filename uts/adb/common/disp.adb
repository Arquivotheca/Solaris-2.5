#include <sys/disp.h>

_disp
./"lock"8t"npri"16t"queue"16t"limit"16t"actmap"n{disp_lock,B}{disp_npri,d}{disp_q,X}{disp_q_limit,X}{disp_qactmap,X}
+/"maxrunpri"16t"max unb pri"16t"nrunnable"n{disp_maxrunpri,d}8t{disp_max_unbound_pri,d}8t{disp_nrunnable,D}{END}
