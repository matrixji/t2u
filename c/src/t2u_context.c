#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <event2/event.h>

#include "t2u.h"
#include "t2u_internal.h"
#include "t2u_thread.h"
#include "t2u_rbtree.h"
#include "t2u_session.h"
#include "t2u_rule.h"
#include "t2u_context.h"

#include "t2u_runner.h"

static int compare_name(void *a, void *b)
{
    return strcmp((char *)a, (char *)b);
}

/* init */
t2u_context * t2u_context_new(sock_t sock)
{
    t2u_context *context = (t2u_context *) malloc(sizeof(t2u_context));
    assert(context != NULL);
    memset(context, 0, sizeof(t2u_context));

    context->rule_tree_ = rbtree_init(compare_name);
    context->sock_ = sock;
    context->utimeout_ = 500;
    context->uretries_ = 3;
    
    LOG_(0, "create new context %p with sock %d", (void *)context, (int)sock);
    return context;
}

/* context free */
static void context_delete_cb_(void *arg)
{
    t2u_context *context = (t2u_context *)arg;
    /* remove rules with this context */
    while (context->rule_tree_->root)
    {
        rbtree_node *node = context->rule_tree_->root;
        void *remove = node->data;

        rbtree_remove(context->rule_tree_, node->key);
        del_forward_rule(remove);
    }

    /* remove the tree */
    free(context->rule_tree_);

    LOG_(0, "delete the context %p with sock %d", (void *)context, (int)context->sock_);
    free(context);
    return;
}

/* context free */
void t2u_context_delete(t2u_context *context)
{
    control_data cdata;
    memset(&cdata, 0, sizeof(cdata));

    cdata.func_ = context_delete_cb_;
    cdata.arg_ = context;
    t2u_runner_control(context->runner_, &cdata);
    return;
}

struct context_and_rule_
{
    t2u_context *context;
    t2u_rule *rule;
};

/* add rule to context */
static void context_add_rule_cb(void *arg)
{
    struct context_and_rule_ *cnr = (struct context_and_rule_ *)arg;
    t2u_context *context = cnr->context;
    t2u_rule *rule = cnr->rule;

    rule->context_ = context;
    rbtree_insert(context->rule_tree_, rule->service_, rule);

}
void t2u_context_add_rule(t2u_context *context, t2u_rule *rule)
{
    control_data cdata;
    memset(&cdata, 0, sizeof(cdata));

    struct context_and_rule_ cnr;
    cnr.context = context;
    cnr.rule = rule;

    cdata.func_ = context_add_rule_cb;
    cdata.arg_ = &cnr;

    t2u_runner_control(context->runner_, &cdata);
    return;
}

/* del rule from context */
static void context_delete_rule_cb(void *arg)
{
    struct context_and_rule_ *cnr = (struct context_and_rule_ *)arg;
    t2u_context *context = cnr->context;
    t2u_rule *rule = cnr->rule;

    rbtree_remove(context->rule_tree_, rule->service_);
}

void t2u_context_delete_rule(t2u_context *context, t2u_rule *rule)
{
    control_data cdata;
    memset(&cdata, 0, sizeof(cdata));

    struct context_and_rule_ cnr;
    cnr.context = context;
    cnr.rule = rule;

    cdata.func_ = context_delete_rule_cb;
    cdata.arg_ = &cnr;

    t2u_runner_control(context->runner_, &cdata);
    return;
}


t2u_rule *t2u_context_find_rule(t2u_context *context, char *service)
{
    return rbtree_lookup(context->rule_tree_, service);
}