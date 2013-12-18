#include "t2u.h"
#include "t2u_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <ev.h>

#include <list>
#include <string>
#include <map>

// constructor
internal_object::internal_object()
{
    mutex_ = PTHREAD_MUTEX_INITIALIZER;
}

// lock for protect internal data
void internal_object::lock()
{
	pthread_mutex_lock(&mutex_);
}

// unlock
void internal_object::unlock()
{
	pthread_mutex_unlock(&mutex_);
}


// constructor
forward_runner::forward_runner():
	ev_(ev_default_loop(0))
{

}

// instance
forward_runner &forward_runner::instance()
{
	static forward_runner instance_;
	return instance_;
}

// start
void forward_runner::start()
{
	ev_run(ev_, EVRUN_NOWAIT);
}

// stop
void forward_runner::stop()
{
	ev_break (ev_, EVBREAK_ALL);
}