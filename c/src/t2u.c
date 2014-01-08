#include <stdio.h>
#include <stdlib.h>
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


/* global runner */
static t2u_runner* g_runner = NULL;

/* global log callback */
static void(*log_callback_func_)(int, const char *) = NULL;

/* log callback */
void set_log_callback(void (*cb)(int level, const char *mess))
{
    log_callback_func_ = cb;
}

log_callback get_log_func_()
{
    return log_callback_func_;
}


/* error callback */
static void (*g_error_callback_func_)(forward_context, forward_rule, int , char *) = NULL;

error_callback get_error_func_()
{
    return g_error_callback_func_;
}


/* unknown callback */
static void (*g_unknown_callback_func_)(forward_context, const char *, size_t) = NULL;

unknown_callback get_unknown_func_()
{
    return g_unknown_callback_func_;
}

/* create a forward context with the udp socket pair 
 * if using this in STUN mode. you need to STUN it by yourself.
 */
forward_context create_forward(sock_t s)
{
    t2u_context *context = t2u_context_new(s);

    if (!g_runner)
    {
        /* new a runner and run it. */
        g_runner = t2u_runner_new();
        assert(NULL != g_runner);
    }

    t2u_runner_add_context(g_runner, context);

    return (forward_context) context;
}


/*
 * destroy the context, and it's rules.
 * udp socket will not be closed, you should manage it by youself.
 */
void free_forward(forward_context c)
{
    t2u_context *context = (t2u_context *)c;

    t2u_runner_delete_context(g_runner, context);
    t2u_context_delete(context);

    /* check runner */
    if (g_runner && ((!g_runner->event_tree_) || (!g_runner->event_tree_->root)))
    {
        /* the runner is already stopped and no events bind. */
        t2u_runner_delete(g_runner);
        g_runner = NULL;
    }
    return;
}


/*
 * forward context option
 */
void set_context_option(forward_context c, int option, unsigned long value)
{
    (void)c;
    (void)option;
    (void)value;
}


/*
 * This function is useful for send data with the socket.
 * as socket it managed by the forward-context, if tou need send some data,
 * the only  way is using forward_send.
 */
int forward_send(sock_t s, const char *buffer, size_t length)
{
    (void)s;
    (void)buffer;
    (void)length;

    return -1;
}


/*
 * unknown packets callback.
 * when context recv some unknown packet, it has a chance to forward to your callbakc handler.
 */
void set_unknown_callback(void (*unknown_cb)(forward_context c, const char *buffer, size_t length))
{
    g_unknown_callback_func_ = unknown_cb;
}

/*
 * error callback functions
 * when error ocupy, callback is called
 */
void set_error_callback(void (*error_cb)(forward_context c, forward_rule rule, int error_code, char *error_message))
{
    g_error_callback_func_ = error_cb;
}

/* add a forward rule, return NULL if failed. */
forward_rule add_forward_rule (forward_context c,       /* context */
                               forward_mode mode,       /* mode: client or server */
                               const char *service,     /* server name, for identify the different tcp server on server side */
                               const char *addr,        /* address, listen for client mode, forward address for client mode */
                               unsigned short port)     /* port, listen for client mode, forward port for client mode */
{
    t2u_context *context = (t2u_context *) c;
    t2u_rule *rule = t2u_rule_new((void *)context, mode, service, addr, port);
    
    /* add rule to context and runner */
    t2u_context_add_rule(c, rule);
    t2u_runner_add_rule((t2u_runner *)context->runner_, rule);

    return (forward_rule) rule;
}


/* remove a forward rule */
void del_forward_rule (forward_rule r)
{
    t2u_rule *rule = (t2u_rule *) r;
    t2u_context *context = (t2u_context *)rule->context_;
    t2u_runner *runner = (t2u_runner *) context->runner_;

    /* add rule to context and runner */
    t2u_runner_delete_rule(runner, rule);
    t2u_context_delete_rule(context, rule);

    t2u_rule_delete(rule);
}


void del_forward_session(void *s)
{
    t2u_session *session = (t2u_session *)s;
    t2u_rule *rule = (t2u_rule *) session->rule_;
    t2u_context *context = (t2u_context *)rule->context_;
    t2u_runner *runner = (t2u_runner *) context->runner_;

    /* add rule to context and runner */
    t2u_runner_delete_session(runner, session);
    t2u_rule_delete_session(rule, session);

    t2u_session_delete(session);
}