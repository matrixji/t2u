#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
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
#include "t2u_thread.h"
#include "t2u_rbtree.h"
#include "t2u_session.h"
#include "t2u_rule.h"
#include "t2u_context.h"
#include "t2u_runner.h"


t2u_rule *t2u_rule_new(void *context, forward_mode mode, const char *service, const char *addr, unsigned short port)
{
    (void) context;
    t2u_rule *rule = (t2u_rule *) malloc(sizeof(t2u_rule));
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
        int reuse = 1;
        setsockopt(rule->listen_sock_, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse));

        /* listen the socket */
        struct sockaddr_in listen_addr;
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
    strcpy(rule->service_, service);

    rule->session_tree_ = rbtree_init(NULL);

    LOG_(1, "create new rule %p, name: %s, addr: %s, port: %u", rule, rule->service_, addr, port);
    return rule;
}

void t2u_rule_delete(t2u_rule *rule)
{
    if (forward_client_mode == rule->mode_)
    {
        closesocket(rule->listen_sock_);
    }

    while (rule->session_tree_->root)
    {
        rbtree_node *node = rule->session_tree_->root;
        void *remove = node->key;
        
        rbtree_remove(rule->session_tree_, node->key);
        del_forward_session(remove);
    }

    LOG_(1, "delete the rule %p, name: %s", rule, rule->service_);

    free(rule->session_tree_);
    free(rule->service_);
    free(rule);
}


/* add rule to context */
void t2u_rule_add_session(t2u_rule *rule, t2u_session *session)
{
    session->rule_ = rule;
    rbtree_insert(rule->session_tree_, session, NULL);
}

/* del rule from context */
void t2u_rule_delete_session(t2u_rule *rule, t2u_session *session)
{
    rbtree_remove(rule->session_tree_, session);
}