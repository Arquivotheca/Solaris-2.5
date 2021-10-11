/*
 * Copyright (c) 1985-1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)softint.c	1.18	94/09/29 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/systm.h>
#include <sys/spl.h>
#include <sys/cmn_err.h>

/*
 * Handle software interrupts through 'softcall' mechanism
 */

typedef void (*func_t)(caddr_t);

#define	NSOFTCALLS	50

static struct softcall {
	func_t	sc_func;		/* function to call */
	caddr_t	sc_arg;			/* arg to pass to func */
	kmutex_t	*sc_mutex;	/* mutex to get for function */
	struct softcall *sc_next;	/* next in list */
} softcalls[NSOFTCALLS];

static struct softcall *softhead, *softtail, *softfree;

static kmutex_t	softcall_lock;		/* protects softcall lists */

void
softcall_init(void)
{
	register struct softcall *sc;

	for (sc = softcalls; sc < &softcalls[NSOFTCALLS]; sc++) {
		sc->sc_next = softfree;
		softfree = sc;
	}
	mutex_init(&softcall_lock, "softcall", MUTEX_SPIN_DEFAULT,
		(void *)ipltospl(SPL7));
}

/*
 * Call function func with argument arg
 * at some later time at software interrupt priority
 */
void
softcall(register func_t func, caddr_t arg)
{
	register struct softcall *sc;
	extern void siron();

	/*
	 * protect against cross-calls
	 */
	mutex_enter(&softcall_lock);
	/* coalesce identical softcalls */
	for (sc = softhead; sc != 0; sc = sc->sc_next)
		if (sc->sc_func == func && sc->sc_arg == arg)
			goto out;
	if ((sc = softfree) == 0)
		panic("too many softcalls");
	softfree = sc->sc_next;
	sc->sc_func = func;
	sc->sc_arg = arg;

	/*
	 * If the thread was holding the unsafe driver mutex, arrange for
	 * it to be acquired when the "func" is called.
	 */
	if (UNSAFE_DRIVER_LOCK_HELD())
		sc->sc_mutex = &unsafe_driver;
	else
		sc->sc_mutex = NULL;

	sc->sc_next = 0;

	if (softhead) {
		softtail->sc_next = sc;
		softtail = sc;
	} else {
		softhead = softtail = sc;
		siron();
	}
out:
	mutex_exit(&softcall_lock);
}

/*
 * Called to process software interrupts
 * take one off queue, call it, repeat
 * Note queue may change during call
 */
void
softint(void)
{
	register struct softcall *sc;
	register func_t func;
	register caddr_t arg;
	register kmutex_t *mp;

	for (;;) {
		mutex_enter(&softcall_lock);
		if ((sc = softhead) != NULL) {
			func = sc->sc_func;
			arg = sc->sc_arg;
			mp = sc->sc_mutex;
			softhead = sc->sc_next;
			sc->sc_next = softfree;
			softfree = sc;
		}
		mutex_exit(&softcall_lock);
		if (sc == NULL)
			return;
		if (mp != NULL) {
			mutex_enter(mp);
			(*func)(arg);
			mutex_exit(mp);
		} else {
			(*func)(arg);
		}
	}
}
