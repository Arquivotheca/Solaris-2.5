#include <sys/types.h>
#include <sys/uio.h>

uio
.>f
./"iovcnt"16t"offset"16t"segflg"n{uio_iovcnt,D}{uio_offset,X}{uio_segflg,X}
+/"fmode"8t"limit"16t"resid"n{uio_fmode,x}{uio_limit,D}{uio_resid,D}
{*uio_iov,<f},{*uio_iovcnt,<f}$<iovec
