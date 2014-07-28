#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <event2/event.h>
#include <string.h>
#include <errno.h>

#if defined __GNUC__
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#endif

#include "t2u.h"
#include "t2u_internal.h"


static rbtree *g_session_tree = NULL;
static rbtree *g_session_tree_remote = NULL;
static unsigned long g_session_count = 0;

static int compare_uint32_ptr(void *a, void *b)
{
    uint32_t *sa = (uint32_t *)a;
    uint32_t *sb = (uint32_t *)b;

    return ((*sa) - (*sb));
}


static void process_tcp_cb_(evutil_socket_t sock, short events, void *arg)
{
    t2u_event *ev = (t2u_event *)arg;
    t2u_runner *runner = ev->runner_;
    t2u_context *context = ev->context_;
    t2u_rule *rule = ev->rule_;
    t2u_session *session = ev->session_;
    char *buff = NULL;
    int read_bytes;
    struct timeval t;
    t2u_event *nev;

    (void)events;

    /* check session is ready for sent */
    if (session->send_buffer_count_ >= context->udp_slide_window_)
    {
        LOG_(1, "data not confirmed, disable event for session: %p", session);
        /* data is not confirmed, disable the event */
        event_del(ev->event_);
        session->saved_event_ = ev->event_;
        return;
    }

    buff = (char *)malloc(T2U_PAYLOAD_MAX);
    assert(NULL != buff);

    read_bytes = recv(sock, buff, T2U_PAYLOAD_MAX, 0);

    if (read_bytes > 0)
    {
    }
#if defined _MSC_VER
    else if ((int)read_bytes == 0 ||
        ((read_bytes < 0) && (WSAGetLastError() != WSAEWOULDBLOCK))
#else
    else if ((int)read_bytes == 0 ||
        ((read_bytes < 0) && (errno != EINTR && errno != EWOULDBLOCK && errno != EAGAIN)))
#endif
    {
        LOG_(3, "recv failed on socket %d, read_bytes(%d). ",
            session->sock_, read_bytes);

        /* error */
        free(buff);

        /* close session */
        delete_session_later(session);
        return;
    }
    else
    {
        LOG_(3, "recv failed on socket %d, blocked ...",
            session->sock_);

        free(buff);
        return;
    }

    /* send the data */
    #warning "send the data out"

    return;
}

void t2u_session_handle_connect_response(t2u_session *session, t2u_message_data *mdata)
{
    t2u_rule *rule = session->rule_;
    t2u_context *context = rule->context_;
    t2u_runner *runner = context->runner_;

    uint32_t pair_handle = *((uint32_t *)(void *)mdata->payload);
    if (pair_handle > 0)
    {
        session->status_ = 2;
        session->pair_handle_ = pair_handle;

        // clear events
        event_del(session->ev_->event_);
        free(session->ev_->event_);
        session->ev_->event_ = NULL;

        // move connecting -> connected
        rbtree_remove(rule->connecting_sessions_, &session->handle_);
        rbtree_insert(rule->sessions_, &session->handle_, session);

        // binding new events
        session->ev_->event_ = event_new(runner->base_, session->sock_, 
            EV_READ|EV_PERSIST, process_tcp_cb_, session->ev_);
        assert(NULL != session->ev_->event_);

        event_add(session->ev_->event_, NULL);

    }
    else
    {
        t2u_delete_connecting_session(session);
    }
}


static void session_connect_response_(t2u_session *session)
{
    t2u_rule *rule = (t2u_rule *) session->rule_;
    t2u_message_data *mdata = (t2u_message_data *) malloc(sizeof(t2u_message_data) + sizeof(uint32_t));
    uint32_t *phandle;

    mdata->magic_ = htonl(T2U_MESS_MAGIC);
    mdata->version_ = htons(0x0001);
    mdata->oper_ = htons(connect_response);
    mdata->handle_ = htonl(session->pair_handle_);

    session->send_seq_ = 0; /* always using 0 as start seq */
    mdata->seq_ = htonl(session->send_seq_);
    phandle = (void *)mdata->payload;

    if (session->status_ == 2)
    {
        *phandle = htonl(session->handle_);
    }
    else
    {
        *phandle = htonl(0);
    }

    t2u_send_message_data(rule->context_, mdata);

    free(mdata);
}

static void session_connect_(t2u_session *session)
{
    t2u_rule *rule = (t2u_rule *) session->rule_;
    size_t name_len = strlen(rule->service_);

    if (rule->mode_ == forward_client_mode)
    {
        /* translate tcp->udp */
       
        t2u_message_data *mdata = (t2u_message_data *) malloc(sizeof(t2u_message_data) + name_len + 1);

        mdata->magic_ = htonl(T2U_MESS_MAGIC);
        mdata->version_ = htons(0x0001);
        mdata->oper_ = htons(connect_request);
        mdata->handle_ = htonl(session->handle_);

        session->send_seq_ = 0; /* always using 0 as start seq */
        mdata->seq_ = htonl(session->send_seq_);
        strcpy(mdata->payload, rule->service_);

        t2u_send_message_data(rule->context_, mdata);

        free(mdata);
    }
    else
    {
        /* connect, async */
        int ret = connect(session->sock_, (struct sockaddr *)&rule->conn_addr_, sizeof(rule->conn_addr_));

#if defined _MSC_VER
        if ((ret == -1) && (WSAGetLastError() != WSAEWOULDBLOCK))
#else
        if ((ret == -1) && (errno != EINPROGRESS))
#endif
        {
            LOG_(3, "connect socket failed");
            closesocket(session->sock_);
            session->sock_ = 0;
            return;
        }      
    }
}

static void session_connect_success_cb_(evutil_socket_t sock, short events, void *arg)
{
    int error = 0;
    unsigned int len = sizeof(int);
    t2u_event *ev = (t2u_event *)arg;
    t2u_runner *runner = ev->runner_;
    t2u_rule *rule = ev->rule_;
    t2u_session *session = ev->session_;

    (void)events;

    getsockopt(sock, SOL_SOCKET, SO_ERROR, (void *)&error, &len);

    if (0 == error)
    {
        LOG_(1, "connect for session: %lu success.", (unsigned long)session->handle_);
        session->status_ = 2;

        // clear events
        event_del(ev->event_);
        free(ev->event_);
        ev->event_ = NULL;

        free(ev->extra_event_);
        ev->extra_event_ = NULL;

        // send response
        session_connect_response_(session);

        // move connecting -> connected
        rbtree_remove(rule->connecting_sessions_, &session->handle_);
        rbtree_insert(rule->sessions_, &session->handle_, session);

        // binding new events
        ev->event_ = event_new(runner->base_, session->sock_, 
            EV_READ|EV_PERSIST, process_tcp_cb_, ev);
        assert(NULL != ev->event_);

        event_add(ev->event_, NULL);

    }
    else
    {
        LOG_(2, "connect for session: %lu failed.", (unsigned long)session->handle_);
        t2u_delete_connecting_session(session);
    }
}

static void session_connect_timeout_cb_(evutil_socket_t sock, short events, void *arg)
{
    t2u_event *ev = (t2u_event *)arg;
    t2u_session *session = ev->session_;
    t2u_rule *rule = ev->rule_;
    t2u_context *context = ev->context_;

    (void) sock;
    (void) events;

    if (++session->connect_retries_ >= context->uretries_)
    {
        LOG_(2, "timeout for accept session connection, session: %lu, retry: %lu, delay: %lums",
            (unsigned long)session->handle_, context->uretries_, context->utimeout_);
        
        /* timeout */
        t2u_delete_connecting_session(session);
    }
    else
    {
        /* readd the timer */
        struct timeval t;
        t.tv_sec = context->utimeout_ / 1000;
        t.tv_usec = (context->utimeout_ % 1000) * 1000;

        event_add(session->ev_->event_, &t);

        /* do connect again, if in client mode. */
        if (forward_client_mode == rule->mode_)
        {
            session_connect_(session);
        }
    }
}


t2u_session *t2u_add_connecting_session(t2u_rule *rule, sock_t sock, uint32_t pair_handle)
{
    struct timeval t;
    t2u_context *context = rule->context_;
    t2u_runner *runner = context->runner_;
    
    t2u_session *session = (t2u_session *) malloc(sizeof(t2u_session));
    assert(NULL != session);
    memset(session, 0, sizeof(t2u_session));

    session->handle_ = (uint32_t)sock;
    session->pair_handle_ = pair_handle;
    session->rule_ = rule;
    session->sock_ = sock;

    session->status_ = 1;

    session->send_mess_ = rbtree_init(compare_uint32_ptr);
    session->recv_mess_ = rbtree_init(compare_uint32_ptr);

    LOG_(1, "create new session %p(%lu)", session, (unsigned long)session->handle_);

    session->ev_ = t2u_event_new();
    session->ev_->runner_ = runner;
    session->ev_->context_ = context;
    session->ev_->rule_ = rule;
    session->ev_->session_ = session;
    session->ev_->event_ = evtimer_new(runner->base_, session_connect_timeout_cb_, session->ev_);
    assert (NULL != session->ev_->event_);
    
    t.tv_sec = context->utimeout_ / 1000;
    t.tv_usec = (context->utimeout_ % 1000) * 1000;
    event_add(session->ev_->event_, &t);

    if (forward_server_mode == rule->mode_)
    {
        /* extra event for server mdoe */
        session->ev_->extra_event_ = event_new(runner->base_, sock, EV_WRITE, session_connect_success_cb_, session->ev_);
        event_add(session->ev_->extra_event_, NULL);
    }

    /* add session to rule, using self handle as key */
    rbtree_insert(rule->connecting_sessions_, &session->handle_, session);

    /* connecting */
    session_connect_(session);

    LOG_(1, "add connecting session: %p to rule: %p", session, rule);

    return session;
}


void t2u_delete_connecting_session(t2u_session *session)
{
    t2u_event_delete(session->ev_);
    session->ev_ = NULL;

    /* close socket */
    if (session->sock_)
    {
        closesocket(session->sock_);
        session->sock_ = 0;
    }

    /* delete from rule */
    rbtree_remove(session->rule_->connecting_sessions_, session);

    /* free */
    free(session);
}

void t2u_delete_connected_session(t2u_session *session)
{
    t2u_event_delete(session->ev_);
    session->ev_ = NULL;

    /* close socket */
    if (session->sock_)
    {
        closesocket(session->sock_);
        session->sock_ = 0;
    }

    /* delete from rule */
    rbtree_remove(session->rule_->sessions_, session);

    /* free */
    free(session);
}




static t2u_session *find_session_in_rule(t2u_rule *rule, uint32_t handle, int ispair)
{
    if (ispair)
    {
        /* sessions_ key using pair_handle */
        return rbtree_lookup(rule->sessions_, &handle);
    }
    else
    {
        /* connecting_sessions_ using self handle */
        return rbtree_lookup(rule->connecting_sessions_, &handle);
    }
}

static t2u_session *find_session_in_context_walk(rbtree_node *node, uint32_t handle, int ispair)
{
    t2u_session *session = NULL;
    if (node)
    {
        session = find_session_in_context_walk(node->left, handle, ispair);
        if (!session)
        {
            t2u_rule *rule = node->data;
            session = find_session_in_rule(rule, handle, ispair);

            if (!session)
            {
                session = find_session_in_context_walk(node->right, handle, ispair);
            }
        }
    }

    return session;
}

t2u_session *find_session_in_context(t2u_context *context, uint32_t handle, int ispair)
{
    return find_session_in_context_walk(context->rules_->root, handle, ispair);
}





