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
#include "t2u_thread.h"
#include "t2u_rbtree.h"
#include "t2u_session.h"

#include "t2u_rule.h"
#include "t2u_context.h"


static rbtree *g_session_tree = NULL;
static rbtree *g_session_tree_remote = NULL;
static unsigned long g_session_count = 0;

static int compare_session_message(void *a, void *b)
{
    session_message *sa = (session_message *)a;
    session_message *sb = (session_message *)b;

    return (sa->data_->seq_ - sb->data_->seq_);
}

t2u_session *t2u_session_new(void *rule, sock_t sock)
{
    t2u_session *session = (t2u_session *)malloc(sizeof(t2u_session));
    assert(NULL != session);
    memset(session, 0, sizeof(t2u_session));

    session->handle_ = (uint32_t)sock * sizeof(long);

    session->rule_ = rule;
    session->sock_ = sock;
    
    session->send_mess_ = rbtree_init(compare_session_message);
    session->recv_mess_ = rbtree_init(compare_session_message);

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



static void t2u_session_message_init(t2u_session *session)
{
    /* remove message */
    while (session->send_mess_->root)
    {
        session_message *sm = (session_message *)session->send_mess_->root;
        free(sm->data_);
        
        if (sm->timeout_ev_.event_)
        {
            event_del(sm->timeout_ev_.event_);
            free(sm->timeout_ev_.event_);
        }

        rbtree_remove(session->send_mess_, (void *)sm);
        free(sm);
    }

    while (session->recv_mess_->root)
    {
        session_message *sm = (session_message *)session->recv_mess_->root;
        free(sm->data_);
        
        if (sm->timeout_ev_.event_)
        {
            event_del(sm->timeout_ev_.event_);
            free(sm->timeout_ev_.event_);
        }

        rbtree_remove(session->recv_mess_, (void *)sm);
        free(sm);
    }
}

void t2u_session_delete(t2u_session *session)
{
    // clear message 
    t2u_session_message_init(session);

    /* clear the timeout callback */
    if (session->timeout_ev_.event_)
    {
        event_del(session->timeout_ev_.event_);
        free(session->timeout_ev_.event_);
        session->timeout_ev_.event_ = NULL;
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


void t2u_session_send_u_mess(t2u_session *session, session_message *sm)
{
    int sent_bytes;
    t2u_rule *rule = (t2u_rule *) session->rule_;
    t2u_context *context = (t2u_context *)rule->context_;

    if (sm->data_)
    {
        sent_bytes = send(context->sock_, (const char *)sm->data_, (int)sm->len_, 0);

        if ((int)sm->len_ != sent_bytes)
        {
            /* post error handle. */
        }
    }
}


void t2u_session_send_u_connect(t2u_session *session)
{
    t2u_rule *rule = (t2u_rule *) session->rule_;
    size_t name_len = strlen(rule->service_);

    t2u_message *mess = (t2u_message *) malloc (sizeof(t2u_message) + name_len + 1);
    session_message sm;
    memset(&sm, 0, sizeof(session_message));

    assert(NULL != mess);

    if (2 == session->status_)
    {
        free(mess);
        return;
    }
    
    /* remove message */
    t2u_session_message_init(session);

    mess->magic_ = htonl(T2U_MESS_MAGIC);
    mess->version_ = htons(0x0001);
    mess->oper_ = htons(connect_request);
    mess->handle_ = htonl(session->handle_);
    
    session->send_seq_ = 1; /* always using 1 as start seq */
    mess->seq_ = htonl(session->send_seq_);
    sprintf(mess->payload, rule->service_);

    sm.data_ = mess;
    sm.len_ = sizeof(t2u_message) + name_len + 1;

    t2u_session_send_u_mess(session, &sm);
    
    free(mess);
}

void t2u_session_send_u_connect_response(t2u_session *session, char *connect_message)
{
    uint32_t *phandle = NULL;
    session_message sm;
    sm.data_ = (t2u_message *) malloc (sizeof(t2u_message) + sizeof(uint32_t));
    assert(NULL != sm.data_);

    sm.len_ = sizeof(t2u_message) + sizeof(uint32_t);

    sm.data_->magic_ = htonl(T2U_MESS_MAGIC);
    sm.data_->version_ = htons(0x0001);
    sm.data_->oper_ = htons(connect_response);
    sm.data_->handle_ = ((t2u_message *)connect_message)->handle_;     /* response using the handle from request */
    sm.data_->seq_ = ((t2u_message *)connect_message)->seq_;           /* response using the seq from request */

    phandle = (void *)sm.data_->payload;
    if (session->status_ == 2)
    {
        *phandle = htonl(session->handle_);     /* payload is new handle in server side */
    }
    else
    {
        *phandle = htonl(0);
    }

    t2u_session_send_u_mess(session, &sm);

    free(sm.data_);
}


session_message *t2u_session_send_u_data(t2u_session *session, char *data, size_t length)
{

    t2u_message *mess = NULL;
    session_message *sm = NULL;

    if (2 != session->status_)
    {
        return NULL;
    }

    mess = (t2u_message *) malloc (sizeof(t2u_message) + length);
    sm = (session_message *) malloc (sizeof(session_message));
    assert(NULL != mess);
    assert(NULL != sm);

    sm->len_ = sizeof(t2u_message) + length;
    sm->data_ = mess;

    mess->magic_ = htonl(T2U_MESS_MAGIC);
    mess->version_ = htons(0x0001);
    mess->oper_ = htons(data_request);
    mess->handle_ = htonl(session->handle_);
    mess->seq_ = htonl(session->send_seq_++);
    memcpy(mess->payload, data, length);

    t2u_session_send_u_mess(session, sm);
    
    return sm;
}

void t2u_session_send_u_data_response(t2u_session *session, char *data_message, uint32_t error)
{
    session_message sm;
    uint32_t *perr = NULL;
    sm.data_ = (t2u_message *) malloc (sizeof(t2u_message) + sizeof(uint32_t));
    assert(NULL != sm.data_);

    sm.len_ = sizeof(t2u_message) + sizeof(uint32_t);

    sm.data_->magic_ = htonl(T2U_MESS_MAGIC);
    sm.data_->version_ = htons(0x0001);
    sm.data_->oper_ = htons(data_response);
    sm.data_->handle_ = ((t2u_message *)data_message)->handle_;   /* response using the handle from request */
    sm.data_->seq_ = ((t2u_message *)data_message)->seq_;         /* response using the seq from request */
    
    perr = (void *)sm.data_->payload;
    *perr = htonl(error);                                          /* error */

    t2u_session_send_u_mess(session, &sm);

    free(sm.data_);
}
