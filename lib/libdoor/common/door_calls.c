/*
 * Copyright (c) Sun Microsystems Inc. 1994
 */
#pragma ident "@(#)door_calls.c 1.3	95/07/13 SMI"

#include <thread.h>
#include <stdio.h>
#include <errno.h>
#include <sys/door.h>

static void door_create_server(void);

#pragma	init	(_door_init)

extern void (*__door_server_func)();

/*
 * Initialize the door server create function if it hasn't been.
 */
static void
_door_init()
{
	if (__door_server_func == NULL)
		__door_server_func = door_create_server;
}

int
door_create(void (*f)(), void *cookie, u_int flags)
{
	int	d;
	int	pid;
	static int firstcall = 0;

	if ((d = _door_create(f, cookie, flags)) < 0)
		return (d);
	/*
	 * If this is the first door created, fire off a server thread.
	 * Additional server threads are created during door invocations.
	 */
	pid = getpid();
	if (firstcall != pid) {
		if (thr_main() == -1) {
			fprintf(stderr,
			    "libdoor: Not linked with a threads library!\n");
			close(d);
			errno = ENOSYS;
			return (-1);
		}
		firstcall = pid;
		(*__door_server_func)();
	}
	return (d);
}

int
door_revoke(int did)
{
	return (_door_revoke(did));
}

int
door_info(int did, door_info_t *di)
{
	return (_door_info(did, di));
}

int
door_cred(door_cred_t *dc)
{
	return (_door_cred(dc));
}

int
door_call(int did, void **buf, int *bsize, int *asize, int *nfd)
{
	return (_door_call(did, buf, bsize, asize, nfd));
}

int
door_return(void *data_ptr, int data_size, int actual_data, int nfd)
{
	return (_door_return(data_ptr, data_size, actual_data, nfd, 0));
}

/*
 * Install a new server creation function.
 */
void (*door_server_create(void (*create_func)()))(void (*)())
{
	void (*prev)() = __door_server_func;

	__door_server_func = create_func;
	return (prev);
}

/*
 * The default server thread creation routine
 */
static void
door_create_server(void)
{
	(void) thr_create(NULL, 0, (void *(*)())door_return,
			NULL, THR_BOUND, NULL);
	yield();	/* Gives server thread a chance to run */
}
