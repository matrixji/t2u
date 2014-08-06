#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <event2/event.h>
#include <event2/util.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#ifdef __GNUC__
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include "t2u.h"
#include "t2u_internal.h"


/* runner proc */
#if defined __GNUC__
    static void* t2u_runner_loop_(void *arg);
#elif defined _MSC_VER
    static DWORD __stdcall t2u_runner_loop_(void * arg);
#else
    #error "Compiler not support."
#endif


/* runner thread proc */
#if defined __GNUC__
    static void* t2u_runner_loop_(void *arg)
#elif defined _MSC_VER
    static DWORD __stdcall t2u_runner_loop_(void * arg)
#endif
{
    t2u_runner *runner = (t2u_runner *)arg;
    runner->tid_ = t2u_thr_self();

    t2u_mutex_lock(&runner->mutex_);
    t2u_cond_signal(&runner->cond_);
    t2u_mutex_unlock(&runner->mutex_);

    LOG_(0, "enter run loop for runner: %p", (void *)runner);
    
    /*  run loop */
    int r = event_base_dispatch(runner->base_);

    LOG_(0, "end run loop for runner: %p, ret: %d", (void *)runner, r);
#if defined __GNUC__
    return NULL;
#elif defined _MSC_VER
    return 0;
#endif
}

static void t2u_runner_control_process(t2u_runner *runner, control_data *cdata)
{
    (void) runner;
    assert (NULL != cdata->func_);
    cdata->func_(runner, cdata->arg_);
}

static void runner_control_cb_(evutil_socket_t sock, short events, void *arg)
{

    t2u_runner *runner = (t2u_runner *)arg;
    control_data cdata;
    size_t len = 0;
    struct sockaddr_in addr_c;
    unsigned int addrlen = sizeof(addr_c);

    (void) events;
    assert(t2u_thr_self() == runner->tid_);

    len = recvfrom(sock, (char *)&cdata, sizeof(cdata), 0, (struct sockaddr *) &addr_c, &addrlen);
    if (len <= 0)
    {
        /* todo: error */
    }

    t2u_runner_control_process(runner, &cdata);

    /* send back message. */
    sendto(sock, (char *)&cdata, sizeof(cdata), 0, (const struct sockaddr *)&addr_c, sizeof(addr_c));

}

void t2u_runner_control(t2u_runner *runner, control_data *cdata)
{
    if (t2u_thr_self() == runner->tid_)
    {
        t2u_runner_control_process(runner, cdata);
    }
    else
    {
        int len; 
        t2u_mutex_lock(&runner->mutex_);
        
        send(runner->sock_[1], (char *) cdata, sizeof(control_data), 0);
        len = recv(runner->sock_[1], (char *) cdata, sizeof(control_data), 0);

        if (len > 0)
        {
        }
        else
        {
            /* todo error handle */
        }
        t2u_mutex_unlock(&runner->mutex_);
    }
}

t2u_event *t2u_event_new()
{
    t2u_event *r = (t2u_event *) malloc(sizeof(t2u_event));
    assert(NULL != r);
    
    memset(r, 0, sizeof(t2u_event));
    return r;
}

void t2u_delete_event(t2u_event *ev)
{
    if (ev)
    {
        if (ev->event_)
        {
            event_free(ev->event_);
            ev->event_ = NULL;
        }

        if (ev->extra_event_)
        {
            event_free(ev->extra_event_);
            ev->extra_event_ = NULL;
        }

        free(ev);
    }
}


#define CONTROL_PORT_START (50505)
#define CONTROL_PORT_END   (50605)
/* runner init */
t2u_runner * t2u_runner_new()
{
    int ret = 0;
    struct sockaddr_in addr_c;
    unsigned short listen_port = 0;

    t2u_runner *runner = (t2u_runner *) malloc(sizeof(t2u_runner));
    assert(runner != NULL);

    /* alloc event base. */
    runner->base_ = event_base_new();
    assert(runner->base_ != NULL);
        
    t2u_mutex_init(&runner->mutex_);
    t2u_cond_init(&runner->cond_);

    runner->running_ = 0; /* not running */
    runner->tid_ = 0;

    /* control message */
    runner->sock_[0] = socket(AF_INET, SOCK_DGRAM, 0);
    assert(runner->sock_[0] > 0);

    for (listen_port = CONTROL_PORT_START; listen_port < CONTROL_PORT_END; listen_port ++)
    {
        addr_c.sin_family = AF_INET;
        addr_c.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr_c.sin_port = htons(listen_port);

        if (bind(runner->sock_[0], (struct sockaddr *)&addr_c, sizeof(addr_c)) == -1)
        {
#if defined _MSC_VER
            LOG_(0, "socket bind failed. %d\n", WSAGetLastError());
#else
            LOG_(0, "socket bind failed. %d\n", errno);
#endif
        }
        else
        {
            LOG_(0, "socket bind ok on port: %u.\n", listen_port);
            break;
        }   
    }
    assert(listen_port != CONTROL_PORT_END);

    /* now connect local control server */
    runner->sock_[1] = socket(AF_INET, SOCK_DGRAM, 0);
    assert(runner->sock_[1] > 0);
    
    ret = connect(runner->sock_[1], (struct sockaddr *)&addr_c, sizeof(addr_c));
    assert(0 == ret);

    /* the event handler for control message processing. */
    runner->control_event_ = event_new(runner->base_, runner->sock_[0], EV_READ | EV_PERSIST, runner_control_cb_, runner);
    assert(NULL != runner->control_event_);

    ret = event_add(runner->control_event_, NULL);
    assert(0 == ret);

	LOG_(0, "create new runner: %p, with control sock: %d", (void *)runner, runner->sock_[0]);

    /* contexts */
    runner->contexts_ = rbtree_init(NULL);

    /* run the runner */
    t2u_mutex_lock(&runner->mutex_);
    runner->running_ = 1;
    t2u_thr_create(&runner->thread_, t2u_runner_loop_, (void *)runner);
    t2u_cond_wait(&runner->cond_, &runner->mutex_);
    t2u_mutex_unlock(&runner->mutex_);

    return runner;
}


static void delete_runner_cb_(t2u_runner *runner, void *arg)
{
    (void) arg;

    while (runner->contexts_->root)
    {
        rbtree_node *node = runner->contexts_->root;
        t2u_context *context = node->data;

        t2u_delete_context(context);    
    }

    free(runner->contexts_);
    runner->contexts_ = NULL;

	/* remove self event */
	if (runner->control_event_)
	{
		event_free(runner->control_event_);
		runner->control_event_ = NULL;
	}
}
    
/* destroy the runner */
void t2u_delete_runner(t2u_runner *runner)
{
    if (!runner)
    {
        return;
    }

    /* makesure stop first */
    if (runner->running_)
    {
        control_data cdata;

        /* stop all event */
        runner->running_ = 0;
        
        cdata.func_ = delete_runner_cb_;
        cdata.arg_ = NULL;

        t2u_runner_control(runner, &cdata);

        t2u_thr_join(runner->thread_);
    }


    /* cleanup */
    closesocket(runner->sock_[0]);
    closesocket(runner->sock_[1]);  

    LOG_(0, "delete the runner: %p", (void *)runner);

    if (runner->base_)
    {
        event_base_free(runner->base_);
        runner->base_ = NULL;
    }

    /* last cleanup */
    free(runner);
}

int t2u_runner_has_context(t2u_runner *runner)
{
    return (runner->contexts_->root != NULL);
}


