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


internal_object::internal_object()
{
    ref_ = 1;
    mutex_ = PTHREAD_MUTEX_INITIALIZER;
}