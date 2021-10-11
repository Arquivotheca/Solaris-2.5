#include <sys/machparam.h>
#include <sys/types.h>
#include <sys/msgbuf.h>

msgbuf
msgbuf/"magic"16t"size"16t"bufx"16t"bufr"n{msg_magic,X}{msg_size,X}{msg_bufx,X}{msg_bufr,X}
+,(*(msgbuf+0t8)-*(msgbuf+0t12))&80000000$<msgbuf.wrap
.+*(msgbuf+0t12),(*(msgbuf+0t8)-*(msgbuf+0t12))/c
