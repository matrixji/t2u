#include "t2u.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <ev.h>

#include <list>
#include <string>
#include <map>

#define FORWARD_MAX (64)


static struct ev_loop* g_reactor = NULL;

// rule internal
class forward_rule_internal
{
public:
    forward_rule_internal(forward_rule *addrule)
    {
        rule = new forward_rule;
        memcpy(rule, addrule, sizeof(rule));
        
        pthread_mutex_init(&mutex_, NULL);
        ref_count_ = 1;

    }

    ~forward_rule_internal()
    {
        delete rule;
    }

    bool init()
    {
        if (rule->mode = forward_client_mode)
        {
            // need to 
        }
    }

    void retain() 
    {
        ++ref_count_;
    };

    void release() 
    {
        --ref_count_;
        if (ref_count_ == 0)
        {
            delete this;
        }
    }

    void lock()
    {
        pthread_mutex_lock(&mutex_);
    }

    void unlock()
    {
        pthread_mutex_unlock(&mutex_);
    }
    
    forward_rule *rule;

private:
    int ref_count_;
    pthread_mutex_t mutex_;
    
    std::list<t2u_socket> sockList_;
    std::list<ev_io> eventList_;
    
};

// context internal
class forward_context_internal
{
public:
    static void handle_udp(struct ev_loop* reactor,ev_io* w,int events)
    {
    
    };

    forward_context_internal(t2u_socket sock)
    {
        sock_ = sock;
        context = new forward_context;
        context->internal = this;
        pthread_mutex_init(&mutex_, NULL);
        ref_count_ = 1;
        
        ev_io_init(&event_, handle_udp, sock_, EV_READ);
        ev_io_start(g_reactor, &event_);
    };

    ~forward_context_internal()
    {
        ev_io_stop(g_reactor, &event_);
        delete context;
    };

    void retain() 
    {
        ++ref_count_;
    };

    void release() 
    {
        --ref_count_;
        if (ref_count_ == 0)
        {
            delete this;
        }
    }

    void lock()
    {
        pthread_mutex_lock(&mutex_);
    }

    void unlock()
    {
        pthread_mutex_unlock(&mutex_);
    }

    forward_context *context;
    std::map<std::string, forward_rule_internal*> rules;

private:
    int ref_count_;
    pthread_mutex_t mutex_;
    
    t2u_socket sock_;
    ev_io event_;
};


std::map<t2u_socket, forward_context_internal*> g_contexts;
pthread_mutex_t g_contexts_mutex = PTHREAD_MUTEX_INITIALIZER;

// start event loop
static void start_event_loop()
{
    g_reactor=ev_loop_new(EVFLAG_AUTO);
    ev_run(g_reactor, EVRUN_NOWAIT);
}

// end event loop
static void stop_event_loop()
{
    ev_loop_destroy(g_reactor);
}


//
forward_context *init_forward(t2u_socket udpsocket)
{
    pthread_mutex_lock(&g_contexts_mutex);
    
    std::map<t2u_socket, forward_context_internal *>::iterator it = g_contexts.find(udpsocket);
    if (it != g_contexts.end())
    {
        pthread_mutex_unlock(&g_contexts_mutex);   
        return NULL;
    }

    // 
    if (0 == g_contexts.size()) 
    {
        start_event_loop();
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
            it->second->release();
            g_contexts.erase(it->first);
            if (0 == g_contexts.size())
            {
                pthread_mutex_unlock(&g_contexts_mutex);
                stop_event_loop();
                return;
            }
            pthread_mutex_unlock(&g_contexts_mutex);
            return;
        }
        ++it;
    }

    pthread_mutex_unlock(&g_contexts_mutex);
    return;
}


int add_forward_rule(forward_rule *rule)
{
    forward_context_internal *internal_context = NULL;

    pthread_mutex_lock(&g_contexts_mutex);
    
    std::map<t2u_socket, forward_context_internal *>::iterator it = g_contexts.begin();
    while (it != g_contexts.end())
    {
        if (it->second == rule->context->internal)
        {
            internal_context = it->second;
            internal_context->retain();
            break;
        }
    }
    pthread_mutex_unlock(&g_contexts_mutex);

    if (internal_context)
    {
        // try add rule
        internal_context->lock();
        std::map<std::string, forward_rule_internal*>::iterator it = internal_context->rules.begin();
        while (it != internal_context->rules.end())
        {
            if ((it->first == std::string(rule->service)) ||
                (it->second->rule->port == rule->port))
            {
                // already exist same name or same port.
                internal_context->unlock();
                internal_context->release();
                return -1;
            }
            ++it;
        }
        
        // add this one.
        internal_context->rules[std::string(rule->service)] = new forward_rule_internal(rule);
        
        internal_context->unlock();
        internal_context->release();
    }
    else
    {
        return -1;
    }

    return 0;
}


