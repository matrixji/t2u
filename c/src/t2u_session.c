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


static rbtree *g_session_tree = NULL;
static rbtree *g_session_tree_remote = NULL;
static unsigned long g_session_count = 0;

t2u_session *t2u_session_new(void *rule, sock_t sock)
{
    t2u_session *session = (t2u_session *)malloc(sizeof(t2u_session));
    assert(NULL != session);
    memset(session, 0, sizeof(t2u_session));

    session->handle_ = (uint32_t)sock * sizeof(long);

    session->rule_ = rule;
    session->sock_ = sock;

    /* alloc a vaid handle */
    if (!g_session_tree)
    {
        g_session_tree = rbtree_init(NULL);
    }

    if (!g_session_tree_remote)
    {
        g_session_tree_remote = rbtree_init(NULL);
    }

    rbtree_insert(g_session_tree, (void *)(session->handle_ * sizeof(long)), session);
    ++g_session_count;
    
    LOG_(1, "create new session %p(%lu)", session, (unsigned long)session->handle_);
    return session;
}

void t2u_session_delete(t2u_session *session)
{
    /* remove message */
    if (session->mess_.data_)
    {
        free(session->mess_.data_);
        session->mess_.data_ = NULL;
        session->mess_.len_ = 0;
    }

    /* delete timeout event */
    if (session->timeout_ev.event_)
    {
        event_del(session->timeout_ev.event_);
        free(session->timeout_ev.event_);
        session->timeout_ev.event_ = NULL;
    }

    /* close the socket */
    closesocket(session->sock_);
    if (session->pair_handle_ > 0)
    {
        rbtree_remove(g_session_tree_remote, (void *)(session->pair_handle_ * sizeof(long)));
    }
    rbtree_remove(g_session_tree, (void *)(session->handle_ * sizeof(long)));

    LOG_(1, "delete the session %p(%lu)", session, (unsigned long)session->handle_);
    free(session);

    --g_session_count;
    if (g_session_count == 0)
    {
        free(g_session_tree);
        free(g_session_tree_remote);

        g_session_tree = NULL;
        g_session_tree_remote = NULL;
    }
}

void t2u_session_assign_remote_handle(t2u_session *session, uint32_t remote_handle)
{
    session->pair_handle_ = remote_handle;
    if (g_session_tree_remote)
    {  
        rbtree_remove(g_session_tree_remote, (void *)(remote_handle * sizeof(long)));
        rbtree_insert(g_session_tree_remote, (void *)(remote_handle * sizeof(long)), session);
    }
}

t2u_session *t2u_session_by_pair_handle(uint32_t remote_handle)
{
    if (g_session_tree_remote)
    {
        return rbtree_lookup(g_session_tree_remote, (void *)(remote_handle * sizeof(long)));
    }
    return NULL;
}

t2u_session *t2u_session_by_handle(uint32_t handle)
{
    if (g_session_tree)
    {
        return rbtree_lookup(g_session_tree, (void *)(handle * sizeof(long)));
    }
    return NULL;
}


static void t2u_session_send_u_mess(t2u_session *session, session_message *mess)
{
    int sent_bytes;
    t2u_rule *rule = (t2u_rule *) session->rule_;
    t2u_context *context = (t2u_context *)rule->context_;

    if (mess->data_)
    {
        sent_bytes = send(context->sock_, mess->data_, mess->len_, 0);

        if ((int)mess->len_ != sent_bytes)
        {
            /* post error handle. */
        }
    }
}


void t2u_session_send_u(t2u_session *session)
{
    return t2u_session_send_u_mess(session, &session->mess_);
}

void t2u_session_send_u_connect(t2u_session *session)
{
    if (2 == session->status_)
    {
        return;
    }

    /* remove message */
    if (session->mess_.data_)
    {
        free(session->mess_.data_);
        session->mess_.data_ = NULL;
        session->mess_.len_ = 0;
    }

    t2u_rule *rule = (t2u_rule *) session->rule_;
    size_t name_len = strlen(rule->service_);

    t2u_message *mess = (t2u_message *) malloc (sizeof(t2u_message) + name_len + 1);
    assert(NULL != mess);

    session->mess_.len_ = sizeof(t2u_message) + name_len + 1;
    session->mess_.data_ = mess;

    mess->magic_ = htonl(T2U_MESS_MAGIC);
    mess->version_ = htons(0x0001);
    mess->oper_ = htons(connect_request);
    mess->handle_ = htonl(session->handle_);
    mess->seq_ = htonl(session->send_seq_++);
    sprintf(mess->payload, rule->service_);

    t2u_session_send_u(session);
}

void t2u_session_send_u_connect_response(t2u_session *session, char *connect_message)
{
    session_message mess;
    mess.data_ = (t2u_message *) malloc (sizeof(t2u_message) + sizeof(uint32_t));
    assert(NULL != mess.data_);

    mess.len_ = sizeof(t2u_message) + sizeof(uint32_t);

    mess.data_->magic_ = htonl(T2U_MESS_MAGIC);
    mess.data_->version_ = htons(0x0001);
    mess.data_->oper_ = htons(connect_response);
    mess.data_->handle_ = ((t2u_message *)connect_message)->handle_;     /* response using the handle from request */
    mess.data_->seq_ = ((t2u_message *)connect_message)->seq_;           /* response using the seq from request */

    if (session->status_ == 2)
    {
        *((uint32_t *)mess.data_->payload) = htonl(session->handle_); /* payload is new handle in server side */
    }
    else
    {
        *((uint32_t *)mess.data_->payload) = htonl(0);
    }

    t2u_session_send_u_mess(session, &mess);

    free(mess.data_);
}


void t2u_session_send_u_data(t2u_session *session, char *data, size_t length)
{
    if (2 != session->status_)
    {
        return;
    }

    assert (NULL == session->mess_.data_);
    t2u_message *mess = (t2u_message *) malloc (sizeof(t2u_message) + length);
    assert(NULL != mess);

    session->mess_.len_ = sizeof(t2u_message) + length;
    session->mess_.data_ = mess;

    mess->magic_ = htonl(T2U_MESS_MAGIC);
    mess->version_ = htons(0x0001);
    mess->oper_ = htons(data_request);
    mess->handle_ = htonl(session->handle_);
    mess->seq_ = htonl(session->send_seq_++);
    memcpy(mess->payload, data, length);

    t2u_session_send_u(session);
}

void t2u_session_send_u_data_response(t2u_session *session, char *data_message, uint32_t error)
{
    session_message mess;
    mess.data_ = (t2u_message *) malloc (sizeof(t2u_message) + sizeof(uint32_t));
    assert(NULL != mess.data_);

    mess.len_ = sizeof(t2u_message) + sizeof(uint32_t);

    mess.data_->magic_ = htonl(T2U_MESS_MAGIC);
    mess.data_->version_ = htons(0x0001);
    mess.data_->oper_ = htons(data_response);
    mess.data_->handle_ = ((t2u_message *)data_message)->handle_;   /* response using the handle from request */
    mess.data_->seq_ = ((t2u_message *)data_message)->seq_;         /* response using the seq from request */
    *((uint32_t *)mess.data_->payload) = htonl(error);              /* error */

    t2u_session_send_u_mess(session, &mess);

    free(mess.data_);
}