#include "t2u.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define FORWARD_MAX (64)

typedef struct internal_list_
{
    struct internal_list_ *prev;
    struct internal_list_ *next;
    void *data;
} internal_list;

typedef struct forward_context_internal_
{
    forward_context *context;
    internal_list *rules;
    t2u_socket udpsocket;
} forward_context_internal;


static internal_list *g_forward_context_list = NULL;
static unsigned int g_forward_context_count = 0;


pthread_mutex_t g_forward_oper_mutex = PTHREAD_MUTEX_INITIALIZER;


#define MAX_EVENTS (1024)
static int g_event_handle = 0;
static int g_events[MAX_EVENTS];

static pthread_t g_event_thread;
static int g_event_loop_run = 0;

static void *event_loop()
{
    while (g_event_loop_run)
    {
        int fds = epoll_wait(g_event_handle, g_events, MAX_EVENTS, 1000);
    }
    return NULL;
}

static void start_event_loop()
{
    if (g_event_handle <= 0)
    {
        g_event_handle = epoll_create(MAX_EVENTS);
    }
    
    g_event_loop_run = 1;
    pthread_create(&g_event_thread, NULL, event_loop, NULL);
}

static void stop_event_loop()
{
    g_event_loop_run = 0;
    pthread_join(g_event_thread, NULL);
}


//
forward_context *init_forward(t2u_socket udpsocket)
{
    pthread_mutex_lock(&g_forward_oper_mutex);
    
    // check duplicate 
    internal_list *curr = g_forward_context_list;
    while (curr != NULL)
    {
        forward_context *context = (forward_context *)curr->data;
        forward_context_internal *internal = context->internal;
        if (internal->udpsocket == udpsocket)
        {
            pthread_mutex_unlock(&g_forward_oper_mutex);   
            return NULL;
        }
        curr = curr->next;
    }
        
    // create new one, then insert.
    forward_context_internal *internal = (forward_context_internal *) malloc(sizeof(forward_context_internal));
    forward_context *context = (forward_context *) malloc(sizeof(forward_context));
    internal_list *node = (internal_list *) malloc(sizeof(internal_list));

    if (NULL == internal || NULL == context || NULL == node) 
    {
        pthread_mutex_unlock(&g_forward_oper_mutex);   
        return NULL;
    }

    context->internal = internal;
    internal->context = context;
    internal->rules = NULL;
    internal->udpsocket = udpsocket;
    
    // append
    if (NULL != g_forward_context_list)
    {
        node->next = g_forward_context_list;
        node->prev = NULL;
        
        g_forward_context_list->prev = node;
        g_forward_context_list = node;
    }
    else
    {
        // this the head
        node->next = NULL;
        node->prev = NULL;
        g_forward_context_list = node;
    }

    node->data = context;
    
    if (0 == g_forward_context_count)
    {
        start_event_loop();
    }
    
    ++g_forward_context_count;
    
    pthread_mutex_unlock(&g_forward_oper_mutex);

    return context;
}

void free_forward(forward_context *context)
{
    pthread_mutex_lock(&g_forward_oper_mutex);
    
    internal_list *curr = g_forward_context_list;
    while (curr != NULL)
    {
        if (context == (forward_context *)curr->data)
        {
            // remove this from list
            if (curr->prev)
            {
                curr->prev->next = curr->next;
            }
            if (curr->next)
            {
                curr->next->prev = curr->prev;
            }
            
            // delete context
            free(curr);
            free(context->internal);
            free(context);
            context->internal = NULL;
            --g_forward_context_count;
            
            if (0 == g_forward_context_count)
            {
                stop_event_loop();
            }
            
            break;
        }
        curr = curr->next;
    }

    pthread_mutex_unlock(&g_forward_oper_mutex);
}

