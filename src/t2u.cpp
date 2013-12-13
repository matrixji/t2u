#include "t2u.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <event.h>

#include <string>
#include <map>

#define FORWARD_MAX (64)


// rule internal
class forward_rule_internal
{
public:
    struct forward_rule_ *rule;
};

// context internal
class forward_context_internal
{
public:
    forward_context_internal(t2u_socket sock)
    {
        sock_ = sock;
        context = new forward_context;
        context->internal = this;
        pthread_mutex_init(&mutex, NULL);
    };

    ~forward_context_internal()
    {
        delete context;
    };

    t2u_socket sock_;
    forward_context *context;
    std::map<std::string, forward_rule_internal*> rules;
    pthread_mutex_t mutex;
};

std::map<t2u_socket, forward_context_internal*> g_contexts;
pthread_mutex_t g_contexts_mutex = PTHREAD_MUTEX_INITIALIZER;

// start event loop
static void start_event_loop()
{
    static int init = 0;

    if (!init)
    {
        event_init();
        init = 1;
    }
    
    event_dispatch();
}

// end event loop
static void stop_event_loop()
{
    event_loopexit(NULL);
}

//
forward_context *init_forward(t2u_socket udpsocket)
{
    forward_context *ret = NULL;    

    pthread_mutex_lock(&g_contexts_mutex);
    
    std::map<t2u_socket, forward_context_internal *>::iterator it = g_contexts.find(udpsocket);
    if (it != g_contexts.end())
    {
        pthread_mutex_unlock(&g_contexts_mutex);   
        return NULL;
    }

    // insert new one.
    forward_context_internal *context_internal = new forward_context_internal(udpsocket);
    g_contexts[udpsocket] = context_internal;

    pthread_mutex_unlock(&g_contexts_mutex);

    return context_internal->context;
}

void free_forward(forward_context *context)
{
    pthread_mutex_lock(&g_contexts_mutex);
    
    std::map<t2u_socket, forward_context_internal *>::iterator it = g_contexts.begin();
    while (it != g_contexts.end())
    {
        if (it->second == context->internal)
        {
            delete context;
            g_contexts.erase(it->first);
        }
    }

    pthread_mutex_unlock(&g_contexts_mutex);
}


