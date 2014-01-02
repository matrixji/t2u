#include "t2u_thread.h"


/* create a thread */
int t2u_thr_create(t2u_thr_t *tid, t2u_thr_proc proc, void *arg)
{
#ifdef __GNUC__
    return pthread_create(tid, NULL, proc, arg);
#endif
#ifdef _MSC_VER
    *tid = CreateThread(NULL, 0, proc, arg, 0, NULL);
    if (NULL != *tid)
    {
        return 0;
    }
    else
    {
        return -1;
    }
#endif
}

/* join thread */
int t2u_thr_join(t2u_thr_t tid)
{
#ifdef __GNUC__
    return pthread_join(tid, NULL);
#endif
#ifdef _MSC_VER
    WaitForSingleObject(tid, INFINITE);
    CloseHandle(tid);
    return 0;
#endif
}

/* mutex init */
int t2u_mutex_init(t2u_mutex_t *mutex)
{
#ifdef __GNUC__
    return pthread_mutex_init(mutex, NULL);
#endif
#ifdef _MSC_VER
    InitializeCriticalSection(mutex);
    return 0;
#endif
}

/* mutex lock */
int t2u_mutex_lock(t2u_mutex_t *mutex)
{
#ifdef __GNUC__    
    return pthread_mutex_lock(mutex);
#endif
#ifdef _MSC_VER
    EnterCriticalSection(mutex);
    return 0;
#endif    
}

/* mutex unlock */
int t2u_mutex_unlock(t2u_mutex_t *mutex)
{
#ifdef __GNUC__
    return pthread_mutex_unlock(mutex);
#endif
#ifdef _MSC_VER
    LeaveCriticalSection(mutex);
    return 0;
#endif
}

/* cond init */
int t2u_cond_init(t2u_cond_t *cond)
{
#ifdef __GNUC__
    return pthread_cond_init(cond, NULL);
#endif
#ifdef _MSC_VER
    *cond = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (NULL != *cond)
    {
        return 0;
    }
    else
    {
        return -1;
    }
#endif
}

/* cond wait */
int t2u_cond_wait(t2u_cond_t *cond, t2u_mutex_t *mutex)
{
#ifdef __GNUC__
    return pthread_cond_wait(cond, mutex);
#endif
#ifdef _MSC_VER
    DWORD ret = 0;
    t2u_mutex_unlock(mutex);
    ret = WaitForSingleObject(*cond, INFINITE);
    t2u_mutex_lock(mutex);
    if (ret == WAIT_OBJECT_0)
    {
        return 0;
    }
    else if (WAIT_TIMEOUT == ret)
    {
        return ETIMEDOUT;
    }
    else
    {
        return -1;
    }
#endif
}

/* cond timedwait */
int t2u_cond_timedwait(t2u_cond_t *cond, t2u_mutex_t *mutex, unsigned long wait /* ms */)
{
#ifdef __GNUC__
    struct timespec w = { wait / 1000, (wait % 1000) * 1000000};
    return pthread_cond_timedwait(cond, mutex, &w);
#endif
#ifdef _MSC_VER
    DWORD ret = 0;
    t2u_mutex_unlock(mutex);
    ret = WaitForSingleObject(*cond, wait);
    t2u_mutex_lock(mutex);
        if (ret == WAIT_OBJECT_0)
    {
        return 0;
    }
    else if (WAIT_TIMEOUT == ret)
    {
        return ETIMEDOUT;
    }
    else
    {
        return -1;
    }
#endif
}

/* cond signal */
int t2u_cond_signal(t2u_cond_t *cond)
{
#ifdef __GNUC__
    return pthread_cond_signal(cond);
#endif
#ifdef _MSC_VER
    if (0 == SetEvent(*cond))
    {
        return -1;
    }
    else
    {
        return 0;
    }
#endif
}

t2u_thr_id t2u_thr_self()
{
#ifdef __GNUC__
    return pthread_self();
#endif
#ifdef _MSC_VER
    return GetCurrentThreadId();
#endif
}


void t2u_sleep(unsigned long ms)
{
#ifdef __GNUC__
    struct timeval t;
    t.tv_sec = ms / 1000;
    t.tv_usec = (ms % 1000) * 1000;
    select(0, 0, 0, 0, &t);
#endif
#ifdef _MSC_VER
    GetCurrentThreadId(ms);
#endif
}