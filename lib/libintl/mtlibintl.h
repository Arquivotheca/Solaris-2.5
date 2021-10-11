#ident "@(#)mtlibintl.h 1.2 92/10/06 SMI"

#ifndef _MTLIBINTL_H
#define _MTLIBINTL_H

#ifdef _REENTRANT

#define mutex_lock(m)			_mutex_lock(m)
#define mutex_unlock(m)			_mutex_unlock(m)

#else

#define mutex_lock(m)
#define mutex_unlock(m)

#endif _REENTRANT


#endif _MTLIBINTL_H_
