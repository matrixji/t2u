#ifndef __t2u_thread_h__
#define __t2u_thread_h__

#if defined __GNUC__
    #include <pthread.h>
    #include <sys/time.h>
    #include <sys/types.h>
    #include <unistd.h>
    typedef pthread_t t2u_thr_t;
    typedef pthread_t t2u_thr_id;
    typedef void*(*t2u_thr_proc)(void *);
    typedef pthread_mutex_t t2u_mutex_t;
    typedef pthread_cond_t t2u_cond_t;
#elif defined _MSC_VER
#include <Windows.h>
    typedef HANDLE t2u_thr_t;
    typedef DWORD t2u_thr_id;
    typedef DWORD (__stdcall *t2u_thr_proc)(PVOID);
    typedef CRITICAL_SECTION t2u_mutex_t;
    typedef HANDLE t2u_cond_t;
#else
    #error "Compiler not support."
#endif

#ifndef ETIMEDOUT
#define ETIMEDOUT (110)
#endif
#define T2U_ETIMEDOUT (ETIMEDOUT)

/* create a thread */
int t2u_thr_create(t2u_thr_t *tid, t2u_thr_proc proc, void *arg);

/* join thread */
int t2u_thr_join(t2u_thr_t tid);

/* mutex init */
int t2u_mutex_init(t2u_mutex_t *mutex);

/* mutex lock */
int t2u_mutex_lock(t2u_mutex_t *mutex);

/* mutex unlock */
int t2u_mutex_unlock(t2u_mutex_t *mutex);

/* cond init */
int t2u_cond_init(t2u_cond_t *cond);

/* cond wait */
int t2u_cond_wait(t2u_cond_t *cond, t2u_mutex_t *mutex);

/* cond timedwait */
int t2u_cond_timedwait(t2u_cond_t *cond, t2u_mutex_t *mutex, unsigned long wait /* ms */);

/* cond signal */
int t2u_cond_signal(t2u_cond_t *cond);

/* get current thread id */
t2u_thr_id t2u_thr_self();

/* sleep ms */
void t2u_sleep(unsigned long ms);


#endif /* __t2u_thread_h__ */
