#ifdef i386
#include <sys/reg.h>
#endif
#include <sys/frame.h>

frame
.>f
#ifdef sparc
<f/16Xn
<f/16pn
#endif
{*fr_savpc,<f}/inn
{*fr_savfp,<f},.$<calltrace.nxt
