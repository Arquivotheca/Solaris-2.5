#include <sys/param.h>
#include <sys/types.h>
#if defined(sun4) || defined(sun4c) || defined(sun4e)
#include <vm/hat_sunm.h>
#endif

pmgrp
#if defined(sun4) || defined(sun4c) || defined(sun4e)
./"num"8t"keepcnt"8t"as"16t"base"16t"next"nx{OFFSETOK}{pmg_keepcnt,u}{pmg_as,X}{pmg_base,X}{pmg_next,X}
+/"prev"16t"hme"16t"pte[0]"16t"sme"n{pmg_prev,X}{pmg_hme[0],4X}{pmg_pte[0],X}{pmg_sme,X}{END}
#endif
