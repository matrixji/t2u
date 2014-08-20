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

    if (*sa > *sb)
    {
        return 1;
    }

    if (*sa < *sb)
    {
        return -1;
    }

    return 0;
}


void t2u_session_process_tcp(evutil_socket_t sock, short events, void *arg)
{
    t2u_event *ev = (t2u_event *)arg;
    //t2u_runner *runner = ev->runner_;
    t2u_context *context = ev->context_;
    //t2u_rule *rule = ev->rule_;
    t2u_session *session = ev->session_;
    char *buff = NULL;
    int read_bytes;

    (void)events;

    /* check session is ready for sent */
    if (session->send_buffer_count_ >= context->udp_slide_window_)
    {
        LOG_(1, "data not confirmed, disable event for session: %p %d", session, session->send_buffer_count_);
        /* data is not confirmed, disable the event */
        event_del(ev->event_);
        ev->event_ = NULL;
        return;
    }

    buff = (char *)malloc(T2U_PAYLOAD_MAX);
    assert(NULL != buff);

    read_bytes = recv(sock, buff, T2U_PAYLOAD_MAX, 0);

#if defined _MSC_VER
    int last_error = WSAGetLastError();
#else
    int last_error = errno;
#endif

    if (read_bytes > 0)
    {
    }
#if defined _MSC_VER
    else if ((int)read_bytes == 0 ||
        ((read_bytes < 0) && (last_error != WSAEWOULDBLOCK)))
#else
    else if ((int)read_bytes == 0 ||
        ((read_bytes < 0) && (last_error != EINTR && last_error != EWOULDBLOCK && last_error != EAGAIN)))
#endif
    {
        LOG_(3, "recv failed on socket %d, read_bytes(%d). %d",
            session->sock_, read_bytes, last_error);

        /* error */
        free(buff);

        /* close session later, after send_mess_ out */
        t2u_delete_connected_session_later(session);
        return;
    }
    else
    {
        LOG_(3, "recv failed on socket %d, blocked ...",
            session->sock_);

        free(buff);
        return;
    }
    
    /* build a session message */
    t2u_add_request_message(session, buff, read_bytes);
    free(buff);

    return;
}


void t2u_session_handle_connect_response(t2u_session *session, t2u_message_data *mdata)
{
    t2u_rule *rule = session->rule_;
    t2u_context *context = rule->context_;
    t2u_runner *runner = context->runner_;

    uint32_t error = *((uint32_t *)(void *)mdata->payload);
    error = ntohl(error);

    if (error == 0)
    {
        session->status_ = 2;

        // clear events
        event_free(session->ev_->event_);
        session->ev_->event_ = NULL;

        // move connecting -> connected
        rbtree_remove(rule->connecting_sessions_, &session->handle_);
        rbtree_insert(rule->sessions_, &session->handle_, session);

        // binding new events
        session->ev_->event_ = event_new(runner->base_, session->sock_, 
            EV_READ | EV_PERSIST, t2u_session_process_tcp, session->ev_);
        assert(NULL != session->ev_->event_);

        event_add(session->ev_->event_, NULL);

		LOG_(1, "connect for session: %p with handle: %llu success. sock: %d", session, session->handle_, session->sock_);

    }
    else
    {
        LOG_(2, "connect for session: %p with handle: %llu failed.", session, session->handle_);
        t2u_delete_connecting_session(session);
    }
}


void t2u_session_handle_data_request(t2u_session *session, t2u_message_data *mdata, int mdata_len)
{
    t2u_rule *rule = session->rule_;
    t2u_context *context = rule->context_;
    t2u_runner *runner = context->runner_;
    t2u_message_data *mdata_resp = NULL;
    t2u_message_data *this_mdata = mdata;

    uint32_t seq_diff = this_mdata->seq_ - session->recv_seq_;

    if ((seq_diff > context->udp_slide_window_) || (seq_diff <= 1))
    {
        mdata_resp = (t2u_message_data *)malloc(sizeof(t2u_message_data) + sizeof(int));
        mdata_resp->handle_ = hton64(session->handle_);
        mdata_resp->magic_ = htonl(T2U_MESS_MAGIC);
        mdata_resp->oper_ = htons(data_response);
        mdata_resp->seq_ = htonl(this_mdata->seq_);
        mdata_resp->version_ = htons(1);
        int *value = (int *)(void *)mdata_resp->payload;

        *value = htonl(0); // success for default.

        if (seq_diff == 1)
        {
            while (this_mdata)
            {
                uint32_t next_seq = this_mdata->seq_ + 1;

                int flags = 0;
#ifdef __linux__
                flags |= MSG_NOSIGNAL;
#endif
#ifdef __apple__
                flags |= SO_NOSIGPIPE;
#endif
                int r = send(session->sock_, this_mdata->payload, mdata_len - sizeof(t2u_message_data), flags);

                if (this_mdata->seq_ != mdata->seq_)
                {
                    // this mdata is copy from recv queue. need to free it.
                    free(this_mdata);
                }
                this_mdata = NULL;

#ifdef _MSC_VER
                int last_error = WSAGetLastError();
                if (r == 0 || (r < 0 && last_error != WSAEWOULDBLOCK))
#else
                int last_error = errno;
                if (r == 0 || (r < 0 && last_error != EWOULDBLOCK && last_error != EAGAIN))
#endif
                {
                    // error, response it's error.
                    *value = htonl(1);
                    t2u_send_message_data(context, (char *)mdata_resp, sizeof(t2u_message_data) + sizeof(int));

                    LOG_(2, "send on session: %p failed. error: %d", session, last_error);
                    t2u_delete_connected_session_later(session);
                    free(mdata_resp);
                    return;
                }
                else if (r > 0)
                {
                    assert(r == mdata_len - sizeof(t2u_message_data));
                    // success.
                    *value = htonl(0);
                    session->recv_seq_++;
                    t2u_send_message_data(context, (char *)mdata_resp, sizeof(t2u_message_data) + sizeof(int));
                }
                else
                {
                    // block
                    *value = htonl(2);
                    t2u_send_message_data(context, (char *)mdata_resp, sizeof(t2u_message_data) + sizeof(int));
                    free(mdata_resp);
                    return;
                }
                
                t2u_message *this_m  = rbtree_lookup(session->recv_mess_, &next_seq);

                if (this_m)
                {
                    this_mdata = this_m->data_;
                    mdata_len = this_m->len_;

                    // find next. remove it from recv queue
                    rbtree_remove(session->recv_mess_, &this_mdata->seq_);

                    // free the mess
                    free(this_m);

                    session->recv_buffer_count_--;

                    // update the response seq.
                    mdata_resp->seq_ = htonl(this_mdata->seq_);

                    // try delete.
                    t2u_try_delete_connected_session(session);
                }
            }
        }
        else
        {
            t2u_send_message_data(context, (char *)mdata_resp, sizeof(t2u_message_data) + sizeof(int));
        }

        free(mdata_resp);
    }
    else
    {
        // in range, but not in sequence. push to recv queue.
        LOG_(1, "we want:%lu but:%lu", session->recv_seq_ + 1, mdata->seq_);
        this_mdata = NULL;
        t2u_message *this_m = rbtree_lookup(session->recv_mess_, &mdata->seq_);
        
        if (!this_m && session->recv_buffer_count_ < context->udp_slide_window_)
        {
            this_m = (t2u_message *) malloc(sizeof(t2u_message));
            this_mdata = (t2u_message_data *)malloc(mdata_len);
            assert(NULL != this_mdata);

            memcpy(this_mdata, mdata, mdata_len);
            this_m->data_ = this_mdata;
            this_m->len_ = mdata_len;

            rbtree_insert(session->recv_mess_, &this_mdata->seq_, this_m);
            session->recv_buffer_count_++;
        }

        // send retrans request
        t2u_message_data retrans_md; 
        retrans_md.handle_ = hton64(session->handle_);
        retrans_md.magic_ = htonl(T2U_MESS_MAGIC);
        retrans_md.oper_ = htons(retrans_request);
        retrans_md.version_ = htons(1);
        
        uint32_t i = 0;
        for (i = 0; i < context->udp_slide_window_; i++)
        {
            uint32_t test_seq = session->recv_seq_ + 1 + i;
            if (rbtree_lookup(session->recv_mess_, &test_seq) == NULL)
            {
                uint32_t span1 = mdata->seq_ - test_seq;
                uint32_t span2 = session->retry_seq_ - test_seq;

                if (span1 <= context->udp_slide_window_ && span2 > context->udp_slide_window_)
                {
                    retrans_md.seq_ = htonl(test_seq);
                    t2u_send_message_data(context, (char *)&retrans_md, sizeof(retrans_md));
                    session->retry_seq_ = test_seq;
                }
            }
        }
    }
}

static void session_connect_response_(t2u_session *session)
{
    t2u_rule *rule = (t2u_rule *) session->rule_;
    t2u_message_data *mdata = (t2u_message_data *) malloc(sizeof(t2u_message_data) + sizeof(uint32_t));
    uint32_t *error;

    mdata->magic_ = htonl(T2U_MESS_MAGIC);
    mdata->version_ = htons(0x0001);
    mdata->oper_ = htons(connect_response);
    mdata->handle_ = hton64(session->handle_);

    session->send_seq_ = 0; /* always using 0 as start seq */
    mdata->seq_ = htonl(session->send_seq_);
    error = (void *)mdata->payload;

    if (session->status_ == 2)
    {
        *error = 0;
    }
    else
    {
        *error = htonl(1);
    }

    t2u_send_message_data(rule->context_, (char *)mdata, sizeof(t2u_message_data) + sizeof(uint32_t));

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
        mdata->handle_ = hton64(session->handle_);

        session->send_seq_ = 0; /* always using 0 as start seq */
        mdata->seq_ = htonl(session->send_seq_);
#if defined _MSC_VER
        strcpy_s(mdata->payload, name_len + 1, rule->service_);
#else
        strcpy(mdata->payload, rule->service_);
#endif
        t2u_send_message_data(rule->context_, (char *)mdata, sizeof(t2u_message_data) + name_len + 1);

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
        session->status_ = 2;

        // clear events
        event_free(ev->event_);
        ev->event_ = NULL;

		event_free(ev->extra_event_);
        ev->extra_event_ = NULL;

        // send response
        session_connect_response_(session);

        // move connecting -> connected
        rbtree_remove(rule->connecting_sessions_, &session->handle_);
        rbtree_insert(rule->sessions_, &session->handle_, session);

        // binding new events
        ev->event_ = event_new(runner->base_, session->sock_, 
            EV_READ | EV_PERSIST, t2u_session_process_tcp, ev);
        assert(NULL != ev->event_);

        event_add(ev->event_, NULL);

		LOG_(1, "connect for session: %p with handle: %llu success. sock: %d", session, session->handle_, session->sock_);

    }
    else
    {
        LOG_(2, "connect for session: %p with handle: %llu failed.", session, session->handle_);
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


t2u_session *t2u_add_connecting_session(t2u_rule *rule, sock_t sock, uint64_t handle)
{
    struct timeval t;
    t2u_context *context = rule->context_;
    t2u_runner *runner = context->runner_;

    t2u_session *session = (t2u_session *)malloc(sizeof(t2u_session));
    assert(NULL != session);
    memset(session, 0, sizeof(t2u_session));

    struct timeval tv;
    evutil_gettimeofday(&tv, NULL);
    
    struct sockaddr_in selfaddr;
    socklen_t namelen = sizeof(selfaddr);
    getsockname(context->sock_, (struct sockaddr*)&selfaddr, &namelen);

    if (handle == 0)
    {
        session->handle_ = ((uint64_t)tv.tv_sec & 0x00ff) | ((uint64_t)selfaddr.sin_addr.s_addr & 0xff00) | ((uint64_t)(sock & 0x00ff) << 32) | ((uint64_t)(tv.tv_usec & 0x00ff) << 48);
    }
    else
    {
        session->handle_ = handle;
    }
    session->rule_ = rule;
    session->sock_ = sock;

    session->status_ = 1;

    session->send_mess_ = rbtree_init(compare_uint32_ptr);
    session->recv_mess_ = rbtree_init(compare_uint32_ptr);

    LOG_(1, "create new session %p handle: %llu, sock :%d", session, session->handle_, sock);

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
		LOG_(1, "add extra event for connecting session: %p handle: %llu sock: %d", session, session->handle_, sock);
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
    t2u_delete_event(session->ev_);
    session->ev_ = NULL;

    /* close socket */
    if (session->sock_)
    {
        closesocket(session->sock_);
        session->sock_ = 0;
    }

    /* delete from rule */
    rbtree_remove(session->rule_->connecting_sessions_, &session->handle_);

    /* free */
    free(session->send_mess_);
    free(session->recv_mess_);
    free(session);
}

void t2u_delete_connected_session(t2u_session *session)
{
    t2u_delete_event(session->ev_);
    session->ev_ = NULL;

    /* close socket */
    if (session->sock_)
    {
        closesocket(session->sock_);
        session->sock_ = 0;
    }

    /* clear recv queue */
    while (session->recv_mess_->root)
    {
        t2u_message *m = session->recv_mess_->root->data;
        rbtree_remove(session->recv_mess_, session->recv_mess_->root->key);
        
        free(m->data_);
        free(m);
    }

    while (session->send_mess_->root)
    {
        t2u_message *m = session->send_mess_->root->data;

        /* t2u_message only in send queue */
        t2u_delete_request_message(m);
    }

    // TODO: remove it
    LOG_(1, "session end with %d send buffers.", session->send_buffer_count_);
    LOG_(1, "session end with %d recv buffers.", session->recv_buffer_count_);
    // t2u_sleep(3000);

    /* delete from rule */
    rbtree_remove(session->rule_->sessions_, &session->handle_);

    LOG_(1, "delete connected session: %p, sock: %d", session, session->sock_);
    
    /* free */
    free(session->send_mess_);
    free(session->recv_mess_);
    free(session);
}

void t2u_try_delete_connected_session(t2u_session *session)
{
    /* check status send_mess_ recv_mess_ */
    if ((session->status_ == 3) && (NULL == session->send_mess_->root) && (NULL == session->recv_mess_->root))
    {
        t2u_delete_connected_session(session);
    }
}

void t2u_delete_connected_session_later(t2u_session *session)
{
    session->status_ = 3; // closing
	t2u_try_delete_connected_session(session);
}


static t2u_session *find_session_in_rule(t2u_rule *rule, uint64_t handle, int connected)
{
    void *n = NULL;
    if (connected)
    {
        /* sessions_ */
        n = rbtree_lookup(rule->sessions_, &handle);
    }
    else
    {
        /* connecting_sessions_  */
        n = rbtree_lookup(rule->connecting_sessions_, &handle);
    }
    return (t2u_session *)n;
}

static t2u_session *find_session_in_context_walk(rbtree_node *node, uint64_t handle, int connected)
{
    t2u_session *session = NULL;
    if (node)
    {
        session = find_session_in_context_walk(node->left, handle, connected);
        if (!session)
        {
            t2u_rule *rule = node->data;
            session = find_session_in_rule(rule, handle, connected);

            if (!session)
            {
                session = find_session_in_context_walk(node->right, handle, connected);
            }
        }
    }
    return session;
}

t2u_session *find_session_in_context(t2u_context *context, uint64_t handle, int connected)
{
    return find_session_in_context_walk(context->rules_->root, handle, connected);
}





