#ifndef __t2u_rule_h__
#define __t2u_rule_h__

typedef struct t2u_rule_
{
    forward_mode mode_;             /* mode c/s */
    sock_t listen_sock_;            /* listen socket if in client mode */
    char *service_;                 /* service name */
    void *context_;                 /* the context */
    rbtree *session_tree_;          /* sub sessions */
    struct sockaddr_in conn_addr_;  /* address for connect if server mode */
} t2u_rule;


t2u_rule *t2u_rule_new(void *context,forward_mode  mode, const char *service, const char *addr, unsigned short port);

void t2u_rule_delete(t2u_rule *rule);

/* add rule to context */
void t2u_rule_add_session(t2u_rule *rule, t2u_session *session);

/* del rule from context */
void t2u_rule_delete_session(t2u_rule *rule, t2u_session *session);


#endif /* __t2u_rule_h__ */
