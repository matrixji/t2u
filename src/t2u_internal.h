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

private:
    // internal lock mutex
    pthread_mutex_t mutex_;

};


class forward_rule_internal: public internal_object
{
public:
    forward_rule_internal(forward_rule *addrule);

    virtual ~forward_rule_internal();

    // init listen for client mode. 
    bool init();

 private:  
    forward_rule *rule;
    
    std::list<t2u_socket> sockList_;
    std::list<ev_io> eventList_;
    
};

class forward_context_internal: public internal_object
{
public:
    forward_context_internal(t2u_socket udpsocket);

    virtual ~forward_context_internal();

    bool init();

    forward_context *context();

private:
    forward_context *context_;
    t2u_socket sock_;
    ev_io event_;
    std::map<std::string, forward_rule_internal*> rules_;
};


class forward_runner
{
public:
    forward_runner &instance();

    void start();

    void stop();

private:
    forward_runner();


private:
    struct ev_loop *ev_;
};



#endif /* __t2u_internal_h__ */