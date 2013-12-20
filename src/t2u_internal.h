#ifndef __t2u_internal_h__
#define __t2u_internal_h__

#include "t2u.h"
#include <pthread.h>
#include <ev.h>

#include <string>
#include <map>
#include <exception>
#include <memory>

using std::shared_ptr;

class forward_session;
class forward_rule_internal;
class forward_context_internal;
class forward_runner;

typedef struct internal_ev_io_
{
    ev_io io;
    sock_t socket;
    forward_runner *runner;
    forward_context_internal *context;
    forward_rule_internal *rule;
    forward_session *session;
} internal_ev_io;

class internal_exception: public std::exception
{
public:
    internal_exception(std::string &what);

    virtual ~internal_exception() throw();

    virtual const char* what() const throw();

private:
    std::string what_;
};

class internal_object
{
public:
    internal_object();
    
    virtual ~internal_object();
 
    // lock for protect members
    void lock();

    void unlock();

private:
    // internal lock mutex
    pthread_mutex_t mutex_;

};

class forward_session: public internal_object
{
public:
    forward_session(sock_t socket, forward_rule_internal *rule);
    
    virtual ~forward_session();

    sock_t socket();

    void send_data(uint32_t start, uint32_t length);

public:
    static void handle_tcp_input_callback(struct ev_loop* reactor, ev_io* w, int events);

private:
    sock_t sock_;
    forward_rule_internal *rule_;
    char *data;
    uint32_t seq_;
    uint32_t ack_;
    char *rbuffer_;
    uint32_t offset_;
    uint64_t signature_;
};

class forward_rule_internal: public internal_object
{
public:
    forward_rule_internal(forward_context &context, 
                          forward_mode mode, 
                          const char *service,
                          unsigned short port);

    virtual ~forward_rule_internal();

    // init listen for client mode. 
    void init() throw (internal_exception &);

    // rule
    forward_rule &rule();

    // get service name.
    std::string &name();

    // get listen socket
    sock_t listen_socket();

    // add session.
    void add_session(shared_ptr<forward_session> session);

    // del session.
    void del_session(sock_t sock);

private:
    static void handle_tcp_connect_callback(struct ev_loop* reactor, ev_io* w, int events);

private:
    sock_t listen_sock_; //useful when in client mode.
    std::string service_name_;
    forward_rule rule_;
    std::map<sock_t, shared_ptr<forward_session> > sessions_;
};


class forward_context_internal: public internal_object
{
public:
    forward_context_internal(sock_t udpsocket, forward_runner &runner);

    virtual ~forward_context_internal();

    forward_runner &runner();

    void init() throw (internal_exception &);

    forward_context &context();

    sock_t socket();

    void add_rule(shared_ptr<forward_rule_internal> internal_rule)
        throw (internal_exception &);

    void del_rule(const char *service_name)
        throw (internal_exception &);

private:
    static void handle_udp_input_callback(struct ev_loop* reactor, ev_io* w, int events);
    
private:
    forward_runner &runner_;
    forward_context context_;
    sock_t sock_;
    std::map<std::string, shared_ptr<forward_rule_internal> > rules_;
};

class forward_runner: public internal_object
{
public:
    virtual ~forward_runner();

    static forward_runner &instance();

    void add_context(shared_ptr<forward_context_internal> context)
        throw (internal_exception &);

    void del_context(forward_context_internal *context)
        throw (internal_exception &);

    void add_watcher(shared_ptr<internal_ev_io> w);
    
    void del_watcher(shared_ptr<internal_ev_io> w);

    void del_watcher(sock_t sock);

private:
    forward_runner();
    
    void start();
    
    void stop();

    static void *loop_func_(void *args);
    static void timeout_callback (EV_P_ ev_timer *w, int revents);
    
    static void add_watcher_callback(int revents, void *args);
    static void del_watcher_callback(int revents, void *args);
    static void destructor_callback(int revents, void *args);

private:
    pthread_t loop_tid_;
    struct ev_loop *loop_;

    // event for add_watcher done.
    pthread_mutex_t ev_event_mutex_;
    pthread_cond_t ev_event_cond_;

    ev_timer timeout_watcher;
    std::map<sock_t, shared_ptr<forward_context_internal> > cs_;
    std::map<sock_t, shared_ptr<internal_ev_io> > ws_;
};



#endif /* __t2u_internal_h__ */
