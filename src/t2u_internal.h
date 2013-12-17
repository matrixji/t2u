#ifndef __t2u_internal_h__
#define __t2u_internal_h__

#include <pthread.h>
#include "t2u.h"

class internal_object
{
private:
   virtual ~internal_object();

public:
    internal_object();
 
    // lock for protect members
    void lock();

    void unlock();

    void retain();

    void release();

private:
    // reference counter
    int ref_;

    // internal lock mutex
    pthread_mutex_t mutex_;

};


class forward_rule_internal: public internal_object
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



#endif /* __t2u_internal_h__ */