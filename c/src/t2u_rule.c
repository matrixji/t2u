#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <event2/event.h>
#include <event2/util.h>
#ifdef __GNUC__
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "t2u.h"
#include "t2u_internal.h"


static int compare_uint64_ptr(void *a, void *b)
{
    uint64_t *sa = (uint64_t *)a;
    uint64_t *sb = (uint64_t *)b;

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

void t2u_rule_handle_connect_request(t2u_rule *rule, t2u_message_data *mdata)
{
    uint64_t handle = mdata->handle_;
    t2u_session *session = NULL;
    t2u_session *oldsession = NULL;

    oldsession = (t2u_session *)rbtree_lookup(rule->sessions_, &handle);
    if (oldsession)
    {
        LOG_(2, "delete old session:%p", oldsession);
        t2u_delete_connected_session(oldsession);
    }

    /* connect to peer */
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == s)
    {
        LOG_(3, "create socket failed");
        return;
    }

    /* unblocking */
    evutil_make_socket_nonblocking(s);

    /* new session */
    session = t2u_add_connecting_session(rule, s, handle);
    assert(NULL != session);
}


static void rule_process_accept_cb_(evutil_socket_t sock, short events, void *arg)
{
    struct sockaddr_in client_addr;
    unsigned int client_len = sizeof(client_addr);

    t2u_event *ev = (t2u_event *)arg;
    t2u_rule *rule = ev->rule_;
    t2u_session *session;

    sock_t s = accept(rule->listen_sock_, (struct sockaddr *)&client_addr, &client_len);
    if (s < 0)
    {
        return;
    }

    /* nonblock */
    evutil_make_socket_nonblocking(s);

    session = t2u_add_connecting_session(rule, s, 0);
    assert(NULL != session);
}


static void add_rule_cb_(t2u_runner *runner, void *arg)
{
    t2u_rule *rule = (t2u_rule *)arg;
    t2u_context *context = (t2u_context *) rule->context_;

    if (rule->mode_ == forward_client_mode)
    {
        rule->ev_listen_ = (t2u_event *)malloc(sizeof(t2u_event));
        assert(NULL != rule->ev_listen_);

        memset(rule->ev_listen_, 0, sizeof(t2u_event));

        rule->ev_listen_->runner_ = runner;
        rule->ev_listen_->context_ = rule->context_;
        rule->ev_listen_->rule_ = rule;

        rule->ev_listen_->event_ = event_new(runner->base_, rule->listen_sock_,
            EV_READ | EV_PERSIST, rule_process_accept_cb_, rule->ev_listen_);
        assert(NULL != rule->ev_listen_->event_);

        event_add(rule->ev_listen_->event_, NULL);
		LOG_(0, "add event for rule listen, rule: %p, sock: %d", rule, rule->listen_sock_);
    }

    rbtree_insert(context->rules_, rule->service_, rule);
}

t2u_rule *t2u_add_rule(t2u_context *context, forward_mode mode, const char *service, const char *addr, unsigned short port)
{
    int reuse = 1;
    struct sockaddr_in listen_addr;
    t2u_rule *rule = (t2u_rule *) malloc(sizeof(t2u_rule));
    control_data cdata;

    assert (NULL != rule);
    memset(rule, 0, sizeof(t2u_rule));

    if (mode == forward_client_mode)
    {
        /* try listen on addr, port */
        rule->listen_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (-1 == rule->listen_sock_)
        {
            LOG_(3, "create socket failed");

            free(rule);
            return NULL;
        }

        /* set socket nonblock */
        evutil_make_socket_nonblocking(rule->listen_sock_);
        setsockopt(rule->listen_sock_, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse));

        /* listen the socket */
        listen_addr.sin_family = AF_INET;
        listen_addr.sin_port = ntohs(port);
        if (addr)
        {
            listen_addr.sin_addr.s_addr = inet_addr(addr);
        }
        else
        {
            listen_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        }

        if (-1 == bind(rule->listen_sock_, (struct sockaddr *)&listen_addr, sizeof(listen_addr)))
        {
            closesocket(rule->listen_sock_);

            LOG_(3, "bind socket failed");
            free(rule);
            return NULL;
        }

        if (-1 == listen(rule->listen_sock_, 256))
        {
            closesocket(rule->listen_sock_);
            
            LOG_(3, "listen socket failed");
            free(rule);
            return NULL;
        }
    }
    else
    {
        /* server mode */
        rule->conn_addr_.sin_family = AF_INET;
        if (addr)
        {
            rule->conn_addr_.sin_addr.s_addr = inet_addr(addr);
        }
        else
        {
            rule->conn_addr_.sin_addr.s_addr = inet_addr("127.0.0.1");
        }
        rule->conn_addr_.sin_port = ntohs(port);
    }

    rule->mode_ = mode;
    rule->service_ = malloc (strlen (service) + 1);
    assert (NULL != rule->service_);

#ifdef _MSC_VER
    strcpy_s(rule->service_, strlen(service)+1, service);
#else
    strcpy(rule->service_, service);
#endif

    rule->context_ = context;
    rule->sessions_ = rbtree_init(compare_uint64_ptr);
    rule->connecting_sessions_ = rbtree_init(compare_uint64_ptr); 

    cdata.func_ = add_rule_cb_;
    cdata.arg_ = rule;


    LOG_(1, "create new rule %p, name: %s, addr: %s, port: %u", rule, rule->service_, addr, port);

    t2u_runner_control(context->runner_, &cdata);

    return rule;
}


void delete_rule_cb_(t2u_runner *runner, void *arg)
{
    t2u_rule *rule = (t2u_rule*)arg;
    t2u_context *context = rule->context_;
	
	/* remove the events */
	t2u_delete_event(rule->ev_listen_);
	rule->ev_listen_ = NULL;

    if (forward_client_mode == rule->mode_)
    {
        closesocket(rule->listen_sock_);
    }

    /* remove sessions */
    while (rule->sessions_->root)
    {
        rbtree_node *node = rule->sessions_->root;
        void *remove = node->data;
        
        t2u_delete_connected_session(remove);
    }

    /* remove sessions */
    while (rule->connecting_sessions_->root)
    {
        rbtree_node *node = rule->connecting_sessions_->root;
        void *remove = node->data;
        
        t2u_delete_connecting_session(remove);
    }
    
    /* remove the tree */
    free(rule->sessions_);
    rule->sessions_ = NULL;

    free(rule->connecting_sessions_);
    rule->connecting_sessions_ = NULL;

    /* remove from context */
    rbtree_remove(context->rules_, rule->service_);

    LOG_(1, "delete the rule %p, name: %s from context: %p", 
        rule, rule->service_, context);

    free(rule->service_);
    free(rule);
}


void t2u_delete_rule(t2u_rule *rule)
{
    control_data cdata;
    memset(&cdata, 0, sizeof(cdata));

    cdata.func_ = delete_rule_cb_;
    cdata.arg_ = rule;
    t2u_runner_control(rule->context_->runner_, &cdata);
    return;
}


