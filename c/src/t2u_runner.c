#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <event2/event.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <event2/event.h>
#include <fcntl.h>

#include "t2u.h"
#include "t2u_internal.h"
#include "t2u_thread.h"
#include "t2u_rbtree.h"
#include "t2u_session.h"
#include "t2u_rule.h"
#include "t2u_context.h"
#include "t2u_runner.h"

/* runner proc */
#if defined __GNUC__
    static void* t2u_runner_loop_(void *arg);
#elif defined _MSC_VER
    static DWORD __stdcall t2u_runner_loop_(void * arg);
#else
    #error "Compiler not support."
#endif

static void t2u_runner_control_callback(evutil_socket_t sock, short events, void *arg);

static void t2u_runner_process_udp_callback(evutil_socket_t sock, short events, void *arg);
static void t2u_runner_process_tcp_callback(evutil_socket_t sock, short events, void *arg);

static void t2u_runner_process_accept_callback(evutil_socket_t sock, short events, void *arg);
static void t2u_runner_process_accept_timeout_callback(evutil_socket_t sock, short events, void *arg);

static void t2u_runner_process_connect_timeout_callback(evutil_socket_t sock, short events, void *arg);
static void t2u_runner_process_connect_success_callback(evutil_socket_t sock, short events, void *arg);

static void t2u_runner_process_send_timeout_callback(evutil_socket_t sock, short events, void *arg);



/* some finder */
static t2u_event_data *find_evdata_by_context(rbtree_node *node, t2u_context *context)
{
    if (node)
    {
        t2u_event_data *ret = NULL;
        ret = find_evdata_by_context(node->left, context);
        if (!ret)
        {
            t2u_event_data *ev = (t2u_event_data *)node->data;
            if (ev->context_ == context)
            {
                ret = ev;
            }    
        }
        if (!ret)
        {
            ret = find_evdata_by_context(node->right, context);
        }

        return ret;
    }

    return NULL;
}

static t2u_event_data *find_evdata_by_rule(rbtree_node *node, t2u_rule *rule)
{
    if (node)
    {
        t2u_event_data *ret = NULL;
        ret = find_evdata_by_rule(node->left, rule);
        if (!ret)
        {
            t2u_event_data *ev = (t2u_event_data *)node->data;
            if (ev->rule_ == rule)
            {
                ret = ev;
            }    
        }
        if (!ret)
        {
            ret = find_evdata_by_rule(node->right, rule);
        }

        return ret;
    }

    return NULL;
}

static t2u_event_data *find_evdata_by_session(rbtree_node *node, t2u_session *session)
{
    if (node)
    {
        t2u_event_data *ret = NULL;
        ret = find_evdata_by_session(node->left, session);
        if (!ret)
        {
            t2u_event_data *ev = (t2u_event_data *)node->data;
            if (ev->session_ == session)
            {
                ret = ev;
            }    
        }
        if (!ret)
        {
            ret = find_evdata_by_session(node->right, session);
        }

        return ret;
    }

    return NULL;
}



#define MAX_CONTROL_BUFF_LEN (1600)

static void t2u_runner_control_process(t2u_runner *runner, control_data *cdata)
{
    (void) runner;
    assert (NULL != cdata->func_);
    cdata->func_(cdata->arg_);
}

static void t2u_runner_control_callback(evutil_socket_t sock, short events, void *arg)
{
    (void) events;

    t2u_runner *runner = (t2u_runner *)arg;
    control_data cdata;
    size_t len = 0;
    struct sockaddr_in addr_c;
    socklen_t addrlen = sizeof(addr_c);
    assert(t2u_thr_self() == runner->tid_);

    len = recvfrom(sock, &cdata, sizeof(cdata), 0, (struct sockaddr *) &addr_c, &addrlen);
    if (len <= 0)
    {
        /* todo: error */
    }

    t2u_runner_control_process(runner, &cdata);

    /* send back message. */
    sendto(sock, &cdata, sizeof(cdata), 0, (const struct sockaddr *)&addr_c, sizeof(addr_c));

}


void t2u_runner_control(t2u_runner *runner, control_data *cdata)
{
    if (t2u_thr_self() == runner->tid_)
    {
        t2u_runner_control_process(runner, cdata);
    }
    else
    {
        int len; 
        t2u_mutex_lock(&runner->mutex_);
        
        send(runner->sock_[1], cdata, sizeof(control_data), 0);
        len = recv(runner->sock_[1], cdata, sizeof(control_data), 0);

        if (len > 0)
        {
        }
        else
        {
            /* todo error handle */
        }

        t2u_mutex_unlock(&runner->mutex_);
    }
}

/* process new udp message in */
static void t2u_runner_process_udp_callback(evutil_socket_t sock, short events, void *arg)
{
    ssize_t recv_bytes;
    char *buff = (char *) malloc(T2U_MESS_BUFFER_MAX);
    t2u_message *message;
    t2u_event_data *ev = (t2u_event_data *)arg;
    t2u_runner *runner = ev->runner_;
    t2u_context *context = ev->context_;

    (void) events;
    assert(NULL != buff);

    recv_bytes = recv(sock, buff, T2U_MESS_BUFFER_MAX, 0);
    if (recv_bytes == -1)
    {
        /* error on context's udp socket */
        LOG_(3, "recv from udp socket failed, context: %p", context);
        free(buff);
        return;
    }

    message = (t2u_message *)(void *)buff;
    if (((int)recv_bytes < (int)sizeof(t2u_message)) ||
        (message->magic_ != htonl(T2U_MESS_MAGIC)) ||
        (message->version_ != htons(0x0001)))
    {
        /* unknown packet */
        LOG_(2, "recv unknown packet from context: %p", context);
        free(buff);
        return;
    }

    switch (ntohs(message->oper_))
    {
    case connect_request:
        {
            char *service_name = message->payload;
            uint32_t pair_handle = ntohl(message->handle_);

            /* check the pair_handle is already in use */
            t2u_session *oldsession = t2u_session_by_pair_handle(pair_handle);
            if (oldsession)
            {
                LOG_(2, "connection with remote handle is already exist.");
                
                /* close old first */
                del_forward_session(oldsession);
            }

            t2u_rule *rule = t2u_context_find_rule(context, service_name);

            if (rule)
            {
                /* got rule */
                sock_t s = socket(AF_INET, SOCK_STREAM, 0);
                if (-1 == s)
                {
                    LOG_(3, "create socket failed");
                    free(buff);
                    return;
                }

                /* unblocking */
                int flags = fcntl(s, F_GETFL, 0);
                fcntl(s, F_SETFL, flags|O_NONBLOCK);

                /* connect, async */
                int ret = connect(s, (struct sockaddr *)&rule->conn_addr_, sizeof(rule->conn_addr_));
                if ((ret == -1) && (errno != EINPROGRESS))
                {
                    LOG_(3, "connect socket failed");
                    closesocket(s);
                    free(buff);
                    return;
                }

                /* new session */
                t2u_session *session = t2u_session_new(rule, s);
                t2u_session_assign_remote_handle(session, pair_handle);

                session->status_ = 1;

                /* add events, timer for connect timeout, EVWRITE for connect ok */
                t2u_event_data *ev = (t2u_event_data *) malloc(sizeof(t2u_event_data));
                assert(NULL != ev);

                ev->runner_ = runner;
                ev->context_ = context;
                ev->rule_ = rule;
                ev->session_ = session;
                ev->sock_ = s;
                ev->message_ = buff;

                ev->event_ = evtimer_new(runner->base_, t2u_runner_process_connect_timeout_callback, ev);
                assert (NULL != ev->event_);

                ev->extra_event_ = event_new(runner->base_, s, EV_WRITE, t2u_runner_process_connect_success_callback, ev);
                assert (NULL != ev->event_);

                struct timeval t;
                t.tv_sec = (context->utimeout_ * context->uretries_) / 1000;
                t.tv_usec = ((context->utimeout_ * context->uretries_) % 1000) * 1000;

                event_add(ev->event_, &t);
                event_add(ev->extra_event_, NULL);
            }
        }
        break;
    case connect_response:
        {
            uint32_t pair_handle = 0;
            uint32_t *phandle = (void *)(message->payload);
            pair_handle = ntohl(*phandle);

            t2u_session *session = t2u_session_by_handle(ntohl(message->handle_));
            if (session)
            {
                t2u_rule *rule = session->rule_;
                t2u_context *context = rule->context_;
                t2u_runner *runner = context->runner_;

                /* clear the timeout callback */
                if (session->timeout_ev.event_)
                {
                    event_del(session->timeout_ev.event_);
                    free(session->timeout_ev.event_);
                    session->timeout_ev.event_ = NULL;
                }

                /* confirm the data */
                if (session->mess_.data_)
                {
                    free(session->mess_.data_);
                    session->mess_.data_= NULL;
                    session->mess_.len_ = 0;
                }

                /* success */
                if (pair_handle > 0)
                {
                    session->status_ = 2;
                    t2u_session_assign_remote_handle(session, pair_handle);
                    t2u_rule_add_session(rule, session);
                    t2u_runner_add_session(runner, session);
                }
                else
                {
                    /* failed */
                    t2u_session_delete(session);
                }

            }
            else
            {
                /* no such session, drop it */
            }

            free(buff);
        }
        break;
    case data_request:
        {
            char *payload = message->payload;
            size_t payload_len = recv_bytes - sizeof(t2u_message);
            uint32_t pair_handle = ntohl(message->handle_);

            /* check the pair_handle is already in use */
            t2u_session *session = t2u_session_by_pair_handle(pair_handle);
            if (session)
            {
                /* session find, forward the data */
                ssize_t sent_bytes = send(session->sock_, payload, payload_len, 0);

                if ((int)sent_bytes < (int)payload_len)
                {
                    /* error: send failed */
                    LOG_(3, "send failed sent_bytes(%lu) < payload_len(%lu). ",
                        (unsigned long) sent_bytes, (unsigned long) payload_len);
                    t2u_session_send_u_data_response(session, buff, 1);
                }
                else
                {
                    t2u_session_send_u_data_response(session, buff, 0);
                }
            }
            else
            {
                LOG_(3, "no such session with pair handle: %lu", (unsigned long) pair_handle);
                /* no such session */
                uint32_t *phandle = (void *)(message->payload);
                *phandle = htonl(2);
                message->oper_ = htons(data_response);

                /* send response */
                send(sock, message, sizeof(t2u_message) + sizeof(uint32_t), 0);
            }

            free(buff);
        }
        break;
    case data_response:
        {
            uint32_t *perror = (void *)message->payload;
            uint32_t error = ntohl(*perror);
            t2u_session *session = t2u_session_by_handle(ntohl(message->handle_));
            if (session)
            {
                /* clear the timeout callback */
                if (session->timeout_ev.event_)
                {
                    event_del(session->timeout_ev.event_);
                    free(session->timeout_ev.event_);
                    session->timeout_ev.event_ = NULL;
                }

                /* confirm the data */
                if (session->mess_.data_)
                {
                    free(session->mess_.data_);
                    session->mess_.data_= NULL;
                    session->mess_.len_ = 0;
                }

                /* success */
                if (error == 0)
                {
                    /* nothing to do */
                    /* add disable event */
                    if (session->disable_event_)
                    {
                        LOG_(1, "data confirmed, add event for session back: %p", session);
                        event_add(session->disable_event_, NULL);
                        session->disable_event_ = NULL;
                    }
                }
                else
                {
                    /* failed */
                    del_forward_session(session);
                }
            }
            else
            {
                LOG_(3, "no such session with handle: %lu", (unsigned long)ntohl(message->handle_));
                /* no such session, drop it */
            }

            free(buff);
        }
        break;
    default:
        {
            /* unknown packet */
            LOG_(2, "recv unknown packet from context: %p, type: %d", context, ntohs(message->oper_));
            free(buff);
            return;
        }
    }

}

static void t2u_runner_process_tcp_callback(evutil_socket_t sock, short events, void *arg)
{
    t2u_event_data *ev = (t2u_event_data *)arg;
    t2u_runner *runner = ev->runner_;
    t2u_context *context = ev->context_;
    t2u_rule *rule = ev->rule_;
    t2u_session *session = ev->session_;
    char *buff = NULL;
    (void)events;

    /* check session is ready for sent */
    if (session->mess_.data_)
    {
        LOG_(1, "data not confirmed, disable event for session: %p", session);
        /* data is not confirmed, disable the event */
        event_del(ev->event_);
        session->disable_event_ = ev->event_;
        return;
    }

    buff = (char *)malloc(T2U_PAYLOAD_MAX);
    assert(NULL != buff);

    ssize_t read_bytes = recv(sock, buff, T2U_PAYLOAD_MAX, 0);

    if (read_bytes <= 0)
    {
        LOG_(3, "recv on session:%p failed", session);
        
        /* error */
        free(buff);

        /* close session */
        del_forward_session(session);
        return;
    }

    /* add a timeout callback for resent data */
    if (session->timeout_ev.event_)
    {
        event_del(session->timeout_ev.event_);
        free(session->timeout_ev.event_);
        session->timeout_ev.event_ = NULL;
    }

    struct timeval t;
    t2u_event_data *nev = &session->timeout_ev;

    nev->runner_ = runner;
    nev->context_ = context;
    nev->rule_ = rule;
    nev->session_ = session;
    nev->sock_ = sock;
    nev->event_ = evtimer_new(runner->base_, t2u_runner_process_send_timeout_callback, nev);
    
    assert (NULL != nev->event_);
    t.tv_sec = context->utimeout_ / 1000;
    t.tv_usec = (context->utimeout_ % 1000) * 1000;
    event_add(nev->event_, &t);

    session->send_retries_ = 0;
    t2u_session_send_u_data(session, buff, read_bytes);

    free(buff);
    return;
}

static void t2u_runner_process_connect_timeout_callback(evutil_socket_t sock, short events, void *arg)
{
    t2u_event_data *ev = (t2u_event_data *)arg;
    /* t2u_runner *runner = ev->runner_; */
    /* t2u_context *context = ev->context_; */
    /* t2u_rule *rule = ev->rule_; */
    t2u_session *session = ev->session_;

    (void)sock;
    (void)events;

    if (session->status_ != 2)
    {
        /* not connect */
        t2u_session_delete(session);
    }

    /* cleanup, delete events and arg */
    event_del(ev->extra_event_);

    free(ev->extra_event_);
    free(ev->event_);
    free(ev->message_);
    free(ev);
}

static void t2u_runner_process_connect_success_callback(evutil_socket_t sock, short events, void *arg)
{
    int error = 0;
    size_t len = sizeof(int);
    t2u_event_data *ev = (t2u_event_data *)arg;
    t2u_runner *runner = ev->runner_;
    t2u_rule *rule = ev->rule_;
    t2u_session *session = ev->session_;

    (void)events;

    getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);

    if (0 == error)
    {
        LOG_(1, "connect for session: %lu success.", (unsigned long)session->handle_);
        session->status_ = 2;

        t2u_rule_add_session(rule, session);
        t2u_runner_add_session(runner, session);

        /* post success */
        t2u_session_send_u_connect_response(session, ev->message_);
    }

    /* cleanup, delete events and arg */
    event_del(ev->event_);

    free(ev->extra_event_);
    free(ev->event_);
    free(ev->message_);
    free(ev);

}


static void t2u_runner_process_accept_callback(evutil_socket_t sock, short events, void *arg)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr); 
    t2u_event_data *ev = (t2u_event_data *)arg;
    t2u_rule *rule = (t2u_rule *)ev->rule_;
    t2u_context *context = (t2u_context *)rule->context_;
    t2u_runner *runner = (t2u_runner *)context->runner_;

    (void)sock;
    (void)events;
    
    sock_t s = accept(rule->listen_sock_, (struct sockaddr *)&client_addr, &client_len);
    if (-1 == s)
    {
        return;
    }

    /* nonblock */
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags|O_NONBLOCK);

    /* new session, this rule must be client mode. */
    t2u_session *session = t2u_session_new(rule, s);
    assert(NULL != session);

    /* session is created, create a timeout timer for remove the session */
    /* send message to remote for connecting */

    struct timeval t;
    t2u_event_data *nev = &session->timeout_ev;

    nev->runner_ = runner;
    nev->context_ = context;
    nev->rule_ = rule;
    nev->session_ = session;
    nev->sock_ = s;
    nev->event_ = evtimer_new(runner->base_, t2u_runner_process_accept_timeout_callback, nev);
    assert (NULL != nev->event_);
    t.tv_sec = context->utimeout_ / 1000;
    t.tv_usec = (context->utimeout_ % 1000) * 1000;
    event_add(nev->event_, &t);

    /* send the udp message */
    t2u_session_send_u_connect(session);
}


static void t2u_runner_process_accept_timeout_callback(evutil_socket_t sock, short events, void *arg)
{
    t2u_event_data *nev = (t2u_event_data *) arg;
    t2u_session * session = nev->session_;
    t2u_context * context = nev->context_;

    (void)sock;
    (void)events;

    if (++session->send_retries_ >= context->uretries_)
    {
        LOG_(2, "timeout for accept new connection, session: %lu, retry: %lu, delay: %lums",
            (unsigned long)session->handle_, context->uretries_, context->utimeout_);
        
        /* timeout */
        free(nev->event_);
        nev->event_ = NULL;

        t2u_session_delete(session);
    }
    else
    {
        /* readd the timer */
        struct timeval t;
        t.tv_sec = context->utimeout_ / 1000;
        t.tv_usec = (context->utimeout_ % 1000) * 1000;
        event_add(nev->event_, &t);

        /* send again */
        t2u_session_send_u(session);
    }
}

static void t2u_runner_process_send_timeout_callback(evutil_socket_t sock, short events, void *arg)
{
    t2u_event_data *nev = (t2u_event_data *) arg;
    t2u_session * session = nev->session_;
    t2u_context * context = nev->context_;

    (void)sock;
    (void)events;


    if (++session->send_retries_ >= context->uretries_)
    {
        LOG_(2, "timeout for send data, session: %lu, retry: %lu, delay: %lums",
            (unsigned long)session->handle_, context->uretries_, context->utimeout_);
        
        /* timeout */
        free(nev->event_);
        nev->event_ = NULL;

        /* delete session cimplete */
        del_forward_session(session);
    }
    else
    {
        /* readd the timer */
        struct timeval t;
        t.tv_sec = context->utimeout_ / 1000;
        t.tv_usec = (context->utimeout_ % 1000) * 1000;

        event_add(nev->event_, &t);

        /* send again */
        t2u_session_send_u(session);
    }
}

#define CONTROL_PORT_START (50505)
#define CONTROL_PORT_END   (50605)
/* runner init */
t2u_runner * t2u_runner_new()
{
    int ret = 0;
    struct sockaddr_in addr_c;
    unsigned short listen_port = 0;

    t2u_runner *runner = (t2u_runner *) malloc(sizeof(t2u_runner));
    assert(runner != NULL);

    /* alloc event base. */
    runner->base_ = event_base_new();
    assert(runner->base_ != NULL);
        
    /* alloc events map. */
    runner->event_tree_ = rbtree_init(NULL);
    assert(runner->event_tree_ != NULL);


    t2u_mutex_init(&runner->mutex_);
    t2u_cond_init(&runner->cond_);

    runner->running_ = 0; /* not running */
    runner->tid_ = 0;

    /* control message */
    runner->sock_[0] = socket(AF_INET, SOCK_DGRAM, 0);
    assert(runner->sock_[0] > 0);

    for (listen_port = CONTROL_PORT_START; listen_port < CONTROL_PORT_END; listen_port ++)
    {
        addr_c.sin_family = AF_INET;
        addr_c.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr_c.sin_port = htons(listen_port);

        if (bind(runner->sock_[0], (struct sockaddr *)&addr_c, sizeof(addr_c)) == -1)
        {
            LOG_(0, "socket bind failed. %s\n", strerror(errno));
        }
        else
        {
            LOG_(0, "socket bind ok on port: %u.\n", listen_port);
            break;
        }   
    }
    assert(listen_port != CONTROL_PORT_END);

    /* now connect local control server */
    runner->sock_[1] = socket(AF_INET, SOCK_DGRAM, 0);
    assert(runner->sock_[1] > 0);
    
    ret = connect(runner->sock_[1], (struct sockaddr *)&addr_c, sizeof(addr_c));
    assert(0 == ret);

    /* the event handler for control message processing. */
    runner->event_ = event_new(runner->base_, runner->sock_[0], EV_READ|EV_PERSIST, t2u_runner_control_callback, runner);
    assert(NULL != runner->event_);

    ret = event_add(runner->event_, NULL);
    assert(0 == ret);

    LOG_(0, "create new runner: %p", (void *)runner);

    /* run the runner */
    t2u_mutex_lock(&runner->mutex_);
    runner->running_ = 1;
    t2u_thr_create(&runner->thread_, t2u_runner_loop_, (void *)runner);
    t2u_cond_wait(&runner->cond_, &runner->mutex_);
    t2u_mutex_unlock(&runner->mutex_);

    return runner;
}

static void runner_delete_cb_(void *arg)
{
    t2u_runner *runner = (t2u_runner *)arg;

    /* remove self event */
    event_del(runner->event_);
    free(runner->event_);
}
    
/* destroy the runner */
void t2u_runner_delete(t2u_runner *runner)
{
    if (!runner)
    {
        return;
    }

    /* makesure stop first */
    if (runner->running_)
    {
        /* stop all event */
        runner->running_ = 0;
        
        /* post stop control message. */
        control_data cdata;
        memset(&cdata, 0, sizeof(cdata));

        cdata.func_ = runner_delete_cb_;
        cdata.arg_ = runner;

        t2u_runner_control(runner, &cdata);

        t2u_thr_join(runner->thread_);
    }

    /* cleanup */
    closesocket(runner->sock_[0]);
    closesocket(runner->sock_[1]);  
    free(runner->event_tree_);
    event_base_free(runner->base_);

    LOG_(0, "delete the runner: %p", (void *)runner);

    /* last cleanup */
    free(runner);
}


struct runner_and_context_
{
    t2u_runner *runner;
    t2u_context *context;
};

static void runner_add_context_cb_(void *arg)
{
    struct runner_and_context_ *rnc = (struct runner_and_context_ *)arg;
    t2u_runner *runner = rnc->runner;
    t2u_context *context = rnc->context;

    t2u_event_data *ev = (t2u_event_data *) malloc(sizeof(t2u_event_data));
    assert(NULL != ev);

    memset(ev, 0, sizeof(t2u_event_data));
    ev->runner_ = runner;
    ev->context_ = context;

    ev->event_ = event_new(runner->base_, context->sock_, 
        EV_READ|EV_PERSIST, t2u_runner_process_udp_callback, ev);
    assert(NULL != ev->event_);

    event_add(ev->event_, NULL);
    rbtree_insert(runner->event_tree_, ev->event_, ev);


    ((t2u_context *)ev->context_)->runner_ = runner;
    LOG_(1, "ADD_CONTEXT runner: %p context: %p", runner, context);
}

/* add context */
int t2u_runner_add_context(t2u_runner *runner, t2u_context *context)
{
    control_data cdata;
    struct runner_and_context_ rnc;

    memset(&cdata, 0, sizeof(cdata));

    cdata.func_ = runner_add_context_cb_;
    cdata.arg_ = &rnc;

    rnc.runner = runner;
    rnc.context = context;

    t2u_runner_control(runner, &cdata);
    return 0;
}


static void runner_delete_context_cb_(void *arg)
{
    struct runner_and_context_ *rnc = (struct runner_and_context_ *)arg;
    t2u_runner *runner = rnc->runner;
    t2u_context *context = rnc->context;

    t2u_event_data *ev = find_evdata_by_context(runner->event_tree_->root, context);
    assert(NULL != ev);

    rbtree_remove(runner->event_tree_, ev->event_);
    event_del(ev->event_);
    free(ev->event_);
    free (ev);

    LOG_(1, "DEL_CONTEXT runner: %p context: %p", runner, context);
}

/* delete context */
int t2u_runner_delete_context(t2u_runner *runner, t2u_context *context)
{
    control_data cdata;
    struct runner_and_context_ rnc;

    memset(&cdata, 0, sizeof(cdata));

    cdata.func_ = runner_delete_context_cb_;
    cdata.arg_ = &rnc;

    rnc.runner = runner;
    rnc.context = context;

    t2u_runner_control(runner, &cdata);
    return 0;
}

struct runner_and_rule_
{
    t2u_runner *runner;
    t2u_rule *rule;
};

void runner_add_rule_cb_(void *arg)
{
    struct runner_and_rule_ *rnr = (struct runner_and_rule_ *)arg;
    t2u_runner *runner = rnr->runner;
    t2u_rule *rule = rnr->rule;

    t2u_event_data *ev = (t2u_event_data *) malloc(sizeof(t2u_event_data));
    assert(NULL != ev);

    memset(ev, 0, sizeof(t2u_event_data));
    ev->context_ = rule->context_ ;
    ev->rule_ = rule;
    ev->runner_ = runner;

    ev->event_ = event_new(runner->base_, rule->listen_sock_, 
        EV_READ|EV_PERSIST, t2u_runner_process_accept_callback, ev);
    assert(NULL != ev->event_);

    event_add(ev->event_, NULL);
    rbtree_insert(runner->event_tree_, ev->event_, ev);

    LOG_(1, "ADD_RULE runner: %p rule: %p", runner, rule);
}

/* add rule */
int t2u_runner_add_rule(t2u_runner *runner, t2u_rule *rule)
{
    if (rule->mode_ == forward_client_mode)
    {
        control_data cdata;
        memset(&cdata, 0, sizeof(cdata));

        struct runner_and_rule_ rnr;
        rnr.rule = rule;
        rnr.runner = runner;

        cdata.func_ = runner_add_rule_cb_;
        cdata.arg_ = &rnr;

        t2u_runner_control(runner, &cdata);
    }
    return 0;
}

void runner_delete_rule_cb_(void *arg)
{
    struct runner_and_rule_ *rnr = (struct runner_and_rule_ *)arg;
    t2u_runner *runner = rnr->runner;
    t2u_rule *rule = rnr->rule;

    t2u_event_data *ev = find_evdata_by_rule(runner->event_tree_->root, rule);
    assert(NULL != ev);

    rbtree_remove(runner->event_tree_, ev->event_);
    event_del(ev->event_);
    free(ev->event_);
    free (ev);

    LOG_(1, "DEL_RULE runner: %p rule: %p", runner, rule);
}

/* delete rule */
int t2u_runner_delete_rule(t2u_runner *runner, t2u_rule *rule)
{
    if (rule->mode_ == forward_client_mode)
    {    
        control_data cdata;
        memset(&cdata, 0, sizeof(cdata));

        struct runner_and_rule_ rnr;
        rnr.rule = rule;
        rnr.runner = runner;

        cdata.func_ = runner_delete_rule_cb_;
        cdata.arg_ = &rnr;

        t2u_runner_control(runner, &cdata);
    }
    return 0;
}

struct runner_and_session_
{
    t2u_runner *runner;
    t2u_session *session;
};

void runner_add_session_cb_(void *arg)
{
    struct runner_and_session_ *rns = (struct runner_and_session_ *)arg;
    t2u_runner *runner = rns->runner;
    t2u_session *session = rns->session;
    t2u_rule *rule = session->rule_;
    t2u_context *context = rule->context_;


    t2u_event_data *ev = (t2u_event_data *) malloc(sizeof(t2u_event_data));
    assert(NULL != ev);

    memset(ev, 0, sizeof(t2u_event_data));
    
    ev->session_ = session;
    ev->rule_ = rule;
    ev->context_ = context;
    ev->runner_ = runner;

    ev->event_ = event_new(runner->base_, session->sock_, 
    EV_READ|EV_PERSIST, t2u_runner_process_tcp_callback, ev);
    assert(NULL != ev->event_);

    event_add(ev->event_, NULL);
    rbtree_insert(runner->event_tree_, ev->event_, ev);

    LOG_(1, "ADD_SESSION runner: %p session: %p, sock: %d", runner, session, (int)session->sock_);
}

/* add session */
int t2u_runner_add_session(t2u_runner *runner, t2u_session *session)
{
    control_data cdata;
    memset(&cdata, 0, sizeof(cdata));

    struct runner_and_session_ rns;
    rns.runner = runner;
    rns.session = session;

    cdata.func_ = runner_add_session_cb_;
    cdata.arg_ = &rns;

    t2u_runner_control(runner, &cdata);
    
    return 0;
}

void runner_delete_session_cb_(void *arg)
{
    struct runner_and_session_ *rns = (struct runner_and_session_ *)arg;
    t2u_runner *runner = rns->runner;
    t2u_session *session = rns->session;

    t2u_event_data *ev = find_evdata_by_session(runner->event_tree_->root, session);
    if (ev)
    {
        rbtree_remove(runner->event_tree_, ev->event_);
        event_del(ev->event_);
        free(ev->event_);
        free (ev);
    }

    LOG_(1, "DEL_RULE runner: %p session: %p", runner, session);
}

/* delete session */
int t2u_runner_delete_session(t2u_runner *runner, t2u_session *session)
{
    control_data cdata;
    memset(&cdata, 0, sizeof(cdata));

    struct runner_and_session_ rns;
    rns.runner = runner;
    rns.session = session;

    cdata.func_ = runner_delete_session_cb_;
    cdata.arg_ = &rns;

    t2u_runner_control(runner, &cdata);
    
    return 0;
}


/* runner proc */
#if defined __GNUC__
    static void* t2u_runner_loop_(void *arg)
#elif defined _MSC_VER
    static DWORD __stdcall t2u_runner_loop_arg(void * arg)
#endif
{
    t2u_runner *runner = (t2u_runner *)arg;
    runner->tid_ = t2u_thr_self();

    t2u_mutex_lock(&runner->mutex_);
    t2u_cond_signal(&runner->cond_);
    t2u_mutex_unlock(&runner->mutex_);

    LOG_(0, "enter run loop for runner: %p", (void *)runner);
    
    /*  run loop */
    event_base_dispatch(runner->base_);

    LOG_(0, "end run loop for runner: %p", (void *)runner);
#if defined __GNUC__
    return NULL;
#elif defined _MSC_VER
    return 0;
#endif
}