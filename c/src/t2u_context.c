#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <event2/event.h>

#include "t2u.h"
#include "t2u_internal.h"

#define MAX_CONTROL_BUFF_LEN (1600)

static int compare_name(void *a, void *b)
{
    return strcmp((char *)a, (char *)b);
}

static void process_udp_cb_(evutil_socket_t sock, short events, void *arg)
{
    int recv_bytes;
    char *buff = (char *) malloc(T2U_MESS_BUFFER_MAX);
    t2u_message_data *mdata;
    t2u_event *ev = (t2u_event *)arg;
    t2u_runner *runner = ev->runner_;
    t2u_context *context = ev->context_;

    recv_bytes = recv(sock, buff, T2U_MESS_BUFFER_MAX, 0);
    if (recv_bytes <= 0)
    {
        /* error on context's udp socket */
        LOG_(3, "recv from udp socket failed, context: %p", context);
        free(buff);
        return;
    }

    mdata = (t2u_message_data *)(void *)buff;
    mdata->magic_ = ntohl(mdata->magic_);
    mdata->version_ = ntohs(mdata->version_);
    mdata->oper_ = ntohs(mdata->oper_);
    mdata->handle_ = ntohl(mdata->handle_);
    mdata->seq_ = ntohl(mdata->seq_);

    if (((int)recv_bytes < (int)sizeof(t2u_message_data)) ||
        (mdata->magic_ != T2U_MESS_MAGIC) ||
        (mdata->version_ != 0x0001))
    {
        /* unknown packet */
        LOG_(2, "recv unknown packet from context: %p", context);
        unknown_callback uc = get_unknown_func_();
        if (uc)
        {
            uc(context, buff, recv_bytes);
        }
        free(buff);
        return;
    }

    switch (mdata->oper_)
    {
    case connect_request:
        {
            char *service = mdata->payload;
            t2u_rule *rule = rbtree_lookup(context->rules_, service);
            if (rule)
            {
                t2u_rule_handle_connect_request(rule, mdata);
            }
            else
            {
                LOG_(2, "no rule match the service: %s", service);
            }
            free(buff);
        }
        break;
    case connect_response:
        {
            /* find with self handle */
            t2u_session *session = find_session_in_context(context, mdata->handle_, 0);
            if (session)
            {
                t2u_session_handle_connect_response(session, mdata);
            }
            else
            {
                LOG_(2, "no session match the handle: %d", mdata->handle_);
            }
            free(buff);
        }
        break;
    case data_request:
        {
            t2u_session *session = find_session_in_context(context, mdata->handle_, 1);
            if (session)
            {
                t2u_session_handle_data_request(session, mdata, recv_bytes);
            }
            else
            {
                LOG_(2, "no session match the handle: %d", mdata->handle_);
            }
            free(buff);
        }    
        break;
    case data_response:
        {
            t2u_session *session = find_session_in_context(context, mdata->handle_, 1);
            if (session)
            {
                /* find it in send queue */
                t2u_message *message = rbtree_lookup(session->send_mess_, &mdata->seq_);
                if (message)
                {
                    t2u_message_handle_data_response(message, mdata);
                }
                else
                {
                    LOG_(2, "no message match the seq: %d", mdata->seq_);
                }
            }
            else
            {
                LOG_(2, "no session match the handle: %d", mdata->handle_);
            }
            free(buff);
        }
        break;
    case retrans_request:
        {
            t2u_session *session = find_session_in_context(context, mdata->handle_, 1);
            if (session)
            {
                /* find it in send queue */
                t2u_message *message = rbtree_lookup(session->send_mess_, &mdata->seq_);
                if (message)
                {
                    t2u_message_handle_retrans_request(message, mdata);
                }
                else
                {
                    LOG_(2, "no message match the seq: %d", mdata->seq_);
                }
            }
            else
            {
                LOG_(2, "no session match the handle: %d", mdata->handle_);
            }
            free(buff);
        }
        break;
    default:
        {
            /* unknown packet */
            LOG_(2, "recv unknown packet from context: %p, type: %d", context, mdata->oper_);
            free(buff);
        }
        break;
    }
}


static void add_context_cb_(t2u_runner *runner, void *arg)
{
    t2u_context *context = (t2u_context *)arg;

    context->ev_udp_ = t2u_event_new();
    context->ev_udp_->runner_ = runner;
    context->ev_udp_->context_ = context;

    context->ev_udp_->event_ = event_new(runner->base_, context->sock_, 
        EV_READ|EV_PERSIST, process_udp_cb_, context->ev_udp_);
    assert(NULL != context->ev_udp_->event_);

    event_add(context->ev_udp_->event_, NULL);
    rbtree_insert(runner->contexts_, context, context);

    LOG_(1, "add context:%p to runner: %p", context, runner);
}


/* init */
t2u_context * t2u_add_context(t2u_runner *runner, sock_t sock)
{
    control_data cdata;
    t2u_context *context = (t2u_context *) malloc(sizeof(t2u_context));
    assert(context != NULL);
    memset(context, 0, sizeof(t2u_context));

    context->rules_ = rbtree_init(compare_name);
    context->sock_ = sock;
    context->utimeout_ = 500;
    context->uretries_ = 3;
    context->udp_slide_window_ = 16;
    context->runner_ = runner;

    cdata.func_ = add_context_cb_;
    cdata.arg_ = context;
    
    LOG_(0, "create new context %p with sock %d", (void *)context, (int)sock);

    t2u_runner_control(runner, &cdata);

    return context;
}

/* context free */
static void delete_context_cb_(t2u_runner *runner, void *arg)
{
    t2u_context *context = (t2u_context *)arg;

    /* remove rules with this context */
    while (context->rules_->root)
    {
        rbtree_node *node = context->rules_->root;
        void *remove = node->data;

        t2u_delete_rule((t2u_rule *)remove);
    }
    /* remove the tree */
    free(context->rules_);
    context->rules_ = NULL;

    /* remove the events */
    t2u_delete_event(context->ev_udp_);
    context->ev_udp_ = NULL;

    /* remove from runner */
    rbtree_remove(runner->contexts_, context);

    LOG_(0, "delete the context %p with sock %d", (void *)context, (int)context->sock_);
    
    free(context);
    return;
}

/* context free */
void t2u_delete_context(t2u_context *context)
{
    control_data cdata;
    memset(&cdata, 0, sizeof(cdata));

    cdata.func_ = delete_context_cb_;
    cdata.arg_ = context;
    t2u_runner_control(context->runner_, &cdata);
    return;
}

void t2u_send_message_data(t2u_context *context, char *data, size_t size)
{
#if 1
    send(context->sock_, data, size, 0);
#else
    int a = rand();
    if (a % 100)
    {
        send(context->sock_, data, size, 0);
    }
    else
    {
        t2u_message_data *md = data;
        LOG_(1, "xxxxxxxxxxxxxxx drop. %d", ntohl(md->seq_));
    }
#endif
}
