/*
 * Win32 critical sections
 *
 * Copyright 1998 Alexandre Julliard
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>

#include "config.h"
#include "winerror.h"
#include "winbase.h"
#include "ntddk.h"
#include "debugtools.h"
#include "thread.h"

DEFAULT_DEBUG_CHANNEL(win32);
DECLARE_DEBUG_CHANNEL(relay);

/***********************************************************************
 *           InitializeCriticalSection   (KERNEL32.@)
 */
void WINAPI InitializeCriticalSection( CRITICAL_SECTION *crit )
{
    NTSTATUS ret = RtlInitializeCriticalSection( crit );
    if (ret) RtlRaiseStatus( ret );
}

/***********************************************************************
 *           InitializeCriticalSectionAndSpinCount   (KERNEL32.@)
 */
BOOL WINAPI InitializeCriticalSectionAndSpinCount( CRITICAL_SECTION *crit, DWORD spincount )
{
    NTSTATUS ret = RtlInitializeCriticalSectionAndSpinCount( crit, spincount );
    if (ret) RtlRaiseStatus( ret );
    return !ret;
}

/***********************************************************************
 *           SetCriticalSectionSpinCount   (KERNEL32.@)
 * This function is available on NT4SP3 or later, but not Win98
 * It is SMP related
 */
DWORD WINAPI SetCriticalSectionSpinCount( CRITICAL_SECTION *crit, DWORD spincount )
{
    ULONG_PTR oldspincount = crit->SpinCount;
    if(spincount) FIXME("critsection=%p: spincount=%ld not supported\n", crit, spincount);
    crit->SpinCount = spincount;
    return oldspincount;
}

/***********************************************************************
 *           MakeCriticalSectionGlobal   (KERNEL32.@)
 */
void WINAPI MakeCriticalSectionGlobal( CRITICAL_SECTION *crit )
{
    /* let's assume that only one thread at a time will try to do this */
    HANDLE sem = crit->LockSemaphore;
    if (!sem) NtCreateSemaphore( &sem, SEMAPHORE_ALL_ACCESS, NULL, 0, 1 );
    crit->LockSemaphore = ConvertToGlobalHandle( sem );
}


/***********************************************************************
 *           ReinitializeCriticalSection   (KERNEL32.@)
 */
void WINAPI ReinitializeCriticalSection( CRITICAL_SECTION *crit )
{
    if ( !crit->LockSemaphore )
        RtlInitializeCriticalSection( crit );
}


/***********************************************************************
 *           UninitializeCriticalSection   (KERNEL32.@)
 */
void WINAPI UninitializeCriticalSection( CRITICAL_SECTION *crit )
{
    RtlDeleteCriticalSection( crit );
}

#ifdef __i386__

/***********************************************************************
 *		InterlockedCompareExchange (KERNEL32.@)
 */
/* PVOID WINAPI InterlockedCompareExchange( PVOID *dest, PVOID xchg, PVOID compare ); */
__ASM_GLOBAL_FUNC(InterlockedCompareExchange,
                  "movl 12(%esp),%eax\n\t"
                  "movl 8(%esp),%ecx\n\t"
                  "movl 4(%esp),%edx\n\t"
                  "lock; cmpxchgl %ecx,(%edx)\n\t"
                  "ret $12");

/***********************************************************************
 *		InterlockedExchange (KERNEL32.@)
 */
/* LONG WINAPI InterlockedExchange( PLONG dest, LONG val ); */
__ASM_GLOBAL_FUNC(InterlockedExchange,
                  "movl 8(%esp),%eax\n\t"
                  "movl 4(%esp),%edx\n\t"
                  "lock; xchgl %eax,(%edx)\n\t"
                  "ret $8");

/***********************************************************************
 *		InterlockedExchangeAdd (KERNEL32.@)
 */
/* LONG WINAPI InterlockedExchangeAdd( PLONG dest, LONG incr ); */
__ASM_GLOBAL_FUNC(InterlockedExchangeAdd,
                  "movl 8(%esp),%eax\n\t"
                  "movl 4(%esp),%edx\n\t"
                  "lock; xaddl %eax,(%edx)\n\t"
                  "ret $8");

/***********************************************************************
 *		InterlockedIncrement (KERNEL32.@)
 */
/* LONG WINAPI InterlockedIncrement( PLONG dest ); */
__ASM_GLOBAL_FUNC(InterlockedIncrement,
                  "movl 4(%esp),%edx\n\t"
                  "movl $1,%eax\n\t"
                  "lock; xaddl %eax,(%edx)\n\t"
                  "incl %eax\n\t"
                  "ret $4");

/***********************************************************************
 *		InterlockedDecrement (KERNEL32.@)
 */
__ASM_GLOBAL_FUNC(InterlockedDecrement,
                  "movl 4(%esp),%edx\n\t"
                  "movl $-1,%eax\n\t"
                  "lock; xaddl %eax,(%edx)\n\t"
                  "decl %eax\n\t"
                  "ret $4");

#elif defined(__sparc__) && defined(__sun__)

/*
 * As the earlier Sparc processors lack necessary atomic instructions,
 * I'm simply falling back to the library-provided _lwp_mutex routines
 * to ensure mutual exclusion in a way appropriate for the current 
 * architecture.  
 *
 * FIXME:  If we have the compare-and-swap instruction (Sparc v9 and above)
 *         we could use this to speed up the Interlocked operations ...
 */

#include <synch.h>
static lwp_mutex_t interlocked_mutex = DEFAULTMUTEX;

/***********************************************************************
 *		InterlockedCompareExchange (KERNEL32.@)
 */
LONG WINAPI InterlockedCompareExchange( PLONG dest, LONG xchg, LONG compare )
{
    _lwp_mutex_lock( &interlocked_mutex );

    if ( *dest == compare )
        *dest = xchg;
    else
        compare = *dest;
    
    _lwp_mutex_unlock( &interlocked_mutex );
    return compare;
}

/***********************************************************************
 *		InterlockedExchange (KERNEL32.@)
 */
LONG WINAPI InterlockedExchange( PLONG dest, LONG val )
{
    LONG retv;
    _lwp_mutex_lock( &interlocked_mutex );

    retv = *dest;
    *dest = val;

    _lwp_mutex_unlock( &interlocked_mutex );
    return retv;
}

/***********************************************************************
 *		InterlockedExchangeAdd (KERNEL32.@)
 */
LONG WINAPI InterlockedExchangeAdd( PLONG dest, LONG incr )
{
    LONG retv;
    _lwp_mutex_lock( &interlocked_mutex );

    retv = *dest;
    *dest += incr;

    _lwp_mutex_unlock( &interlocked_mutex );
    return retv;
}

/***********************************************************************
 *		InterlockedIncrement (KERNEL32.@)
 */
LONG WINAPI InterlockedIncrement( PLONG dest )
{
    LONG retv;
    _lwp_mutex_lock( &interlocked_mutex );

    retv = ++*dest;

    _lwp_mutex_unlock( &interlocked_mutex );
    return retv;
}

/***********************************************************************
 *		InterlockedDecrement (KERNEL32.@)
 */
LONG WINAPI InterlockedDecrement( PLONG dest )
{
    LONG retv;
    _lwp_mutex_lock( &interlocked_mutex );

    retv = --*dest;

    _lwp_mutex_unlock( &interlocked_mutex );
    return retv;
}

#else
#error You must implement the Interlocked* functions for your CPU
#endif
