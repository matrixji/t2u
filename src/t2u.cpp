#include "t2u.h"
#include "t2u_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <ev.h>

#include <list>
#include <string>
#include <map>
#include <exception>

// log callback
static void(*log_callback_func_)(int, const char *) = NULL;

#define LOG_(level, fmt...) do { \
    if (log_callback_func_) { \
        char mess_[1024]; \
        int n = snprintf(mess_, sizeof(mess_), "[%s:%d] ", __FILE__, __LINE__); \
        n += snprintf(&mess_[n], sizeof(mess_) - n, ##fmt); \
        if (n < (int)sizeof(mess_) - 2) { \
            if (mess_[n-1] != '\n') {\
                mess_[n++] = '\n'; \
                mess_[n++] = '\0'; \
            } \
            log_callback_func_ (level, mess_) ;\
        } \
    } \
} while (0)


// constructor
internal_exception::internal_exception(std::string &what):
    std::exception(), what_(what)
{
}

//
internal_exception::~internal_exception() throw()
{
}

// what error message
const char* internal_exception::what() const throw()
{
    return what_.c_str();
}

// constructor
internal_object::internal_object()
{
    pthread_mutex_init(&mutex_, NULL);
}

// destructor
internal_object::~internal_object()
{
}

// lock for protect internal data
void internal_object::lock()
{
    pthread_mutex_lock(&mutex_);
    LOG_(0, "lock %p.", this);
}

// unlock
void internal_object::unlock()
{
    pthread_mutex_unlock(&mutex_);
    LOG_(0, "unlock %p.", this);
}

// constructor
forward_rule_internal::forward_rule_internal(
    forward_context &context, 
    forward_mode mode, 
    const char *service,
    unsigned short port):
    internal_object(), 
    service_name_(service),
    rule_({mode, &context, service_name_.c_str(), port})
{
    LOG_(0, "new rule created, mode: %d, service: %s, port: %u.", mode, service, port);
}

// destructor
forward_rule_internal::~forward_rule_internal()
{
    LOG_(0, "delete rule mode: %d, service: %s, port: %u.", rule_.mode, rule_.service, rule_.port);
}

// init, XXX
void forward_rule_internal::init() throw (internal_exception &)
{
    // if in client mode, we need listen on lo
    if (rule_.mode == forward_client_mode)
    {
        sock_t socket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (-1 == socket)
        {
            std::string mess("create socket failed");
            throw internal_exception(mess);
        }

        // listen the socket
        if (-1 == bind())
    }
}

// rule
forward_rule &forward_rule_internal::rule()
{
    return rule_;
}

// name
std::string &forward_rule_internal::name()
{
    return service_name_;
}

// constructor
forward_context_internal::forward_context_internal(sock_t udpsocket, forward_runner &runner):
    internal_object(), runner_(runner), context_({(void *)this}), sock_(udpsocket)
{
    LOG_(0, "new context for socket: %d.", (int) sock_);
}

// destructor
forward_context_internal::~forward_context_internal()
{
    lock();
    std::map<std::string, shared_ptr<forward_rule_internal> >::iterator it = 
        rules_.begin();
    while (it != rules_.end())
    {
        rules_.erase(it++);
    }
    unlock();
    LOG_(0, "delete context for socket: %d.", int (sock_));
}

// get the runner
forward_runner &forward_context_internal::runner()
{
    return runner_;
}


// init XXX
void forward_context_internal::init() throw (internal_exception &)
{
    std::string mess_;
    throw internal_exception(mess_);
}

// 
forward_context &forward_context_internal::context()
{
    return context_;
}

//
sock_t forward_context_internal::socket()
{
    return sock_;
}

// add new rule
void forward_context_internal::add_rule(shared_ptr<forward_rule_internal> rule)
    throw (internal_exception &)
{
    lock();
    std::map<std::string, shared_ptr<forward_rule_internal> >::iterator it = 
        rules_.find(rule->name());
    if (it != rules_.end())
    {
        unlock();
        std::string mess = std::string("rule with name: ") + 
                           rule->name() + 
                           std::string(" is already exist");
        throw internal_exception(mess);
        return;
    }

    rules_[rule->name()] = rule;
    unlock();
}

void forward_context_internal::del_rule(const char *service_name)
    throw (internal_exception &)
{
    lock();
    std::map<std::string, shared_ptr<forward_rule_internal> >::iterator it = 
        rules_.find(service_name);
    if (it == rules_.end())
    {
        unlock();
        std::string mess = std::string("rule with name: ") + 
                           service_name + 
                           std::string(" is not exist");
        throw internal_exception(mess);
        return;
    }
    rules_.erase(it);
    unlock();
}

// constructor
forward_runner::forward_runner():
    internal_object(),
    loop_(ev_default_loop(0))
{
    pthread_mutex_init(&ev_event_mutex_, NULL);
    pthread_cond_init(&ev_event_cond_, NULL);

    LOG_(0, "constructor of forward_runner: %p.", this);
    start();
}

forward_runner::~forward_runner()
{
    LOG_(0, "destructor of forward_runner: %p.", this); 

    pthread_mutex_lock(&ev_event_mutex_);
    ev_once (loop_, 0, EV_READ, 0, 
        forward_runner::destructor_callback, (void *)this);

    // wait add finish
    pthread_cond_wait(&ev_event_cond_, &ev_event_mutex_);
    pthread_mutex_unlock(&ev_event_mutex_);

    stop();
}

void forward_runner::timeout_callback (EV_P_ ev_timer *w, int revents)
{
    printf("xxxxx\n");
}


// handle the udp packet. 
void forward_runner::handle_udp_input_callback(struct ev_loop* reactor, ev_io* w, int events)
{

}

// handle new watcher add
void forward_runner::add_watcher_callback(int revents, void *args)
{
    internal_ev_io *w = (internal_ev_io *) args;
    forward_runner *runner = (forward_runner *)w->runner;

    ev_io_start(runner->loop_, (ev_io *)w);

    pthread_mutex_lock(&runner->ev_event_mutex_);
    pthread_cond_signal(&runner->ev_event_cond_);
    pthread_mutex_unlock(&runner->ev_event_mutex_);
}

// handle watcher del
void forward_runner::del_watcher_callback(int revents, void *args)
{
    internal_ev_io *w = (internal_ev_io *) args;
    forward_runner *runner = (forward_runner *)w->runner;

    ev_io_stop(runner->loop_, (ev_io *)w);

    pthread_mutex_lock(&runner->ev_event_mutex_);
    pthread_cond_signal(&runner->ev_event_cond_);
    pthread_mutex_unlock(&runner->ev_event_mutex_);
}


// destructor callback
void forward_runner::destructor_callback(int revents, void *args)
{
    LOG_(0, "destructor_callback of forward_runner");
    forward_runner *runner = (forward_runner *)args;

    std::map<sock_t, shared_ptr<internal_ev_io> >::iterator it = 
        runner->ws_.begin();
    while (it != runner->ws_.end())
    {
        LOG_(0, "ev_io_stop ...");
        ev_io_stop(runner->loop_, (ev_io *)it->second.get());
        ++it;
    }

    pthread_mutex_lock(&runner->ev_event_mutex_);
    pthread_cond_signal(&runner->ev_event_cond_);
    pthread_mutex_unlock(&runner->ev_event_mutex_);
}

void *forward_runner::loop_func_(void *args)
{
    LOG_(1, "runner loop start ...");
    struct ev_loop *loop = (struct ev_loop *)args;
    ev_loop(loop, 0);
    LOG_(1, "runner loop finish ...");
    return NULL;
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
    // setup 
    ev_timer_init(&timeout_watcher, forward_runner::timeout_callback, 0., 1.0);
    ev_timer_start(loop_, &timeout_watcher);

    // 
    pthread_create(&loop_tid_, NULL, forward_runner::loop_func_, (void *)loop_);
}

// stop
void forward_runner::stop()
{
    // clear the timer
    ev_timer_stop(loop_, &timeout_watcher);

    // break it
    ev_break(loop_, EVBREAK_ALL);

    // wait ev loop end
    pthread_join(loop_tid_, NULL);
}

// add context
void forward_runner::add_context(shared_ptr<forward_context_internal> context)
    throw (internal_exception &)
{
    sock_t sock = context->socket();
    lock();

    {
        std::map<sock_t, shared_ptr<internal_ev_io> >::iterator it = ws_.find(sock);
        if (it != ws_.end())
        {
            unlock();
            std::string mess = std::string("event io with the socket is already exist");
            throw internal_exception(mess);
            return;
        }
    }

    // add the udp socket to loop_
    shared_ptr<internal_ev_io> w(new internal_ev_io());
    ev_io_init((ev_io *)w.get(), &forward_runner::handle_udp_input_callback, sock, EV_READ);
    w->socket = sock;
    w->runner = this;
    w->context = context;

    add_watcher(w);

    // add to map
    ws_[sock] = w;
    unlock();
}

// del context
void forward_runner::del_context(forward_context_internal *context)
    throw (internal_exception &)
{
    sock_t sock = context->socket();

    // try remove event in ws_
    {
        lock();
        std::map<sock_t, shared_ptr<internal_ev_io> >::iterator it =
            ws_.find(sock);
        if (it != ws_.end())
        {
            del_watcher(it->second);
            ws_.erase(it);
        }
        unlock();
    }
}

void forward_runner::add_watcher(shared_ptr<internal_ev_io> w)
{
    pthread_mutex_lock(&ev_event_mutex_);
    ev_once (loop_, 0, EV_READ, 0, 
        forward_runner::add_watcher_callback, (void *)w.get());

    // wait add finish
    pthread_cond_wait(&ev_event_cond_, &ev_event_mutex_);
    pthread_mutex_unlock(&ev_event_mutex_);
}

void forward_runner::del_watcher(shared_ptr<internal_ev_io> w)
{
    pthread_mutex_lock(&ev_event_mutex_);
    ev_once (loop_, 0, EV_READ, 0, 
        forward_runner::del_watcher_callback, (void *)w.get());

    // wait delete finish
    pthread_cond_wait(&ev_event_cond_, &ev_event_mutex_);
    pthread_mutex_unlock(&ev_event_mutex_);
}

// c exports
forward_context *init_forward(sock_t udpsocket)
{
    shared_ptr<forward_context_internal> internal_context(
        new forward_context_internal(udpsocket, forward_runner::instance())
    );

    forward_runner::instance().add_context(internal_context);

    return &internal_context->context();
}


void free_forward(forward_context *context)
{
    forward_context_internal *internal_context = 
        (forward_context_internal *) context->internal;

    forward_runner::instance().del_context(internal_context);
}


forward_rule *add_forward_rule (forward_context *context, 
                                forward_mode mode, 
                                const char *service,
                                unsigned short port)
{
    if ((NULL == context) || (NULL == service) || (0 == port)) 
    {
        return NULL;
    }

    forward_context_internal *internal_context = 
        (forward_context_internal *)context->internal;

    shared_ptr<forward_rule_internal> internal_rule(
        new forward_rule_internal(*context, mode, service, port)
    );

    internal_context->add_rule(internal_rule);


    return NULL;
}

void del_forward_rule (forward_rule *rule)
{
    forward_context_internal *internal_context = 
        (forward_context_internal *)rule->context->internal;

    internal_context->del_rule(rule->service);
}

// log callback function
void set_log_callback(void (*cb)(int level, const char *message))
{
    log_callback_func_ = cb;
}
