#include <sys/types.h>
#if defined(sun4) || defined(sun4c) || defined(sun4e)
#include <vm/hat_sunm.h>
#endif

pmgrp
#if defined(sun4) || defined(sun4c) || defined(sun4e)
.>P
(((<P-*hments)%0x300)*{SIZEOF})+*pmgrps$<pmgrp
#endif
