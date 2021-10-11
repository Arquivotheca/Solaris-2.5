#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/priocntl.h>
#include <sys/rtpriocntl.h>
#include <thread.h>
#include <lwp.h>

#define M60

#ifdef M260
#define PERIOD	(60e-9)
#endif M260
#ifdef M60
#define PERIOD	(50e-9)
#endif M60
#ifdef M490
#define PERIOD	(30e-9)
#endif M490
#ifdef M65
#define PERIOD	(40e-9)
#endif M65

static struct timeval before, after;
long end_time();
void null();

#define N 100000

long lt;

main()
{
	register int i;
	register int pid;
	register lwpid_t lwp;
	long mt, gt, st, ratio;
	long not, ot;

	setbuf(stdout, 0);
#ifdef PRINT
	printf("empty loop: %d iterations\n", 100*N);
	sleep(1);	/* let bytes dribble out */
#endif PRINT
	start_time();
	for (i = 100*N; --i >= 0; ) {
	}
	lt = end_time() / 100;

#ifdef PRINT
	printf("%d moves (%.2f sec @ %.2fMhz)\n",
	    1000*N, (long)PERIOD*(1000*N), (long)1.0/(PERIOD*1000000));
	sleep(1);	/* let bytes dribble out */
#endif PRINT
	start_time();
#define	move5 asm("mov %g1,%g1;mov %g1,%g1;mov %g1,%g1;mov %g1,%g1;mov %g1,%g1")
	for (i = N; --i >= 0; ) {
		/* 20 lines of 10 * 5 move instructions == 1000 moves */
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
		move5;move5;move5;move5;move5;move5;move5;move5;move5;move5;
	}
	mt = end_time();
	printf("speed of light: %d.%.2d%% degradation\n",
	    (mt-lt-(50*N))*100/(50*N),
	    (mt-lt-(50*N))*10000/(50*N)%100);
	printf(
"\t(computed = %d usec, actual = %d usec,\n\t%d loop overhead = %d usec)\n",
	    50*N, mt, N, lt);
	sleep(1);	/* let bytes dribble out */

#ifdef PRINT
	printf("%d getpid()s\n", N);
	sleep(1);	/* let bytes dribble out */
#endif PRINT
	start_time();
	for (i = N; --i >= 0; ) {
		getpid();
	}
	gt = end_time();
	printf("getpid = %d usec\n", (gt - lt) / N);
	sleep(1);	/* let bytes dribble out */

#ifdef PRINT
	printf("%d signals\n", N);
	sleep(1);	/* let bytes dribble out */
#endif PRINT
	sigset(SIGUSR1, null);
	lwp = lwp_self();
	start_time();
	for (i = N; --i >= 0; ) {
		lwp_kill(lwp, SIGUSR1);
	}
	st = end_time();
	printf("lwp_kill = %d usec\n", (st - gt) / N);
	cswitch();
}

char *rtstk[8192];

pcparms_t pcp;
thread_t rt;
lwpid_t lwp0;

cswitch()
{
	long ct, pt, st, ot;
	pcinfo_t pci;
	int i;
	char buf[1024];
	void csw();

	rt = thr_create(rtstk, 8192, csw, NULL, THR_BOUND|THR_SUSPENDED);
	if (rt == (thread_t)-1) {
		printf("couldn't create thread\n");
		exit (1);
	}
	strcpy(&pci.pc_clname, "RT");
	if (priocntl(NULL,NULL, PC_GETCID, (caddr_t)&pci) < 0) {
		printf("unknown class\n");
		exit (1);
	}
	pcp.pc_cid = pci.pc_cid;
	lwp0 = lwp_self();
	start_time();
	for (i = N; --i >= 0; ) {
		setrtpri(lwp0, 16);
	}
	pt = end_time();
	ot = lt/10000;
	printf("priocntl SETPARMS = %d usec\n", (pt - lt)/N);
	fflush(stdout);
	setrtpri(thr_bound(rt), 0);
	if (thr_continue(rt) < 0) {
		printf("couldn't continue realtime thread\n");
		exit (1);
	}
	start_time();
	for(i = 0; i < N; i++) {
		setrtpri(thr_bound(rt), 16);
		setrtpri(lwp0, 1);
	}
	st = end_time();
	ot = 4 * pt + lt;
	printf("context switch time = %d usec\n", (st - ot)/(2*N));
	exit (0);
}

setrtpri(lwp, pri)
	lwpid_t lwp;
	int pri;
{
	rtparms_t *rtp;

	rtp = (rtparms_t *)pcp.pc_clparms;
	rtp->rt_pri = pri;
	rtp->rt_tqnsecs = RT_NOCHANGE;
	if (priocntl(P_LWPID, lwp, PC_SETPARMS, (caddr_t)&pcp) < 0) {
		printf("priocntl couldn't setrtpri for lwp %d\n", lwp);
		exit (1);
	}
}

void
csw()
{
	for(;;) {
		setrtpri(lwp0, 16);
		setrtpri(thr_bound(rt), 1);
	}
}

start_time()
{
	gettimeofday(&before, 0);
}

long
end_time()
{
	long usec;

	gettimeofday(&after, 0);
	usec =
	    (after.tv_sec - before.tv_sec) * 1000000 +
            after.tv_usec - before.tv_usec;
/*
	printf("real time = %d usec\n", usec);
*/
	return(usec);
}

void null() {}
