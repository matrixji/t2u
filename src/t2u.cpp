#include "t2u.h"
#include "t2u_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
 #include <arpa/inet.h>

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


forward_session::forward_session(sock_t sock, forward_rule_internal &rule):
    sock_(sock), rule_(rule)
{

}

forward_session::~forward_session()
{
    // remove self from rule & watcher
    forward_context_internal *internal_context = (forward_context_internal *)(rule_.rule().context->internal);
    forward_runner &runner = internal_context->runner();

    // del from watchers
    runner.del_watcher(sock_);

    // del from rule
    rule_.del_session(sock_);
}

void forward_session::handle_tcp_input_callback(struct ev_loop* reactor, ev_io* w, int events)
{

}


// constructor
forward_rule_internal::forward_rule_internal(
    forward_context &context, 
    forward_mode mode, 
    const char *service,
    unsigned short port):
    internal_object(), 
    listen_sock_(0),
    service_name_(service),
    rule_({mode, &context, service_name_.c_str(), port})
{
    LOG_(0, "new rule created, mode: %d, service: %s, port: %u.", mode, service, port);
}

// destructor
forward_rule_internal::~forward_rule_internal()
{
    // remove the watcher
    if (forward_client_mode == rule_.mode && listen_sock_ > 0)
    {
        forward_context_internal *internal_context_ = (forward_context_internal *)rule_.context->internal;
        internal_context_->runner().del_watcher(listen_sock_);
    }

    LOG_(0, "delete rule mode: %d, service: %s, port: %u.", rule_.mode, rule_.service, rule_.port);
}

// init, listen if client mode.
void forward_rule_internal::init() throw (internal_exception &)
{
    // if in client mode, we need listen on lo
    if (rule_.mode == forward_client_mode)
    {
        listen_sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (-1 == listen_sock_)
        {
            std::string mess("create socket failed");
            throw internal_exception(mess);
        }

        // listen the socket
        struct sockaddr_in listen_addr;
        listen_addr.sin_port = ntohs(rule_.port);
        listen_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (-1 == bind(listen_sock_, (struct sockaddr *)&listen_addr, sizeof(listen_addr)))
        {
            closesocket(listen_sock_);
            listen_sock_ = 0;
            std::string mess("bind socket failed");
            throw internal_exception(mess);
        }

        if (-1 == listen(listen_sock_, 256))
        {
            closesocket(listen_sock_);
            listen_sock_ = 0;
            std::string mess("listen socket failed");
            throw internal_exception(mess);
        }

        // if have listen socket, add it to ev's watcher
        forward_context_internal *internal_context = (forward_context_internal *) rule_.context->internal;
        shared_ptr<internal_ev_io> w(new internal_ev_io);
        ev_io_init((ev_io *)w.get(), &forward_rule_internal::handle_tcp_connect_callback, listen_sock_, EV_READ);
        w->socket = listen_sock_;
        w->runner = &internal_context->runner();
        w->context = internal_context;
        w->rule = this;

        internal_context->runner().add_watcher(w);
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

// return listen socket if exist.
sock_t forward_rule_internal::listen_socket()
{
    return listen_sock_;
}

// add session.
void forward_rule_internal::add_session(shared_ptr<forward_session> session)
{
    lock();
    sessions_[session->socket()] = session;
    unlock();
}

// del session.
void forward_rule_internal::del_session(sock_t sock)
{
    lock();
    sessions_.erase(sock);
    unlock();
}

void forward_rule_internal::handle_tcp_connect_callback(struct ev_loop* reactor, ev_io* w, int events)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr); 
    internal_ev_io *iw = (internal_ev_io *)w;

    shared_ptr<internal_ev_io> nw(new internal_ev_io);

    nw->socket = accept(iw->rule->listen_sock_, (struct sockaddr *)&client_addr, &client_len);
    if (-1 == nw->socket)
    {
        return;
    }

    shared_ptr<forward_session> session(new forward_session(nw->socket, *(nw->rule)));
    nw->runner = iw->runner;
    nw->context = iw->context;
    nw->rule = iw->rule;
    nw->session = session.get();

    // add to map
    nw->rule->add_session(session);

    ev_io_init((ev_io *)nw.get(), &forward_session::handle_tcp_input_callback, nw->socket, EV_READ);

    // add watcher
    nw->runner->add_watcher(nw);
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

    // delete the watcher.
    runner().del_watcher(sock_);

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
    // add the udp socket to ev's watcher
    shared_ptr<internal_ev_io> w(new internal_ev_io());
    ev_io_init((ev_io *)w.get(), &forward_context_internal::handle_udp_input_callback, sock_, EV_READ);
    w->socket = sock_;
    w->runner = &runner();
    w->context = this;
    w->rule = NULL;

    runner().add_watcher(w);
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

    rule->init(); // if failed throw.

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


// handle the udp packet. 
void forward_context_internal::handle_udp_input_callback(struct ev_loop* reactor, ev_io* w, int events)
{
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
        runner->ws_.erase(it++);
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
        std::map<sock_t, shared_ptr<forward_context_internal> >::iterator it = cs_.find(sock);
        if (it != cs_.end())
        {
            unlock();
            std::string mess = std::string("context with the socket is already exist");
            throw internal_exception(mess);
            return;
        }
    }

    //
    context->init(); // if failed will throw.

    // add to map
    cs_[sock] = context;
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

    // add to map
    ws_[w->socket] = w;
    pthread_mutex_unlock(&ev_event_mutex_);
}

void forward_runner::del_watcher(shared_ptr<internal_ev_io> w)
{
    pthread_mutex_lock(&ev_event_mutex_);
    ev_once (loop_, 0, EV_READ, 0, 
        forward_runner::del_watcher_callback, (void *)w.get());

    // wait delete finish
    pthread_cond_wait(&ev_event_cond_, &ev_event_mutex_);

    // del from map
    ws_.erase(w->socket);
    pthread_mutex_unlock(&ev_event_mutex_);
}

void forward_runner::del_watcher(sock_t sock)
{
    pthread_mutex_lock(&ev_event_mutex_);
    std::map<sock_t, shared_ptr<internal_ev_io> >::iterator it = ws_.find(sock);
    if (ws_.end() == it)
    {
        // not exist,
        pthread_mutex_unlock(&ev_event_mutex_);
        return;
    }

    shared_ptr<internal_ev_io> w = it->second;
    ev_once (loop_, 0, EV_READ, 0, 
        forward_runner::del_watcher_callback, (void *)w.get());

    // wait delete finish
    pthread_cond_wait(&ev_event_cond_, &ev_event_mutex_);

    // del from map
    ws_.erase(w->socket);
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
