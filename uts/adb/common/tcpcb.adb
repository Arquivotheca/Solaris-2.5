#include <sys/types.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp.h>
#include <netinet/tcp_var.h>

tcpcb
./"next"16t"prev"16t"state"8t"rxtshift"n{seg_next,X}{seg_prev,X}{t_state,d}{t_rxtshift,d}
+/"rxtcur"8t"dups"8t"maxseg"8t"force"8t"flags"n{t_rxtcur,d}{t_dupacks,d}{t_maxseg,d}{t_force,b}{t_flags,b}
+/"templ"16t"inpcb"16t"sdnuna"16t"sndnxt"n{t_template,X}{t_inpcb,X}{snd_una,D}{snd_nxt,D}
+/"sndup"16t"sndwl1"16t"sndwl2"16t"iss"n{snd_up,D}{snd_wl1,D}{snd_wl2,D}{iss,D}
+/"sndwnd"8t"rcvwnd"8t"rcvnxt"16t"rcvup"16t"irs"n{snd_wnd,d}{rcv_wnd,d}{rcv_nxt,D}{rcv_up,D}{irs,D}
+/"rcvadv"16t"sndmax"16t"sndcwnd"8t"sndssth"n{rcv_adv,D}{snd_max,D}{snd_cwnd,d}{snd_ssthresh,d}
+/"idle"8t"rtt"8t"rtseq"16t"srtt"8t"rttvar"n{t_idle,d}{t_rtt,d}{t_rtseq,D}{t_srtt,d}{t_rttvar,d}
+/"maxrcv"8t"maxswnd"8t"oobflg"8t"iobc"n{max_rcvd,d}{max_sndwnd,d}{t_oobflags,b}{t_iobc,b}
