#ifndef __t2u_context_h__
#define __t2u_context_h__

typedef struct t2u_context_
{
    sock_t sock_;
    void *runner_;
    rbtree *rule_tree_;
    unsigned long utimeout_;        /* timeout for message */
    unsigned long uretries_;        /* retries for message */
    int udp_slide_window_;          /* slide window for udp packets */
} t2u_context;

/* init */
t2u_context * t2u_context_new(sock_t sock);

/* context free */
void t2u_context_delete(t2u_context *context);

/* add rule to context */
void t2u_context_add_rule(t2u_context *context, t2u_rule *rule);

/* del rule from context */
void t2u_context_delete_rule(t2u_context *context, t2u_rule *rule);

/* find rule by service name */
t2u_rule *t2u_context_find_rule(t2u_context *context, char *service);

#endif /* __t2u_context_h__ */