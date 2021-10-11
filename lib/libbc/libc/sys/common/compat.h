#include <utmp.h>
#include <sys/types.h>
#include <sys/time.h>

struct compat_utmp
  {
      char ut_user[8] ;               /* User login name */
      char ut_id[4] ;                 /* /etc/inittab id(usually line #) */
      char ut_line[12] ;              /* device name (console, lnxx) */
      short ut_pid ;                  /* leave short for compatiblity - process id */
      short ut_type ;                 /* type of entry */
      struct exit_status
        {
          short e_termination ;       /* Process termination status */
          short e_exit ;              /* Process exit status */
        }
      ut_exit ;                       /* The exit status of a process
                                       * marked as DEAD_PROCESS.
                                       */
      time_t ut_time ;                /* time entry was made */
  } ;



struct utmpx
  {
      char    ut_user[32];            /* user login name */
      char    ut_id[4];               /* inittab id */
      char    ut_line[32];            /* device name (console, lnxx) */
      long   ut_pid;                 /* process id */
      short   ut_type;                /* type of entry */
      struct exit_status ut_exit;     /* process termination/exit status */
      struct timeval ut_tv;           /* time entry was made */
      long    ut_session;             /* session ID, used for windowing */
      long    pad[5];                 /* reserved for future use */
      short   ut_syslen;              /* significant length of ut_host */
                                      /*   including terminating null */
      char    ut_host[257];           /* remote host name */
  } ;


#define getmodsize(size, ftype, ttype)	\
	(((size / ftype) * ttype) + (size % ftype))
