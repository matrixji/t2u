#include <assert.h>
#include <event2/event.h>
#include "t2u.h"
#include "t2u_internal.h"


static void process_request_timeout_cb_(evutil_socket_t sock, short events, void *arg)
{
    t2u_event *ev = (t2u_event *)arg;

    t2u_message *message = ev->message_;
    t2u_session *session = ev->session_;
    t2u_context *context = ev->context_;

    if (message->send_retries_++ > context->udp_slide_window_)
    {
        // timeout.
        LOG_(3, "timeout for message: %p, in session: %p", message, session);
        t2u_delete_connected_session_later(session);
    }
    else
    {
        t2u_send_message_data(context, (char *)message->data_, message->len_);
    }
}

t2u_message *t2u_add_request_message(t2u_session *session, char *payload, int payload_len)
{ 
    t2u_message *message = (t2u_message *)malloc(sizeof(t2u_message));
    t2u_rule *rule = session->rule_;
    t2u_context *context = rule->context_;

    t2u_event *nev = NULL;
    struct timeval t;
    int r = 0;

    message->len_ = sizeof(t2u_message_data) + payload_len;
    message->data_ = (t2u_message_data *)malloc(message->len_);
    message->data_->handle_ = htonl(session->pair_handle_);
    message->data_->magic_ = htonl(T2U_MESS_MAGIC);
    message->data_->oper_ = htons(data_request);
    memcpy(message->data_->payload, payload, payload_len);
    message->data_->seq_ = ntohl(++session->send_seq_);
    message->data_->version_ = ntohs(1);

    message->send_retries_ = 0;
    message->seq_ = session->send_seq_;
    message->session_ = session;
    message->ev_timeout_ = (t2u_event *)malloc(sizeof(t2u_event));

    nev = message->ev_timeout_;
    memset(nev, 0, sizeof(t2u_event));

    nev->message_ = message;
    nev->session_ = session;
    nev->rule_ = rule;
    nev->context_ = context;
    nev->runner_ = context->runner_;
    nev->event_ = evtimer_new(nev->runner_->base_, process_request_timeout_cb_, nev);

    t.tv_sec = context->utimeout_ / 1000;
    t.tv_usec = (context->utimeout_ % 1000) * 1000;
    r = evtimer_add(nev->event_, &t);
    assert(r == 0);

    rbtree_insert(session->send_mess_, &message->seq_, message);
    session->send_buffer_count_++;

    t2u_send_message_data(context, (char *)message->data_, message->len_);
    
    return message;
}

void t2u_delete_request_message(t2u_message *message)
{
    t2u_session *session = message->session_;

    t2u_delete_event(message->ev_timeout_);
    message->ev_timeout_ = NULL;

    free(message->data_);
    message->data_ = NULL;

    // remove from session
    if (0 == rbtree_remove(session->send_mess_, &message->seq_))
    {
        session->send_buffer_count_--;

        // check saved event
        if (session->ev_)
        {
            if (!session->ev_->event_)
            {
                session->ev_->event_ = event_new(session->rule_->context_->runner_->base_, session->sock_,
                    EV_READ | EV_PERSIST, t2u_session_process_tcp, session->ev_);

                event_add(session->ev_->event_, NULL);
            }
        }

    }

    free(message);
}

void t2u_message_handle_data_response(t2u_message *message, t2u_message_data *mdata)
{
    t2u_session *session = message->session_;

    int value = ntohl(*((int *)mdata->payload));
    if (value == 0)
    {
        /* success, remove same seq from send_mess_ */
        t2u_delete_request_message(message);
    }
    else if (value == 2)
    {
        /* block, try later */
    }
    else
    {
        /* error */
        LOG_(2, "data response with error for session: %p, value: %d", session, value);
        t2u_delete_connected_session(session);
    }
}

void t2u_message_handle_retrans_request(t2u_message *message, t2u_message_data *mdata)
{
    LOG_(1, "retrans: %lu", message->data_->seq_);
    t2u_send_message_data(message->session_->rule_->context_, (char *)message->data_, message->len_);
}
