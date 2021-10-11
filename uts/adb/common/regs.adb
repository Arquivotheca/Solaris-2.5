#include <sys/types.h>
#if defined(sparc)
#	include <sys/privregs.h>
#elif defined(i386)
#	include <sys/reg.h>
#endif

regs
#if defined(sparc)
#if defined(sun4m) || defined(sun4c) || defined(sun4d)
./"psr"16t"pc"16t"npc"n{r_ps,X}{r_pc,X}{r_npc,X}
+/"y"16t"g1"16t"g2"16t"g3"n{r_y,X}{r_g1,X}{r_g2,X}{r_g3,X}
+/"g4"16t"g5"16t"g6"16t"g7"n{r_g4,X}{r_g5,X}{r_g6,X}{r_g7,X}
+/"o0"16t"o1"16t"o2"16t"o3"n{r_o0,X}{r_o1,X}{r_o2,X}{r_o3,X}
+/"o4"16t"o5"16t"o6"16t"o7"n{r_o4,X}{r_o5,X}{r_o6,X}{r_o7,X}
#endif /* sun4m/sun4c/sun4d */
#if defined(sun4u)
./"tstate"16t"pc"16t"npc"n{r_tstate,2X}{r_pc,X}{r_npc,X}
+/"y"16t"g1"16t"g2"16t"g3"n{r_y,X}{r_g1,2X}{r_g2,2X}{r_g3,2X}
+/"g4"16t"g5"16t"g6"16t"g7"n{r_g4,2X}{r_g5,2X}{r_g6,2X}{r_g7,2X}
+/"o0"16t"o1"16t"o2"16t"o3"n{r_o0,2X}{r_o1,2X}{r_o2,2X}{r_o3,2X}
+/"o4"16t"o5"16t"o6"16t"o7"n{r_o4,2X}{r_o5,2X}{r_o6,2X}{r_o7,2X}
#endif /* sun4m/sun4c/sun4d */

#elif defined(i386)
./"gs"16t"fs"16t"es"16t"ds"n{r_gs,X}{r_fs,X}{r_es,X}{r_ds,X}
+/"edi"16t"esi"16t"ebp"16t"esp"n{r_edi,X}{r_esi,X}{r_ebp,X}{r_esp,X}
+/"ebx"16t"edx"16t"ecx"16t"eax"n{r_ebx,X}{r_edx,X}{r_ecx,X}{r_eax,X}
+/"trapno"16t"err"16t"eip"16t"cs"n{r_trapno,X}{r_err,X}{r_eip,X}{r_cs,X}
+/"efl"16t"uesp"16t"ss"n{r_efl,X}{r_uesp,X}{r_ss,X}
#else
#error Unknown architecture!
#endif
