#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <event2/event.h>

#include "t2u.h"
#include "t2u_internal.h"


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

static t2u_mutex_t __g_runner_mutex_;
static int __g_runner_mutex_init_ = 0;

/* create a forward context with the udp socket pair 
 * if using this in STUN mode. you need to STUN it by yourself.
 */
forward_context create_forward(sock_t s)
{
	/* check */
	if (sizeof(t2u_message_data) != 20)
	{
		LOG_(4, "Compiler Error: sizeof(t2u_message_data) != 20");
		return NULL;
	}

	if (!__g_runner_mutex_init_)
	{
		t2u_mutex_init(&__g_runner_mutex_);
		__g_runner_mutex_init_ = 1;
	}

	t2u_mutex_lock(&__g_runner_mutex_);
    if (!g_runner)
    {
		/* new a runner and run it. */
		g_runner = t2u_runner_new();
		assert(NULL != g_runner);				
    }

	forward_context ret = (forward_context) t2u_add_context(g_runner, s);
	t2u_mutex_unlock(&__g_runner_mutex_);

	return ret;

}


/*
 * destroy the context, and it's rules.
 * udp socket will not be closed, you should manage it by youself.
 */
void free_forward(forward_context c)
{
    t2u_context *context = (t2u_context *)c;

    t2u_delete_context(context);

    /* check runner */
    if (g_runner && ((!g_runner->contexts_) || (!g_runner->contexts_->root)))
    {
		t2u_mutex_lock(&__g_runner_mutex_);
        /* the runner is already stopped and no events bind. */
		t2u_delete_runner(g_runner);
		g_runner = NULL;
		t2u_mutex_unlock(&__g_runner_mutex_);
    }
    return;
}


/*
 * forward context option
 */
void set_context_option(forward_context c, int option, unsigned long value)
{
    t2u_context *context = (t2u_context *)c;

    switch (option)
    {
        case CTX_UDP_TIMEOUT:
            {
                if (value < 10)
                {
                    value = 10;
                }
                else if (value > 30000)
                {
                    value = 30000;
                }
                context->utimeout_ = value;
            }
            break;
        case CTX_UDP_RETRIES:
            {
                if (value < 1)
                {
                    value = 1;
                }
                else if (value > 20)
                {
                    value = 20;
                }
                context->uretries_ = value;
            }
            break;
        case CTX_UDP_SLIDEWINDOW:
        {
            if (value < 1)
            {
                value = 1;
            }
            else if (value > 64)
            {
                value = 64;
            }
            context->udp_slide_window_ = value;
        }
            break; 
        case CTX_SESSION_TIMEOUT:
            {
                if (value < 10)
                {
                    value = 10;
                }
                else if (value > 86400)
                {
                    value = 86400;
                }
                context->session_timeout_ = value;
            }
                break;
        default:
            break;
    }
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
    t2u_rule *rule = t2u_add_rule(context, mode, service, addr, port);

    return (forward_rule) rule;
}


/* remove a forward rule */
void del_forward_rule (forward_rule r)
{
    t2u_rule *rule = (t2u_rule *) r;
    t2u_delete_rule(rule);
}

static void debug_dump_cb_(t2u_runner *runner, void *arg)
{
    FILE *fp = (FILE *)arg;
    fprintf(fp, "runner: %p\n", runner);
}

void debug_dump(FILE *fp)
{
    if (g_runner)
    {
        control_data cdata;
        cdata.func_ = debug_dump_cb_;
        cdata.arg_ = fp;

        t2u_runner_control(g_runner, &cdata);
    }
}
